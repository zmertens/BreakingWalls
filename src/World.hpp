#ifndef WORLD_HPP
#define WORLD_HPP

#include <box2d/box2d.h>

#include <array>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <glm/glm.hpp>

#include "CommandQueue.hpp"
#include "PostProcessingManager.hpp"
#include "RenderWindow.hpp"
#include "ResourceIdentifiers.hpp"
#include "SceneNode.hpp"
#include "View.hpp"
#include "Material.hpp"  // For MaterialType enum

class Ball;
class Pathfinder;
class Player;
class RenderWindow;
class Sphere;

class World final
{
public:
    explicit World(RenderWindow& window, FontManager& fonts, TextureManager& textures);

    void init() noexcept;

    // Update the world (update physics and entities)
    void update(float dt);

    // Draw the world (render entities) - pass renderer for drawing
    void draw() const noexcept;

    CommandQueue& getCommandQueue() noexcept;

    // Destroy the world
    void destroyWorld();

    void handleEvent(const SDL_Event& event);

    void setPlayer(Player* player);
    
    // Access to 3D spheres for rendering
    const std::vector<Sphere>& getSpheres() const noexcept { return mSpheres; }
    std::vector<Sphere>& getSpheres() noexcept { return mSpheres; }
    
    // Update sphere chunks based on camera position
    void updateSphereChunks(const glm::vec3& cameraPosition) noexcept;
    
    // Get spawn position from maze (position "0")
    glm::vec3 getMazeSpawnPosition() const noexcept { return mPlayerSpawnPosition; }

private:
    
    // Initialize 3D path tracer scene with spheres
    void initPathTracerScene() noexcept;
    
    // Sync physics bodies to sphere positions
    void syncPhysicsToSpheres() noexcept;
    
    // Chunk-based sphere management with maze integration
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
    
    // Maze cell data for sphere spawning
    struct MazeCell {
        int row, col;
        int distance;        // Base-10 distance from start
        float worldX, worldZ; // 3D world position
    };
    
    ChunkCoord getChunkCoord(const glm::vec3& position) const noexcept;
    void loadChunk(const ChunkCoord& coord) noexcept;
    void unloadChunk(const ChunkCoord& coord) noexcept;
    
    // Maze-based sphere generation
    void generateSpheresInChunkFromMaze(const ChunkCoord& coord) noexcept;
    std::string generateMazeForChunk(const ChunkCoord& coord) const noexcept;
    std::vector<MazeCell> parseMazeCells(const std::string& mazeStr, const ChunkCoord& coord) const noexcept;
    int parseBase36(const std::string& str) const noexcept;
    MaterialType getMaterialForDistance(int distance) const noexcept;
    
    // Helper to generate random float
    static float getRandomFloat(float low, float high) noexcept;

    enum class Layer
    {
        PARALLAX_BACK = 0,
        PARALLAX_MID = 1,
        PARALLAX_FORE = 2,
        BACKGROUND = 3,
        FOREGROUND = 4,
        LAYER_COUNT = 5
    };

    static constexpr auto FORCE_DUE_TO_GRAVITY = 9.8f;  // Positive Y is down in Box2D

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

    // Ball pointers for testing launch mechanics
    Ball* mBallNormal;
    Ball* mBallHeavy;
    Ball* mBallLight;
    Ball* mBallExplosive;

    bool mIsPanning;
    SDL_FPoint mLastMousePosition;

    std::unique_ptr<PostProcessingManager> mPostProcessingManager;
    
    // 3D Path tracer scene data
    std::vector<Sphere> mSpheres;
    std::vector<b2BodyId> mSphereBodyIds;  // Physics bodies for each sphere
    
    // Chunk management with maze integration
    static constexpr float CHUNK_SIZE = 100.0f;
    static constexpr int CHUNK_LOAD_RADIUS = 3;
    static constexpr int MAZE_ROWS = 20;    // Rows per chunk maze
    static constexpr int MAZE_COLS = 20;    // Columns per chunk maze
    static constexpr float CELL_SIZE = CHUNK_SIZE / static_cast<float>(MAZE_COLS); // 5 units per cell
    
    std::unordered_set<ChunkCoord, ChunkCoordHash> mLoadedChunks;
    std::unordered_map<ChunkCoord, std::vector<size_t>, ChunkCoordHash> mChunkSphereIndices;
    std::unordered_map<ChunkCoord, std::string, ChunkCoordHash> mChunkMazes; // Cache generated mazes
    glm::vec3 mLastChunkUpdatePosition;
    glm::vec3 mPlayerSpawnPosition;  // Spawn at maze position "0"
    
    static constexpr int TOTAL_SPHERES = 200;  // Initial reserved capacity
};

#endif // WORLD_HPP
