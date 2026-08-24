// chunk_manager.cpp — chunk lifecycle, worker threads, collision, lighting, terrain
#include "world.h"

#include "db.h"
#include "geometries.h"
#include "item.h"
#include "matrix.h"
#include "sdl_gl_helper.h"

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <glad/glad.h>
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <mutex>
#include <ranges>
#include <thread>
#include <vector>

// Match defines in world.cpp
#define CREATE_CHUNK_RADIUS 10
#define RENDER_CHUNK_RADIUS 20
#define BUILD_CHUNK_SIZE 32
#define DELETE_CHUNK_RADIUS 14
#define CHUNK_WORKERS_TOTAL 4

// Macros used in light_fill / compute_chunk
#define XZ_SIZE  (BUILD_CHUNK_SIZE * 3 + 2)
#define XZ_LO    (BUILD_CHUNK_SIZE)
#define XZ_HI    (BUILD_CHUNK_SIZE * 2 + 1)
#define Y_SIZE   258
#define XYZ(x, y, z) ((y) * XZ_SIZE * XZ_SIZE + (x) * XZ_SIZE + (z))
#define XZ(x, z)     ((x) * XZ_SIZE + (z))

// ---------------------------------------------------------------------------
// Worker threads
// ---------------------------------------------------------------------------

bool world::worker_run(worker *w) const noexcept
{
    while (true)
    {
        std::unique_lock<std::mutex> my_lock(w->mtx);
        w->cnd.wait(my_lock, [w]
                    { return w->state == WorkerState::BUSY || w->should_stop; });
        if (w->should_stop)
            break;

        worker_item *item = &w->item;
        my_lock.unlock();

        if (item->load)
            this->load_chunk(item);
        this->compute_chunk(item);

        std::lock_guard<std::mutex> done_lock(w->mtx);
        w->state = WorkerState::DONE;
    }
    return true;
}

void world::init_worker_threads() noexcept
{
    chunk_workers.reserve(CHUNK_WORKERS_TOTAL);
    for (int i = 0; i < CHUNK_WORKERS_TOTAL; i++)
    {
        auto w = std::make_unique<worker>();
        w->index       = i;
        w->state       = WorkerState::IDLE;
        w->should_stop = false;
        chunk_workers.emplace_back(std::move(w));
        worker *worker_ptr = chunk_workers.back().get();
        worker_ptr->thrd = std::thread([this, worker_ptr]()
                                       { this->worker_run(worker_ptr); });
    }
}

void world::cleanup_worker_threads() noexcept
{
    for (auto &&w : chunk_workers)
    {
        w->mtx.lock();
        w->should_stop = true;
        w->cnd.notify_one();
        w->mtx.unlock();
    }
    for (auto &&w : chunk_workers)
    {
#if !defined(__EMSCRIPTEN__)
        w->thrd.join();
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "worker thread %d finished!", w->index);
#else
        if (w->thrd.joinable())
            w->thrd.detach();
#endif
    }
    chunk_workers.clear();
}

// ---------------------------------------------------------------------------
// Frustum / visibility
// ---------------------------------------------------------------------------

bool world::chunk_visible(float planes[6][4], const int p, const int q,
                          const int miny, const int maxy) const noexcept
{
    const auto miny_f = static_cast<float>(miny);
    const auto maxy_f = static_cast<float>(maxy);
    const float x = static_cast<float>(p * BUILD_CHUNK_SIZE - 1);
    const float z = static_cast<float>(q * BUILD_CHUNK_SIZE - 1);
    const float d = static_cast<float>(BUILD_CHUNK_SIZE + 1);
    const float points[8][3] = {
        {x + 0.f, miny_f, z + 0.f}, {x + d, miny_f, z + 0.f},
        {x + 0.f, miny_f, z + d},   {x + d, miny_f, z + d},
        {x + 0.f, maxy_f, z + 0.f}, {x + d, maxy_f, z + 0.f},
        {x + 0.f, maxy_f, z + d},   {x + d, maxy_f, z + d}};
    const int n = active_player->_configs.ortho_scaling() ? 4 : 6;
    for (int i = 0; i < n; i++)
    {
        int in = 0, out = 0;
        for (int j = 0; j < 8; j++)
        {
            const float d1 = planes[i][0] * points[j][0]
                           + planes[i][1] * points[j][1]
                           + planes[i][2] * points[j][2]
                           + planes[i][3];
            if (d1 < 0) out++; else in++;
            if (in && out) break;
        }
        if (in == 0) return false;
    }
    return true;
}

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
        const voxels_map *map = &chunk_opt.value()->map;
        for (const auto [ex, ey, ez, ew] : *map)
        {
            if (item::is_obstacle(ew) && ex == nx && ez == nz)
                result = SDL_max(result, ey);
        }
    }
    return result;
}

int world::_hit_test(const voxels_map *map, const float max_distance, const int previous,
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
                if (previous) { *hx = px; *hy = py; *hz = pz; }
                else          { *hx = nx; *hy = ny; *hz = nz; }
                return hw;
            }
            px = nx; py = ny; pz = nz;
        }
        x += vx / m; y += vy / m; z += vz / m;
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

    const auto &background_layer = scene_graph_layers[static_cast<std::size_t>(Layer::BACKGROUND)];
    for (std::size_t i = 1; i < next_chunk_slot_in_view; ++i)
    {
        const chunk *c = background_layer[i];
        if (c == nullptr || chunk_distance(c, p, q) > 1)
            continue;
        int hx, hy, hz;
        const int hw = _hit_test(&c->map, 8, previous, x, y, z, vx, vy, vz, &hx, &hy, &hz);
        if (hw > 0)
        {
            if (const auto dist = SDL_sqrtf(SDL_powf(static_cast<float>(hx) - x, 2)
                                         + SDL_powf(static_cast<float>(hy) - y, 2)
                                         + SDL_powf(static_cast<float>(hz) - z, 2));
                best == 0 || dist < best)
            {
                best = dist;
                *bx = hx; *by = hy; *bz = hz;
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
        if (dx == -1 && dy == 0 && dz == 0) { *face = 0; return 1; }
        if (dx ==  1 && dy == 0 && dz == 0) { *face = 1; return 1; }
        if (dx == 0 && dy == 0 && dz == -1) { *face = 2; return 1; }
        if (dx == 0 && dy == 0 && dz ==  1) { *face = 3; return 1; }
        if (dx == 0 && dy == 1 && dz == 0)
        {
            auto degrees = SDL_roundf(static_cast<float>(matrix::to_degrees(SDL_atan2(s->x - hx, s->z - hz))));
            if (degrees < 0.f) degrees += 360.f;
            const auto top = static_cast<int>(((degrees + 45.f) / 90.f)) % 4;
            *face = 4 + top; return 1;
        }
        if (dx == 0 && dy == -1 && dz == 0) { *face = 8; return 1; }
    }
    return 0;
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
    const voxels_map *map = &c->map;
    const int nx = static_cast<int>(SDL_roundf(*x));
    const int ny = static_cast<int>(SDL_roundf(*y));
    const int nz = static_cast<int>(SDL_roundf(*z));
    const float px = *x - nx, py = *y - ny, pz = *z - nz;
    for (int dy = 0; dy < height; dy++)
    {
        constexpr float pad = 0.25f;
        if (px < -pad && item::is_obstacle(map->get(nx - 1, ny - dy, nz))) *x = nx - pad;
        if (px >  pad && item::is_obstacle(map->get(nx + 1, ny - dy, nz))) *x = nx + pad;
        if (py < -pad && item::is_obstacle(map->get(nx, ny - dy - 1, nz))) { *y = ny - pad; result = 1; }
        if (py >  pad && item::is_obstacle(map->get(nx, ny - dy + 1, nz))) { *y = ny + pad; result = 1; }
        if (pz < -pad && item::is_obstacle(map->get(nx, ny - dy, nz - 1))) *z = nz - pad;
        if (pz >  pad && item::is_obstacle(map->get(nx, ny - dy, nz + 1))) *z = nz + pad;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Lighting helpers
// ---------------------------------------------------------------------------

bool world::has_lights(const chunk *c) const noexcept
{
    static constexpr std::array<std::array<int, 2>, 9> offsets = {{{-1, -1}, {-1, 0}, {-1, 1},
                                                                    {0, -1},  {0, 0},  {0, 1},
                                                                    {1, -1},  {1, 0},  {1, 1}}};
    return std::ranges::any_of(offsets, [&](const auto &off)
    {
        const int dp = off[0], dq = off[1];
        const chunk *other = (dp || dq)
            ? find_chunk(c->p + dp, c->q + dq).value_or(nullptr)
            : c;
        return other && other->lights.size() != 0;
    });
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
        worker->mtx.lock();
        if (worker->state == WorkerState::IDLE)
        {
            const auto &background_layer = scene_graph_layers[static_cast<std::size_t>(Layer::BACKGROUND)];
            for (std::size_t i = 1; i < next_chunk_slot_in_view && i < background_layer.size(); ++i)
            {
                chunk *c = background_layer[i];
                if (c == nullptr || !c->dirty)
                    continue;
                int index = (SDL_abs(c->p) ^ SDL_abs(c->q)) % chunk_workers.size();
                if (index != worker->index)
                    continue;

                worker_item *item = &worker->item;
                item->p = c->p; item->q = c->q; item->load = 0;

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
                            item->block_maps[dp + 1][dq + 1] = new voxels_map{other->map};
                            item->light_maps[dp + 1][dq + 1] = new voxels_map{other->lights};
                        }
                        else
                        {
                            item->block_maps[dp + 1][dq + 1] = nullptr;
                            item->light_maps[dp + 1][dq + 1] = nullptr;
                        }
                    }
                }
                worker->state = WorkerState::BUSY;
                worker->cnd.notify_one();
                break;
            }
        }
        worker->mtx.unlock();
    }
}

// ---------------------------------------------------------------------------
// Occlusion / ambient occlusion
// ---------------------------------------------------------------------------

void world::occlusion(char neighbors[27], char lights[27], float shades[27],
                      float ao[6][4], float light[6][4]) noexcept
{
    static constexpr int lookup3[6][4][3] = {
        {{0,1,3},{2,1,5},{6,3,7},{8,5,7}},
        {{18,19,21},{20,19,23},{24,21,25},{26,23,25}},
        {{6,7,15},{8,7,17},{24,15,25},{26,17,25}},
        {{0,1,9},{2,1,11},{18,9,19},{20,11,19}},
        {{0,3,9},{6,3,15},{18,9,21},{24,15,21}},
        {{2,5,11},{8,5,17},{20,11,23},{26,17,23}}};
    static constexpr int lookup4[6][4][4] = {
        {{0,1,3,4},{1,2,4,5},{3,4,6,7},{4,5,7,8}},
        {{18,19,21,22},{19,20,22,23},{21,22,24,25},{22,23,25,26}},
        {{6,7,15,16},{7,8,16,17},{15,16,24,25},{16,17,25,26}},
        {{0,1,9,10},{1,2,10,11},{9,10,18,19},{10,11,19,20}},
        {{0,3,9,12},{3,6,12,15},{9,12,18,21},{12,15,21,24}},
        {{2,5,11,14},{5,8,14,17},{11,14,20,23},{14,17,23,26}}};
    static constexpr float curve[4] = {0.0, 0.25, 0.5, 0.75};

    for (int i = 0; i < 6; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            const int corner = neighbors[lookup3[i][j][0]];
            const int side1  = neighbors[lookup3[i][j][1]];
            const int side2  = neighbors[lookup3[i][j][2]];
            const int value  = side1 && side2 ? 3 : corner + side1 + side2;
            float shade_sum = 0, light_sum = 0;
            const int is_light = lights[13] == 15;
            for (int k = 0; k < 4; k++)
            {
                shade_sum += shades[lookup4[i][j][k]];
                light_sum += lights[lookup4[i][j][k]];
            }
            if (is_light) light_sum = 15 * 4 * 10;
            const float total = curve[value] + shade_sum / 4.0f;
            ao[i][j]    = SDL_min(total, 1.0f);
            light[i][j] = light_sum / 15.0f / 4.0f;
        }
    }
}

void world::light_fill(char *opaque, char *light, const int x, const int y, const int z,
                       const int w, const int force) noexcept
{
    struct entry { int x, y, z, w; };
    std::vector<entry> stk;
    stk.reserve(512);

    if (x + w < XZ_LO || z + w < XZ_LO) return;
    if (x - w > XZ_HI || z - w > XZ_HI) return;
    if (y < 0 || y >= Y_SIZE) return;
    if (light[XYZ(x, y, z)] >= w) return;
    if (!force && opaque[XYZ(x, y, z)]) return;
    light[XYZ(x, y, z)] = static_cast<char>(w);
    if (w - 1 > 0)
    {
        stk.push_back({x - 1, y, z, w - 1}); stk.push_back({x + 1, y, z, w - 1});
        stk.push_back({x, y - 1, z, w - 1}); stk.push_back({x, y + 1, z, w - 1});
        stk.push_back({x, y, z - 1, w - 1}); stk.push_back({x, y, z + 1, w - 1});
    }

    while (!stk.empty())
    {
        const auto [cx, cy, cz, cw] = stk.back();
        stk.pop_back();
        if (cx + cw < XZ_LO || cz + cw < XZ_LO) continue;
        if (cx - cw > XZ_HI || cz - cw > XZ_HI) continue;
        if (cy < 0 || cy >= Y_SIZE) continue;
        if (light[XYZ(cx, cy, cz)] >= cw) continue;
        if (opaque[XYZ(cx, cy, cz)]) continue;
        light[XYZ(cx, cy, cz)] = static_cast<char>(cw);
        if (cw - 1 <= 0) continue;
        stk.push_back({cx - 1, cy, cz, cw - 1}); stk.push_back({cx + 1, cy, cz, cw - 1});
        stk.push_back({cx, cy - 1, cz, cw - 1}); stk.push_back({cx, cy + 1, cz, cw - 1});
        stk.push_back({cx, cy, cz - 1, cw - 1}); stk.push_back({cx, cy, cz + 1, cw - 1});
    }
}

// ---------------------------------------------------------------------------
// Terrain generation (runs in worker thread)
// ---------------------------------------------------------------------------

void world::compute_chunk(worker_item *item) noexcept
{
    auto *opaque  = static_cast<char *>(SDL_calloc(XZ_SIZE * XZ_SIZE * Y_SIZE, sizeof(char)));
    auto *light   = static_cast<char *>(SDL_calloc(XZ_SIZE * XZ_SIZE * Y_SIZE, sizeof(char)));
    auto *highest = static_cast<int  *>(SDL_calloc(XZ_SIZE * XZ_SIZE, sizeof(int)));

    if (!opaque || !light || !highest)
    {
        SDL_free(opaque); SDL_free(light); SDL_free(highest);
        item->data = nullptr; item->faces = 0;
        return;
    }

    int ox = item->p * BUILD_CHUNK_SIZE - BUILD_CHUNK_SIZE - 1;
    int oy = -1;
    int oz = item->q * BUILD_CHUNK_SIZE - BUILD_CHUNK_SIZE - 1;

    int has_light = 0;
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++)
            if (voxels_map *map = item->light_maps[a][b]; map && map->size())
                has_light = 1;

    // Populate opaque array
    for (int a = 0; a < 3; a++)
    {
        for (int b = 0; b < 3; b++)
        {
            voxels_map *block_map = item->block_maps[a][b];
            if (!block_map) continue;
            for (const auto [ex, ey, ez, ew] : *block_map)
            {
                int x = ex - ox, y = ey - oy, z = ez - oz, w = ew;
                if (x < 0 || y < 0 || z < 0) continue;
                if (x >= XZ_SIZE || y >= Y_SIZE || z >= XZ_SIZE) continue;
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
                voxels_map *map = item->light_maps[a][b];
                if (!map) continue;
                for (const auto [ex, ey, ez, ew] : *map)
                    light_fill(opaque, light, ex - ox, ey - oy, ez - oz, ew, 1);
            }
        }
    }

    voxels_map *block_map = item->block_maps[1][1];

    // Count exposed faces
    int miny = 256, maxy = 0, faces = 0;
    for (const auto [ex, ey, ez, ew] : *block_map)
    {
        if (ew <= 0) continue;
        int x = ex - ox, y = ey - oy, z = ez - oz;
        int f1 = !opaque[XYZ(x-1,y,z)], f2 = !opaque[XYZ(x+1,y,z)];
        int f3 = !opaque[XYZ(x,y+1,z)], f4 = !opaque[XYZ(x,y-1,z)] && (ey > 0);
        int f5 = !opaque[XYZ(x,y,z-1)], f6 = !opaque[XYZ(x,y,z+1)];
        int total = f1 + f2 + f3 + f4 + f5 + f6;
        if (total == 0) continue;
        if (item::is_plant(ew)) total = 4;
        miny = SDL_min(miny, ey); maxy = SDL_max(maxy, ey);
        faces += total;
    }

    // Generate geometry
    static constexpr int components = 10;
    GLfloat *data = sdl_gl_helper::malloc_faces(components, faces);
    int offset = 0;
    for (const auto [ex, ey, ez, ew] : *block_map)
    {
        if (ew <= 0) continue;
        int x = ex - ox, y = ey - oy, z = ez - oz;
        int f1 = !opaque[XYZ(x-1,y,z)], f2 = !opaque[XYZ(x+1,y,z)];
        int f3 = !opaque[XYZ(x,y+1,z)], f4 = !opaque[XYZ(x,y-1,z)] && (ey > 0);
        int f5 = !opaque[XYZ(x,y,z-1)], f6 = !opaque[XYZ(x,y,z+1)];
        int total = f1 + f2 + f3 + f4 + f5 + f6;
        if (total == 0) continue;

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
                    neighbors[index] = opaque[XYZ(x+dx, y+dy, z+dz)];
                    lights[index]    = light[XYZ(x+dx, y+dy, z+dz)];
                    shades[index]    = 0;
                    if (int hi = XZ(x+dx, z+dz);
                        hi >= 0 && hi < XZ_SIZE * XZ_SIZE && y + dy <= highest[hi])
                    {
                        for (int oy1 = 0; oy1 < 8; oy1++)
                        {
                            if (y + dy + oy1 >= Y_SIZE) break;
                            if (opaque[XYZ(x+dx, y+dy+oy1, z+dz)])
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
                    min_ao   = SDL_min(min_ao,   ao[a][b]);
                    max_light = SDL_max(max_light, light2[a][b]);
                }
            float rotation = SDL_sinf(static_cast<float>(ex) * 12.9898f
                                    + static_cast<float>(ez) * 78.233f) * 43758.5453f;
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

    SDL_free(opaque); SDL_free(light); SDL_free(highest);
    item->miny = miny; item->maxy = maxy; item->faces = faces; item->data = data;
}

// ---------------------------------------------------------------------------
// Buffer / chunk generation (main thread or from worker result)
// ---------------------------------------------------------------------------

void world::generate_chunk(chunk *c, const worker_item *item) noexcept
{
    c->miny = item->miny; c->maxy = item->maxy; c->faces = item->faces;
    sdl_gl_helper::del_buffer(c->buffer);
    c->buffer = sdl_gl_helper::gen_faces(10, item->faces, item->data);
    SDL_free(item->data);
    const_cast<worker_item *>(item)->data = nullptr;
    sdl_gl_helper::gen_sign_buffer(c);
}

void world::gen_chunk_buffer(chunk *c) const noexcept
{
    worker_item _item, *item = &_item;
    item->p = c->p; item->q = c->q;
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
                item->block_maps[dp + 1][dq + 1] = &other->map;
                item->light_maps[dp + 1][dq + 1] = &other->lights;
            }
            else
            {
                item->block_maps[dp + 1][dq + 1] = 0;
                item->light_maps[dp + 1][dq + 1] = 0;
            }
        }
    }
    this->compute_chunk(item);
    this->generate_chunk(c, item);
    c->dirty = 0;
}

void world::load_chunk(const worker_item *item) const noexcept
{
    const int p = item->p, q = item->q;
    voxels_map *block_map = item->block_maps[1][1];
    voxels_map *light_map = item->light_maps[1][1];

    const bool enable_heightmap = active_player ? active_player->_configs.show_heightmap() : true;
    const float player_x = active_player ? active_player->pos.x : 0.0f;
    const float player_z = active_player ? active_player->pos.z : 0.0f;
    constexpr float flatten_radius = 256.0f;

    geometries::create_voxel_world(
        [](voxels_map *m, int x, int y, int z, int w) { m->set(x, y, z, w); },
        block_map, p, q, BUILD_CHUNK_SIZE,
        enable_heightmap, player_x, player_z, flatten_radius);

    db_load_blocks(block_map, p, q);
    db_load_lights(light_map, p, q);
}

void world::init_chunk(chunk *c, int p, int q) noexcept
{
    c->p = p; c->q = q;
    c->faces = 0; c->sign_faces = 0;
    c->buffer = 0; c->sign_buffer = 0;
    c->set_category(Entity::CHUNK);
    dirty_chunk(c);
    auto *signs = &c->signs;
    sign_list_alloc(signs, 16);
    db_load_signs(signs, p, q);
    voxels_map *block_map = &c->map;
    voxels_map *light_map = &c->lights;
    const int dx = p * BUILD_CHUNK_SIZE - 1;
    constexpr int dy = 0;
    const int dz = q * BUILD_CHUNK_SIZE - 1;
    block_map->init(dx, dy, dz, 0x7fff);
    light_map->init(dx, dy, dz, 0xf);
    this->attach_chunk_to_layer(c, static_cast<int>(Layer::BACKGROUND));
}

void world::create_chunk(chunk *c, const int p, const int q) noexcept
{
    init_chunk(c, p, q);
    worker_item _item, *item = &_item;
    item->p = c->p; item->q = c->q;
    item->block_maps[1][1] = &c->map;
    item->light_maps[1][1] = &c->lights;
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
        if (c == nullptr) continue;
        if (chunk_distance(c, p, q) < DELETE_CHUNK_RADIUS) break;

        detach_chunk_from_layer(c);
        sign_list_free(&c->signs);
        sdl_gl_helper::del_buffer(c->buffer);
        sdl_gl_helper::del_buffer(c->sign_buffer);
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
        if (c == nullptr) continue;
        detach_chunk_from_layer(c);
        sign_list_free(&c->signs);
        sdl_gl_helper::del_buffer(c->buffer);
        sdl_gl_helper::del_buffer(c->sign_buffer);
        delete c;
        background_layer[i] = nullptr;
    }
    this->next_chunk_slot_in_view = 1;
    if (background_layer[0] != nullptr && background_layer[0]->get_category() == Entity::SCENE)
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
                    c->map    = *item->block_maps[1][1];
                    c->lights = *item->light_maps[1][1];
                }
                generate_chunk(c, item);
            }
            else
            {
                SDL_free(item->data);
                item->data = nullptr;
            }
            for (int a = 0; a < 3; a++)
                for (int b = 0; b < 3; b++)
                {
                    delete item->block_maps[a][b]; item->block_maps[a][b] = nullptr;
                    delete item->light_maps[a][b]; item->light_maps[a][b] = nullptr;
                }
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
                    gen_chunk_buffer(c);
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

void world::ensure_chunks_worker(player *_player, worker *w) noexcept
{
    auto [width, height] = simple_direct_medialayer->get_window_size();
    player::position *s = &_player->pos;
    float matrix[16];
    matrix::set_3d(matrix, width, height,
                   s->x, s->y, s->z, s->rx, s->ry,
                   active_player->_configs.fov(),
                   active_player->_configs.ortho_scaling(),
                   RENDER_CHUNK_RADIUS);
    float planes[6][4];
    matrix::frustum_planes(planes, RENDER_CHUNK_RADIUS, matrix);
    const int p = chunked(s->x), q = chunked(s->z);

    int start = 0x0fffffff, best_score = start, best_a = 0, best_b = 0;
    for (int dp = -CREATE_CHUNK_RADIUS; dp <= CREATE_CHUNK_RADIUS; dp++)
    {
        for (int dq = -CREATE_CHUNK_RADIUS; dq <= CREATE_CHUNK_RADIUS; dq++)
        {
            int a = p + dp, b = q + dq;
            int index = (SDL_abs(a) ^ SDL_abs(b)) % CHUNK_WORKERS_TOTAL;
            if (index != w->index) continue;
            auto chunk_opt = find_chunk(a, b);
            if (chunk_opt.has_value() && !chunk_opt.value()->dirty) continue;
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
                best_score = score; best_a = a; best_b = b;
            }
        }
    }
    if (best_score == start) return;

    int a = best_a, b = best_b, load = 0;
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
        else return;
    }
    else c = chunk_opt.value();

    worker_item *item = &w->item;
    item->p = c->p; item->q = c->q; item->load = load;
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
                item->block_maps[dp + 1][dq + 1] = new voxels_map{other->map};
                item->light_maps[dp + 1][dq + 1] = new voxels_map{other->lights};
            }
            else
            {
                item->block_maps[dp + 1][dq + 1] = 0;
                item->light_maps[dp + 1][dq + 1] = 0;
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
    for (auto &&w : chunk_workers)
    {
        w->mtx.lock();
        if (w->state == WorkerState::IDLE)
            ensure_chunks_worker(_player, w.get());
        w->mtx.unlock();
    }
}
