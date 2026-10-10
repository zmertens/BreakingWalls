#include "bloom_pass.h"

#include <SDL3/SDL.h>
#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <glad/gl.h>
#endif

// ---------------------------------------------------------------------------
// render_pass implementation
// ---------------------------------------------------------------------------

bool bloom_pass::components::init(const int w, const int h,
                                  const int color_attachments,
                                  const bool depth, const bool normalized) noexcept
{
    // width and height members removed
    num_color_attachments = color_attachments;

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    GLenum draw_buffers[MAX_COLOR_ATTACHMENTS]{};
    for (int i = 0; i < color_attachments; ++i)
    {
        glGenTextures(1, &color_tex.at(static_cast<std::size_t>(i)));
        glBindTexture(GL_TEXTURE_2D, color_tex.at(static_cast<std::size_t>(i)));
        // GL_RGBA16F allows values > 1.0, which is required for the HDR bloom threshold.
        glTexImage2D(GL_TEXTURE_2D, 0, normalized ? GL_RGBA8 : GL_RGBA16F,
                     w, h, 0, GL_RGBA, normalized ? GL_UNSIGNED_BYTE : GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i),
                               GL_TEXTURE_2D,
                               color_tex.at(static_cast<std::size_t>(i)), 0);
        draw_buffers[i] = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i);
    }
    glDrawBuffers(color_attachments, draw_buffers);

    if (depth)
    {
        glGenRenderbuffers(1, &depth_rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, depth_rbo);
    }

    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return complete;
}

bloom_pass::components::components()
    : fbo(0),
      color_tex{},
      depth_rbo(0),
      num_color_attachments(1)
{
}

bloom_pass::components::~components() noexcept
{
    destroy();
}

void bloom_pass::components::destroy() noexcept
{
    for (int i = 0; i < num_color_attachments; ++i)
    {
        if (color_tex.at(static_cast<std::size_t>(i)))
        {
            glDeleteTextures(1, &color_tex.at(static_cast<std::size_t>(i)));
            color_tex.at(static_cast<std::size_t>(i)) = 0;
        }
    }
    if (depth_rbo)
    {
        glDeleteRenderbuffers(1, &depth_rbo);
        depth_rbo = 0;
    }
    if (fbo)
    {
        glDeleteFramebuffers(1, &fbo);
        fbo = 0;
    }
}

// ---------------------------------------------------------------------------
// bloom_pass implementation
// ---------------------------------------------------------------------------

bloom_pass::~bloom_pass() noexcept
{
    destroy();
}

bool bloom_pass::init(const int width, const int height,
                      const std::uint32_t blur_program,
                      const std::uint32_t composite_program) noexcept
{
    if (width <= 0 || height <= 0 || !blur_program || !composite_program || m_quad_vao)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "bloom_pass: invalid initialization or pass already initialized");
        return false;
    }
    m_blur_program = blur_program;
    m_composite_program = composite_program;

    // Cache uniform locations to avoid per-frame queries.
    m_blur_loc_horizontal = glGetUniformLocation(blur_program, "horizontal");
    m_blur_loc_image = glGetUniformLocation(blur_program, "image");
    m_comp_loc_scene = glGetUniformLocation(composite_program, "scene");
    m_comp_loc_bloom = glGetUniformLocation(composite_program, "bloom_blur");
    m_comp_loc_strength = glGetUniformLocation(composite_program, "bloom_strength");

    if (!create_fbos(width, height))
    {
        destroy_fbos();
        m_width = m_height = 0;
        return false;
    }
    create_screen_quad();

    if (!m_quad_vao || !m_quad_vbo)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "bloom_pass: screen quad initialization failed");
        destroy();
        return false;
    }
    return true;
}

bool bloom_pass::create_fbos(const int w, const int h) noexcept
{
    m_width = w;
    m_height = h;
    bool complete = true;

    // MRT scene pass: attachment 0 = full scene, attachment 1 = bright pixels.
    if (!m_scene_pass.init(w, h, 2, true))
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "bloom_pass: scene MRT FBO incomplete\n");
        complete = false;
    }

    // Two ping-pong blur targets at half resolution: bloom is low-frequency, and this cuts
    // the blur's fill cost by 75%. No depth needed for full-screen passes.
    for (auto &pass : m_blur_pass)
    {
        if (!pass.init((w + 1) / 2, (h + 1) / 2, 1, false))
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "bloom_pass: blur FBO incomplete\n");
            complete = false;
        }
    }
    if (m_depth_enabled)
    {
        if (!m_glass_pass.init(w, h, 2, false, true))
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Glass accumulation framebuffer incomplete");
            return false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, m_glass_pass.fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, m_scene_pass.depth_rbo);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Glass shared-depth framebuffer incomplete");
            complete = false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    return complete;
}

void bloom_pass::create_screen_quad() noexcept
{
    // Triangle strip covering the entire clip space, with UVs [0,1].
    static constexpr float quad_verts[] = {
        -1.f,
        -1.f,
        0.f,
        0.f,
        1.f,
        -1.f,
        1.f,
        0.f,
        -1.f,
        1.f,
        0.f,
        1.f,
        1.f,
        1.f,
        1.f,
        1.f,
    };

    glGenVertexArrays(1, &m_quad_vao);
    glGenBuffers(1, &m_quad_vbo);

    glBindVertexArray(m_quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), quad_verts, GL_STATIC_DRAW);

    // layout(location = 0) vec2 position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<const void *>(0));
    // layout(location = 1) vec2 uv
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<const void *>(2 * sizeof(float)));

    glBindVertexArray(0);
}

void bloom_pass::destroy_fbos() noexcept
{
    m_scene_pass.destroy();
    m_glass_pass.destroy();
    m_transition_pass.destroy();
    m_transition_ready = false;
    for (auto &pass : m_blur_pass)
    {
        pass.destroy();
    }
}

void bloom_pass::bind_scene() const noexcept
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_scene_pass.fbo);
    // Activate both draw buffers so the block shader can write to both.
    // Switched per sub-pass from world::draw() via set_mrt_mode / set_single_mode.
    constexpr GLenum both[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, both);
}

void bloom_pass::set_mrt_mode() const noexcept
{
    constexpr GLenum both[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, both);
}

void bloom_pass::set_single_mode() const noexcept
{
    constexpr GLenum single[] = {GL_COLOR_ATTACHMENT0, GL_NONE};
    glDrawBuffers(2, single);
}

void bloom_pass::set_transition_programs(
    const std::array<std::uint32_t, static_cast<std::size_t>(TransitionEffect::COUNT)> &programs) noexcept
{
    m_transition_programs = programs;
}

void bloom_pass::execute(const float strength, const int blur_iterations, const TransitionEffect effect,
                         const float progress, const float seconds) const noexcept
{
    const auto transition_program = m_transition_programs.at(static_cast<std::size_t>(effect));
    bool transition = progress > 0.0f && progress < 1.0f && transition_program != 0;
    if (transition && !m_transition_ready)
    {
        m_transition_ready = m_transition_pass.init(static_cast<int>(m_width), static_cast<int>(m_height), 1, false, true);
        if (!m_transition_ready)
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "bloom_pass: transition target unavailable");
            m_transition_pass.destroy();
            transition = false;
        }
    }
    GLint output_viewport[4];
    glGetIntegerv(GL_VIEWPORT, output_viewport);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // When strength is 0 (bloom disabled) skip the blur passes entirely.
    GLuint src_tex = m_scene_pass.color_tex.at(1);

    if (strength > 0.0f)
    {
        // --- Gaussian blur on the bright attachment (ping-pong) ---
        glUseProgram(m_blur_program);
        glUniform1i(m_blur_loc_image, 0);

        bool horizontal = true;
        glViewport(0, 0, static_cast<GLsizei>((m_width + 1) / 2), static_cast<GLsizei>((m_height + 1) / 2));

        for (int i = 0; i < blur_iterations * 2; ++i)
        {
            const auto dst_idx = static_cast<std::size_t>(horizontal ? 0 : 1);
            glBindFramebuffer(GL_FRAMEBUFFER, m_blur_pass.at(dst_idx).fbo);
            glClear(GL_COLOR_BUFFER_BIT);

            glUniform1i(m_blur_loc_horizontal, static_cast<GLint>(horizontal));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, src_tex);

            glBindVertexArray(m_quad_vao);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            src_tex = m_blur_pass.at(dst_idx).color_tex.at(0);
            horizontal = !horizontal;
        }
        // src_tex now holds the final blurred bright texture.
    }

    // --- Composite: scene + blurred bloom → default framebuffer ---
    if (transition)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_transition_pass.fbo);
        glViewport(0, 0, static_cast<GLsizei>(m_width), static_cast<GLsizei>(m_height));
    }
    else
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(output_viewport[0], output_viewport[1], output_viewport[2], output_viewport[3]);
    }

    glUseProgram(m_composite_program);
    glUniform1i(m_comp_loc_scene, 0);
    glUniform1i(m_comp_loc_bloom, 1);
    glUniform1f(m_comp_loc_strength, strength);

    // Full scene render
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_scene_pass.color_tex.at(0));
    glActiveTexture(GL_TEXTURE1);
    // blurred bright pass
    glBindTexture(GL_TEXTURE_2D, src_tex);

    glBindVertexArray(m_quad_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (transition)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(output_viewport[0], output_viewport[1], output_viewport[2], output_viewport[3]);
        glUseProgram(transition_program);
        glUniform1i(glGetUniformLocation(transition_program, "scene"), 0);
        // Ease out so the effect resolves smoothly into the sharp frame.
        const float remaining = 1.0f - progress;
        glUniform1f(glGetUniformLocation(transition_program, "intensity"), remaining * remaining);
        glUniform1f(glGetUniformLocation(transition_program, "phase"), seconds);
        glUniform2f(glGetUniformLocation(transition_program, "texel"),
                    1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_transition_pass.color_tex.at(0));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glBindVertexArray(0);

    // Restore GL state for the next frame's geometry pass.
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

bool bloom_pass::resize(const int new_width, const int new_height) noexcept
{
    if (new_width <= 0 || new_height <= 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "bloom_pass: invalid framebuffer dimensions %d x %d",
                     new_width, new_height);
        return false;
    }
    if (m_width == static_cast<std::uint32_t>(new_width) &&
        m_height == static_cast<std::uint32_t>(new_height))
        return true;
    destroy_fbos();
    if (!create_fbos(new_width, new_height))
    {
        destroy_fbos();
        m_width = m_height = 0;
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "bloom_pass: framebuffer resize failed");
        return false;
    }
    return true;
}

void bloom_pass::destroy() noexcept
{
    destroy_fbos();
    if (m_quad_vao)
    {
        glDeleteVertexArrays(1, &m_quad_vao);
        m_quad_vao = 0;
    }
    if (m_quad_vbo)
    {
        glDeleteBuffers(1, &m_quad_vbo);
        m_quad_vbo = 0;
    }
    m_width = m_height = 0;
}

void bloom_pass::init_depth_effects()
{
    if (m_depth_enabled)
        return;
#if defined(__EMSCRIPTEN__)
    constexpr auto quad = "Shaders/es/bloom_quad_vertex.es.glsl";
    constexpr auto field = "Shaders/es/depth_field_fragment.es.glsl";
    constexpr auto resolve = "Shaders/es/glass_resolve_fragment.es.glsl";
#else
    constexpr auto quad = "Shaders/bloom_quad_vertex.glsl";
    constexpr auto field = "Shaders/depth_field_fragment.glsl";
    constexpr auto resolve = "Shaders/glass_resolve_fragment.glsl";
#endif
    m_depth_field.load_program(quad, field);
    m_glass_resolve.load_program(quad, resolve);
    m_field_time = m_depth_field.uniform("seconds");
    m_field_level = m_depth_field.uniform("level");
    m_field_lights = m_depth_field.uniform("lights");
    m_resolve_accumulation = m_glass_resolve.uniform("accumulation");
    m_resolve_optical_depth = m_glass_resolve.uniform("optical_depth");
    m_depth_enabled = true;
    destroy_fbos();
    if (!create_fbos(m_width, m_height))
        throw std::runtime_error("Glass framebuffer initialization failed");
}

void bloom_pass::draw_depth_field(const float seconds, const float level, const float lights) const noexcept
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(m_depth_field.get());
    glUniform1f(m_field_time, seconds);
    glUniform1f(m_field_level, level);
    glUniform1f(m_field_lights, lights);
    glBindVertexArray(m_quad_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

void bloom_pass::draw_quad() const noexcept
{
    glBindVertexArray(m_quad_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void bloom_pass::bind_glass() const noexcept
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_glass_pass.fbo);
    constexpr GLenum targets[]{GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, targets);
    constexpr float clear[]{0, 0, 0, 0};
    glClearBufferfv(GL_COLOR, 0, clear);
    glClearBufferfv(GL_COLOR, 1, clear);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
}

void bloom_pass::resolve_glass() const noexcept
{
    bind_scene();
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_glass_resolve.get());
    glUniform1i(m_resolve_accumulation, 0);
    glUniform1i(m_resolve_optical_depth, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_glass_pass.color_tex[0]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_glass_pass.color_tex[1]);
    glBindVertexArray(m_quad_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}
