#ifndef CHUNK_H
#define CHUNK_H

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <memory>
#include <ranges>
#include <span>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "craft_map.h"
#include "db.h"
#include "heightmap.h"

struct worker_item;

// Unified voxel chunk: spatial scene-graph node + terrain/light data.
struct chunk final
{
    void initialize(int p, int q) noexcept;

    void generate(worker_item *item) noexcept;

    void release_buffers() noexcept;

    [[nodiscard]] bool intersects_bounds(const int min_x, const int min_z,
                                         const int max_x, const int max_z) const noexcept
    {
        return !(bounds_max_x < min_x || bounds_min_x > max_x ||
                 bounds_max_z < min_z || bounds_min_z > max_z);
    }

    std::span<chunk *> spatial_hierarchy{};
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

private:
    static void occlusion(char neighbors[27], char lights[27], float shades[27],
                          float ao[6][4], float light[6][4]) noexcept;
    static void light_fill(char *opaque, char *light, int x, int y, int z,
                           int w, int force) noexcept;
};

#endif // CHUNK_H
