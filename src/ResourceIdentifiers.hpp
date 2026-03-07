#ifndef RESOURCE_IDENTIFIERS_HPP
#define RESOURCE_IDENTIFIERS_HPP

#include <string_view>

#include <SFML/Audio/SoundBuffer.hpp>

namespace Fonts
{
    enum class ID : unsigned int
    {
        COUSINE_REGULAR = 0,
        LIMELIGHT = 1,
        NUNITO_SANS = 2
    };
}

namespace GUIOptions
{
    enum class ID : unsigned int
    {
        DE_FACTO = 0,
        ZEN_MODE = 1,
        TIME_TRIAL_MODE = 2,
        TOTAL_OPTIONS = 3
    };
}

namespace Levels
{
    enum class ID : unsigned int
    {
        LEVEL_ONE = 0,
        LEVEL_TWO = 1,
        LEVEL_THREE = 2,
        LEVEL_FOUR = 3,
        LEVEL_FIVE = 4,
        LEVEL_SIX = 5
    };
}

namespace Models
{
    enum class ID : unsigned int
    {
        STYLIZED_CHARACTER = 0,
        TOTAL_MODELS = 1
    };
}

namespace Music
{
    enum class ID : unsigned int
    {
        GAME_MUSIC = 0,
        MENU_MUSIC = 1,
        SPLASH_MUSIC = 2,
    };
}

namespace Shaders
{
    enum class ID : unsigned int
    {
        GLSL_BILLBOARD_SPRITE = 0,
        GLSL_COMPOSITE_SCENE = 1,
        GLSL_FULLSCREEN_QUAD = 2,
        GLSL_FULLSCREEN_QUAD_MVP = 3,
        GLSL_MODEL_WITH_SKINNING = 4,
        GLSL_STENCIL_OUTLINE = 5,
        GLSL_PATH_TRACER_COMPUTE = 6,
        GLSL_PARTICLES_COMPUTE = 7,
        GLSL_SHADOW_VOLUME = 8,
        GLSL_OIT_RESOLVE = 9,
        GLSL_PATH_TRACER_OUTPUT = 10,
        GLSL_PATH_TRACER_TONEMAP = 11,
        GLSL_OUTPUT_TEST = 12,
        GLSL_TONE_TEST = 13,
        GLSL_PREVIEW_TEST = 14,
        GLSL_TILE_TEST = 15,
        GLSL_TOTAL_SHADERS = 16
    };
}

namespace SoundEffect
{
    enum ID : unsigned int
    {
        GENERATE = 0,
        WHITE_NOISE = 1,
        SELECT = 2,
        THROW = 3
    };
}

namespace Textures
{
    enum class ID : unsigned int
    {
        BALL_NORMAL = 0,
        CHARACTER = 1,
        CHARACTER_SPRITE_SHEET = 2,
        LEVEL_ONE = 3,
        LEVEL_TWO = 4,
        LEVEL_THREE = 5,
        LEVEL_FOUR = 6,
        LEVEL_FIVE = 7,
        LEVEL_SIX = 8,
        NOISE2D = 9,
        PATH_TRACER_ACCUM = 10,
        PATH_TRACER_OUTPUT = 11,
        PATH_TRACER_STAGE = 12,
        PATH_TRACER_DISPLAY = 13,
        PATH_TRACER_PREVIEW_ACCUM = 14,
        PATH_TRACER_PREVIEW_OUTPUT = 15,
        SDL_LOGO = 16,
        SFML_LOGO = 17,
        SPLASH_TITLE_IMAGE = 18,
        WALL_HORIZONTAL = 19,
        WINDOW_ICON = 20,
        BILLBOARD_COLOR = 21,
        OIT_ACCUM = 22,
        OIT_REVEAL = 23,
        SHADOW_MAP = 24,
        REFLECTION_COLOR = 25,
        RUNNER_BREAK_PLANE = 26,
        TOTAL_IDS = 27
    };
}

class Font;
class GLTFModel;
class Level;
class MusicPlayer;
class Shader;
class Texture;

struct Options;

// Forward declaration and a few type definitions
template <typename Resource, typename Identifier>
class ResourceManager;

typedef ResourceManager<Font, Fonts::ID> FontManager;
typedef ResourceManager<Options, GUIOptions::ID> OptionsManager;
typedef ResourceManager<Level, Levels::ID> LevelsManager;
typedef ResourceManager<GLTFModel, Models::ID> ModelsManager;
typedef ResourceManager<MusicPlayer, Music::ID> MusicManager;
typedef ResourceManager<Shader, Shaders::ID> ShaderManager;
typedef ResourceManager<sf::SoundBuffer, SoundEffect::ID> SoundBufferManager;
typedef ResourceManager<Texture, Textures::ID> TextureManager;

#endif // RESOURCE_IDENTIFIERS_HPP
