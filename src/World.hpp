#ifndef WORLD_HPP
#define WORLD_HPP

#include <box2d/box2d.h>

#include <array>
#include <vector>
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

private:
    
    // Initialize 3D path tracer scene with spheres
    void initPathTracerScene() noexcept;
    
    // Sync physics bodies to sphere positions
    void syncPhysicsToSpheres() noexcept;
    
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
    
    static constexpr int TOTAL_SPHERES = 200;
};

#endif // WORLD_HPP
