#include "world.h"

#include "geometries.h"
#include "worker_traits.h"

#include <SDL3/SDL.h>

#include <vector>

namespace
{
// Mesh scratch space includes the chunk and its eight neighbors.
#define XZ_SIZE (world::BUILD_CHUNK_SIZE * 3 + 2)
#define XZ_LO (world::BUILD_CHUNK_SIZE)
#define XZ_HI (world::BUILD_CHUNK_SIZE * 2 + 1)
#define Y_SIZE 258
#define XYZ(x, y, z) ((y) * XZ_SIZE * XZ_SIZE + (x) * XZ_SIZE + (z))
#define XZ(x, z) ((x) * XZ_SIZE + (z))

    void occlusion(char neighbors[27], char lights[27], float shades[27],
                   float ao[6][4], float light[6][4]) noexcept
    {
        static constexpr int lookup3[6][4][3] = {
            {{0, 1, 3}, {2, 1, 5}, {6, 3, 7}, {8, 5, 7}},
            {{18, 19, 21}, {20, 19, 23}, {24, 21, 25}, {26, 23, 25}},
            {{6, 7, 15}, {8, 7, 17}, {24, 15, 25}, {26, 17, 25}},
            {{0, 1, 9}, {2, 1, 11}, {18, 9, 19}, {20, 11, 19}},
            {{0, 3, 9}, {6, 3, 15}, {18, 9, 21}, {24, 15, 21}},
            {{2, 5, 11}, {8, 5, 17}, {20, 11, 23}, {26, 17, 23}}};
        static constexpr int lookup4[6][4][4] = {
            {{0, 1, 3, 4}, {1, 2, 4, 5}, {3, 4, 6, 7}, {4, 5, 7, 8}},
            {{18, 19, 21, 22}, {19, 20, 22, 23}, {21, 22, 24, 25}, {22, 23, 25, 26}},
            {{6, 7, 15, 16}, {7, 8, 16, 17}, {15, 16, 24, 25}, {16, 17, 25, 26}},
            {{0, 1, 9, 10}, {1, 2, 10, 11}, {9, 10, 18, 19}, {10, 11, 19, 20}},
            {{0, 3, 9, 12}, {3, 6, 12, 15}, {9, 12, 18, 21}, {12, 15, 21, 24}},
            {{2, 5, 11, 14}, {5, 8, 14, 17}, {11, 14, 20, 23}, {14, 17, 23, 26}}};
        static constexpr float curve[4] = {0.0, 0.25, 0.5, 0.75};

        for (int i = 0; i < 6; i++)
        {
            for (int j = 0; j < 4; j++)
            {
                const int corner = neighbors[lookup3[i][j][0]];
                const int side1 = neighbors[lookup3[i][j][1]];
                const int side2 = neighbors[lookup3[i][j][2]];
                const int value = side1 && side2 ? 3 : corner + side1 + side2;
                float shade_sum = 0, light_sum = 0;
                const int is_light = lights[13] == 15;
                for (int k = 0; k < 4; k++)
                {
                    shade_sum += shades[lookup4[i][j][k]];
                    light_sum += lights[lookup4[i][j][k]];
                }
                if (is_light)
                    light_sum = 15 * 4 * 10;
                const float total = curve[value] + shade_sum / 4.0f;
                ao[i][j] = SDL_min(total, 1.0f);
                light[i][j] = light_sum / 15.0f / 4.0f;
            }
        }
    }

    void light_fill(char *opaque, char *light, const int x, const int y, const int z,
                    const int w, const int force) noexcept
    {
        struct entry
        {
            int x, y, z, w;
        };
        std::vector<entry> stk;
        stk.reserve(512);

        if (x + w < XZ_LO || z + w < XZ_LO)
            return;
        if (x - w > XZ_HI || z - w > XZ_HI)
            return;
        if (y < 0 || y >= Y_SIZE)
            return;
        if (light[XYZ(x, y, z)] >= w)
            return;
        if (!force && opaque[XYZ(x, y, z)])
            return;
        light[XYZ(x, y, z)] = static_cast<char>(w);
        if (w - 1 > 0)
        {
            stk.push_back({x - 1, y, z, w - 1});
            stk.push_back({x + 1, y, z, w - 1});
            stk.push_back({x, y - 1, z, w - 1});
            stk.push_back({x, y + 1, z, w - 1});
            stk.push_back({x, y, z - 1, w - 1});
            stk.push_back({x, y, z + 1, w - 1});
        }

        while (!stk.empty())
        {
            const auto [cx, cy, cz, cw] = stk.back();
            stk.pop_back();
            if (cx + cw < XZ_LO || cz + cw < XZ_LO)
                continue;
            if (cx - cw > XZ_HI || cz - cw > XZ_HI)
                continue;
            if (cy < 0 || cy >= Y_SIZE)
                continue;
            if (light[XYZ(cx, cy, cz)] >= cw)
                continue;
            if (opaque[XYZ(cx, cy, cz)])
                continue;
            light[XYZ(cx, cy, cz)] = static_cast<char>(cw);
            if (cw - 1 <= 0)
                continue;
            stk.push_back({cx - 1, cy, cz, cw - 1});
            stk.push_back({cx + 1, cy, cz, cw - 1});
            stk.push_back({cx, cy - 1, cz, cw - 1});
            stk.push_back({cx, cy + 1, cz, cw - 1});
            stk.push_back({cx, cy, cz - 1, cw - 1});
            stk.push_back({cx, cy, cz + 1, cw - 1});
        }
    }

    void terrain(worker_item *item) noexcept
    {
        auto *opaque = static_cast<char *>(SDL_calloc(XZ_SIZE * XZ_SIZE * Y_SIZE, sizeof(char)));
        auto *light = static_cast<char *>(SDL_calloc(XZ_SIZE * XZ_SIZE * Y_SIZE, sizeof(char)));
        auto *highest = static_cast<int *>(SDL_calloc(XZ_SIZE * XZ_SIZE, sizeof(int)));

        if (!opaque || !light || !highest)
        {
            SDL_free(opaque);
            SDL_free(light);
            SDL_free(highest);
            item->data = nullptr;
            item->faces = 0;
            return;
        }

        int ox = item->p * world::BUILD_CHUNK_SIZE - world::BUILD_CHUNK_SIZE - 1;
        int oy = -1;
        int oz = item->q * world::BUILD_CHUNK_SIZE - world::BUILD_CHUNK_SIZE - 1;

        int has_light = 0;
        for (int a = 0; a < 3; a++)
            for (int b = 0; b < 3; b++)
                if (craft_map *map = item->light_maps.get(a, b); map && map->size())
                    has_light = 1;

        // Populate opaque array
        for (int a = 0; a < 3; a++)
        {
            for (int b = 0; b < 3; b++)
            {
                craft_map *block_map = item->block_maps.get(a, b);
                if (!block_map)
                    continue;
                for (const auto [ex, ey, ez, ew] : *block_map)
                {
                    int x = ex - ox, y = ey - oy, z = ez - oz, w = ew;
                    if (x < 0 || y < 0 || z < 0)
                        continue;
                    if (x >= XZ_SIZE || y >= Y_SIZE || z >= XZ_SIZE)
                        continue;
                    opaque[XYZ(x, y, z)] = !item::is_transparent(w);
                    if (opaque[XYZ(x, y, z)])
                        highest[XZ(x, z)] = SDL_max(highest[XZ(x, z)], y);
                }
            }
        }

        // Flood fill light
        if (has_light)
        {
            for (int a = 0; a < 3; a++)
            {
                for (int b = 0; b < 3; b++)
                {
                    craft_map *map = item->light_maps.get(a, b);
                    if (!map)
                        continue;
                    for (const auto [ex, ey, ez, ew] : *map)
                        light_fill(opaque, light, ex - ox, ey - oy, ez - oz, ew, 1);
                }
            }
        }

        craft_map *block_map = item->block_maps.get(1, 1);

        // Count exposed faces
        int miny = 256, maxy = 0, faces = 0;
        for (const auto [ex, ey, ez, ew] : *block_map)
        {
            if (ew <= 0)
                continue;
            int x = ex - ox, y = ey - oy, z = ez - oz;
            int f1 = !opaque[XYZ(x - 1, y, z)], f2 = !opaque[XYZ(x + 1, y, z)];
            int f3 = !opaque[XYZ(x, y + 1, z)], f4 = !opaque[XYZ(x, y - 1, z)] && (ey > 0);
            int f5 = !opaque[XYZ(x, y, z - 1)], f6 = !opaque[XYZ(x, y, z + 1)];
            int total = f1 + f2 + f3 + f4 + f5 + f6;
            if (total == 0)
                continue;
            if (item::is_plant(ew))
                total = 4;
            miny = SDL_min(miny, ey);
            maxy = SDL_max(maxy, ey);
            faces += total;
        }

        // Generate geometry
        static constexpr int components = 10;
        float *data = sdl_gl_helper::malloc_faces(components, faces);
        int offset = 0;
        for (const auto [ex, ey, ez, ew] : *block_map)
        {
            if (ew <= 0)
                continue;
            int x = ex - ox, y = ey - oy, z = ez - oz;
            int f1 = !opaque[XYZ(x - 1, y, z)], f2 = !opaque[XYZ(x + 1, y, z)];
            int f3 = !opaque[XYZ(x, y + 1, z)], f4 = !opaque[XYZ(x, y - 1, z)] && (ey > 0);
            int f5 = !opaque[XYZ(x, y, z - 1)], f6 = !opaque[XYZ(x, y, z + 1)];
            int total = f1 + f2 + f3 + f4 + f5 + f6;
            if (total == 0)
                continue;

            char neighbors[27] = {0};
            char lights[27] = {0};
            float shades[27] = {0};
            int index = 0;
            for (int dx = -1; dx <= 1; dx++)
            {
                for (int dy = -1; dy <= 1; dy++)
                {
                    for (int dz = -1; dz <= 1; dz++)
                    {
                        neighbors[index] = opaque[XYZ(x + dx, y + dy, z + dz)];
                        lights[index] = light[XYZ(x + dx, y + dy, z + dz)];
                        shades[index] = 0;
                        if (int hi = XZ(x + dx, z + dz);
                            hi >= 0 && hi < XZ_SIZE * XZ_SIZE && y + dy <= highest[hi])
                        {
                            for (int oy1 = 0; oy1 < 8; oy1++)
                            {
                                if (y + dy + oy1 >= Y_SIZE)
                                    break;
                                if (opaque[XYZ(x + dx, y + dy + oy1, z + dz)])
                                {
                                    shades[index] = 1.0f - oy1 * 0.125f;
                                    break;
                                }
                            }
                        }
                        index++;
                    }
                }
            }

            float ao[6][4], light2[6][4];
            occlusion(neighbors, lights, shades, ao, light2);

            if (item::is_plant(ew))
            {
                total = 4;
                float min_ao = 1, max_light = 0;
                for (int a = 0; a < 6; a++)
                    for (int b = 0; b < 4; b++)
                    {
                        min_ao = SDL_min(min_ao, ao[a][b]);
                        max_light = SDL_max(max_light, light2[a][b]);
                    }
                float rotation = SDL_sinf(static_cast<float>(ex) * 12.9898f + static_cast<float>(ez) * 78.233f) * 43758.5453f;
                geometries::make_plant(data + offset, min_ao, max_light,
                                       static_cast<float>(ex), static_cast<float>(ey), static_cast<float>(ez),
                                       0.5f, ew, rotation);
            }
            else
            {
                geometries::make_cube(data + offset, ao, light2,
                                      f1, f2, f3, f4, f5, f6,
                                      static_cast<float>(ex), static_cast<float>(ey), static_cast<float>(ez), 0.5f, ew);
            }
            offset += total * 60;
        }

        SDL_free(opaque);
        SDL_free(light);
        SDL_free(highest);
        item->miny = miny;
        item->maxy = maxy;
        item->faces = faces;
        item->data = data;
    }
}

bool world::worker_run(worker *w) const noexcept
{
    while (true)
    {
        std::unique_lock<std::mutex> my_lock(w->mtx);
        w->cnd.wait(my_lock, [w]
                    { return w->state == WorkerState::BUSY || w->should_stop; });
        if (w->should_stop)
        {
            break;
        }
        worker_item *item = &w->item;
        my_lock.unlock();

        if (item->load)
        {
            this->load_chunk(item);
        }
        terrain(item);

        std::lock_guard<std::mutex> done_lock(w->mtx);
        w->state = WorkerState::DONE;
    }
    return true;
}

void world::init_worker_threads() noexcept
{
    for (int i = 0; i < chunk_thread_pool.get_thread_count(); i++)
    {
        auto w = std::make_unique<worker>();
        w->index = i;
        w->state = WorkerState::IDLE;
        w->should_stop = false;
        chunk_workers.emplace_back(std::move(w));
        worker *worker_ptr = chunk_workers.back().get();
        worker_ptr->thrd = std::thread([this, worker_ptr]()
                                       { this->worker_run(static_cast<worker *>(worker_ptr)); });
    }
}

void world::cleanup_worker_threads() noexcept
{
    for (auto &&w : chunk_workers)
    {
        std::lock_guard<std::mutex> lock(w->mtx);
        w->should_stop = true;
        w->cnd.notify_one();
    }
    for (auto &&w : chunk_workers)
    {
        // A worker still holds pointers into its job until the thread exits.
        if (w->thrd.joinable())
        {
            w->thrd.join();
        }
        SDL_free(w->item.data);
        w->item = {};
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "worker thread %d finished!", w->index);
    }
    chunk_workers.clear();
    chunk_thread_pool.wait_all();
}
