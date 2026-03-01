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
        GLSL_PATH_TRACER_COMPUTE = 5,
        GLSL_PARTICLES_COMPUTE = 6,
        GLSL_SHADOW_VOLUME = 7,
        GLSL_OIT_RESOLVE = 8,
        GLSL_TOTAL_SHADERS = 9
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
        PATH_TRACER_DISPLAY = 11,
        SDL_LOGO = 12,
        SFML_LOGO = 13,
        SPLASH_TITLE_IMAGE = 14,
        WALL_HORIZONTAL = 15,
        WINDOW_ICON = 16,
        TOTAL_IDS = 17
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
