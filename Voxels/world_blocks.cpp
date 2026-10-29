#include "world.h"

#include "chunk.h"
#include "db.h"
#include "db_manager.h"

// ---------------------------------------------------------------------------
// Block / sign / light manipulation
// ---------------------------------------------------------------------------

void world::unset_sign(const int x, const int y, const int z) const noexcept
{
    const int p = chunked(static_cast<float>(x));
    const int q = chunked(static_cast<float>(z));
    if (const auto chunk_opt = find_chunk(p, q); chunk_opt.has_value())
    {
        chunk *c = chunk_opt.value();
        if (auto *signs = &c->signs; db::sign_list_remove_all(signs, x, y, z))
        {
            c->dirty = 1;
            if (world_db)
                world_db->delete_signs(x, y, z);
        }
    }
    else
    {
        if (world_db)
            world_db->delete_signs(x, y, z);
    }
}

void world::unset_sign_face(const int x, const int y, const int z, const int face) const noexcept
{
    const int p = chunked(static_cast<float>(x));
    const int q = chunked(static_cast<float>(z));
    if (const auto chunk_opt = find_chunk(p, q); chunk_opt.has_value())
    {
        chunk *c = chunk_opt.value();
        if (auto *signs = &c->signs; db::sign_list_remove(signs, x, y, z, face))
        {
            c->dirty = 1;
            if (world_db)
                world_db->delete_sign(x, y, z, face);
        }
    }
    else
    {
        if (world_db)
            world_db->delete_sign(x, y, z, face);
    }
}

void world::_set_sign(const int p, const int q, const int x, const int y, const int z,
                      const int face, const std::string_view text, const int dirty) const noexcept
{
    if (text.empty())
    {
        unset_sign_face(x, y, z, face);
        return;
    }
    if (const auto chunk_opt = find_chunk(p, q); chunk_opt.has_value())
    {
        chunk *c = chunk_opt.value();
        auto *signs = &c->signs;
        db::sign_list_add(signs, x, y, z, face, text.data());
        if (dirty)
            c->dirty = 1;
    }
    if (world_db)
        world_db->insert_sign(p, q, x, y, z, face, text.data());
}

void world::set_sign(const int x, const int y, const int z, const int face, const std::string_view text) const noexcept
{
    const int p = chunked(static_cast<float>(x));
    const int q = chunked(static_cast<float>(z));
    _set_sign(p, q, x, y, z, face, text, 1);
}

void world::toggle_light(int x, int y, int z) const noexcept
{
    const int p = chunked(static_cast<float>(x));
    const int q = chunked(static_cast<float>(z));
    if (const auto chunk_opt = find_chunk(p, q); chunk_opt.has_value())
    {
        chunk *c = chunk_opt.value();
        craft_map *map = &c->lights;
        const int w = map->get(x, y, z) ? 0 : 15;
        map->set(x, y, z, w);
        if (world_db)
            world_db->insert_light(p, q, x, y, z, w);
        dirty_chunk(c);
    }
}

void world::set_light(int p, int q, int x, int y, int z, int w) const noexcept
{
    if (auto chunk_opt = find_chunk(p, q); chunk_opt.has_value())
    {
        chunk *c = chunk_opt.value();
        if (craft_map *map = &c->lights; map->set(x, y, z, w))
        {
            dirty_chunk(c);
            if (world_db)
                world_db->insert_light(p, q, x, y, z, w);
        }
    }
    else
    {
        if (world_db)
            world_db->insert_light(p, q, x, y, z, w);
    }
}

void world::_set_block(const int p, int q, int x, int y, int z, const int w, const int dirty) const noexcept
{
    if (const auto chunk_opt = find_chunk(p, q); chunk_opt.has_value())
    {
        chunk *c = chunk_opt.value();
        if (craft_map *map = &c->map; map->set(x, y, z, w))
        {
            if (dirty)
                dirty_chunk(c);
            if (world_db)
                world_db->insert_block(p, q, x, y, z, w);
        }
    }
    else
    {
        if (world_db)
            world_db->insert_block(p, q, x, y, z, w);
    }
    if (w == 0 && chunked(static_cast<float>(x)) == p && chunked(static_cast<float>(z)) == q)
    {
        unset_sign(x, y, z);
        set_light(p, q, x, y, z, 0);
    }
}

void world::set_block(const int x, const int y, const int z, const int w) const noexcept
{
    const int p = chunked(static_cast<float>(x));
    const int q = chunked(static_cast<float>(z));
    _set_block(p, q, x, y, z, w, 1);
    for (int dx = -1; dx <= 1; dx++)
    {
        for (int dz = -1; dz <= 1; dz++)
        {
            if (dx == 0 && dz == 0)
                continue;
            if (dx && chunked(static_cast<float>(x + dx)) == p)
                continue;
            if (dz && chunked(static_cast<float>(z + dz)) == q)
                continue;
            _set_block(p + dx, q + dz, x, y, z, -w, 1);
        }
    }
}

int world::get_block(const int x, const int y, const int z) const noexcept
{
    const int p = chunked(static_cast<float>(x));
    const int q = chunked(static_cast<float>(z));
    if (auto chunk_opt = find_chunk(p, q); chunk_opt.has_value())
    {
        const craft_map *map = &chunk_opt.value()->map;
        return map->get(x, y, z);
    }
    return 0;
}

