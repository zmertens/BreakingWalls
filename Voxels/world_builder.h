#ifndef WORLD_BUILDER_H
#define WORLD_BUILDER_H

#include "resource_identifiers.h"

class db_manager;

struct world_builder final
{
    friend class world;

    world_builder &with_fonts(font_manager &fonts) noexcept { build_fonts = &fonts; return *this; }
    world_builder &with_shaders(shader_manager &shaders) noexcept { build_shaders = &shaders; return *this; }
    world_builder &with_textures(texture_manager &textures) noexcept { build_textures = &textures; return *this; }
    world_builder &with_bloom_passes(bloom_pass_manager &bloom_passes) noexcept { build_bloom_passes = &bloom_passes; return *this; }
    world_builder &with_player(player &active_player) noexcept { build_player = &active_player; return *this; }
    world_builder &with_sdl(sdl_gl_helper &sdl) noexcept { build_sdl = &sdl; return *this; }
    world_builder &with_db(db_manager *db) noexcept { build_db = db; return *this; }

private:
    font_manager *build_fonts{nullptr};
    shader_manager *build_shaders{nullptr};
    texture_manager *build_textures{nullptr};
    bloom_pass_manager *build_bloom_passes{nullptr};
    player *build_player{nullptr};
    sdl_gl_helper *build_sdl{nullptr};
    db_manager *build_db{nullptr};
};

#endif // WORLD_BUILDER_H
