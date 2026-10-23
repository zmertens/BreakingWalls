#ifndef WORLD_H
#define WORLD_H

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <bvhv2/bvh.h>
#include <bvhv2/executor.h>
#include <bvhv2/thread_pool.h>

#include "bloom_pass.h"
#include "chunk.h"
#include "item.h"
#include "player.h"
#include "resource_identifiers.h"
#include "sdl_gl_helper.h"
#include "craft_map.h"
#include "heightmap.h"

union SDL_Event;

namespace mazes
{
    class randomizer;
}

class world
{
    friend class player;

public:
    struct builder
    {
        builder(font_manager &fonts, shader_manager &shaders,
                texture_manager &textures, bloom_pass_manager &bloom_passes,
                player &active_player, sdl_gl_helper &sdl);

        font_manager *build_fonts;
        shader_manager *build_shaders;
        texture_manager *build_textures;
        bloom_pass_manager *build_bloom_passes;
        player *build_player;
        sdl_gl_helper *build_sdl;
    };

    // Spatial arithmetic includes negative coordinates and radius offsets.
    static constexpr int CREATE_CHUNK_RADIUS{10};
    static constexpr int RENDER_CHUNK_RADIUS{20};
    static constexpr int BUILD_CHUNK_SIZE{32};
    static constexpr int RENDER_SIGN_RADIUS{4};
    static constexpr int DELETE_CHUNK_RADIUS{14};

    virtual ~world();

    virtual void init() noexcept = 0;
    virtual void handle_event(const SDL_Event &event) noexcept = 0;
    virtual void update(float delta_time, mazes::randomizer &rng) noexcept = 0;
    virtual void draw() const noexcept = 0;

    // Used by player for artifact export coordinates
    [[nodiscard]] static int chunked(float x) noexcept;

protected:
    explicit world(const builder &b);

    world(const world &) = delete;
    world &operator=(const world &) = delete;
    void init_voxel_engine() noexcept;

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
    bool worker_run(chunk::worker *w) const noexcept;
    void init_worker_threads() noexcept;
    void cleanup_worker_threads() noexcept;

    // Time functions
    [[nodiscard]] double get_time_s() const noexcept;
    [[nodiscard]] float time_of_day() const noexcept;
    [[nodiscard]] float get_daylight() const noexcept;

    [[nodiscard]] std::optional<chunk *> find_chunk(int p, int q) const noexcept;
    [[nodiscard]] std::span<chunk *const> active_chunks() const noexcept;
    static int chunk_distance(const chunk *c, int p, int q) noexcept;
    bool chunk_visible(float planes[6][4], int p, int q, int miny, int maxy) const noexcept;

    [[nodiscard]] int highest_block(float x, float z) const noexcept;
    static int _hit_test(const craft_map *map, float max_distance, int previous,
                         float x, float y, float z, float vx, float vy, float vz, int *hx, int *hy, int *hz) noexcept;
    int hit_test(int previous, float x, float y, float z,
                 float rx, float ry, int *bx, int *by, int *bz) const noexcept;
    int hit_test_face(int *x, int *y, int *z, int *face) const noexcept;
    [[nodiscard]] std::array<float, 2> camera_rotation() const noexcept;
    [[nodiscard]] std::array<float, 3> camera_position() const noexcept;
    int collide(int height, float *x, float *y, float *z) const noexcept;
    [[nodiscard]] static bool player_intersects_block(int height, float x, float y, float z,
                                                      int hx, int hy, int hz) noexcept;

    void dirty_chunk(chunk *c) const noexcept;
    void update_dirty_chunks_async() const noexcept;

    bool has_lights(const chunk *c) const noexcept;

    void gen_chunk_buffer(chunk *c) const noexcept;

    void load_chunk(const chunk::worker_item *item) const noexcept;

    // Optional constrained terrain; fixed for the lifetime of the world so workers can read it unlocked.
    std::shared_ptr<const heightmap> world_heightmap;
    bool world_include_decorations{true};
    bool world_load_persisted_blocks{true};
    void init_chunk(chunk *c, int p, int q) noexcept;
    void create_chunk(chunk *c, int p, int q) noexcept;
    void delete_chunks() noexcept;
    void delete_all_chunks() noexcept;
    void force_chunks(player *player) noexcept;
    void check_workers() noexcept;
    [[nodiscard]] std::optional<std::pair<int, int>> select_chunk_for_worker(float planes[6][4], int p, int q,
                                                                             int worker_index) const noexcept;
    void dispatch_chunk_to_worker(chunk::worker *w, int a, int b) noexcept;
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
    // Matrix setup helpers — bind shader program, upload projection matrix uniform.
    std::pair<int, int> begin_3d_pass(const sdl_gl_helper::attrib *a, float matrix[16]) const noexcept;

    enum class Layer
    {
        BACKGROUND = 0,
        FOREGROUND = 1,
        LAYER_COUNT = 2
    };

    const sdl_gl_helper *simple_direct_medialayer;

    sdl_gl_helper::attrib s_block_attrib{};

    font_manager &active_fonts;
    shader_manager &world_shaders;
    texture_manager &world_textures;

    static constexpr auto MAX_CHUNKS = 8192;
    std::array<std::array<chunk *, MAX_CHUNKS>, static_cast<std::size_t>(Layer::LAYER_COUNT)> scene_graph_layers;

    std::size_t next_chunk_slot_in_view;

    // (p, q) -> chunk for O(1) lookup; mutated on the main thread only.
    std::unordered_map<std::uint64_t, chunk *> chunk_index;
    [[nodiscard]] static std::uint64_t chunk_key(int p, int q) noexcept
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(p)) << 32) |
               static_cast<std::uint32_t>(q);
    }

    player *active_player;

    bvh::v2::ThreadPool chunk_thread_pool{};
    std::vector<std::unique_ptr<chunk::worker>> chunk_workers;

    bloom_pass *const m_bloom;
};

#endif // WORLD_H
