#include "world.h"

#include <MazeBuilder/randomizer.h>

#include "db.h"
#include "font.h"
#include "geometries.h"
#include "item.h"
#include "matrix.h"
#include "texture.h"
#include "resource_identifiers.h"
#include "resource_manager.h"
#include "sdl_gl_helper.h"
#include "shader.h"

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <glad/glad.h>
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <ranges>
#include <thread>

// World configs
#define CREATE_CHUNK_RADIUS 10
#define RENDER_CHUNK_RADIUS 20
#define BUILD_CHUNK_SIZE 32
#define RENDER_SIGN_RADIUS 4
#define DELETE_CHUNK_RADIUS 14
#define CHUNK_WORKERS_TOTAL 4

namespace
{
    constexpr float FORCE_DUE_TO_GRAVITY = -9.8f;
}

// Static GL attribute slots — shared across world.cpp, chunk_manager.cpp, world_renderer.cpp
sdl_gl_helper::attrib world::s_block_attrib;
sdl_gl_helper::attrib world::s_line_attrib;
sdl_gl_helper::attrib world::s_text_attrib;
sdl_gl_helper::attrib world::s_sky_attrib;

std::string gl_error_checker(const char *file, const int line) noexcept
{
    GLenum error_code;
    std::string error_str;
    while ((error_code = glGetError()) != GL_NO_ERROR)
    {
        switch (error_code)
        {
        case GL_INVALID_ENUM:       error_str += "INVALID_ENUM"; break;
        case GL_INVALID_VALUE:      error_str += "INVALID_VALUE"; break;
        case GL_INVALID_OPERATION:  error_str += "INVALID_OPERATION"; break;
        case GL_OUT_OF_MEMORY:      error_str += "OUT_OF_MEMORY"; break;
        case GL_INVALID_FRAMEBUFFER_OPERATION: error_str += "INVALID_FRAMEBUFFER_OPERATION"; break;
        default: break;
        }
        SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                     "OpenGL ERROR: %s\n\t\tFILE: %s, LINE: %d\n", error_str.c_str(), file, line);
    }
    return error_code == GL_NO_ERROR ? "" : error_str;
}

#define CHECK_GL_ERR() gl_error_checker(__FILE__, __LINE__)

// ---------------------------------------------------------------------------
// Private static geometry helpers
// ---------------------------------------------------------------------------

world::ivec3 world::face_normal(const int face) noexcept
{
    switch (face)
    {
    case 0: return {-1, 0, 0};
    case 1: return {1, 0, 0};
    case 2: return {0, 0, -1};
    case 3: return {0, 0, 1};
    case 8: return {0, -1, 0};
    default: return {0, 1, 0};
    }
}

world::ivec3 world::face_u_axis(const int face) noexcept
{
    switch (face)
    {
    case 0: return {0, 0, 1};
    case 1: return {0, 0, -1};
    case 2: return {-1, 0, 0};
    case 3: return {1, 0, 0};
    case 4: return {1, 0, 0};
    case 5: return {0, 0, 1};
    case 6: return {-1, 0, 0};
    case 7: return {0, 0, -1};
    case 8: return {1, 0, 0};
    default: return {1, 0, 0};
    }
}

world::ivec3 world::face_v_axis(const int face) noexcept
{
    switch (face)
    {
    case 0:
    case 1:
    case 2:
    case 3: return {0, -1, 0};
    case 4: return {0, 0, 1};
    case 5: return {-1, 0, 0};
    case 6: return {0, 0, -1};
    case 7: return {1, 0, 0};
    case 8: return {0, 0, 1};
    default: return {0, 0, 1};
    }
}

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

world::world(SDL_Window *window, font_manager &fonts,
             player *p,
             shader_manager &shaders,
             texture_manager &textures,
             const sdl_gl_helper *sdl)
    : simple_direct_medialayer{sdl}, active_fonts{fonts}, world_shaders{shaders}, world_textures{textures},
      scene_graph_layers{}, next_chunk_slot_in_view{1}, commands_during_world_events{},
      active_player{p}, world_sky_gl_buffer{0}, current_projected_plane{}
{
    current_projected_plane.projected_texture = &world_textures.get(TextureIdentifier::MAZE);

    if (active_player)
    {
        active_player->set_world(this);
    }
}

world::~world()
{
    destroy_world();
}

// ---------------------------------------------------------------------------
// Scene graph
// ---------------------------------------------------------------------------

void world::build_scene()
{
    auto &background_layer = scene_graph_layers[static_cast<std::size_t>(Layer::BACKGROUND)];

    chunk *root = new chunk{};
    root->set_category(Entity::SCENE);
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
    c->set_category(Entity::CHUNK);

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

        if ((static_cast<int>(node->get_category()) & static_cast<int>(Entity::CHUNK)) != 0)
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

        if ((static_cast<int>(node->get_category()) & static_cast<int>(Entity::CHUNK)) != 0)
            callback(node);

        for (chunk *child : node->children)
            traverse_node(child);
    };

    traverse_node(root);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void world::init() noexcept
{
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    build_scene();
    init_worker_threads();
    force_chunks(active_player);
    active_player->pos.y = static_cast<float>(highest_block(active_player->pos.x, active_player->pos.z) + 2);

    s_block_attrib.program = world_shaders.get(ShaderIdentifier::BLOCK_SHADER).get();
    s_block_attrib.position = 0;
    s_block_attrib.normal = 1;
    s_block_attrib.uv = 2;
    s_block_attrib.matrix   = glGetUniformLocation(s_block_attrib.program, "matrix");
    s_block_attrib.sampler  = glGetUniformLocation(s_block_attrib.program, "sampler");
    s_block_attrib.extra1   = glGetUniformLocation(s_block_attrib.program, "sky_sampler");
    s_block_attrib.extra2   = glGetUniformLocation(s_block_attrib.program, "daylight");
    s_block_attrib.extra3   = glGetUniformLocation(s_block_attrib.program, "fog_distance");
    s_block_attrib.extra4   = glGetUniformLocation(s_block_attrib.program, "is_ortho");
    s_block_attrib.camera   = glGetUniformLocation(s_block_attrib.program, "camera");
    s_block_attrib.timer    = glGetUniformLocation(s_block_attrib.program, "timer");

    s_line_attrib.program  = world_shaders.get(ShaderIdentifier::LINE_SHADER).get();
    s_line_attrib.position = 0;
    s_line_attrib.matrix   = glGetUniformLocation(s_line_attrib.program, "matrix");
    s_line_attrib.extra1   = glGetUniformLocation(s_line_attrib.program, "color");

    s_text_attrib.program  = world_shaders.get(ShaderIdentifier::TEXT_SHADER).get();
    s_text_attrib.position = 0;
    s_text_attrib.uv       = 1;
    s_text_attrib.matrix   = glGetUniformLocation(s_text_attrib.program, "matrix");
    s_text_attrib.sampler  = glGetUniformLocation(s_text_attrib.program, "sampler");
    s_text_attrib.extra1   = glGetUniformLocation(s_text_attrib.program, "is_sign");

    s_sky_attrib.program  = world_shaders.get(ShaderIdentifier::SKY_SHADER).get();
    s_sky_attrib.position = 0;
    s_sky_attrib.normal   = 1;
    s_sky_attrib.uv       = 2;
    s_sky_attrib.matrix   = glGetUniformLocation(s_sky_attrib.program, "matrix");
    s_sky_attrib.sampler  = glGetUniformLocation(s_sky_attrib.program, "sampler");
    s_sky_attrib.timer    = glGetUniformLocation(s_sky_attrib.program, "timer");

    world_sky_gl_buffer = sdl_gl_helper::gen_sky_buffer();

    auto [vw, vh] = simple_direct_medialayer->get_window_size_in_pixels();
    if (!m_bloom.init(
            vw, vh,
            world_shaders.get(ShaderIdentifier::BLOOM_BLUR_SHADER).get(),
            world_shaders.get(ShaderIdentifier::BLOOM_COMPOSITE_SHADER).get()))
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "world::init - bloom_pass init failed\n");
    }
}

void world::update(const float delta_time, mazes::randomizer &rng) noexcept
{
    while (!commands_during_world_events.empty())
    {
        auto [action, _] = commands_during_world_events.front();
        commands_during_world_events.pop();
        action(*active_player, delta_time, std::ref(rng));
    }

    process_build_queue();

    const float dt_seconds = delta_time / 1000.0f;

    if (!active_player->is_flying())
    {
        active_player->vel.vy += FORCE_DUE_TO_GRAVITY * dt_seconds;
    }
    else
    {
        active_player->vel.vy *= 0.85f;
    }

    constexpr float horizontal_damping = 0.80f;
    active_player->vel.vx *= horizontal_damping;
    active_player->vel.vz *= horizontal_damping;

    active_player->pos.y += active_player->vel.vy * dt_seconds;

    int hx, hy, hz, face;
    if (hit_test_face(&hx, &hy, &hz, &face))
    {
        current_projected_plane.target_x = hx;
        current_projected_plane.target_y = hy;
        current_projected_plane.target_z = hz;
        current_projected_plane.target_face = face;
        current_projected_plane.has_valid_target = true;
    }
    else
    {
        current_projected_plane.has_valid_target = false;
    }

    if (!active_player->is_flying())
    {
        if (const int collision_result = collide(2, &active_player->pos.x, &active_player->pos.y, &active_player->pos.z);
            collision_result == 1)
        {
            active_player->vel.vy = 0.0f;
            active_player->set_on_ground(true);
        }
        else
        {
            active_player->set_on_ground(false);
        }
    }
    else
    {
        active_player->set_on_ground(false);
    }

    delete_chunks();
    sdl_gl_helper::del_buffer(active_player->get_buffer());
    ensure_chunks(active_player);
    update_dirty_chunks_async();
}

void world::draw() const noexcept
{
    CHECK_GL_ERR();

    int viewport_width, viewport_height;
    SDL_GetWindowSizeInPixels(simple_direct_medialayer->window, &viewport_width, &viewport_height);
    glViewport(0, 0, viewport_width, viewport_height);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.53f, 0.81f, 0.92f, 1.0f);

    m_bloom.bind_scene();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const auto atlas_texture = world_textures.get(TextureIdentifier::ATLAS).gl_texture;
    const auto signs_texture = world_textures.get(TextureIdentifier::SIGNS).gl_texture;
    const auto sky_texture   = world_textures.get(TextureIdentifier::SKY).gl_texture;

    m_bloom.set_single_mode();
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    render_sky(world_sky_gl_buffer, sky_texture);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);

    m_bloom.set_mrt_mode();
    [[maybe_unused]] const auto triangle_faces = render_chunks(atlas_texture);
    render_player(atlas_texture);

    m_bloom.set_single_mode();
    render_item(atlas_texture);
    render_plane();

    render_signs(signs_texture);
    render_sign(signs_texture);

    std::array<std::string, 3> debug_lines{"Press [ESC] for menu",
                                           "Press [E] to make new maze",
                                           "Press [B] to build a maze"};
    SDL_snprintf(debug_lines[0].data(), debug_lines[0].size(), "%s", debug_lines.at(0).c_str());
    render_text(world_textures.get(TextureIdentifier::BITMAP_FONT).gl_texture, 0,
                10, viewport_height - 15, 12.0f, debug_lines[0].c_str());
    render_text(world_textures.get(TextureIdentifier::BITMAP_FONT).gl_texture, 0,
                10, viewport_height - 35, 12.0f, debug_lines[1].c_str());
    render_text(world_textures.get(TextureIdentifier::BITMAP_FONT).gl_texture, 0,
                10, viewport_height - 55, 12.0f, debug_lines[2].c_str());

    render_wireframe();
    render_crosshairs();
    render_grid_overlay();
    render_hover_info();
    render_maze_preview_ghost();

    const float bloom_str = active_player->_configs.use_bloom_effect() ? bloom_pass::BLOOM_STRENGTH : 0.0f;
    m_bloom.execute(bloom_str);
}

void world::draw_preview(const std::uint32_t target_fbo, const int target_width, const int target_height) const noexcept
{
    if (target_fbo == 0 || target_width <= 0 || target_height <= 0)
        return;

    CHECK_GL_ERR();

    GLint previous_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);
    std::array<GLint, 4> previous_viewport{};
    glGetIntegerv(GL_VIEWPORT, previous_viewport.data());

    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glViewport(0, 0, target_width, target_height);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.53f, 0.81f, 0.92f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const auto atlas_texture = world_textures.get(TextureIdentifier::ATLAS).gl_texture;
    const auto sky_texture   = world_textures.get(TextureIdentifier::SKY).gl_texture;

    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    render_sky(world_sky_gl_buffer, sky_texture);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);

    [[maybe_unused]] const auto triangle_faces = render_chunks(atlas_texture);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_fbo));
    glViewport(previous_viewport[0], previous_viewport[1], previous_viewport[2], previous_viewport[3]);
}

command_queue &world::get_command_queue() noexcept
{
    return commands_during_world_events;
}

void world::destroy_world()
{
    db_flush();
    m_bloom.destroy();
    delete_all_chunks();
    if (active_player)
    {
        active_player->set_world(nullptr);
        sdl_gl_helper::del_buffer(active_player->get_buffer());
        active_player = nullptr;
    }
    cleanup_worker_threads();
}

void world::handle_event(const SDL_Event &event) noexcept
{
    if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        const int w = event.window.data1;
        const int h = event.window.data2;
        if (w > 0 && h > 0)
        {
            m_bloom.resize(w, h);
            glViewport(0, 0, w, h);
        }
    }
}

// ---------------------------------------------------------------------------
// Building queue / preview
// ---------------------------------------------------------------------------

bool world::update_preview(const std::vector<std::uint8_t> &pixel_data,
                           const int width, const int height) const noexcept
{
    if (pixel_data.empty() || width <= 0 || height <= 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Invalid maze preview buffer\n");
        return false;
    }
    if (!current_projected_plane.projected_texture)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Maze preview texture is not initialized\n");
        return false;
    }
    if (!current_projected_plane.projected_texture->update_from_memory(
            pixel_data.data(), width, height,
            static_cast<std::uint32_t>(TextureIdentifier::MAZE), false))
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to update maze texture from memory\n");
        return false;
    }
    return true;
}

void world::finalize_buildings(std::vector<std::uint8_t> pixel_data,
                               int width, int height, int scale,
                               int wall_height, int item_type) noexcept
{
    current_preview_data.pixel_data = std::move(pixel_data);
    current_preview_data.width      = width;
    current_preview_data.height     = height;
    current_preview_data.scale      = scale;
    current_preview_data.wall_height = wall_height;
    current_preview_data.item_type  = item_type;
    current_preview_data.has_data   = true;
}

void world::commit_preview_to_world(int item_type) noexcept
{
    if (!current_preview_data.has_data)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "No preview to build - press 'E' first to generate a preview\n");
        return;
    }
    if (current_preview_data.item_type != item_type)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Maze preview is stale after changing the build item - press 'E' to regenerate it\n");
        return;
    }
    if (current_preview_data.scale <= 0 || current_preview_data.width <= 0 || current_preview_data.height <= 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Maze preview has invalid dimensions - press 'E' to regenerate it\n");
        return;
    }

    const int face   = current_projected_plane.target_face;
    const ivec3 normal = face_normal(face);
    const ivec3 axis_u = face_u_axis(face);
    const ivec3 axis_v = face_v_axis(face);

    const int anchor_x = current_projected_plane.target_x + normal.x;
    const int anchor_y = current_projected_plane.target_y + normal.y;
    const int anchor_z = current_projected_plane.target_z + normal.z;

    const int logical_width  = current_preview_data.width  / current_preview_data.scale;
    const int logical_height = current_preview_data.height / current_preview_data.scale;

    for (int cell_y = 0; cell_y < logical_height; ++cell_y)
    {
        for (int cell_x = 0; cell_x < logical_width; ++cell_x)
        {
            const int pix_x = cell_x * current_preview_data.scale + current_preview_data.scale / 2;
            const int pix_y = cell_y * current_preview_data.scale + current_preview_data.scale / 2;
            const int pixel_index = (pix_y * current_preview_data.width + pix_x) * 4;

            if (pixel_index < 0 || pixel_index + 2 >= static_cast<int>(current_preview_data.pixel_data.size()))
                continue;

            const uint8_t r = current_preview_data.pixel_data[pixel_index + 0];
            const uint8_t g = current_preview_data.pixel_data[pixel_index + 1];
            const uint8_t b = current_preview_data.pixel_data[pixel_index + 2];

            if (r < 50 && g < 50 && b < 50)
            {
                for (int depth = 0; depth < current_preview_data.wall_height; ++depth)
                {
                    const int world_x = anchor_x + cell_x * axis_u.x + cell_y * axis_v.x + depth * normal.x;
                    const int world_y = anchor_y + cell_x * axis_u.y + cell_y * axis_v.y + depth * normal.y;
                    const int world_z = anchor_z + cell_x * axis_u.z + cell_y * axis_v.z + depth * normal.z;
                    set_block(world_x, world_y, world_z, item_type);
                }
            }
        }
    }
}

void world::process_build_queue() noexcept
{
    std::ranges::for_each(m_player_building_events, [](const auto &f) { f(); });
    m_player_building_events.clear();
}

// ---------------------------------------------------------------------------
// Time helpers
// ---------------------------------------------------------------------------

double world::get_time() const noexcept
{
    return (static_cast<double>(SDL_GetTicks())
            + static_cast<double>(active_player->_configs.start_time())
            - static_cast<double>(active_player->_configs.start_ticks())) / 1000.0;
}

float world::time_of_day() const noexcept
{
    if (active_player->_configs.day_length() <= 0)
        return 0.5f;
    auto t = static_cast<float>(get_time());
    t /= static_cast<float>(active_player->_configs.day_length());
    t -= static_cast<float>(static_cast<int>(t));
    return t;
}

float world::get_daylight() const noexcept
{
    if (const float timer = time_of_day(); timer < 0.5)
    {
        const float t = (timer - 0.25f) * 100.f;
        return 1 / (1 + SDL_powf(2.f, -t));
    }
    else
    {
        const float t = (timer - 0.85f) * 100.f;
        return 1.f - 1.f / (1.f + SDL_powf(2.f, -t));
    }
}

// ---------------------------------------------------------------------------
// Chunk query helpers (used by many methods across all .cpp files)
// ---------------------------------------------------------------------------

int world::chunked(const float x) noexcept
{
    return static_cast<int>(SDL_floorf(SDL_roundf(x) / static_cast<float>(BUILD_CHUNK_SIZE)));
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

int world::chunk_distance(const chunk *c, const int p, const int q) noexcept
{
    const int dp = SDL_abs(c->p - p);
    const int dq = SDL_abs(c->q - q);
    return SDL_max(dp, dq);
}

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
        if (auto *signs = &c->signs; sign_list_remove_all(signs, x, y, z))
        {
            c->dirty = 1;
            db_delete_signs(x, y, z);
        }
    }
    else
    {
        db_delete_signs(x, y, z);
    }
}

void world::unset_sign_face(const int x, const int y, const int z, const int face) const noexcept
{
    const int p = chunked(static_cast<float>(x));
    const int q = chunked(static_cast<float>(z));
    if (const auto chunk_opt = find_chunk(p, q); chunk_opt.has_value())
    {
        chunk *c = chunk_opt.value();
        if (auto *signs = &c->signs; sign_list_remove(signs, x, y, z, face))
        {
            c->dirty = 1;
            db_delete_sign(x, y, z, face);
        }
    }
    else
    {
        db_delete_sign(x, y, z, face);
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
        sign_list_add(signs, x, y, z, face, text.data());
        if (dirty)
            c->dirty = 1;
    }
    db_insert_sign(p, q, x, y, z, face, text.data());
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
        voxels_map *map = &c->lights;
        const int w = map->get(x, y, z) ? 0 : 15;
        map->set(x, y, z, w);
        db_insert_light(p, q, x, y, z, w);
        dirty_chunk(c);
    }
}

void world::set_light(int p, int q, int x, int y, int z, int w) const noexcept
{
    if (auto chunk_opt = find_chunk(p, q); chunk_opt.has_value())
    {
        chunk *c = chunk_opt.value();
        if (voxels_map *map = &c->lights; map->set(x, y, z, w))
        {
            dirty_chunk(c);
            db_insert_light(p, q, x, y, z, w);
        }
    }
    else
    {
        db_insert_light(p, q, x, y, z, w);
    }
}

void world::_set_block(const int p, int q, int x, int y, int z, const int w, const int dirty) const noexcept
{
    if (const auto chunk_opt = find_chunk(p, q); chunk_opt.has_value())
    {
        chunk *c = chunk_opt.value();
        if (voxels_map *map = &c->map; map->set(x, y, z, w))
        {
            if (dirty)
                dirty_chunk(c);
            db_insert_block(p, q, x, y, z, w);
        }
    }
    else
    {
        db_insert_block(p, q, x, y, z, w);
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
        const voxels_map *map = &chunk_opt.value()->map;
        return map->get(x, y, z);
    }
    return 0;
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
