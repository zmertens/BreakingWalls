
#include "World.hpp"

#include "Ball.hpp"
#include "Entity.hpp"
#include "JsonUtils.hpp"
#include "Pathfinder.hpp"
#include "RenderWindow.hpp"
#include "ResourceManager.hpp"
#include "SpriteNode.hpp"
#include "Texture.hpp"
#include "Wall.hpp"

#include "Physics.hpp"

#include <MazeBuilder/create.h>

#include <box2d/box2d.h>

#include <SDL3/SDL.h>

#include <SFML/Network.hpp>

World::World(RenderWindow& window, FontManager& fonts, TextureManager& textures)
    : mWindow{window}
      , mWorldView{window.getView()}
      , mFonts{fonts}
      , mTextures{textures}
      , mSceneGraph{}
      , mSceneLayers{}
      , mWorldId{b2_nullWorldId}
      , mMazeWallsBodyId{b2_nullBodyId}
      , mCommandQueue{}
      , mPlayerPathfinder{nullptr}
      , mIsPanning{false}
      , mLastMousePosition{0.f, 0.f}
{
}

void World::init() noexcept
{
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = {0.0f, FORCE_DUE_TO_GRAVITY};

    mWorldId = b2CreateWorld(&worldDef);

    // Post-processing is disabled - requires OpenGL shader implementation
    // TODO: Implement post-processing effects using OpenGL compute shaders
    mPostProcessingManager = nullptr;

    mPlayerPathfinder = nullptr;

    buildScene();
}

void World::update(float dt)
{
    // Reset player velocity before processing commands (like SFML does)
    if (mPlayerPathfinder)
    {
        mPlayerPathfinder->setVelocity(0.f, 0.f);
        mWorldView.setCenter(mPlayerPathfinder->getPosition().x, mPlayerPathfinder->getPosition().y);
    }

    mWindow.setView(mWorldView);

    // Process commands from the queue BEFORE physics step (like SFML does)
    // This ensures player input forces are applied in the same frame
    while (!mCommandQueue.isEmpty())
    {
        Command command = mCommandQueue.pop();
        mSceneGraph.onCommand(command, dt);
    }

    // Step physics simulation (integrates forces applied by commands)
    if (b2World_IsValid(mWorldId))
    {
        b2World_Step(mWorldId, dt, 4);

#if defined(MAZE_DEBUG)
        static int stepCounter = 0;
        if (stepCounter++ % 60 == 0)
        {
            b2Counters counters = b2World_GetCounters(mWorldId);
            SDL_Log("Physics step #%d: bodies=%d, contacts=%d", stepCounter, counters.bodyCount, counters.contactCount);
        }
#endif

        // Poll contact events after stepping
        b2ContactEvents events = b2World_GetContactEvents(mWorldId);

        // Process begin contact events
        for (int i = 0; i < events.beginCount; ++i)
        {
            b2ContactBeginTouchEvent* beginEvent = events.beginEvents + i;
            b2BodyId bodyIdA = b2Shape_GetBody(beginEvent->shapeIdA);
            b2BodyId bodyIdB = b2Shape_GetBody(beginEvent->shapeIdB);

            if (b2Body_IsValid(bodyIdA) && b2Body_IsValid(bodyIdB))
            {
                void* userDataA = b2Body_GetUserData(bodyIdA);
                void* userDataB = b2Body_GetUserData(bodyIdB);

                auto* entityA = static_cast<Entity*>(userDataA);
                auto* entityB = static_cast<Entity*>(userDataB);

                if (entityA) entityA->onBeginContact(entityB);
                if (entityB) entityB->onBeginContact(entityA);
            }
        }

        // Process end contact events
        for (int i = 0; i < events.endCount; ++i)
        {
            b2ContactEndTouchEvent* endEvent = events.endEvents + i;
            b2BodyId bodyIdA = b2Shape_GetBody(endEvent->shapeIdA);
            b2BodyId bodyIdB = b2Shape_GetBody(endEvent->shapeIdB);

            if (b2Body_IsValid(bodyIdA) && b2Body_IsValid(bodyIdB))
            {
                void* userDataA = b2Body_GetUserData(bodyIdA);
                void* userDataB = b2Body_GetUserData(bodyIdB);

                Entity* entityA = static_cast<Entity*>(userDataA);
                Entity* entityB = static_cast<Entity*>(userDataB);

                if (entityA) entityA->onEndContact(entityB);
                if (entityB) entityB->onEndContact(entityA);
            }
        }
    }


    // Update scene graph (this calls Entity::updateCurrent which syncs transforms)
    mSceneGraph.update(dt, std::ref(mCommandQueue));
}

void World::draw() const noexcept
{
    // Post-processing disabled - render directly with OpenGL
    mWindow.draw(mSceneGraph);
}

CommandQueue& World::getCommandQueue() noexcept
{
    return mCommandQueue;
}

void World::handleEvent(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_KEY_DOWN:
        // Testing: Launch balls with number keys 1-4
        switch (event.key.key)
        {
        case SDLK_1:
            if (mBallNormal)
            {
                mBallNormal->launch(b2Vec2{2.0f, -5.0f});  // Upward and right impulse (negative Y = up)
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Launched Normal Ball");
            }
            break;
        case SDLK_2:
            if (mBallHeavy)
            {
                mBallHeavy->launch(b2Vec2{3.0f, -6.0f});  // Stronger impulse for heavier ball
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Launched Heavy Ball");
            }
            break;
        case SDLK_3:
            if (mBallLight)
            {
                mBallLight->launch(b2Vec2{1.5f, -4.0f});  // Lighter impulse for light ball
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Launched Light Ball");
            }
            break;
        case SDLK_4:
            if (mBallExplosive)
            {
                mBallExplosive->launch(b2Vec2{2.5f, -5.5f});
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Launched Explosive Ball");
            }
            break;
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (event.wheel.y > 0)
            mWorldView.zoom(1.1f);
        else if (event.wheel.y < 0)
            mWorldView.zoom(0.9f);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button == SDL_BUTTON_MIDDLE)
        {
            mIsPanning = true;
            mLastMousePosition = {static_cast<float>(event.button.x), static_cast<float>(event.button.y)};
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button == SDL_BUTTON_MIDDLE)
        {
            mIsPanning = false;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (mIsPanning)
        {
            SDL_FPoint currentMousePosition = {static_cast<float>(event.motion.x), static_cast<float>(event.motion.y)};
            SDL_FPoint delta = {currentMousePosition.x - mLastMousePosition.x, currentMousePosition.y - mLastMousePosition.y};
            mLastMousePosition = currentMousePosition;

            if (SDL_GetModState() & SDL_KMOD_SHIFT)
            {
                mWorldView.rotate(delta.x);
            }
            else
            {
                mWorldView.move(-delta.x, -delta.y);
            }
        }
        break;
    }
}

void World::destroyWorld()
{
    if (b2World_IsValid(mWorldId))
    {
        b2DestroyWorld(mWorldId);
        mWorldId = b2_nullWorldId;
    }
}

void World::setPlayer(Player* player)
{
    if (mPlayerPathfinder)
    {
        // mPlayerPathfinder->setPosition(player->)
    }
}

void World::buildScene()
{
    using std::cref;
    using std::make_unique;
    using std::size_t;
    using std::unique_ptr;

    for (size_t i = 0; i < static_cast<size_t>(Layer::LAYER_COUNT); ++i)
    {
        auto layer = make_unique<SceneNode>();
        mSceneLayers[i] = layer.get();
        mSceneGraph.attachChild(std::move(layer));
    }

    // Create parallax background layers using LEVEL_ONE texture
    // Each layer scrolls at a different speed to create depth effect
    // Negative speeds scroll left (like in the raylib example)

    // Static background with LEVEL_TWO
    auto mazeNode = make_unique<SpriteNode>(mTextures.get(Textures::ID::LEVEL_TWO));
    mazeNode->setPosition(0.f, 0.f);
    mSceneLayers[static_cast<size_t>(Layer::BACKGROUND)]->attachChild(std::move(mazeNode));

    auto leader = make_unique<Pathfinder>(Pathfinder::Type::ALLY, cref(mTextures));
    mPlayerPathfinder = leader.get();
    mPlayerPathfinder->setPosition(20.f, 20.f);  // Start near top-left of maze
    mSceneLayers[static_cast<size_t>(Layer::FOREGROUND)]->attachChild(std::move(leader));

    // Create and add ball entities - positioned to be visible
    // Note: Physics bodies created AFTER entities are added to scene
    auto ballNormal = make_unique<Ball>(Ball::Type::NORMAL, mTextures);
    mBallNormal = ballNormal.get();
    mSceneLayers[static_cast<size_t>(Layer::FOREGROUND)]->attachChild(std::move(ballNormal));

    auto ballHeavy = make_unique<Ball>(Ball::Type::HEAVY, mTextures);
    mBallHeavy = ballHeavy.get();
    mSceneLayers[static_cast<size_t>(Layer::FOREGROUND)]->attachChild(std::move(ballHeavy));

    auto ballLight = make_unique<Ball>(Ball::Type::LIGHT, mTextures);
    mBallLight = ballLight.get();
    mSceneLayers[static_cast<size_t>(Layer::FOREGROUND)]->attachChild(std::move(ballLight));

    auto ballExplosive = make_unique<Ball>(Ball::Type::EXPLOSIVE, mTextures);
    mBallExplosive = ballExplosive.get();
    mSceneLayers[static_cast<size_t>(Layer::FOREGROUND)]->attachChild(std::move(ballExplosive));


    auto wallHorizontal = make_unique<Wall>(Wall::Orientation::HORIZONTAL, mTextures);
    auto* wallHorizontalPtr = wallHorizontal.get();
    mSceneLayers[static_cast<size_t>(Layer::FOREGROUND)]->attachChild(std::move(wallHorizontal));
    wallHorizontalPtr->setPosition(60.0f, 150.0f); // Middle area of maze

    auto wallVertical = make_unique<Wall>(Wall::Orientation::VERTICAL, mTextures);
    auto* wallVerticalPtr = wallVertical.get();
    mSceneLayers[static_cast<size_t>(Layer::FOREGROUND)]->attachChild(std::move(wallVertical));
    wallVerticalPtr->setPosition(120.0f, 150.0f); // Middle area of maze

    // Create maze walls from LEVEL_TWO texture layout
    // Load the maze layout to extract wall positions
    if (b2World_IsValid(mWorldId))
    {
        // Create a single static body for all maze walls (like Box2D samples)
        b2BodyDef wallBodyDef = b2DefaultBodyDef();
        wallBodyDef.type = b2_staticBody;
        mMazeWallsBodyId = b2CreateBody(mWorldId, &wallBodyDef);
        
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.material.friction = 0.8f;
        shapeDef.material.restitution = 0.1f;
        
        // Create a box shape for each wall cell (10x10 pixels = 1x1 meters)
        float halfCellSize = physics::toMeters(5.0f);  // Half of 10 pixels
    }
}

