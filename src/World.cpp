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
#include "Sphere.hpp"

#include <MazeBuilder/create.h>

#include <box2d/box2d.h>

#include <SDL3/SDL.h>

#include <SFML/Network.hpp>

#include <glm/glm.hpp>
#include <glad/glad.h>

#include <random>

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
    
    // Initialize 3D path tracer scene with physics-enabled spheres
    initPathTracerScene();
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
        
        // Sync physics body positions to 3D sphere positions for path tracer
        syncPhysicsToSpheres();
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
                // Increased impulse values for visible effect (in meters)
                mBallNormal->launch(b2Vec2{8.0f, -15.0f});  // Upward and right impulse (negative Y = up)
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Launched Normal Ball with impulse (8.0, -15.0)");
            }
            break;
        case SDLK_2:
            if (mBallHeavy)
            {
                mBallHeavy->launch(b2Vec2{10.0f, -20.0f});  // Stronger impulse for heavier ball
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Launched Heavy Ball with impulse (10.0, -20.0)");
            }
            break;
        case SDLK_3:
            if (mBallLight)
            {
                mBallLight->launch(b2Vec2{6.0f, -12.0f});  // Lighter impulse for light ball
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Launched Light Ball with impulse (6.0, -12.0)");
            }
            break;
        case SDLK_4:
            if (mBallExplosive)
            {
                mBallExplosive->launch(b2Vec2{9.0f, -18.0f});
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Launched Explosive Ball with impulse (9.0, -18.0)");
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

    auto leader = make_unique<Pathfinder>(Pathfinder::Type::ALLY, cref(mTextures));
    mPlayerPathfinder = leader.get();
    mPlayerPathfinder->setPosition(100.f, 100.f);  // Start position in screen space
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

    // Create physics bodies for all entities now that the world is valid
    if (b2World_IsValid(mWorldId))
    {
        // Create player physics body (dynamic kinematic body for free-flying)
        b2BodyDef playerBodyDef = b2DefaultBodyDef();
        playerBodyDef.type = b2_kinematicBody;  // Kinematic for WASD free-flying (no gravity)
        playerBodyDef.position = physics::toMetersVec({100.0f, 100.0f});
        playerBodyDef.linearDamping = 5.0f;  // High damping for responsive control
        playerBodyDef.fixedRotation = true;  // Don't rotate the player
        
        mPlayerPathfinder->createBody(mWorldId, &playerBodyDef);
        
        b2BodyId playerBodyId = mPlayerPathfinder->getBodyId();
        if (b2Body_IsValid(playerBodyId))
        {
            // Create a capsule shape for the player
            b2ShapeDef playerShapeDef = b2DefaultShapeDef();
            playerShapeDef.density = 1.0f;
            
            b2Capsule playerCapsule;
            playerCapsule.center1 = {0.0f, -physics::toMeters(10.0f)};  // 10 pixels up
            playerCapsule.center2 = {0.0f, physics::toMeters(10.0f)};   // 10 pixels down
            playerCapsule.radius = physics::toMeters(8.0f);              // 8 pixel radius
            
            b2ShapeId playerShapeId = b2CreateCapsuleShape(playerBodyId, &playerShapeDef, &playerCapsule);
            b2Shape_SetFriction(playerShapeId, 0.3f);
            b2Body_SetAwake(playerBodyId, true);
            
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Created player physics body (kinematic for free-flying)");
        }

        // Position balls in a visible arrangement and create their physics bodies
        // Balls positioned higher so they fall into view
        b2Vec2 ballPositions[] = {
            {150.0f, 50.0f},   // Normal ball
            {200.0f, 50.0f},   // Heavy ball
            {250.0f, 50.0f},   // Light ball
            {300.0f, 50.0f}    // Explosive ball
        };

        Ball* balls[] = {mBallNormal, mBallHeavy, mBallLight, mBallExplosive};
        
        for (int i = 0; i < 4; ++i)
        {
            balls[i]->createPhysicsBody(mWorldId, ballPositions[i]);
            
            // Set collision filtering so balls can collide with each other and everything else
            b2BodyId ballBodyId = balls[i]->getBodyId();
            if (b2Body_IsValid(ballBodyId))
            {
                // Get the first shape from the body
                int shapeCount = b2Body_GetShapeCount(ballBodyId);
                if (shapeCount > 0)
                {
                    b2ShapeId shapeIds[1];
                    b2Body_GetShapes(ballBodyId, shapeIds, 1);
                    
                    // Set filter to allow ball-to-ball collisions
                    b2Filter filter = b2Shape_GetFilter(shapeIds[0]);
                    filter.categoryBits = 0x0002;  // Ball category
                    filter.maskBits = 0xFFFF;       // Collides with everything
                    b2Shape_SetFilter(shapeIds[0], filter);
                }
            }
        }

        // Create a single static body for all maze walls (like Box2D samples)
        b2BodyDef wallBodyDef = b2DefaultBodyDef();
        wallBodyDef.type = b2_staticBody;
        mMazeWallsBodyId = b2CreateBody(mWorldId, &wallBodyDef);
        
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        
        // Create a box shape for each wall cell (10x10 pixels = 1x1 meters)
        float halfCellSize = physics::toMeters(5.0f);  // Half of 10 pixels
        
        // Add ground at the bottom of the screen for balls to land on
        b2Polygon groundBox = b2MakeBox(physics::toMeters(400.0f), physics::toMeters(10.0f));
        b2Vec2 groundPosition = physics::toMetersVec({400.0f, 580.0f});
        b2ShapeId groundShapeId = b2CreatePolygonShape(mMazeWallsBodyId, &shapeDef, &groundBox);
        b2Shape_SetFriction(groundShapeId, 0.8f);
        b2Shape_SetRestitution(groundShapeId, 0.1f);
        b2Body_SetTransform(mMazeWallsBodyId, groundPosition, b2Rot_identity);
        
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Created ground and maze walls");
    }
}

float World::getRandomFloat(float low, float high) noexcept
{
    static std::random_device rd;
    static std::mt19937 mt(rd());
    std::uniform_real_distribution<float> dist(low, high);
    return dist(mt);
}

void World::initPathTracerScene() noexcept
{
    SDL_Log("World: Initializing 3D Path Tracer scene with physics...");
    
    mSpheres.clear();
    mSphereBodyIds.clear();
    
    if (!b2World_IsValid(mWorldId))
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "World: Cannot init path tracer - physics world invalid");
        return;
    }
    
    // Ground sphere (large Lambertian) - static body
    mSpheres.emplace_back(
        glm::vec3(0.0f, -1000.0f, 0.0f),  // center
        1000.0f,                           // radius
        glm::vec3(0.5f, 0.5f, 0.5f),      // color (gray)
        MaterialType::LAMBERTIAN,          // type
        0.0f,                              // fuzz
        0.0f                               // refractive index
    );
    
    // Ground has no physics body (too large, acts as infinite plane)
    mSphereBodyIds.push_back(b2_nullBodyId);

    // Center glass sphere - dynamic physics body
    mSpheres.emplace_back(
        glm::vec3(0.0f, 1.0f, 0.0f),
        1.0f,
        glm::vec3(1.0f, 1.0f, 1.0f),
        MaterialType::DIELECTRIC,
        0.0f,
        1.5f  // Glass refractive index
    );
    
    // Create physics body for center sphere
    {
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position = {0.0f, 1.0f};  // 3D center maps to 2D position
        bodyDef.linearDamping = 0.1f;
        bodyDef.angularDamping = 0.3f;
        
        b2BodyId bodyId = b2CreateBody(mWorldId, &bodyDef);
        
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = 0.8f;  // Glass-like density
        
        b2Circle circle = {{0.0f, 0.0f}, 1.0f};
        b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
        b2Shape_SetFriction(shapeId, 0.3f);
        b2Shape_SetRestitution(shapeId, 0.4f);
        b2Body_SetAwake(bodyId, true);
        
        mSphereBodyIds.push_back(bodyId);
    }

    // Left diffuse sphere - dynamic physics body
    mSpheres.emplace_back(
        glm::vec3(-4.0f, 1.0f, 0.0f),
        1.0f,
        glm::vec3(0.4f, 0.2f, 0.1f),
        MaterialType::LAMBERTIAN,
        0.0f,
        0.0f
    );
    
    {
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position = {-4.0f, 1.0f};
        bodyDef.linearDamping = 0.2f;
        bodyDef.angularDamping = 0.3f;
        
        b2BodyId bodyId = b2CreateBody(mWorldId, &bodyDef);
        
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = 1.0f;
        
        b2Circle circle = {{0.0f, 0.0f}, 1.0f};
        b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
        b2Shape_SetFriction(shapeId, 0.5f);
        b2Shape_SetRestitution(shapeId, 0.3f);
        b2Body_SetAwake(bodyId, true);
        
        mSphereBodyIds.push_back(bodyId);
    }

    // Right metal sphere - dynamic physics body
    mSpheres.emplace_back(
        glm::vec3(4.0f, 1.0f, 0.0f),
        1.0f,
        glm::vec3(0.7f, 0.6f, 0.5f),
        MaterialType::METAL,
        0.0f,  // No fuzz for sharp reflection
        0.0f
    );
    
    {
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position = {4.0f, 1.0f};
        bodyDef.linearDamping = 0.1f;
        bodyDef.angularDamping = 0.3f;
        
        b2BodyId bodyId = b2CreateBody(mWorldId, &bodyDef);
        
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = 2.5f;  // Metal is heavier
        
        b2Circle circle = {{0.0f, 0.0f}, 1.0f};
        b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
        b2Shape_SetFriction(shapeId, 0.2f);
        b2Shape_SetRestitution(shapeId, 0.5f);
        b2Body_SetAwake(bodyId, true);
        
        mSphereBodyIds.push_back(bodyId);
    }

    // Generate random small spheres in a circle pattern with physics bodies
    float imgCircleRadius = 125.0f;
    float offset = 15.25f;

    for (unsigned int index = 4; index < TOTAL_SPHERES; ++index)
    {
        float matChoice = getRandomFloat(0.0f, 1.0f);

        float angle = static_cast<float>(index - 4) / static_cast<float>(TOTAL_SPHERES - 4) * 360.0f;
        float angleRad = glm::radians(angle);
        float displacement = getRandomFloat(-offset, offset);

        float xpos = std::sin(angleRad) * imgCircleRadius + displacement;
        displacement = getRandomFloat(-offset, offset);
        float y = std::abs(displacement) * 7.5f;
        displacement = getRandomFloat(-offset, offset);
        float z = std::cos(angleRad) * imgCircleRadius + displacement;

        glm::vec3 center = glm::vec3(xpos, y, z);
        float radius = getRandomFloat(5.0f, 12.0f);

        MaterialType matType;
        float density;
        float fuzzOrRefract = 0.0f;

        if (matChoice < 0.7f) {
            // Lambertian (diffuse) - 70% chance
            glm::vec3 albedo(
                getRandomFloat(0.1f, 0.9f),
                getRandomFloat(0.1f, 0.9f),
                getRandomFloat(0.1f, 0.9f)
            );
            mSpheres.emplace_back(center, radius, albedo, MaterialType::LAMBERTIAN, 0.0f, 0.0f);
            matType = MaterialType::LAMBERTIAN;
            density = 1.0f;
        } else if (matChoice < 0.9f) {
            // Metal - 20% chance
            glm::vec3 albedo(
                getRandomFloat(0.5f, 1.0f),
                getRandomFloat(0.5f, 1.0f),
                getRandomFloat(0.5f, 1.0f)
            );
            float fuzz = getRandomFloat(0.0f, 0.5f);
            mSpheres.emplace_back(center, radius, albedo, MaterialType::METAL, fuzz, 0.0f);
            matType = MaterialType::METAL;
            density = 2.0f;
            fuzzOrRefract = fuzz;
        } else {
            // Glass (dielectric) - 10% chance
            mSpheres.emplace_back(center, radius, glm::vec3(1.0f), MaterialType::DIELECTRIC, 0.0f, 1.5f);
            matType = MaterialType::DIELECTRIC;
            density = 0.8f;
            fuzzOrRefract = 1.5f;
        }
        
        // Create physics body for small sphere
        // Map 3D position (x, z) to 2D physics (x, y)
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position = {xpos, z};  // Use X and Z from 3D as X and Y in 2D physics
        bodyDef.linearDamping = 0.2f;
        bodyDef.angularDamping = 0.3f;
        bodyDef.isBullet = radius < 7.0f;  // Small fast spheres need CCD
        
        b2BodyId bodyId = b2CreateBody(mWorldId, &bodyDef);
        
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = density;
        
        b2Circle circle = {{0.0f, 0.0f}, radius};
        b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
        
        // Set friction and restitution after creation
        float friction = matType == MaterialType::METAL ? 0.2f : 0.4f;
        float restitution = matType == MaterialType::DIELECTRIC ? 0.6f : 0.3f;
        b2Shape_SetFriction(shapeId, friction);
        b2Shape_SetRestitution(shapeId, restitution);
        b2Body_SetAwake(bodyId, true);
        
        // Set collision filtering for sphere-to-sphere collision
        b2Filter filter = b2Shape_GetFilter(shapeId);
        filter.categoryBits = 0x0004;  // Sphere category
        filter.maskBits = 0xFFFF;       // Collides with everything
        b2Shape_SetFilter(shapeId, filter);
        
        mSphereBodyIds.push_back(bodyId);
    }

    SDL_Log("World: Path tracer scene initialized with %zu spheres (%zu with physics bodies)", 
            mSpheres.size(), mSphereBodyIds.size());
}

void World::syncPhysicsToSpheres() noexcept
{
    // Sync physics body positions back to 3D sphere positions
    // Skip index 0 (ground sphere has no body)
    for (size_t i = 1; i < mSpheres.size() && i < mSphereBodyIds.size(); ++i)
    {
        b2BodyId bodyId = mSphereBodyIds[i];
        if (b2Body_IsValid(bodyId))
        {
            b2Vec2 pos = b2Body_GetPosition(bodyId);
            
            // Update sphere center: map 2D physics (x, y) back to 3D (x, y_original, z)
            // Keep Y coordinate from original sphere, use physics Y as Z coordinate
            float originalY = mSpheres[i].center.y;
            mSpheres[i].center.x = pos.x;
            mSpheres[i].center.y = originalY;  // Preserve height
            mSpheres[i].center.z = pos.y;       // Physics Y becomes Z
        }
    }
}

