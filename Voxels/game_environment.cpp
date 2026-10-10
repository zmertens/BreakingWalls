#include "game_environment.h"
#include "game_balls.h"
#include "maze_game_model.h"
#include "sdl_gl_helper.h"

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <glad/gl.h>
#endif

#include <SDL3/SDL.h>
#include <stb/stb_image.h>

#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>

game_environment::~game_environment() noexcept
{
    if (m_cubemap)
        glDeleteTextures(1, &m_cubemap);
    if (m_vao)
        glDeleteVertexArrays(1, &m_vao);
    if (!m_directory.empty())
    {
        // Only remove the six named files and our directory, never unrelated temp files.
        for (const auto name : maze_game_model::ENVIRONMENT_FACE_NAMES)
        {
            std::error_code error;
            std::filesystem::remove(m_directory / (std::string(name) + ".png"), error);
            if (error)
                SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Maze environment cleanup: %s", error.message().c_str());
        }
        std::error_code error;
        std::filesystem::remove(m_directory, error);
        if (error)
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Maze environment directory cleanup: %s", error.message().c_str());
    }
}

void game_environment::init(const maze_game_model &board, const std::uint32_t seed)
{
    if (!m_directory.empty())
        throw std::logic_error("Maze environment is already initialized");
    const auto root = std::filesystem::temp_directory_path();
    std::random_device nonce;
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const auto directory = root / ("breakingwalls_environment_" + std::to_string(nonce()));
        if (std::filesystem::create_directory(directory))
        {
            m_directory = directory;
            break;
        }
    }
    if (m_directory.empty())
        throw std::runtime_error("Maze environment: could not allocate a unique temporary directory");
    m_images = board.write_environment_images(m_directory, seed);

    glActiveTexture(GL_TEXTURE2);
    glGenTextures(1, &m_cubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_cubemap);
    int face_size = 0;
    GLint max_size{};
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &max_size);
    for (std::size_t face = 0; face < m_images.size(); ++face)
    {
        const auto bytes = sdl_gl_helper::load_file_binary(m_images[face].string());
        int width{}, height{}, channels{};
        stbi_set_flip_vertically_on_load(false);
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels{
            stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4),
            stbi_image_free};
        stbi_set_flip_vertically_on_load(true);
        if (!pixels)
            throw std::runtime_error("Maze environment: PNG decode failed for " + m_images[face].string());
        if (width != height || width <= 0 || width > max_size || (face_size != 0 && face_size != width))
            throw std::runtime_error("Maze environment: cubemap faces must have matching square dimensions");
        face_size = width;
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(face), 0, GL_RGBA8,
                     width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.get());
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    if (!m_vao)
        glGenVertexArrays(1, &m_vao);
    if (const auto error = glGetError(); error != GL_NO_ERROR)
        throw std::runtime_error("Maze environment: OpenGL upload failed (" + std::to_string(error) + ")");

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
    SDL_Log("Maze environment: six distance-colored PNGs cached in %s", m_directory.string().c_str());
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
    if (!m_vao)
        glGenVertexArrays(1, &m_vao);
}

void game_environment::draw_balls(const game_balls &balls, const float *projection,
                                  const bool use_environment) const noexcept
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
                                            const float strength) const noexcept
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
