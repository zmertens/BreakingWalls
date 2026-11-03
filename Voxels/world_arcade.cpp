#include "world.h"

#include "craft_map.h"

#include <MazeBuilder/randomizer.h>

#include <algorithm>
#include <string>

namespace
{
    constexpr int FALLBACK_FLOOR_Y = 48; // used when the world has no heightmap
}

void world::start_arcade(const int level, mazes::randomizer &rng) noexcept
{
    if (arena_active.load())
    {
        stop_arcade();
    }

    const int size = 2 * arcade.cells_for_level(level) + 1;
    int origin_x = 0, origin_z = 0;
    if (world_heightmap)
    {
        origin_x = world_heightmap->side / 2 - size / 2;
        origin_z = world_heightmap->side / 2 - size / 2;
    }

    // The floor sits flush with the island's average height under the maze.
    int floor_y = FALLBACK_FLOOR_Y;
    if (world_heightmap)
    {
        long long total = 0;
        int samples = 0;
        for (int z = origin_z; z < origin_z + size; ++z)
        {
            for (int x = origin_x; x < origin_x + size; ++x)
            {
                if (world_heightmap->contains(x, z))
                {
                    total += world_heightmap->column_height(x, z);
                    ++samples;
                }
            }
        }
        floor_y = std::clamp(samples > 0 ? static_cast<int>(total / samples) - 1 : FALLBACK_FLOOR_Y, 4, 200);
    }

    arcade.start_level(level, rng, origin_x, floor_y, origin_z, world_heightmap);
    const auto built = arcade.level();
    std::atomic_store(&arena, std::shared_ptr<const arcade_level>(built));
    arena_active.store(true);

    // Chunks that are already loaded are edited now; chunks that load later are stamped by the workers.
    for (int z = built->min_z(); z < built->max_z(); ++z)
    {
        for (int x = built->min_x(); x < built->max_x(); ++x)
        {
            for (int y = 0, top = built->top_y(x, z); y <= top; ++y)
            {
                if (const int block = built->block_at(x, y, z); block >= 0 && block != get_block(x, y, z))
                {
                    set_block(x, y, z, block);
                }
            }
        }
    }

    ticker.push("Level " + std::to_string(level) + ": reach the bottom-right corner");
}

void world::stop_arcade() noexcept
{
    if (const auto built = std::atomic_load(&arena))
    {
        // Put the heightmap back where the arena replaced it.
        for (int z = built->min_z(); z < built->max_z(); ++z)
        {
            for (int x = built->min_x(); x < built->max_x(); ++x)
            {
                for (int y = 0, top = built->top_y(x, z); y <= top; ++y)
                {
                    if (built->block_at(x, y, z) < 0)
                    {
                        continue;
                    }
                    if (const int original = built->terrain_block_at(x, y, z); get_block(x, y, z) != original)
                    {
                        set_block(x, y, z, original);
                    }
                }
            }
        }
    }
    arena_active.store(false);
    std::atomic_store(&arena, std::shared_ptr<const arcade_level>(nullptr));
    arcade.stop();
}

void world::arcade_primary_action(mazes::randomizer &rng) noexcept
{
    switch (arcade.state())
    {
    case arcade_state::RUNNING:
        arcade.pause();
        ticker.set_message("PAUSED - press Q to resume");
        break;
    case arcade_state::PAUSED:
        arcade.resume();
        ticker.set_message("GO!");
        break;
    case arcade_state::WON:
        start_arcade(arcade.level_number() + 1, rng);
        break;
    default:
        break;
    }
}

void world::spawn_particles(const float x, const float y, const float z, const int count,
                            const particle_system::kind k) const noexcept
{
    if (active_player && active_player->get_configs().show_particles())
    {
        particles.burst(x, y, z, count, k);
    }
}

bool world::arcade_collect() noexcept
{
    std::vector<arcade_block_change> changes;
    const bool scored = active_player && arcade.collect(active_player->get_selected_item_index(), changes);
    if (scored && !changes.empty())
    {
        spawn_particles(static_cast<float>(changes.front().x), static_cast<float>(changes.front().y),
                        static_cast<float>(changes.front().z), 30, particle_system::kind::SPARK);
    }
    apply_arcade_changes(changes);
    if (scored)
    {
        ticker.push("+" + std::to_string(arcade.rules.block_points) + " points");
    }
    return scored;
}

void world::apply_arcade_changes(std::vector<arcade_block_change> &changes) const noexcept
{
    for (const auto &change : changes)
    {
        set_block(change.x, change.y, change.z, change.block);
    }
    changes.clear();
}

void world::stamp_arena(craft_map *map, const int p, const int q) const noexcept
{
    if (!arena_active.load())
    {
        return;
    }
    const auto built = std::atomic_load(&arena);
    if (!built)
    {
        return;
    }
    const int x0 = std::max(p * BUILD_CHUNK_SIZE, built->min_x());
    const int x1 = std::min((p + 1) * BUILD_CHUNK_SIZE, built->max_x());
    const int z0 = std::max(q * BUILD_CHUNK_SIZE, built->min_z());
    const int z1 = std::min((q + 1) * BUILD_CHUNK_SIZE, built->max_z());
    for (int x = x0; x < x1; ++x)
    {
        for (int z = z0; z < z1; ++z)
        {
            for (int y = 0, top = built->top_y(x, z); y <= top; ++y)
            {
                if (const int block = built->block_at(x, y, z); block >= 0)
                {
                    map->set(x, y, z, block);
                }
            }
        }
    }
}
