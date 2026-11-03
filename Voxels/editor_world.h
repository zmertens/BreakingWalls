#ifndef EDITOR_WORLD_H
#define EDITOR_WORLD_H

#include "game_environment.h"
#include "world.h"

#include <box3d/box3d.h>

#include <array>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <cstdint>
#include <vector>

namespace mazes
{
    class configurator;
}

class editor_world final : public world
{
    friend class player;

public:
    editor_world(const world_builder &b);
    ~editor_world() override;

    void init() noexcept override;
    void handle_event(const SDL_Event &event) noexcept override;
    void update(float delta_time, mazes::randomizer &rng) noexcept override;
    void draw() const noexcept override;
    command_queue &get_command_queue() noexcept;
    void update_physics(float delta_time) noexcept;
    // Nearest-to-player impact point (world space) since the last call, for positional sound effects.
    [[nodiscard]] std::optional<std::array<float, 3>> take_pending_impact() noexcept
    {
        return std::exchange(m_physics_pending_impact, std::nullopt);
    }
    void draw_physics_controls() const noexcept;
    [[nodiscard]] bool physics_enabled() const noexcept { return B3_IS_NON_NULL(m_physics_world); }
    // Enters arcade mode: ORTHO camera, maze arena, cursor-driven player.
    void begin_arcade(int level, mazes::randomizer &rng) noexcept;
    void end_arcade() noexcept;
    [[nodiscard]] int rendered_terrain_faces() const noexcept { return m_rendered_terrain_faces; }

private:
    void render_signs(std::uint32_t sign) const noexcept;
    void render_sign(std::uint32_t sign) const noexcept;
    void render_sky(std::uint32_t buffer, std::uint32_t sky_tex) const noexcept;
    void render_wireframe() const noexcept;
    void render_crosshairs() const noexcept;
    void render_item(std::uint32_t texture) const noexcept;
    void render_player(std::uint32_t texture) const noexcept;
    void render_held_stick(std::uint32_t texture) const noexcept;
    void render_physics() const noexcept;
    void render_ticker(std::uint32_t font) const noexcept;
    void render_arcade_hud(std::uint32_t font) const noexcept;
    void update_arcade(float delta_time_ms, mazes::randomizer &rng) noexcept;
    void render_text(std::uint32_t font, int justify,
                     float x, float y, float n, std::string_view text) const noexcept;

    std::pair<int, int> begin_sky_pass(const sdl_gl_helper::attrib *a, float matrix[16]) const noexcept;
    std::pair<int, int> begin_item_pass(const sdl_gl_helper::attrib *a, float matrix[16]) const noexcept;
    std::pair<int, int> begin_2d_pass(const sdl_gl_helper::attrib *a, float matrix[16]) const noexcept;

    command_queue world_commands;
    game_environment environment;

    std::uint32_t world_sky_gl_buffer{};

    sdl_gl_helper::attrib s_line_attrib{};
    sdl_gl_helper::attrib s_text_attrib{};
    sdl_gl_helper::attrib s_sky_attrib{};

    int block_emission{-1};
    int block_light_attenuation{-1};
    int block_taper{-1};
    int block_center{-1};

    mutable int m_rendered_terrain_faces{};
    // Full-screen effect triggered by gameplay events; progress runs 0 -> 1 over its duration.
    ShaderEffect m_fx_effect{ShaderEffect::WAVE};
    float m_fx_remaining{0.0f};
    float m_fx_duration{1.0f};
    void trigger_effect(ShaderEffect effect, float seconds) noexcept;
    std::array<float, 3> m_last_trail_position{};
    std::vector<arcade_block_change> m_arcade_changes;
    std::uint64_t m_swing_start_ms{0};
    bool m_swinging{false};

    struct physics_object
    {
        b3BodyId body{b3_nullBodyId};
        bool sphere{false};
        int block_type{};
        std::array<float, 3> color{};
    };

    // Spring-damper "soft body" that trails the player so the third-person model wobbles, leans and squashes.
    struct player_body
    {
        float ox{}, oy{}, oz{};
        float vx{}, vy{}, vz{};
        float yaw_offset{}, yaw_velocity{};
        float squash{}, squash_velocity{};
        float last_x{}, last_y{}, last_z{}, last_av_y{};
        bool last_grounded{true};
        bool initialized{false};
    } m_player_body;
    void update_player_body(float dt_seconds) noexcept;

    static constexpr std::size_t MAX_PHYSICS_OBJECTS = 4096;
    static constexpr std::size_t MAX_PHYSICS_SPHERES = 64;
    static constexpr float PHYSICS_STEP = 1.0f / 60.0f;
    b3WorldId m_physics_world{b3_nullWorldId};
    // Kinematic capsule that mirrors the player while they are inside the physics slice.
    b3BodyId m_physics_player{b3_nullBodyId};
    b3BodyId m_physics_static{b3_nullBodyId};
    std::vector<physics_object> m_physics_objects;
    std::unordered_map<int, b3ShapeId> m_physics_walls;
    std::shared_ptr<const arcade_level> m_physics_level;
    std::uint64_t m_physics_impact_count{};
    std::optional<std::array<float, 3>> m_physics_pending_impact;
    float m_physics_pending_impact_distance_sq{};
    std::array<std::uint32_t, 6> m_physics_cube_buffers{};
    std::array<std::uint32_t, 6> m_physics_sphere_buffers{};
    int m_physics_sphere_vertices{};
    float m_physics_accumulator{};
    std::uint64_t m_physics_step_counter{};
    std::uint64_t m_physics_step_ticks{};
    float m_physics_origin_x{};
    float m_physics_origin_z{};
    float m_physics_spawn_height{40.0f};
    float m_physics_right_x{1.0f};
    float m_physics_right_z{};
    float m_physics_yaw{};
    float m_bump_cooldown{};
    mutable bool m_physics_paused{false};
    mutable bool m_physics_reset_requested{false};
    mutable bool m_add_physics_block_requested{false};
    mutable bool m_add_physics_sphere_requested{false};
    mutable bool m_physics_jolt_requested{false};
    std::uint32_t m_physics_jolt_seed{1};

    void jolt_physics_blocks() noexcept;
    // Plane coordinates of a world point: along the slice, and distance from it.
    [[nodiscard]] std::pair<float, float> physics_plane_coords(float x, float z) const noexcept;
    void sync_physics_player() noexcept;
    void support_player_on_physics() noexcept;

    void reset_physics() noexcept;
    void destroy_physics() noexcept;
    void bump_player_with_spheres(float dt) noexcept;
    void release_physics_walls(const std::vector<arcade_block_change> &changes) noexcept;
    [[nodiscard]] std::array<float, 3> random_maze_spawn() noexcept;
    void add_physics_object(bool sphere) noexcept;
};

#endif // EDITOR_WORLD_H
