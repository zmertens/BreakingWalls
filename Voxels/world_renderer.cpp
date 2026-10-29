// Shared voxel mesh rendering.
#include "world.h"

#include "chunk.h"
#include "matrix.h"
#include "resource_identifiers.h"
#include "sdl_gl_helper.h"

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <glad/gl.h>
#endif

#include <SDL3/SDL.h>

std::pair<int, int> world::begin_3d_pass(const sdl_gl_helper::attrib *a, float matrix[16]) const noexcept
{
    auto [w, h] = simple_direct_medialayer->get_window_size();
    const auto *s = &active_player->pos;
    const auto cam = camera_position();
    const auto rot = camera_rotation();
    matrix::set_3d(matrix, w, h,
                   cam[0], cam[1], cam[2], rot[0], rot[1],
                   active_player->get_configs().fov(),
                   active_player->get_configs().ortho_scaling(),
                   RENDER_CHUNK_RADIUS);
    glUseProgram(a->program);
    glUniformMatrix4fv(a->matrix, 1, GL_FALSE, matrix);
    return {w, h};
}

int world::render_chunks(const std::uint32_t texture) const noexcept
{
    float matrix[16];
    begin_3d_pass(&s_block_attrib, matrix);
    int result = 0;
    const player::position *s = &this->active_player->pos;
    const int p = chunked(s->x), q = chunked(s->z);
    const float light = get_daylight();
    float planes[6][4];
    matrix::frustum_planes(planes, RENDER_CHUNK_RADIUS, matrix);
    glActiveTexture(GL_TEXTURE0 + static_cast<unsigned int>(TextureIdentifier::ATLAS));
    glBindTexture(GL_TEXTURE_2D, texture);
    const auto cam = camera_position();
    glUniform3f(s_block_attrib.camera, cam[0], cam[1], cam[2]);
    glUniform1i(s_block_attrib.sampler, 0);
    glUniform1f(s_block_attrib.extra2, light);
    glUniform1f(s_block_attrib.extra3, static_cast<GLfloat>(RENDER_CHUNK_RADIUS * BUILD_CHUNK_SIZE));
    glUniform1i(s_block_attrib.extra4, active_player->get_configs().ortho_scaling());
    glUniform1f(s_block_attrib.timer, time_of_day());

    int chunks_rendered = 0, chunks_culled_distance = 0, chunks_culled_frustum = 0;
    const int min_p = p - RENDER_CHUNK_RADIUS, min_q = q - RENDER_CHUNK_RADIUS;
    const int max_p = p + RENDER_CHUNK_RADIUS, max_q = q + RENDER_CHUNK_RADIUS;

    traverse_chunks_in_bounds(min_p, min_q, max_p, max_q, [&](chunk *c)
    {
        if (chunk_distance(c, p, q) > RENDER_CHUNK_RADIUS)
        {
            chunks_culled_distance++;
            return;
        }
        if (!chunk_visible(planes, c->p, c->q, c->miny, c->maxy))
        {
            chunks_culled_frustum++;
            return;
        }
        sdl_gl_helper::draw_chunk(&s_block_attrib, c);
        result += c->faces;
        chunks_rendered++;
    });

    return result;
}
