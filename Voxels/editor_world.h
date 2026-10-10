#ifndef EDITOR_WORLD_H
#define EDITOR_WORLD_H

#include "world.h"

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
                 const sdl_gl_helper *sdl, bloom_pass *bloom);
    ~editor_world() override;

    void init() noexcept override;
    void handle_event(const SDL_Event &event) noexcept override;
    void update(float delta_time, mazes::randomizer &rng) noexcept override;
    void draw() const noexcept override;
    command_queue &get_command_queue() noexcept;
    [[nodiscard]] int rendered_terrain_faces() const noexcept { return m_rendered_terrain_faces; }

private:
    struct ivec3
    {
        int x;
        int y;
        int z;
    };
    static ivec3 face_normal(int face) noexcept;
    static ivec3 face_u_axis(int face) noexcept;
    static ivec3 face_v_axis(int face) noexcept;

    void update_projected_plane() noexcept;
    bool generate_ghost(const mazes::configurator &config) noexcept;
    void commit_ghost(int item_type) noexcept;

    void render_signs(std::uint32_t sign) const noexcept;
    void render_sign(std::uint32_t sign) const noexcept;
    void render_sky(std::uint32_t buffer, std::uint32_t sky_tex) const noexcept;
    void render_wireframe() const noexcept;
    void render_crosshairs() const noexcept;
    void render_item(std::uint32_t texture) const noexcept;
    void render_player(std::uint32_t texture) const noexcept;
    void render_hover_info() const noexcept;
    void render_ghost() const noexcept;
    void render_text(std::uint32_t font, int justify,
                     float x, float y, float n, std::string_view text) const noexcept;

    std::pair<int, int> begin_sky_pass(const sdl_gl_helper::attrib *a, float matrix[16]) const noexcept;
    std::pair<int, int> begin_item_pass(const sdl_gl_helper::attrib *a, float matrix[16]) const noexcept;
    std::pair<int, int> begin_2d_pass(const sdl_gl_helper::attrib *a, float matrix[16]) const noexcept;

    // Face under the crosshair; anchors the ghost and feeds the hover info.
    struct projected_plane
    {
        int target_x{0};
        int target_y{0};
        int target_z{0};
        int target_face{0};
        bool has_valid_target{false};
    } current_projected_plane{};

    // Logical maze layout (1 = wall) projected onto the targeted face as a 3D ghost.
    struct ghost_structure
    {
        std::vector<std::uint8_t> walls;
        int width{0};
        int height{0};
        int wall_height{0};
        bool has_data{false};
    } current_ghost{};

    command_queue world_commands;

    std::uint32_t world_sky_gl_buffer{};

    sdl_gl_helper::attrib s_line_attrib{};
    sdl_gl_helper::attrib s_text_attrib{};
    sdl_gl_helper::attrib s_sky_attrib{};

    int block_emission{-1};
    int block_light_attenuation{-1};
    int block_taper{-1};
    int block_center{-1};

    mutable int m_rendered_terrain_faces{};
};

#endif // EDITOR_WORLD_H
