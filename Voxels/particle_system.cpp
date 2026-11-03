#include "particle_system.h"

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <glad/gl.h>
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace
{
#if defined(__EMSCRIPTEN__)
    constexpr auto GLSL_HEADER = "#version 300 es\nprecision highp float;\n";
#else
    constexpr auto GLSL_HEADER = "#version 330 core\n";
#endif

    constexpr auto ATTRIBUTES = R"(
layout(location = 0) in vec3 in_pos;
layout(location = 1) in float in_life;
layout(location = 2) in vec3 in_vel;
layout(location = 3) in float in_max;
layout(location = 4) in vec3 in_color;
layout(location = 5) in float in_kind;
)";

    constexpr auto UPDATE_VERTEX = R"(
out vec3 out_pos;
out float out_life;
out vec3 out_vel;
out float out_max;
out vec3 out_color;
out float out_kind;
uniform float dt;

void main() {
    out_color = in_color;
    out_kind = in_kind;
    out_max = in_max;
    if (in_life <= 0.0) {
        out_pos = in_pos;
        out_vel = vec3(0.0);
        out_life = 0.0;
        return;
    }
    vec3 v = in_vel;
    if (in_kind < 0.5) {            // fire: buoyant, drifts sideways less over time
        v.y += 4.0 * dt;
        v.xz *= max(0.0, 1.0 - 1.5 * dt);
    } else if (in_kind < 1.5) {     // trail: floats down slowly
        v.y -= 1.0 * dt;
        v *= max(0.0, 1.0 - 2.0 * dt);
    } else {                        // spark: ballistic
        v.y -= 9.0 * dt;
    }
    out_pos = in_pos + v * dt;
    out_vel = v;
    out_life = in_life - dt;
}
)";

    constexpr auto UPDATE_FRAGMENT = R"(
layout(location = 0) out vec4 frag_color;
void main() { frag_color = vec4(0.0); }
)";

    constexpr auto DRAW_VERTEX = R"(
uniform mat4 matrix;
uniform float scale;
uniform float perspective;
out vec4 v_color;

void main() {
    if (in_life <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 0.0;
        v_color = vec4(0.0);
        return;
    }
    float ratio = clamp(in_life / max(in_max, 0.001), 0.0, 1.0);
    vec4 clip = matrix * vec4(in_pos, 1.0);
    gl_Position = clip;
    float base = in_kind < 0.5 ? 14.0 : (in_kind < 1.5 ? 6.0 : 8.0);
    float s = base * (in_kind < 0.5 ? (0.4 + ratio) : 1.0);
    gl_PointSize = perspective > 0.5 ? s * scale * 8.0 / max(clip.w, 1.0) : s * scale;
    vec3 col = in_kind < 0.5 ? mix(vec3(0.7, 0.05, 0.0), in_color, ratio) : in_color;
    v_color = vec4(col, ratio);
}
)";

    constexpr auto DRAW_FRAGMENT = R"(
in vec4 v_color;
layout(location = 0) out vec4 frag_color;
layout(location = 1) out vec4 bright_color;

void main() {
    vec2 d = gl_PointCoord - 0.5;
    float r = dot(d, d);
    if (r > 0.25) discard;
    float a = v_color.a * (1.0 - r * 4.0);
    frag_color = vec4(v_color.rgb, a);
    bright_color = vec4(v_color.rgb * 0.6, a);
}
)";

    GLuint compile(const GLenum type, const std::string &body)
    {
        const std::string source = std::string{GLSL_HEADER} + body;
        const GLuint shader = glCreateShader(type);
        const char *text = source.c_str();
        glShaderSource(shader, 1, &text, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[1024]{};
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "particle shader: %s", log);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    GLuint link(const GLuint vertex, const GLuint fragment, const bool feedback)
    {
        if (!vertex || !fragment)
        {
            return 0;
        }
        const GLuint program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        if (feedback)
        {
            static const char *varyings[] = {"out_pos", "out_life", "out_vel", "out_max", "out_color", "out_kind"};
            glTransformFeedbackVaryings(program, 6, varyings, GL_INTERLEAVED_ATTRIBS);
        }
        glLinkProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char log[1024]{};
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "particle program: %s", log);
            glDeleteProgram(program);
            return 0;
        }
        return program;
    }

    void bind_layout(const GLuint vao, const GLuint vbo, const int floats)
    {
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        const auto stride = static_cast<GLsizei>(floats * sizeof(float));
        auto attribute = [&](const GLuint index, const GLint size, const int offset)
        {
            glEnableVertexAttribArray(index);
            glVertexAttribPointer(index, size, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<const void *>(static_cast<std::uintptr_t>(offset) * sizeof(float)));
        };
        attribute(0, 3, 0);
        attribute(1, 1, 3);
        attribute(2, 3, 4);
        attribute(3, 1, 7);
        attribute(4, 3, 8);
        attribute(5, 1, 11);
    }
} // namespace

particle_system::~particle_system() { destroy(); }

bool particle_system::init() noexcept
{
    if (m_ready)
    {
        return true;
    }
    try
    {
        const std::string attributes{ATTRIBUTES};
        m_update_program = link(compile(GL_VERTEX_SHADER, attributes + UPDATE_VERTEX),
                                compile(GL_FRAGMENT_SHADER, UPDATE_FRAGMENT), true);
        m_draw_program = link(compile(GL_VERTEX_SHADER, attributes + DRAW_VERTEX),
                              compile(GL_FRAGMENT_SHADER, DRAW_FRAGMENT), false);
    }
    catch (...)
    {
        return false;
    }
    if (!m_update_program || !m_draw_program)
    {
        destroy();
        return false;
    }
    m_u_dt = glGetUniformLocation(m_update_program, "dt");
    m_u_matrix = glGetUniformLocation(m_draw_program, "matrix");
    m_u_scale = glGetUniformLocation(m_draw_program, "scale");
    m_u_perspective = glGetUniformLocation(m_draw_program, "perspective");

    GLint previous_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vao);
    glGenVertexArrays(2, m_vao.data());
    glGenBuffers(2, m_vbo.data());
    const std::vector<float> zeros(static_cast<std::size_t>(CAPACITY) * FLOATS, 0.0f);
    for (int i = 0; i < 2; ++i)
    {
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo[i]);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(zeros.size() * sizeof(float)), zeros.data(),
                     GL_DYNAMIC_COPY);
        bind_layout(m_vao[i], m_vbo[i], FLOATS);
    }
    glBindVertexArray(static_cast<GLuint>(previous_vao));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_ready = true;
    return true;
}

void particle_system::destroy() noexcept
{
    if (m_update_program)
        glDeleteProgram(m_update_program);
    if (m_draw_program)
        glDeleteProgram(m_draw_program);
    if (m_vbo[0])
        glDeleteBuffers(2, m_vbo.data());
    if (m_vao[0])
        glDeleteVertexArrays(2, m_vao.data());
    m_update_program = m_draw_program = 0;
    m_vbo = {};
    m_vao = {};
    m_ready = false;
}

float particle_system::random_unit() noexcept
{
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return static_cast<float>(m_rng & 0xFFFFFF) / static_cast<float>(0xFFFFFF) * 2.0f - 1.0f;
}

void particle_system::push(const float x, const float y, const float z, const float vx, const float vy,
                           const float vz, const float life, const float r, const float g, const float b,
                           const float k)
{
    if (pending() >= static_cast<std::size_t>(CAPACITY))
    {
        return;
    }
    const float data[FLOATS] = {x, y, z, life, vx, vy, vz, life, r, g, b, k};
    m_pending.insert(m_pending.end(), data, data + FLOATS);
}

void particle_system::burst(const float x, const float y, const float z, const int count, const kind k,
                            const float spread)
{
    switch (k)
    {
    case kind::FIRE:
        burst_colored(x, y, z, count, k, 1.0f, 0.75f, 0.1f, spread);
        break;
    case kind::TRAIL:
        burst_colored(x, y, z, count, k, 0.4f, 0.9f, 1.0f, spread);
        break;
    case kind::SPARK:
        burst_colored(x, y, z, count, k, 1.0f, 0.9f, 0.3f, spread);
        break;
    }
}

void particle_system::burst_colored(const float x, const float y, const float z, const int count, const kind k,
                                    const float r, const float g, const float b, const float spread)
{
    for (int i = 0; i < count; ++i)
    {
        const float a = random_unit(), c = random_unit(), d = random_unit();
        switch (k)
        {
        case kind::FIRE:
            push(x + a * 0.3f * spread, y + c * 0.3f * spread, z + d * 0.3f * spread, a * 1.5f * spread,
                 1.0f + (c + 1.0f), d * 1.5f * spread, 0.55f + (random_unit() + 1.0f) * 0.3f, r, g, b, 0.0f);
            break;
        case kind::TRAIL:
            push(x + a * 0.2f, y + c * 0.1f, z + d * 0.2f, a * 0.4f, 0.3f + c * 0.2f, d * 0.4f,
                 0.3f + (random_unit() + 1.0f) * 0.15f, r, g, b, 1.0f);
            break;
        case kind::SPARK:
            push(x, y, z, a * 3.0f * spread, 3.0f + (c + 1.0f) * 2.0f, d * 3.0f * spread,
                 0.9f + (random_unit() + 1.0f) * 0.4f, r * (0.6f + 0.4f * (a + 1.0f)), g * (0.6f + 0.4f * (c + 1.0f)),
                 b * (0.6f + 0.4f * (d + 1.0f)), 2.0f);
            break;
        }
    }
}

void particle_system::update(const float dt) noexcept
{
    if (!m_ready)
    {
        return;
    }
    GLint previous_vao = 0, previous_program = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vao);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);

    if (!m_pending.empty())
    {
        const int count = static_cast<int>(m_pending.size()) / FLOATS;
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo[m_current]);
        int done = 0;
        while (done < count)
        {
            const int chunk = std::min(count - done, CAPACITY - m_ring);
            glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(m_ring) * FLOATS * sizeof(float),
                            static_cast<GLsizeiptr>(chunk) * FLOATS * sizeof(float),
                            m_pending.data() + static_cast<std::size_t>(done) * FLOATS);
            done += chunk;
            m_ring = (m_ring + chunk) % CAPACITY;
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        m_pending.clear();
    }

    glUseProgram(m_update_program);
    glUniform1f(m_u_dt, std::clamp(dt, 0.0f, 0.1f));
    glEnable(GL_RASTERIZER_DISCARD);
    glBindVertexArray(m_vao[m_current]);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, m_vbo[1 - m_current]);
    glBeginTransformFeedback(GL_POINTS);
    glDrawArrays(GL_POINTS, 0, CAPACITY);
    glEndTransformFeedback();
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
    glDisable(GL_RASTERIZER_DISCARD);
    m_current = 1 - m_current;

    glBindVertexArray(static_cast<GLuint>(previous_vao));
    glUseProgram(static_cast<GLuint>(previous_program));
}

void particle_system::draw(const float matrix[16], const float viewport_height, const bool perspective) const noexcept
{
    if (!m_ready)
    {
        return;
    }
    GLint previous_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vao);
    const GLboolean blend_was_on = glIsEnabled(GL_BLEND);
    GLboolean depth_mask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);

    glUseProgram(m_draw_program);
    glUniformMatrix4fv(m_u_matrix, 1, GL_FALSE, matrix);
    glUniform1f(m_u_scale, viewport_height / 1080.0f);
    glUniform1f(m_u_perspective, perspective ? 1.0f : 0.0f);
#ifndef __EMSCRIPTEN__
    glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
#endif
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    glBindVertexArray(m_vao[m_current]);
    glDrawArrays(GL_POINTS, 0, CAPACITY);
    glDepthMask(depth_mask);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (!blend_was_on)
    {
        glDisable(GL_BLEND);
    }
    glBindVertexArray(static_cast<GLuint>(previous_vao));
}
