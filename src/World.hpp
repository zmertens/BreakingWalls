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
    // NOTE: World's primary responsibility is physics simulation/state.
    // Rendering helper APIs remain only for compatibility while raster paths are being retired.
    explicit World(RenderWindow &window, FontManager &fonts, TextureManager &textures, ShaderManager &shaders, LevelsManager &levels);
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
                                  const AnimationRect &frame,
                                  const Camera &camera,
                                  bool useWorldAxes = false,
                                  const glm::vec3 &rightAxisWS = glm::vec3(1.0f, 0.0f, 0.0f),
                                  const glm::vec3 &upAxisWS = glm::vec3(0.0f, 1.0f, 0.0f),
                                  bool doubleSided = false) const noexcept;

    /// Render a textured billboard quad from a full texture
    /// @param position World position of billboard center
    /// @param halfSize Billboard half-size fallback used by shader path
    /// @param halfSizeXY Explicit half-width/half-height override (0 uses halfSize)
    /// @param textureId Texture resource identifier
    /// @param camera Camera for view/projection matrices
    void renderTexturedBillboard(const glm::vec3 &position,
                                 float halfSize,
                                 const glm::vec2 &halfSizeXY,
                                 Textures::ID textureId,
                                 bool useWorldAxes,
                                 const glm::vec3 &rightAxisWS,
                                 const glm::vec3 &upAxisWS,
                                 bool doubleSided,
                                 const Camera &camera) const noexcept;

    /// Get the character sprite sheet texture (for external rendering)
    [[nodiscard]] const Texture *getCharacterSpriteSheet() const noexcept;

    // ========================================================================
    // Scoring system
    // ========================================================================

    /// Get current player score
    [[nodiscard]] int getScore() const noexcept { return mScore; }

    /// Add points to score (can be negative)
    void addScore(int points) noexcept { mScore += points; }

    /// Get pickup sphere positions and values for rendering score billboards
    struct PickupSphere
    {
        glm::vec3 position;
        int value;
        bool collected{false};
    };

    [[nodiscard]] const std::vector<PickupSphere> &getPickupSpheres() const noexcept { return mPickupSpheres; }

    /// Check and collect pickups near a position, returns points gained
    int collectNearbyPickups(const glm::vec3 &playerPos, float collectRadius = 3.0f) noexcept;

    // ========================================================================
    // Physics player body
    // ========================================================================

    /// Create a dynamic player body in the physics world
    void createPlayerBody(const glm::vec3 &position) noexcept;

    /// Get the player physics body for external queries
    [[nodiscard]] b2BodyId getPlayerBodyId() const noexcept { return mPlayerBodyId; }

    /// Apply impulse to player body (for jumping)
    void applyPlayerJumpImpulse(float impulse) noexcept;

    /// Get player body velocity for motion blur
    [[nodiscard]] glm::vec2 getPlayerVelocity() const noexcept;

private:
    void initPathTracerScene() noexcept;
    
    // Sphere physics coordinate transformation
    glm::vec3 projectOntoSphere(glm::vec2 flatPos) const noexcept;
    glm::vec2 projectFromSphere(const glm::vec3 &spherePos) const noexcept;
    void syncPhysicsToSpheres() noexcept;
    void breakQueuedWalls() noexcept;

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
        bool wallNorth{false};
        bool wallSouth{false};
        bool wallEast{false};
        bool wallWest{false};
    };

    struct ChunkWorkItem
    {
        ChunkCoord coord;
        std::vector<MazeCell> cells;
        std::vector<Sphere> spheres;
        std::vector<PickupSphere> pickupSpheres;
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
    void buildMazeWallSpheres(const std::vector<MazeCell> &cells, std::vector<Sphere> &outSpheres) const noexcept;
    void buildPickupSpheres(const std::vector<MazeCell> &cells, std::vector<PickupSphere> &outPickups, const ChunkCoord &coord) const noexcept;
    Material::MaterialType getMaterialForDistance(int distance) const noexcept;

    static constexpr auto FORCE_DUE_TO_GRAVITY = 9.8f;

    RenderWindow &mWindow;
    FontManager &mFonts;
    TextureManager &mTextures;
    ShaderManager &mShaders; // Added for billboard shader access
    LevelsManager &mLevels;

    b2WorldId mWorldId;
    b2BodyId mMazeWallsBodyId;
    bool mIsPanning;
    SDL_FPoint mLastMousePosition;

    std::vector<Sphere> mSpheres;
    std::vector<b2BodyId> mSphereBodyIds;
    std::vector<b2BodyId> mWallBreakQueue;
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
    size_t mPersistentSphereCount{0};

    static constexpr int TOTAL_SPHERES = 200;
    
    // Voronoi sphere planet parameters
    static constexpr float PLANET_RADIUS = 50.0f;
    float mPlanetRadius{PLANET_RADIUS};
    glm::vec3 mPlanetCenter{glm::vec3(0.0f)};

    // Character sprite sheet dimensions (for animation rendering)
    static constexpr int CHARACTER_TILE_SIZE = 128;
    static constexpr int CHARACTER_FRAMES_PER_ROW = 9;

    // Scoring system
    int mScore{0};
    std::vector<PickupSphere> mPickupSpheres;

    // Player physics body
    b2BodyId mPlayerBodyId{b2_nullBodyId};
};

#endif // WORLD_HPP
