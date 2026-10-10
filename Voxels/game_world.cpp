#include "game_world.h"
#include "geometries.h"
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
#include <noise/noise.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <set>
#include <stdexcept>

bool game_world::tracing_running() const noexcept
{
    return m_tracing && m_tracing->running;
}

void game_world::suspend_input() noexcept
{
    if (m_tracing)
        cancel_tracing_input();
}

const maze_game_model *game_world::tracing_board() const noexcept
{
    return m_tracing ? &m_tracing->board : nullptr;
}

const voxel_board_layout *game_world::tracing_layout() const noexcept
{
    return m_tracing ? &m_tracing->layout : nullptr;
}

const game_balls *game_world::tracing_balls() const noexcept
{
    return m_tracing ? &m_tracing->balls : nullptr;
}

void game_world::configure_tracing_board(const std::size_t side)
{
    if (!m_tracing)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Cannot configure a board outside tracing mode");
        throw std::logic_error("Tracing mode is not active");
    }
    if (side < 4 || side > maze_game_model::MAX_SIDE)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Board side must be between 4 and 40");
        throw std::invalid_argument("Board side must be between 4 and 40");
    }
    auto &game = *m_tracing;
    game.board = maze_game_model(game.rng(), true, side);
    game.selected_side = static_cast<int>(side);
    game.configuration_error.clear();
    cancel_tracing_input();
    game.layout = {};
    game.rendered.clear();
    game.rendered_level = 0;
    game.falling_vertices = 0;
    game.paused = false;
    sync_tracing_board();
}

void game_world::sync_tracing_board()
{
    auto &game = *m_tracing;
    const auto side = game.board.side();
    const auto &cells = game.board.cells();
    if (game.rendered_level != game.board.level() || game.rendered_side != side)
        game.balls.reset(game.board, game.rng);
    else if (game.rendered != cells)
        game.balls.sync_bricks(game.board);
    if (game.rendered_level != game.board.level())
    {
        if (game.rendered_level != 0 && !game.board.history().empty())
        {
            std::vector<float> falling;
            constexpr int indices[]{0, 1, 2, 0, 2, 3};
            for (const auto &[from, to] : game.board.history().back().lines)
            {
                const float ax = static_cast<float>(from % side), az = static_cast<float>(from / side);
                const float bx = static_cast<float>(to % side), bz = static_cast<float>(to / side);
                const float dx = bx - ax, dz = bz - az;
                const float length = std::hypot(dx, dz);
                const float nx = -dz / length * 0.05f, nz = dx / length * 0.05f;
                const float corners[4][2]{{ax + nx, az + nz}, {bx + nx, bz + nz}, {bx - nx, bz - nz}, {ax - nx, az - nz}};
                for (const int corner : indices)
                    falling.insert(falling.end(), {corners[corner][0], 7.55f, corners[corner][1],
                                                   0, 1, 0, 0, 0, 0, 2});
            }
            sdl_gl_helper::del_buffer(game.falling_buffer);
            game.falling_buffer = sdl_gl_helper::gen_buffer(falling.size() * sizeof(float), falling.data());
            game.falling_vertices = static_cast<int>(falling.size() / 10);
            game.fall_started = game.effect_time;
            game.transition_effect = TRANSITION_EFFECTS[std::uniform_int_distribution<std::size_t>(
                0, TRANSITION_EFFECTS.size() - 1)(game.rng)];
            game.transition_started = game.effect_time;
        }
        game.rendered.clear();
        game.rendered_level = game.board.level();
    }
    if (game.rendered_side != side)
    {
        delete_all_chunks();
        for (int p = 0; p <= static_cast<int>(side - 1) / BUILD_CHUNK_SIZE; ++p)
            for (int q = 0; q <= static_cast<int>(side - 1) / BUILD_CHUNK_SIZE; ++q)
            {
                auto *c = new chunk{};
                scene_graph_layers[static_cast<std::size_t>(Layer::BACKGROUND)][next_chunk_slot_in_view++] = c;
                init_chunk(c, p, q);
            }
        game.rendered.clear();
        game.rendered_side = side;
    }
    if (game.rendered == cells)
        return;

    using kind = maze_game_model::cell_kind;
    bool new_goal = game.rendered.empty();
    for (std::size_t i = 0; i < cells.size() && !new_goal; ++i)
        new_goal = cells[i] == kind::goal && game.rendered[i] != kind::goal;
    if (new_goal)
    {
        constexpr item::BlockType green_blocks[]{item::BlockType::GRASS, item::BlockType::LEAVES,
                                                 item::BlockType::CACTUS_1, item::BlockType::CACTUS_2};
        game.goal_block = static_cast<int>(green_blocks[std::uniform_int_distribution<int>(0, 3)(game.rng)]);
    }

    // Rebuild only what changed: lamps light every glass cube, so only their toggling forces a full
    // glass rebuild; otherwise just the changed cells' blocks, glass slots and nearby chunks update.
    const bool full = game.rendered.empty();
    const std::size_t count = cells.size();
    std::vector<std::size_t> changed;
    // A new goal re-rolls the goal block, which every goal cell must share.
    bool lamps_changed = full || new_goal;
    if (!full)
        for (std::size_t i = 0; i < count; ++i)
            if (game.rendered[i] != cells[i])
            {
                changed.push_back(i);
                lamps_changed |= game.rendered[i] == kind::completed || cells[i] == kind::completed;
            }
    std::vector<std::pair<float, float>> lamps;
    if (lamps_changed)
        for (std::size_t i = 0; i < count; ++i)
            if (cells[i] == kind::completed)
                lamps.emplace_back(static_cast<float>(i % side), static_cast<float>(i / side));
    constexpr std::size_t SLOT_FLOATS = 360;
    // Every (layer, cell) owns a fixed glass slot; non-glass slots stay zeroed (degenerate triangles).
    game.glass_data.resize(count * maze_game_model::VISIBLE_LEVELS * SLOT_FLOATS);
    const auto cell_at = [&](const std::size_t layer, const std::size_t i)
    { return layer == 0 ? cells[i] : game.board.previews()[layer - 1][i]; };
    const auto block_of = [&](const std::size_t layer, const kind cell)
    {
        switch (cell)
        {
        case kind::goal:
            return layer == 0 ? game.goal_block : static_cast<int>(item::BlockType::GRASS);
        case kind::good:
            return static_cast<int>(item::BlockType::EMPTY);
        case kind::bad:
            return static_cast<int>(item::BlockType::BRICK);
        case kind::completed:
            return static_cast<int>(item::BlockType::COLOR_00);
        default:
            return static_cast<int>(item::BlockType::GLASS);
        }
    };
    const auto place = [&](const std::size_t layer, const std::size_t i, const bool write_map)
    {
        const auto cell = cell_at(layer, i);
        const int x = static_cast<int>(i % side);
        const int z = static_cast<int>(i / side);
        const int y = 7 - static_cast<int>(layer) * 2;
        const int block = block_of(layer, cell);
        auto *slot = game.glass_data.data() + (layer * count + i) * SLOT_FLOATS;
        if (item::is_glass(block))
        {
            float ao[6][4]{}, light[6][4]{};
            float illumination = 0;
            for (const auto &[lamp_x, lamp_z] : lamps)
            {
                const float dx = lamp_x - x;
                const float dz = lamp_z - z;
                illumination += 0.6f / (1.0f + dx * dx + dz * dz + static_cast<float>(layer));
            }
            for (auto &face : light)
                std::fill(std::begin(face), std::end(face), std::min(illumination, 0.9f));
            geometries::make_cube(slot, ao, light, 1, 1, 1, 1, 1, 1,
                                  static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), 0.48f, block);
        }
        else
            std::fill(slot, slot + SLOT_FLOATS, 0.0f);
        if (write_map)
        {
            auto *c = find_chunk(x / BUILD_CHUNK_SIZE, z / BUILD_CHUNK_SIZE).value();
            c->map.set(x, y, z, item::is_glass(block) ? 0 : block);
            c->lights.set(x, y + 1, z, cell == kind::completed ? 5 : 0);
        }
    };

    if (lamps_changed)
    {
        for (std::size_t layer = 0; layer < maze_game_model::VISIBLE_LEVELS; ++layer)
            for (std::size_t i = 0; i < count; ++i)
                place(layer, i, full || layer == 0);
        sdl_gl_helper::del_buffer(game.glass_buffer);
        game.glass_buffer = sdl_gl_helper::gen_buffer(game.glass_data.size() * sizeof(float), game.glass_data.data());
    }
    else if (!changed.empty())
    {
        for (const auto i : changed)
            place(0, i, true);
        const auto first = changed.front() * SLOT_FLOATS;
        const auto last = (changed.back() + 1) * SLOT_FLOATS;
        glBindBuffer(GL_ARRAY_BUFFER, game.glass_buffer);
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(first * sizeof(float)),
                        static_cast<GLsizeiptr>((last - first) * sizeof(float)), game.glass_data.data() + first);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    game.glass_vertices = static_cast<int>(game.glass_data.size() / 10);

    // Light (reach 5) and AO cross chunk borders, so remesh chunks within that margin of a change.
    constexpr int reach = 6;
    const int last_chunk = static_cast<int>(side - 1) / BUILD_CHUNK_SIZE;
    std::set<std::pair<int, int>> dirty;
    if (full)
        for (int p = 0; p <= last_chunk; ++p)
            for (int q = 0; q <= last_chunk; ++q)
                dirty.emplace(p, q);
    else
        for (const auto i : changed)
        {
            const int x = static_cast<int>(i % side), z = static_cast<int>(i / side);
            for (int p = std::max(0, x - reach) / BUILD_CHUNK_SIZE; p <= std::min(last_chunk, (x + reach) / BUILD_CHUNK_SIZE); ++p)
                for (int q = std::max(0, z - reach) / BUILD_CHUNK_SIZE; q <= std::min(last_chunk, (z + reach) / BUILD_CHUNK_SIZE); ++q)
                    dirty.emplace(p, q);
        }
    for (const auto &[p, q] : dirty)
        gen_chunk_buffer(find_chunk(p, q).value());
    game.rendered = cells;
}

void game_world::refresh_tracing_layout() const
{
    auto &game = *m_tracing;
    const auto size = ImGui::GetIO().DisplaySize;
    if (game.layout.side != game.board.side())
        game.layout = voxel_board_layout::fit(size.x, size.y, game.board.side());
    else
        game.layout.resize(size.x, size.y);
}

void game_world::play_boink() const
{
    const auto hit = m_tracing->balls.last_hit();
    m_sounds->play_at(SoundIdentifier::BOINK, hit.x, game_balls::HEIGHT, hit.y);
}

void game_world::cancel_tracing_input()
{
    auto &game = *m_tracing;
    game.board.cancel();
    game.mouse_down = false;
    game.finger.reset();
    game.pointer.reset();
    game.swipe_pointer.reset();
    game.pan_origin.reset();
    game.touches.clear();
    game.swarm.clear();
    game.gesture_lockout = false;
    game.gesture_rotation_latched = false;
}

void game_world::trace_pointer(float x, float y, bool begin, bool release)
{
    auto &game = *m_tracing;
    if (game.paused || game.gesture_lockout)
        return;
    if (game.layout.extent > 0)
    {
        if (!begin && game.swipe_pointer)
        {
            const auto [px, py] = *game.swipe_pointer;
            const auto [ax, az] = game.layout.to_world(px, py);
            const auto [bx, bz] = game.layout.to_world(x, y);
            if (game.balls.swipe({ax, az}, {bx, bz}))
                play_boink();
        }
        if (release)
            game.swipe_pointer.reset();
        else
            game.swipe_pointer = {x, y};
    }
    const auto cell = game.layout.pick(x, y);
    if (cell)
    {
        if (begin)
            game.board.begin(*cell);
        else
            game.board.trace(*cell);
    }
    // Leaving the grid suspends cell visits, not the stroke or its release.
    if (release)
        game.board.release();
}

void game_world::latch_camera_gesture()
{
    auto &game = *m_tracing;
    const auto [ax, ay] = game.touches.begin()->second;
    const auto [bx, by] = std::next(game.touches.begin())->second;
    game.gesture_midpoint = {(ax + bx) * 0.5f, (ay + by) * 0.5f};
    game.gesture_distance = std::hypot(bx - ax, by - ay);
    game.gesture_angle = std::atan2(by - ay, bx - ax);
}

void game_world::update_camera_gesture()
{
    auto &game = *m_tracing;
    const auto [previous_x, previous_y] = game.gesture_midpoint;
    const float previous_distance = game.gesture_distance;
    const float previous_angle = game.gesture_angle;
    latch_camera_gesture();
    const auto [x, y] = game.gesture_midpoint;
    if (previous_distance > 1.0f && game.gesture_distance > 1.0f)
        game.layout.zoom_at(game.gesture_distance / previous_distance, x, y);
    const float delta = std::remainder(game.gesture_angle - previous_angle,
                                       2.0f * static_cast<float>(matrix::MAT_PI_VAL));
    const float degrees = matrix::to_degrees(delta);
    if (game.gesture_rotation_latched || std::abs(degrees) > 3.0f)
    {
        game.gesture_rotation_latched = true;
        game.layout.rotate_degrees(degrees);
    }
    game.layout.pan(x - previous_x, y - previous_y);
}

void game_world::handle_tracing_event(const SDL_Event &event)
{
    auto &game = *m_tracing;
    refresh_tracing_layout();
    switch (event.type)
    {
    case SDL_EVENT_QUIT:
        cancel_tracing_input();
        game.running = false;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        cancel_tracing_input();
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        game.pointer.reset();
        game.swipe_pointer.reset();
        break;
    case SDL_EVENT_KEY_DOWN:
        if (event.key.scancode == SDL_SCANCODE_ESCAPE && !event.key.repeat)
        {
            cancel_tracing_input();
            game.paused = !game.paused;
        }
        else if (!game.paused && !ImGui::GetIO().WantCaptureKeyboard)
        {
            switch (event.key.scancode)
            {
            case SDL_SCANCODE_LEFT:
                game.layout.pan(40, 0);
                break;
            case SDL_SCANCODE_RIGHT:
                game.layout.pan(-40, 0);
                break;
            case SDL_SCANCODE_UP:
                game.layout.pan(0, 40);
                break;
            case SDL_SCANCODE_DOWN:
                game.layout.pan(0, -40);
                break;
            case SDL_SCANCODE_Q:
                game.layout.rotate_degrees(-5);
                break;
            case SDL_SCANCODE_E:
                game.layout.rotate_degrees(5);
                break;
            case SDL_SCANCODE_R:
                game.layout.reset_camera();
                break;
            default:
                break;
            }
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.which == SDL_TOUCH_MOUSEID || game.paused || !game.touches.empty() ||
            !game.layout.contains(event.button.x, event.button.y) || ImGui::GetIO().WantCaptureMouse)
            break;
        game.pointer = {event.button.x, event.button.y};
        if (event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE)
        {
            game.pan_origin = game.pointer;
            game.pan_button = event.button.button;
        }
        else if (event.button.button == SDL_BUTTON_LEFT)
        {
            game.mouse_down = true;
            trace_pointer(event.button.x, event.button.y, true);
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (event.motion.which == SDL_TOUCH_MOUSEID || !game.touches.empty())
            break;
        game.pointer = {event.motion.x, event.motion.y};
        if (!game.paused && game.pan_origin)
        {
            const auto [x, y] = *game.pan_origin;
            game.layout.pan(event.motion.x - x, event.motion.y - y);
            game.pan_origin = game.pointer;
        }
        else if (game.mouse_down)
            trace_pointer(event.motion.x, event.motion.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.which == SDL_TOUCH_MOUSEID)
        {
            break;
        }
        if (event.button.button == game.pan_button)
        {
            game.pan_origin.reset();
        }
        if (event.button.button == SDL_BUTTON_LEFT && game.mouse_down)
        {
            trace_pointer(event.button.x, event.button.y, false, true);
            game.mouse_down = false;
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (!game.paused && game.touches.empty() &&
            game.layout.contains(event.wheel.mouse_x, event.wheel.mouse_y) &&
            !ImGui::GetIO().WantCaptureMouse)
        {
            const float delta = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
            if (SDL_GetModState() & SDL_KMOD_CTRL)
            {
                game.layout.rotate_degrees(delta * 5.0f);
            }
            else
            {
                game.layout.zoom_at(std::pow(1.12f, -delta), event.wheel.mouse_x, event.wheel.mouse_y);
            }
        }
        break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
    {
        if (event.tfinger.touchID == SDL_MOUSE_TOUCHID)
        {
            break;
        }
        const auto key = std::make_pair(event.tfinger.touchID, event.tfinger.fingerID);
        const auto size = ImGui::GetIO().DisplaySize;
        const float x = event.tfinger.x * size.x, y = event.tfinger.y * size.y;
        if (event.type == SDL_EVENT_FINGER_DOWN)
        {
            if (game.paused || game.mouse_down || game.pan_origin || !game.layout.contains(x, y) ||
                (game.touches.empty() && ImGui::GetIO().WantCaptureMouse))
            {
                break;
            }
            game.touches[key] = {x, y};
            if (game.touches.size() >= 2)
            {
                game.board.cancel();
                game.finger.reset();
                game.pointer.reset();
                game.gesture_lockout = true;
                game.gesture_rotation_latched = false;
                latch_camera_gesture();
            }
            else if (!game.gesture_lockout)
            {
                game.finger = event.tfinger.fingerID;
                game.pointer = {x, y};
                trace_pointer(x, y, true);
            }
            break;
        }
        const auto touch = game.touches.find(key);
        if (touch == game.touches.end())
        {
            break;
        }
        if (event.type == SDL_EVENT_FINGER_MOTION)
        {
            touch->second = {x, y};
            if (game.touches.size() >= 2)
            {
                update_camera_gesture();
            }
            else if (!game.gesture_lockout && game.finger == event.tfinger.fingerID)
            {
                game.pointer = {x, y};
                trace_pointer(x, y);
            }
        }
        else
        {
            if (!game.gesture_lockout && game.finger == event.tfinger.fingerID)
            {
                if (event.type == SDL_EVENT_FINGER_CANCELED)
                {
                    game.board.cancel();
                    game.swipe_pointer.reset();
                }
                else
                {
                    trace_pointer(x, y, false, true);
                }
                game.finger.reset();
            }
            game.touches.erase(touch);
            game.pointer.reset();
            if (game.touches.size() >= 2)
            {
                latch_camera_gesture();
            }
            else
            {
                game.gesture_rotation_latched = false;
            }
            if (game.touches.empty())
            {
                game.gesture_lockout = false;
            }
        }
        break;
    }
    default:
        break;
    }
}

void game_world::draw_tracing() const
{
    auto &game = *m_tracing;
    auto &&configs{active_player->get_configs()};
    const auto size = ImGui::GetIO().DisplaySize;
    refresh_tracing_layout();

    // Input is batched per frame: rebuild only after all scoring changes have arrived.
    const_cast<game_world *>(this)->sync_tracing_board();
    refresh_tracing_layout();
    const float new_center = static_cast<float>(game.board.side() - 1) * 0.5f;
    active_player->pos = {game.layout.camera_x, 20.0f, game.layout.camera_z, 0.0f,
                          -static_cast<float>(matrix::MAT_PI_VAL) / 2.0f, 0.0f};
    configs.ortho_scaling(game.layout.ortho);
    if (game.layout.extent <= 0)
        return;

    const auto [pixel_width, pixel_height] = simple_direct_medialayer->get_window_size_in_pixels();
    const float scale_x = pixel_width / size.x, scale_y = pixel_height / size.y;
    const int viewport_width = std::max(1, static_cast<int>(game.layout.width * scale_x));
    const int viewport_height = std::max(1, static_cast<int>(game.layout.height * scale_y));
    const bool bloom = active_player->get_configs().use_bloom_effect();
    // Fill-rate budget: scene, glass, fog and bloom render at no more than ~1.2M pixels and are
    // upscaled by the composite, so frame cost stays flat as the window grows.
    constexpr float PIXEL_BUDGET = 1.2e6f;
    const float render_scale = std::min(1.0f, std::sqrt(PIXEL_BUDGET / (static_cast<float>(viewport_width) * viewport_height)));
    const int render_width = std::max(1, static_cast<int>(viewport_width * render_scale));
    const int render_height = std::max(1, static_cast<int>(viewport_height * render_scale));
    if (!m_bloom->resize(render_width, render_height))
        return;
    m_bloom->bind_scene();
    glViewport(0, 0, render_width, render_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glStencilMask(0xFF);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    const bool environment = game.environment_enabled && game.environment.ready();
    if (environment)
        game.environment.draw(game.layout.rotation, game.layout.zoom,
                              (game.layout.camera_x - new_center) / game.layout.ortho,
                              (game.layout.camera_z - new_center) / game.layout.ortho,
                              game.layout.width / game.layout.extent,
                              game.layout.height / game.layout.extent,
                              game.effect_time, static_cast<float>(game.board.level()),
                              static_cast<float>(game.board.lights_activated()),
                              static_cast<float>(game.board.side()) * 0.5f / game.layout.ortho);
    else
        m_bloom->draw_depth_field(game.effect_time, static_cast<float>(game.board.level()),
                                  static_cast<float>(game.board.lights_activated()));
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(game.vao);
    float projection[16];
    matrix::set_3d(projection, 1, 1,
                   active_player->pos.x, active_player->pos.y, active_player->pos.z,
                   active_player->pos.rx, active_player->pos.ry, 60, game.layout.ortho, 2);
    float camera_transform[16];
    matrix::rotate(camera_transform, 0, 0, 1, -game.layout.rotation);
    matrix::multiply(projection, camera_transform, projection);
    matrix::identity(camera_transform);
    camera_transform[0] = camera_transform[5] = game.layout.zoom;
    matrix::multiply(projection, camera_transform, projection);
    matrix::identity(camera_transform);
    camera_transform[0] = game.layout.extent / game.layout.width;
    camera_transform[5] = game.layout.extent / game.layout.height;
    matrix::multiply(projection, camera_transform, projection);
    glUseProgram(s_block_attrib.program);
    glUniform1f(game.bloom_emission, bloom ? 0.35f : 0.0f);
    glUniform1f(game.light_attenuation, 0.75f);
    glUniform1f(game.block_taper, 0.025f);
    glUniform1f(game.block_center, new_center);
    glUniformMatrix4fv(s_block_attrib.matrix, 1, GL_FALSE, projection);
    glUniform3f(s_block_attrib.camera, active_player->pos.x, active_player->pos.y, active_player->pos.z);
    glUniform1f(s_block_attrib.extra2, 0.8f);
    glUniform1f(s_block_attrib.extra3, 10000.0f);
    glUniform1i(s_block_attrib.extra4, 1);
    glUniform1f(s_block_attrib.timer, 0.25f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, world_textures.get(TextureIdentifier::ATLAS).gl_texture);
    glUniform1i(s_block_attrib.sampler, 0);
    std::array<float, game_balls::MAX_BALLS * 4> ball_spheres{}, ball_surfaces{};
    const auto ball_count = static_cast<int>(game.balls.pack(ball_spheres, ball_surfaces));
    glUniform1i(game.block_ball_count, ball_count);
    glUniform4fv(game.block_ball_spheres, ball_count, ball_spheres.data());
    glUniform4fv(game.block_ball_surfaces, ball_count, ball_surfaces.data());
    for (std::size_t i = 1; i < next_chunk_slot_in_view; ++i)
    {
        const auto *c = scene_graph_layers[static_cast<std::size_t>(Layer::BACKGROUND)][i];
        if (c->faces > 0)
            sdl_gl_helper::draw_chunk(&s_block_attrib, c);
    }
    // Stencil mask: re-draw only the visible block tops (no color/depth writes) to mark where reflections show.
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glStencilMask(0xFF);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glUniform1i(game.block_mirror_mask, 1);
    for (std::size_t i = 1; i < next_chunk_slot_in_view; ++i)
    {
        const auto *c = scene_graph_layers[static_cast<std::size_t>(Layer::BACKGROUND)][i];
        if (c->faces > 0)
            sdl_gl_helper::draw_chunk(&s_block_attrib, c);
    }
    glUniform1i(game.block_mirror_mask, 0);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glStencilMask(0x00);
    glDisable(GL_STENCIL_TEST);
    // The block program is shared with other worlds, so leave it without ball occluders.
    glUniform1i(game.block_ball_count, 0);
    // Block tops sit at y = 7.5, which is where the balls rest.
    game.environment.draw_reflected_balls(game.balls, projection, environment, 7.5f, 0.35f);
    game.environment.draw_balls(game.balls, projection, environment);
    glBindVertexArray(game.vao);
    m_bloom->bind_glass();
    glUseProgram(game.glass_shader.get());
    game.environment.bind();
    glUniform1i(game.glass_environment, 2);
    glUniform1f(game.glass_environment_strength, environment ? 0.18f : 0.0f);
    glUniform3f(game.glass_environment_center, new_center, 5.0f, new_center);
    glUniform1f(game.glass_environment_extent, static_cast<float>(game.board.side()) * 0.5f + 4.0f);
    glUniformMatrix4fv(game.glass_attrib.matrix, 1, GL_FALSE, projection);
    glUniform1f(game.glass_time, game.effect_time);
    glUniform1f(game.glass_taper, 0.025f);
    glUniform1f(game.glass_center, new_center);
    glUniform1i(game.glass_ball_count, ball_count);
    glUniform4fv(game.glass_ball_spheres, ball_count, ball_spheres.data());
    glUniform4fv(game.glass_ball_surfaces, ball_count, ball_surfaces.data());
    glUniform1f(game.glass_fall, std::clamp(game.effect_time - game.fall_started, 0.0f, 1.0f));
    sdl_gl_helper::draw_triangles_3d_ao(&game.glass_attrib, game.glass_buffer, game.glass_vertices);
    glDisable(GL_CULL_FACE);
    if (game.falling_vertices && game.effect_time - game.fall_started < 1.0f)
        sdl_gl_helper::draw_triangles_3d_ao(&game.glass_attrib, game.falling_buffer, game.falling_vertices);
    // Traced strokes are scene geometry (uv.w 3 active, 4 saved) so they share glass, shadows and bloom.
    static thread_local std::vector<float> strokes;
    strokes.clear();
    const auto add_edges = [&](const std::set<maze_game_model::edge> &edges, const float kind, const float half_width)
    {
        constexpr int indices[]{0, 1, 2, 0, 2, 3};
        const auto side = game.board.side();
        for (const auto &[from, to] : edges)
        {
            const float ax = static_cast<float>(from % side), az = static_cast<float>(from / side);
            const float bx = static_cast<float>(to % side), bz = static_cast<float>(to / side);
            const float length = std::hypot(bx - ax, bz - az);
            if (length <= 0.0f)
                continue;
            const float tx = (bx - ax) / length * half_width, tz = (bz - az) / length * half_width;
            const float nx = -tz, nz = tx;
            const float sx = ax - tx, sz = az - tz, ex = bx + tx, ez = bz + tz;
            const float corners[4][3]{{sx + nx, sz + nz, 1}, {ex + nx, ez + nz, 1},
                                      {ex - nx, ez - nz, -1}, {sx - nx, sz - nz, -1}};
            for (const int corner : indices)
                strokes.insert(strokes.end(), {corners[corner][0], 7.56f, corners[corner][1],
                                               0, 1, 0, corners[corner][2], 0, 0, kind});
        }
    };
    add_edges(game.board.saved_edges(), 4.0f, 0.075f);
    add_edges(game.board.active_edges(), 3.0f, 0.1f);
    game.swarm.append_vertices(strokes, game.effect_time);
    game.line_vertices = static_cast<int>(strokes.size() / 10);
    if (game.line_vertices)
    {
        // One persistent streaming buffer: orphaned and refilled instead of deleted and recreated per frame.
        if (!game.line_buffer)
            glGenBuffers(1, &game.line_buffer);
        glBindBuffer(GL_ARRAY_BUFFER, game.line_buffer);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(strokes.size() * sizeof(float)), strokes.data(),
                     GL_STREAM_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        sdl_gl_helper::draw_triangles_3d_ao(&game.glass_attrib, game.line_buffer, game.line_vertices);
    }
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
    m_bloom->resolve_glass();
    {
        // Fog is world-anchored: the radius follows the grid size, and zoom, pan and resize
        // reach the shader through the layout uniforms every frame.
        const auto &fog = game.fog_uniforms;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(game.fog_shader.get());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, game.fog_texture);
        glUniform1i(fog[0], 0);
        glUniform2f(fog[1], game.layout.width, game.layout.height);
        glUniform1f(fog[2], game.layout.pitch());
        glUniform1f(fog[3], game.layout.rotation);
        glUniform2f(fog[4], game.layout.camera_x, game.layout.camera_z);
        glUniform2f(fog[5], new_center, new_center);
        glUniform1f(fog[6], static_cast<float>(game.board.side()) * 0.5f + 1.0f);
        glUniform1f(fog[7], game.effect_time);
        glUniform1f(fog[8], game.fog_enabled ? 1.0f : 0.0f);
        m_bloom->draw_quad();
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
    }
    glViewport(static_cast<int>(game.layout.left * scale_x),
               pixel_height - static_cast<int>((game.layout.top + game.layout.height) * scale_y),
               viewport_width, viewport_height);
    m_bloom->execute(bloom ? 0.12f : 0.0f, 2, game.transition_effect,
                     (game.effect_time - game.transition_started) / tracing_state::TRANSITION_SECONDS,
                     game.effect_time);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, pixel_width, pixel_height);

    auto *overlay = ImGui::GetBackgroundDrawList();
    overlay->PushClipRect(ImVec2(game.layout.left, game.layout.top),
                          ImVec2(game.layout.left + game.layout.width, game.layout.top + game.layout.height), true);
    const float pitch = game.layout.pitch();
    for (std::size_t i = 0; i < game.board.cells().size(); ++i)
    {
        const auto kind = game.board.cells()[i];
        const char *label = nullptr;
        if (kind == maze_game_model::cell_kind::start)
            label = "S";
        if (kind == maze_game_model::cell_kind::goal)
            label = "+10";
        if (kind == maze_game_model::cell_kind::bad)
            label = "-2";
        if (kind == maze_game_model::cell_kind::completed)
            label = "LIGHT";
        if (!label)
            continue;
        const auto [x, y] = game.layout.center(i);
        if (kind == maze_game_model::cell_kind::goal)
        {
            const float column = static_cast<float>(i % game.board.side());
            const float row = static_cast<float>(i / game.board.side());
            for (const auto &[dx, dz] : std::array<std::pair<float, float>, 4>{
                     {{-0.48f, -0.48f}, {0.48f, -0.48f}, {0.48f, 0.48f}, {-0.48f, 0.48f}}})
            {
                const auto [px, py] = game.layout.to_screen(column + dx, row + dz);
                overlay->PathLineTo(ImVec2(px, py));
            }
            overlay->PathStroke(IM_COL32(30, 255, 90, 255), ImDrawFlags_Closed, 2);
        }
        if (pitch >= 24)
        {
            const auto text_size = ImGui::CalcTextSize(label);
            overlay->AddText(ImVec2(x - text_size.x * 0.5f + 1, y - text_size.y * 0.5f + 1),
                             IM_COL32(0, 0, 0, 255), label);
            overlay->AddText(ImVec2(x - text_size.x * 0.5f, y - text_size.y * 0.5f),
                             IM_COL32(255, 255, 255, 255), label);
        }
    }
    overlay->PopClipRect();
}

void game_world::draw_menu_options()
{
    auto &game = *m_tracing;
    ImGui::Text("Score: %lld   Streak: %d   Board: %zu x %zu",
                static_cast<long long>(game.board.score()), game.board.streak(),
                game.board.side(), game.board.side());
    ImGui::Text("Level: %zu   Lights: %d / %d", game.board.level(),
                game.board.lights_activated(), maze_game_model::LIGHTS_PER_LEVEL);

    if (ImGui::Button(game.paused ? "Resume Game" : "Pause Game"))
    {
        cancel_tracing_input();
        game.paused = !game.paused;
    }
    ImGui::SameLine();
    if (ImGui::Button("New Game"))
    {
        game.board.reset();
        cancel_tracing_input();
        game.layout.reset_camera();
        game.paused = false;
        game.rendered.clear();
        game.rendered_level = 0;
        game.falling_vertices = 0;
        game.selected_side = static_cast<int>(game.board.side());
        game.configuration_error.clear();
    }

    ImGui::Checkbox("Radial fog", &game.fog_enabled);
    ImGui::BeginDisabled(!game.environment.ready());
    ImGui::Checkbox("Maze environment", &game.environment_enabled);
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(100.f);
    ImGui::InputInt("Board side", &game.selected_side, 0, 0);
    if (ImGui::Button("Apply / reset board"))
    {
        if (game.selected_side >= 4 && game.selected_side <= static_cast<int>(maze_game_model::MAX_SIDE))
            configure_tracing_board(static_cast<std::size_t>(game.selected_side));
        else
        {
            game.configuration_error = "Board side must be between 4 and 40.";
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Board side must be between 4 and 40");
        }
    }
    if (!game.configuration_error.empty())
        ImGui::TextWrapped("%s", game.configuration_error.c_str());
    else if (game.paused)
        ImGui::TextUnformatted("Game paused.");
    else
        ImGui::TextWrapped("%s", game.board.message().c_str());
    if (!game.environment_error.empty())
        ImGui::TextWrapped("Environment unavailable: %s", game.environment_error.c_str());

    ImGui::Separator();
    ImGui::TextWrapped("Blue glass reveals two lower levels. Rewards open holes (+5); red costs 2; green gives 10 and lights yellow. Keep holding to descend.");
    ImGui::TextWrapped("Hold left mouse / one finger: trace. Arrows / right or middle drag: pan. Wheel / pinch: zoom. Q/E, Ctrl+wheel or two-finger twist: rotate. R: reset camera. Hover near viewport edges to pan; off-grid strokes stay active.");
}

game_world::game_world(SDL_Window *window, font_manager &fonts, player *p,
                       shader_manager &shaders, texture_manager &textures,
                       const sdl_gl_helper *sdl, bloom_pass *bloom)
    : world{window, fonts, p, shaders, textures, sdl, bloom},
      m_sounds{soundfx::instance()}
{
    if (active_player)
    {
        active_player->set_world(this);
    }  
}

game_world::~game_world()
{
    if (m_tracing)
    {
        m_sounds->stop(SoundIdentifier::GAME_THEME_SONG_1);
        m_sounds->stop(SoundIdentifier::BOINK);
        glDeleteVertexArrays(1, &m_tracing->vao);
        sdl_gl_helper::del_buffer(m_tracing->glass_buffer);
        sdl_gl_helper::del_buffer(m_tracing->falling_buffer);
        sdl_gl_helper::del_buffer(m_tracing->line_buffer);
        glDeleteTextures(1, &m_tracing->fog_texture);
    }
    if (active_player)
    {
        active_player->set_world(nullptr);
    }  
}

void game_world::init() noexcept
{
    auto &&configs{active_player->get_configs()};

    init_voxel_engine();
    m_tracing = std::make_unique<tracing_state>();
    glGenVertexArrays(1, &m_tracing->vao);
    configs.view_mode(player_configs::ViewMode::ISOMETRIC).show_ghosts(false).show_crosshair_details(false);
    m_tracing->bloom_emission = glGetUniformLocation(s_block_attrib.program, "bloom_emission");
    m_tracing->light_attenuation = glGetUniformLocation(s_block_attrib.program, "light_attenuation");
#if defined(__EMSCRIPTEN__)
    m_tracing->glass_shader.load_program("Shaders/es/block_vertex.es.glsl", "Shaders/es/glass_fragment.es.glsl");
#else
    m_tracing->glass_shader.load_program("Shaders/block_vertex.glsl", "Shaders/glass_fragment.glsl");
#endif
    auto &game = *m_tracing;
    game.environment.init_balls();
    try
    {
        game.environment.init(game.board, game.rng());
    }
    catch (const std::exception &error)
    {
        game.environment_error = error.what();
        game.environment_enabled = false;
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Game maze environment: %s", error.what());
    }
#if defined(__EMSCRIPTEN__)
    game.fog_shader.load_program("Shaders/es/bloom_quad_vertex.es.glsl", "Shaders/es/radial_fog_fragment.es.glsl");
#else
    game.fog_shader.load_program("Shaders/bloom_quad_vertex.glsl", "Shaders/radial_fog_fragment.glsl");
#endif
    {
        constexpr const char *names[]{"cloud_noise", "view_size", "pitch", "rotation", "camera",
                                      "board_center", "fog_radius", "seconds", "density"};
        for (std::size_t i = 0; i < std::size(names); ++i)
            game.fog_uniforms[i] = game.fog_shader.uniform(names[i]);
        // Seamlessly tiling cloud noise: fractal simplex blended across its own edges.
        constexpr int N = 128;
        constexpr float scale = 4.0f;
        std::vector<std::uint8_t> pixels(N * N);
        const auto sample = [&](const float x, const float y)
        { return simplex2(x * scale, y * scale, 4, 0.5f, 2.0f) * 0.5f + 0.5f; };
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x)
            {
                const float u = static_cast<float>(x) / N, v = static_cast<float>(y) / N;
                const float value = sample(u, v) * (1 - u) * (1 - v) + sample(u - 1, v) * u * (1 - v) +
                                    sample(u, v - 1) * (1 - u) * v + sample(u - 1, v - 1) * u * v;
                pixels[static_cast<std::size_t>(y * N + x)] =
                    static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
            }
        glGenTextures(1, &game.fog_texture);
        glBindTexture(GL_TEXTURE_2D, game.fog_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, N, N, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }
    game.glass_attrib.program = game.glass_shader.get();
    game.glass_attrib.position = 0;
    game.glass_attrib.normal = 1;
    game.glass_attrib.uv = 2;
    game.glass_attrib.matrix = game.glass_shader.uniform("matrix");
    game.glass_time = game.glass_shader.uniform("seconds");
    game.glass_fall = game.glass_shader.uniform("fall_progress");
    game.glass_environment = game.glass_shader.uniform("environment_map");
    game.glass_environment_strength = game.glass_shader.uniform("environment_strength");
    game.glass_environment_center = game.glass_shader.uniform("environment_center");
    game.glass_environment_extent = game.glass_shader.uniform("environment_extent");
    game.glass_ball_count = game.glass_shader.uniform("ball_count");
    game.glass_ball_spheres = game.glass_shader.uniform("spheres[0]");
    game.glass_ball_surfaces = game.glass_shader.uniform("surfaces[0]");
    game.block_ball_count = glGetUniformLocation(s_block_attrib.program, "ball_count");
    game.block_mirror_mask = glGetUniformLocation(s_block_attrib.program, "mirror_mask");
    game.block_ball_spheres = glGetUniformLocation(s_block_attrib.program, "spheres[0]");
    game.block_ball_surfaces = glGetUniformLocation(s_block_attrib.program, "surfaces[0]");
    game.glass_taper = game.glass_shader.uniform("stack_taper");
    game.glass_center = game.glass_shader.uniform("stack_center");
    game.block_taper = world_shaders.get(ShaderIdentifier::BLOCK_SHADER).uniform("stack_taper");
    game.block_center = world_shaders.get(ShaderIdentifier::BLOCK_SHADER).uniform("stack_center");
    sync_tracing_board();
    m_sounds->set_spatialization(12.0f, 0.25f);
    m_sounds->play(SoundIdentifier::GAME_THEME_SONG_1);
}

void game_world::update(const float delta_time, mazes::randomizer &) noexcept
{
    auto &game = *m_tracing;
    refresh_tracing_layout();
    if (!game.paused && !game.gesture_lockout && !game.pan_origin && game.pointer)
    {
        const auto [x, y] = *game.pointer;
        if (game.layout.contains(x, y) && !ImGui::GetIO().WantCaptureMouse)
        {
            const float fx = voxel_board_layout::edge_factor(x - game.layout.left, game.layout.width);
            const float fy = voxel_board_layout::edge_factor(y - game.layout.top, game.layout.height);
            const float distance = voxel_board_layout::EDGE_SPEED * std::max(0.0f, delta_time) / 1000.0f;
            game.layout.pan(-fx * distance, -fy * distance);
            if (game.mouse_down || game.finger)
                trace_pointer(x, y);
        }
    }
    if (!m_tracing->paused)
    {
        m_tracing->effect_time += std::max(0.0f, delta_time) / 1000.0f;
    }
    // The listener hovers over the camera focus so panning moves the soundstage with the view.
    m_sounds->set_listener_position(game.layout.camera_x, active_player->pos.y, game.layout.camera_z);
    sync_tracing_board();
    std::optional<game_swarm::point> swarm_head;
    if (!game.paused && !game.gesture_lockout && game.layout.extent > 0 && game.swipe_pointer &&
        (game.mouse_down || game.finger))
        swarm_head = game.layout.to_world(game.swipe_pointer->first, game.swipe_pointer->second);
    game.swarm.update(std::max(0.0f, delta_time) / 1000.0f, swarm_head, game.rng);
    if (!game.paused)
    {
        if (game.balls.update(delta_time / 1000.0f))
            play_boink();
    }
}

void game_world::handle_event(const SDL_Event &event) noexcept
{
    handle_tracing_event(event);
}

void game_world::draw() const noexcept
{
    draw_tracing();
}
