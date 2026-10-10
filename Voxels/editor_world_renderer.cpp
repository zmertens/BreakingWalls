// Editor overlays, sky, signs, and ghost render passes.
#include "editor_world.h"

#include "geometries.h"
#include "item.h"
#include "matrix.h"
#include "texture.h"
#include "resource_identifiers.h"
#include "resource_manager.h"
#include "sdl_gl_helper.h"

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <glad/gl.h>
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <vector>

// ---------------------------------------------------------------------------
// Matrix setup helpers
// ---------------------------------------------------------------------------

std::pair<int, int> editor_world::begin_sky_pass(const sdl_gl_helper::attrib *a, float matrix[16]) const noexcept
{
    auto [w, h] = simple_direct_medialayer->get_window_size();
    const auto *s = &active_player->pos;
    matrix::set_3d(matrix, w, h,
                   0, 0, 0, s->rx, s->ry,
                   active_player->get_configs().fov(), 0, RENDER_CHUNK_RADIUS);
    glUseProgram(a->program);
    glUniformMatrix4fv(a->matrix, 1, GL_FALSE, matrix);
    return {w, h};
}

std::pair<int, int> editor_world::begin_item_pass(const sdl_gl_helper::attrib *a, float matrix[16]) const noexcept
{
    auto [w, h] = simple_direct_medialayer->get_window_size();
    matrix::set_item(matrix, w, h, simple_direct_medialayer->get_scale_factor());
    glUseProgram(a->program);
    glUniformMatrix4fv(a->matrix, 1, GL_FALSE, matrix);
    return {w, h};
}

std::pair<int, int> editor_world::begin_2d_pass(const sdl_gl_helper::attrib *a, float matrix[16]) const noexcept
{
    auto [w, h] = simple_direct_medialayer->get_window_size();
    matrix::set_2d(matrix, w, h);
    glUseProgram(a->program);
    glUniformMatrix4fv(a->matrix, 1, GL_FALSE, matrix);
    return {w, h};
}

// ---------------------------------------------------------------------------
// Main render passes
// ---------------------------------------------------------------------------

void editor_world::render_signs(const std::uint32_t sign) const noexcept
{
    float matrix[16];
    begin_3d_pass(&s_text_attrib, matrix);
    const player::position *s = &this->active_player->pos;
    const int p = chunked(s->x), q = chunked(s->z);
    float planes[6][4];
    matrix::frustum_planes(planes, RENDER_CHUNK_RADIUS, matrix);
    glActiveTexture(GL_TEXTURE0 + static_cast<unsigned int>(TextureIdentifier::SIGNS));
    glBindTexture(GL_TEXTURE_2D, sign);
    glUniform1i(s_text_attrib.sampler, static_cast<unsigned int>(TextureIdentifier::SIGNS));
    glUniform1i(s_text_attrib.extra1, 1);

    const int min_p = p - RENDER_SIGN_RADIUS, min_q = q - RENDER_SIGN_RADIUS;
    const int max_p = p + RENDER_SIGN_RADIUS, max_q = q + RENDER_SIGN_RADIUS;

    traverse_chunks_in_bounds(min_p, min_q, max_p, max_q, [&](chunk *c)
    {
        if (chunk_distance(c, p, q) > RENDER_SIGN_RADIUS)
        {
            return;
        }
        if (!chunk_visible(planes, c->p, c->q, c->miny, c->maxy))
        {
            return;
        }
        sdl_gl_helper::draw_signs(&s_text_attrib, c);
    });
}

void editor_world::render_sign(const std::uint32_t sign) const noexcept
{
    int x, y, z, face;
    if (!hit_test_face(&x, &y, &z, &face))
    {
        return;
    }

    float matrix[16];
    begin_3d_pass(&s_text_attrib, matrix);
    glActiveTexture(GL_TEXTURE0 + static_cast<unsigned int>(TextureIdentifier::SIGNS));
    glBindTexture(GL_TEXTURE_2D, sign);
    glUniform1i(s_text_attrib.sampler, static_cast<unsigned int>(TextureIdentifier::SIGNS));
    glUniform1i(s_text_attrib.extra1, 1);
    char text[db::MAX_SIGN_LENGTH];
    SDL_strlcpy(text, active_player->get_configs().tag().c_str(), db::MAX_SIGN_LENGTH);
    text[db::MAX_SIGN_LENGTH - 1] = '\0';
    GLfloat *data = sdl_gl_helper::malloc_faces(5, SDL_strlen(text));
    const int length = sdl_gl_helper::_gen_sign_buffer(data, static_cast<float>(x), static_cast<float>(y),
                                                       static_cast<float>(z), face, text);
    const GLuint buffer = sdl_gl_helper::gen_faces(5, length, data);
    sdl_gl_helper::draw_sign(&s_text_attrib, buffer, length);
    sdl_gl_helper::del_buffer(buffer);
}

void editor_world::render_sky(const std::uint32_t buffer, const std::uint32_t sky_tex) const noexcept
{
    float matrix[16];
    begin_sky_pass(&s_sky_attrib, matrix);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sky_tex);
    glUniform1i(s_sky_attrib.sampler, 0);
    glUniform1f(s_sky_attrib.timer, time_of_day());
    sdl_gl_helper::draw_triangles_3d(&s_sky_attrib, buffer, 512 * 3);
}

void editor_world::render_wireframe() const noexcept
{
    float matrix[16];
    begin_3d_pass(&s_line_attrib, matrix);
    const player::position *s = &active_player->pos;
    int hx, hy, hz;
    if (const int hw = hit_test(0, s->x, s->y, s->z, s->rx, s->ry, &hx, &hy, &hz);
        item::is_obstacle(hw))
    {
        glLineWidth(1);
        const GLuint wireframe_buffer = sdl_gl_helper::gen_wireframe_buffer(
            static_cast<float>(hx), static_cast<float>(hy), static_cast<float>(hz), 0.53f);
        sdl_gl_helper::draw_lines(&s_line_attrib, wireframe_buffer, 3, 24);
        sdl_gl_helper::del_buffer(wireframe_buffer);
    }
}

void editor_world::render_crosshairs() const noexcept
{
    float matrix[16];
    begin_2d_pass(&s_line_attrib, matrix);
    glLineWidth(static_cast<GLfloat>(4 * simple_direct_medialayer->get_scale_factor()));
    const GLuint crosshair_buffer = simple_direct_medialayer->gen_crosshair_buffer();
    sdl_gl_helper::draw_lines(&s_line_attrib, crosshair_buffer, 2, 4);
    sdl_gl_helper::del_buffer(crosshair_buffer);
}

void editor_world::render_hover_info() const noexcept
{
    if (!active_player->get_configs().show_crosshair_details() || !current_projected_plane.has_valid_target)
     {SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "No hover info");   return;
     }

    int viewport_width, viewport_height;
    SDL_GetWindowSizeInPixels(simple_direct_medialayer->window, &viewport_width, &viewport_height);

    const int hx = current_projected_plane.target_x;
    const int hy = current_projected_plane.target_y;
    const int hz = current_projected_plane.target_z;
    const int face = current_projected_plane.target_face;
    const int block_type = get_block(hx, hy, hz);

    const float dx = active_player->pos.x - static_cast<float>(hx);
    const float dy = active_player->pos.y - static_cast<float>(hy);
    const float dz = active_player->pos.z - static_cast<float>(hz);
    const float distance = SDL_sqrt(dx * dx + dy * dy + dz * dz);

    const float x_offset   = static_cast<float>(viewport_width) * 0.5f + 30.0f;
    const float y_base      = static_cast<float>(viewport_height) * 0.5f;
    const float line_height = 15.0f;

    char info_buffer[256];
    const auto font_tex = world_textures.get(TextureIdentifier::BITMAP_FONT).gl_texture;

    SDL_snprintf(info_buffer, sizeof(info_buffer), "Block: %s",
                 player::get_block_name(item::get_block_type(block_type)));
    render_text(font_tex, 0, x_offset, y_base - line_height * 2, 10.0f, info_buffer);

    SDL_snprintf(info_buffer, sizeof(info_buffer), "Pos: (%d, %d, %d)", hx, hy, hz);
    render_text(font_tex, 0, x_offset, y_base - line_height, 10.0f, info_buffer);

    SDL_snprintf(info_buffer, sizeof(info_buffer), "Face: %s", player::get_face_name(face));
    render_text(font_tex, 0, x_offset, y_base, 10.0f, info_buffer);

    SDL_snprintf(info_buffer, sizeof(info_buffer), "Dist: %.1f", distance);
    render_text(font_tex, 0, x_offset, y_base + line_height, 10.0f, info_buffer);

    constexpr std::string_view BUILD_HINTS{"Press [E] to preview - [B] to build"};
    render_text(world_textures.get(TextureIdentifier::BITMAP_FONT).gl_texture, 0, static_cast<float>(viewport_width / 2) - 10.f, 55.f, 14.0f, BUILD_HINTS.data());
}

void editor_world::render_ghost() const noexcept
{
    if (!active_player->get_configs().show_ghosts() || !current_ghost.has_data ||
        !current_projected_plane.has_valid_target)
        return;

    const int face      = current_projected_plane.target_face;
    const ivec3 normal  = face_normal(face);
    const ivec3 axis_u  = face_u_axis(face);
    const ivec3 axis_v  = face_v_axis(face);

    const int anchor_x  = current_projected_plane.target_x + normal.x;
    const int anchor_y  = current_projected_plane.target_y + normal.y;
    const int anchor_z  = current_projected_plane.target_z + normal.z;

    float matrix[16];
    begin_3d_pass(&s_line_attrib, matrix);
    glLineWidth(1.0f);

    std::vector<float> wireframe_data;

    const float player_x = active_player->pos.x;
    const float player_y = active_player->pos.y;
    const float player_z = active_player->pos.z;
    constexpr float render_distance = 64.0f;

    // Only the first and last layers are outlined to keep tall ghosts cheap to draw.
    const int depth_step = std::max(1, current_ghost.wall_height - 1);

    for (int v = 0; v < current_ghost.height; ++v)
    {
        for (int u = 0; u < current_ghost.width; ++u)
        {
            if (!current_ghost.walls[static_cast<std::size_t>(v) * static_cast<std::size_t>(current_ghost.width) +
                                     static_cast<std::size_t>(u)])
                continue;

            for (int depth = 0; depth < current_ghost.wall_height; depth += depth_step)
            {
                const int world_x = anchor_x + axis_u.x * u + axis_v.x * v + normal.x * depth;
                const int world_y = anchor_y + axis_u.y * u + axis_v.y * v + normal.y * depth;
                const int world_z = anchor_z + axis_u.z * u + axis_v.z * v + normal.z * depth;

                const float fx = static_cast<float>(world_x) - player_x;
                const float fy = static_cast<float>(world_y) - player_y;
                const float fz = static_cast<float>(world_z) - player_z;
                if (fx * fx + fy * fy + fz * fz > render_distance * render_distance)
                    continue;
                if (get_block(world_x, world_y, world_z) != 0)
                    continue;

                float cube_data[72];
                geometries::make_cube_wireframe(cube_data,
                                               static_cast<float>(world_x),
                                               static_cast<float>(world_y),
                                               static_cast<float>(world_z),
                                               0.51f);
                wireframe_data.insert(wireframe_data.end(), cube_data, cube_data + 72);
            }
        }
    }

    if (!wireframe_data.empty())
    {
        const GLuint batch_buffer = sdl_gl_helper::gen_buffer(
            wireframe_data.size() * sizeof(float), wireframe_data.data());
        sdl_gl_helper::draw_lines(&s_line_attrib, batch_buffer, 3,
                                  static_cast<int>(wireframe_data.size() / 3));
        sdl_gl_helper::del_buffer(batch_buffer);
    }
    glLineWidth(1.0f);
}

void editor_world::render_item(const std::uint32_t texture) const noexcept
{
    const GLboolean depth_was_enabled = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    float matrix[16];
    begin_item_pass(&s_block_attrib, matrix);
    glActiveTexture(GL_TEXTURE0 + static_cast<unsigned int>(TextureIdentifier::ATLAS));
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform3f(s_block_attrib.camera, 0, 0, 5);
    glUniform1i(s_block_attrib.sampler, 0);
    glUniform1f(s_block_attrib.timer, time_of_day());
    glUniform1i(s_block_attrib.extra4, 0);

    if (const int w = active_player->get_item(); item::is_plant(w))
    {
        const GLuint buffer = sdl_gl_helper::gen_plant_buffer(0, 0, 0, 0.5f, w);
        sdl_gl_helper::draw_plant(&s_block_attrib, buffer);
        sdl_gl_helper::del_buffer(buffer);
    }
    else
    {
        const GLuint buffer = sdl_gl_helper::gen_cube_buffer(0, 0, 0, 0.5f, w);
        sdl_gl_helper::draw_cube(&s_block_attrib, buffer);
        sdl_gl_helper::del_buffer(buffer);
    }

    glDepthMask(GL_TRUE);
    if (depth_was_enabled)
        glEnable(GL_DEPTH_TEST);
}

void editor_world::render_player(const std::uint32_t texture) const noexcept
{
    const auto &c = active_player->get_configs();
    if (c.ortho_scaling() < 1)
        return;

    float matrix[16];
    begin_3d_pass(&s_block_attrib, matrix);
    const player::position *s = &active_player->pos;
    glActiveTexture(GL_TEXTURE0 + static_cast<unsigned int>(TextureIdentifier::ATLAS));
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform3f(s_block_attrib.camera, s->x, s->y, s->z);
    glUniform1i(s_block_attrib.sampler, 0);
    glUniform1f(s_block_attrib.timer, time_of_day());

    const bool is_isometric_mode     = c.view_mode() == player_configs::ViewMode::ISOMETRIC;
    const bool is_orthographic_active = c.ortho_scaling() > 0;
    const bool use_isometric_fx      = is_isometric_mode || is_orthographic_active;

    static bool logged_iso_player_fx = false;
    if (use_isometric_fx && !logged_iso_player_fx)
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "Player marker FX enabled (mode=%d, ortho=%d)\n",
                     static_cast<int>(c.view_mode()), c.ortho_scaling());
        logged_iso_player_fx = true;
    }

    const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;

    float px = s->x, py = s->y - 0.5f, pz = s->z;
    if (use_isometric_fx)
    {
        float vx, vy, vz;
        matrix::compute_sight_vector(s->rx, s->ry, vx, vy, vz);
        constexpr float marker_distance = 3.0f;
        px = s->x + vx * marker_distance;
        py = s->y + vy * marker_distance - 0.35f + 0.16f * SDL_sinf(t * 3.0f);
        pz = s->z + vz * marker_distance;
    }
    else
    {
        constexpr float offset_distance = 3.0f;
        px = s->x - offset_distance * SDL_sinf(s->rx);
        pz = s->z + offset_distance * SDL_cosf(s->rx);
    }

    const float animated_rx = use_isometric_fx
                                  ? (s->rx + 0.20f * SDL_sinf(t * 2.4f) + 0.10f * SDL_sinf(t * 6.1f))
                                  : s->rx;
    const float animated_ry = use_isometric_fx
                                  ? (s->ry + 0.08f * SDL_sinf(t * 1.9f + 0.7f))
                                  : s->ry;

    const GLboolean depth_was_enabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cull_was_enabled  = glIsEnabled(GL_CULL_FACE);
    if (use_isometric_fx) { glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); }

    const GLuint player_buffer = sdl_gl_helper::gen_player_buffer(px, py, pz, animated_rx, animated_ry);
    sdl_gl_helper::draw_player(&s_block_attrib, player_buffer);
    sdl_gl_helper::del_buffer(player_buffer);

    if (use_isometric_fx)
    {
        float line_matrix[16];
        begin_3d_pass(&s_line_attrib, line_matrix);
        const float pulse_n = 0.55f + 0.10f * SDL_sinf(t * 3.8f);
        glLineWidth(2.0f);
        const GLuint halo = sdl_gl_helper::gen_wireframe_buffer(px, py, pz, pulse_n);
        sdl_gl_helper::draw_lines(&s_line_attrib, halo, 3, 24);
        sdl_gl_helper::del_buffer(halo);
        glLineWidth(1.0f);

        constexpr int particle_count = 10;
        for (int i = 0; i < particle_count; ++i)
        {
            const float phase = t * 2.0f + static_cast<float>(i) * 0.65f;
            const float radius = 0.90f + 0.16f * SDL_sinf(t * 2.2f + static_cast<float>(i));
            const float ox = SDL_cosf(phase) * radius;
            const float oy = 0.35f * SDL_sinf(phase * 1.37f);
            const float oz = SDL_sinf(phase) * radius;
            const float sm = 0.05f;

            const GLuint lx = sdl_gl_helper::gen_line_buffer(
                px + ox - sm, py + oy, pz + oz, px + ox + sm, py + oy, pz + oz);
            sdl_gl_helper::draw_lines(&s_line_attrib, lx, 3, 2);
            sdl_gl_helper::del_buffer(lx);

            const GLuint ly = sdl_gl_helper::gen_line_buffer(
                px + ox, py + oy - sm, pz + oz, px + ox, py + oy + sm, pz + oz);
            sdl_gl_helper::draw_lines(&s_line_attrib, ly, 3, 2);
            sdl_gl_helper::del_buffer(ly);
        }

        float overlay_matrix[16];
        auto [overlay_w, overlay_h] = begin_2d_pass(&s_line_attrib, overlay_matrix);
        const float cx = static_cast<float>(overlay_w) * 0.5f;
        const float cy = static_cast<float>(overlay_h) * 0.5f;
        const float r  = 18.0f + 5.0f * SDL_sinf(t * 4.2f);

        const GLuint top    = sdl_gl_helper::gen_line_buffer(cx - r, cy + r, 0.0f, cx + r, cy + r, 0.0f);
        sdl_gl_helper::draw_lines(&s_line_attrib, top, 3, 2); sdl_gl_helper::del_buffer(top);
        const GLuint right  = sdl_gl_helper::gen_line_buffer(cx + r, cy + r, 0.0f, cx + r, cy - r, 0.0f);
        sdl_gl_helper::draw_lines(&s_line_attrib, right, 3, 2); sdl_gl_helper::del_buffer(right);
        const GLuint bottom = sdl_gl_helper::gen_line_buffer(cx + r, cy - r, 0.0f, cx - r, cy - r, 0.0f);
        sdl_gl_helper::draw_lines(&s_line_attrib, bottom, 3, 2); sdl_gl_helper::del_buffer(bottom);
        const GLuint left   = sdl_gl_helper::gen_line_buffer(cx - r, cy - r, 0.0f, cx - r, cy + r, 0.0f);
        sdl_gl_helper::draw_lines(&s_line_attrib, left, 3, 2); sdl_gl_helper::del_buffer(left);

        constexpr int overlay_particles = 8;
        for (int i = 0; i < overlay_particles; ++i)
        {
            const float phase = t * 2.8f + static_cast<float>(i) * 0.785f;
            const float pr = r + 10.0f + 3.0f * SDL_sinf(t * 3.1f + static_cast<float>(i));
            const float px2 = cx + SDL_cosf(phase) * pr;
            const float py2 = cy + SDL_sinf(phase) * pr;
            const float s2 = 2.0f;

            const GLuint hp = sdl_gl_helper::gen_line_buffer(px2 - s2, py2, 0.0f, px2 + s2, py2, 0.0f);
            sdl_gl_helper::draw_lines(&s_line_attrib, hp, 3, 2); sdl_gl_helper::del_buffer(hp);
            const GLuint vp = sdl_gl_helper::gen_line_buffer(px2, py2 - s2, 0.0f, px2, py2 + s2, 0.0f);
            sdl_gl_helper::draw_lines(&s_line_attrib, vp, 3, 2); sdl_gl_helper::del_buffer(vp);
        }

        if (depth_was_enabled) glEnable(GL_DEPTH_TEST);
        if (cull_was_enabled)  glEnable(GL_CULL_FACE);
    }
}

void editor_world::render_text(const std::uint32_t font, const int justify,
                        float x, const float y, const float n, const std::string_view text) const noexcept
{
    while (glGetError() != GL_NO_ERROR) {}

    float matrix[16];
    begin_2d_pass(&s_text_attrib, matrix);

    glUniform1i(s_text_attrib.sampler, static_cast<int>(TextureIdentifier::BITMAP_FONT));
    glUniform1i(s_text_attrib.extra1, 0);

    glActiveTexture(GL_TEXTURE0 + static_cast<unsigned int>(TextureIdentifier::BITMAP_FONT));
    glBindTexture(GL_TEXTURE_2D, font);

    const GLsizei length = static_cast<GLsizei>(text.length());
    if (length <= 0) return;

    x -= n * justify * (length - 1) / 2;
    const GLuint buffer = sdl_gl_helper::gen_text_buffer(x, y, n, text);
    if (buffer == 0) return;

    sdl_gl_helper::draw_text(&s_text_attrib, buffer, length);
    sdl_gl_helper::del_buffer(buffer);
}
