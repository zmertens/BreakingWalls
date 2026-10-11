#include "game_environment.h"
#include "game_balls.h"

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <glad/gl.h>
#endif

#include <SDL3/SDL.h>
#include <noise/noise.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr int FACE_SIZE = 128;

// Direction through texel (x, y) of a cubemap face, following the GL face orientation rules.
std::array<float, 3> face_direction(const std::size_t face, const int x, const int y)
{
    const float sc = 2.0f * (static_cast<float>(x) + 0.5f) / FACE_SIZE - 1.0f;
    const float tc = 2.0f * (static_cast<float>(y) + 0.5f) / FACE_SIZE - 1.0f;
    float d[3];
    switch (face)
    {
    case 0: d[0] = 1; d[1] = -tc; d[2] = -sc; break;
    case 1: d[0] = -1; d[1] = -tc; d[2] = sc; break;
    case 2: d[0] = sc; d[1] = 1; d[2] = tc; break;
    case 3: d[0] = sc; d[1] = -1; d[2] = -tc; break;
    case 4: d[0] = sc; d[1] = -tc; d[2] = 1; break;
    default: d[0] = -sc; d[1] = -tc; d[2] = -1; break;
    }
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    return {d[0] / length, d[1] / length, d[2] / length};
}

// Sampling by direction keeps noise continuous across face edges. The seed offsets the lookup so the
// global noise permutation, which terrain generation owns, is never reseeded here.
std::vector<std::uint8_t> make_face(const std::size_t face, const std::uint32_t seed)
{
    const float ox = static_cast<float>(seed & 0xFFFu) * 0.37f, oy = static_cast<float>((seed >> 12) & 0xFFFu) * 0.29f,
                oz = static_cast<float>((seed >> 20) & 0xFFFu) * 0.31f;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(FACE_SIZE) * FACE_SIZE * 4);
    for (int y = 0; y < FACE_SIZE; ++y)
        for (int x = 0; x < FACE_SIZE; ++x)
        {
            const auto [dx, dy, dz] = face_direction(face, x, y);
            const float px = dx * 2.2f + ox, py = dy * 2.2f + oy, pz = dz * 2.2f + oz;
            // Domain-warped fractal noise gives billowy cloud edges.
            const float warp = simplex3(px * 0.7f + 11.0f, py * 0.7f, pz * 0.7f, 2, 0.5f, 2.0f);
            const float cloud = simplex3(px + warp * 0.8f, py, pz, 5, 0.5f, 2.0f) * 0.5f + 0.5f;
            const float density = std::clamp((cloud - 0.42f) * 2.4f, 0.0f, 1.0f);
            const float tint = simplex3(px * 0.35f, py * 0.35f + 31.0f, pz * 0.35f, 2, 0.5f, 2.0f) * 0.5f + 0.5f;
            // Dark violet sky, with clouds lit from cyan toward magenta.
            const float sky[3]{0.03f, 0.04f, 0.12f};
            const float lit[3]{0.30f + 0.55f * tint, 0.50f - 0.20f * tint, 0.95f};
            const auto out = static_cast<std::size_t>(y * FACE_SIZE + x) * 4;
            for (int c = 0; c < 3; ++c)
                pixels[out + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                    std::clamp(sky[c] + (lit[c] - sky[c]) * density, 0.0f, 1.0f) * 255.0f);
            pixels[out + 3] = 255;
        }
    return pixels;
}
} // namespace

game_environment::~game_environment() noexcept
{
    if (m_cubemap)
        glDeleteTextures(1, &m_cubemap);
    if (m_vao)
        glDeleteVertexArrays(1, &m_vao);
}

void game_environment::init(const std::uint32_t seed)
{
    if (m_cubemap)
        throw std::logic_error("Game environment is already initialized");
    glActiveTexture(GL_TEXTURE2);
    glGenTextures(1, &m_cubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_cubemap);
    for (std::size_t face = 0; face < 6; ++face)
    {
        const auto pixels = make_face(face, seed);
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(face), 0, GL_RGBA8,
                     FACE_SIZE, FACE_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    if (!m_vao)
        glGenVertexArrays(1, &m_vao);
    if (const auto error = glGetError(); error != GL_NO_ERROR)
        throw std::runtime_error("Game environment: OpenGL upload failed (" + std::to_string(error) + ")");

#if defined(__EMSCRIPTEN__)
    m_background.load_program("Shaders/es/game_environment_vertex.es.glsl",
                              "Shaders/es/game_environment_fragment.es.glsl");
#else
    m_background.load_program("Shaders/game_environment_vertex.glsl",
                              "Shaders/game_environment_fragment.glsl");
#endif
    m_sampler = m_background.uniform("environment_map");
    m_rotation = m_background.uniform("rotation");
    m_zoom = m_background.uniform("zoom");
    m_pan = m_background.uniform("pan");
    m_viewport_scale = m_background.uniform("viewport_scale");
    m_animation = m_background.uniform("animation");
    m_ready = true;
    SDL_Log("Game environment: procedural noise cubemap ready");
}

void game_environment::bind() const noexcept
{
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_cubemap);
}

void game_environment::init_balls()
{
#if defined(__EMSCRIPTEN__)
    m_spheres.load_program("Shaders/es/game_ball_vertex.es.glsl", "Shaders/es/game_ball_fragment.es.glsl");
#else
    m_spheres.load_program("Shaders/game_ball_vertex.glsl", "Shaders/game_ball_fragment.glsl");
#endif
    m_ball_matrix = m_spheres.uniform("matrix");
    m_ball_count = m_spheres.uniform("ball_count");
    m_ball_index = m_spheres.uniform("ball_index");
    m_ball_spheres = m_spheres.uniform("spheres[0]");
    m_ball_surfaces = m_spheres.uniform("surfaces[0]");
    m_ball_environment = m_spheres.uniform("environment_map");
    m_ball_environment_enabled = m_spheres.uniform("environment_enabled");
    m_ball_mirrored = m_spheres.uniform("mirrored");
    m_ball_seconds = m_spheres.uniform("seconds");
    if (!m_vao)
        glGenVertexArrays(1, &m_vao);
}

void game_environment::draw_balls(const game_balls &balls, const float *projection,
                                  const bool use_environment, const float seconds) const noexcept
{
    std::array<float, game_balls::MAX_BALLS * 4> spheres{}, surfaces{};
    const auto instances = balls.balls();
    if (instances.empty())
        return;
    balls.pack(spheres, surfaces);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(m_spheres.get());
    bind();
    glUniform1i(m_ball_environment, 2);
    glUniform1i(m_ball_environment_enabled, use_environment && m_ready);
    glUniform1i(m_ball_mirrored, 0);
    glUniform1f(m_ball_seconds, seconds);
    const auto count = static_cast<GLsizei>(instances.size());
    glUniform1i(m_ball_count, count);
    glUniform4fv(m_ball_spheres, count, spheres.data());
    glUniform4fv(m_ball_surfaces, count, surfaces.data());
    glUniformMatrix4fv(m_ball_matrix, 1, GL_FALSE, projection);
    glBindVertexArray(m_vao);
    for (int i = 0; i < count; ++i)
    {
        glUniform1i(m_ball_index, i);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
}

void game_environment::draw_reflected_balls(const game_balls &balls, const float *projection,
                                            const bool use_environment, const float plane_y,
                                            const float strength, const float seconds) const noexcept
{
    std::array<float, game_balls::MAX_BALLS * 4> spheres{}, surfaces{};
    const auto count = static_cast<GLsizei>(balls.pack(spheres, surfaces));
    if (count == 0)
        return;
    for (GLsizei i = 0; i < count; ++i)
        spheres[static_cast<std::size_t>(i) * 4 + 1] = 2.0f * plane_y - spheres[static_cast<std::size_t>(i) * 4 + 1];
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilMask(0x00);
    // Reflections sit below the floor's depth, so the stencil alone confines them to the floor.
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendColor(0.0f, 0.0f, 0.0f, strength);
    glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
    glUseProgram(m_spheres.get());
    bind();
    glUniform1i(m_ball_environment, 2);
    glUniform1i(m_ball_environment_enabled, use_environment && m_ready);
    glUniform1i(m_ball_mirrored, 1);
    glUniform1f(m_ball_seconds, seconds);
    glUniform1i(m_ball_count, count);
    glUniform4fv(m_ball_spheres, count, spheres.data());
    glUniform4fv(m_ball_surfaces, count, surfaces.data());
    glUniformMatrix4fv(m_ball_matrix, 1, GL_FALSE, projection);
    glBindVertexArray(m_vao);
    for (int i = 0; i < count; ++i)
    {
        glUniform1i(m_ball_index, i);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glBindVertexArray(0);
    glUniform1i(m_ball_mirrored, 0);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

void game_environment::draw(const float rotation, const float zoom,
                            const float pan_x, const float pan_y,
                            const float scale_x, const float scale_y,
                            const float seconds, const float level,
                            const float lights, const float board_radius) const noexcept
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(m_background.get());
    bind();
    glUniform1i(m_sampler, 2);
    glUniform1f(m_rotation, rotation);
    glUniform1f(m_zoom, zoom);
    glUniform2f(m_pan, pan_x, pan_y);
    glUniform2f(m_viewport_scale, scale_x, scale_y);
    glUniform4f(m_animation, seconds, level, lights, board_radius);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}
