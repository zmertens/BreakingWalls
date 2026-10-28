#ifndef EDITOR_WORLD_H
#define EDITOR_WORLD_H

#include "game_environment.h"
#include "world.h"

#include <box3d/box3d.h>

#include <array>
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
    editor_world(SDL_Window *window, font_manager &fonts, player *p,
                 shader_manager &shaders, texture_manager &textures,
                 const sdl_gl_helper *sdl, bloom_pass *bloom, db_manager *db);
    ~editor_world() override;

    void init() noexcept override;
    void handle_event(const SDL_Event &event) noexcept override;
    void update(float delta_time, mazes::randomizer &rng) noexcept override;
    void draw() const noexcept override;
    command_queue &get_command_queue() noexcept;
    void update_physics(float delta_time) noexcept;
    void draw_physics_controls() const noexcept;
    [[nodiscard]] int rendered_terrain_faces() const noexcept { return m_rendered_terrain_faces; }

private:
    void update_projected_plane() noexcept;

    void render_signs(std::uint32_t sign) const noexcept;
    void render_sign(std::uint32_t sign) const noexcept;
    void render_sky(std::uint32_t buffer, std::uint32_t sky_tex) const noexcept;
    void render_wireframe() const noexcept;
    void render_crosshairs() const noexcept;
    void render_item(std::uint32_t texture) const noexcept;
    void render_player(std::uint32_t texture) const noexcept;
    void render_held_stick(std::uint32_t texture) const noexcept;
    void render_physics() const noexcept;
    void render_hover_info() const noexcept;
    void render_text(std::uint32_t font, int justify,
                     float x, float y, float n, std::string_view text) const noexcept;

    std::pair<int, int> begin_sky_pass(const sdl_gl_helper::attrib *a, float matrix[16]) const noexcept;
    std::pair<int, int> begin_item_pass(const sdl_gl_helper::attrib *a, float matrix[16]) const noexcept;
    std::pair<int, int> begin_2d_pass(const sdl_gl_helper::attrib *a, float matrix[16]) const noexcept;

    // Face under the crosshair; feeds the hover info.
    struct projected_plane
    {
        int target_x{0};
        int target_y{0};
        int target_z{0};
        int target_face{0};
        bool has_valid_target{false};
    } current_projected_plane{};

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
    std::uint64_t m_swing_start_ms{0};
    bool m_swinging{false};

    struct physics_object
    {
        b3BodyId body{b3_nullBodyId};
        bool sphere{false};
        int block_type{};
        std::array<float, 3> color{};
    };

    struct physics_voxel_source
    {
        int x{};
        int y{};
        int z{};
        int block_type{};
    };

    static constexpr std::size_t MAX_PHYSICS_OBJECTS = 4096;
    static constexpr std::size_t MAX_PHYSICS_SPHERES = 64;
    static constexpr float PHYSICS_STEP = 1.0f / 60.0f;
    b3WorldId m_physics_world{b3_nullWorldId};
    // Kinematic capsule that mirrors the player while they are inside the physics slice.
    b3BodyId m_physics_player{b3_nullBodyId};
    std::vector<physics_object> m_physics_objects;
    std::vector<physics_voxel_source> m_physics_voxel_sources;
    std::uint64_t m_physics_impact_count{};
    std::array<std::uint32_t, 6> m_physics_cube_buffers{};
    std::array<std::uint32_t, 6> m_physics_sphere_buffers{};
    int m_physics_sphere_vertices{};
    float m_physics_accumulator{};
    float m_physics_origin_x{};
    float m_physics_origin_z{};
    float m_physics_spawn_height{40.0f};
    float m_physics_right_x{1.0f};
    float m_physics_right_z{};
    float m_physics_yaw{};
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
    void add_physics_object(bool sphere) noexcept;
    void restore_physics_voxels() noexcept;
};

#endif // EDITOR_WORLD_H
