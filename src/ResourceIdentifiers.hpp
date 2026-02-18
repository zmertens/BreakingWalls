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
        GLSL_FULLSCREEN_QUAD = 1,
        GLSL_PATH_TRACER_COMPUTE = 2,
        GLSL_TOTAL_SHADERS = 3
    };
}

namespace SoundEffect
{
    enum ID : unsigned int
    {
        GENERATE = 0,
        SELECT = 1,
        THROW = 2
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
class Level;
class MusicPlayer;
class Options;
class Shader;
class Texture;

// Forward declaration and a few type definitions
template <typename Resource, typename Identifier>
class ResourceManager;

typedef ResourceManager<Font, Fonts::ID> FontManager;
typedef ResourceManager<Options, GUIOptions::ID> OptionsManager;
typedef ResourceManager<Level, Levels::ID> LevelsManager;
typedef ResourceManager<MusicPlayer, Music::ID> MusicManager;
typedef ResourceManager<Shader, Shaders::ID> ShaderManager;
typedef ResourceManager<sf::SoundBuffer, SoundEffect::ID> SoundBufferManager;
typedef ResourceManager<Texture, Textures::ID> TextureManager;

#endif // RESOURCE_IDENTIFIERS_HPP
