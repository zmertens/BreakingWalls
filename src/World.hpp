#ifndef WORLD_HPP
#define WORLD_HPP

#include <box2d/box2d.h>

#include <array>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <future>
#include <atomic>
#include <memory>
#include <queue>
#include <mutex>
#include <glm/glm.hpp>

#include "CommandQueue.hpp"
#include "PostProcessingManager.hpp"
#include "RenderWindow.hpp"
#include "ResourceIdentifiers.hpp"
#include "SceneNode.hpp"
#include "View.hpp"
#include "Material.hpp"

class Pathfinder;
class Player;
class RenderWindow;
class Sphere;

class World final
{
public:
    explicit World(RenderWindow& window, FontManager& fonts, TextureManager& textures);
    ~World();  // Defined in .cpp to avoid incomplete type issues

    void init() noexcept;
    void update(float dt);
    void draw() const noexcept;
    CommandQueue& getCommandQueue() noexcept;
    void destroyWorld();
    void handleEvent(const SDL_Event& event);
    void setPlayer(Player* player);

    const std::vector<Sphere>& getSpheres() const noexcept { return mSpheres; }
    std::vector<Sphere>& getSpheres() noexcept { return mSpheres; }

    void updateSphereChunks(const glm::vec3& cameraPosition) noexcept;
    glm::vec3 getMazeSpawnPosition() const noexcept { return mPlayerSpawnPosition; }

private:

    void initPathTracerScene() noexcept;
    void syncPhysicsToSpheres() noexcept;

    struct ChunkCoord {
        int x, z;

        bool operator==(const ChunkCoord& other) const {
            return x == other.x && z == other.z;
        }
    };

    struct ChunkCoordHash {
        std::size_t operator()(const ChunkCoord& coord) const {
            return std::hash<int>()(coord.x) ^ (std::hash<int>()(coord.z) << 1);
        }
    };

    struct MazeCell {
        int row, col;
        int distance;
        float worldX, worldZ;
    };

    struct ChunkWorkItem {
        ChunkCoord coord;
        std::vector<MazeCell> cells;
        std::vector<Sphere> spheres;
        glm::vec3 spawnPosition;  // Track spawn in work item instead of const_cast
        bool hasSpawnPosition{ false };
    };

    // Worker management
    void initWorkerPool() noexcept;
    void shutdownWorkerPool() noexcept;
    void submitChunkForGeneration(const ChunkCoord& coord) noexcept;
    void processCompletedChunks() noexcept;
    ChunkWorkItem generateChunkAsync(const ChunkCoord& coord) const noexcept;

    ChunkCoord getChunkCoord(const glm::vec3& position) const noexcept;
    void loadChunk(const ChunkCoord& coord) noexcept;
    void unloadChunk(const ChunkCoord& coord) noexcept;

    // Thread-safe maze generation helpers
    std::string generateMazeForChunk(const ChunkCoord& coord) const noexcept;
    std::vector<MazeCell> parseMazeCells(const std::string& mazeStr, const ChunkCoord& coord,
        glm::vec3& outSpawnPosition, bool& outHasSpawn) const noexcept;
    int parseBase36(const std::string& str) const noexcept;
    MaterialType getMaterialForDistance(int distance) const noexcept;

    enum class Layer
    {
        PARALLAX_BACK = 0,
        PARALLAX_MID = 1,
        PARALLAX_FORE = 2,
        BACKGROUND = 3,
        FOREGROUND = 4,
        LAYER_COUNT = 5
    };

    static constexpr auto FORCE_DUE_TO_GRAVITY = 9.8f;

    RenderWindow& mWindow;
    View mWorldView;
    FontManager& mFonts;
    TextureManager& mTextures;

    SceneNode mSceneGraph;
    std::array<SceneNode*, static_cast<std::size_t>(Layer::LAYER_COUNT)> mSceneLayers;

    b2WorldId mWorldId;
    b2BodyId mMazeWallsBodyId;

    CommandQueue mCommandQueue;
    Pathfinder* mPlayerPathfinder;

    bool mIsPanning;
    SDL_FPoint mLastMousePosition;

    std::unique_ptr<PostProcessingManager> mPostProcessingManager;

    std::vector<Sphere> mSpheres;
    std::vector<b2BodyId> mSphereBodyIds;

    // Modern C++ worker pool - INCREASED spawn rate to 1% for better performance
    static constexpr size_t NUM_WORKER_THREADS = 4;
    static constexpr float SPHERE_SPAWN_RATE = 0.01f;  // 1% = ~200 total spheres
    std::atomic<bool> mWorkersShouldStop{ false };
    std::vector<std::future<void>> mWorkerThreads;

    mutable std::mutex mWorkQueueMutex;
    std::queue<std::packaged_task<ChunkWorkItem()>> mWorkQueue;
    std::condition_variable mWorkAvailable;

    mutable std::mutex mCompletedChunksMutex;
    std::vector<std::pair<ChunkCoord, std::future<ChunkWorkItem>>> mPendingChunks;

    // Chunk management - REDUCED radius for better performance
    static constexpr float CHUNK_SIZE = 100.0f;
    static constexpr int CHUNK_LOAD_RADIUS = 2;  // Reduced from 3 (25 chunks instead of 49)
    static constexpr int MAZE_ROWS = 20;
    static constexpr int MAZE_COLS = 20;
    static constexpr float CELL_SIZE = CHUNK_SIZE / static_cast<float>(MAZE_COLS);

    std::unordered_set<ChunkCoord, ChunkCoordHash> mLoadedChunks;
    std::unordered_map<ChunkCoord, std::vector<size_t>, ChunkCoordHash> mChunkSphereIndices;

    // Thread-safe maze cache with mutex
    mutable std::mutex mMazeCacheMutex;
    mutable std::unordered_map<ChunkCoord, std::string, ChunkCoordHash> mChunkMazes;

    glm::vec3 mLastChunkUpdatePosition;
    glm::vec3 mPlayerSpawnPosition;

    static constexpr int TOTAL_SPHERES = 200;
};

#endif // WORLD_HPP
