#include "LoadingState.hpp"

#include <SDL3/SDL.h>

#include <filesystem>
#include <thread>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <optional>
#include <algorithm>
#include <cctype>

#include <MazeBuilder/io_utils.h>
#include <MazeBuilder/configurator.h>
#include <MazeBuilder/create.h>
#include <MazeBuilder/json_helper.h>

#include "Font.hpp"
#include "JsonUtils.hpp"
#include "Level.hpp"
#include "MusicPlayer.hpp"
#include "ResourceIdentifiers.hpp"
#include "ResourceManager.hpp"
#include "Shader.hpp"
#include "StateStack.hpp"
#include "Texture.hpp"

#include <fonts/Cousine_Regular.h>
#include <fonts/Limelight_Regular.h>
#include <fonts/nunito_sans.h>

namespace
{
    /// @brief Represents a single resource loading work item
    struct ResourceWorkItem
    {
        std::string key;
        std::string value;
        int index;

        ResourceWorkItem(std::string k, std::string v, int idx)
            : key(std::move(k)), value(std::move(v)), index(idx)
        {
        }
    };

    /// @brief Represents a texture that needs to be loaded on the main thread
    struct TextureLoadRequest
    {
        Textures::ID id;
        std::string path;

        TextureLoadRequest(Textures::ID textureId, std::string texturePath)
            : id(textureId), path(std::move(texturePath))
        {
        }
    };
}

/// @brief Concurrent resource loader for handling file I/O and configuration parsing in background threads
class ResourceLoader
{
public:
    /// @brief Represents a texture that needs to be loaded on the main thread
    struct TextureLoadRequest
    {
        Textures::ID id;
        std::string path;

        TextureLoadRequest(Textures::ID textureId, std::string texturePath)
            : id(textureId), path(std::move(texturePath))
        {
        }
    };

    explicit ResourceLoader()
        : mShouldExit(false)
        , mPendingWorkCount(0)
        , mTotalWorkItems(0)
        , mConfigMappings({
              {JSONKeys::BALL_NORMAL, Textures::ID::BALL_NORMAL},
              {JSONKeys::CHARACTER_IMAGE, Textures::ID::CHARACTER},
              {JSONKeys::LEVEL_DEFAULTS, Textures::ID::LEVEL_TWO},
              {JSONKeys::CHARACTERS_SPRITE_SHEET, Textures::ID::CHARACTER_SPRITE_SHEET},
              {JSONKeys::SPLASH_IMAGE, Textures::ID::SPLASH_TITLE_IMAGE},
              {JSONKeys::SDL_LOGO, Textures::ID::SDL_LOGO},
              {JSONKeys::SFML_LOGO, Textures::ID::SFML_LOGO},
              {JSONKeys::WALL_HORIZONTAL, Textures::ID::WALL_HORIZONTAL},
              {JSONKeys::WINDOW_ICON, Textures::ID::WINDOW_ICON}
          })
    {
    }

    ~ResourceLoader()
    {
        shutdown();
    }

    ResourceLoader(const ResourceLoader&) = delete;
    ResourceLoader& operator=(const ResourceLoader&) = delete;
    ResourceLoader(ResourceLoader&&) = delete;
    ResourceLoader& operator=(ResourceLoader&&) = delete;

    /// @brief Initialize worker threads for concurrent resource loading
    void initThreads(unsigned int numWorkers = 4) noexcept
    {
        if (mWorkerThreads.size() > 0)
        {
            return;
        }

        for (unsigned int i = 0; i < numWorkers; ++i)
        {
            mWorkerThreads.emplace_back([this]() { workerThreadFunc(); });
        }
    }

    /// @brief Queue resources for concurrent loading from the given path
    void load(std::string_view resourcePath) noexcept
    {
        if (resourcePath.empty())
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Resource path is empty\n");
            return;
        }

        {
            std::unique_lock<std::mutex> lock(mQueueMutex);
            mWorkQueue.clear();
            mResources.clear();
            mProcessedConfigs.clear();
            mTextureLoadRequests.clear();
            mComposedMazeStrings.clear();
        }

        mResourcePathPrefix = mazes::io_utils::getDirectoryPath(std::string(resourcePath)) + "/";

        std::unordered_map<std::string, std::string> resources{};

        try
        {
            JSONUtils::loadConfiguration(std::string(resourcePath), resources);
        } catch (const std::exception& e)
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "ResourceLoader: Failed to load resources: %s\n", e.what());
            return;
        }

        if (resources.empty())
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "ResourceLoader: No resources found in %s\n", resourcePath.data());
            return;
        }

        {
            std::unique_lock<std::mutex> lock(mQueueMutex);

            int index = 0;
            for (const auto& [key, value] : resources)
            {
                mWorkQueue.emplace_back(key, value, index++);
            }

            mTotalWorkItems = static_cast<int>(mWorkQueue.size());
            mPendingWorkCount = mTotalWorkItems;

            mCondVar.notify_all();
        }
    }

    /// @brief Check if all work items have been processed
    bool isDone() const noexcept
    {
        std::unique_lock<std::mutex> lock(mQueueMutex);
        return mPendingWorkCount <= 0;
    }

    /// @brief Get completion as a percentage (0.0 to 1.0)
    float getCompletion() const noexcept
    {
        std::unique_lock<std::mutex> lock(mQueueMutex);
        if (mTotalWorkItems <= 0)
        {
            return 0.0f;
        }

        const int completed = mTotalWorkItems - mPendingWorkCount;
        return static_cast<float>(completed) / static_cast<float>(mTotalWorkItems);
    }

    /// @brief Get all loaded resources (thread-safe copy)
    std::unordered_map<std::string, std::string> getResources() const noexcept
    {
        std::unique_lock<std::mutex> lock(mQueueMutex);
        return mResources;
    }

    /// @brief Get all texture load requests (thread-safe copy)
    std::vector<TextureLoadRequest> getTextureLoadRequests() const noexcept
    {
        std::unique_lock<std::mutex> lock(mQueueMutex);
        return mTextureLoadRequests;
    }

    /// @brief Get all composed maze strings (thread-safe copy)
    std::unordered_map<Textures::ID, std::string> getComposedMazeStrings() const noexcept
    {
        std::unique_lock<std::mutex> lock(mQueueMutex);
        return mComposedMazeStrings;
    }

private:
    /// @brief Worker thread function that processes work items from the queue
    void workerThreadFunc() noexcept
    {
        while (!mShouldExit.load())
        {
            std::unique_lock<std::mutex> lock(mQueueMutex);

            mCondVar.wait(lock, [this]() { return !mWorkQueue.empty() || mShouldExit.load(); });

            if (mShouldExit.load())
            {
                break;
            }

            std::optional<ResourceWorkItem> workItem;

            if (!mWorkQueue.empty())
            {
                workItem = mWorkQueue.front();
                mWorkQueue.pop_front();
            }

            lock.unlock();

            if (workItem.has_value())
            {
                processWorkItem(workItem.value());

                lock.lock();
                --mPendingWorkCount;

                if (mPendingWorkCount <= 0)
                {
                    mCondVar.notify_all();
                }
                lock.unlock();
            }
        }
    }

    /// @brief Process a single work item (resource loading and parsing)
    void processWorkItem(const ResourceWorkItem& item) noexcept
    {
        if (mShouldExit.load())
        {
            return;
        }

        {
            std::unique_lock<std::mutex> lock(mQueueMutex);

            if (mShouldExit.load())
            {
                return;
            }

            mResources[item.key] = item.value;

            if (mProcessedConfigs.contains(item.key))
            {
                return;
            }
        }

        if (item.key == JSONKeys::LEVEL_DEFAULTS)
        {
            processLevelDefaults(item.value);
        } else if (item.key == JSONKeys::PLAYER_HITPOINTS_DEFAULT ||
                   item.key == JSONKeys::PLAYER_SPEED_DEFAULT ||
                   item.key == JSONKeys::ENEMY_HITPOINTS_DEFAULT ||
                   item.key == JSONKeys::ENEMY_SPEED_DEFAULT)
        {
            // Numeric configs handled here in the future
        } else
        {
            processTextureRequest(item.key, item.value);
        }

        std::unique_lock<std::mutex> lock(mQueueMutex);
        mProcessedConfigs[item.key] = true;
    }

    /// @brief Process level configuration and generate mazes
    void processLevelDefaults(const std::string& value) noexcept
    {
        std::vector<std::unordered_map<std::string, std::string>> levelConfigs;
        mazes::json_helper jh{};

        if (!jh.from_array(value, levelConfigs))
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "ResourceLoader: Failed to parse level_defaults array\n");
            return;
        }

        std::string composedMazeString;

        for (size_t i = 0; i < levelConfigs.size(); ++i)
        {
            try
            {
                mazes::configurator config;

                for (const auto& [key, val] : levelConfigs[i])
                {
                    if (key == "rows" || key == "columns" || key == "seed")
                    {
                        try
                        {
                            int intValue = std::stoi(JSONUtils::extractJsonValue(val));
                            if (key == "rows")
                                config.rows(static_cast<unsigned int>(intValue));
                            else if (key == "columns")
                                config.columns(static_cast<unsigned int>(intValue));
                            else if (key == "seed")
                                config.seed(static_cast<unsigned int>(intValue));
                        } catch (...)
                        {
                            SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                                "ResourceLoader: Failed to parse %s from level config %zu\n", key.c_str(), i);
                        }
                    }
                    else if (key == "algo")
                    {
                        std::string algoStr = JSONUtils::extractJsonValue(val);
                        config.algo_id(mazes::to_algo_from_sv(algoStr));
                    }
                    else if (key == "distances")
                    {
                        std::string distValue = val;
                        distValue.erase(std::remove_if(distValue.begin(), distValue.end(),
                            [](char c) { return c == '"' || c == '\'' || std::isspace(static_cast<unsigned char>(c)); }),
                            distValue.end());

                        bool showDistances = (distValue == "true" || distValue == "1");
                        if (showDistances)
                        {
                            config.distances("[0:-1]");
                        }
                    }
                }

                std::string mazeStr = mazes::create(config);

                if (!mazeStr.empty())
                {
                    if (!composedMazeString.empty())
                    {
                        composedMazeString += "\n\n";
                    }
                    composedMazeString += mazeStr;
                }
            } catch (const std::exception& e)
            {
                SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                    "ResourceLoader: Failed to create maze from config %zu: %s\n", i, e.what());
            }
        }

        if (!composedMazeString.empty())
        {
            std::unique_lock<std::mutex> lock(mQueueMutex);
            mComposedMazeStrings[Textures::ID::LEVEL_TWO] = composedMazeString;
        }
    }

    /// @brief Process texture load requests
    void processTextureRequest(const std::string& key, const std::string& value) noexcept
    {
        for (const auto& [cfgKey, textureId] : mConfigMappings)
        {
            if (key == cfgKey)
            {
                std::unique_lock<std::mutex> lock(mQueueMutex);
                mTextureLoadRequests.emplace_back(textureId,
                    mResourcePathPrefix + JSONUtils::extractJsonValue(value));
                break;
            }
        }
    }

    /// @brief Shut down all worker threads
    void shutdown() noexcept
    {
        {
            std::unique_lock<std::mutex> lock(mQueueMutex);
            mShouldExit.store(true);
            mCondVar.notify_all();
        }

        for (auto& thread : mWorkerThreads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }

        mWorkerThreads.clear();
    }

    mutable std::mutex mQueueMutex;
    std::condition_variable mCondVar;
    std::deque<ResourceWorkItem> mWorkQueue;
    std::vector<std::thread> mWorkerThreads;
    std::atomic<bool> mShouldExit;
    int mPendingWorkCount;
    int mTotalWorkItems;

    std::unordered_map<std::string, std::string> mResources;
    std::unordered_map<std::string, bool> mProcessedConfigs;
    std::vector<TextureLoadRequest> mTextureLoadRequests;
    std::unordered_map<Textures::ID, std::string> mComposedMazeStrings;

    std::string mResourcePathPrefix;

    const std::vector<std::pair<std::string_view, Textures::ID>> mConfigMappings;
};

///
/// @param stack
/// @param context
/// @param resourcePath ""
LoadingState::LoadingState(StateStack& stack, Context context, std::string_view resourcePath)
    : State(stack, context)
    , mResourceLoader(nullptr)
    , mHasFinished{ false }
    , mResourcePath{ resourcePath }
{
    // Allocate the ResourceLoader on the heap (will be deleted in destructor or moved to unique_ptr)
    mResourceLoader = new ResourceLoader();
    mResourceLoader->initThreads();

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

// Destructor
LoadingState::~LoadingState()
{
    if (mResourceLoader)
    {
        delete mResourceLoader;
        mResourceLoader = nullptr;
    }
}

void LoadingState::draw() const noexcept
{
    const auto& window = *getContext().window;

    //window.draw(mLoadingSprite);
}

bool LoadingState::update(float dt, unsigned int subSteps) noexcept
{
    if (mResourceLoader && !mHasFinished && mResourceLoader->isDone())
    {
        // Loading is complete - get the loaded resources
        const auto resources = mResourceLoader->getResources();
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

    if (mResourceLoader && !mHasFinished)
    {
        setCompletion(mResourceLoader->getCompletion());
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
    if (mResourceLoader)
    {
        mResourceLoader->load(mResourcePath);
    }

    loadAudio();

    loadFonts();

    loadLevels();

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
        music.load(Music::ID::GAME_MUSIC, std::string_view("./audio/loading.wav"), 70.f, true);
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

void LoadingState::loadLevels() noexcept
{
    auto& levels = *getContext().levels;
    try
    {
        using mazes::configurator;

        levels.load(Levels::ID::LEVEL_ONE, configurator().rows(50).columns(50), false);
        log("LoadingState: Levels loaded successfully");
    } catch (const std::exception& e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "LoadingState: Failed to load levels: %s", e.what());
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

        // Load billboard shader for character sprites (vertex + geometry + fragment)
        auto billboardShader = std::make_unique<Shader>();
        billboardShader->compileAndAttachShader(ShaderType::VERTEX, "./shaders/billboard.vert.glsl");
        billboardShader->compileAndAttachShader(ShaderType::GEOMETRY, "./shaders/billboard.geom.glsl");
        billboardShader->compileAndAttachShader(ShaderType::FRAGMENT, "./shaders/billboard.frag.glsl");
        billboardShader->linkProgram();

        log("LoadingState: Billboard shader compiled and linked");
        log(billboardShader->getGlslUniforms().c_str());
        log("\n");

        // Insert billboard shader into manager
        shaders.insert(Shaders::ID::BILLBOARD_SPRITE, std::move(billboardShader));

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
    if (!mResourceLoader)
    {
        return;
    }

    auto textureRequests = mResourceLoader->getTextureLoadRequests();

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
