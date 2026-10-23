#include "world.h"

#include "chunk_worker.h"
#include "resource_manager.h"
#include "shader.h"

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <glad/gl.h>
#endif
#include <SDL3/SDL.h>
#include <stdexcept>

world::builder::builder(font_manager &fonts, shader_manager &shaders,
                        texture_manager &textures, bloom_pass_manager &bloom_passes,
                        player &active_player, sdl_gl_helper &sdl)
    : build_fonts{&fonts}, build_shaders{&shaders}, build_textures{&textures},
      build_bloom_passes{&bloom_passes}, build_player{&active_player}, build_sdl{&sdl}
{
}

world::world(const builder &b)
    : simple_direct_medialayer{b.build_sdl}, active_fonts{*b.build_fonts}, world_shaders{*b.build_shaders},
      world_textures{*b.build_textures}, scene_graph_layers{}, next_chunk_slot_in_view{1},
      active_player{b.build_player}, m_bloom{b.build_bloom_passes ? &b.build_bloom_passes->get(BloomPassIdentifier::EDITOR) : nullptr}
{
    if (!m_bloom)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "world: a loaded bloom pass is required");
    }
}

world::~world()
{
    cleanup_worker_threads();
    delete_all_chunks();
    auto &root = scene_graph_layers[static_cast<std::size_t>(Layer::BACKGROUND)][0];
    delete root;
    root = nullptr;
}

void world::init_voxel_engine() noexcept
{
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    build_scene();

    s_block_attrib.program = world_shaders.get(ShaderIdentifier::BLOCK_SHADER).get();
    s_block_attrib.position = 0;
    s_block_attrib.normal = 1;
    s_block_attrib.uv = 2;
    s_block_attrib.matrix = glGetUniformLocation(s_block_attrib.program, "matrix");
    s_block_attrib.sampler = glGetUniformLocation(s_block_attrib.program, "sampler");
    s_block_attrib.extra1 = glGetUniformLocation(s_block_attrib.program, "sky_sampler");
    s_block_attrib.extra2 = glGetUniformLocation(s_block_attrib.program, "daylight");
    s_block_attrib.extra3 = glGetUniformLocation(s_block_attrib.program, "fog_distance");
    s_block_attrib.extra4 = glGetUniformLocation(s_block_attrib.program, "is_ortho");
    s_block_attrib.camera = glGetUniformLocation(s_block_attrib.program, "camera");
    s_block_attrib.timer = glGetUniformLocation(s_block_attrib.program, "timer");
}

// Get current time in seconds
double world::get_time_s() const noexcept
{
    return (static_cast<double>(SDL_GetTicks()) + static_cast<double>(active_player->get_configs().start_time()) - static_cast<double>(active_player->get_configs().start_ticks())) / 1000.0;
}

// Returns time of day as fraction of day
float world::time_of_day() const noexcept
{
    if (active_player->get_configs().day_length() <= 0)
    {
        return 0.5f;
    }
    auto t = static_cast<float>(get_time_s());
    t /= static_cast<float>(active_player->get_configs().day_length());
    t -= static_cast<float>(static_cast<int>(t));
    return t;
}

// Daylight calculation
// Returns daylight factor based on time of day
float world::get_daylight() const noexcept
{
    if (const float timer = time_of_day(); timer < 0.5)
    {
        const float t = (timer - 0.25f) * 100.f;
        return 1.f / (1.f + SDL_powf(2.f, -t));
    }
    else
    {
        const float t = (timer - 0.85f) * 100.f;
        return 1.f - 1.f / (1.f + SDL_powf(2.f, -t));
    }
}
