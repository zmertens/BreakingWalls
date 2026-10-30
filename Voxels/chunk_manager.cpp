// World-owned chunk scene, lifecycle, neighbor coordination, and collision.
#include "world.h"

#include "chunk.h"
#include "craft_map.h"
#include "db.h"
#include "geometries.h"
#include "db_manager.h"
#include "item.h"
#include "matrix.h"
#include "player.h"
#include "sdl_gl_helper.h"
#include "worker_traits.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <ranges>
#include <string>

#include <bvhv2/bvh.h>
#include <bvhv2/executor.h>
#include <bvhv2/thread_pool.h>

namespace
{
    void load(const worker_item *item, const bool enable_heightmap,
              const float player_x, const float player_z, const heightmap *constrained,
              const bool include_decorations, const bool load_persisted_blocks,
              db_manager *db) noexcept
    {
        const int p = item->p, q = item->q;
        craft_map *block_map = item->block_maps.get(1, 1);
        craft_map *light_map = item->light_maps.get(1, 1);

        constexpr float FLATTEN_RADIUS = 256.0f;

        geometries::create_voxel_world(
            [](craft_map *m, int x, int y, int z, int w)
            { m->set(x, y, z, w); },
            block_map, p, q, world::BUILD_CHUNK_SIZE,
            enable_heightmap, player_x, player_z, FLATTEN_RADIUS, constrained, include_decorations);

        if (load_persisted_blocks && db)
        {
            db->load_blocks(block_map, p, q);
            db->load_lights(light_map, p, q);
        }
    }

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
} // namespace

void world::terrain(worker_item *item) noexcept
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
} // terrain

// ---------------------------------------------------------------------------
// Hit tests / collision
// ---------------------------------------------------------------------------
int world::highest_block(const float x, const float z) const noexcept
{
    int result = -1;
    const int nx = static_cast<int>(SDL_roundf(x));
    const int nz = static_cast<int>(SDL_roundf(z));
    const int p = chunked(x);
    const int q = chunked(z);
    if (const auto chunk_opt = find_chunk(p, q); chunk_opt.has_value())
    {
        const craft_map *map = &chunk_opt.value()->map;
        for (const auto [ex, ey, ez, ew] : *map)
        {
            if (item::is_obstacle(ew) && ex == nx && ez == nz)
                result = SDL_max(result, ey);
        }
    }
    return result;
}

int world::_hit_test(const craft_map *map, const float max_distance, const int previous,
                     float x, float y, float z, float vx, float vy, float vz,
                     int *hx, int *hy, int *hz) noexcept
{
    static constexpr int m = 32;
    int px = 0, py = 0, pz = 0;
    for (int i = 0; i < max_distance * m; i++)
    {
        const int nx = SDL_lroundf(x);
        const int ny = SDL_lroundf(y);
        if (const int nz = SDL_lroundf(z); nx != px || ny != py || nz != pz)
        {
            if (const int hw = map->get(nx, ny, nz); hw > 0)
            {
                if (previous)
                {
                    *hx = px;
                    *hy = py;
                    *hz = pz;
                }
                else
                {
                    *hx = nx;
                    *hy = ny;
                    *hz = nz;
                }
                return hw;
            }
            px = nx;
            py = ny;
            pz = nz;
        }
        x += vx / m;
        y += vy / m;
        z += vz / m;
    }
    return 0;
}

int world::hit_test(const int previous, const float x, const float y, const float z,
                    const float rx, const float ry, int *bx, int *by, int *bz) const noexcept
{
    int result = 0;
    float best = 0;
    const int p = chunked(x);
    const int q = chunked(z);
    float vx, vy, vz;
    matrix::compute_sight_vector(rx, ry, std::ref(vx), std::ref(vy), std::ref(vz));

    for (const chunk *c : active_chunks())
    {
        if (c == nullptr || chunk_distance(c, p, q) > 1)
            continue;
        int hx, hy, hz;
        const int hw = _hit_test(&c->map, 8, previous, x, y, z, vx, vy, vz, &hx, &hy, &hz);
        if (hw > 0)
        {
            if (const auto dist = SDL_sqrtf(SDL_powf(static_cast<float>(hx) - x, 2) + SDL_powf(static_cast<float>(hy) - y, 2) + SDL_powf(static_cast<float>(hz) - z, 2));
                best == 0 || dist < best)
            {
                best = dist;
                *bx = hx;
                *by = hy;
                *bz = hz;
                result = hw;
            }
        }
    }
    return result;
}

int world::hit_test_face(int *x, int *y, int *z, int *face) const noexcept
{
    const player::position *s = &active_player->pos;
    if (int w = this->hit_test(0, s->x, s->y, s->z, s->rx, s->ry, x, y, z);
        item::is_obstacle(w))
    {
        int hx, hy, hz;
        hit_test(1, s->x, s->y, s->z, s->rx, s->ry, &hx, &hy, &hz);
        const int dx = hx - *x, dy = hy - *y, dz = hz - *z;
        if (dx == -1 && dy == 0 && dz == 0)
        {
            *face = 0;
            return 1;
        }
        if (dx == 1 && dy == 0 && dz == 0)
        {
            *face = 1;
            return 1;
        }
        if (dx == 0 && dy == 0 && dz == -1)
        {
            *face = 2;
            return 1;
        }
        if (dx == 0 && dy == 0 && dz == 1)
        {
            *face = 3;
            return 1;
        }
        if (dx == 0 && dy == 1 && dz == 0)
        {
            auto degrees = SDL_roundf(static_cast<float>(matrix::to_degrees(SDL_atan2(s->x - hx, s->z - hz))));
            if (degrees < 0.f)
                degrees += 360.f;
            const auto top = static_cast<int>(((degrees + 45.f) / 90.f)) % 4;
            *face = 4 + top;
            return 1;
        }
        if (dx == 0 && dy == -1 && dz == 0)
        {
            *face = 8;
            return 1;
        }
    }
    return 0;
}

std::array<float, 2> world::camera_rotation() const noexcept
{
    const auto &p = active_player->pos;
    if (active_player->get_configs().view_mode() == player_configs::ViewMode::ORTHO)
        return {p.rx, -matrix::to_radians(90.0f)};
    return {p.rx, p.ry};
}

std::array<float, 3> world::camera_position() const noexcept
{
    const auto &p = active_player->pos;
    if (active_player->get_configs().view_mode() != player_configs::ViewMode::THIRD_PERSON)
        return {p.x, p.y, p.z};

    constexpr float boom_length = 6.0f, step = 0.1f, clearance = 0.3f;
    float vx, vy, vz;
    matrix::compute_sight_vector(p.rx, p.ry, vx, vy, vz);
    float dist = 0.0f;
    for (float d = step; d <= boom_length; d += step)
    {
        const float cx = p.x - vx * d, cy = p.y - vy * d, cz = p.z - vz * d;
        const auto chunk_opt = find_chunk(chunked(cx), chunked(cz));
        if (!chunk_opt.has_value())
            break;
        const craft_map *map = &chunk_opt.value()->map;
        // Probe around the camera so the near plane never clips into a wall.
        bool blocked = false;
        for (int ox = -1; ox <= 1 && !blocked; ox += 2)
            for (int oy = -1; oy <= 1 && !blocked; oy += 2)
                for (int oz = -1; oz <= 1 && !blocked; oz += 2)
                    blocked = item::is_obstacle(map->get(static_cast<int>(SDL_roundf(cx + ox * clearance)),
                                                         static_cast<int>(SDL_roundf(cy + oy * clearance)),
                                                         static_cast<int>(SDL_roundf(cz + oz * clearance))));
        if (blocked)
            break;
        dist = d;
    }
    return {p.x - vx * dist, p.y - vy * dist, p.z - vz * dist};
}

int world::collide(const int height, float *x, float *y, float *z) const noexcept
{
    int result = 0;
    const int p = this->chunked(*x);
    const int q = this->chunked(*z);
    const auto chunk_opt = find_chunk(p, q);
    if (!chunk_opt.has_value())
        return result;
    const chunk *c = chunk_opt.value();
    const craft_map *map = &c->map;
    const int nx = static_cast<int>(SDL_roundf(*x));
    const int ny = static_cast<int>(SDL_roundf(*y));
    const int nz = static_cast<int>(SDL_roundf(*z));
    const float px = *x - nx, py = *y - ny, pz = *z - nz;
    for (int dy = 0; dy < height; dy++)
    {
        constexpr float pad = 0.25f;
        if (px < -pad && item::is_obstacle(map->get(nx - 1, ny - dy, nz)))
            *x = nx - pad;
        if (px > pad && item::is_obstacle(map->get(nx + 1, ny - dy, nz)))
            *x = nx + pad;
        if (py < -pad && item::is_obstacle(map->get(nx, ny - dy - 1, nz)))
        {
            *y = ny - pad;
            result = 1;
        }
        if (py > pad && item::is_obstacle(map->get(nx, ny - dy + 1, nz)))
        {
            *y = ny + pad;
            result = 1;
        }
        if (pz < -pad && item::is_obstacle(map->get(nx, ny - dy, nz - 1)))
            *z = nz - pad;
        if (pz > pad && item::is_obstacle(map->get(nx, ny - dy, nz + 1)))
            *z = nz + pad;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Lighting helpers
// ---------------------------------------------------------------------------

bool world::has_lights(const chunk *c) const noexcept
{
    static constexpr std::array<std::array<int, 2>, 9> offsets = {{{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 0}, {0, 1}, {1, -1}, {1, 0}, {1, 1}}};
    return std::ranges::any_of(offsets, [&](const auto &off)
                               {
        const int dp = off[0], dq = off[1];
        const chunk *other = (dp || dq)
            ? find_chunk(c->p + dp, c->q + dq).value_or(nullptr)
            : c;
        return other && other->lights.size() != 0; });
}

void world::dirty_chunk(chunk *c) const noexcept
{
    c->dirty = 1;
    if (has_lights(c))
    {
        for (int dp = -1; dp <= 1; dp++)
        {
            for (int dq = -1; dq <= 1; dq++)
            {
                if (dp != 0 && dq != 0)
                    continue;
                if (auto other_opt = find_chunk(c->p + dp, c->q + dq); other_opt.has_value())
                    other_opt.value()->dirty = 1;
            }
        }
    }
}

void world::update_dirty_chunks_async() const noexcept
{
    for (auto &&worker : chunk_workers)
    {
        std::lock_guard<std::mutex> lock(worker->mtx);
        if (worker->state == WorkerState::IDLE)
        {
            for (chunk *c : active_chunks())
            {
                if (c == nullptr || !c->dirty)
                    continue;
                int index = (SDL_abs(c->p) ^ SDL_abs(c->q)) % chunk_workers.size();
                if (index != worker->index)
                    continue;

                worker_item *item = &worker->item;
                item->p = c->p;
                item->q = c->q;
                item->load = 0;

                for (int dp = -1; dp <= 1; dp++)
                {
                    for (int dq = -1; dq <= 1; dq++)
                    {
                        chunk *other = c;
                        if (dp || dq)
                        {
                            if (auto other_opt = find_chunk(c->p + dp, c->q + dq); !other_opt.has_value())
                                other = nullptr;
                            else
                                other = other_opt.value();
                        }
                        if (other)
                        {
                            item->block_maps.own(dp + 1, dq + 1, other->map);
                            item->light_maps.own(dp + 1, dq + 1, other->lights);
                        }
                    }
                }
                worker->state = WorkerState::BUSY;
                worker->cnd.notify_one();
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Synchronous mesh generation using neighboring chunks
// ---------------------------------------------------------------------------

void world::gen_chunk_buffer(chunk *c) const noexcept
{
    worker_item _item, *item = &_item;
    item->p = c->p;
    item->q = c->q;
    for (int dp = -1; dp <= 1; dp++)
    {
        for (int dq = -1; dq <= 1; dq++)
        {
            chunk *other = c;
            if (dp || dq)
            {
                if (auto other_opt = find_chunk(c->p + dp, c->q + dq); !other_opt.has_value())
                    other = nullptr;
                else
                    other = other_opt.value();
            }
            if (other)
            {
                item->block_maps.borrow(dp + 1, dq + 1, other->map);
                item->light_maps.borrow(dp + 1, dq + 1, other->lights);
            }
        }
    }
    terrain(item);
    c->generate(item);
    c->dirty = 0;
}

void world::create_chunk(chunk *c, const int p, const int q) noexcept
{
    init_chunk(c, p, q);
    worker_item _item, *item = &_item;
    item->p = c->p;
    item->q = c->q;
    item->block_maps.borrow(1, 1, c->map);
    item->light_maps.borrow(1, 1, c->lights);
    load_chunk(item);
}

// ---------------------------------------------------------------------------
// Chunk deletion
// ---------------------------------------------------------------------------

void world::delete_chunks() noexcept
{
    std::size_t count = this->next_chunk_slot_in_view;
    const player::position *s1 = &active_player->pos;
    auto &background_layer = scene_graph_layers[static_cast<std::size_t>(Layer::BACKGROUND)];
    const int p = chunked(s1->x), q = chunked(s1->z);

    for (std::size_t i = 1; i < count; ++i)
    {
        chunk *c = background_layer[i];
        if (c == nullptr)
            continue;
        if (chunk_distance(c, p, q) < DELETE_CHUNK_RADIUS)
            break;

        detach_chunk_from_layer(c);
        chunk_index.erase(chunk_key(c->p, c->q));
        c->release_buffers();
        delete c;

        --count;
        if (i != count)
        {
            background_layer[i] = background_layer[count];
            --i;
        }
        background_layer[count] = nullptr;
    }
    this->next_chunk_slot_in_view = count;
}

void world::delete_all_chunks() noexcept
{
    auto &background_layer = scene_graph_layers[static_cast<std::size_t>(Layer::BACKGROUND)];
    for (std::size_t i = 1; i < this->next_chunk_slot_in_view; ++i)
    {
        chunk *c = background_layer[i];
        if (c == nullptr)
            continue;
        detach_chunk_from_layer(c);
        c->release_buffers();
        delete c;
        background_layer[i] = nullptr;
    }
    chunk_index.clear();
    this->next_chunk_slot_in_view = 1;
    if (background_layer[0] != nullptr)
        background_layer[0]->children.clear();
}

// ---------------------------------------------------------------------------
// Chunk loading / ensuring
// ---------------------------------------------------------------------------

void world::check_workers() noexcept
{
    for (auto &&w : chunk_workers)
    {
        w->mtx.lock();
        if (w->state == WorkerState::DONE)
        {
            worker_item *item = &w->item;
            if (auto chunk_opt = find_chunk(item->p, item->q); chunk_opt.has_value())
            {
                chunk *c = chunk_opt.value();
                if (item->load)
                {
                    c->map = *item->block_maps.get(1, 1);
                    c->lights = *item->light_maps.get(1, 1);
                }
                c->generate(item);
            }
            else
            {
                SDL_free(item->data);
                item->data = nullptr;
            }
            item->block_maps.clear();
            item->light_maps.clear();
            w->state = WorkerState::IDLE;
        }
        w->mtx.unlock();
    }
}

void world::force_chunks(player *_player) noexcept
{
    player::position *s = &_player->pos;
    const int p = chunked(s->x), q = chunked(s->z);
    const int r = 1;
    for (int dp = -r; dp <= r; dp++)
    {
        for (int dq = -r; dq <= r; dq++)
        {
            const int a = p + dp, b = q + dq;
            if (auto chunk_opt = find_chunk(a, b); chunk_opt.has_value())
            {
                chunk *c = chunk_opt.value();
                if (c->dirty && c->buffer == 0)
                {
                    gen_chunk_buffer(c);
                }
            }
            else if (this->next_chunk_slot_in_view < MAX_CHUNKS)
            {
                auto &background_layer = scene_graph_layers[static_cast<std::size_t>(Layer::BACKGROUND)];
                chunk *c = new chunk{};
                background_layer[this->next_chunk_slot_in_view] = c;
                ++this->next_chunk_slot_in_view;
                create_chunk(c, a, b);
                gen_chunk_buffer(c);
            }
        }
    }
}

// Read-only scan: safe to run concurrently with other scans as long as the
// scene graph is not mutated until all scans have finished.
std::optional<std::pair<int, int>> world::select_chunk_for_worker(float planes[6][4], int p, int q,
                                                                  int worker_index) const noexcept
{
    int start = 0x0fffffff, best_score = start, best_a = 0, best_b = 0;
    for (int dp = -CREATE_CHUNK_RADIUS; dp <= CREATE_CHUNK_RADIUS; dp++)
    {
        for (int dq = -CREATE_CHUNK_RADIUS; dq <= CREATE_CHUNK_RADIUS; dq++)
        {
            int a = p + dp, b = q + dq;
            int index = (SDL_abs(a) ^ SDL_abs(b)) % chunk_thread_pool.get_thread_count();
            if (index != worker_index)
            {
                continue;
            }
            auto chunk_opt = find_chunk(a, b);
            if (chunk_opt.has_value() && !chunk_opt.value()->dirty)
            {
                continue;
            }
            int distance = SDL_max(SDL_abs(dp), SDL_abs(dq));
            const auto invisible = ~static_cast<int>(chunk_visible(planes, a, b, 0, item::TOTAL_BLOCKS));
            int priority = 0;
            if (chunk_opt.has_value())
            {
                chunk *c = chunk_opt.value();
                priority = c->buffer & c->dirty;
            }
            if (const int score = (invisible << 24) | (priority << 16) | distance; score < best_score)
            {
                best_score = score;
                best_a = a;
                best_b = b;
            }
        }
    }
    if (best_score == start)
        return std::nullopt;
    return std::pair{best_a, best_b};
}

// Mutates the scene graph and the worker; main thread only.
void world::dispatch_chunk_to_worker(worker *w, int a, int b) noexcept
{
    int load = 0;
    auto chunk_opt = find_chunk(a, b);
    chunk *c = nullptr;
    if (!chunk_opt.has_value())
    {
        load = 1;
        if (this->next_chunk_slot_in_view < MAX_CHUNKS)
        {
            auto &background_layer = scene_graph_layers[static_cast<std::size_t>(Layer::BACKGROUND)];
            c = new chunk{};
            background_layer[this->next_chunk_slot_in_view] = c;
            ++this->next_chunk_slot_in_view;
            init_chunk(c, a, b);
        }
        else
            return;
    }
    else
        c = chunk_opt.value();

    worker_item *item = &w->item;
    item->p = c->p;
    item->q = c->q;
    item->load = load;
    for (int dp = -1; dp <= 1; dp++)
    {
        for (int dq = -1; dq <= 1; dq++)
        {
            chunk *other = c;
            if (dp || dq)
            {
                auto other_opt = find_chunk(c->p + dp, c->q + dq);
                other = other_opt.has_value() ? other_opt.value() : nullptr;
            }
            if (other)
            {
                item->block_maps.own(dp + 1, dq + 1, other->map);
                item->light_maps.own(dp + 1, dq + 1, other->lights);
            }
        }
    }
    c->dirty = 0;
    w->state = WorkerState::BUSY;
    w->cnd.notify_one();
}

void world::ensure_chunks(player *_player) noexcept
{
    check_workers();

    force_chunks(_player);

    auto [width, height] = simple_direct_medialayer->get_window_size();
    const player::position *s = &_player->pos;
    float matrix[16];
    const auto cam = camera_position();
    const auto rot = camera_rotation();
    matrix::set_3d(matrix, width, height,
                   cam[0], cam[1], cam[2], rot[0], rot[1],
                   active_player->get_configs().fov(),
                   active_player->get_configs().ortho_scaling(),
                   RENDER_CHUNK_RADIUS);
    float planes[6][4];
    matrix::frustum_planes(planes, RENDER_CHUNK_RADIUS, matrix);
    const int p = chunked(s->x), q = chunked(s->z);

    // Only the main thread moves a worker out of IDLE, so this list stays valid.
    std::vector<worker *> idle;
    for (auto &&w : chunk_workers)
    {
        std::lock_guard<std::mutex> lock(w->mtx);
        if (w->state == WorkerState::IDLE)
        {
            idle.push_back(w.get());
        }
    }

    // Phase 1: each idle worker's candidate search runs on the pool. The scene
    // graph is only read here; the main thread waits before mutating it.
    std::vector<std::optional<std::pair<int, int>>> picks(idle.size());
    bvh::v2::ThreadPool::TaskGroup task_group{};
    for (std::size_t i = 0; i < idle.size(); ++i)
    {
        chunk_thread_pool.push(task_group, [this, &picks, &planes, &idle, i, p, q](std::size_t)
                               { picks[i] = select_chunk_for_worker(planes, p, q, idle[i]->index); });
    }
    chunk_thread_pool.wait(task_group);

    // Phase 2: creating chunks and copying maps stays serial.
    for (std::size_t i = 0; i < idle.size(); ++i)
    {
        if (!picks[i])
            continue;
        std::lock_guard<std::mutex> lock(idle[i]->mtx);
        dispatch_chunk_to_worker(idle[i], picks[i]->first, picks[i]->second);
    }
}

// ---------------------------------------------------------------------------
// Scene graph
// ---------------------------------------------------------------------------

void world::build_scene()
{
    auto &background_layer = scene_graph_layers[static_cast<std::size_t>(Layer::BACKGROUND)];

    chunk *root = new chunk{};
    root->bounds_min_x = -10000;
    root->bounds_min_z = -10000;
    root->bounds_max_x = 10000;
    root->bounds_max_z = 10000;
    root->parent = nullptr;
    background_layer[0] = root;

    std::fill(background_layer.begin() + 1, background_layer.end(), nullptr);

    auto &foreground_layer = scene_graph_layers[static_cast<std::size_t>(Layer::FOREGROUND)];
    std::ranges::fill(foreground_layer, nullptr);
}

void world::attach_chunk_to_layer(chunk *c, int layer_index) noexcept
{
    if (layer_index >= static_cast<int>(Layer::LAYER_COUNT) || c == nullptr)
        return;

    constexpr int CHUNK_SIZE = BUILD_CHUNK_SIZE;
    c->bounds_min_x = c->p * CHUNK_SIZE;
    c->bounds_min_z = c->q * CHUNK_SIZE;
    c->bounds_max_x = (c->p + 1) * CHUNK_SIZE - 1;
    c->bounds_max_z = (c->q + 1) * CHUNK_SIZE - 1;

    insert_chunk_into_spatial_tree(c);
}

void world::detach_chunk_from_layer(chunk *c) noexcept
{
    if (c == nullptr || c->parent == nullptr)
        return;
    remove_chunk_from_spatial_tree(c);
}

void world::insert_chunk_into_spatial_tree(chunk *c) const noexcept
{
    if (c == nullptr)
        return;

    chunk *root = scene_graph_layers.at(static_cast<std::size_t>(Layer::BACKGROUND)).at(0);
    if (root == nullptr)
        return;

    if (chunk *spatial_parent = root; spatial_parent != nullptr)
    {
        c->parent = spatial_parent;
        if (const bool is_attached = std::ranges::find(spatial_parent->children, c) != spatial_parent->children.cend();
            !is_attached)
        {
            spatial_parent->children.push_back(c);
        }
    }
}

void world::remove_chunk_from_spatial_tree(chunk *c) noexcept
{
    if (c == nullptr || c->parent == nullptr)
        return;
    std::erase(c->parent->children, c);
    c->parent = nullptr;
}

void world::traverse_chunks(const std::function<void(chunk *)> &callback) const noexcept
{
    chunk *root = scene_graph_layers.at(static_cast<std::size_t>(Layer::BACKGROUND)).at(0);
    if (root == nullptr)
        return;

    std::function<void(chunk *)> traverse_node = [&](chunk *node)
    {
        if (node == nullptr)
            return;

        if (node != root)
            callback(node);

        for (chunk *child : node->children)
            traverse_node(child);
    };

    traverse_node(root);
}

void world::traverse_chunks_in_bounds(const int min_p, const int min_q, const int max_p, const int max_q,
                                      const std::function<void(chunk *)> &callback) const noexcept
{
    constexpr int CHUNK_SIZE = BUILD_CHUNK_SIZE;
    const int min_x = min_p * CHUNK_SIZE;
    const int min_z = min_q * CHUNK_SIZE;
    const int max_x = (max_p + 1) * CHUNK_SIZE - 1;
    const int max_z = (max_q + 1) * CHUNK_SIZE - 1;

    chunk *root = scene_graph_layers.at(static_cast<std::size_t>(Layer::BACKGROUND)).at(0);
    if (root == nullptr)
        return;

    std::function<void(chunk *)> traverse_node = [&](chunk *node)
    {
        if (node == nullptr)
            return;

        if (!node->intersects_bounds(min_x, min_z, max_x, max_z))
            return;

        if (node != root)
            callback(node);

        for (chunk *child : node->children)
            traverse_node(child);
    };

    traverse_node(root);
}

// ---------------------------------------------------------------------------
// Chunk query helpers (used by many methods across all .cpp files)
// ---------------------------------------------------------------------------

int world::chunked(const float x) noexcept
{
    auto coordinate = [](auto x)
    { return static_cast<int>(SDL_floorf(SDL_roundf(x) / static_cast<float>(BUILD_CHUNK_SIZE))); };
    return coordinate(x);
}

std::optional<chunk *> world::find_chunk(const int p, const int q) const noexcept
{
    if (const auto it = chunk_index.find(chunk_key(p, q)); it != chunk_index.end())
        return it->second;
    return std::nullopt;
}

// Contiguous view of occupied slots; slot 0 is the root and is excluded.
std::span<chunk *const> world::active_chunks() const noexcept
{
    const auto &layer = scene_graph_layers[static_cast<std::size_t>(Layer::BACKGROUND)];
    return std::span<chunk *const>{layer}.subspan(1, next_chunk_slot_in_view - 1);
}

bool world::chunk_visible(float planes[6][4], const int p, const int q,
                          const int miny, const int maxy) const noexcept
{
    auto visible = [](auto planes, auto p, auto q, auto miny, auto maxy, auto is_ortho)
    {
        const auto miny_f = static_cast<float>(miny);
        const auto maxy_f = static_cast<float>(maxy);
        const float x = static_cast<float>(p * world::BUILD_CHUNK_SIZE - 1);
        const float z = static_cast<float>(q * world::BUILD_CHUNK_SIZE - 1);
        const float d = static_cast<float>(world::BUILD_CHUNK_SIZE + 1);
        const float points[8][3] = {
            {x + 0.f, miny_f, z + 0.f}, {x + d, miny_f, z + 0.f}, {x + 0.f, miny_f, z + d}, {x + d, miny_f, z + d}, {x + 0.f, maxy_f, z + 0.f}, {x + d, maxy_f, z + 0.f}, {x + 0.f, maxy_f, z + d}, {x + d, maxy_f, z + d}};
        const int n = is_ortho ? 4 : 6;
        for (int i = 0; i < n; i++)
        {
            int in = 0, out = 0;
            for (int j = 0; j < 8; j++)
            {
                const float d1 = planes[i][0] * points[j][0] + planes[i][1] * points[j][1] + planes[i][2] * points[j][2] + planes[i][3];
                if (d1 < 0)
                {
                    out++;
                }
                else
                {
                    in++;
                }
                if (in && out)
                {
                    break;
                }
            }

            if (in == 0)
            {
                return false;
            }
        }
        return true;
    };
    return visible(planes, p, q, miny, maxy, active_player->get_configs().ortho_scaling() != 0);
}

int world::chunk_distance(const chunk *c, const int p, const int q) noexcept
{
    auto distance = [](const chunk *c, const int p, const int q)
    {
        const int dp = SDL_abs(c->p - p);
        const int dq = SDL_abs(c->q - q);
        return SDL_max(dp, dq);
    };
    return distance(c, p, q);
}

void world::load_chunk(const worker_item *item) const noexcept
{
    load(item, true,
         active_player ? active_player->pos.x : 0.0f,
         active_player ? active_player->pos.z : 0.0f,
         world_heightmap.get(),
         world_include_decorations,
         world_load_persisted_blocks,
         world_db);
}

void world::init_chunk(chunk *c, const int p, const int q) noexcept
{
    c->initialize(p, q, world_db);
    chunk_index[chunk_key(p, q)] = c;
    dirty_chunk(c);
    attach_chunk_to_layer(c, static_cast<int>(Layer::BACKGROUND));
}

std::size_t world::get_chunk_count() const noexcept
{
    return this->next_chunk_slot_in_view - 1;
}

bool world::player_intersects_block(const int height, const float x, const float y, const float z,
                                    const int hx, const int hy, const int hz) noexcept
{
    const auto nx = static_cast<int>(SDL_roundf(x));
    const auto ny = static_cast<int>(SDL_roundf(y));
    const auto nz = static_cast<int>(SDL_roundf(z));
    for (int i = 0; i < height; i++)
    {
        if (nx == hx && ny - i == hy && nz == hz)
            return true;
    }
    return false;
}
