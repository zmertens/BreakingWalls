#ifndef PLAYER_H
#define PLAYER_H

#include <cstdint>
#include <future>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <unordered_map>
#include <string>
#include <type_traits>
#include <vector>

#include <MazeBuilder/configurator.h>

#include "artifact_exporter.h"
#include "gamepad_manager.h"
#include "item.h"
#include "player_configs.h"

class texture;
class world;

enum class PlayerAction
{
    MOVE_AUTO,
    MOVE_LEFT,
    MOVE_RIGHT,
    MOVE_FORWARD,
    MOVE_BACKWARD,
    MOVE_UP,
    MOVE_DOWN,
    JUMP,
    FLY,
    TAG_SIGN,
    BUILD_BLOCK,
    COPY_BLOCK,
    DESTROY_BLOCK,
    PLACE_LIGHT,
    CHANGE_PERSPECTIVE,
    ZOOM_IN_ISO_VIEW,
    ZOOM_OUT_ISO_VIEW,
    DONE,
    COUNT
};

union SDL_Event;

namespace mazes
{
    class randomizer;
}

class player;

struct command
{
    std::function<void(player &, float, mazes::randomizer &rng)> action;
};

using command_queue = std::queue<command>;

template <typename GameObject, typename Function>
std::function<void(player &, float, mazes::randomizer &)> derived_action(Function fn)
{
    return [=](player &p, float dt, mazes::randomizer &rng)
    {
        if constexpr (std::is_base_of_v<player, GameObject>)
        {
            fn(static_cast<GameObject &>(p), dt, std::ref(rng));
        }
    };
}

// Handles player input and state
class player final
{
public:
    struct position
    {
        float x, y, z, rx, ry;
    } pos{};

    struct velocity
    {
        float vx, vy, vz;
    } vel{};

    explicit player();

    ~player() = default;

    player(const player &) = delete;
    player &operator=(const player &) = delete;

    player(player &&) noexcept = delete;
    player &operator=(player &&) = delete;

    void handle_event(const SDL_Event &event, command_queue &commands) noexcept;

    void update(float delta_time, mazes::randomizer &rng) noexcept;

    void draw() const noexcept;

    void handle_realtime_input(command_queue &commands);

    void assign_key(PlayerAction action, std::uint32_t key);

    [[nodiscard]] std::uint32_t get_assigned_key(PlayerAction action) const;

    /// Controller bindings use SDL_GamepadButton values; -1 means unbound. A button belongs to one action.
    void assign_gamepad_button(PlayerAction action, int button);
    [[nodiscard]] int get_assigned_gamepad_button(PlayerAction action) const;
    void reset_gamepad_bindings();
    [[nodiscard]] gamepad_manager &get_gamepads() const noexcept { return gamepads; }
    /// Rumbles the active controller if the player enabled haptics.
    void rumble(float low, float high, std::uint32_t duration_ms) const noexcept;

    [[nodiscard]] bool is_active() const noexcept;
    void set_active(bool active) noexcept;

    [[nodiscard]] bool is_flying() const noexcept;
    void set_flying(bool flying) noexcept;

    [[nodiscard]] bool is_on_ground() const noexcept;
    void set_on_ground(bool grounded) noexcept;

    [[nodiscard]] bool is_auto_running() const noexcept;
    void set_auto_running(bool auto_running) noexcept;

    [[nodiscard]] std::uint32_t get_buffer() const noexcept;
    void set_buffer(std::uint32_t value) noexcept;

    [[nodiscard]] std::int32_t get_selected_item_index() const noexcept;
    void set_selected_item_index(std::int32_t value) noexcept;

    void set_world(world *w) noexcept;

    [[nodiscard]] const player_configs &get_configs() const noexcept;
    [[nodiscard]] player_configs &get_configs() noexcept;
    void set_configs(const player_configs &cfg);

    /// Flattens persistent configs, font, selected block and position into key/value strings.
    [[nodiscard]] std::map<std::string, std::string> export_settings() const;
    /// Applies previously exported settings; unknown or malformed entries are ignored.
    void import_settings(const std::map<std::string, std::string> &settings);
    /// Position restored from storage; returned once, then cleared.
    [[nodiscard]] std::optional<position> take_saved_position() noexcept;

    [[nodiscard]] std::int32_t get_font_index() const noexcept;
    void set_font_index(std::int32_t index) noexcept;

    [[nodiscard]] const std::string &get_name() const noexcept;
    void set_name(const std::string &name);

    [[nodiscard]] std::string get_local_time() const noexcept;

    [[nodiscard]] std::string artifacts() const noexcept;

    // Async artifact export
    void start_async_artifact_export() noexcept;
    [[nodiscard]] bool is_artifact_export_ready() const noexcept;
    [[nodiscard]] std::string get_artifact_export_result() noexcept;
private:
    void initialize_actions();
    static bool is_realtime_action(PlayerAction action) noexcept;

    void handle_gamepad_button(int button, command_queue &commands) noexcept;
    void push_action(PlayerAction action, command_queue &commands) const;
    void cycle_view_mode() noexcept;
    void zoom_iso_view(bool zoom_in) noexcept;
    void step_selected_item(int direction) noexcept;
    void apply_gamepad_settings() const noexcept;

    void on_light() const noexcept;
    void on_left_click() const noexcept;
    void on_right_click() const noexcept;
    void on_middle_click() noexcept;
    void on_tag_sign() const noexcept;

    std::unordered_map<std::uint32_t, PlayerAction> keyboard_bindings;

    std::unordered_map<PlayerAction, command> player_commands;
    std::unordered_map<PlayerAction, int> gamepad_bindings;
    mutable gamepad_manager gamepads;
    bool m_left_trigger_down{false};
    bool m_right_trigger_down{false};

    player_configs configs;
    std::optional<position> saved_position;

    std::uint32_t player_buffer;

    std::int32_t selected_item_index{0};
    std::int32_t selected_font_index{0};

    std::string my_name{"Flips and Ale"};

    world *current_world{nullptr};

    artifact_exporter m_exporter;
};

#endif // PLAYER_H
