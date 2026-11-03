#include "player.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ranges>
#include <sstream>

#include "db_manager.h"
#include "geometries.h"
#include "item.h"
#include "matrix.h"
#include "texture.h"
#include "editor_world.h"
#include "world.h"

#include <MazeBuilder/algos.h>
#include <algorithm>
#include <utility>

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
    reset_gamepad_bindings();
}

void player::reset_gamepad_bindings()
{
    gamepad_bindings = {
        {PlayerAction::JUMP, SDL_GAMEPAD_BUTTON_SOUTH},
        {PlayerAction::MOVE_AUTO, SDL_GAMEPAD_BUTTON_EAST},
        {PlayerAction::PLACE_LIGHT, SDL_GAMEPAD_BUTTON_WEST},
        {PlayerAction::TAG_SIGN, SDL_GAMEPAD_BUTTON_NORTH},
        {PlayerAction::COPY_BLOCK, SDL_GAMEPAD_BUTTON_LEFT_STICK},
        {PlayerAction::CHANGE_PERSPECTIVE, SDL_GAMEPAD_BUTTON_RIGHT_STICK},
        {PlayerAction::FLY, SDL_GAMEPAD_BUTTON_BACK},
        {PlayerAction::ZOOM_IN_ISO_VIEW, SDL_GAMEPAD_BUTTON_DPAD_UP},
        {PlayerAction::ZOOM_OUT_ISO_VIEW, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
        {PlayerAction::MOVE_UP, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
        {PlayerAction::MOVE_DOWN, SDL_GAMEPAD_BUTTON_DPAD_RIGHT}};
}

void player::assign_gamepad_button(const PlayerAction action, const int button)
{
    gamepad_bindings.erase(action);
    if (button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT)
    {
        return;
    }
    std::erase_if(gamepad_bindings, [button](const auto &kv)
                  { return kv.second == button; });
    gamepad_bindings[action] = button;
}

int player::get_assigned_gamepad_button(const PlayerAction action) const
{
    const auto found = gamepad_bindings.find(action);
    return found == gamepad_bindings.end() ? -1 : found->second;
}

void player::apply_gamepad_settings() const noexcept
{
    gamepads.set_rumble_enabled(configs.rumble_enabled());
    gamepads.set_dead_zone(configs.gamepad_dead_zone());
}

void player::rumble(const float low, const float high, const std::uint32_t duration_ms) const noexcept
{
    apply_gamepad_settings();
    gamepads.rumble(low, high, duration_ms);
}

void player::push_action(const PlayerAction action, command_queue &commands) const
{
    if (const auto binding = player_commands.find(action); binding != player_commands.cend() && binding->second.action)
    {
        commands.push(binding->second);
    }
}

void player::cycle_view_mode() noexcept
{
    const char *view_name = "Perspective";
    switch (configs.view_mode())
    {
    case player_configs::ViewMode::ISOMETRIC:
        configs.view_mode(player_configs::ViewMode::ORTHO).ortho_scaling(ORTHO_MIN_SCALE);
        view_name = "Ortho View";
        break;
    case player_configs::ViewMode::ORTHO:
        configs.view_mode(player_configs::ViewMode::FIRST_PERSON).ortho_scaling(0);
        break;
    case player_configs::ViewMode::FIRST_PERSON:
        configs.view_mode(player_configs::ViewMode::THIRD_PERSON).ortho_scaling(0);
        view_name = "Third Person";
        break;
    case player_configs::ViewMode::THIRD_PERSON:
        configs.view_mode(player_configs::ViewMode::ISOMETRIC).ortho_scaling(ORTHO_MIN_SCALE);
        view_name = "Isometric View";
        break;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "View Mode: %s", view_name);
}

void player::zoom_iso_view(const bool zoom_in) noexcept
{
    if (configs.view_mode() != player_configs::ViewMode::ISOMETRIC)
    {
        return;
    }
    configs.ortho_scaling(zoom_in ? SDL_max(ORTHO_MIN_SCALE, configs.ortho_scaling() - 2)
                                  : SDL_min(ORTHO_MAX_SCALE, configs.ortho_scaling() + 2));
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Isometric Zoom: %d", configs.ortho_scaling());
}

void player::step_selected_item(const int direction) noexcept
{
    constexpr std::int32_t count = item::TOTAL_ITEMS;
    selected_item_index = (selected_item_index + direction + count) % count;
}

void player::handle_gamepad_button(const int button, command_queue &commands) noexcept
{
    if (button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)
    {
        step_selected_item(-1);
        return;
    }
    if (button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)
    {
        step_selected_item(1);
        return;
    }
    const auto bound = std::ranges::find_if(gamepad_bindings, [button](const auto &kv)
                                            { return kv.second == button; });
    if (bound == gamepad_bindings.end())
    {
        return;
    }
    const auto action = bound->first;
    if (action == PlayerAction::CHANGE_PERSPECTIVE)
    {
        cycle_view_mode();
    }
    else if (action == PlayerAction::ZOOM_IN_ISO_VIEW || action == PlayerAction::ZOOM_OUT_ISO_VIEW)
    {
        zoom_iso_view(action == PlayerAction::ZOOM_IN_ISO_VIEW);
    }
    else if (action == PlayerAction::JUMP)
    {
        if (is_on_ground() || is_flying())
        {
            push_action(action, commands);
        }
    }
    else if (!is_realtime_action(action))
    {
        push_action(action, commands);
    }
}

void player::handle_event(const SDL_Event &event, command_queue &commands) noexcept
{
    if (event.type == SDL_EVENT_QUIT)
    {
        this->set_active(false);
    }

    if (!is_active())
    {
        return;
    }

    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        switch (event.key.scancode)
        {
        case SDL_SCANCODE_O:
            cycle_view_mode();
            return;
        case SDL_SCANCODE_EQUALS:
        case SDL_SCANCODE_KP_PLUS:
            if (configs.view_mode() == player_configs::ViewMode::ISOMETRIC)
            {
                zoom_iso_view(true);
                return;
            }
            break;
        case SDL_SCANCODE_MINUS:
        case SDL_SCANCODE_KP_MINUS:
            if (configs.view_mode() == player_configs::ViewMode::ISOMETRIC)
            {
                zoom_iso_view(false);
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
                if (is_on_ground())
                {
                    commands.push(binding->second);
                }
                return;
            }
            commands.push(binding->second);
        }
    }
    if (const auto pad_button = gamepads.pressed_button(event))
    {
        handle_gamepad_button(*pad_button, commands);
    }

    if (event.type == SDL_EVENT_MOUSE_WHEEL)
    {
        constexpr std::int32_t MAX_ITEM_INDEX = item::TOTAL_ITEMS - 1;

        if (event.wheel.y > SCROLL_THRESHOLD)
        {
            // Scroll up (backward through items)
            if (selected_item_index > 0)
            {
                selected_item_index--;
            }
            else
            {
                selected_item_index = MAX_ITEM_INDEX; // Wrap to last valid item
            }
        }
        else if (event.wheel.y < -SCROLL_THRESHOLD)
        {
            // Scroll down (forward through items)
            if (selected_item_index < MAX_ITEM_INDEX)
            {
                selected_item_index++;
            }
            else
            {
                selected_item_index = 0; // Wrap to first item
            }
        }
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
    apply_gamepad_settings();
    const float look_x = gamepads.axis(SDL_GAMEPAD_AXIS_RIGHTX);
    const float look_y = gamepads.axis(SDL_GAMEPAD_AXIS_RIGHTY);
    if (look_x != 0.0f || look_y != 0.0f)
    {
        const float step = configs.gamepad_look_speed() * delta_time / 1000.0f;
        pos.rx += look_x * step;
        pos.ry += (configs.invert_mouse() ? look_y : -look_y) * step;
        constexpr float TWO_PI = 6.28318531f;
        pos.rx = pos.rx < 0.0f ? pos.rx + TWO_PI : (pos.rx >= TWO_PI ? pos.rx - TWO_PI : pos.rx);
        pos.ry = SDL_clamp(pos.ry, -matrix::to_radians(90.0f), matrix::to_radians(90.0f));
    }

    if (is_auto_running())
    {
        // Implement auto-running logic here
    }
}

void player::draw() const noexcept
{
}

void player::handle_realtime_input(command_queue &commands)
{
    int numKeys = 0;
    const auto *keyState = SDL_GetKeyboardState(&numKeys);

    if (is_auto_running())
    {
        if (const auto binding = player_commands.find(PlayerAction::MOVE_FORWARD);
            binding != player_commands.cend() && binding->second.action)
        {
            commands.push(binding->second);
        }
    }

    apply_gamepad_settings();
    if (gamepads.connected())
    {
        constexpr float STICK_THRESHOLD = 0.35f;
        const float lx = gamepads.axis(SDL_GAMEPAD_AXIS_LEFTX), ly = gamepads.axis(SDL_GAMEPAD_AXIS_LEFTY);
        if (ly < -STICK_THRESHOLD && !is_auto_running())
            push_action(PlayerAction::MOVE_FORWARD, commands);
        else if (ly > STICK_THRESHOLD)
            push_action(PlayerAction::MOVE_BACKWARD, commands);
        if (lx < -STICK_THRESHOLD)
            push_action(PlayerAction::MOVE_LEFT, commands);
        else if (lx > STICK_THRESHOLD)
            push_action(PlayerAction::MOVE_RIGHT, commands);

        for (const auto &[action, button] : gamepad_bindings)
        {
            if (is_realtime_action(action) && action != PlayerAction::MOVE_FORWARD &&
                gamepads.button(static_cast<SDL_GamepadButton>(button)))
            {
                push_action(action, commands);
            }
        }

        // Triggers act as click buttons: right destroys, left builds, once per pull.
        const bool right = gamepads.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.5f;
        const bool left = gamepads.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 0.5f;
        if (right && !m_right_trigger_down)
            push_action(PlayerAction::DESTROY_BLOCK, commands);
        if (left && !m_left_trigger_down)
            push_action(PlayerAction::BUILD_BLOCK, commands);
        m_right_trigger_down = right;
        m_left_trigger_down = left;
    }

    // Process all realtime action keys
    for (auto &[id, action] : keyboard_bindings)
    {
        if (is_realtime_action(action))
        {
            // Skip JUMP if not flying
            if (action == PlayerAction::JUMP && !is_flying())
            {
                continue;
            }

            if (action == PlayerAction::MOVE_FORWARD && is_auto_running())
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
    return this->configs.is_active();
}

void player::set_active(bool active) noexcept
{
    this->configs.is_active(active);
}

bool player::is_flying() const noexcept
{
    return this->configs.is_flying();
}

void player::set_flying(const bool flying) noexcept
{
    this->configs.is_flying(flying);
}

bool player::is_on_ground() const noexcept
{
    return this->configs.is_on_ground();
}

void player::set_on_ground(const bool grounded) noexcept
{
    this->configs.is_on_ground(grounded);
}

bool player::is_auto_running() const noexcept
{
    return this->configs.is_auto_running();
}

void player::set_auto_running(bool auto_running) noexcept
{
    this->configs.is_auto_running(auto_running);
}

std::uint32_t player::get_buffer() const noexcept
{
    return this->player_buffer;
}

void player::set_buffer(const std::uint32_t value) noexcept
{
    this->player_buffer = value;
}

std::int32_t player::get_selected_item_index() const noexcept
{
    if (this->selected_item_index >= 0 && this->selected_item_index < item::ITEMS.size())
    {
        return item::ITEMS.at(this->selected_item_index);
    }
    return -1;
}

void player::set_selected_item_index(const std::int32_t value) noexcept
{
    if (value >= 0 && value < item::ITEMS.size())
    {
        this->selected_item_index = value;
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
            if (p.current_world && p.current_world->arcade.active())
            {
                p.current_world->arcade_primary_action(rng);
                return;
            }
            p.configs.is_auto_running(!p.configs.is_auto_running());
        });

    player_commands[PlayerAction::JUMP].action = derived_action<player>(
        [](player &p, float dt, mazes::randomizer &rng)
        {
            if (p.configs.is_flying())
            {
                // In flying mode, move up
                constexpr float flySpeed = 0.15f;
                p.vel.vy = flySpeed;
            }
            else if (p.configs.is_on_ground())
            {
                // Normal jump when on ground
                constexpr float jumpVelocity = 8.0f;
                p.vel.vy = jumpVelocity;
                p.configs.is_on_ground(false);
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
            if (p.configs.is_flying())
            {
                constexpr float flySpeed = 4.85f;
                p.vel.vy = -flySpeed;
            }
        });

    player_commands[PlayerAction::MOVE_UP].action = derived_action<player>(
        [](player &p, float dt, mazes::randomizer &rng)
        {
            if (p.configs.is_flying())
            {
                constexpr float flySpeed = 4.85f;
                p.vel.vy = flySpeed;
            }
        });

#if defined(BREAKING_WALLS_DEBUG_DEF)
    player_commands[PlayerAction::FLY].action = derived_action<player>(
        [](player &p, float dt, mazes::randomizer &rng)
        {
            p.configs.is_flying(!p.configs.is_flying());
            if (p.configs.is_flying())
            {
                p.vel.vy = 0.0f;
            }
        });
#endif

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
    if (current_world->arcade.active())
    {
        if (current_world->arcade_collect())
        {
            rumble(0.2f, 0.5f, 80);
        }
        return;
    }
    const position *s = &this->pos;
    int hx, hy, hz;
    if (const auto hw = current_world->hit_test(0, s->x, s->y, s->z, s->rx, s->ry, &hx, &hy, &hz);
        hy > 0 && hy < 256 && item::is_destructable(hw))
    {
        current_world->set_block(hx, hy, hz, 0);
        if (configs.show_particles())
            current_world->particles.burst(static_cast<float>(hx), static_cast<float>(hy), static_cast<float>(hz), 48,
                                           particle_system::kind::FIRE);
        rumble(0.5f, 0.8f, 120);
        current_world->ticker.push("Destroyed " + std::string{item::to_block_name(static_cast<item::BlockType>(hw))});

        if (item::is_plant(current_world->get_block(hx, hy + 1, hz)))
        {
            current_world->set_block(hx, hy + 1, hz, 0);
        }
    }
}

void player::on_right_click() const noexcept
{
    if (current_world->arcade.active())
    {
        return;
    }
    const position *s = &this->pos;
    int hx, hy, hz;
    if (const int hw = current_world->hit_test(1, s->x, s->y, s->z, s->rx, s->ry, &hx, &hy, &hz);
        hy > 0 && hy < item::TOTAL_BLOCKS && item::is_obstacle(hw))
    {
        if (!world::player_intersects_block(2, s->x, s->y, s->z, hx, hy, hz))
        {
            if (const int block = get_selected_item_index(); block >= 0)
            {
                current_world->set_block(hx, hy, hz, block);
                rumble(0.15f, 0.25f, 50);
                current_world->ticker.push("Placed " + std::string{item::to_block_name(static_cast<item::BlockType>(block))});
            }
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
        selected_item_index = static_cast<std::int32_t>(it - item::ITEMS.begin());
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
    db_manager *db = current_world ? current_world->world_db : nullptr;
    if (!db || !db->is_open())
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
    const auto blocks = db->query_blocks_near_chunks(player_chunk_p, player_chunk_q, chunk_radius);

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

    db_manager *db = current_world ? current_world->world_db : nullptr;
    if (!db || !db->is_open())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Database not enabled for artifacts export\n");
        m_exporter.start_async({});
        return;
    }

    // Query blocks on the calling (main) thread.
    // SQLite is not safe to call from std::async worker threads in Emscripten
    // (SQLITE_THREADSAFE=0 builds), so the query stays on this thread.
    const int player_chunk_p = world::chunked(pos.x);
    const int player_chunk_q = world::chunked(pos.z);

    SDL_Log("Async export: Querying database for blocks...\n");
    constexpr int chunk_radius = 4;
    auto blocks = db->query_blocks_near_chunks(player_chunk_p, player_chunk_q, chunk_radius);

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

std::map<std::string, std::string> player::export_settings() const
{
    const auto flag = [](const bool v)
    { return std::string{v ? "1" : "0"}; };
    const auto num = [](const float v)
    { return std::to_string(v); };
    const auto &m = configs.maze();
    std::map<std::string, std::string> out{
        {"graphics.fullscreen", flag(configs.fullscreen())},
        {"graphics.vsync", flag(configs.vsync())},
        {"graphics.bloom", flag(configs.use_bloom_effect())},
        {"graphics.show_stats", flag(configs.show_stats_window())},
        {"graphics.tickerbox", flag(configs.show_tickerbox())},
        {"graphics.particles", flag(configs.show_particles())},
        {"graphics.exposure", num(configs.exposure_val())},
        {"graphics.grid_opacity", num(configs.grid_opacity())},
        {"graphics.font_scale", num(configs.gui_font_scale())},
        {"graphics.font_index", std::to_string(selected_font_index)},
        {"camera.fov", num(configs.fov())},
        {"camera.invert_mouse", flag(configs.invert_mouse())},
        {"world.day_length", std::to_string(configs.day_length())},
        {"world.tag", configs.tag()},
        {"world.maze_rows", std::to_string(m.rows())},
        {"world.maze_columns", std::to_string(m.columns())},
        {"world.maze_levels", std::to_string(m.levels())},
        {"world.maze_algo", std::string{mazes::to_sv_from_algo(m.algo_id())}},
        {"input.rumble", flag(configs.rumble_enabled())},
        {"input.look_speed", num(configs.gamepad_look_speed())},
        {"input.dead_zone", num(configs.gamepad_dead_zone())},
        {"audio.master", num(configs.master_volume())},
        {"audio.music", num(configs.music_volume())},
        {"audio.sfx", num(configs.sfx_volume())},
        {"audio.mute", flag(configs.mute_audio())},
        {"player.selected_item", std::to_string(selected_item_index)},
        {"player.x", num(pos.x)},
        {"player.z", num(pos.z)},
        {"player.rx", num(pos.rx)},
        {"player.ry", num(pos.ry)}};
    for (int i = 0; i < static_cast<int>(PlayerAction::COUNT); ++i)
    {
        out["pad." + std::to_string(i)] = std::to_string(get_assigned_gamepad_button(static_cast<PlayerAction>(i)));
    }
    return out;
}

void player::import_settings(const std::map<std::string, std::string> &settings)
{
    const auto get = [&settings](const char *key) -> const std::string *
    {
        const auto found = settings.find(key);
        return found == settings.end() ? nullptr : &found->second;
    };
    const auto get_float = [&get](const char *key, auto &&apply)
    {
        if (const auto *v = get(key))
        {
            char *end = nullptr;
            const float f = std::strtof(v->c_str(), &end);
            if (end != v->c_str() && std::isfinite(f))
            {
                apply(f);
            }
        }
    };
    const auto get_int = [&get](const char *key, auto &&apply)
    {
        if (const auto *v = get(key))
        {
            char *end = nullptr;
            const long i = std::strtol(v->c_str(), &end, 10);
            if (end != v->c_str())
            {
                apply(static_cast<int>(i));
            }
        }
    };
    const auto get_bool = [&get_int](const char *key, auto &&apply)
    { get_int(key, [&apply](const int i)
              { apply(i != 0); }); };

    get_bool("graphics.fullscreen", [this](bool v)
             { configs.fullscreen(v); });
    get_bool("graphics.vsync", [this](bool v)
             { configs.vsync(v); });
    get_bool("graphics.bloom", [this](bool v)
             { configs.use_bloom_effect(v); });
    get_bool("graphics.show_stats", [this](bool v)
             { configs.show_stats_window(v); });
    get_bool("graphics.tickerbox", [this](bool v)
             { configs.show_tickerbox(v); });
    get_bool("graphics.particles", [this](bool v)
             { configs.show_particles(v); });
    get_float("graphics.exposure", [this](float v)
              { configs.exposure_val(std::clamp(v, 0.1f, 2.f)); });
    get_float("graphics.grid_opacity", [this](float v)
              { configs.grid_opacity(v); });
    get_float("graphics.font_scale", [this](float v)
              { configs.gui_font_scale(std::clamp(v, 0.6f, 1.4f)); });
    get_int("graphics.font_index", [this](int v)
            { if (v >= 0) selected_font_index = v; });
    get_float("camera.fov", [this](float v)
              { configs.fov(std::clamp(v, 30.f, 120.f)); });
    get_bool("camera.invert_mouse", [this](bool v)
             { configs.invert_mouse(v); });
    get_int("world.day_length", [this](int v)
            { configs.day_length(std::clamp(v, 60, 1800)); });
    if (const auto *tag = get("world.tag"))
    {
        configs.tag(*tag);
    }

    auto maze = configs.maze();
    get_int("world.maze_rows", [&maze](int v)
            { maze.rows(static_cast<unsigned int>(std::max(v, 1))); });
    get_int("world.maze_columns", [&maze](int v)
            { maze.columns(static_cast<unsigned int>(std::max(v, 1))); });
    get_int("world.maze_levels", [&maze](int v)
            { maze.levels(static_cast<unsigned int>(std::max(v, 1))); });
    if (const auto *algo = get("world.maze_algo"))
    {
        maze.algo_id(mazes::to_algo_from_sv(*algo));
    }
    configs.maze(maze);

    get_bool("input.rumble", [this](bool v)
             { configs.rumble_enabled(v); });
    get_float("input.look_speed", [this](float v)
              { configs.gamepad_look_speed(v); });
    get_float("input.dead_zone", [this](float v)
              { configs.gamepad_dead_zone(v); });
    for (int i = 0; i < static_cast<int>(PlayerAction::COUNT); ++i)
    {
        const std::string key = "pad." + std::to_string(i);
        get_int(key.c_str(), [this, i](int v)
                { assign_gamepad_button(static_cast<PlayerAction>(i), v); });
    }
    get_float("audio.master", [this](float v)
              { configs.master_volume(v); });
    get_float("audio.music", [this](float v)
              { configs.music_volume(v); });
    get_float("audio.sfx", [this](float v)
              { configs.sfx_volume(v); });
    get_bool("audio.mute", [this](bool v)
             { configs.mute_audio(v); });
    get_int("player.selected_item", [this](int v)
            { set_selected_item_index(v); });

    position restored{};
    int found = 0;
    get_float("player.x", [&](float v)
              { restored.x = v; ++found; });
    get_float("player.z", [&](float v)
              { restored.z = v; ++found; });
    get_float("player.rx", [&](float v)
              { restored.rx = v; ++found; });
    get_float("player.ry", [&](float v)
              { restored.ry = v; ++found; });
    if (found == 4)
    {
        saved_position = restored;
    }
}

std::optional<player::position> player::take_saved_position() noexcept
{
    return std::exchange(saved_position, std::nullopt);
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
