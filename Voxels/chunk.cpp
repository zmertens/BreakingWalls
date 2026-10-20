#include "chunk.h"

#include "geometries.h"
#include "item.h"
#include "sdl_gl_helper.h"
#include "world.h"

#include <SDL3/SDL.h>

#include <vector>

// Mesh scratch space includes the chunk and its eight neighbors.
#define XZ_SIZE  (world::BUILD_CHUNK_SIZE * 3 + 2)
#define XZ_LO    (world::BUILD_CHUNK_SIZE)
#define XZ_HI    (world::BUILD_CHUNK_SIZE * 2 + 1)
#define Y_SIZE   258
#define XYZ(x, y, z) ((y) * XZ_SIZE * XZ_SIZE + (x) * XZ_SIZE + (z))
#define XZ(x, z)     ((x) * XZ_SIZE + (z))

int chunk::coordinate(const float x) noexcept
{
    return static_cast<int>(SDL_floorf(SDL_roundf(x) / static_cast<float>(world::BUILD_CHUNK_SIZE)));
}

int chunk::distance(const int other_p, const int other_q) const noexcept
{
    const int dp = SDL_abs(p - other_p);
    const int dq = SDL_abs(q - other_q);
    return SDL_max(dp, dq);
}

bool chunk::visible(float planes[6][4], const int p, const int q,
                    const int miny, const int maxy, const bool orthographic) noexcept
{
    const auto miny_f = static_cast<float>(miny);
    const auto maxy_f = static_cast<float>(maxy);
    const float x = static_cast<float>(p * world::BUILD_CHUNK_SIZE - 1);
    const float z = static_cast<float>(q * world::BUILD_CHUNK_SIZE - 1);
    const float d = static_cast<float>(world::BUILD_CHUNK_SIZE + 1);
    const float points[8][3] = {
        {x + 0.f, miny_f, z + 0.f}, {x + d, miny_f, z + 0.f},
        {x + 0.f, miny_f, z + d},   {x + d, miny_f, z + d},
        {x + 0.f, maxy_f, z + 0.f}, {x + d, maxy_f, z + 0.f},
        {x + 0.f, maxy_f, z + d},   {x + d, maxy_f, z + d}};
    const int n = orthographic ? 4 : 6;
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
// Occlusion / ambient occlusion
// ---------------------------------------------------------------------------

void chunk::occlusion(char neighbors[27], char lights[27], float shades[27],
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

void chunk::light_fill(char *opaque, char *light, const int x, const int y, const int z,
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

void chunk::compute(worker_item *item) noexcept
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

    int ox = item->p * world::BUILD_CHUNK_SIZE - world::BUILD_CHUNK_SIZE - 1;
    int oy = -1;
    int oz = item->q * world::BUILD_CHUNK_SIZE - world::BUILD_CHUNK_SIZE - 1;

    int has_light = 0;
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++)
            if (craft_map *map = item->light_maps[a][b]; map && map->size())
                has_light = 1;

    // Populate opaque array
    for (int a = 0; a < 3; a++)
    {
        for (int b = 0; b < 3; b++)
        {
            craft_map *block_map = item->block_maps[a][b];
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
                craft_map *map = item->light_maps[a][b];
                if (!map) continue;
                for (const auto [ex, ey, ez, ew] : *map)
                    light_fill(opaque, light, ex - ox, ey - oy, ez - oz, ew, 1);
            }
        }
    }

    craft_map *block_map = item->block_maps[1][1];

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
    float *data = sdl_gl_helper::malloc_faces(components, faces);
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

void chunk::generate(worker_item *item) noexcept
{
    miny = item->miny; maxy = item->maxy; faces = item->faces;
    sdl_gl_helper::del_buffer(buffer);
    buffer = sdl_gl_helper::gen_faces(10, item->faces, item->data);
    SDL_free(item->data);
    item->data = nullptr;
    sdl_gl_helper::gen_sign_buffer(this);
}

void chunk::load(const worker_item *item, const bool enable_heightmap,
                 const float player_x, const float player_z, const heightmap *constrained,
                 const bool include_decorations, const bool load_persisted_blocks) noexcept
{
    const int p = item->p, q = item->q;
    craft_map *block_map = item->block_maps[1][1];
    craft_map *light_map = item->light_maps[1][1];

    constexpr float flatten_radius = 256.0f;

    geometries::create_voxel_world(
        [](craft_map *m, int x, int y, int z, int w) { m->set(x, y, z, w); },
        block_map, p, q, world::BUILD_CHUNK_SIZE,
        enable_heightmap, player_x, player_z, flatten_radius, constrained, include_decorations);

    if (load_persisted_blocks)
    {
        db::load_blocks(block_map, p, q);
        db::load_lights(light_map, p, q);
    }
}

void chunk::initialize(const int chunk_p, const int chunk_q) noexcept
{
    p = chunk_p; q = chunk_q;
    faces = 0; sign_faces = 0;
    buffer = 0; sign_buffer = 0;
    dirty = 1;
    db::sign_list_alloc(&signs, 16);
    db::load_signs(&signs, p, q);
    craft_map *block_map = &map;
    craft_map *light_map = &lights;
    const int dx = p * world::BUILD_CHUNK_SIZE - 1;
    constexpr int dy = 0;
    const int dz = q * world::BUILD_CHUNK_SIZE - 1;
    block_map->init(dx, dy, dz, 0x7fff);
    light_map->init(dx, dy, dz, 0xf);
}

void chunk::release_buffers() noexcept
{
    db::sign_list_free(&signs);
    sdl_gl_helper::del_buffer(buffer);
    sdl_gl_helper::del_buffer(sign_buffer);
    buffer = 0;
    sign_buffer = 0;
}
