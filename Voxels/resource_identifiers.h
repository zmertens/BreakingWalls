#ifndef VOXELS_RESOURCE_IDENTIFIERS_H
#define VOXELS_RESOURCE_IDENTIFIERS_H

#include <array>
#include <cstddef>

enum class ShaderIdentifier : unsigned int
{
    BLOCK_SHADER          = 0,
    LINE_SHADER           = 1,
    SKY_SHADER            = 2,
    TEXT_SHADER           = 3,
    BLOOM_BLUR_SHADER     = 4,
    BLOOM_COMPOSITE_SHADER = 5,
    TRANSITION_BLUR_SHADER = 6,
    TRANSITION_PIXELATE_SHADER = 7,
    TRANSITION_WAVE_SHADER = 8,
    TOTAL                 = 9
};

// Full-screen level-transition effects; each maps to a ShaderIdentifier.
// Performance: they run only for the ~1.2s after a new level is generated, as one extra
// full-screen pass over a lazily allocated RGBA8 target. Outside that window the
// bloom composite goes straight to the screen with no extra cost or memory.
enum class TransitionEffect : unsigned int
{
    BLUR,
    PIXELATE,
    WAVE,
    COUNT
};

inline constexpr std::array<TransitionEffect, static_cast<std::size_t>(TransitionEffect::COUNT)> TRANSITION_EFFECTS{
    TransitionEffect::BLUR, TransitionEffect::PIXELATE, TransitionEffect::WAVE};

enum class TextureIdentifier : unsigned int
{
    ATLAS = 0,
    SIGNS = 1,
    SKY = 2,
    BITMAP_FONT = 3,
    WINDOW_ICON = 4,
    MAZE = 5,
    TOTAL = 6
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
    GAME = 1,
    TOTAL = 2
};

enum class SoundIdentifier : unsigned int
{
    BOINK = 0,
    GAME_THEME_SONG_1 = 1,
    LOADING_THEME_SONG = 2,
    DEMO_THEME_SONG = 3,
    TOTAL = 4
};

class bloom_pass;
class font;
class shader;
class texture;
class soundfx;

// Forward declaration and a few type definitions
template <typename Resource, typename Identifier>
class resource_manager;

typedef resource_manager<font, FontIdentifier> font_manager;
typedef resource_manager<shader, ShaderIdentifier> shader_manager;
typedef resource_manager<texture, TextureIdentifier> texture_manager;
typedef resource_manager<bloom_pass, BloomPassIdentifier> bloom_pass_manager;
typedef resource_manager<soundfx, SoundIdentifier> soundfx_manager;

#endif // VOXELS_RESOURCE_IDENTIFIERS_H
