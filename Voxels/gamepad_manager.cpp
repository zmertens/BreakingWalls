#include "gamepad_manager.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Raw joystick layout assumed for devices without an SDL mapping.
    constexpr int FALLBACK_AXIS_LX = 0;
    constexpr int FALLBACK_AXIS_LY = 1;
    constexpr int FALLBACK_AXIS_RX = 2;
    constexpr int FALLBACK_AXIS_RY = 3;

    constexpr SDL_GamepadButton FALLBACK_BUTTONS[] = {
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
        SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_START,
        SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER};

    float normalize(const Sint16 raw, const float dead_zone) noexcept
    {
        const float v = std::clamp(static_cast<float>(raw) / 32767.0f, -1.0f, 1.0f);
        const float magnitude = std::abs(v);
        if (magnitude <= dead_zone)
        {
            return 0.0f;
        }
        return std::copysign((magnitude - dead_zone) / (1.0f - dead_zone), v);
    }
} // namespace

void gamepad_manager::open_all()
{
    int count = 0;
    if (SDL_JoystickID *ids = SDL_GetJoysticks(&count))
    {
        for (int i = 0; i < count; ++i)
        {
            open(ids[i]);
        }
        SDL_free(ids);
    }
}

void gamepad_manager::close_all() noexcept
{
    for (auto &d : m_devices)
    {
        if (d.pad)
        {
            SDL_CloseGamepad(d.pad);
        }
        if (d.joystick)
        {
            SDL_CloseJoystick(d.joystick);
        }
    }
    m_devices.clear();
}

void gamepad_manager::open(const SDL_JoystickID id)
{
    if (std::ranges::any_of(m_devices, [id](const device &d)
                            { return d.id == id; }))
    {
        return;
    }
    device d{id};
    if (SDL_IsGamepad(id))
    {
        d.pad = SDL_OpenGamepad(id);
    }
    else
    {
        d.joystick = SDL_OpenJoystick(id);
    }
    if (!d.pad && !d.joystick)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not open controller %u: %s\n", id, SDL_GetError());
        return;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Controller connected: %s%s\n",
                d.pad ? SDL_GetGamepadName(d.pad) : SDL_GetJoystickName(d.joystick),
                d.pad ? "" : " (joystick fallback)");
    m_devices.push_back(d);
}

void gamepad_manager::close(const SDL_JoystickID id) noexcept
{
    const auto found = std::ranges::find_if(m_devices, [id](const device &d)
                                            { return d.id == id; });
    if (found == m_devices.end())
    {
        return;
    }
    if (found->pad)
    {
        SDL_CloseGamepad(found->pad);
    }
    if (found->joystick)
    {
        SDL_CloseJoystick(found->joystick);
    }
    m_devices.erase(found);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Controller disconnected\n");
}

bool gamepad_manager::handle_device_event(const SDL_Event &event)
{
    switch (event.type)
    {
    case SDL_EVENT_GAMEPAD_ADDED:
        open(event.gdevice.which);
        return true;
    case SDL_EVENT_JOYSTICK_ADDED:
        // Gamepads arrive as both events; only unmapped devices need the joystick path.
        if (!SDL_IsGamepad(event.jdevice.which))
        {
            open(event.jdevice.which);
        }
        return true;
    case SDL_EVENT_GAMEPAD_REMOVED:
        close(event.gdevice.which);
        return true;
    case SDL_EVENT_JOYSTICK_REMOVED:
        close(event.jdevice.which);
        return true;
    default:
        return false;
    }
}

std::string gamepad_manager::name() const
{
    const auto *d = active();
    if (!d)
    {
        return {};
    }
    const char *n = d->pad ? SDL_GetGamepadName(d->pad) : SDL_GetJoystickName(d->joystick);
    return n ? n : "Unknown controller";
}

bool gamepad_manager::is_fallback() const noexcept
{
    const auto *d = active();
    return d && !d->pad;
}

float gamepad_manager::axis(const SDL_GamepadAxis which) const noexcept
{
    const auto *d = active();
    if (!d)
    {
        return 0.0f;
    }
    if (d->pad)
    {
        const bool trigger = which == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || which == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        const float v = normalize(SDL_GetGamepadAxis(d->pad, which), m_dead_zone);
        return trigger ? std::max(v, 0.0f) : v;
    }
    int index = -1;
    switch (which)
    {
    case SDL_GAMEPAD_AXIS_LEFTX:
        index = FALLBACK_AXIS_LX;
        break;
    case SDL_GAMEPAD_AXIS_LEFTY:
        index = FALLBACK_AXIS_LY;
        break;
    case SDL_GAMEPAD_AXIS_RIGHTX:
        index = FALLBACK_AXIS_RX;
        break;
    case SDL_GAMEPAD_AXIS_RIGHTY:
        index = FALLBACK_AXIS_RY;
        break;
    default:
        return 0.0f;
    }
    return index < SDL_GetNumJoystickAxes(d->joystick) ? normalize(SDL_GetJoystickAxis(d->joystick, index), m_dead_zone) : 0.0f;
}

bool gamepad_manager::button(const SDL_GamepadButton which) const noexcept
{
    const auto *d = active();
    if (!d)
    {
        return false;
    }
    if (d->pad)
    {
        return SDL_GetGamepadButton(d->pad, which);
    }
    for (int i = 0; i < static_cast<int>(std::size(FALLBACK_BUTTONS)); ++i)
    {
        if (FALLBACK_BUTTONS[i] == which)
        {
            return i < SDL_GetNumJoystickButtons(d->joystick) && SDL_GetJoystickButton(d->joystick, i);
        }
    }
    if (SDL_GetNumJoystickHats(d->joystick) > 0)
    {
        const Uint8 hat = SDL_GetJoystickHat(d->joystick, 0);
        switch (which)
        {
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            return hat & SDL_HAT_UP;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            return hat & SDL_HAT_DOWN;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            return hat & SDL_HAT_LEFT;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            return hat & SDL_HAT_RIGHT;
        default:
            break;
        }
    }
    return false;
}

bool gamepad_manager::is_active_id(const SDL_JoystickID id) const noexcept
{
    const auto *d = active();
    return d && d->id == id;
}

std::optional<SDL_GamepadButton> gamepad_manager::pressed_button(const SDL_Event &event) const noexcept
{
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && is_active_id(event.gbutton.which))
    {
        return static_cast<SDL_GamepadButton>(event.gbutton.button);
    }
    if (event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN && is_active_id(event.jbutton.which) && is_fallback() &&
        event.jbutton.button < std::size(FALLBACK_BUTTONS))
    {
        return FALLBACK_BUTTONS[event.jbutton.button];
    }
    return std::nullopt;
}

void gamepad_manager::rumble(const float low, const float high, const std::uint32_t duration_ms) const noexcept
{
    const auto *d = active();
    if (!d || !m_rumble_enabled)
    {
        return;
    }
    const auto to_u16 = [](const float v)
    { return static_cast<Uint16>(std::clamp(v, 0.0f, 1.0f) * 65535.0f); };
    if (d->pad)
    {
        SDL_RumbleGamepad(d->pad, to_u16(low), to_u16(high), duration_ms);
    }
    else
    {
        SDL_RumbleJoystick(d->joystick, to_u16(low), to_u16(high), duration_ms);
    }
}
