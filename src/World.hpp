#ifndef WORLD_HPP
#define WORLD_HPP

#include <box2d/box2d.h>

#include <array>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <glm/glm.hpp>

#include "CommandQueue.hpp"
#include "PostProcessingManager.hpp"
#include "RenderWindow.hpp"
#include "ResourceIdentifiers.hpp"
#include "SceneNode.hpp"
#include "View.hpp"

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

private:
    
    // Initialize 3D path tracer scene with spheres
    void initPathTracerScene() noexcept;
    
    // Sync physics bodies to sphere positions
    void syncPhysicsToSpheres() noexcept;
    
    // Chunk-based sphere management
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
    
    ChunkCoord getChunkCoord(const glm::vec3& position) const noexcept;
    void loadChunk(const ChunkCoord& coord) noexcept;
    void unloadChunk(const ChunkCoord& coord) noexcept;
    void generateSpheresInChunk(const ChunkCoord& coord) noexcept;
    
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
    
    // Chunk management
    static constexpr float CHUNK_SIZE = 100.0f;
    static constexpr int CHUNK_LOAD_RADIUS = 3;  // Load chunks within 3 chunk radius
    static constexpr int SPHERES_PER_CHUNK = 15;
    std::unordered_set<ChunkCoord, ChunkCoordHash> mLoadedChunks;
    std::unordered_map<ChunkCoord, std::vector<size_t>, ChunkCoordHash> mChunkSphereIndices;
    glm::vec3 mLastChunkUpdatePosition;
    
    static constexpr int TOTAL_SPHERES = 200;  // Initial reserved capacity
};

#endif // WORLD_HPP
