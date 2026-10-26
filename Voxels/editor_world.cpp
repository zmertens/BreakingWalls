#include <cmath>
#include <algorithm>
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
    // Half the slice thickness plus the capsule radius, plus a little margin.
    constexpr float PLAYER_SLICE_REACH = 14.0f + PLAYER_RADIUS;
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
    restore_physics_voxels();
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
            sdl_gl_helper::gen_voxel_sphere_buffer(0.65f, 7, block_types[i], m_physics_sphere_vertices);
    }
    reset_physics();

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
    m_bloom->execute(bloom_str);
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
        m_add_physics_block_requested = false;
        m_add_physics_sphere_requested = false;
    }
    if (m_physics_jolt_requested)
    {
        jolt_physics_blocks();
        m_physics_jolt_requested = false;
    }
    if (m_add_physics_block_requested)
    {
        add_physics_object(false);
        m_add_physics_block_requested = false;
    }
    if (m_add_physics_sphere_requested)
    {
        add_physics_object(true);
        m_add_physics_sphere_requested = false;
    }
    if (!m_physics_paused && std::isfinite(dt_seconds) && dt_seconds > 0.0f)
    {
        // One variable-length step per frame: catch-up loops make slow frames slower.
        const float step = std::min(dt_seconds, 1.0f / 30.0f);
        const auto t0 = SDL_GetPerformanceCounter();
        sync_physics_player();
        b3World_Step(m_physics_world, step, 2);
        const auto t1 = SDL_GetPerformanceCounter();
        // Bodies that roll off the arena edge are dropped back in rather than blocked by walls.
        if (++m_physics_step_counter % 8 == 0)
        {
            for (const auto &object : m_physics_objects)
            {
                if (b3Body_GetPosition(object.body).y < -10.0f)
                {
                    b3Body_SetTransform(object.body, {0.0f, m_physics_spawn_height, 0.0f}, b3Quat_identity);
                    b3Body_SetLinearVelocity(object.body, {0.0f, 0.0f, 0.0f});
                    b3Body_SetAngularVelocity(object.body, {0.0f, 0.0f, 0.0f});
                }
            }
        }
        const auto contacts = b3World_GetContactEvents(m_physics_world);
        m_physics_impact_count += static_cast<std::uint64_t>(contacts.hitCount);
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
    restore_physics_voxels();
    if (B3_IS_NON_NULL(m_physics_world))
        b3DestroyWorld(m_physics_world);
    m_physics_objects.clear();
    m_physics_accumulator = 0.0f;
    m_physics_impact_count = 0;

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

    auto terrain_shape_def = b3DefaultShapeDef();
    terrain_shape_def.baseMaterial.friction = 0.85f;
    auto terrain_body_def = b3DefaultBodyDef();
    const auto terrain_body = b3CreateBody(m_physics_world, &terrain_body_def);
    constexpr int half_columns = 14;
    constexpr int columns = half_columns * 2 + 1;
    std::array<int, columns> terrain_materials{};
    int maximum_height = 1;
    m_physics_objects.reserve(MAX_PHYSICS_OBJECTS);
    m_physics_voxel_sources.reserve(MAX_PHYSICS_OBJECTS);

    for (int column = 0; column < columns; ++column)
    {
        const int offset = column - half_columns;
        const int world_x = static_cast<int>(
            SDL_roundf(m_physics_origin_x + m_physics_right_x * static_cast<float>(offset)));
        const int world_z = static_cast<int>(
            SDL_roundf(m_physics_origin_z + m_physics_right_z * static_cast<float>(offset)));
        const bool inside = world_heightmap && world_heightmap->contains(world_x, world_z);
        const int height = inside ? world_heightmap->column_height(world_x, world_z) : 0;
        maximum_height = std::max(maximum_height, height);
        terrain_materials[static_cast<std::size_t>(column)] =
            inside ? static_cast<int>(&heightmap::material_for(world_heightmap->elevation(world_x, world_z)) -
                                      HEIGHTMAP_MATERIALS.data())
                   : 0;

        if (height <= 0)
            continue;

        const auto foundation_at = [&](const float y)
        {
            const auto box = b3MakeOffsetBoxHull(0.5f, 0.5f, 0.5f,
                                                 {static_cast<float>(offset), y, 0.0f});
            b3CreateHullShape(terrain_body, &terrain_shape_def, &box.base);
        };
        foundation_at(0.0f);
        if (height > 1)
            foundation_at(1.0f);

        const auto source_chunk = find_chunk(chunked(static_cast<float>(world_x)),
                                             chunked(static_cast<float>(world_z)));
        if (!inside || !source_chunk)
            continue;

        for (int y = 2; y < height; ++y)
        {
            const int block_type = y == height - 1
                                       ? heightmap::material_for(world_heightmap->elevation(world_x, world_z)).block
                                       : static_cast<int>(item::BlockType::STONE);
            if (get_block(world_x, y, world_z) != block_type ||
                !source_chunk.value()->map.set(world_x, y, world_z, 0))
                continue;

            m_physics_voxel_sources.push_back({world_x, y, world_z, block_type});
            dirty_chunk(source_chunk.value());

            auto body_def = b3DefaultBodyDef();
            body_def.type = b3_dynamicBody;
            body_def.position = {static_cast<float>(offset), static_cast<float>(y), 0.0f};
            const auto body = b3CreateBody(m_physics_world, &body_def);
            auto shape_def = b3DefaultShapeDef();
            shape_def.density = 0.5f;
            shape_def.baseMaterial.friction = 0.3f;
            shape_def.baseMaterial.restitution = 0.04f;
            shape_def.enableHitEvents = true;
            const auto voxel = b3MakeBoxHull(0.5f, 0.5f, 0.5f);
            b3CreateHullShape(body, &shape_def, &voxel.base);

            const int material = y == height - 1 ? terrain_materials[static_cast<std::size_t>(column)] : 2;
            constexpr std::array<std::array<float, 3>, 3> colors{{{0.95f, 0.37f, 0.16f}, {0.2f, 0.65f, 0.95f}, {0.88f, 0.72f, 0.2f}}};
            m_physics_objects.push_back(
                {body, false, material, colors[static_cast<std::size_t>(material % colors.size())]});
        }
    }

    constexpr float floor_half_width = static_cast<float>(half_columns) + 0.5f;
    constexpr float arena_half_depth = 14.0f;
    // Off-slice terrain: static columns matching the visible voxel heights so bodies don't sink.
    constexpr int arena_depth = static_cast<int>(arena_half_depth);
    for (int column = 0; column < columns; ++column)
    {
        const int offset = column - half_columns;
        std::array<int, 2 * arena_depth + 1> heights{};
        for (int depth = -arena_depth; depth <= arena_depth; ++depth)
        {
            if (depth == 0)
                continue;
            const int world_x = static_cast<int>(SDL_roundf(
                m_physics_origin_x + m_physics_right_x * static_cast<float>(offset) -
                m_physics_right_z * static_cast<float>(depth)));
            const int world_z = static_cast<int>(SDL_roundf(
                m_physics_origin_z + m_physics_right_z * static_cast<float>(offset) +
                m_physics_right_x * static_cast<float>(depth)));
            if (world_heightmap && world_heightmap->contains(world_x, world_z))
                heights[static_cast<std::size_t>(depth + arena_depth)] =
                    std::max(0, world_heightmap->column_height(world_x, world_z));
        }
        // Merge runs of equal height along depth into one box to keep the static broadphase small.
        for (std::size_t i = 0; i < heights.size();)
        {
            const int height = heights[i];
            const auto run_end = std::find_if(heights.begin() + static_cast<std::ptrdiff_t>(i), heights.end(),
                                              [height](const int h) { return h != height; });
            const auto run = static_cast<std::size_t>(run_end - (heights.begin() + static_cast<std::ptrdiff_t>(i)));
            if (height > 0)
            {
                const float half = static_cast<float>(height) * 0.5f;
                const float run_half = static_cast<float>(run) * 0.5f;
                const float center_depth = static_cast<float>(static_cast<int>(i) - arena_depth) + run_half - 0.5f;
                const auto box = b3MakeOffsetBoxHull(0.5f, half, run_half,
                                                     {static_cast<float>(offset), half - 0.5f, center_depth});
                b3CreateHullShape(terrain_body, &terrain_shape_def, &box.base);
            }
            i += run;
        }
    }
    const auto floor = b3MakeOffsetBoxHull(floor_half_width, 0.05f, arena_half_depth,
                                           {0.0f, -0.55f, 0.0f});
    b3CreateHullShape(terrain_body, &terrain_shape_def, &floor.base);
    m_physics_spawn_height = static_cast<float>(maximum_height) + 5.0f;

    constexpr int startup_spheres = 24;
    for (int i = 0; i < startup_spheres; ++i)
    {
        const float x = -9.0f + static_cast<float>(i % 8) * 2.6f;
        const float z = -3.0f + static_cast<float>((i / 8) % 3) * 3.0f;
        auto body_def = b3DefaultBodyDef();
        body_def.type = b3_dynamicBody;
        body_def.position = {x, m_physics_spawn_height + static_cast<float>(i / 8) * 2.0f, z};
        body_def.linearVelocity = {x < 0.0f ? 6.0f : -6.0f, 0.0f, z < 0.0f ? 4.0f : (z > 0.0f ? -4.0f : 1.5f * static_cast<float>(i % 3 - 1))};
        const auto body = b3CreateBody(m_physics_world, &body_def);
        auto shape_def = b3DefaultShapeDef();
        shape_def.density = 10.0f;
        shape_def.baseMaterial.friction = 0.2f;
        shape_def.baseMaterial.restitution = 0.35f;
        shape_def.enableHitEvents = true;
        const b3Sphere sphere{{0.0f, 0.0f, 0.0f}, 0.65f};
        b3CreateSphereShape(body, &shape_def, &sphere);
        m_physics_objects.push_back({body, true, i % 6, {1.0f, 1.0f, 1.0f}});
    }
    m_physics_jolt_seed = 1;
    jolt_physics_blocks();
}

std::pair<float, float> editor_world::physics_plane_coords(const float x, const float z) const noexcept
{
    const float dx = x - m_physics_origin_x;
    const float dz = z - m_physics_origin_z;
    return {dx * m_physics_right_x + dz * m_physics_right_z,
            -dx * m_physics_right_z + dz * m_physics_right_x};
}

void editor_world::sync_physics_player() noexcept
{
    if (!active_player || !B3_IS_NON_NULL(m_physics_player))
        return;
    const auto &pos = active_player->pos;
    const auto [along, depth] = physics_plane_coords(pos.x, pos.z);
    if (std::abs(depth) > PLAYER_SLICE_REACH)
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
    if (std::abs(depth) > PLAYER_SLICE_REACH)
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

void editor_world::jolt_physics_blocks() noexcept
{
    // Deterministic LCG so every reset produces the same wobble.
    const auto next = [this]
    {
        m_physics_jolt_seed = m_physics_jolt_seed * 1664525u + 1013904223u;
        return static_cast<float>(m_physics_jolt_seed >> 8) / static_cast<float>(1u << 24) * 2.0f - 1.0f;
    };
    for (const auto &object : m_physics_objects)
    {
        if (object.sphere)
            continue;
        b3Body_SetAwake(object.body, true);
        b3Body_SetLinearVelocity(object.body, {next() * 2.5f, 1.0f + next() * 1.5f, next() * 2.5f});
        b3Body_SetAngularVelocity(object.body, {next() * 3.0f, next() * 3.0f, next() * 3.0f});
    }
}

void editor_world::add_physics_object(const bool sphere) noexcept
{
    if (m_physics_objects.size() >= MAX_PHYSICS_OBJECTS)
        return;
    if (sphere && std::ranges::count_if(m_physics_objects, [](const physics_object &object)
                                        { return object.sphere; }) >= MAX_PHYSICS_SPHERES)
        return;

    const auto index = m_physics_objects.size();
    auto body_def = b3DefaultBodyDef();
    body_def.type = b3_dynamicBody;
    body_def.position = {-4.0f + static_cast<float>(index % 9), m_physics_spawn_height, static_cast<float>(index % 5) * 2.0f - 4.0f};
    const auto body = b3CreateBody(m_physics_world, &body_def);

    auto shape_def = b3DefaultShapeDef();
    shape_def.density = sphere ? 10.0f : 0.5f;
    shape_def.baseMaterial.friction = 0.25f;
    shape_def.baseMaterial.restitution = 0.12f;
    shape_def.enableHitEvents = true;
    if (sphere)
    {
        const b3Sphere circle{{0.0f, 0.0f, 0.0f}, 0.65f};
        b3CreateSphereShape(body, &shape_def, &circle);
        b3Body_SetLinearVelocity(body, {index % 2 == 0 ? 6.0f : -6.0f, 0.0f, index % 3 == 0 ? 4.0f : -4.0f});
    }
    else
    {
        const auto box = b3MakeBoxHull(0.45f, 0.45f, 0.45f);
        b3CreateHullShape(body, &shape_def, &box.base);
    }
    constexpr std::array<std::array<float, 3>, 3> colors{{{0.95f, 0.37f, 0.16f}, {0.2f, 0.65f, 0.95f}, {0.88f, 0.72f, 0.2f}}};
    m_physics_objects.push_back({body, sphere, 4, colors[index % colors.size()]});
}

void editor_world::restore_physics_voxels() noexcept
{
    for (const auto &voxel : m_physics_voxel_sources)
    {
        if (get_block(voxel.x, voxel.y, voxel.z) != 0)
            continue;
        if (auto source_chunk = find_chunk(chunked(static_cast<float>(voxel.x)),
                                           chunked(static_cast<float>(voxel.z)));
            source_chunk && source_chunk.value()->map.set(voxel.x, voxel.y, voxel.z, voxel.block_type))
        {
            dirty_chunk(source_chunk.value());
        }
    }
    m_physics_voxel_sources.clear();
}

