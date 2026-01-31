#include "LoadingState.hpp"

#include <SDL3/SDL.h>
#include <filesystem>

#include <MazeBuilder/io_utils.h>
#include <MazeBuilder/create2.h>
#include <MazeBuilder/configurator.h>

#include "JsonUtils.hpp"
#include "ResourceIdentifiers.hpp"
#include "ResourceManager.hpp"
#include "StateStack.hpp"
#include "Texture.hpp"

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
    SDL_Log("LoadingState constructor - resource path: '%s'", resourcePath.empty() ? "(empty)" : resourcePath.data());
    
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
    
    SDL_Log("LoadingState constructor complete");
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
            
            textures.load(request.id, fullPath);
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
