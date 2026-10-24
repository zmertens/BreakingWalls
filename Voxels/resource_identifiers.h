#ifndef VOXELS_RESOURCE_IDENTIFIERS_H
#define VOXELS_RESOURCE_IDENTIFIERS_H

#include <array>
#include <cstddef>

enum class ArtifactIdentifier : unsigned int
{
    WORLD_OBJ_1 = 0,
    WORLD_IMG_1 = 1,
    TOTAL = 2
};

enum class ShaderIdentifier : unsigned int
{
    BLOCK_SHADER = 0,
    LINE_SHADER = 1,
    SKY_SHADER = 2,
    TEXT_SHADER = 3,
    BLOOM_BLUR_SHADER = 4,
    BLOOM_COMPOSITE_SHADER = 5,
    TRANSITION_BLUR_SHADER = 6,
    TRANSITION_PIXELATE_SHADER = 7,
    BLOOM_DEPTH_FIELD_SHADER = 8,
    BLOOM_GLASS_RESOLVE_SHADER = 9,
    TRANSITION_WAVE_SHADER = 10,
    TOTAL = 11
};

enum class ShaderEffect : unsigned int
{
    BLUR,
    PIXELATE,
    WAVE,
    COUNT
};

enum class TextureIdentifier : unsigned int
{
    ATLAS = 0,
    SIGNS = 1,
    SKY = 2,
    BITMAP_FONT = 3,
    WINDOW_ICON = 4,
    MAZE = 5,
    FLIPS_AND_ALE_ICON = 6,
    TOTAL = 7
};

enum class FontIdentifier : unsigned int
{
    COUSINE_REGULAR = 0,
    KARLA_REGULAR = 1,
    LIMELIGHT = 2,
    NUNITO_SANS = 3,
    PROGGY_CLEAN = 4,
    ROBOTO_MEDIUM = 5,
    TOTAL = 6
};

enum class BloomPassIdentifier : unsigned int
{
    EDITOR = 0,
    TOTAL = 1
};

enum class SoundIdentifier : unsigned int
{
    BOINK = 0,
    GAME_THEME_SONG_1 = 1,
    LOADING_THEME_SONG = 2,
    DEMO_THEME_SONG = 3,
    TOTAL = 4
};

class artifact_exporter;
class bloom_pass;
class font;
class shader;
class texture;
class soundfx;

// Forward declaration and a few type definitions
template <typename Resource, typename Identifier>
class resource_manager;

typedef resource_manager<artifact_exporter, ArtifactIdentifier> artifact_exporter_manager;
typedef resource_manager<font, FontIdentifier> font_manager;
typedef resource_manager<shader, ShaderIdentifier> shader_manager;
typedef resource_manager<texture, TextureIdentifier> texture_manager;
typedef resource_manager<bloom_pass, BloomPassIdentifier> bloom_pass_manager;
typedef resource_manager<soundfx, SoundIdentifier> soundfx_manager;

#endif // VOXELS_RESOURCE_IDENTIFIERS_H
