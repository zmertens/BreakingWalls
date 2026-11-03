#ifndef GAMEPAD_MANAGER_H
#define GAMEPAD_MANAGER_H

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/// Tracks connected controllers through the SDL3 Gamepad API (standard mapping, rumble) and falls
/// back to the raw Joystick API for devices SDL has no mapping for. The first connected device is
/// the active one. Main-thread only; call close_all() before SDL shuts down.
class gamepad_manager final
{
public:
    gamepad_manager() = default;
    ~gamepad_manager() { close_all(); }
    gamepad_manager(const gamepad_manager &) = delete;
    gamepad_manager &operator=(const gamepad_manager &) = delete;

    /// Opens every device that is already attached. Safe to call repeatedly.
    void open_all();
    void close_all() noexcept;
    /// Handles device added/removed/remapped events; returns true if the event was a device event.
    bool handle_device_event(const SDL_Event &event);

    [[nodiscard]] bool connected() const noexcept { return !m_devices.empty(); }
    [[nodiscard]] std::string name() const;
    /// True when the active device is driven through the raw Joystick fallback.
    [[nodiscard]] bool is_fallback() const noexcept;

    /// Stick or trigger value with the dead zone removed: -1..1 for sticks, 0..1 for triggers.
    [[nodiscard]] float axis(SDL_GamepadAxis axis) const noexcept;
    [[nodiscard]] bool button(SDL_GamepadButton button) const noexcept;
    /// Button for a button-down event from the active device (either API), if any.
    [[nodiscard]] std::optional<SDL_GamepadButton> pressed_button(const SDL_Event &event) const noexcept;

    /// Low/high frequency motors, strengths 0..1 scaled by the global rumble strength.
    void rumble(float low, float high, std::uint32_t duration_ms) const noexcept;

    void set_rumble_enabled(const bool enabled) noexcept { m_rumble_enabled = enabled; }
    [[nodiscard]] bool rumble_enabled() const noexcept { return m_rumble_enabled; }
    void set_dead_zone(const float dead_zone) noexcept { m_dead_zone = dead_zone; }

private:
    struct device
    {
        SDL_JoystickID id{};
        SDL_Gamepad *pad{};
        SDL_Joystick *joystick{};
    };

    void open(SDL_JoystickID id);
    void close(SDL_JoystickID id) noexcept;
    [[nodiscard]] const device *active() const noexcept { return m_devices.empty() ? nullptr : &m_devices.front(); }
    [[nodiscard]] bool is_active_id(SDL_JoystickID id) const noexcept;

    std::vector<device> m_devices;
    bool m_rumble_enabled{true};
    float m_dead_zone{0.2f};
};

#endif // GAMEPAD_MANAGER_H
