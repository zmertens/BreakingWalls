#include "LoadingState.hpp"

#include <SDL3/SDL.h>
#include <filesystem>

#include <MazeBuilder/io_utils.h>
#include <MazeBuilder/create2.h>
#include <MazeBuilder/configurator.h>

#include "Font.hpp"
#include "JsonUtils.hpp"
#include "ResourceIdentifiers.hpp"
#include "ResourceManager.hpp"
#include "Shader.hpp"
#include "StateStack.hpp"
#include "Texture.hpp"

#include <fonts/Cousine_Regular.h>
#include <fonts/Limelight_Regular.h>
#include <fonts/nunito_sans.h>

#include "Audio.hpp"

/// @brief
/// @param stack
/// @param context
/// @param resourcePath ""
LoadingState::LoadingState(StateStack& stack, Context context, std::string_view resourcePath)
    : State(stack, context)
      , mForeman{}
      , mHasFinished{false}
      , mResourcePath{resourcePath}
{
    mForeman.initThreads();

    // Start loading resources in background if path is provided
    if (!mResourcePath.empty())
    {
        loadResources();
    }
    else
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
        SDL_Log("Loading complete! Loaded %zu resources. Loading textures...\n", resources.size());

        // Now actually load the textures from the worker-collected texture requests
        loadTexturesFromWorkerRequests();

        // Handle window icon separately (special case, not managed by TextureManager)
        loadWindowIcon(resources);

        mHasFinished = true;
        SDL_Log("All textures loaded! Press any key to continue...\n");
        
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
    SDL_Log("Loading progress: %.2f%%", percent * 100.f);
}

bool LoadingState::isFinished() const noexcept { return mHasFinished; }

/// @brief Load resources from the specified path
/// @param resourcePath Path to the JSON resource configuration
void LoadingState::loadResources() noexcept
{
    SDL_Log("LoadingState::loadResources - Loading from: %s\n", mResourcePath.data());

    // This would be called by the application to trigger resource loading
    // The resources would be loaded by the worker threads and stored
    mForeman.generate(mResourcePath);

    loadFonts();

    loadShaders();

    loadAudio();
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

        SDL_Log("LoadingState: Display shader compiled and linked");
        SDL_Log("%s", displayShader->getGlslUniforms().c_str());
        SDL_Log("%s", displayShader->getGlslAttribs().c_str());

        // Insert display shader into manager (using vertex ID as the combined shader program ID)
        shaders.insert(Shaders::ID::DISPLAY_QUAD_VERTEX, std::move(displayShader));

        // Load compute shader for path tracing
        auto computeShader = std::make_unique<Shader>();
        computeShader->compileAndAttachShader(ShaderType::COMPUTE, "./shaders/pathtracer.cs.glsl");
        computeShader->linkProgram();

        SDL_Log("LoadingState: Compute shader compiled and linked");
        SDL_Log("%s", computeShader->getGlslUniforms().c_str());

        // Insert compute shader into manager
        shaders.insert(Shaders::ID::COMPUTE_PATH_TRACER_COMPUTE, std::move(computeShader));

        SDL_Log("LoadingState: All shaders loaded successfully");
    }
    catch (const std::exception& e)
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
            }
            else if (!std::filesystem::path(fullPath).is_absolute())
            {
                // Path is relative, prepend resource directory
                fullPath = resourcePathPrefix + fullPath;
            }
            
            // Use string_view to avoid ambiguity with the overloaded load methods
            textures.load(request.id, std::string_view(fullPath), 0u);
            SDL_Log("DEBUG: Loaded texture ID %d from: %s\n", static_cast<int>(request.id), fullPath.c_str());
        }
    }
    catch (const std::exception& e)
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
    if (auto windowIconKey = resources.find(string{JSONKeys::WINDOW_ICON}); windowIconKey != resources.cend())
    {
        string windowIconPath = resourcePathPrefix + JSONUtils::extractJsonValue(windowIconKey->second);

        if (SDL_Surface* icon = SDL_LoadBMP(windowIconPath.c_str()); icon != nullptr)
        {
            if (auto* renderWindow = getContext().window; renderWindow != nullptr)
            {
                SDL_SetWindowIcon(renderWindow->getSDLWindow(), icon);
                SDL_DestroySurface(icon);
                SDL_Log("DEBUG: Loading window icon from: %s\n", windowIconPath.c_str());
            }
        }
        else
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load icon: %s - %s\n", windowIconPath.c_str(),
                         SDL_GetError());
        }
    }
}

void LoadingState::loadAudio() noexcept
{
    auto& audioManager = *getContext().audio;
    const auto resources = mForeman.getResources();
    auto resourcePathPrefix = mazes::io_utils::getDirectoryPath(mResourcePath);

    try
    {
        // Helper lambda to parse JSON array string into vector of strings
        auto parseJsonArray = [](const std::string& jsonStr) -> std::vector<std::string> {
            std::vector<std::string> result;
            if (jsonStr.empty()) return result;

            std::string str = jsonStr;
            // Remove outer brackets
            if (str.front() == '[') str = str.substr(1);
            if (str.back() == ']') str = str.substr(0, str.length() - 1);

            // Split by comma and extract quoted strings
            size_t pos = 0;
            while (pos < str.length()) {
                // Find next opening quote
                size_t quoteStart = str.find('"', pos);
                if (quoteStart == std::string::npos) break;

                // Find closing quote
                size_t quoteEnd = str.find('"', quoteStart + 1);
                if (quoteEnd == std::string::npos) break;

                // Extract the string between quotes
                std::string value = str.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                result.push_back(value);

                pos = quoteEnd + 1;
            }
            return result;
        };

        // Load OGG files
        if (auto oggFilesKey = resources.find(std::string{JSONKeys::OGG_FILES}); 
            oggFilesKey != resources.cend())
        {
            auto oggFiles = parseJsonArray(oggFilesKey->second);
            
            SDL_Log("Loading %zu OGG audio files...\n", oggFiles.size());
            
            for (const auto& oggFile : oggFiles)
            {
                std::string audioPath = resourcePathPrefix + oggFile;
                Audio::ID audioId;
                
                // Map filenames to Audio IDs
                if (oggFile.find("generate.ogg") != std::string::npos)
                {
                    audioId = Audio::ID::GENERATE;
                }
                else if (oggFile.find("sfx_select.ogg") != std::string::npos)
                {
                    audioId = Audio::ID::SFX_SELECT;
                }
                else if (oggFile.find("sfx_throw.ogg") != std::string::npos)
                {
                    audioId = Audio::ID::SFX_THROW;
                }
                else
                {
                    SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "Unknown OGG file: %s, skipping", 
                               oggFile.c_str());
                    continue;
                }
                
                audioManager.load(nullptr, audioId, audioPath);
                SDL_Log("Loaded audio: %s", audioPath.c_str());
            }
        }

        // Load WAV files
        if (auto wavFilesKey = resources.find(std::string{JSONKeys::WAV_FILES}); 
            wavFilesKey != resources.cend())
        {
            auto wavFiles = parseJsonArray(wavFilesKey->second);
            
            SDL_Log("Loading %zu WAV audio files...\n", wavFiles.size());
            
            for (const auto& wavFile : wavFiles)
            {
                std::string audioPath = resourcePathPrefix + wavFile;
                Audio::ID audioId;
                
                // Map filenames to Audio IDs
                if (wavFile.find("loading.wav") != std::string::npos)
                {
                    audioId = Audio::ID::LOADING;
                }
                else
                {
                    SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "Unknown WAV file: %s, skipping", 
                               wavFile.c_str());
                    continue;
                }
                
                audioManager.load(nullptr, audioId, audioPath);
                SDL_Log("Loaded audio: %s", audioPath.c_str());
            }
        }

        SDL_Log("All audio files loaded successfully\n");
    }
    catch (const std::exception& e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "LoadingState: Audio loading failed: %s", e.what());
    }
}
