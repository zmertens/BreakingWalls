#ifndef CHUNK_H
#define CHUNK_H

#include <cstdint>
#include <vector>

#include "db.h"
#include "craft_map.h"

// CPU mesh job. Maps are borrowed for synchronous jobs and copied for workers.
struct worker_item
{
    int p{};
    int q{};
    int load{};
    craft_map *block_maps[3][3]{};
    craft_map *light_maps[3][3]{};
    int miny{};
    int maxy{};
    int faces{};
    float *data{};
};

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
    craft_map map{};
    craft_map lights{};
    db::sign_list signs{};
    int p{0};
    int q{0};
    int faces{0};
    int sign_faces{0};
    int dirty{0};
    int miny{0};
    int maxy{0};
    std::uint32_t buffer{0};
    std::uint32_t sign_buffer{0};

    [[nodiscard]] static int coordinate(float x) noexcept;
    [[nodiscard]] int distance(int p, int q) const noexcept;
    [[nodiscard]] static bool visible(float planes[6][4], int p, int q,
                                      int miny, int maxy, bool orthographic) noexcept;
    void initialize(int p, int q) noexcept;
    void generate(worker_item *item) noexcept;
    void release_buffers() noexcept;
    static void load(const worker_item *item, bool enable_heightmap,
                     float player_x, float player_z) noexcept;
    static void compute(worker_item *item) noexcept;

    [[nodiscard]] bool intersects_bounds(const int min_x, const int min_z,
                                         const int max_x, const int max_z) const noexcept
    {
        return !(bounds_max_x < min_x || bounds_min_x > max_x ||
                 bounds_max_z < min_z || bounds_min_z > max_z);
    }

private:
    static void occlusion(char neighbors[27], char lights[27], float shades[27],
                          float ao[6][4], float light[6][4]) noexcept;
    static void light_fill(char *opaque, char *light, int x, int y, int z,
                           int w, int force) noexcept;
};

#endif // CHUNK_H
