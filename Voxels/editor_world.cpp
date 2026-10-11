#include "editor_world.h"

#include <MazeBuilder/algos.h>
#include <MazeBuilder/configurator.h>
#include <MazeBuilder/randomizer.h>
#include <MazeBuilder/runtime_app.h>
#include <MazeBuilder/topology.h>

#include "item.h"
#include "matrix.h"
#include "texture.h"
#include "resource_identifiers.h"
#include "resource_manager.h"
#include "sdl_gl_helper.h"
#include "shader.h"

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <glad/gl.h>
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <ranges>

namespace
{
    constexpr float FORCE_DUE_TO_GRAVITY = -9.8f;
}

static std::string gl_error_checker(const char *file, const int line) noexcept
{
    GLenum error_code;
    std::string error_str;
    while ((error_code = glGetError()) != GL_NO_ERROR)
    {
        switch (error_code)
        {
        case GL_INVALID_ENUM:
            error_str += "INVALID_ENUM";
            break;
        case GL_INVALID_VALUE:
            error_str += "INVALID_VALUE";
            break;
        case GL_INVALID_OPERATION:
            error_str += "INVALID_OPERATION";
            break;
        case GL_OUT_OF_MEMORY:
            error_str += "OUT_OF_MEMORY";
            break;
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            error_str += "INVALID_FRAMEBUFFER_OPERATION";
            break;
        default:
            break;
        }
        SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                     "OpenGL ERROR: %s\n\t\tFILE: %s, LINE: %d\n", error_str.c_str(), file, line);
    }
    return error_code == GL_NO_ERROR ? "" : error_str;
}

#define CHECK_GL_ERR() gl_error_checker(__FILE__, __LINE__)

editor_world::ivec3 editor_world::face_normal(const int face) noexcept
{
    switch (face)
    {
    case 0:
        return {-1, 0, 0};
    case 1:
        return {1, 0, 0};
    case 2:
        return {0, 0, -1};
    case 3:
        return {0, 0, 1};
    case 8:
        return {0, -1, 0};
    default:
        return {0, 1, 0};
    }
}

editor_world::ivec3 editor_world::face_u_axis(const int face) noexcept
{
    switch (face)
    {
    case 0:
        return {0, 0, 1};
    case 1:
        return {0, 0, -1};
    case 2:
        return {-1, 0, 0};
    case 3:
        return {1, 0, 0};
    case 4:
        return {1, 0, 0};
    case 5:
        return {0, 0, 1};
    case 6:
        return {-1, 0, 0};
    case 7:
        return {0, 0, -1};
    case 8:
        return {1, 0, 0};
    default:
        return {1, 0, 0};
    }
}

editor_world::ivec3 editor_world::face_v_axis(const int face) noexcept
{
    switch (face)
    {
    case 0:
    case 1:
    case 2:
    case 3:
        return {0, -1, 0};
    case 4:
        return {0, 0, 1};
    case 5:
        return {-1, 0, 0};
    case 6:
        return {0, 0, -1};
    case 7:
        return {1, 0, 0};
    case 8:
        return {0, 0, 1};
    default:
        return {0, 0, 1};
    }
}

editor_world::editor_world(SDL_Window *window, font_manager &fonts, player *p,
                           shader_manager &shaders, texture_manager &textures,
                           const sdl_gl_helper *sdl, bloom_pass *bloom)
    : world{window, fonts, p, shaders, textures, sdl, bloom}
{
    if (active_player)
    {
        active_player->set_world(this);
    }
}

editor_world::~editor_world()
{
    // Workers read player settings and must finish before the player is detached.
    cleanup_worker_threads();
    sdl_gl_helper::del_buffer(world_sky_gl_buffer);
    if (active_player)
    {
        active_player->set_world(nullptr);
        sdl_gl_helper::del_buffer(active_player->get_buffer());
        active_player->set_buffer(0);
    }
}

void editor_world::init() noexcept
{
    auto &&configs{active_player->get_configs()};
    configs.view_mode(player_configs::ViewMode::PERSPECTIVE).ortho_scaling(0);
    active_player->pos.rx = 0.0f;
    active_player->pos.ry = -matrix::to_radians(15.0f);
    active_player->vel = {};
    init_voxel_engine();
    block_emission = glGetUniformLocation(s_block_attrib.program, "bloom_emission");
    block_light_attenuation = glGetUniformLocation(s_block_attrib.program, "light_attenuation");
    block_taper = glGetUniformLocation(s_block_attrib.program, "stack_taper");
    block_center = glGetUniformLocation(s_block_attrib.program, "stack_center");
    init_worker_threads();
    force_chunks(active_player);
    active_player->pos.y = static_cast<float>(highest_block(active_player->pos.x, active_player->pos.z) + 2);

    s_line_attrib.program = world_shaders.get(ShaderIdentifier::LINE_SHADER).get();
    s_line_attrib.position = 0;
    s_line_attrib.matrix = glGetUniformLocation(s_line_attrib.program, "matrix");
    s_line_attrib.extra1 = glGetUniformLocation(s_line_attrib.program, "color");

    s_text_attrib.program = world_shaders.get(ShaderIdentifier::TEXT_SHADER).get();
    s_text_attrib.position = 0;
    s_text_attrib.uv = 1;
    s_text_attrib.matrix = glGetUniformLocation(s_text_attrib.program, "matrix");
    s_text_attrib.sampler = glGetUniformLocation(s_text_attrib.program, "sampler");
    s_text_attrib.extra1 = glGetUniformLocation(s_text_attrib.program, "is_sign");

    s_sky_attrib.program = world_shaders.get(ShaderIdentifier::SKY_SHADER).get();
    s_sky_attrib.position = 0;
    s_sky_attrib.normal = 1;
    s_sky_attrib.uv = 2;
    s_sky_attrib.matrix = glGetUniformLocation(s_sky_attrib.program, "matrix");
    s_sky_attrib.sampler = glGetUniformLocation(s_sky_attrib.program, "sampler");
    s_sky_attrib.timer = glGetUniformLocation(s_sky_attrib.program, "timer");

    world_sky_gl_buffer = sdl_gl_helper::gen_sky_buffer();
}

void editor_world::update(const float delta_time, mazes::randomizer &rng) noexcept
{
    while (!world_commands.empty())
    {
        auto cmd = world_commands.front();
        world_commands.pop();
        cmd.action(*active_player, delta_time, std::ref(rng));
    }

    const float dt_seconds = delta_time / 1000.0f;

    if (!active_player->is_flying())
    {
        active_player->vel.vy += FORCE_DUE_TO_GRAVITY * dt_seconds;
    }
    else
    {
        active_player->vel.vy *= 0.85f;
    }

    constexpr float horizontal_damping = 0.80f;
    active_player->vel.vx *= horizontal_damping;
    active_player->vel.vz *= horizontal_damping;

    active_player->pos.y += active_player->vel.vy * dt_seconds;

    if (!active_player->is_flying())
    {
        if (const int collision_result = collide(2, &active_player->pos.x, &active_player->pos.y, &active_player->pos.z);
            collision_result == 1)
        {
            active_player->vel.vy = 0.0f;
            active_player->set_on_ground(true);
        }
        else
        {
            active_player->set_on_ground(false);
        }
    }
    else
    {
        active_player->set_on_ground(false);
    }

    update_projected_plane();

    delete_chunks();
    sdl_gl_helper::del_buffer(active_player->get_buffer());
    ensure_chunks(active_player);
    update_dirty_chunks_async();
}

void editor_world::draw() const noexcept
{
    CHECK_GL_ERR();

    // Programs are shared resources; game-specific uniforms must not leak into the editor.
    glUseProgram(s_block_attrib.program);
    glUniform1f(block_emission, 0.0f);
    glUniform1f(block_light_attenuation, 0.0f);
    glUniform1f(block_taper, 0.0f);
    glUniform1f(block_center, 0.0f);

    int viewport_width, viewport_height;
    SDL_GetWindowSizeInPixels(simple_direct_medialayer->window, &viewport_width, &viewport_height);
    glViewport(0, 0, viewport_width, viewport_height);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.53f, 0.81f, 0.92f, 1.0f);

    if (!m_bloom->resize(viewport_width, viewport_height))
    {
        return;
    }
    m_bloom->bind_scene();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const auto atlas_texture = world_textures.get(TextureIdentifier::ATLAS).gl_texture;
    const auto signs_texture = world_textures.get(TextureIdentifier::SIGNS).gl_texture;
    const auto sky_texture = world_textures.get(TextureIdentifier::SKY).gl_texture;

    m_bloom->set_single_mode();
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    render_sky(world_sky_gl_buffer, sky_texture);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);

    m_bloom->set_mrt_mode();
    m_rendered_terrain_faces = render_chunks(atlas_texture);
    render_player(atlas_texture);

    m_bloom->set_single_mode();
    render_item(atlas_texture);

    render_signs(signs_texture);
    render_sign(signs_texture);

    render_wireframe();
    render_crosshairs();
    render_hover_info();
    render_ghost();

    const auto &configs{active_player->get_configs()};
    const float bloom_str = configs.use_bloom_effect() ? configs.exposure_val() : 0.0f;
    m_bloom->execute(bloom_str);
}

command_queue &editor_world::get_command_queue() noexcept
{
    return world_commands;
}

void editor_world::handle_event(const SDL_Event &event) noexcept
{
    if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        const int w = event.window.data1;
        const int h = event.window.data2;
        if (w > 0 && h > 0 && m_bloom->resize(w, h))
        {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Resizing viewport to %dx%d\n", w, h);

            glViewport(0, 0, w, h);
        }
    }
}

void editor_world::update_projected_plane() noexcept
{
    int hx, hy, hz, face;
    current_projected_plane.has_valid_target = hit_test_face(&hx, &hy, &hz, &face) != 0;
    if (current_projected_plane.has_valid_target)
    {
        current_projected_plane.target_x = hx;
        current_projected_plane.target_y = hy;
        current_projected_plane.target_z = hz;
        current_projected_plane.target_face = face;
    }
}

bool editor_world::generate_ghost(const mazes::configurator &config) noexcept
{
    const auto app = mazes::runtime_app::instance();
    if (!app)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Maze ghost: MazeBuilder runtime is unavailable\n");
        return false;
    }

    const std::string request = "--rows=" + std::to_string(config.rows()) +
                                " --columns=" + std::to_string(config.columns()) +
                                " --levels=1 --algo=" + std::string{mazes::to_sv_from_algo(config.algo_id())} +
                                " --seed=" + std::to_string(config.seed()) +
                                " --output=stdout";
    const auto result = app->apply(request);
    const auto topo = mazes::topology::parse(std::string{result});
    if (topo.rows == 0u || topo.columns == 0u)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Maze ghost: failed to generate maze layout\n");
        return false;
    }

    // Cells sit on odd coordinates; even coordinates are walls unless carved open.
    const int width = static_cast<int>(topo.columns) * 2 + 1;
    const int height = static_cast<int>(topo.rows) * 2 + 1;
    std::vector<std::uint8_t> walls(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 1u);
    const auto open = [&](const int x, const int y)
    { walls[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = 0u; };

    for (unsigned int row = 0; row < topo.rows; ++row)
    {
        for (unsigned int col = 0; col < topo.columns; ++col)
        {
            const auto *cell = topo.at(row, col);
            if (!cell)
            {
                continue;
            }
            const int x = static_cast<int>(col) * 2 + 1;
            const int y = static_cast<int>(row) * 2 + 1;
            open(x, y);
            if (!cell->east() && col + 1 < topo.columns)
            {
                open(x + 1, y);
            }
            if (!cell->south() && row + 1 < topo.rows)
            {
                open(x, y + 1);
            }
        }
    }

    current_ghost.walls = std::move(walls);
    current_ghost.width = width;
    current_ghost.height = height;
    current_ghost.wall_height = static_cast<int>(config.levels());
    current_ghost.has_data = true;
    return true;
}

void editor_world::commit_ghost(const int item_type) noexcept
{
    if (!current_ghost.has_data)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No ghost to build - press 'E' first to generate one\n");
        return;
    }
    if (!current_projected_plane.has_valid_target)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No valid target block. Aim at a block face first.\n");
        return;
    }

    const int face = current_projected_plane.target_face;
    const ivec3 normal = face_normal(face);
    const ivec3 axis_u = face_u_axis(face);
    const ivec3 axis_v = face_v_axis(face);
    const int anchor_x = current_projected_plane.target_x + normal.x;
    const int anchor_y = current_projected_plane.target_y + normal.y;
    const int anchor_z = current_projected_plane.target_z + normal.z;

    for (int v = 0; v < current_ghost.height; ++v)
    {
        for (int u = 0; u < current_ghost.width; ++u)
        {
            if (!current_ghost.walls[static_cast<std::size_t>(v) * static_cast<std::size_t>(current_ghost.width) +
                                     static_cast<std::size_t>(u)])
            {
                continue;
            }
            for (int depth = 0; depth < current_ghost.wall_height; ++depth)
            {
                set_block(anchor_x + u * axis_u.x + v * axis_v.x + depth * normal.x,
                          anchor_y + u * axis_u.y + v * axis_v.y + depth * normal.y,
                          anchor_z + u * axis_u.z + v * axis_v.z + depth * normal.z,
                          item_type);
            }
        }
    }
}
