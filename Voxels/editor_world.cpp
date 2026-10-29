#include <cmath>
#include <algorithm>
#include <span>
#include "editor_world.h"

#include <MazeBuilder/algos.h>
#include <MazeBuilder/configurator.h>
#include <MazeBuilder/randomizer.h>
#include <MazeBuilder/runtime_app.h>

#include "chunk.h"
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
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <stdexcept>

namespace
{
    constexpr float FORCE_DUE_TO_GRAVITY = -9.8f;
    // The player's position is the eye; the voxel collider puts the feet this far below it.
    constexpr float PLAYER_FEET_OFFSET = 1.25f;
    constexpr float PLAYER_RADIUS = 0.4f;
    constexpr std::uint64_t PLAYER_CATEGORY = 0x2;
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

editor_world::editor_world(const world_builder &b)
    : world{b}
{
    auto &library = heightmap_library::instance();
    library.ensure_loaded();
    if (!library.current())
    {
        library.add(heightmap_params{});
    }
    world_heightmap = library.current();
    if (active_player)
    {
        active_player->set_world(this);
    }
}

editor_world::~editor_world()
{
    // Workers read player settings and must finish before the player is detached.
    cleanup_worker_threads();
    if (B3_IS_NON_NULL(m_physics_world))
        b3DestroyWorld(m_physics_world);
    for (const auto buffer : m_physics_cube_buffers)
        sdl_gl_helper::del_buffer(buffer);
    for (const auto buffer : m_physics_sphere_buffers)
        sdl_gl_helper::del_buffer(buffer);
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
    configs.view_mode(player_configs::ViewMode::THIRD_PERSON).ortho_scaling(0);
    active_player->pos.rx = 0.0f;
    active_player->pos.ry = -matrix::to_radians(15.0f);
    active_player->vel = {};
    active_player->set_active(true);
    if (world_heightmap)
    {
        active_player->pos.x = active_player->pos.z = static_cast<float>(world_heightmap->side) * 0.5f;
    }
    init_voxel_engine();
    block_emission = glGetUniformLocation(s_block_attrib.program, "bloom_emission");
    block_light_attenuation = glGetUniformLocation(s_block_attrib.program, "light_attenuation");
    block_taper = glGetUniformLocation(s_block_attrib.program, "stack_taper");
    block_center = glGetUniformLocation(s_block_attrib.program, "stack_center");
    init_worker_threads();
    force_chunks(active_player);
    active_player->pos.y = static_cast<float>(highest_block(active_player->pos.x, active_player->pos.z) + 2);
    float sight_x, sight_y, sight_z;
    matrix::compute_sight_vector(active_player->pos.rx, active_player->pos.ry,
                                 sight_x, sight_y, sight_z);
    m_physics_origin_x = active_player->pos.x + sight_x * 8.0f;
    m_physics_origin_z = active_player->pos.z + sight_z * 8.0f;
    m_physics_yaw = active_player->pos.rx;
    m_physics_right_x = SDL_cosf(active_player->pos.rx);
    m_physics_right_z = SDL_sinf(active_player->pos.rx);

    constexpr std::array<int, 6> block_types{
        static_cast<int>(item::BlockType::SAND),
        static_cast<int>(item::BlockType::GRASS),
        static_cast<int>(item::BlockType::STONE),
        static_cast<int>(item::BlockType::SNOW),
        static_cast<int>(item::BlockType::BRICK),
        static_cast<int>(item::BlockType::WOOD)};
    for (std::size_t i = 0; i < m_physics_cube_buffers.size(); ++i)
    {
        m_physics_cube_buffers[i] = sdl_gl_helper::gen_cube_buffer(
            0, 0, 0, i < HEIGHTMAP_MATERIALS.size() ? 0.5f : 0.45f, block_types[i]);
        m_physics_sphere_buffers[i] =
            sdl_gl_helper::gen_voxel_sphere_buffer(0.4f, 7, block_types[i], m_physics_sphere_vertices);
    }
    reset_physics();

    s_line_attrib.program = world_shaders.get(ShaderIdentifier::LINE_SHADER).get();
    s_line_attrib.position = 0;
    s_line_attrib.matrix = glGetUniformLocation(s_line_attrib.program, "matrix");
    s_line_attrib.extra1 = glGetUniformLocation(s_line_attrib.program, "color");

    if (m_bloom)
    {
        std::array<std::uint32_t, static_cast<std::size_t>(ShaderEffect::COUNT)> transition_programs{};
        try
        {
            transition_programs[static_cast<std::size_t>(ShaderEffect::PIXELATE)] =
                world_shaders.get(ShaderIdentifier::TRANSITION_PIXELATE_SHADER).get();
        }
        catch (const std::exception &error)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Pixelate transition unavailable: %s\n", error.what());
        }
        m_bloom->set_transition_programs(transition_programs);
    }

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
    try
    {
        thread_local mazes::randomizer rng{};
        environment.init(static_cast<std::uint32_t>(rng.get_int(0, 1'000)));
    }
    catch (const std::exception &error)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Editor environment unavailable: %s\n", error.what());
    }
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
            support_player_on_physics();
        }
    }
    else
    {
        active_player->set_on_ground(false);
    }

    // Invisible, unbreakable wall along the terrain/ocean boundary (blocks span +-0.5 around each cell).
    if (world_heightmap)
    {
        constexpr float wall_margin = 0.25f;
        const float low = -0.5f + wall_margin;
        const float high = static_cast<float>(world_heightmap->side) - 0.5f - wall_margin;
        auto &p = active_player->pos;
        if (p.x < low || p.x > high)
        {
            p.x = std::clamp(p.x, low, high);
            active_player->vel.vx = 0.0f;
        }
        if (p.z < low || p.z > high)
        {
            p.z = std::clamp(p.z, low, high);
            active_player->vel.vz = 0.0f;
        }
    }

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

    m_bloom->set_single_mode();
    if (environment.ready())
    {
        const float side = world_heightmap ? static_cast<float>(world_heightmap->side) : 128.0f;
        const auto [window_width, window_height] = simple_direct_medialayer->get_window_size();
        const auto &eye = active_player->pos;
        const float view[4]{eye.rx, eye.ry,
                            std::tan(active_player->get_configs().fov() * 0.5f * 3.14159265f / 180.0f),
                            static_cast<float>(window_width) / static_cast<float>(std::max(window_height, 1))};
        const float max_height = world_heightmap ? static_cast<float>(world_heightmap->max_height) : 32.0f;
        const float ocean[4]{eye.x, eye.y, eye.z, side};
        const float sea_level = 1.0f + HEIGHTMAP_MATERIALS[0].max_elevation * (max_height - 1.0f) * 0.85f;
        environment.draw(0.0f, 1.0f, 0.0f, 0.0f,
                         1.0f, 1.0f, time_of_day() * static_cast<float>(active_player->get_configs().day_length()),
                         1.0f, 0.0f,
                         side * 0.5f / static_cast<float>(RENDER_CHUNK_RADIUS * BUILD_CHUNK_SIZE),
                         true, active_player->get_configs().ortho_scaling() ? nullptr : view,
                         ocean, sea_level);
    }
    else
    {
        const auto sky_texture = world_textures.get(TextureIdentifier::SKY).gl_texture;
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        render_sky(world_sky_gl_buffer, sky_texture);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
    }

    m_bloom->set_mrt_mode();
    if (environment.ready())
    {
        environment.bind();
        glUseProgram(s_block_attrib.program);
        glUniform1i(glGetUniformLocation(s_block_attrib.program, "environment_map"), 2);
        glUniform1f(glGetUniformLocation(s_block_attrib.program, "environment_strength"), 0.5f);
    }
    else
    {
        glUseProgram(s_block_attrib.program);
        glUniform1f(glGetUniformLocation(s_block_attrib.program, "environment_strength"), 0.0f);
    }
    m_rendered_terrain_faces = render_chunks(atlas_texture);
    render_physics();
    render_player(atlas_texture);

    m_bloom->set_single_mode();
    render_item(atlas_texture);

    render_signs(signs_texture);
    render_sign(signs_texture);

    render_wireframe();
    if (this->active_player->get_configs().view_mode() == player_configs::ViewMode::FIRST_PERSON)
    {
        render_held_stick(atlas_texture);
        render_crosshairs();
    }

    const auto &configs{active_player->get_configs()};
    const float bloom_str = configs.use_bloom_effect() ? configs.exposure_val() : 0.0f;
    // Pixelate the frame more strongly as the player nears the wall, blending terrain into ocean.
    float blend_progress = 1.0f;
    if (world_heightmap)
    {
        constexpr float blend_distance = 8.0f;
        const auto &p = active_player->pos;
        const float side = static_cast<float>(world_heightmap->side);
        const float edge_distance = std::min({p.x + 0.5f, side - 0.5f - p.x, p.z + 0.5f, side - 0.5f - p.z});
        const float closeness = std::clamp(edge_distance / blend_distance, 0.0f, 1.0f);
        blend_progress = 0.5f + 0.5f * closeness;
    }
    m_bloom->execute(bloom_str, bloom_pass::BLUR_PASSES, ShaderEffect::PIXELATE, blend_progress,
                     static_cast<float>(SDL_GetTicks()) / 1000.0f);
}

command_queue &editor_world::get_command_queue() noexcept
{
    return world_commands;
}

void editor_world::update_player_body(const float dt_seconds) noexcept
{
    auto &b = m_player_body;
    const auto &p = active_player->pos;
    const bool grounded = active_player->is_on_ground();
    const float dt = std::min(dt_seconds, 1.0f / 30.0f);
    if (!b.initialized || std::abs(p.x - b.last_x) > 4.0f || std::abs(p.z - b.last_z) > 4.0f ||
        std::abs(p.y - b.last_y) > 4.0f)
    {
        b = player_body{};
        b.last_x = p.x;
        b.last_y = p.y;
        b.last_z = p.z;
        b.last_grounded = grounded;
        b.initialized = true;
        return;
    }
    if (dt <= 0.0f)
        return;

    // Anchor velocity from actual motion, so collisions and flying are covered too.
    const float av_x = (p.x - b.last_x) / dt, av_y = (p.y - b.last_y) / dt, av_z = (p.z - b.last_z) / dt;
    b.last_x = p.x;
    b.last_y = p.y;
    b.last_z = p.z;

    constexpr float stiffness = 120.0f, damping = 9.0f, restitution = 0.35f, floor_offset = -0.15f;
    b.vx += (-stiffness * b.ox - damping * (b.vx - av_x)) * dt;
    b.vy += (-stiffness * b.oy - damping * (b.vy - av_y)) * dt;
    b.vz += (-stiffness * b.oz - damping * (b.vz - av_z)) * dt;
    b.ox += (b.vx - av_x) * dt;
    b.oy += (b.vy - av_y) * dt;
    b.oz += (b.vz - av_z) * dt;
    if (b.oy < floor_offset)
    {
        b.oy = floor_offset;
        if (b.vy - av_y < 0.0f)
            b.vy = av_y - (b.vy - av_y) * restitution;
    }

    float yaw_error = p.rx - b.yaw_offset;
    yaw_error = std::remainder(yaw_error, 2.0f * 3.14159265f);
    b.yaw_velocity += (60.0f * yaw_error - 8.0f * b.yaw_velocity) * dt;
    b.yaw_offset += b.yaw_velocity * dt;

    if (grounded != b.last_grounded)
        b.squash_velocity += grounded ? std::max(b.last_av_y, -20.0f) * 0.6f : 4.0f;
    b.last_grounded = grounded;
    b.last_av_y = av_y;
    const float stretch_target = grounded ? 0.0f : std::clamp(std::abs(av_y) * 0.02f, 0.0f, 0.3f);
    b.squash_velocity += (-150.0f * (b.squash - stretch_target) - 10.0f * b.squash_velocity) * dt;
    b.squash = std::clamp(b.squash + b.squash_velocity * dt, -0.45f, 0.45f);
}

void editor_world::update_physics(const float delta_time) noexcept
{
    update_player_body(delta_time / 1000.0f);
    const float dt_seconds = delta_time / 1000.0f;
    if (m_physics_reset_requested)
    {
        reset_physics();
        m_physics_reset_requested = false;
    }
    if (!m_physics_paused && std::isfinite(dt_seconds) && dt_seconds > 0.0f)
    {
        // One variable-length step per frame: catch-up loops make slow frames slower.
        const float step = std::min(dt_seconds, 1.0f / 30.0f);
        const auto t0 = SDL_GetPerformanceCounter();
        sync_physics_player();
        steer_maze_bodies(step);
        b3World_Step(m_physics_world, step, 2);
        const auto t1 = SDL_GetPerformanceCounter();
        ++m_physics_step_counter;
        const auto contacts = b3World_GetContactEvents(m_physics_world);
        m_physics_impact_count += static_cast<std::uint64_t>(contacts.hitCount);
        const auto &listener = active_player->pos;
        for (const auto &hit : std::span{contacts.hitEvents, static_cast<std::size_t>(contacts.hitCount)})
        {
            const float px = static_cast<float>(hit.point.x), pz = static_cast<float>(hit.point.z);
            const std::array<float, 3> world_point{m_physics_origin_x + m_physics_right_x * px - m_physics_right_z * pz,
                                                   static_cast<float>(hit.point.y),
                                                   m_physics_origin_z + m_physics_right_z * px + m_physics_right_x * pz};
            const float dx = world_point[0] - listener.x, dy = world_point[1] - listener.y,
                        dz = world_point[2] - listener.z;
            const float distance_sq = dx * dx + dy * dy + dz * dz;
            if (!m_physics_pending_impact || distance_sq < m_physics_pending_impact_distance_sq)
            {
                m_physics_pending_impact = world_point;
                m_physics_pending_impact_distance_sq = distance_sq;
            }
        }
        m_physics_step_ticks += t1 - t0;
        if (m_physics_step_counter % 120 == 0)
        {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "b3World_Step avg %.3f ms over 120 steps (%zu bodies)",
                         static_cast<double>(m_physics_step_ticks) * 1000.0 /
                             static_cast<double>(SDL_GetPerformanceFrequency()) / 120.0,
                         m_physics_objects.size());
            m_physics_step_ticks = 0;
        }
    }
}

void editor_world::handle_event(const SDL_Event &event) noexcept
{
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT))
    {
        m_swing_start_ms = SDL_GetTicks();
        m_swinging = true;
    }
    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        switch (event.key.scancode)
        {
        case SDL_SCANCODE_ESCAPE: {
            break;
        }
        case SDL_SCANCODE_R:
            // Reset
            break;
        default:
            break;
        }
    }
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

void editor_world::reset_physics() noexcept
{
    if (B3_IS_NON_NULL(m_physics_world))
        b3DestroyWorld(m_physics_world);
    m_physics_objects.clear();
    m_maze_flow = {};
    m_maze_headings.clear();
    m_maze_walls.clear();
    m_physics_accumulator = 0.0f;
    m_physics_impact_count = 0;
    m_physics_pending_impact.reset();

    auto world_def = b3DefaultWorldDef();
    world_def.gravity = {0.0f, -9.8f, 0.0f};
    m_physics_world = b3CreateWorld(&world_def);

    auto player_body_def = b3DefaultBodyDef();
    player_body_def.type = b3_kinematicBody;
    player_body_def.isEnabled = false;
    player_body_def.motionLocks.angularX = true;
    player_body_def.motionLocks.angularY = true;
    player_body_def.motionLocks.angularZ = true;
    m_physics_player = b3CreateBody(m_physics_world, &player_body_def);
    auto player_shape_def = b3DefaultShapeDef();
    player_shape_def.baseMaterial.friction = 0.4f;
    // Own category so the ground probe ray can skip the player.
    player_shape_def.filter.categoryBits = PLAYER_CATEGORY;
    const b3Capsule player_capsule{{0.0f, -PLAYER_FEET_OFFSET + PLAYER_RADIUS, 0.0f}, {0.0f, 0.2f, 0.0f}, PLAYER_RADIUS};
    b3CreateCapsuleShape(m_physics_player, &player_shape_def, &player_capsule);

    // Hand the maze a copy clamped to what fits the slice.
    auto maze = active_player->get_configs().maze();
    constexpr unsigned int max_cells = 13;
    maze.rows(std::clamp<unsigned int>(maze.rows(), 2u, max_cells))
        .columns(std::clamp<unsigned int>(maze.columns(), 2u, max_cells));
    if (!spawn_physics_maze(maze, m_physics_maze_seed++))
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Physics maze could not be built\n");
}

std::pair<float, float> editor_world::physics_plane_coords(const float x, const float z) const noexcept
{
    const float dx = x - m_physics_origin_x;
    const float dz = z - m_physics_origin_z;
    return {dx * m_physics_right_x + dz * m_physics_right_z,
            -dx * m_physics_right_z + dz * m_physics_right_x};
}

bool editor_world::player_in_maze(const float along, const float depth) const noexcept
{
    return std::abs(along) <= m_maze_half_w + PLAYER_RADIUS && std::abs(depth) <= m_maze_half_h + PLAYER_RADIUS;
}

void editor_world::sync_physics_player() noexcept
{
    if (!active_player || !B3_IS_NON_NULL(m_physics_player))
        return;
    const auto &pos = active_player->pos;
    const auto [along, depth] = physics_plane_coords(pos.x, pos.z);
    if (!player_in_maze(along, depth))
    {
        if (b3Body_IsEnabled(m_physics_player))
            b3Body_Disable(m_physics_player);
        return;
    }
    if (!b3Body_IsEnabled(m_physics_player))
    {
        b3Body_SetTransform(m_physics_player, {along, pos.y, depth}, b3Quat_identity);
        b3Body_Enable(m_physics_player);
        return;
    }
    // Drive the kinematic capsule with the player's own velocity in plane space, plus a correction toward
    // the player's position. Unlike a target transform this pushes at any speed and along every axis.
    const auto &vel = active_player->vel;
    const float along_velocity = vel.vx * m_physics_right_x + vel.vz * m_physics_right_z;
    const float depth_velocity = -vel.vx * m_physics_right_z + vel.vz * m_physics_right_x;
    const auto current = b3Body_GetPosition(m_physics_player);
    const float correction = 1.0f / PHYSICS_STEP;
    b3Body_SetLinearVelocity(m_physics_player,
                             {along_velocity + (along - static_cast<float>(current.x)) * correction * 0.5f,
                              vel.vy + (pos.y - static_cast<float>(current.y)) * correction * 0.5f,
                              depth_velocity + (depth - static_cast<float>(current.z)) * correction * 0.5f});
    b3Body_SetAwake(m_physics_player, true);
}

void editor_world::support_player_on_physics() noexcept
{
    if (!active_player || !B3_IS_NON_NULL(m_physics_world) || active_player->vel.vy > 0.0f)
        return;
    auto &pos = active_player->pos;
    const auto [along, depth] = physics_plane_coords(pos.x, pos.z);
    if (!player_in_maze(along, depth))
        return;

    auto filter = b3DefaultQueryFilter();
    filter.maskBits = ~PLAYER_CATEGORY;
    const auto hit = b3World_CastRayClosest(m_physics_world, {along, pos.y, depth},
                                            {0.0f, -(PLAYER_FEET_OFFSET + 0.35f), 0.0f}, filter);
    if (!hit.hit)
        return;
    const float stand_y = static_cast<float>(hit.point.y) + PLAYER_FEET_OFFSET;
    if (pos.y <= stand_y + 0.35f)
    {
        pos.y = std::max(pos.y, stand_y);
        active_player->vel.vy = 0.0f;
        active_player->set_on_ground(true);
    }
}

void editor_world::steer_maze_bodies(const float dt_seconds) noexcept
{
    if (m_maze_headings.empty())
        return;
    constexpr float roll_speed = 3.0f;
    const float blend = std::min(1.0f, 4.0f * dt_seconds);
    const int columns = m_maze_flow.width * MAZE_HEADING_SCALE;
    const int rows = m_maze_flow.height * MAZE_HEADING_SCALE;
    for (const auto &object : m_physics_objects)
    {
        if (!object.follows_maze_path)
            continue;
        const auto position = b3Body_GetPosition(object.body);
        const int cx = static_cast<int>(std::floor((static_cast<float>(position.x) + m_maze_half_w) * MAZE_HEADING_SCALE));
        const int cy = static_cast<int>(std::floor((static_cast<float>(position.z) + m_maze_half_h) * MAZE_HEADING_SCALE));
        const bool outside = cx < 0 || cy < 0 || cx >= columns || cy >= rows ||
                             static_cast<float>(position.y) < m_maze_platform_y - 10.0f;
        const maze_heading *heading =
            outside ? nullptr : &m_maze_headings[static_cast<std::size_t>(cy * columns + cx)];
        if (outside || heading->goal)
        {
            b3Body_SetTransform(object.body, {object.spawn[0], object.spawn[1], object.spawn[2]}, b3Quat_identity);
            b3Body_SetLinearVelocity(object.body, {0.0f, 0.0f, 0.0f});
            b3Body_SetAngularVelocity(object.body, {0.0f, 0.0f, 0.0f});
            continue;
        }
        auto velocity = b3Body_GetLinearVelocity(object.body);
        // Steer only the horizontal velocity; gravity and collisions keep control of y.
        velocity.x += (heading->x * roll_speed - velocity.x) * blend;
        velocity.z += (heading->y * roll_speed - velocity.z) * blend;
        b3Body_SetAwake(object.body, true);
        b3Body_SetLinearVelocity(object.body, velocity);
    }
}

bool editor_world::spawn_physics_maze(const mazes::configurator &maze, const int seed, const int body_count) noexcept
{
    if (!B3_IS_NON_NULL(m_physics_world) || m_physics_objects.size() >= MAX_PHYSICS_OBJECTS)
        return false;

    // The physics slice is 29 columns wide and 28 deep, so mazes up to 13x13 cells (27x27 pixels) fit.
    constexpr unsigned int max_cells = 13;
    m_maze_walls.clear();
    if (maze.rows() == 0 || maze.columns() == 0 || maze.rows() > max_cells || maze.columns() > max_cells)
        return false;

    const auto layout = generate_maze_layout(maze, seed);
    if (!layout)
        return false;

    const int width = layout->width();
    const int height = layout->height();
    const float half_w = static_cast<float>(width) * 0.5f;
    const float half_h = static_cast<float>(height) * 0.5f;
    constexpr float wall_half_height = 1.0f;
    // Sit the floor flush with the highest terrain under the maze footprint.
    int terrain_top = 0;
    for (float along = -half_w; along <= half_w; along += 3.0f)
    {
        for (float depth = -half_h; depth <= half_h; depth += 3.0f)
        {
            terrain_top = std::max(terrain_top,
                                   highest_block(m_physics_origin_x + m_physics_right_x * along - m_physics_right_z * depth,
                                                 m_physics_origin_z + m_physics_right_z * along + m_physics_right_x * depth));
        }
    }
    const float platform_y = static_cast<float>(terrain_top) + 1.0f;

    auto static_def = b3DefaultBodyDef();
    static_def.position = {0.0f, platform_y, 0.0f};
    const auto body = b3CreateBody(m_physics_world, &static_def);
    auto shape_def = b3DefaultShapeDef();
    shape_def.baseMaterial.friction = 0.6f;

    const auto floor = b3MakeOffsetBoxHull(half_w, 0.25f, half_h, {0.0f, -0.75f, 0.0f});
    b3CreateHullShape(body, &shape_def, &floor.base);

    // One box per horizontal run of wall pixels keeps the shape count small.
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width;)
        {
            if (!layout->is_wall(x, y))
            {
                ++x;
                continue;
            }
            int end = x;
            while (end < width && layout->is_wall(end, y))
                ++end;
            for (int wx = x; wx < end; ++wx)
                m_maze_walls.emplace_back(wx, y);
            const float run_half = static_cast<float>(end - x) * 0.5f;
            const auto box = b3MakeOffsetBoxHull(
                run_half, wall_half_height, 0.5f,
                {static_cast<float>(x) + run_half - half_w, wall_half_height - 0.5f,
                 static_cast<float>(y) + 0.5f - half_h});
            b3CreateHullShape(body, &shape_def, &box.base);
            x = end;
        }
    }

    // Goal is the last cell; followers start in the cells farthest from it, spread across that set.
    const int goal_x = width - 2, goal_y = height - 2;
    m_maze_flow = build_maze_flow_field(*layout, goal_x, goal_y);
    m_maze_half_w = half_w;
    m_maze_half_h = half_h;
    m_maze_platform_y = platform_y;

    // Bake headings and goal zones once so each step is a single array lookup per body.
    m_maze_headings.assign(static_cast<std::size_t>(width * MAZE_HEADING_SCALE) * static_cast<std::size_t>(height * MAZE_HEADING_SCALE), {});
    for (int cy = 0; cy < height * MAZE_HEADING_SCALE; ++cy)
    {
        for (int cx = 0; cx < width * MAZE_HEADING_SCALE; ++cx)
        {
            const float fx = (static_cast<float>(cx) + 0.5f) / MAZE_HEADING_SCALE;
            const float fy = (static_cast<float>(cy) + 0.5f) / MAZE_HEADING_SCALE;
            const auto d = m_maze_flow.direction(fx, fy);
            m_maze_headings[static_cast<std::size_t>(cy * width * MAZE_HEADING_SCALE + cx)] =
                {d[0], d[1], [&] { const float r = m_maze_flow.sample(fx, fy); return r >= 0.0f && r < 0.75f; }()};
        }
    }

    std::vector<std::pair<float, std::pair<int, int>>> cells;
    for (unsigned int row = 0; row < layout->rows; ++row)
    {
        for (unsigned int col = 0; col < layout->columns; ++col)
        {
            const int x = static_cast<int>(col) * 2 + 1, y = static_cast<int>(row) * 2 + 1;
            if (const float d = m_maze_flow.at(x, y); d > 0.0f)
                cells.push_back({d, {x, y}});
        }
    }
    if (cells.empty())
        return false;
    std::ranges::sort(cells, [](const auto &l, const auto &r) { return l.first > r.first; });
    const std::size_t pool = std::max<std::size_t>(1, cells.size() / 2);
    const auto count = static_cast<std::size_t>(std::max(1, body_count));
    for (std::size_t i = 0; i < count && m_physics_objects.size() < MAX_PHYSICS_OBJECTS; ++i)
    {
        const auto &[px, py] = cells[i * pool / count].second;
        const std::array<float, 3> spawn{static_cast<float>(px) + 0.5f - half_w, platform_y + 1.0f,
                                         static_cast<float>(py) + 0.5f - half_h};
        auto body_def = b3DefaultBodyDef();
        body_def.type = b3_dynamicBody;
        body_def.position = {spawn[0], spawn[1], spawn[2]};
        const auto ball = b3CreateBody(m_physics_world, &body_def);
        auto ball_shape_def = b3DefaultShapeDef();
        ball_shape_def.density = 10.0f;
        ball_shape_def.baseMaterial.friction = 0.2f;
        ball_shape_def.baseMaterial.restitution = 0.2f;
        ball_shape_def.enableHitEvents = true;
        const b3Sphere sphere{{0.0f, 0.0f, 0.0f}, 0.4f};
        b3CreateSphereShape(ball, &ball_shape_def, &sphere);
        m_physics_objects.push_back({ball, true, 4, {1.0f, 0.4f, 0.2f}, true, spawn});
    }
    return true;
}
