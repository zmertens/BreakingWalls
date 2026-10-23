#include "player.h"

#include <chrono>
#include <cmath>
#include <ranges>
#include <sstream>

#include "db.h"
#include "geometries.h"
#include "item.h"
#include "matrix.h"
#include "texture.h"
#include "editor_world.h"

#include <SDL3/SDL.h>

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <glad/gl.h>
#endif

constexpr auto SCROLL_THRESHOLD = 0.1f;
constexpr auto ORTHO_MIN_SCALE = 8;
constexpr auto ORTHO_MAX_SCALE = 96;

player::player()
    : _is_active{true}, _is_on_ground{false}, _is_flying{false}, _is_auto_running{false}, player_buffer{}, current_item_index{0}, selected_font_index{0}, current_world{nullptr}
{
    // Movement key bindings
    assign_key(PlayerAction::MOVE_LEFT, SDL_SCANCODE_A);
    assign_key(PlayerAction::MOVE_RIGHT, SDL_SCANCODE_D);
    assign_key(PlayerAction::MOVE_FORWARD, SDL_SCANCODE_W);
    assign_key(PlayerAction::MOVE_BACKWARD, SDL_SCANCODE_S);
    assign_key(PlayerAction::MOVE_AUTO, SDL_SCANCODE_Q);
    assign_key(PlayerAction::MOVE_UP, SDL_SCANCODE_F);
    assign_key(PlayerAction::MOVE_DOWN, SDL_SCANCODE_LSHIFT);
    assign_key(PlayerAction::JUMP, SDL_SCANCODE_SPACE);
    assign_key(PlayerAction::FLY, SDL_SCANCODE_TAB);
    assign_key(PlayerAction::PLACE_LIGHT, SDL_SCANCODE_LCTRL);
    assign_key(PlayerAction::TAG_SIGN, SDL_SCANCODE_T);
    assign_key(PlayerAction::COPY_BLOCK, SDL_SCANCODE_C);
    assign_key(PlayerAction::CHANGE_PERSPECTIVE, SDL_SCANCODE_O);
    assign_key(PlayerAction::ZOOM_IN_ISO_VIEW, SDL_SCANCODE_EQUALS);
    assign_key(PlayerAction::ZOOM_OUT_ISO_VIEW, SDL_SCANCODE_MINUS);

    initialize_actions();
}

void player::handle_event(const SDL_Event &event, command_queue &commands) noexcept
{
    if (event.type == SDL_EVENT_QUIT)
    {
        _is_active = false;
    }

    if (event.type == SDL_EVENT_MOUSE_WHEEL)
    {
        constexpr std::int32_t MAX_ITEM_INDEX = item::TOTAL_ITEMS - 1;

        if (event.wheel.y > SCROLL_THRESHOLD)
        {
            // Scroll up (backward through items)
            if (current_item_index > 0)
            {
                current_item_index--;
            }
            else
            {
                current_item_index = MAX_ITEM_INDEX; // Wrap to last valid item
            }
        }
        else if (event.wheel.y < -SCROLL_THRESHOLD)
        {
            // Scroll down (forward through items)
            if (current_item_index < MAX_ITEM_INDEX)
            {
                current_item_index++;
            }
            else
            {
                current_item_index = 0; // Wrap to first item
            }
        }
    }
    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        switch (event.key.scancode)
        {
        case SDL_SCANCODE_O:
        {
            const char *view_name = "Perspective";
            switch (configs.view_mode())
            {
            case player_configs::ViewMode::ISOMETRIC:
                configs.view_mode(player_configs::ViewMode::ORTHO).ortho_scaling(ORTHO_MIN_SCALE);
                view_name = "Ortho View";
                break;
            case player_configs::ViewMode::ORTHO:
                configs.view_mode(player_configs::ViewMode::PERSPECTIVE).ortho_scaling(0);
                break;
            case player_configs::ViewMode::PERSPECTIVE:
                configs.view_mode(player_configs::ViewMode::ISOMETRIC).ortho_scaling(ORTHO_MIN_SCALE);
                view_name = "Isometric View";
                break;
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "View Mode: %s", view_name);
            return;
        }
        case SDL_SCANCODE_EQUALS:
        case SDL_SCANCODE_KP_PLUS:
            if (configs.view_mode() == player_configs::ViewMode::ISOMETRIC)
            {
                configs.ortho_scaling(SDL_max(ORTHO_MIN_SCALE, configs.ortho_scaling() - 2));
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Isometric Zoom: %d", configs.ortho_scaling());
                return;
            }
            break;
        case SDL_SCANCODE_MINUS:
        case SDL_SCANCODE_KP_MINUS:
            if (configs.view_mode() == player_configs::ViewMode::ISOMETRIC)
            {
                configs.ortho_scaling(SDL_min(ORTHO_MAX_SCALE, configs.ortho_scaling() + 2));
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Isometric Zoom: %d", configs.ortho_scaling());
                return;
            }
            break;
        default:
            break;
        }

        // Handle regular key bindings
        if (const auto found = keyboard_bindings.find(event.key.scancode);
            found != keyboard_bindings.cend() && !is_realtime_action(found->second))
        {
            const auto binding = player_commands.find(found->second);
            if (binding == player_commands.cend() || !binding->second.action)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Ignoring unmapped player action %d\n",
                            static_cast<int>(found->second));
                return;
            }

            if (found->second == PlayerAction::JUMP)
            {
                // Only allow jumping when on ground
                if (_is_on_ground)
                {
                    commands.push(binding->second);
                }
                return;
            }
            commands.push(binding->second);
        }
    }
    if (event.type == SDL_EVENT_KEY_UP)
    {
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            if (const auto binding = player_commands.find(PlayerAction::DESTROY_BLOCK);
                binding != player_commands.cend() && binding->second.action)
            {
                commands.push(binding->second);
            }
        }
        else if (event.button.button == SDL_BUTTON_RIGHT)
        {
            if (const auto binding = player_commands.find(PlayerAction::BUILD_BLOCK);
                binding != player_commands.cend() && binding->second.action)
            {
                commands.push(binding->second);
            }
        }
        else if (event.button.button == SDL_BUTTON_MIDDLE)
        {
            if (const auto binding = player_commands.find(PlayerAction::COPY_BLOCK);
                binding != player_commands.cend() && binding->second.action)
            {
                commands.push(binding->second);
            }
        }
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        constexpr float MOUSE_SENSITIVITY = 0.0025f;
        position *player_pos = &this->pos;
        player_pos->rx += event.motion.xrel * MOUSE_SENSITIVITY;

        if (this->configs.invert_mouse())
        {
            player_pos->ry += event.motion.yrel * MOUSE_SENSITIVITY;
        }
        else
        {
            player_pos->ry -= event.motion.yrel * MOUSE_SENSITIVITY;
        }

        // Keep rotation within bounds
        if (player_pos->rx < 0)
        {
            player_pos->rx += matrix::to_radians(360.0f);
        }
        if (player_pos->rx >= matrix::to_radians(360.0f))
        {
            player_pos->rx -= matrix::to_radians(360.0f);
        }
        player_pos->ry = SDL_max(player_pos->ry, -matrix::to_radians(90.0f));
        player_pos->ry = SDL_min(player_pos->ry, matrix::to_radians(90.0f));
    }
}

void player::update([[maybe_unused]] float delta_time, [[maybe_unused]] mazes::randomizer &rng) noexcept
{
}

void player::draw() const noexcept
{
}

void player::handle_realtime_input(command_queue &commands)
{
    int numKeys = 0;
    const auto *keyState = SDL_GetKeyboardState(&numKeys);

    if (_is_auto_running)
    {
        if (const auto binding = player_commands.find(PlayerAction::MOVE_FORWARD);
            binding != player_commands.cend() && binding->second.action)
        {
            commands.push(binding->second);
        }
    }

    // Process all realtime action keys
    for (auto &[id, action] : keyboard_bindings)
    {
        if (is_realtime_action(action))
        {
            // Skip JUMP if not flying
            if (action == PlayerAction::JUMP && !_is_flying)
            {
                continue;
            }

            if (action == PlayerAction::MOVE_FORWARD && _is_auto_running)
            {
                continue;
            }

            // Check if the key is currently pressed
            if (keyState && id < static_cast<std::uint32_t>(numKeys) && keyState[id])
            {
                if (const auto binding = player_commands.find(action);
                    binding != player_commands.cend() && binding->second.action)
                {
                    commands.push(binding->second);
                }
                // Check for disablement
                if (_is_auto_running &&
                    (action == PlayerAction::MOVE_LEFT ||
                     action == PlayerAction::MOVE_RIGHT ||
                     action == PlayerAction::MOVE_BACKWARD))
                {
                    _is_auto_running = false;
                }
            }
        }
    }
}

void player::assign_key(const PlayerAction action, const std::uint32_t key)
{
    std::erase_if(keyboard_bindings, [action](const auto &kv)
                  { return kv.second == action; });
    keyboard_bindings.insert_or_assign(key, action);
}

[[nodiscard]] std::uint32_t player::get_assigned_key(const PlayerAction action) const
{
    const auto it = std::ranges::find_if(keyboard_bindings,
                                         [action](const auto &kv)
                                         { return kv.second == action; });
    return it != keyboard_bindings.end() ? it->first : static_cast<std::uint32_t>(SDL_SCANCODE_UNKNOWN);
}

bool player::is_active() const noexcept
{
    return _is_active;
}

void player::set_active(bool active) noexcept
{
    this->_is_active = active;
}

bool player::is_flying() const noexcept
{
    return _is_flying;
}

void player::set_flying(const bool flying) noexcept
{
    this->_is_flying = flying;
}

bool player::is_on_ground() const noexcept
{
    return _is_on_ground;
}

void player::set_on_ground(const bool grounded) noexcept
{
    this->_is_on_ground = grounded;
}

std::uint32_t player::get_buffer() const noexcept
{
    return this->player_buffer;
}

void player::set_buffer(const std::uint32_t value) noexcept
{
    this->player_buffer = value;
}

std::int32_t player::get_item() const noexcept
{
    if (this->current_item_index >= 0 && this->current_item_index < item::ITEMS.size())
    {
        return item::ITEMS.at(this->current_item_index);
    }
    return -1;
}

void player::set_item(const std::int32_t value) noexcept
{
    if (value >= 0 && value < item::ITEMS.size())
    {
        this->current_item_index = value;
    }
}

void player::set_world(world *w) noexcept
{
    current_world = w;
}

std::string player::get_local_time() const noexcept
{
    if (!current_world)
    {
        return "00:00";
    }

    // time_of_day() returns 0.0-1.0 representing position in the day cycle
    const float time_fraction = current_world->time_of_day();
    const float total_hours = time_fraction * 24.0f;

    // Extract hours and minutes
    int hour = static_cast<int>(total_hours);
    const int minute = static_cast<int>((total_hours - static_cast<float>(hour)) * 60.0f);

    // Convert to 12-hour format
    const auto am_pm = hour < 12 ? "am" : "pm";
    hour = hour % 12;
    // Convert 0 to 12 for midnight/noon
    hour = hour ? hour : 12;

    const auto hour_str = std::to_string(hour);
    const auto formatted_time = hour_str + ":" + (minute < 10 ? "0" : "") + std::to_string(minute) + am_pm;

    return formatted_time;
}

void player::initialize_actions()
{
    // Movement parameters for smooth interpolation
    constexpr float max_move_speed = 5.0f;
    constexpr float acceleration = 0.2f;

    player_commands[PlayerAction::MOVE_BACKWARD].action = derived_action<player>(
        [](player &p, const float dt, mazes::randomizer &rng)
        {
            const float target_vx = -SDL_sinf(p.pos.rx) * max_move_speed;
            const float target_vz = SDL_cosf(p.pos.rx) * max_move_speed;

            p.vel.vx = matrix::lerp(p.vel.vx, target_vx, acceleration);
            p.vel.vz = matrix::lerp(p.vel.vz, target_vz, acceleration);

            const float dt_seconds = dt / 1000.0f;
            p.pos.x += p.vel.vx * dt_seconds;
            p.pos.z += p.vel.vz * dt_seconds;
        });

    player_commands[PlayerAction::MOVE_FORWARD].action = derived_action<player>(
        [](player &p, const float dt, mazes::randomizer &rng)
        {
            const float target_vx = SDL_sinf(p.pos.rx) * max_move_speed;
            const float target_vz = -SDL_cosf(p.pos.rx) * max_move_speed;

            p.vel.vx = matrix::lerp(p.vel.vx, target_vx, acceleration);
            p.vel.vz = matrix::lerp(p.vel.vz, target_vz, acceleration);

            const float dt_seconds = dt / 1000.0f;
            p.pos.x += p.vel.vx * dt_seconds;
            p.pos.z += p.vel.vz * dt_seconds;
        });

    player_commands[PlayerAction::MOVE_LEFT].action = derived_action<player>(
        [](player &p, const float dt, mazes::randomizer &rng)
        {
            const float target_vx = -SDL_cosf(p.pos.rx) * max_move_speed;
            const float target_vz = -SDL_sinf(p.pos.rx) * max_move_speed;

            p.vel.vx = matrix::lerp(p.vel.vx, target_vx, acceleration);
            p.vel.vz = matrix::lerp(p.vel.vz, target_vz, acceleration);

            const float dt_seconds = dt / 1000.0f;
            p.pos.x += p.vel.vx * dt_seconds;
            p.pos.z += p.vel.vz * dt_seconds;
        });

    player_commands[PlayerAction::MOVE_RIGHT].action = derived_action<player>(
        [](player &p, const float dt, mazes::randomizer &rng)
        {
            const float target_vx = SDL_cosf(p.pos.rx) * max_move_speed;
            const float target_vz = SDL_sinf(p.pos.rx) * max_move_speed;

            p.vel.vx = matrix::lerp(p.vel.vx, target_vx, acceleration);
            p.vel.vz = matrix::lerp(p.vel.vz, target_vz, acceleration);

            const float dt_seconds = dt / 1000.0f;
            p.pos.x += p.vel.vx * dt_seconds;
            p.pos.z += p.vel.vz * dt_seconds;
        });

    player_commands[PlayerAction::MOVE_AUTO].action = derived_action<player>(
        [](player &p, const float dt, mazes::randomizer &rng)
        {
            p._is_auto_running = !p._is_auto_running;
        });

    player_commands[PlayerAction::JUMP].action = derived_action<player>(
        [](player &p, float dt, mazes::randomizer &rng)
        {
            if (p._is_flying)
            {
                // In flying mode, move up
                constexpr float flySpeed = 0.15f;
                p.vel.vy = flySpeed;
            }
            else if (p._is_on_ground)
            {
                // Normal jump when on ground
                constexpr float jumpVelocity = 8.0f;
                p.vel.vy = jumpVelocity;
                p._is_on_ground = false;
            }
        });

    player_commands[PlayerAction::TAG_SIGN].action = derived_action<player>(
        [](player &p, float dt, mazes::randomizer &rng)
        {
            if (p.current_world)
            {
                p.on_tag_sign();
            }
        });

    player_commands[PlayerAction::MOVE_DOWN].action = derived_action<player>(
        [](player &p, float dt, mazes::randomizer &rng)
        {
            if (p._is_flying)
            {
                constexpr float flySpeed = 4.85f;
                p.vel.vy = -flySpeed;
            }
        });

    player_commands[PlayerAction::MOVE_UP].action = derived_action<player>(
        [](player &p, float dt, mazes::randomizer &rng)
        {
            if (p._is_flying)
            {
                constexpr float flySpeed = 4.85f;
                p.vel.vy = flySpeed;
            }
        });

    player_commands[PlayerAction::FLY].action = derived_action<player>(
        [](player &p, float dt, mazes::randomizer &rng)
        {
            p._is_flying = !p._is_flying;
            if (p._is_flying)
            {
                p.vel.vy = 0.0f;
            }
        });

    player_commands[PlayerAction::BUILD_BLOCK].action = derived_action<player>(
        [](const player &p, float dt, mazes::randomizer &rng)
        {
            if (p.current_world)
            {
                p.on_right_click();
            }
        });

    player_commands[PlayerAction::COPY_BLOCK].action = derived_action<player>(
        [](player &p, float dt, mazes::randomizer &rng)
        {
            if (p.current_world)
            {
                p.on_middle_click();
            }
        });

    player_commands[PlayerAction::DESTROY_BLOCK].action = derived_action<player>(
        [](const player &p, float dt, mazes::randomizer &rng)
        {
            if (p.current_world)
            {
                p.on_left_click();
            }
        });

    player_commands[PlayerAction::PLACE_LIGHT].action = derived_action<player>(
        [](const player &p, float dt, mazes::randomizer &rng)
        {
            if (p.current_world)
            {
                p.on_light();
            }
        });
}

bool player::is_realtime_action(const PlayerAction action) noexcept
{
    switch (action)
    {
    case PlayerAction::MOVE_LEFT:
    case PlayerAction::MOVE_RIGHT:
    case PlayerAction::MOVE_FORWARD:
    case PlayerAction::MOVE_BACKWARD:
    case PlayerAction::MOVE_DOWN:
    case PlayerAction::MOVE_UP:
        return true;
    case PlayerAction::CHANGE_PERSPECTIVE:
        [[fallthrough]];
    case PlayerAction::ZOOM_IN_ISO_VIEW:
        [[fallthrough]];
    case PlayerAction::ZOOM_OUT_ISO_VIEW:
        [[fallthrough]];
    default:
        return false;
    }
}

void player::on_light() const noexcept
{
    const position *s = &this->pos;
    int hx, hy, hz;
    if (const int hw = current_world->hit_test(0, s->x, s->y, s->z, s->rx, s->ry, &hx, &hy, &hz);
        hy > 0 && hy < item::TOTAL_BLOCKS && item::is_destructable(hw))
    {
        current_world->toggle_light(hx, hy, hz);
    }
}

void player::on_left_click() const noexcept
{
    const position *s = &this->pos;
    int hx, hy, hz;
    if (const auto hw = current_world->hit_test(0, s->x, s->y, s->z, s->rx, s->ry, &hx, &hy, &hz);
        hy > 0 && hy < 256 && item::is_destructable(hw))
    {
        current_world->set_block(hx, hy, hz, 0);

        if (item::is_plant(current_world->get_block(hx, hy + 1, hz)))
        {
            current_world->set_block(hx, hy + 1, hz, 0);
        }
    }
}

void player::on_right_click() const noexcept
{
    const position *s = &this->pos;
    int hx, hy, hz;
    if (const int hw = current_world->hit_test(1, s->x, s->y, s->z, s->rx, s->ry, &hx, &hy, &hz);
        hy > 0 && hy < item::TOTAL_BLOCKS && item::is_obstacle(hw))
    {
        if (!world::player_intersects_block(2, s->x, s->y, s->z, hx, hy, hz))
        {
            current_world->set_block(hx, hy, hz, get_item());
        }
    }
}

void player::on_middle_click() noexcept
{
    const position *s = &this->pos;
    int hx, hy, hz;
    const int hw = current_world->hit_test(0, s->x, s->y, s->z, s->rx, s->ry, &hx, &hy, &hz);
    if (const auto it = std::ranges::find(item::ITEMS, hw); it != item::ITEMS.end())
    {
        current_item_index = static_cast<std::int32_t>(it - item::ITEMS.begin());
    }
}

void player::on_tag_sign() const noexcept
{
    int hx, hy, hz, face;
    if (auto result = current_world->hit_test_face(&hx, &hy, &hz, &face))
    {
        current_world->set_sign(hx, hy, hz, face, configs.tag());
    }
}

/// Gather player's voxel world artifacts from the database
/// @return Wavefront .obj data as a string, or empty if not ready
std::string player::artifacts() const noexcept
{
    // Check if database is enabled
    if (!db::is_enabled())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Database not enabled for artifacts export\n");
        return "";
    }

    // Calculate player's chunk coordinates
    const int player_chunk_p = world::chunked(pos.x);
    const int player_chunk_q = world::chunked(pos.z);
    // Query blocks from nearby chunks - increased radius for better coverage
    // Radius of 4 chunks = 9x9 chunk area (~2304 blocks if fully populated)
    constexpr int chunk_radius = 4;
    const auto blocks = db::query_blocks_near_chunks(player_chunk_p, player_chunk_q, chunk_radius);

    if (blocks.empty())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No blocks found in database for export\n");
        return "";
    }

    return artifact_exporter::blocks_to_wavefront_obj(blocks);
}

// ============================================================================
// Async Artifact Export
// ============================================================================

void player::start_async_artifact_export() noexcept
{
    // Don't start a new export if one is already running
    if (m_exporter.is_running())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Artifact export already in progress\n");
        return;
    }

    SDL_Log("Starting async artifact export...\n");

    if (!db::is_enabled())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Database not enabled for artifacts export\n");
        m_exporter.start_async({});
        return;
    }

    // Query blocks on the calling (main) thread.
    // SQLite is not safe to call from std::async worker threads in Emscripten
    // (SQLITE_THREADSAFE=0 builds) because sqlite3_step runs outside load_mtx
    // while the db_worker can concurrently execute "commit; begin;", causing a
    // data race that silently returns 0 rows.
    const int player_chunk_p = world::chunked(pos.x);
    const int player_chunk_q = world::chunked(pos.z);

    SDL_Log("Async export: Querying database for blocks...\n");
    constexpr int chunk_radius = 4;
    auto blocks = db::query_blocks_near_chunks(player_chunk_p, player_chunk_q, chunk_radius);

    if (blocks.empty())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No blocks found in database for export\n");
        m_exporter.start_async({});
        return;
    }

    m_exporter.start_async(std::move(blocks));
}

bool player::is_artifact_export_ready() const noexcept
{
    return m_exporter.is_ready();
}

std::string player::get_artifact_export_result() noexcept
{
    if (!m_exporter.is_ready())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Artifact export not ready yet\n");
        return "";
    }
    return m_exporter.get_result();
}

// ============================================================================
// CAD Helper Functions (Tier 1)
// ============================================================================

const char *player::get_block_name(item::BlockType block_type) noexcept
{
    return item::to_block_name(block_type).data();
}

const char *player::get_face_name(const int face) noexcept
{
    switch (face)
    {
    case 0:
        return "Left";
    case 1:
        return "Right";
    case 2:
        return "Top";
    case 3:
        return "Bottom";
    case 4:
        return "Front";
    case 5:
        return "Back";
    default:
        return "Unknown";
    }
}

const player_configs &player::get_configs() const noexcept
{
    return configs;
}

player_configs &player::get_configs() noexcept
{
    return configs;
}

void player::set_configs(const player_configs &cfg)
{
    configs = cfg;
}

std::int32_t player::get_font_index() const noexcept
{
    return selected_font_index;
}

void player::set_font_index(const std::int32_t index) noexcept
{
    selected_font_index = index;
}

const std::string &player::get_name() const noexcept
{
    return my_name;
}

void player::set_name(const std::string &name)
{
    my_name = name;
}
