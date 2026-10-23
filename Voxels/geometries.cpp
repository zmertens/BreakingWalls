#include "geometries.h"

#include <array>
#include <mutex>

#include <SDL3/SDL.h>

#include <noise/noise.h>

#include "item.h"
#include "heightmap.h"
#include "matrix.h"

void geometries::make_cube_faces(
    float *data, float ao[6][4], float light[6][4],
    int left, int right, int top, int bottom, int front, int back,
    int wleft, int wright, int wtop, int wbottom, int wfront, int wback,
    float x, float y, float z, float n) noexcept
{
    static const float positions[6][4][3] = {
        {{-1, -1, -1}, {-1, -1, +1}, {-1, +1, -1}, {-1, +1, +1}},
        {{+1, -1, -1}, {+1, -1, +1}, {+1, +1, -1}, {+1, +1, +1}},
        {{-1, +1, -1}, {-1, +1, +1}, {+1, +1, -1}, {+1, +1, +1}},
        {{-1, -1, -1}, {-1, -1, +1}, {+1, -1, -1}, {+1, -1, +1}},
        {{-1, -1, -1}, {-1, +1, -1}, {+1, -1, -1}, {+1, +1, -1}},
        {{-1, -1, +1}, {-1, +1, +1}, {+1, -1, +1}, {+1, +1, +1}}};
    static const float normals[6][3] = {
        {-1, 0, 0},
        {+1, 0, 0},
        {0, +1, 0},
        {0, -1, 0},
        {0, 0, -1},
        {0, 0, +1}};
    static const float uvs[6][4][2] = {
        {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
        {{1, 0}, {0, 0}, {1, 1}, {0, 1}},
        {{0, 1}, {0, 0}, {1, 1}, {1, 0}},
        {{0, 0}, {0, 1}, {1, 0}, {1, 1}},
        {{0, 0}, {0, 1}, {1, 0}, {1, 1}},
        {{1, 0}, {1, 1}, {0, 0}, {0, 1}}};
    static const float indices[6][6] = {
        {0, 3, 2, 0, 1, 3},
        {0, 3, 1, 0, 2, 3},
        {0, 3, 2, 0, 1, 3},
        {0, 3, 1, 0, 2, 3},
        {0, 3, 2, 0, 1, 3},
        {0, 3, 1, 0, 2, 3}};
    static const float flipped[6][6] = {
        {0, 1, 2, 1, 3, 2},
        {0, 2, 1, 2, 3, 1},
        {0, 1, 2, 1, 3, 2},
        {0, 2, 1, 2, 3, 1},
        {0, 1, 2, 1, 3, 2},
        {0, 2, 1, 2, 3, 1}};
    float *d = data;
    float s = 0.0625;
    float a = 0 + 1 / 2048.0;
    float b = s - 1 / 2048.0;
    int faces[6] = {left, right, top, bottom, front, back};
    int tiles[6] = {wleft, wright, wtop, wbottom, wfront, wback};
    for (int i = 0; i < 6; i++)
    {
        if (faces[i] == 0)
        {
            continue;
        }
        float du = (tiles[i] % 16) * s;
        float dv = (tiles[i] / 16) * s;
        int flip = ao[i][0] + ao[i][3] > ao[i][1] + ao[i][2];
        for (int v = 0; v < 6; v++)
        {
            int j = flip ? flipped[i][v] : indices[i][v];
            *(d++) = x + n * positions[i][j][0];
            *(d++) = y + n * positions[i][j][1];
            *(d++) = z + n * positions[i][j][2];
            *(d++) = normals[i][0];
            *(d++) = normals[i][1];
            *(d++) = normals[i][2];
            *(d++) = du + (uvs[i][j][0] ? b : a);
            *(d++) = dv + (uvs[i][j][1] ? b : a);
            *(d++) = ao[i][j];
            *(d++) = light[i][j];
        }
    }
}

void geometries::make_cube(
    float *data, float ao[6][4], float light[6][4],
    int left, int right, int top, int bottom, int front, int back,
    float x, float y, float z, float n, int w) noexcept
{
    int wleft = item::BLOCKS[w][0];
    int wright = item::BLOCKS[w][1];
    int wtop = item::BLOCKS[w][2];
    int wbottom = item::BLOCKS[w][3];
    int wfront = item::BLOCKS[w][4];
    int wback = item::BLOCKS[w][5];
    make_cube_faces(
        data, ao, light,
        left, right, top, bottom, front, back,
        wleft, wright, wtop, wbottom, wfront, wback,
        x, y, z, n);
}

void geometries::make_plant(
    float *data, float ao, float light,
    float px, float py, float pz, float n, int w, float rotation) noexcept
{
    static const float positions[4][4][3] = {
        {{0, -1, -1}, {0, -1, +1}, {0, +1, -1}, {0, +1, +1}},
        {{0, -1, -1}, {0, -1, +1}, {0, +1, -1}, {0, +1, +1}},
        {{-1, -1, 0}, {-1, +1, 0}, {+1, -1, 0}, {+1, +1, 0}},
        {{-1, -1, 0}, {-1, +1, 0}, {+1, -1, 0}, {+1, +1, 0}}};
    static const float normals[4][3] = {
        {-1, 0, 0},
        {+1, 0, 0},
        {0, 0, -1},
        {0, 0, +1}};
    static const float uvs[4][4][2] = {
        {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
        {{1, 0}, {0, 0}, {1, 1}, {0, 1}},
        {{0, 0}, {0, 1}, {1, 0}, {1, 1}},
        {{1, 0}, {1, 1}, {0, 0}, {0, 1}}};
    static const float indices[4][6] = {
        {0, 3, 2, 0, 1, 3},
        {0, 3, 1, 0, 2, 3},
        {0, 3, 2, 0, 1, 3},
        {0, 3, 1, 0, 2, 3}};
    float *d = data;
    float s = 0.0625;
    float a = 0 + 1 / 2048.0;
    float b = s - 1 / 2048.0;
    float du = (item::PLANTS[w] % 16) * s;
    float dv = (item::PLANTS[w] / 16) * s;
    for (int i = 0; i < 4; i++)
    {
        for (int v = 0; v < 6; v++)
        {
            int j = indices[i][v];
            *(d++) = n * positions[i][j][0];
            *(d++) = n * positions[i][j][1];
            *(d++) = n * positions[i][j][2];
            *(d++) = normals[i][0];
            *(d++) = normals[i][1];
            *(d++) = normals[i][2];
            *(d++) = du + (uvs[i][j][0] ? b : a);
            *(d++) = dv + (uvs[i][j][1] ? b : a);
            *(d++) = ao;
            *(d++) = light;
        }
    }
    float ma[16];
    float mb[16];
    matrix::identity(ma);
    matrix::rotate(mb, 0, 1, 0, matrix::to_radians(rotation));
    matrix::multiply(ma, mb, ma);
    matrix::apply(data, ma, 24, 3, 10);
    matrix::translate(mb, px, py, pz);
    matrix::multiply(ma, mb, ma);
    matrix::apply(data, ma, 24, 0, 10);
}

void geometries::make_player(
    float *data,
    const float x, const float y, const float z, const float rx, const float ry,
    const float sx, const float sy, const float sz,
    const float lean_x, const float lean_z) noexcept
{
    float ao[6][4]{};
    float light[6][4] = {
        {0.8, 0.8, 0.8, 0.8},
        {0.8, 0.8, 0.8, 0.8},
        {0.8, 0.8, 0.8, 0.8},
        {0.8, 0.8, 0.8, 0.8},
        {0.8, 0.8, 0.8, 0.8},
        {0.8, 0.8, 0.8, 0.8}};
    make_cube_faces(
        data, ao, light,
        1, 1, 1, 1, 1, 1,
        226, 224, 241, 209, 225, 227,
        0, 0, 0, 0.4);
    for (int i = 0; i < 36; ++i)
    {
        data[i * 10 + 0] *= sx;
        data[i * 10 + 1] *= sy;
        data[i * 10 + 2] *= sz;
    }
    float ma[16];
    float mb[16];
    matrix::identity(ma);
    matrix::rotate(mb, 0, 1, 0, rx);
    matrix::multiply(ma, mb, ma);
    matrix::rotate(mb, SDL_cosf(rx), 0, SDL_sinf(rx), -ry);
    matrix::multiply(ma, mb, ma);
    matrix::apply(data, ma, 36, 3, 10);
    matrix::translate(mb, x, y, z);
    matrix::multiply(ma, mb, ma);
    matrix::apply(data, ma, 36, 0, 10);
    // Shear in world space so the top of the body leans while the base stays put.
    for (int i = 0; i < 36; ++i)
    {
        const float h = data[i * 10 + 1] - y;
        data[i * 10 + 0] += lean_x * h;
        data[i * 10 + 2] += lean_z * h;
    }
}

void geometries::make_cube_wireframe(float *data, const float x, const float y, const float z, const float n) noexcept
{
    static const float positions[8][3] = {
        {-1, -1, -1},
        {-1, -1, +1},
        {-1, +1, -1},
        {-1, +1, +1},
        {+1, -1, -1},
        {+1, -1, +1},
        {+1, +1, -1},
        {+1, +1, +1}};
    static const int indices[24] = {
        0, 1, 0, 2, 0, 4, 1, 3,
        1, 5, 2, 3, 2, 6, 3, 7,
        4, 5, 4, 6, 5, 7, 6, 7};
    float *d = data;
    for (int i = 0; i < 24; i++)
    {
        int j = indices[i];
        *(d++) = x + n * positions[j][0];
        *(d++) = y + n * positions[j][1];
        *(d++) = z + n * positions[j][2];
    }
}

void geometries::make_character(
    float *data,
    const float x, const float y, const float n, const float m, const char c) noexcept
{
    float *d = data;
    constexpr float s = 0.0625;
    constexpr float a = s;
    constexpr float b = s * 2;
    const int w = c - 32;
    const float du = (w % 16) * a;
    const float dv = 1 - (w / 16) * b - b;
    *(d++) = x - n;
    *(d++) = y - m;
    *(d++) = du + 0;
    *(d++) = dv;
    *(d++) = x + n;
    *(d++) = y - m;
    *(d++) = du + a;
    *(d++) = dv;
    *(d++) = x + n;
    *(d++) = y + m;
    *(d++) = du + a;
    *(d++) = dv + b;
    *(d++) = x - n;
    *(d++) = y - m;
    *(d++) = du + 0;
    *(d++) = dv;
    *(d++) = x + n;
    *(d++) = y + m;
    *(d++) = du + a;
    *(d++) = dv + b;
    *(d++) = x - n;
    *(d++) = y + m;
    *(d++) = du + 0;
    *(d++) = dv + b;
}

void geometries::make_character_3d(
    float *data, float x, float y, float z, float n, int face, char c) noexcept
{
    static const float positions[8][6][3] = {
        {{0, -2, -1}, {0, +2, +1}, {0, +2, -1}, {0, -2, -1}, {0, -2, +1}, {0, +2, +1}},
        {{0, -2, -1}, {0, +2, +1}, {0, -2, +1}, {0, -2, -1}, {0, +2, -1}, {0, +2, +1}},
        {{-1, -2, 0}, {+1, +2, 0}, {+1, -2, 0}, {-1, -2, 0}, {-1, +2, 0}, {+1, +2, 0}},
        {{-1, -2, 0}, {+1, -2, 0}, {+1, +2, 0}, {-1, -2, 0}, {+1, +2, 0}, {-1, +2, 0}},
        {{-1, 0, +2}, {+1, 0, +2}, {+1, 0, -2}, {-1, 0, +2}, {+1, 0, -2}, {-1, 0, -2}},
        {{-2, 0, +1}, {+2, 0, -1}, {-2, 0, -1}, {-2, 0, +1}, {+2, 0, +1}, {+2, 0, -1}},
        {{+1, 0, +2}, {-1, 0, -2}, {-1, 0, +2}, {+1, 0, +2}, {+1, 0, -2}, {-1, 0, -2}},
        {{+2, 0, -1}, {-2, 0, +1}, {+2, 0, +1}, {+2, 0, -1}, {-2, 0, -1}, {-2, 0, +1}}};
    static const float uvs[8][6][2] = {
        {{0, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 0}, {1, 1}},
        {{1, 0}, {0, 1}, {0, 0}, {1, 0}, {1, 1}, {0, 1}},
        {{1, 0}, {0, 1}, {0, 0}, {1, 0}, {1, 1}, {0, 1}},
        {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}},
        {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}},
        {{0, 1}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 0}},
        {{0, 1}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 0}},
        {{0, 1}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 0}}};
    static const float offsets[8][3] = {
        {-1, 0, 0},
        {+1, 0, 0},
        {0, 0, -1},
        {0, 0, +1},
        {0, +1, 0},
        {0, +1, 0},
        {0, +1, 0},
        {0, +1, 0},
    };
    float *d = data;
    float s = 0.0625;
    float pu = s / 5;
    float pv = s / 2.5;
    float u1 = pu;
    float v1 = pv;
    float u2 = s - pu;
    float v2 = s * 2 - pv;
    float p = 0.5;
    int w = c - 32;
    float du = (w % 16) * s;
    float dv = 1 - (w / 16 + 1) * s * 2;
    x += p * offsets[face][0];
    y += p * offsets[face][1];
    z += p * offsets[face][2];
    for (int i = 0; i < 6; i++)
    {
        *(d++) = x + n * positions[face][i][0];
        *(d++) = y + n * positions[face][i][1];
        *(d++) = z + n * positions[face][i][2];
        *(d++) = du + (uvs[face][i][0] ? u2 : u1);
        *(d++) = dv + (uvs[face][i][1] ? v2 : v1);
    }
}

int geometries::_make_sphere(
    float *data, float r, int detail,
    float *a, float *b, float *c,
    float *ta, float *tb, float *tc) noexcept
{
    if (detail == 0)
    {
        float *d = data;
        *(d++) = a[0] * r;
        *(d++) = a[1] * r;
        *(d++) = a[2] * r;
        *(d++) = a[0];
        *(d++) = a[1];
        *(d++) = a[2];
        *(d++) = ta[0];
        *(d++) = ta[1];
        *(d++) = b[0] * r;
        *(d++) = b[1] * r;
        *(d++) = b[2] * r;
        *(d++) = b[0];
        *(d++) = b[1];
        *(d++) = b[2];
        *(d++) = tb[0];
        *(d++) = tb[1];
        *(d++) = c[0] * r;
        *(d++) = c[1] * r;
        *(d++) = c[2] * r;
        *(d++) = c[0];
        *(d++) = c[1];
        *(d++) = c[2];
        *(d++) = tc[0];
        *(d++) = tc[1];
        return 1;
    }
    else
    {
        float ab[3], ac[3], bc[3];
        for (int i = 0; i < 3; i++)
        {
            ab[i] = (a[i] + b[i]) / 2;
            ac[i] = (a[i] + c[i]) / 2;
            bc[i] = (b[i] + c[i]) / 2;
        }
        matrix::normalize(ab + 0, ab + 1, ab + 2);
        matrix::normalize(ac + 0, ac + 1, ac + 2);
        matrix::normalize(bc + 0, bc + 1, bc + 2);
        float tab[2], tac[2], tbc[2];
        tab[0] = 0.f;
        tab[1] = 1.f - SDL_acosf(ab[1]) / matrix::MAT_PI_VAL;
        tac[0] = 0.f;
        tac[1] = 1.f - SDL_acosf(ac[1]) / matrix::MAT_PI_VAL;
        tbc[0] = 0.f;
        tbc[1] = 1.f - SDL_acosf(bc[1]) / matrix::MAT_PI_VAL;
        int total = 0;
        int n;
        n = _make_sphere(data, r, detail - 1, a, ab, ac, ta, tab, tac);
        total += n;
        data += n * 24;
        n = _make_sphere(data, r, detail - 1, b, bc, ab, tb, tbc, tab);
        total += n;
        data += n * 24;
        n = _make_sphere(data, r, detail - 1, c, ac, bc, tc, tac, tbc);
        total += n;
        data += n * 24;
        n = _make_sphere(data, r, detail - 1, ab, bc, ac, tab, tbc, tac);
        total += n;
        data += n * 24;
        return total;
    }
}

void geometries::make_sphere(float *data, float r, int detail) noexcept
{
    // detail, triangles, floats
    // 0, 8, 192
    // 1, 32, 768
    // 2, 128, 3072
    // 3, 512, 12288
    // 4, 2048, 49152
    // 5, 8192, 196608
    // 6, 32768, 786432
    // 7, 131072, 3145728
    static int indices[8][3] = {
        {4, 3, 0}, {1, 4, 0}, {3, 4, 5}, {4, 1, 5}, {0, 3, 2}, {0, 2, 1}, {5, 2, 3}, {5, 1, 2}};
    static float positions[6][3] = {
        {0, 0, -1}, {1, 0, 0}, {0, -1, 0}, {-1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    static float uvs[6][3] = {
        {0, 0.5}, {0, 0.5}, {0, 0}, {0, 0.5}, {0, 1}, {0, 0.5}};
    int total = 0;
    for (int i = 0; i < 8; i++)
    {
        int n = _make_sphere(
            data, r, detail,
            positions[indices[i][0]],
            positions[indices[i][1]],
            positions[indices[i][2]],
            uvs[indices[i][0]],
            uvs[indices[i][1]],
            uvs[indices[i][2]]);
        total += n;
        data += n * 24;
    }
}

void geometries::create_voxel_world(const std::function<void(craft_map *, int, int, int, int)> &setter,
                                    craft_map *m, const int p, const int q, const int chunk_size,
                                    bool enable_heightmap,
                                    float player_x, float player_z,
                                    float flatten_radius,
                                    const heightmap *constrained,
                                    const bool include_decorations) noexcept
{
    if (constrained)
    {
        // Voxels exist only inside the heightmap; everything outside is empty for performance.
        for (int dx = -1; dx < chunk_size + 1; dx++)
        {
            for (int dz = -1; dz < chunk_size + 1; dz++)
            {
                const int flag = (dx < 0 || dz < 0 || dx >= chunk_size || dz >= chunk_size) ? -1 : 1;
                const int x = p * chunk_size + dx;
                const int z = q * chunk_size + dz;
                if (!constrained->contains(x, z))
                {
                    continue;
                }
                const int h = constrained->column_height(x, z);
                const int top = heightmap::material_for(constrained->elevation(x, z)).block;
                for (int y = 0; y < h; y++)
                {
                    setter(m, x, y, z, (y == h - 1 ? top : 3) * flag);
                }

                // Trees belong to the owning chunk and remain fully inside the constrained map.
                if (include_decorations && flag > 0 && top == static_cast<int>(item::BlockType::GRASS) &&
                    x >= 3 && z >= 3 && x + 3 < constrained->side && z + 3 < constrained->side &&
                    simplex2(static_cast<float>(x), static_cast<float>(z), 6, 0.5f, 2.0f) > 0.84f)
                {
                    for (int y = h + 3; y < h + 8; ++y)
                    {
                        for (int ox = -3; ox <= 3; ++ox)
                        {
                            for (int oz = -3; oz <= 3; ++oz)
                            {
                                const int d = ox * ox + oz * oz + (y - (h + 4)) * (y - (h + 4));
                                if (d < 11)
                                {
                                    setter(m, x + ox, y, z + oz, static_cast<int>(item::BlockType::LEAVES));
                                }
                            }
                        }
                    }
                    for (int y = h; y < h + 7; ++y)
                    {
                        setter(m, x, y, z, static_cast<int>(item::BlockType::WOOD));
                    }
                }

                // Keep constrained heightmaps bounded while preserving the voxel-cloud sky layer.
                for (int y = 64; include_decorations && y < 72; ++y)
                {
                    if (simplex3(static_cast<float>(x) * 0.01f,
                                 static_cast<float>(y) * 0.1f,
                                 static_cast<float>(z) * 0.01f,
                                 8, 0.5f, 2.0f) > 0.75f)
                    {
                        setter(m, x, y, z, static_cast<int>(item::BlockType::CLOUD) * flag);
                    }
                }
            }
        }
        return;
    }

    constexpr int pad = 1;
    for (int dx = -pad; dx < chunk_size + pad; dx++)
    {
        for (int dz = -pad; dz < chunk_size + pad; dz++)
        {
            int flag = 1;
            if (dx < 0 || dz < 0 || dx >= chunk_size || dz >= chunk_size)
            {
                flag = -1;
            }
            const int x = p * chunk_size + dx;
            const int z = q * chunk_size + dz;

            // Check if this position should be flattened
            const float dist_to_player = SDL_sqrtf(
                (static_cast<float>(x) - player_x) * (static_cast<float>(x) - player_x) +
                (static_cast<float>(z) - player_z) * (static_cast<float>(z) - player_z));
            const bool should_flatten = !enable_heightmap && dist_to_player <= flatten_radius;

            // Build the environment
            const float f = simplex2(static_cast<float>(x) * 0.01f, static_cast<float>(z) * 0.01f,
                                     4, 0.5f, 2.f);
            const float g = simplex2(static_cast<float>(-x) * 0.01f, static_cast<float>(-z) * 0.01f,
                                     2, 0.9f, 2.f);
            const int mh = g * 32 + 16;
            auto h = static_cast<int>(f * static_cast<float>(mh));
            int w = 1;
            if (constexpr int t = 12; h <= t)
            {
                h = t;
                w = 2;
            }

            // Apply flattening if within range and heightmap disabled
            if (should_flatten)
            {
                h = 12; // Flatten to water level
                w = 1;  // Grass terrain
            }

            // sand and grass terrain
            for (int y = 0; y < h; y++)
            {
                setter(m, x, y, z, w * flag);
            }

            if (w == 1 && !should_flatten)
            {
                // grass
                if (simplex2(static_cast<float>(-x) * 0.1f,
                             static_cast<float>(z) * 0.1f,
                             4,
                             0.8f, 2.0f) > 0.6f)
                {
                    setter(m, x, h, z, 17 * flag);
                }
                // flowers
                if (simplex2(static_cast<float>(x) * 0.05f,
                             static_cast<float>(-z) * 0.05f,
                             4,
                             0.8f,
                             2.0f) > 0.7f)
                {
                    const auto w1 = 18.f + simplex2(static_cast<float>(x) * 0.1f,
                                                    static_cast<float>(z) * 0.1f,
                                                    4,
                                                    0.8f,
                                                    2.0f) *
                                               7.f;

                    setter(m, x, h, z, static_cast<int>(w1 * static_cast<float>(flag)));
                }

                // trees
                int ok = 1;
                if (dx - 4 < 0 || dz - 4 < 0 ||
                    dx + 4 >= chunk_size || dz + 4 >= chunk_size)
                {
                    ok = 0;
                }
                if (ok && simplex2(static_cast<float>(x), static_cast<float>(z), 6, 0.5f, 2.0f) > 0.84f)
                {
                    for (int y = h + 3; y < h + 8; y++)
                    {
                        for (int ox = -3; ox <= 3; ox++)
                        {
                            for (int oz = -3; oz <= 3; oz++)
                            {
                                const int d = (ox * ox) + (oz * oz) +
                                              (y - (h + 4)) * (y - (h + 4));
                                if (d < 11)
                                {
                                    setter(m, x + ox, y, z + oz, 15);
                                }
                            }
                        }
                    }
                    for (int y = h; y < h + 7; y++)
                    {
                        setter(m, x, y, z, 5);
                    }
                }
            }
            // clouds
            for (int y = 64; y < 72; y++)
            {
                if (simplex3(
                        static_cast<float>(x) * 0.01f,
                        static_cast<float>(y) * 0.1f,
                        static_cast<float>(z) * 0.01f,
                        8,
                        0.5f,
                        2.0f) > 0.75f)
                {
                    setter(m, x, y, z, 16 * flag);
                }
            }
        }
    }
} // create_voxel_world
