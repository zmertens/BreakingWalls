#ifndef WORLD_HPP
#define WORLD_HPP

#include <box2d/box2d.h>

#include <SDL3/SDL_rect.h>

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <future>
#include <atomic>
#include <queue>
#include <mutex>
#include <glm/glm.hpp>

#include "RenderWindow.hpp"
#include "ResourceIdentifiers.hpp"
#include "Material.hpp"
#include "Animation.hpp"
#include "Plane.hpp"

class Camera;
class Player;
class RenderWindow;
class Shader;
class Sphere;

union SDL_Event;

class World final
{
    // Allow Player to access World internals for animation rendering
    friend class Player;

public:
    explicit World(RenderWindow &window, FontManager &fonts, TextureManager &textures, ShaderManager &shaders);
    ~World(); // Defined in .cpp to avoid incomplete type issues

    void init() noexcept;
    void update(float dt);
    void draw() const noexcept;
    void destroyWorld();
    void handleEvent(const SDL_Event &event);

    const std::vector<Sphere> &getSpheres() const noexcept { return mSpheres; }
    std::vector<Sphere> &getSpheres() noexcept { return mSpheres; }
    const Plane &getGroundPlane() const noexcept { return mGroundPlane; }

    void updateSphereChunks(const glm::vec3 &cameraPosition) noexcept;
    glm::vec3 getMazeSpawnPosition() const noexcept { return mPlayerSpawnPosition; }

    // ========================================================================
    // Character rendering for third-person mode
    // ========================================================================

    /// Render player character as a billboard sprite using geometry shader
    /// @param player Player with animation state
    /// @param camera Camera for view/projection matrices
    void renderPlayerCharacter(const Player &player, const Camera &camera) const noexcept;

    /// Render a character from raw state data (for remote players)
    /// @param position World position of character
    /// @param facing Facing direction in degrees
    /// @param frame Animation frame to render
    /// @param camera Camera for view/projection matrices
    void renderCharacterFromState(const glm::vec3 &position, float facing,
                                  const AnimationRect &frame, const Camera &camera) const noexcept;

    /// Get the character sprite sheet texture (for external rendering)
    [[nodiscard]] const Texture *getCharacterSpriteSheet() const noexcept;

private:
    void initPathTracerScene() noexcept;
    void syncPhysicsToSpheres() noexcept;

    struct ChunkCoord
    {
        int x, z;

        bool operator==(const ChunkCoord &other) const
        {
            return x == other.x && z == other.z;
        }
    };

    struct ChunkCoordHash
    {
        std::size_t operator()(const ChunkCoord &coord) const
        {
            return std::hash<int>()(coord.x) ^ (std::hash<int>()(coord.z) << 1);
        }
    };

    struct MazeCell
    {
        int row, col;
        int distance;
        float worldX, worldZ;
    };

    struct ChunkWorkItem
    {
        ChunkCoord coord;
        std::vector<MazeCell> cells;
        std::vector<Sphere> spheres;
        glm::vec3 spawnPosition;
        bool hasSpawnPosition{false};
    };

    // Worker management
    void initWorkerPool() noexcept;
    void shutdownWorkerPool() noexcept;
    void submitChunkForGeneration(const ChunkCoord &coord) noexcept;
    void processCompletedChunks() noexcept;
    ChunkWorkItem generateChunkAsync(const ChunkCoord &coord) const noexcept;

    ChunkCoord getChunkCoord(const glm::vec3 &position) const noexcept;
    void loadChunk(const ChunkCoord &coord) noexcept;
    void unloadChunk(const ChunkCoord &coord) noexcept;

    // Thread-safe maze generation helpers
    std::string generateMazeForChunk(const ChunkCoord &coord) const noexcept;
    std::vector<MazeCell> parseMazeCells(const std::string &mazeStr, const ChunkCoord &coord,
                                         glm::vec3 &outSpawnPosition, bool &outHasSpawn) const noexcept;
    Material::MaterialType getMaterialForDistance(int distance) const noexcept;

    static constexpr auto FORCE_DUE_TO_GRAVITY = 9.8f;

    RenderWindow &mWindow;
    FontManager &mFonts;
    TextureManager &mTextures;
    ShaderManager &mShaders; // Added for billboard shader access

    b2WorldId mWorldId;
    b2BodyId mMazeWallsBodyId;

    bool mIsPanning;
    SDL_FPoint mLastMousePosition;

    std::vector<Sphere> mSpheres;
    std::vector<b2BodyId> mSphereBodyIds;
    Plane mGroundPlane;

    // Modern C++ worker pool
    static constexpr size_t NUM_WORKER_THREADS = 4;
    static constexpr float SPHERE_SPAWN_RATE = 0.01f;
    std::atomic<bool> mWorkersShouldStop{false};
    std::vector<std::future<void>> mWorkerThreads;

    mutable std::mutex mWorkQueueMutex;
    std::queue<std::packaged_task<ChunkWorkItem()>> mWorkQueue;
    std::condition_variable mWorkAvailable;

    mutable std::mutex mCompletedChunksMutex;
    std::vector<std::pair<ChunkCoord, std::future<ChunkWorkItem>>> mPendingChunks;

    // Chunk management
    static constexpr float CHUNK_SIZE = 100.0f;
    static constexpr int CHUNK_LOAD_RADIUS = 2;
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

    // Character sprite sheet dimensions (for animation rendering)
    static constexpr int CHARACTER_TILE_SIZE = 128;
    static constexpr int CHARACTER_FRAMES_PER_ROW = 9;
};

#endif // WORLD_HPP
