// World-owned chunk scene, lifecycle, neighbor coordination, and collision.
#include "world.h"
#include "chunk_worker.h"

#include "db.h"
#include "item.h"
#include "matrix.h"
#include "sdl_gl_helper.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <ranges>
#include <string>

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
                            item->block_maps[dp + 1][dq + 1] = new craft_map{other->map};
                            item->light_maps[dp + 1][dq + 1] = new craft_map{other->lights};
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
    chunk::compute(item);
    c->generate(item);
    c->dirty = 0;
}

void world::create_chunk(chunk *c, const int p, const int q) noexcept
{
    init_chunk(c, p, q);
    worker_item _item, *item = &_item;
    item->p = c->p;
    item->q = c->q;
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
        if (c == nullptr)
            continue;
        if (chunk_distance(c, p, q) < DELETE_CHUNK_RADIUS)
            break;

        detach_chunk_from_layer(c);
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
                    c->map = *item->block_maps[1][1];
                    c->lights = *item->light_maps[1][1];
                }
                c->generate(item);
            }
            else
            {
                SDL_free(item->data);
                item->data = nullptr;
            }
            for (int a = 0; a < 3; a++)
                for (int b = 0; b < 3; b++)
                {
                    delete item->block_maps[a][b];
                    item->block_maps[a][b] = nullptr;
                    delete item->light_maps[a][b];
                    item->light_maps[a][b] = nullptr;
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
                   active_player->get_configs().fov(),
                   active_player->get_configs().ortho_scaling(),
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
            if (index != w->index)
                continue;
            auto chunk_opt = find_chunk(a, b);
            if (chunk_opt.has_value() && !chunk_opt.value()->dirty)
                continue;
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
        return;

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
                item->block_maps[dp + 1][dq + 1] = new craft_map{other->map};
                item->light_maps[dp + 1][dq + 1] = new craft_map{other->lights};
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
    return chunk::coordinate(x);
}

std::optional<chunk *> world::find_chunk(const int p, const int q) const noexcept
{
    const auto &background_layer = scene_graph_layers[static_cast<std::size_t>(Layer::BACKGROUND)];
    for (std::size_t i = 1; i < next_chunk_slot_in_view && i < background_layer.size(); ++i)
    {
        chunk *c = background_layer[i];
        if (c != nullptr && c->p == p && c->q == q)
            return c;
    }
    return std::nullopt;
}

bool world::chunk_visible(float planes[6][4], const int p, const int q,
                          const int miny, const int maxy) const noexcept
{
    return chunk::visible(planes, p, q, miny, maxy, active_player->get_configs().ortho_scaling() != 0);
}

int world::chunk_distance(const chunk *c, const int p, const int q) noexcept
{
    return c->distance(p, q);
}

void world::load_chunk(const worker_item *item) const noexcept
{
    chunk::load(item, active_player ? active_player->get_configs().show_heightmap() : true,
                active_player ? active_player->pos.x : 0.0f,
                active_player ? active_player->pos.z : 0.0f);
}

void world::init_chunk(chunk *c, const int p, const int q) noexcept
{
    c->initialize(p, q);
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
