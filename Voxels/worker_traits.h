#ifndef WORKER_TRAITS_H
#define WORKER_TRAITS_H

#include "craft_map.h"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <ranges>
#include <thread>

struct worker_item final
{
private:
    // 3x3 neighborhood of maps keyed by a * 3 + b. Owned copies are freed automatically;
    // borrowed entries use a non-owning shared_ptr and never free the map.
    template <typename Map>
    class neighbor_maps
    {
    public:
        [[nodiscard]] Map *get(const int a, const int b) const noexcept
        {
            const auto &slot = m_slots[a * 3 + b];
            return slot ? slot.get() : nullptr;
        }
        void own(const int a, const int b, const Map &src)
        {
            m_slots[a * 3 + b] = std::make_shared<Map>(src);
        }
        void borrow(const int a, const int b, Map &src)
        {
            m_slots[a * 3 + b] = std::shared_ptr<Map>(std::shared_ptr<void>{}, &src);
        }
        void clear() noexcept
        {
            std::ranges::for_each(m_slots.begin(), m_slots.end(), [](auto &slot)
                                  { slot.reset(); });
        }

    private:
        std::array<std::shared_ptr<Map>, 9u> m_slots;
    };

public:
    int p{};
    int q{};
    int load{};
    neighbor_maps<craft_map> block_maps{};
    neighbor_maps<craft_map> light_maps{};
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

struct worker final
{
    int index{};
    WorkerState state{WorkerState::IDLE};
    std::thread thrd;
    std::mutex mtx;
    std::condition_variable cnd;
    worker_item item;
    bool should_stop{};
};

#endif // WORKER_TRAITS_H