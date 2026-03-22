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
        GLSL_GOAL_PATH_STENCIL = 4,
        GLSL_HIGHLIGHT_TILE = 5,
        GLSL_MAZE = 6,
        GLSL_MOTION_BLUR = 7,
        GLSL_PARTICLES_COMPUTE = 8,
        GLSL_SHADOW_VOLUME = 9,
        GLSL_SKINNED_MODEL = 10,
        GLSL_SKY = 11,
        GLSL_OIT_RESOLVE = 12,
        GLSL_TOTAL_SHADERS = 13
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

namespace VAOs
{
    enum class ID : unsigned int
    {
        FULLSCREEN_QUAD = 0,
        SHADOW_QUAD = 1,
        WALK_PARTICLES = 2,
        RASTER_MAZE = 3,
        GOAL_PATH = 4,
        PLAYER_TILE_HIGHLIGHT = 5,
        PICKUP_SPHERES = 6,
        TOTAL_IDS = 7
    };
}

namespace FBOs
{
    enum class ID : unsigned int
    {
        BILLBOARD = 0,
        OIT = 1,
        SHADOW = 2,
        REFLECTION = 3,
        MOTION_BLUR = 4,
        TOTAL_IDS = 5
    };
}

namespace VBOs
{
    enum class ID : unsigned int
    {
        SHADOW = 0,
        WALK_PARTICLES_POS_SSBO = 1,
        WALK_PARTICLES_VEL_SSBO = 2,
        RASTER_MAZE = 3,
        GOAL_PATH = 4,
        PICKUP = 5,
        TOTAL_IDS = 6
    };
}

namespace Textures
{
    enum class ID : unsigned int
    {
        BALL_NORMAL = 0,
        CHARACTER = 1,
        CHARACTER_SPRITE_SHEET = 2,
        FAA_LOGO = 3,
        LEVEL_ONE = 4,
        LEVEL_TWO = 5,
        LEVEL_THREE = 6,
        LEVEL_FOUR = 7,
        LEVEL_FIVE = 8,
        LEVEL_SIX = 9,
        NOISE2D = 10,
        SDL_LOGO = 11,
        SFML_LOGO = 12,
        SPLASH_TITLE_IMAGE = 13,
        WALL_HORIZONTAL = 14,
        WINDOW_ICON = 15,
        BILLBOARD_COLOR = 16,
        OIT_ACCUM = 17,
        OIT_REVEAL = 18,
        SHADOW_MAP = 19,
        REFLECTION_COLOR = 20,
        RUNNER_BREAK_PLANE = 21,
        MOTION_BLUR_COLOR = 22,
        PREV_FRAME = 23,
        TOTAL_IDS = 24
    };
}

class Font;
class FramebufferObject;
class GLTFModel;
class Level;
class MusicPlayer;
class Shader;
class Texture;
class VertexArrayObject;
class VertexBufferObject;

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
typedef ResourceManager<VertexArrayObject, VAOs::ID> VAOManager;
typedef ResourceManager<FramebufferObject, FBOs::ID> FBOManager;
typedef ResourceManager<VertexBufferObject, VBOs::ID> VBOManager;

#endif // RESOURCE_IDENTIFIERS_HPP
