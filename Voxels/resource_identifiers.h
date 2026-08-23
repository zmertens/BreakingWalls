#ifndef VOXELS_RESOURCE_IDENTIFIERS_H
#define VOXELS_RESOURCE_IDENTIFIERS_H

enum class ShaderIdentifier : unsigned int
{
    BLOCK_SHADER          = 0,
    LINE_SHADER           = 1,
    SKY_SHADER            = 2,
    TEXT_SHADER           = 3,
    BLOOM_BLUR_SHADER     = 4,
    BLOOM_COMPOSITE_SHADER = 5,
    TOTAL                 = 6
};

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

class font;
class shader;
class texture;

// Forward declaration and a few type definitions
template <typename Resource, typename Identifier>
class resource_manager;

typedef resource_manager<font, FontIdentifier> font_manager;
typedef resource_manager<shader, ShaderIdentifier> shader_manager;
typedef resource_manager<texture, TextureIdentifier> texture_manager;

#endif // VOXELS_RESOURCE_IDENTIFIERS_H
