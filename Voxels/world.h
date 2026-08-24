#ifndef WORLD_H
#define WORLD_H

#include <array>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "bloom_pass.h"
#include "player.h"
#include "resource_identifiers.h"
#include "sdl_gl_helper.h"
#include "voxels_map.h"

union SDL_Event;

namespace mazes
{
    class randomizer;
}

// Unified voxel chunk: spatial scene-graph node + terrain/light data.
struct chunk
{
    // Spatial hierarchy
    chunk *parent{nullptr};
    std::vector<chunk *> children{};
    int bounds_min_x{0};
    int bounds_min_z{0};
    int bounds_max_x{0};
    int bounds_max_z{0};

    // Voxel data
    voxels_map map{};
    voxels_map lights{};
    sign_list signs{};
    int p{0};
    int q{0};
    int faces{0};
    int sign_faces{0};
    int dirty{0};
    int miny{0};
    int maxy{0};
    std::uint32_t buffer{0};
    std::uint32_t sign_buffer{0};

    [[nodiscard]] Entity get_category() const noexcept { return m_category; }
    void set_category(const Entity category) noexcept { m_category = category; }

    [[nodiscard]] bool intersects_bounds(const int min_x, const int min_z,
                                         const int max_x, const int max_z) const noexcept
    {
        return !(bounds_max_x < min_x || bounds_min_x > max_x ||
                 bounds_max_z < min_z || bounds_min_z > max_z);
    }

private:
    Entity m_category{Entity::NONE};
};

// Worker state machine
enum class WorkerState : int { IDLE = 0, BUSY = 1, DONE = 2 };

struct worker_item
{
    int p{};
    int q{};
    int load{};
    voxels_map *block_maps[3][3]{};
    voxels_map *light_maps[3][3]{};
    int miny{};
    int maxy{};
    int faces{};
    float *data{};
};

struct worker
{
    int index{};
    WorkerState state{WorkerState::IDLE};
    std::thread thrd;
    std::mutex mtx;
    std::condition_variable cnd;
    worker_item item;
    bool should_stop{};
};

class world final
{
    friend class player;

public:
    explicit world(SDL_Window *window,
                   font_manager &fonts,
                   player *p,
                   shader_manager &shaders,
                   texture_manager &textures,
                   const sdl_gl_helper *sdl);

    ~world();

    void init() noexcept;

    void handle_event(const SDL_Event &event) noexcept;

    void update(float delta_time, mazes::randomizer &rng) noexcept;

    void draw() const noexcept;

    // Renders a simplified (no bloom, no HUD/wireframe/ghost overlays) snapshot of the
    // world's current camera view into a caller-owned FBO, for a non-interactive picture-
    // in-picture preview (e.g. the menu's Builder tab). Restores the previously bound
    // framebuffer/viewport before returning.
    void draw_preview(std::uint32_t target_fbo, int target_width, int target_height) const noexcept;

    command_queue &get_command_queue() noexcept;

    // Used by player for artifact export coordinates
    [[nodiscard]] static int chunked(float x) noexcept;

private:
    // Internal geometry helper
    struct ivec3 { int x; int y; int z; };
    static ivec3 face_normal(int face) noexcept;
    static ivec3 face_u_axis(int face) noexcept;
    static ivec3 face_v_axis(int face) noexcept;

    void destroy_world();

    // Building helper methods
    bool update_preview(const std::vector<std::uint8_t> &pixel_data,
                        int width,
                        int height) const noexcept;
    void finalize_buildings(std::vector<std::uint8_t> pixel_data,
                            int width, int height, int scale,
                            int wall_height, int item_type) noexcept;
    void commit_preview_to_world(int item_type) noexcept;
    void process_build_queue() noexcept;

    // Scene graph methods
    void build_scene();
    void attach_chunk_to_layer(chunk *c, int layer_index) noexcept;
    void detach_chunk_from_layer(chunk *c) noexcept;
    void traverse_chunks(const std::function<void(chunk *)> &callback) const noexcept;
    void traverse_chunks_in_bounds(int min_p, int min_q, int max_p, int max_q,
                                   const std::function<void(chunk *)> &callback) const noexcept;
    void insert_chunk_into_spatial_tree(chunk *c) const noexcept;
    static void remove_chunk_from_spatial_tree(chunk *c) noexcept;

    // Worker functions
    bool worker_run(worker *w) const noexcept;
    void init_worker_threads() noexcept;
    void cleanup_worker_threads() noexcept;

    // Time functions
    [[nodiscard]] double get_time() const noexcept;
    [[nodiscard]] float time_of_day() const noexcept;
    [[nodiscard]] float get_daylight() const noexcept;

    [[nodiscard]] std::optional<chunk *> find_chunk(int p, int q) const noexcept;
    static int chunk_distance(const chunk *c, int p, int q) noexcept;
    bool chunk_visible(float planes[6][4], int p, int q, int miny, int maxy) const noexcept;

    [[nodiscard]] int highest_block(float x, float z) const noexcept;
    static int _hit_test(const voxels_map *map, float max_distance, int previous,
                         float x, float y, float z, float vx, float vy, float vz, int *hx, int *hy, int *hz) noexcept;
    int hit_test(int previous, float x, float y, float z,
                 float rx, float ry, int *bx, int *by, int *bz) const noexcept;
    int hit_test_face(int *x, int *y, int *z, int *face) const noexcept;
    int collide(int height, float *x, float *y, float *z) const noexcept;
    [[nodiscard]] static bool player_intersects_block(int height, float x, float y, float z,
                                                      int hx, int hy, int hz) noexcept;

    void dirty_chunk(chunk *c) const noexcept;
    void update_dirty_chunks_async() const noexcept;

    static void occlusion(char neighbors[27], char lights[27], float shades[27],
                          float ao[6][4], float light[6][4]) noexcept;
    static void light_fill(char *opaque, char *light, int x, int y, int z, const int w, int force) noexcept;
    bool has_lights(const chunk *c) const noexcept;

    static void compute_chunk(worker_item *item) noexcept;

    static void generate_chunk(chunk *c, const worker_item *item) noexcept;
    void gen_chunk_buffer(chunk *c) const noexcept;

    void load_chunk(const worker_item *item) const noexcept;
    void init_chunk(chunk *c, int p, int q) noexcept;
    void create_chunk(chunk *c, int p, int q) noexcept;
    void delete_chunks() noexcept;
    void delete_all_chunks() noexcept;
    void force_chunks(player *player) noexcept;
    void check_workers() noexcept;
    void ensure_chunks_worker(player *_player, worker *w) noexcept;
    void ensure_chunks(player *_player) noexcept;

    void unset_sign(int x, int y, int z) const noexcept;
    void unset_sign_face(int x, int y, int z, int face) const noexcept;
    void _set_sign(int p, int q, int x, int y, int z, int face, std::string_view text, int dirty) const noexcept;
    void set_sign(int x, int y, int z, int face, std::string_view text) const noexcept;

    void toggle_light(int x, int y, int z) const noexcept;
    void set_light(int p, int q, int x, int y, int z, int w) const noexcept;

    void _set_block(int p, int q, int x, int y, int z, int w, int dirty) const noexcept;
    void set_block(int x, int y, int z, int w) const noexcept;
    [[nodiscard]] int get_block(int x, int y, int z) const noexcept;

    [[nodiscard]] std::size_t get_chunk_count() const noexcept;

    [[nodiscard]] int render_chunks(std::uint32_t texture) const noexcept;
    void render_signs(std::uint32_t sign) const noexcept;
    void render_sign(std::uint32_t sign) const noexcept;
    void render_sky(std::uint32_t buffer, std::uint32_t sky_tex) const noexcept;
    void render_wireframe() const noexcept;
    void render_crosshairs() const noexcept;
    void render_item(std::uint32_t texture) const noexcept;
    void render_player(std::uint32_t texture) const noexcept;

    // CAD Feature Rendering (Tier 1)
    void render_hover_info() const noexcept;
    void render_grid_overlay() const noexcept;
    void render_measurement_lines() const noexcept;
    void render_maze_preview_ghost() const noexcept;

    void render_text(std::uint32_t font, int justify,
                     float x, float y, float n, std::string_view text) const noexcept;
    void render_plane() const noexcept;

    // Matrix setup helpers — bind shader program, upload projection matrix uniform.
    std::pair<int, int> begin_3d_pass(const sdl_gl_helper::attrib *a, float matrix[16]) const noexcept;
    std::pair<int, int> begin_sky_pass(const sdl_gl_helper::attrib *a, float matrix[16]) const noexcept;
    std::pair<int, int> begin_item_pass(const sdl_gl_helper::attrib *a, float matrix[16]) const noexcept;
    std::pair<int, int> begin_2d_pass(const sdl_gl_helper::attrib *a, float matrix[16]) const noexcept;

    // Static GL attribute slots — initialized in world::init(), shared across all render methods.
    static sdl_gl_helper::attrib s_block_attrib;
    static sdl_gl_helper::attrib s_line_attrib;
    static sdl_gl_helper::attrib s_text_attrib;
    static sdl_gl_helper::attrib s_sky_attrib;

    enum class Layer
    {
        BACKGROUND = 0,
        FOREGROUND = 1,
        LAYER_COUNT = 2
    };

    struct projected_plane
    {
        bool visible{false};
        texture *projected_texture{nullptr};
        int target_x{0};
        int target_y{0};
        int target_z{0};
        int target_face{0};
        bool has_valid_target{false};
    } current_projected_plane{};

    // Store current preview data for reusable building
    struct preview_data
    {
        std::vector<std::uint8_t> pixel_data;
        int width{1};
        int height{1};
        int scale{1};
        int wall_height{0};
        int item_type{0};
        bool has_data{false};
    } current_preview_data{};

    const sdl_gl_helper *simple_direct_medialayer;

    font_manager &active_fonts;
    shader_manager &world_shaders;
    texture_manager &world_textures;

    static constexpr auto MAX_CHUNKS = 8192;
    std::array<std::array<chunk *, MAX_CHUNKS>, static_cast<std::size_t>(Layer::LAYER_COUNT)> scene_graph_layers;

    std::size_t next_chunk_slot_in_view;

    command_queue commands_during_world_events;
    player *active_player;

    std::vector<std::unique_ptr<worker>> chunk_workers;

    std::uint32_t world_sky_gl_buffer;

    std::vector<std::function<void()>> m_player_building_events;

    bloom_pass m_bloom;
};

#endif // WORLD_H
