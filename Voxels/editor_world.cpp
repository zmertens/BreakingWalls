#include <cmath>
#include <algorithm>
#include <span>
#include "editor_world.h"

#include <MazeBuilder/algos.h>
#include <MazeBuilder/configurator.h>
#include <MazeBuilder/randomizer.h>
#include <MazeBuilder/runtime_app.h>

#include "arcade_game.h"
#include "chunk.h"
#include "db_manager.h"
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
    // Half the maze corridor width is 0.5, so bodies stay a little smaller than that.
    constexpr float ARCADE_BODY_SIZE = 0.4f;
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
    library.ensure_loaded(world_db);
    if (!library.current())
    {
        library.add(heightmap_params{}, world_db);
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
    destroy_physics();
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
        // A position saved from a previous session is only valid inside the current island.
        if (const auto saved = active_player->take_saved_position())
        {
            const auto side = static_cast<float>(world_heightmap->side);
            if (saved->x >= 0.f && saved->x < side && saved->z >= 0.f && saved->z < side)
            {
                active_player->pos.x = saved->x;
                active_player->pos.z = saved->z;
                active_player->pos.rx = saved->rx;
                active_player->pos.ry = saved->ry;
            }
        }
    }
    init_voxel_engine();
    ticker.set_message("Welcome to Breaking Walls!");
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
        m_physics_cube_buffers[i] = sdl_gl_helper::gen_cube_buffer(0, 0, 0, ARCADE_BODY_SIZE, block_types[i]);
        m_physics_sphere_buffers[i] =
            sdl_gl_helper::gen_voxel_sphere_buffer(ARCADE_BODY_SIZE, 7, block_types[i], m_physics_sphere_vertices);
    }

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
        for (const auto &[effect, id] : {std::pair{ShaderEffect::BLUR, ShaderIdentifier::TRANSITION_BLUR_SHADER},
                                         std::pair{ShaderEffect::WAVE, ShaderIdentifier::TRANSITION_WAVE_SHADER}})
        {
            try
            {
                transition_programs[static_cast<std::size_t>(effect)] = world_shaders.get(id).get();
            }
            catch (const std::exception &error)
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Transition effect unavailable: %s\n", error.what());
            }
        }
        m_bloom->set_transition_programs(transition_programs);
    }

    if (!particles.init())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Particle system unavailable (transform feedback)\n");
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

    ticker.set_enabled(active_player->get_configs().show_tickerbox());
    ticker.update(delta_time);
    if (arcade.active())
    {
        update_arcade(delta_time, rng);
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

    if (arcade.active() && arcade.level())
    {
        // Free roam: the player moves normally but cannot leave the maze; the camera stays fixed.
        const auto &built = *arcade.level();
        constexpr float margin = 0.3f;
        auto &p = active_player->pos;
        const float low_x = static_cast<float>(built.origin_x()) + margin;
        const float high_x = static_cast<float>(built.origin_x() + built.columns() - 1) - margin;
        const float low_z = static_cast<float>(built.origin_z()) + margin;
        const float high_z = static_cast<float>(built.origin_z() + built.rows() - 1) - margin;
        p.x = std::clamp(p.x, low_x, high_x);
        p.z = std::clamp(p.z, low_z, high_z);
        active_player->pos.rx = 0.0f;
        active_player->pos.ry = -matrix::to_radians(60.0f);
    }

    // Player trail in the orthographic (arcade) camera while moving.
    {
        const auto &p = active_player->pos;
        const float dx = p.x - m_last_trail_position[0], dz = p.z - m_last_trail_position[2];
        if (active_player->get_configs().ortho_scaling() > 0 && dx * dx + dz * dz > 0.0025f)
        {
            spawn_particles(p.x, p.y - 1.0f, p.z, 2, particle_system::kind::TRAIL);
        }
        m_last_trail_position = {p.x, p.y, p.z};
    }
    m_fx_remaining = std::max(0.0f, m_fx_remaining - dt_seconds);
    particles.update(dt_seconds);

    delete_chunks();
    sdl_gl_helper::del_buffer(active_player->get_buffer());
    ensure_chunks(active_player);
    update_dirty_chunks_async();
}

void editor_world::trigger_effect(const ShaderEffect effect, const float seconds) noexcept
{
    static constexpr auto INTERNAL_DELAY_MS{5'000.f};
    static auto last_trigger_time_ms{0.f};
    const auto current_time_ms = static_cast<float>(SDL_GetTicks());
    if (current_time_ms - last_trigger_time_ms < INTERNAL_DELAY_MS)
        return;
    last_trigger_time_ms = current_time_ms;
    m_fx_effect = effect;
    m_fx_duration = m_fx_remaining = std::max(1.f, seconds);
}

void editor_world::begin_arcade(const int level, mazes::randomizer &rng) noexcept
{
    active_player->get_configs().view_mode(player_configs::ViewMode::ORTHO).ortho_scaling(24);
    active_player->get_configs().is_flying(false).is_auto_running(false);
    start_arcade(level, rng);
    if (const auto built = arcade.level())
    {
        active_player->pos.x = static_cast<float>(built->world_x(built->start_column()));
        active_player->pos.z = static_cast<float>(built->world_z(built->start_row()));
        active_player->pos.y = static_cast<float>(built->origin_y() + 6);
        active_player->vel = {};
    }
    reset_physics();
    arcade.pause();
    ticker.set_message("Press Q or click to start");
}

void editor_world::end_arcade() noexcept
{
    stop_arcade();
    destroy_physics();
    active_player->get_configs().view_mode(player_configs::ViewMode::THIRD_PERSON).ortho_scaling(0);
    ticker.set_message("Arcade ended");
}

void editor_world::update_arcade(const float delta_time_ms, mazes::randomizer &rng) noexcept
{
    const float dt = delta_time_ms / 1000.0f;
    arcade.update(dt, rng, m_arcade_changes);
    if (arcade.level() != m_physics_level)
    {
        reset_physics();
    }

    if (arcade.running())
    {
        const auto &p = active_player->pos;
        constexpr float feet_offset = 1.25f;
        switch (arcade.move_player(p.x, p.y - feet_offset, p.z, active_player->vel.vx, active_player->vel.vz,
                                   m_arcade_changes))
        {
        case arcade_event::BROKE_WALL:
            for (const auto &change : m_arcade_changes)
            {
                spawn_particles(static_cast<float>(change.x), static_cast<float>(change.y),
                                static_cast<float>(change.z), 36, particle_system::kind::FIRE);
            }
            trigger_effect(ShaderEffect::WAVE, 0.5f);
            ticker.push("Wall broken -" + std::to_string(arcade.rules.wall_penalty));
            active_player->rumble(0.7f, 0.3f, 90);
            break;
        case arcade_event::WON:
            spawn_particles(p.x, p.y, p.z, 400, particle_system::kind::SPARK);
            trigger_effect(ShaderEffect::PIXELATE, SDL_sinf(SDL_GetTicksNS() / 10'00.0f));
            ticker.set_message("LEVEL CLEAR! Score " + std::to_string(arcade.score()) + " (bonus " +
                               std::to_string(arcade.final_bonus()) + "). Press Q for next level");
            active_player->rumble(0.8f, 0.8f, 400);
            break;
        default:
            break;
        }
        bump_player_with_spheres(dt);
    }

    if (arcade.take_win() && world_db)
    {
        world_db->save_highscore({arcade.level_number(), arcade.score(),
                                  static_cast<int>(arcade.elapsed() * 1000.0f), arcade.walls_broken()});
    }
    release_physics_walls(m_arcade_changes);
    apply_arcade_changes(m_arcade_changes);
}

void editor_world::bump_player_with_spheres(const float dt) noexcept
{
    m_bump_cooldown = std::max(0.0f, m_bump_cooldown - dt);
    if (!B3_IS_NON_NULL(m_physics_world) || m_bump_cooldown > 0.0f)
    {
        return;
    }
    auto &p = active_player->pos;
    // The capsule's centre sits about half a block below the eye.
    const float cy = p.y - 0.5f;
    constexpr float reach = ARCADE_BODY_SIZE + 0.45f;
    for (const auto &object : m_physics_objects)
    {
        if (!object.sphere)
        {
            continue;
        }
        const auto at = b3Body_GetPosition(object.body);
        const float dx = p.x - static_cast<float>(at.x), dy = cy - static_cast<float>(at.y),
                    dz = p.z - static_cast<float>(at.z);
        const float distance_sq = dx * dx + dy * dy + dz * dz;
        if (distance_sq > reach * reach)
        {
            continue;
        }
        const float inv = 1.0f / std::max(std::sqrt(distance_sq), 0.001f);
        active_player->vel.vx += dx * inv * 6.0f;
        active_player->vel.vz += dz * inv * 6.0f;
        active_player->vel.vy = std::max(active_player->vel.vy, 3.0f);
        active_player->rumble(0.4f, 0.6f, 100);
        spawn_particles(p.x, cy, p.z, 12, particle_system::kind::SPARK);
        m_bump_cooldown = 0.4f;
        return;
    }
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
                         ocean, sea_level,
                         arcade.level() ? (eye.x - side * 0.5f) * 0.02f : 0.0f,
                         arcade.level() ? (eye.z - side * 0.5f) * 0.02f : 0.0f);
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
    if (particles.ready() && active_player->get_configs().show_particles())
    {
        float particle_matrix[16];
        const auto [pw, ph] = simple_direct_medialayer->get_window_size();
        const auto cam = camera_position();
        const auto rot = camera_rotation();
        matrix::set_3d(particle_matrix, pw, ph, cam[0], cam[1], cam[2], rot[0], rot[1],
                       active_player->get_configs().fov(), active_player->get_configs().ortho_scaling(),
                       RENDER_CHUNK_RADIUS);
        particles.draw(particle_matrix, static_cast<float>(ph), active_player->get_configs().ortho_scaling() == 0);
    }
    if (this->active_player->get_configs().view_mode() == player_configs::ViewMode::FIRST_PERSON)
    {
        render_held_stick(atlas_texture);
        render_crosshairs();
    }

    render_ticker(world_textures.get(TextureIdentifier::BITMAP_FONT).gl_texture);
    render_arcade_hud(world_textures.get(TextureIdentifier::BITMAP_FONT).gl_texture);

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
    auto effect = ShaderEffect::PIXELATE;
    if (m_fx_remaining > 0.0f)
    {
        effect = m_fx_effect;
        blend_progress = std::clamp(1.0f - m_fx_remaining / m_fx_duration, 0.02f, 0.98f);
    }
    else if (arcade.state() == arcade_state::PAUSED)
    {
        effect = ShaderEffect::BLUR;
        blend_progress = 0.5f;
    }
    m_bloom->execute(bloom_str, bloom_pass::BLUR_PASSES, effect, blend_progress,
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
    if (!B3_IS_NON_NULL(m_physics_world))
        return;
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
    if (!m_physics_paused && arcade.running() && std::isfinite(dt_seconds) && dt_seconds > 0.0f)
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
                const auto at = b3Body_GetPosition(object.body);
                const auto &lv = *m_physics_level;
                const bool outside = at.x < lv.origin_x() - 0.5f || at.x > lv.origin_x() + lv.columns() ||
                                     at.z < lv.origin_z() - 0.5f || at.z > lv.origin_z() + lv.rows();
                if (at.y < lv.origin_y() - 6.0f || outside)
                {
                    const auto spawn = random_maze_spawn();
                    b3Body_SetTransform(object.body, {spawn[0], spawn[1], spawn[2]}, b3Quat_identity);
                    b3Body_SetLinearVelocity(object.body, {0.0f, 0.0f, 0.0f});
                    b3Body_SetAngularVelocity(object.body, {0.0f, 0.0f, 0.0f});
                }
            }
        }
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
    const auto built = arcade.level();
    if (!built)
        return;
    if (B3_IS_NON_NULL(m_physics_world))
        b3DestroyWorld(m_physics_world);
    m_physics_level = built;
    // Arcade physics lives in world space.
    m_physics_origin_x = m_physics_origin_z = 0.0f;
    m_physics_right_x = 1.0f;
    m_physics_right_z = 0.0f;
    m_physics_yaw = 0.0f;
    m_physics_walls.clear();
    m_physics_objects.clear();
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

    auto terrain_shape_def = b3DefaultShapeDef();
    terrain_shape_def.baseMaterial.friction = 0.85f;
    auto terrain_body_def = b3DefaultBodyDef();
    const auto terrain_body = b3CreateBody(m_physics_world, &terrain_body_def);
    m_physics_static = terrain_body;
    m_physics_objects.reserve(MAX_PHYSICS_OBJECTS);

    // Physics space equals world space while the arcade runs.
    const auto &level = *m_physics_level;
    const float oy = static_cast<float>(level.origin_y());
    const auto floor = b3MakeOffsetBoxHull(
        static_cast<float>(level.columns()) * 0.5f, 0.5f, static_cast<float>(level.rows()) * 0.5f,
        {static_cast<float>(level.world_x(0)) + static_cast<float>(level.columns() - 1) * 0.5f, oy,
         static_cast<float>(level.world_z(0)) + static_cast<float>(level.rows() - 1) * 0.5f});
    b3CreateHullShape(terrain_body, &terrain_shape_def, &floor.base);

    constexpr float wall_half = static_cast<float>(arcade_level::WALL_HEIGHT) * 0.5f;
    for (int r = 0; r < level.rows(); ++r)
    {
        for (int c = 0; c < level.columns(); ++c)
        {
            if (!level.is_wall(r, c))
                continue;
            const auto box = b3MakeOffsetBoxHull(0.5f, wall_half, 0.5f,
                                                 {static_cast<float>(level.world_x(c)), oy + 0.5f + wall_half,
                                                  static_cast<float>(level.world_z(r))});
            m_physics_walls[r * level.columns() + c] = b3CreateHullShape(terrain_body, &terrain_shape_def, &box.base);
        }
    }
    // Bodies rain in from above the walls.
    m_physics_spawn_height = oy + 8.0f;

    m_physics_jolt_seed = 1;
    const std::size_t count = std::min<std::size_t>(32, static_cast<std::size_t>(level.cells()) * 3);
    for (std::size_t i = 0; i < count; ++i)
        add_physics_object(i % 4 != 0);
}

void editor_world::destroy_physics() noexcept
{
    if (B3_IS_NON_NULL(m_physics_world))
        b3DestroyWorld(m_physics_world);
    m_physics_world = b3_nullWorldId;
    m_physics_player = b3_nullBodyId;
    m_physics_static = b3_nullBodyId;
    m_physics_objects.clear();
    m_physics_walls.clear();
    m_physics_level.reset();
    m_physics_pending_impact.reset();
    m_physics_paused = false;
    m_physics_reset_requested = m_add_physics_block_requested = m_add_physics_sphere_requested =
        m_physics_jolt_requested = false;
}

void editor_world::release_physics_walls(const std::vector<arcade_block_change> &changes) noexcept
{
    if (!m_physics_level)
        return;
    for (const auto &change : changes)
    {
        if (change.block != 0)
            continue;
        const int key = (change.z - m_physics_level->world_z(0)) * m_physics_level->columns() +
                        (change.x - m_physics_level->world_x(0));
        if (const auto wall = m_physics_walls.find(key); wall != m_physics_walls.end())
        {
            b3DestroyShape(wall->second, true);
            m_physics_walls.erase(wall);
        }
    }
}

std::array<float, 3> editor_world::random_maze_spawn() noexcept
{
    const auto next = [this]
    {
        m_physics_jolt_seed = m_physics_jolt_seed * 1664525u + 1013904223u;
        return m_physics_jolt_seed >> 8;
    };
    const auto &level = *m_physics_level;
    for (int attempt = 0; attempt < 64; ++attempt)
    {
        const int r = static_cast<int>(next() % static_cast<unsigned>(level.rows()));
        const int c = static_cast<int>(next() % static_cast<unsigned>(level.columns()));
        if (level.is_wall(r, c) || (r == level.start_row() && c == level.start_column()) ||
            (r == level.goal_row() && c == level.goal_column()))
            continue;
        const float jitter_x = static_cast<float>(next() % 100) / 100.0f * 0.2f - 0.1f;
        const float jitter_z = static_cast<float>(next() % 100) / 100.0f * 0.2f - 0.1f;
        return {static_cast<float>(level.world_x(c)) + jitter_x,
                m_physics_spawn_height + static_cast<float>(next() % 300) / 100.0f,
                static_cast<float>(level.world_z(r)) + jitter_z};
    }
    return {static_cast<float>(level.world_x(level.goal_column() - 2)), m_physics_spawn_height,
            static_cast<float>(level.world_z(level.goal_row()))};
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
    const auto spawn = random_maze_spawn();
    body_def.position = {spawn[0], spawn[1], spawn[2]};
    const auto body = b3CreateBody(m_physics_world, &body_def);

    auto shape_def = b3DefaultShapeDef();
    shape_def.density = sphere ? 10.0f : 0.5f;
    shape_def.baseMaterial.friction = 0.25f;
    shape_def.baseMaterial.restitution = 0.12f;
    shape_def.enableHitEvents = true;
    if (sphere)
    {
        const b3Sphere circle{{0.0f, 0.0f, 0.0f}, ARCADE_BODY_SIZE};
        b3CreateSphereShape(body, &shape_def, &circle);
    }
    else
    {
        const auto box = b3MakeBoxHull(ARCADE_BODY_SIZE, ARCADE_BODY_SIZE, ARCADE_BODY_SIZE);
        b3CreateHullShape(body, &shape_def, &box.base);
    }
    constexpr std::array<std::array<float, 3>, 3> colors{{{0.95f, 0.37f, 0.16f}, {0.2f, 0.65f, 0.95f}, {0.88f, 0.72f, 0.2f}}};
    m_physics_objects.push_back({body, sphere, static_cast<int>(index % 6), colors[index % colors.size()]});
}

