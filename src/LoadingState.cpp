#include "LoadingState.hpp"

#include <SDL3/SDL.h>

#include <filesystem>

#include <MazeBuilder/io_utils.h>

#include "Font.hpp"
#include "JsonUtils.hpp"
#include "MusicPlayer.hpp"
#include "ResourceIdentifiers.hpp"
#include "ResourceManager.hpp"
#include "Shader.hpp"
#include "StateStack.hpp"
#include "Texture.hpp"

#include <fonts/Cousine_Regular.h>
#include <fonts/Limelight_Regular.h>
#include <fonts/nunito_sans.h>

/// @brief
/// @param stack
/// @param context
/// @param resourcePath ""
LoadingState::LoadingState(StateStack& stack, Context context, std::string_view resourcePath)
    : State(stack, context)
    , mForeman{}
    , mHasFinished{ false }
    , mResourcePath{ resourcePath }
{
    mForeman.initThreads();

    // Start loading resources in background if path is provided
    if (!mResourcePath.empty())
    {
        loadResources();
    } else
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "LoadingState: No resource path provided\n");
        mHasFinished = true;
    }
}

void LoadingState::draw() const noexcept
{
    const auto& window = *getContext().window;

    //window.draw(mLoadingSprite);
}

bool LoadingState::update(float dt, unsigned int subSteps) noexcept
{
    if (!mHasFinished && mForeman.isDone())
    {
        // Loading is complete - get the loaded resources
        const auto resources = mForeman.getResources();
        log("Loading complete! Loaded %zu resources. Loading textures... " +  std::to_string(resources.size()));

        // Now actually load the textures from the worker-collected texture requests
        loadTexturesFromWorkerRequests();

        // Handle window icon separately (special case, not managed by TextureManager)
        loadWindowIcon(resources);

        mHasFinished = true;
        log("All textures loaded! Press any key to continue...\n");

        // Note: State transition is handled by SplashState when user presses a key
        // SplashState checks isFinished() before allowing transition to MENU
    }

    if (!mHasFinished)
    {
        setCompletion(mForeman.getCompletion());
    }

    return true;
}

bool LoadingState::handleEvent(const SDL_Event& event) noexcept
{
    return true;
}

void LoadingState::setCompletion(float percent) noexcept
{
    if (percent > 1.f)
    {
        percent = 1.f;
    }

    // Update loading sprite or progress bar based on percent
    log("Loading progress: " + std::to_string(percent * 100.f));
}

bool LoadingState::isFinished() const noexcept { return mHasFinished; }

/// @brief Load resources from the specified path
/// @param resourcePath Path to the JSON resource configuration
void LoadingState::loadResources() noexcept
{
    log("Loading resources from:\t");
    log(mResourcePath.c_str());
    log("\n");

    // This would be called by the application to trigger resource loading
    // The resources would be loaded by the worker threads and stored
    mForeman.generate(mResourcePath);

    loadAudio();

    loadFonts();

    loadShaders();
}

void LoadingState::loadFonts() noexcept
{
    static constexpr auto FONT_PIXEL_SIZE = 28.f;

    auto& fonts = *getContext().fonts;

    fonts.load(Fonts::ID::LIMELIGHT,
        Limelight_Regular_compressed_data,
        Limelight_Regular_compressed_size,
        FONT_PIXEL_SIZE);

    fonts.load(Fonts::ID::NUNITO_SANS,
        NunitoSans_compressed_data,
        NunitoSans_compressed_size,
        FONT_PIXEL_SIZE);

    fonts.load(Fonts::ID::COUSINE_REGULAR,
        Cousine_Regular_compressed_data,
        Cousine_Regular_compressed_size,
        FONT_PIXEL_SIZE);
}

void LoadingState::loadAudio() noexcept
{
    auto& music = *getContext().music;

    try
    {
        // Load music tracks through the MusicManager
        // Adjust paths as needed for your audio files
        music.load(Music::ID::GAME_MUSIC, std::string_view("./audio/loading.wav"), 50.f, true);
        //music.load(Music::ID::MENU_MUSIC, std::string_view("./audio/menu_music.ogg"), 50.f, true);
        //music.load(Music::ID::SPLASH_MUSIC, std::string_view("./audio/splash_music.ogg"), 50.f, false);

        log("LoadingState: Music loaded successfully");
    } catch (const std::exception& e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "LoadingState: Failed to load music: %s", e.what());
    }

    auto& soundBuffers = *getContext().soundBuffers;

    try
    {
        // Load sound effects through the SoundBufferManager
        // Adjust paths as needed for your audio files
        soundBuffers.load(SoundEffect::ID::GENERATE, "./audio/generate.ogg");
        soundBuffers.load(SoundEffect::ID::SELECT, "./audio/sfx_select.ogg");
        soundBuffers.load(SoundEffect::ID::THROW, "./audio/sfx_throw.ogg");

        log("LoadingState: Sound effects loaded successfully");
    } catch (const std::exception& e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "LoadingState: Failed to load sound effects: %s", e.what());
    }
}

void LoadingState::loadShaders() noexcept
{
    auto& shaders = *getContext().shaders;

    try
    {
        // Load display shader (vertex + fragment)
        auto displayShader = std::make_unique<Shader>();
        displayShader->compileAndAttachShader(ShaderType::VERTEX, "./shaders/raytracer.vert.glsl");
        displayShader->compileAndAttachShader(ShaderType::FRAGMENT, "./shaders/raytracer.frag.glsl");
        displayShader->linkProgram();

        log("LoadingState: Display shader compiled and linked");
        log(displayShader->getGlslUniforms().c_str());
        log("\n");
        log(displayShader->getGlslAttribs().c_str());
        log("\n");

        // Insert display shader into manager (using vertex ID as the combined shader program ID)
        shaders.insert(Shaders::ID::DISPLAY_QUAD_VERTEX, std::move(displayShader));

        // Load compute shader for path tracing
        auto computeShader = std::make_unique<Shader>();
        computeShader->compileAndAttachShader(ShaderType::COMPUTE, "./shaders/pathtracer.cs.glsl");
        computeShader->linkProgram();

        log("LoadingState: Compute shader compiled and linked");
        log(computeShader->getGlslUniforms().c_str());
        log("\n");

        // Insert compute shader into manager
        shaders.insert(Shaders::ID::COMPUTE_PATH_TRACER_COMPUTE, std::move(computeShader));

        log("LoadingState: All shaders loaded successfully");
    } catch (const std::exception& e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "LoadingState: Shader initialization failed: %s", e.what());
    }
}

void LoadingState::loadTexturesFromWorkerRequests() const noexcept
{
    auto& textures = *getContext().textures;

    // Get the texture load requests that were collected by worker threads
    auto textureRequests = mForeman.getTextureLoadRequests();

    SDL_Log("Loading %zu textures on main thread...\n", textureRequests.size());

    // Get the resource directory prefix (same as used for window icon)
    auto resourcePathPrefix = mazes::io_utils::getDirectoryPath(mResourcePath);

    try
    {
        for (const auto& request : textureRequests)
        {
            // Construct full path: if request.path is relative, prepend resource directory
            std::string fullPath = request.path;

            // If path starts with '/' or is not absolute, treat as relative to resource directory
            if (!fullPath.empty() && fullPath[0] == '/')
            {
                // Remove leading slash and prepend resource directory
                fullPath = resourcePathPrefix + fullPath.substr(1);
            } else if (!std::filesystem::path(fullPath).is_absolute())
            {
                // Path is relative, prepend resource directory
                fullPath = resourcePathPrefix + fullPath;
            }

            // Use string_view to avoid ambiguity with the overloaded load methods
            textures.load(request.id, std::string_view(fullPath), 0u);
            SDL_Log("DEBUG: Loaded texture ID %d from: %s\n", static_cast<int>(request.id), fullPath.c_str());
        }
    } catch (const std::exception& e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load textures: %s\n", e.what());
    }
}

void LoadingState::loadWindowIcon(const std::unordered_map<std::string, std::string>& resources) noexcept
{
    using std::string;

    JSONUtils jsonUtils{};
    auto resourcePathPrefix = mazes::io_utils::getDirectoryPath(mResourcePath);

    // Window icon is special case, no need to save the texture in the manager
    if (auto windowIconKey = resources.find(string{ JSONKeys::WINDOW_ICON }); windowIconKey != resources.cend())
    {
        string windowIconPath = resourcePathPrefix + JSONUtils::extractJsonValue(windowIconKey->second);

        if (SDL_Surface* icon = SDL_LoadBMP(windowIconPath.c_str()); icon != nullptr)
        {
            if (auto* renderWindow = getContext().window; renderWindow != nullptr)
            {
                SDL_SetWindowIcon(renderWindow->getSDLWindow(), icon);
                SDL_DestroySurface(icon);
                log("Loading window icon from:\t");
                log(windowIconPath.c_str());
            }
        } else
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load icon: %s - %s\n", windowIconPath.c_str(),
                SDL_GetError());
        }
    }
}
