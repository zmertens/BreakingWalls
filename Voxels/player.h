#ifndef PLAYER_H
#define PLAYER_H

#include <cstdint>
#include <future>
#include <functional>
#include <memory>
#include <optional>
#include <queue>
#include <unordered_map>
#include <string>
#include <type_traits>
#include <vector>

#include <MazeBuilder/configurator.h>

#include "artifact_exporter.h"
#include "db.h"
#include "geometries.h"
#include "item.h"
#include "player configs.h"
#include "craft_map.h"
#include "world.h"

class texture;

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
    PLACE_GHOST,
    PREVIEW_GHOST,
    CHANGE_PERSPECTIVE,
    ZOOM_IN_ISO_VIEW,
    ZOOM_OUT_ISO_VIEW,
    DONE,
    COUNT
};

union SDL_Event;

class world;

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
        float x, y, z, rx, ry, t;
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

    [[nodiscard]] bool is_active() const noexcept;
    void set_active(bool active) noexcept;

    [[nodiscard]] bool is_flying() const noexcept;
    void set_flying(bool flying) noexcept;

    [[nodiscard]] bool is_on_ground() const noexcept;
    void set_on_ground(bool grounded) noexcept;

    [[nodiscard]] std::uint32_t get_buffer() const noexcept;
    void set_buffer(std::uint32_t value) noexcept;

    [[nodiscard]] std::int32_t get_item() const noexcept;
    void set_item(std::int32_t value) noexcept;

    void set_world(world *w) noexcept;

    [[nodiscard]] const player_configs &get_configs() const noexcept;
    [[nodiscard]] player_configs &get_configs() noexcept;
    void set_configs(const player_configs &cfg);

    [[nodiscard]] std::int32_t get_font_index() const noexcept;
    void set_font_index(std::int32_t index) noexcept;
    [[nodiscard]] const std::string &get_name() const noexcept;
    void set_name(const std::string &name);

    [[nodiscard]] std::string get_local_time() const noexcept;

    [[nodiscard]] std::string artifacts() const noexcept;

    [[nodiscard]] bool is_download_ready() const noexcept;

    // Async artifact export
    void start_async_artifact_export() noexcept;
    [[nodiscard]] bool is_artifact_export_ready() const noexcept;
    [[nodiscard]] std::string get_artifact_export_result() noexcept;

    // CAD Helper Functions
    [[nodiscard]] static const char *get_block_name(item::BlockType block_type) noexcept;
    [[nodiscard]] static const char *get_face_name(int face) noexcept;

private:
    void initialize_actions();
    static bool is_realtime_action(PlayerAction action) noexcept;

    void on_light() const noexcept;
    void on_left_click() const noexcept;
    void on_right_click() const noexcept;
    void on_middle_click() noexcept;
    void on_tag_sign() const noexcept;

    std::unordered_map<std::uint32_t, PlayerAction> keyboard_bindings;

    std::unordered_map<PlayerAction, command> player_commands;

    bool _is_active;
    bool _is_on_ground;
    bool _is_flying;
    bool _is_auto_running;

    player_configs configs;

    std::uint32_t player_buffer;

    std::int32_t current_item_index;
    std::int32_t selected_font_index;
    std::string my_name{"zm"};

    world *current_world{nullptr};

    artifact_exporter m_exporter;
};

#endif // PLAYER_H
