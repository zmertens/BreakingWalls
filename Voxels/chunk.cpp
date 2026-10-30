#include "chunk.h"

#include "geometries.h"
#include "item.h"
#include "db_manager.h"
#include "player.h"
#include "sdl_gl_helper.h"
#include "worker_traits.h"
#include "world.h"

#include <SDL3/SDL.h>

#include <vector>

void chunk::generate(worker_item *item) noexcept
{
    miny = item->miny; maxy = item->maxy; faces = item->faces;
    sdl_gl_helper::del_buffer(buffer);
    buffer = sdl_gl_helper::gen_faces(10, item->faces, item->data);
    SDL_free(item->data);
    item->data = nullptr;
    sdl_gl_helper::gen_sign_buffer(this);
}

void chunk::initialize(const int chunk_p, const int chunk_q, db_manager *db) noexcept
{
    p = chunk_p; q = chunk_q;
    faces = 0; sign_faces = 0;
    buffer = 0; sign_buffer = 0;
    dirty = 1;
    db::sign_list_alloc(&signs, 16);
    if (db)
    {
        db->load_signs(&signs, p, q);
    }
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
