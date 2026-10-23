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

// Unified voxel chunk: spatial scene-graph node + terrain/light data.
struct chunk final
{
    // 3x3 neighborhood of maps keyed by a * 3 + b. Owned copies are freed automatically;
    // borrowed entries use a non-owning shared_ptr and never free the map.
    class neighbor_maps
    {
    public:
        [[nodiscard]] craft_map *get(const int a, const int b) const noexcept
        {
            const auto &slot = m_slots[a * 3 + b];
            return slot ? slot.get() : nullptr;
        }
        void own(const int a, const int b, const craft_map &src)
        {
            m_slots[a * 3 + b] = std::make_shared<craft_map>(src);
        }
        void borrow(const int a, const int b, craft_map &src)
        {
            m_slots[a * 3 + b] = std::shared_ptr<craft_map>(std::shared_ptr<void>{}, &src);
        }
        void clear() noexcept
        {
            std::ranges::for_each(m_slots.begin(), m_slots.end(), [](auto &slot)
                                  { slot.reset(); });
        }

    private:
        std::array<std::shared_ptr<craft_map>, 9u> m_slots;
    };

    // CPU mesh job. Maps are borrowed for synchronous jobs and copied for workers.
    struct worker_item
    {
        int p{};
        int q{};
        int load{};
        neighbor_maps block_maps{};
        neighbor_maps light_maps{};
        int miny{};
        int maxy{};
        int faces{};
        float *data{};
    };

    enum class WorkerState : int
    {
        IDLE = 0,
        BUSY = 1,
        DONE = 2
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

    [[nodiscard]] static int coordinate(float x) noexcept;
    [[nodiscard]] int distance(int p, int q) const noexcept;
    [[nodiscard]] static bool visible(float planes[6][4], int p, int q,
                                      int miny, int maxy, bool orthographic) noexcept;

    void initialize(int p, int q) noexcept;
    void generate(worker_item *item) noexcept;
    void release_buffers() noexcept;

    static void load(const worker_item *item, bool enable_heightmap,
                     float player_x, float player_z, const heightmap *constrained = nullptr,
                     bool include_decorations = true, bool load_persisted_blocks = true) noexcept;
    static void compute(worker_item *item) noexcept;

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
