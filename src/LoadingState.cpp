#include "LoadingState.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <thread>
#include <mutex>
#include <optional>

#include <MazeBuilder/configurator.h>
#include <MazeBuilder/create.h>
#include <MazeBuilder/io_utils.h>
#include <MazeBuilder/json_helper.h>
#include <MazeBuilder/singleton_base.h>

#include "Font.hpp"
#include "GLTFModel.hpp"
#include "JSONUtils.hpp"
#include "Level.hpp"
#include "MusicPlayer.hpp"
#include "Options.hpp"
#include "ResourceIdentifiers.hpp"
#include "ResourceManager.hpp"
#include "Shader.hpp"
#include "StateStack.hpp"
#include "Texture.hpp"

#include <fonts/Cousine_Regular.h>
#include <fonts/Limelight_Regular.h>
#include <fonts/nunito_sans.h>

extern "C"
{
    float simplex2(float x, float y, int octaves, float persistence, float lacunarity);
    float simplex3(float x, float y, float z, int octaves, float persistence, float lacunarity);
}

// Resource Keys
namespace JSONKeys
{
    constexpr std::string_view CHARACTER_IMAGE = "character_default_image";
    constexpr std::string_view CHARACTERS_SPRITE_SHEET = "characters_spritesheet";
    constexpr std::string_view BALL_NORMAL = "ball_normal";
    constexpr std::string_view ENEMY_HITPOINTS_DEFAULT = "enemy_hitpoints_default";
    constexpr std::string_view ENEMY_SPEED_DEFAULT = "enemy_speed_default";
    constexpr std::string_view EXPLOSIONS_SPRITE_SHEET = "explosion_spritesheet";
    constexpr std::string_view GAME_MENU_REMIXED_MUSIC = "game_menu_remixed_music";
    constexpr std::string_view LOADING_MUSIC = "loading_music";
    constexpr std::string_view LEVEL_DEFAULTS = "level_defaults";
    constexpr std::string_view NETWORK_URL = "network_url";
    constexpr std::string_view OGG_FILES = "ogg_files";
    constexpr std::string_view PLAYER_HITPOINTS_DEFAULT = "player_hitpoints_default";
    constexpr std::string_view PLAYER_SPEED_DEFAULT = "player_speed_default";
    constexpr std::string_view SDL_LOGO = "SDL_logo";
    constexpr std::string_view SFML_LOGO = "SFML_logo";
    constexpr std::string_view SHADER_BILLBOARD_VERTEX = "shader_billboard_vert_glsl";
    constexpr std::string_view SHADER_BILLBOARD_FRAGMENT = "shader_billboard_frag_glsl";
    constexpr std::string_view SHADER_BILLBOARD_GEOMETRY = "shader_billboard_geom_glsl";
    constexpr std::string_view SHADER_COMPOSITE_VERTEX = "shader_composite_vert_glsl";
    constexpr std::string_view SHADER_COMPOSITE_FRAGMENT = "shader_composite_frag_glsl";
    constexpr std::string_view SHADER_MODEL_VERTEX = "shader_model_vert_glsl";
    constexpr std::string_view SHADER_MODEL_FRAGMENT = "shader_model_frag_glsl";
    constexpr std::string_view SHADER_PARTICLES_COMPUTE = "shader_particles_cs_glsl";
    constexpr std::string_view SHADER_PATHTRACER_COMPUTE = "shader_pathtracer_cs_glsl";
    constexpr std::string_view SHADER_PARTICLES_VERTEX = "shader_particles_vert_glsl";
    constexpr std::string_view SHADER_PARTICLES_FRAGMENT = "shader_particles_frag_glsl";
    constexpr std::string_view SHADER_SCREEN_VERTEX = "shader_screen_vert_glsl";
    constexpr std::string_view SHADER_SCREEN_FRAGMENT = "shader_screen_frag_glsl";
    constexpr std::string_view SHADER_SHADOW_VERTEX = "shader_shadow_vert_glsl";
    constexpr std::string_view SHADER_SHADOW_GEOMETRY = "shader_shadow_geom_glsl";
    constexpr std::string_view SHADER_SHADOW_FRAGMENT = "shader_shadow_frag_glsl";
    constexpr std::string_view STYLIZED_CHARACTER_GLTF2_MODEL = "stylized_character_gltf2";
    constexpr std::string_view SOUND_GENERATE = "generate_ogg";
    constexpr std::string_view SOUND_SELECT = "select_ogg";
    constexpr std::string_view SOUND_THROW = "throw_ogg";
    constexpr std::string_view SPLASH_IMAGE = "splash_image";
    constexpr std::string_view WALL_HORIZONTAL = "wall_horizontal";
    constexpr std::string_view WINDOW_ICON = "window_icon";
}

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
class ResourceLoader : public mazes::singleton_base<ResourceLoader>
{
    friend class mazes::singleton_base<ResourceLoader>;

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
        : mShouldExit(false), mPendingWorkCount(0), mTotalWorkItems(0), mConfigMappings({{JSONKeys::BALL_NORMAL, Textures::ID::BALL_NORMAL},
                                                                                         {JSONKeys::CHARACTER_IMAGE, Textures::ID::CHARACTER},
                                                                                         {JSONKeys::LEVEL_DEFAULTS, Textures::ID::LEVEL_TWO},
                                                                                         {JSONKeys::CHARACTERS_SPRITE_SHEET, Textures::ID::CHARACTER_SPRITE_SHEET},
                                                                                         {JSONKeys::SPLASH_IMAGE, Textures::ID::SPLASH_TITLE_IMAGE},
                                                                                         {JSONKeys::SDL_LOGO, Textures::ID::SDL_LOGO},
                                                                                         {JSONKeys::SFML_LOGO, Textures::ID::SFML_LOGO},
                                                                                         {JSONKeys::WALL_HORIZONTAL, Textures::ID::WALL_HORIZONTAL},
                                                                                         {JSONKeys::WINDOW_ICON, Textures::ID::WINDOW_ICON}})
    {
    }

    ~ResourceLoader()
    {
        shutdown();
    }

    ResourceLoader(const ResourceLoader &) = delete;
    ResourceLoader &operator=(const ResourceLoader &) = delete;
    ResourceLoader(ResourceLoader &&) = delete;
    ResourceLoader &operator=(ResourceLoader &&) = delete;

    /// @brief Initialize worker threads for concurrent resource loading
    void initThreads(unsigned int numWorkers = 4) noexcept
    {
        if (mWorkerThreads.size() > 0)
        {
            return;
        }

        for (unsigned int i = 0; i < numWorkers; ++i)
        {
            mWorkerThreads.emplace_back([this]()
                                        { workerThreadFunc(); });
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

        // Convert relative path to absolute if needed
        std::string absolutePath = std::string(resourcePath);
        if (!std::filesystem::path(absolutePath).is_absolute())
        {
            try
            {
                absolutePath = std::filesystem::absolute(absolutePath).string();
            }
            catch (const std::exception &e)
            {
                SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to resolve absolute path: %s\n", e.what());
                absolutePath = std::string(resourcePath);
            }
        }

        mResourcePathPrefix = mazes::io_utils::getDirectoryPath(absolutePath) + "/";

        SDL_Log("ResourceLoader: Resource path: %s,\nabsolute: %s,\nprefix: %s\n",
                resourcePath.data(), absolutePath.c_str(), mResourcePathPrefix.c_str());

        std::unordered_map<std::string, std::string> resources{};

        try
        {
            JSONUtils::loadConfiguration(absolutePath, resources);
        }
        catch (const std::exception &e)
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "ResourceLoader: Failed to load resources: %s\n", e.what());
            return;
        }

        if (resources.empty())
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "ResourceLoader: No resources found in %s\n", absolutePath.c_str());
            return;
        }

        {
            std::unique_lock<std::mutex> lock(mQueueMutex);

            int index = 0;
            for (const auto &[key, value] : resources)
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

    /// @brief Get the resource path prefix used by the loader (thread-safe)
    std::string getResourcePathPrefix() const noexcept
    {
        std::unique_lock<std::mutex> lock(mQueueMutex);
        return mResourcePathPrefix;
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

            mCondVar.wait(lock, [this]()
                          { return !mWorkQueue.empty() || mShouldExit.load(); });

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
    void processWorkItem(const ResourceWorkItem &item) noexcept
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

            if (auto k = mProcessedConfigs.find(item.key); k != mProcessedConfigs.cend() && k->second)
            {
                return;
            }
        }

        if (item.key == JSONKeys::LEVEL_DEFAULTS)
        {
            processLevelDefaults(item.value);
        }
        else if (item.key == JSONKeys::PLAYER_HITPOINTS_DEFAULT ||
                 item.key == JSONKeys::PLAYER_SPEED_DEFAULT ||
                 item.key == JSONKeys::ENEMY_HITPOINTS_DEFAULT ||
                 item.key == JSONKeys::ENEMY_SPEED_DEFAULT)
        {
            // Numeric configs handled here in the future
        }
        else
        {
            processTextureRequest(item.key, item.value);
        }

        std::unique_lock<std::mutex> lock(mQueueMutex);
        mProcessedConfigs[item.key] = true;
    }

    // @TODO - Move to Level / LevelServices
    /// @brief Process level configuration and generate mazes
    void processLevelDefaults(const std::string &value) noexcept
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

                for (const auto &[key, val] : levelConfigs[i])
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
                        }
                        catch (...)
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
                                                       [](char c)
                                                       { return c == '"' || c == '\'' || std::isspace(static_cast<unsigned char>(c)); }),
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
            }
            catch (const std::exception &e)
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
    void processTextureRequest(const std::string &key, const std::string &value) noexcept
    {
        for (const auto &[cfgKey, textureId] : mConfigMappings)
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

        for (auto &t : mWorkerThreads)
        {
            if (t.joinable())
            {
                t.join();
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

namespace
{
    inline ResourceLoader &resourceLoader()
    {
        return *mazes::singleton_base<ResourceLoader>::instance();
    }
}

///
/// @param stack
/// @param context
/// @param resourcePath ""
LoadingState::LoadingState(StateStack &stack, Context context, std::string_view resourcePath)
    : State(stack, context), mHasFinished{false}, mResourcePath{resourcePath}
{
    resourceLoader().initThreads();

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
    // Show resource loading progress in a simple ImGui window near bottom-left
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 screenSize = io.DisplaySize;
    float completion = resourceLoader().getCompletion();
    char buf[64];
    SDL_snprintf(buf, sizeof(buf), "Resource loading progress: %.0f%%", completion * 100.0f);

    ImVec2 windowPos = ImVec2(10, screenSize.y - 50);
    ImVec2 windowSize = ImVec2(320, 40);
    ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::Begin("LoadingProgress", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("%s", buf);
    ImGui::End();}

bool LoadingState::update(float dt, unsigned int subSteps) noexcept
{
    if (!mHasFinished && resourceLoader().isDone())
    {
        if (const auto resources = resourceLoader().getResources(); !resources.empty())
        {
            log("Loading complete! Loaded " + std::to_string(resources.size()) + " resources.");

            // Now actually load the getTextureManager() from the worker-collected texture requests
            loadTexturesFromWorkerRequests();
            // Handle window icon separately (special case, not managed by TextureManager)
            loadWindowIcon(resources);
            loadAudio();
            loadModels();
        }

        mHasFinished = true;
        requestStackPop();
        requestStackPush(States::ID::SPLASH);
    }

    if (!mHasFinished)
    {
        setCompletion(resourceLoader().getCompletion());
        log("LoadingState::update - completion: " + std::to_string(resourceLoader().getCompletion() * 100.f) + "%");
    }

    return true;
}

bool LoadingState::handleEvent(const SDL_Event &event) noexcept
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
    log("Loading resources from:\t" + mResourcePath);

    if (resourceLoader().isDone())
    {
        resourceLoader().load(mResourcePath);
        loadFonts();
        loadLevels();
        loadShaders();
        loadNetworkConfig();
    }
}

void LoadingState::loadFonts() noexcept
{
    auto &fonts = *getContext().getFontManager();

    fonts.load(Fonts::ID::LIMELIGHT,
               Limelight_Regular_compressed_data,
               Limelight_Regular_compressed_size);

    fonts.load(Fonts::ID::NUNITO_SANS,
               NunitoSans_compressed_data,
               NunitoSans_compressed_size);

    fonts.load(Fonts::ID::COUSINE_REGULAR,
               Cousine_Regular_compressed_data,
               Cousine_Regular_compressed_size);
}

void LoadingState::loadAudio() noexcept
{
    auto &music = *getContext().getMusicManager();
    // Use the same resource path prefix that was computed by the ResourceLoader
    auto resourcePathPrefix = resourceLoader().getResourcePathPrefix();

    log("LoadingState::loadAudio - resourcePathPrefix: " + resourcePathPrefix);

    try
    {
        const auto resources = resourceLoader().getResources();
        const std::string loadingMusicPath = JSONUtils::getResourcePath(
            std::string(JSONKeys::LOADING_MUSIC), resources, resourcePathPrefix);
        const std::string gameMenuRemixedMusicPath = JSONUtils::getResourcePath(
            std::string(JSONKeys::GAME_MENU_REMIXED_MUSIC), resources, resourcePathPrefix);

        if (loadingMusicPath.empty() || gameMenuRemixedMusicPath.empty())
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "LoadingState: music resource key not found in configuration\n");
        }
        else
        {
            const auto &options = *getContext().getOptionsManager();
            music.load(Music::ID::GAME_MUSIC,
                       std::string_view{loadingMusicPath},
                       options.get(GUIOptions::ID::DE_FACTO).getMusicVolume(), true);

                
            music.load(Music::ID::MENU_MUSIC,
                       std::string_view{gameMenuRemixedMusicPath},
                       options.get(GUIOptions::ID::DE_FACTO).getMusicVolume(), true);

            log("LoadingState: Music loaded successfully");
            music.get(Music::ID::GAME_MUSIC).print();
        }
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "LoadingState: Failed to load music: %s", e.what());
    }

    // Continue with existing sound effects loading...
    auto &soundBuffers = *getContext().getSoundBufferManager();

    try
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "LoadingState: Loading sound effects...");
        const auto resources = resourceLoader().getResources();
        const std::string generatePath = JSONUtils::getResourcePath(
            std::string(JSONKeys::SOUND_GENERATE), resources, resourcePathPrefix);
        const std::string selectPath = JSONUtils::getResourcePath(
            std::string(JSONKeys::SOUND_SELECT), resources, resourcePathPrefix);
        const std::string throwPath = JSONUtils::getResourcePath(
            std::string(JSONKeys::SOUND_THROW), resources, resourcePathPrefix);

        soundBuffers.load(SoundEffect::ID::GENERATE, generatePath);

        soundBuffers.load(SoundEffect::ID::SELECT, selectPath);

        soundBuffers.load(SoundEffect::ID::THROW, throwPath);
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                     "LoadingState: Failed to load sound effects: %s", e.what());
    }
}

void LoadingState::loadLevels() noexcept
{
    auto &levels = *getContext().getLevelsManager();
    try
    {
        using mazes::configurator;

        std::vector<configurator> levelConfigs;
        levelConfigs.push_back(configurator().rows(50).columns(50));
        levels.load(Levels::ID::LEVEL_ONE, std::cref(levelConfigs), false);
        log("LoadingState: Levels loaded successfully");
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "LoadingState: Failed to load levels: %s", e.what());
    }
}

void LoadingState::loadModels() noexcept
{
    auto &models = *getContext().getModelsManager();

    try
    {
        const auto resources = resourceLoader().getResources();
        const std::string modelPath = JSONUtils::getResourcePath(
            std::string(JSONKeys::STYLIZED_CHARACTER_GLTF2_MODEL), resources, resourceLoader().getResourcePathPrefix());

        if (modelPath.empty())
        {
            log("LoadingState: model resource key not found in configuration");
        }
        else
        {
            models.load(Models::ID::STYLIZED_CHARACTER, modelPath);

            log("LoadingState: Found model path: " + modelPath);
            log("LoadingState: Loading model into manager...");

            try
            {
                auto &model = models.get(Models::ID::STYLIZED_CHARACTER);
                
                // Break down the logging into smaller chunks for better debugging
                const size_t meshCount = model.getMeshes().size();
                const size_t boneCount = model.getBoneCount();
                const size_t totalMeshBones = model.getTotalMeshBones();
                const size_t animationCount = model.getAnimationNames().size();

                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GLTFModel: loaded %s", modelPath.c_str());
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "  Meshes: %zu", meshCount);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "  Bones (mapped): %zu", boneCount);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "  Mesh-bones (raw): %zu", totalMeshBones);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "  Animations: %zu", animationCount);

                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "LoadingState: Model verified in manager");
            }
            catch (const std::exception &e)
            {
                log("LoadingState: Failed to verify model in manager: " + std::string(e.what()));
            }
        }
    }
    catch (const std::exception &e)
    {
        log("LoadingState: Failed to load models: " + std::string(e.what()));
    }
}

void LoadingState::loadShaders() noexcept
{
    auto &shaders = *getContext().getShaderManager();

    try
    {
        auto &&resources = resourceLoader().getResources();
        const auto resourcePathPrefix = resourceLoader().getResourcePathPrefix();

        const auto shaderPath = [&](std::string_view key)
        {
            return JSONUtils::getResourcePath(std::string(key), resources, resourcePathPrefix);
        };

        auto displayShader = std::make_unique<Shader>();
        displayShader->compileAndAttachShader(Shader::ShaderType::VERTEX, shaderPath(JSONKeys::SHADER_SCREEN_VERTEX));
        displayShader->compileAndAttachShader(Shader::ShaderType::FRAGMENT, shaderPath(JSONKeys::SHADER_SCREEN_FRAGMENT));
        displayShader->linkProgram();

        log("LoadingState: Display shader compiled and linked");

        // Insert display shader into manager (using vertex ID as the combined shader program ID)
        shaders.insert(Shaders::ID::GLSL_FULLSCREEN_QUAD, std::move(displayShader));

        auto particlesDisplayShader = std::make_unique<Shader>();
        particlesDisplayShader->compileAndAttachShader(Shader::ShaderType::VERTEX, shaderPath(JSONKeys::SHADER_PARTICLES_VERTEX));
        particlesDisplayShader->compileAndAttachShader(Shader::ShaderType::FRAGMENT, shaderPath(JSONKeys::SHADER_PARTICLES_FRAGMENT));
        particlesDisplayShader->linkProgram();
        log("LoadingState: Particles display shader compiled and linked");
        shaders.insert(Shaders::ID::GLSL_FULLSCREEN_QUAD_MVP, std::move(particlesDisplayShader));

        auto compositeShader = std::make_unique<Shader>();
        compositeShader->compileAndAttachShader(Shader::ShaderType::VERTEX, shaderPath(JSONKeys::SHADER_COMPOSITE_VERTEX));
        compositeShader->compileAndAttachShader(Shader::ShaderType::FRAGMENT, shaderPath(JSONKeys::SHADER_COMPOSITE_FRAGMENT));
        compositeShader->linkProgram();
        log("LoadingState: Composite shader compiled and linked");
        shaders.insert(Shaders::ID::GLSL_COMPOSITE_SCENE, std::move(compositeShader));

        // Load shadow volume shader for character shadow rendering (vertex + geometry + fragment)
        auto shadowShader = std::make_unique<Shader>();
        shadowShader->compileAndAttachShader(Shader::ShaderType::VERTEX, shaderPath(JSONKeys::SHADER_SHADOW_VERTEX));
        shadowShader->compileAndAttachShader(Shader::ShaderType::GEOMETRY, shaderPath(JSONKeys::SHADER_SHADOW_GEOMETRY));
        shadowShader->compileAndAttachShader(Shader::ShaderType::FRAGMENT, shaderPath(JSONKeys::SHADER_SHADOW_FRAGMENT));
        shadowShader->linkProgram();
        log("LoadingState: Shadow shader compiled and linked");
        shaders.insert(Shaders::ID::GLSL_SHADOW_VOLUME, std::move(shadowShader));

        // Load compute shader for path tracing
        auto computeShader = std::make_unique<Shader>();
        computeShader->compileAndAttachShader(Shader::ShaderType::COMPUTE, shaderPath(JSONKeys::SHADER_PATHTRACER_COMPUTE));
        computeShader->linkProgram();

        auto computeShader2 = std::make_unique<Shader>();
        computeShader2->compileAndAttachShader(Shader::ShaderType::COMPUTE, shaderPath(JSONKeys::SHADER_PARTICLES_COMPUTE));
        computeShader2->linkProgram();

        log("LoadingState: Compute shaders compiled and linked");

        shaders.insert(Shaders::ID::GLSL_PATH_TRACER_COMPUTE, std::move(computeShader));
        shaders.insert(Shaders::ID::GLSL_PARTICLES_COMPUTE, std::move(computeShader2));

        // Load billboard shader for character sprites (vertex + geometry + fragment)
        auto billboardShader = std::make_unique<Shader>();
        billboardShader->compileAndAttachShader(Shader::ShaderType::VERTEX, shaderPath(JSONKeys::SHADER_BILLBOARD_VERTEX));
        billboardShader->compileAndAttachShader(Shader::ShaderType::GEOMETRY, shaderPath(JSONKeys::SHADER_BILLBOARD_GEOMETRY));
        billboardShader->compileAndAttachShader(Shader::ShaderType::FRAGMENT, shaderPath(JSONKeys::SHADER_BILLBOARD_FRAGMENT));
        billboardShader->linkProgram();

        log("LoadingState: Billboard shader compiled and linked");
        // Insert billboard shader into manager
        shaders.insert(Shaders::ID::GLSL_BILLBOARD_SPRITE, std::move(billboardShader));

        auto modelShader = std::make_unique<Shader>();
        modelShader->compileAndAttachShader(Shader::ShaderType::VERTEX, shaderPath(JSONKeys::SHADER_MODEL_VERTEX));
        modelShader->compileAndAttachShader(Shader::ShaderType::FRAGMENT, shaderPath(JSONKeys::SHADER_MODEL_FRAGMENT));
        modelShader->linkProgram();
        log("LoadingState: Model shader compiled and linked");
        shaders.insert(Shaders::ID::GLSL_MODEL_WITH_SKINNING, std::move(modelShader));
        
        log("LoadingState: All shaders loaded successfully");
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "LoadingState: Shader initialization failed: %s", e.what());
    }
}

void LoadingState::loadNetworkConfig() noexcept
{
    auto &httpClient = *getContext().getHttpClient();

    try
    {
        const auto resources = resourceLoader().getResources();

        if (const auto url = JSONUtils::getResourcePath(std::string(JSONKeys::NETWORK_URL), resources, resourceLoader().getResourcePathPrefix())
        ; url.empty())
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, 
                "LoadingState: NETWORK_URL resource key not found in configuration");
        }
        else
        {
            httpClient.setServerURL(url);
            log("LoadingState: Found network URL: " + url);
        }
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "LoadingState: Failed to load network configuration: %s", e.what());
    }
}

void LoadingState::loadTexturesFromWorkerRequests() const noexcept
{
    auto &textures = *getContext().getTextureManager();

    auto textureRequests = resourceLoader().getTextureLoadRequests();

    try
    {
        for (const auto &request : textureRequests)
        {
            textures.load(request.id, std::string_view(request.path), 0u);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loaded texture: %s for ID: %d", request.path.c_str(), static_cast<int>(request.id));
        }

        loadProceduralTextures();
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load procedural textures: %s\n", e.what());
    }
}

void LoadingState::loadWindowIcon(const std::unordered_map<std::string, std::string> &resources) const noexcept
{
    using std::string;

    // Use the same resource path prefix that was computed by the ResourceLoader
    auto resourcePathPrefix = resourceLoader().getResourcePathPrefix();

    const string windowIconPath = JSONUtils::getResourcePath(
        std::string(JSONKeys::WINDOW_ICON), resources, resourcePathPrefix);

    if (windowIconPath.empty())
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "LoadingState: Window icon resource not found in configuration");

        return;
    }

    if (SDL_Surface *icon = SDL_LoadBMP(windowIconPath.c_str()); icon != nullptr)
    {
        if (auto *renderWindow = getContext().getRenderWindow(); renderWindow != nullptr)
        {
            SDL_SetWindowIcon(renderWindow->getSDLWindow(), icon);
            SDL_DestroySurface(icon);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading window icon from:\t%s", windowIconPath.c_str());
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "LoadingState: Window icon loaded successfully\n");
        }
    }
    else
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load icon: %s - %s\n", windowIconPath.c_str(),
                     SDL_GetError());
    }
}

void LoadingState::loadProceduralTextures() const noexcept
{
    auto &textures = *getContext().getTextureManager();

    try
    {
        auto generator = [](std::vector<std::uint8_t> &buffer, int width, int height)
        {
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    float value = simplex2(static_cast<float>(x) / 64.f, static_cast<float>(y) / 64.f, 4, 0.5f, 2.f);
                    buffer[y * width + x] = static_cast<Uint8>((value + 1.f) * 127.5f); // Map from [-1,1] to [0,255]
                }
            }
        };

        textures.load(Textures::ID::PATH_TRACER_ACCUM, 512, 512, {}, 0);
        textures.load(Textures::ID::PATH_TRACER_DISPLAY, 512, 512, {}, 0);
        textures.load(Textures::ID::NOISE2D, 256, 256, generator, 2);
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load procedural textures: %s\n", e.what());
    }
}