#include "World.hpp"

#include "Animation.hpp"
#include "Camera.hpp"
#include "JSONUtils.hpp"
#include "Material.hpp"
#include "RenderWindow.hpp"
#include "ResourceManager.hpp"
#include "Sphere.hpp"
#include "Texture.hpp"

#include <MazeBuilder/maze_builder.h>

#include <box2d/box2d.h>

#include <SDL3/SDL.h>

#include <SFML/Network.hpp>

#include <glm/glm.hpp>
#include <glad/glad.h>

#include <random>
#include <limits>
#include <algorithm>
#include <sstream>
#include <cctype>
#include <cmath>
#include <cstdint>

namespace
{
    constexpr std::uintptr_t kBodyTagSphereBase = 10;
    constexpr std::uintptr_t kBodyTagRunnerPlayer = 2;
    constexpr std::uintptr_t kBodyTagRunnerBounds = 3;

    float randomFloat(float low, float high)
    {
        static thread_local std::mt19937 rng{13371337u};
        std::uniform_real_distribution<float> distribution(low, high);
        return distribution(rng);
    }
}

World::World(RenderWindow &window, FontManager &fonts, TextureManager &textures, ShaderManager &shaders)
    : mWindow{window},
      mFonts{fonts},
      mTextures{textures},
      mShaders{shaders},
      mWorldId{b2_nullWorldId},
      mMazeWallsBodyId{b2_nullBodyId},
      mRunnerBoundsBodyId{b2_nullBodyId},
      mRunnerPlayerBodyId{b2_nullBodyId},
      mRunnerBoundNegZShapeId{b2_nullShapeId},
      mRunnerBoundPosZShapeId{b2_nullShapeId},
      mIsPanning{false},
      mLastMousePosition{0.f, 0.f},
      mGroundPlane{
          glm::vec3(0.0f, 0.0f, 0.0f),
          glm::vec3(0.0f, 1.0f, 0.0f),
          Material(glm::vec3(0.0f, 0.0f, 0.0f), Material::MaterialType::LAMBERTIAN, 0.0f, 1.0f)}, // Transparent/black ground to reveal pathtraced planet
      mLastChunkUpdatePosition{},
      mPlayerSpawnPosition{},
      mRunnerPlayerPosition{}
{
}

World::~World()
{
    destroyWorld();
}

void World::init() noexcept
{
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = {0.0f, 0.0f};

    mWorldId = b2CreateWorld(&worldDef);

    mPlayerSpawnPosition = glm::vec3(0.0f, 1.0f, 0.0f);

    initPathTracerScene();

    mSpheres.reserve(TOTAL_SPHERES * 4);
    mSphereBodyIds.reserve(TOTAL_SPHERES * 4);
    mRunnerCollisionEvents.reserve(32);

    mLastChunkUpdatePosition = glm::vec3(std::numeric_limits<float>::max());
    mRunnerSpawnTimer = 0.0f;

    // Initialize modern worker pool
    initWorkerPool();
}

void World::initWorkerPool() noexcept
{
    mWorkersShouldStop = false;

    // Create worker threads
    for (size_t i = 0; i < NUM_WORKER_THREADS; ++i)
    {
        mWorkerThreads.push_back(std::async(std::launch::async, [this, i]()
                                            {
                while (!mWorkersShouldStop.load(std::memory_order_acquire))
                {
                    std::packaged_task<ChunkWorkItem()> task;

                    {
                        std::unique_lock<std::mutex> lock(mWorkQueueMutex);

                        // Wait for work or shutdown signal
                        mWorkAvailable.wait(lock, [this]() {
                            return !mWorkQueue.empty() || mWorkersShouldStop.load(std::memory_order_acquire);
                            });

                        if (mWorkersShouldStop.load(std::memory_order_acquire) && mWorkQueue.empty())
                        {
                            break;
                        }

                        if (!mWorkQueue.empty())
                        {
                            task = std::move(mWorkQueue.front());
                            mWorkQueue.pop();
                        }
                    }

                    // Execute task outside the lock
                    if (task.valid())
                    {
                        task();
                    }
                } }));
    }
}

void World::shutdownWorkerPool() noexcept
{
    // Signal stop FIRST
    mWorkersShouldStop.store(true, std::memory_order_release);

    // Clear pending work to prevent new tasks
    {
        std::lock_guard<std::mutex> lock(mWorkQueueMutex);
        // Empty the queue by popping all items
        std::queue<std::packaged_task<ChunkWorkItem()>> empty;
        std::swap(mWorkQueue, empty);
    }

    // Wake all workers so they can see the stop signal
    mWorkAvailable.notify_all();

    // Clear pending chunks to abandon futures
    {
        std::lock_guard<std::mutex> lock(mCompletedChunksMutex);
        mPendingChunks.clear();
    }

    // Wait for all workers with timeout
    for (size_t i = 0; i < mWorkerThreads.size(); ++i)
    {
        auto &worker = mWorkerThreads[i];
        if (worker.valid())
        {
            try
            {
                // Wait with timeout
                auto status = worker.wait_for(std::chrono::seconds(5));
                if (status == std::future_status::timeout)
                {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "Worker thread %zu timed out waiting for shutdown", i);
                }
            }
            catch (const std::exception &e)
            {
                SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                             "Worker thread %zu shutdown exception: %s", i, e.what());
            }
        }
    }

    mWorkerThreads.clear();

    // Clear maze cache AFTER all workers stopped
    {
        std::lock_guard<std::mutex> lock(mMazeCacheMutex);
        mChunkMazes.clear();
    }
}

void World::submitChunkForGeneration(const ChunkCoord &coord) noexcept
{
    // Create packaged task for chunk generation
    std::packaged_task<ChunkWorkItem()> task(
        [this, coord]() -> ChunkWorkItem
        {
            return generateChunkAsync(coord);
        });

    // Get future before moving task
    auto future = task.get_future();

    // Add to work queue
    {
        std::lock_guard<std::mutex> lock(mWorkQueueMutex);
        mWorkQueue.push(std::move(task));
    }

    // Store future for later retrieval
    {
        std::lock_guard<std::mutex> lock(mCompletedChunksMutex);
        mPendingChunks.emplace_back(coord, std::move(future));
    }

    // Notify one worker
    mWorkAvailable.notify_one();
}

World::ChunkWorkItem World::generateChunkAsync(const ChunkCoord &coord) const noexcept
{
    using MaterialType = Material::MaterialType;

    ChunkWorkItem result;
    result.coord = coord;
    result.hasSpawnPosition = false;

    try
    {
        // Generate maze (thread-safe with mutex)
        std::string mazeStr = generateMazeForChunk(coord);
        if (mazeStr.empty())
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Worker: Empty maze for chunk (%d, %d)", coord.x, coord.z);
            return result;
        }

        // Parse maze cells - now returns spawn position in work item
        result.cells = parseMazeCells(mazeStr, coord, result.spawnPosition, result.hasSpawnPosition);

        // Runner mode now owns sphere spawning and movement directly in Box2D.
        // Chunk generation still provides maze cells/spawn metadata, but no static chunk spheres.
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                     "Worker: Exception generating chunk (%d, %d): %s",
                     coord.x, coord.z, e.what());
    }

    return result;
}

void World::processCompletedChunks() noexcept
{
    using MaterialType = Material::MaterialType;

    std::lock_guard<std::mutex> lock(mCompletedChunksMutex);

    auto it = mPendingChunks.begin();
    while (it != mPendingChunks.end())
    {
        auto &[coord, future] = *it;

        if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        {
            try
            {
                ChunkWorkItem workItem = future.get();

                // Update spawn position if found
                if (workItem.hasSpawnPosition)
                {
                    mPlayerSpawnPosition = workItem.spawnPosition;
                }

                std::vector<size_t> sphereIndices;

                // Create physics bodies for all spheres
                for (const auto &sphere : workItem.spheres)
                {
                    size_t sphereIndex = mSpheres.size();
                    sphereIndices.push_back(sphereIndex);

                    mSpheres.push_back(sphere);

                    if (b2World_IsValid(mWorldId))
                    {
                        b2BodyDef bodyDef = b2DefaultBodyDef();
                        bodyDef.type = b2_dynamicBody;
                        bodyDef.position = {sphere.getCenter().x, sphere.getCenter().z};
                        bodyDef.linearDamping = 0.2f;
                        bodyDef.angularDamping = 0.3f;
                        bodyDef.isBullet = sphere.getRadius() < 4.0f;

                        b2BodyId bodyId = b2CreateBody(mWorldId, &bodyDef);

                        if (b2Body_IsValid(bodyId))
                        {
                            b2ShapeDef shapeDef = b2DefaultShapeDef();
                            shapeDef.density = sphere.getMaterialType() == MaterialType::METAL ? 2.5f : (sphere.getMaterialType() == MaterialType::DIELECTRIC ? 0.8f : 1.0f);
                            shapeDef.enableContactEvents = true;
                            shapeDef.enableHitEvents = true;

                            b2Circle circle = {{0.0f, 0.0f}, sphere.getRadius()};
                            b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);

                            float friction = sphere.getMaterialType() == MaterialType::METAL ? 0.2f : 0.4f;
                            float restitution = sphere.getMaterialType() == MaterialType::DIELECTRIC ? 0.6f : 0.3f;
                            b2Shape_SetFriction(shapeId, friction);
                            b2Shape_SetRestitution(shapeId, restitution);
                            b2Body_SetAwake(bodyId, true);

                            b2Filter filter = b2Shape_GetFilter(shapeId);
                            filter.categoryBits = 0x0004;
                            filter.maskBits = 0xFFFF;
                            b2Shape_SetFilter(shapeId, filter);

                            b2Body_SetUserData(bodyId, reinterpret_cast<void *>(kBodyTagSphereBase + sphereIndex));
                            b2Shape_SetUserData(shapeId, reinterpret_cast<void *>(kBodyTagSphereBase + sphereIndex));
                        }

                        mSphereBodyIds.push_back(bodyId);
                    }
                    else
                    {
                        mSphereBodyIds.push_back(b2_nullBodyId);
                    }
                }

                // IMPORTANT: Mark chunk as loaded BEFORE updating indices
                // This prevents race condition where unloadChunk is called before we're done
                mLoadedChunks.insert(coord);
                mChunkSphereIndices[coord] = sphereIndices;
            }
            catch (const std::exception &e)
            {
                SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                             "World: Exception integrating chunk (%d, %d): %s",
                             coord.x, coord.z, e.what());
            }

            it = mPendingChunks.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void World::update(float dt)
{
    // Process any completed chunk generation work
    processCompletedChunks();

    if (b2World_IsValid(mWorldId))
    {
        if (b2Body_IsValid(mRunnerPlayerBodyId))
        {
            b2Body_SetTransform(mRunnerPlayerBodyId, {mRunnerPlayerPosition.x, mRunnerPlayerPosition.z}, b2MakeRot(0.0f));
            b2Body_SetLinearVelocity(mRunnerPlayerBodyId, {0.0f, 0.0f});
            b2Body_SetAwake(mRunnerPlayerBodyId, true);
        }

        updateRunnerSpheres(dt);
        b2World_Step(mWorldId, dt, 4);
        processRunnerContactEvents();
        pruneRunnerSpheres();

        // Sync physics body positions to 3D sphere positions for path tracer
        syncPhysicsToSpheres();
    }
}

void World::draw() const noexcept
{

}

void World::handleEvent(const SDL_Event &event)
{
    
}

void World::destroyWorld()
{
    // Shutdown worker pool FIRST - this clears all pending work
    shutdownWorkerPool();

    // Destroy all physics bodies BEFORE destroying world
    // Do this in reverse order to be safe
    for (auto it = mSphereBodyIds.rbegin(); it != mSphereBodyIds.rend(); ++it)
    {
        if (b2Body_IsValid(*it))
        {
            try
            {
                b2DestroyBody(*it);
            }
            catch (const std::exception &e)
            {
                SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error destroying body: %s", e.what());
            }
            catch (...)
            {
                // Ignore unknown errors during cleanup
            }
        }
    }
    mSphereBodyIds.clear();

    // Destroy physics world
    if (b2World_IsValid(mWorldId))
    {
        try
        {
            b2DestroyWorld(mWorldId);
        }
        catch (const std::exception &e)
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error destroying physics world: %s", e.what());
        }
        catch (...)
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Unknown error destroying physics world");
        }
        mWorldId = b2_nullWorldId;
    }

    // Clear all data structures
    mSpheres.clear();
    mChunkSphereIndices.clear();
    mLoadedChunks.clear();
    mRunnerCollisionEvents.clear();
    mRunnerBoundsBodyId = b2_nullBodyId;
    mRunnerPlayerBodyId = b2_nullBodyId;
    mRunnerBoundNegZShapeId = b2_nullShapeId;
    mRunnerBoundPosZShapeId = b2_nullShapeId;
}

void World::initPathTracerScene() noexcept
{
    mSpheres.clear();
    mSphereBodyIds.clear();
    mRunnerCollisionEvents.clear();
    mLoadedChunks.clear();
    mChunkSphereIndices.clear();

    if (!b2World_IsValid(mWorldId))
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "World: Cannot init path tracer - physics world invalid");
        return;
    }

    createRunnerBounds();

    b2BodyDef playerBodyDef = b2DefaultBodyDef();
    playerBodyDef.type = b2_kinematicBody;
    playerBodyDef.position = {mRunnerPlayerPosition.x, mRunnerPlayerPosition.z};
    playerBodyDef.fixedRotation = true;
    mRunnerPlayerBodyId = b2CreateBody(mWorldId, &playerBodyDef);

    b2ShapeDef playerShapeDef = b2DefaultShapeDef();
    playerShapeDef.density = 1.0f;
    playerShapeDef.enableContactEvents = true;

    b2Circle playerCircle = {{0.0f, 0.0f}, RUNNER_PLAYER_RADIUS};
    b2ShapeId playerShapeId = b2CreateCircleShape(mRunnerPlayerBodyId, &playerShapeDef, &playerCircle);
    b2Shape_SetFriction(playerShapeId, 0.0f);
    b2Shape_SetRestitution(playerShapeId, 0.0f);
    b2Shape_SetUserData(playerShapeId, reinterpret_cast<void *>(kBodyTagRunnerPlayer));
    b2Body_SetUserData(mRunnerPlayerBodyId, reinterpret_cast<void *>(kBodyTagRunnerPlayer));

    mRunnerSpawnTimer = 0.0f;
    constexpr int kInitialRunnerSphereCount = 10;
    for (int i = 0; i < kInitialRunnerSphereCount; ++i)
    {
        spawnRunnerSphere();
    }
}

void World::syncPhysicsToSpheres() noexcept
{
    // Sync physics body positions back to 3D sphere positions
    for (size_t i = 0; i < mSpheres.size() && i < mSphereBodyIds.size(); ++i)
    {
        b2BodyId bodyId = mSphereBodyIds[i];
        if (b2Body_IsValid(bodyId))
        {
            b2Vec2 pos = b2Body_GetPosition(bodyId);

            // Update sphere center: map 2D physics (x, y) back to 3D (x, y_original, z)
            // Keep Y coordinate from original sphere, use physics Y as Z coordinate
            float originalY = mSpheres[i].getCenter().y;
            glm::vec3 newCenter = mSpheres[i].getCenter();
            newCenter.x = pos.x;
            newCenter.y = originalY;
            newCenter.z = pos.y;
            mSpheres[i].setCenter(newCenter);
        }
    }
}

void World::setRunnerPlayerPosition(const glm::vec3 &playerPosition) noexcept
{
    mRunnerPlayerPosition = playerPosition;
}

void World::setRunnerTuning(float strafeLimit, float spawnAheadDistance, float sphereSpeed) noexcept
{
    mRunnerStrafeLimit = std::max(4.0f, strafeLimit);
    mRunnerSpawnAheadDistance = std::max(30.0f, spawnAheadDistance);
    mRunnerSphereSpeed = std::max(8.0f, sphereSpeed);

    createRunnerBounds();
}

std::vector<World::RunnerCollisionEvent> World::consumeRunnerCollisionEvents() noexcept
{
    std::vector<RunnerCollisionEvent> events;
    events.swap(mRunnerCollisionEvents);
    return events;
}

void World::createRunnerBounds() noexcept
{
    if (!b2World_IsValid(mWorldId))
    {
        return;
    }

    if (!b2Body_IsValid(mRunnerBoundsBodyId))
    {
        b2BodyDef boundsBodyDef = b2DefaultBodyDef();
        boundsBodyDef.type = b2_staticBody;
        boundsBodyDef.position = {0.0f, 0.0f};
        mRunnerBoundsBodyId = b2CreateBody(mWorldId, &boundsBodyDef);
        b2Body_SetUserData(mRunnerBoundsBodyId, reinterpret_cast<void *>(kBodyTagRunnerBounds));
    }

    if (!b2Body_IsValid(mRunnerBoundsBodyId))
    {
        return;
    }

    if (b2Shape_IsValid(mRunnerBoundNegZShapeId))
    {
        b2DestroyShape(mRunnerBoundNegZShapeId, false);
        mRunnerBoundNegZShapeId = b2_nullShapeId;
    }
    if (b2Shape_IsValid(mRunnerBoundPosZShapeId))
    {
        b2DestroyShape(mRunnerBoundPosZShapeId, false);
        mRunnerBoundPosZShapeId = b2_nullShapeId;
    }

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = 0.0f;

    b2Segment negZ = {{-RUNNER_BOUNDS_HALF_WIDTH, -mRunnerStrafeLimit}, {RUNNER_BOUNDS_HALF_WIDTH, -mRunnerStrafeLimit}};
    b2Segment posZ = {{-RUNNER_BOUNDS_HALF_WIDTH, mRunnerStrafeLimit}, {RUNNER_BOUNDS_HALF_WIDTH, mRunnerStrafeLimit}};

    mRunnerBoundNegZShapeId = b2CreateSegmentShape(mRunnerBoundsBodyId, &shapeDef, &negZ);
    mRunnerBoundPosZShapeId = b2CreateSegmentShape(mRunnerBoundsBodyId, &shapeDef, &posZ);
    b2Shape_SetFriction(mRunnerBoundNegZShapeId, 0.9f);
    b2Shape_SetRestitution(mRunnerBoundNegZShapeId, 0.2f);
    b2Shape_SetFriction(mRunnerBoundPosZShapeId, 0.9f);
    b2Shape_SetRestitution(mRunnerBoundPosZShapeId, 0.2f);
}

void World::updateRunnerSpheres(float dt) noexcept
{
    if (dt <= 0.0f)
    {
        return;
    }

    mRunnerSpawnTimer += dt;
    while (mRunnerSpawnTimer >= RUNNER_SPAWN_INTERVAL_SECONDS)
    {
        mRunnerSpawnTimer -= RUNNER_SPAWN_INTERVAL_SECONDS;
        spawnRunnerSphere();
    }
}

void World::spawnRunnerSphere() noexcept
{
    using MaterialType = Material::MaterialType;

    if (!b2World_IsValid(mWorldId))
    {
        return;
    }

    const float zLimit = std::max(1.0f, mRunnerStrafeLimit - RUNNER_SPAWN_Z_MARGIN);
    const float spawnZ = randomFloat(-zLimit, zLimit);
    const float spawnX = mRunnerPlayerPosition.x + mRunnerSpawnAheadDistance + randomFloat(-6.0f, 6.0f);
    const float radius = randomFloat(0.9f, 2.2f);

    const float materialPick = randomFloat(0.0f, 1.0f);
    MaterialType matType = MaterialType::LAMBERTIAN;
    glm::vec3 albedo = glm::vec3(0.55f, 0.55f, 0.55f);
    float fuzz = 0.0f;
    float refractIdx = 1.5f;
    float density = 1.0f;

    if (materialPick < 0.25f)
    {
        matType = MaterialType::METAL;
        albedo = glm::vec3(0.8f, 0.78f, 0.72f);
        fuzz = 0.08f;
        density = 2.2f;
    }
    else if (materialPick > 0.78f)
    {
        matType = MaterialType::DIELECTRIC;
        albedo = glm::vec3(1.0f);
        density = 0.9f;
    }

    glm::vec3 center(spawnX, radius, spawnZ);
    mSpheres.emplace_back(center, radius, albedo, matType, fuzz, refractIdx);

    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = {center.x, center.z};
    bodyDef.linearDamping = 0.05f;
    bodyDef.angularDamping = 0.12f;
    bodyDef.fixedRotation = false;

    b2BodyId bodyId = b2CreateBody(mWorldId, &bodyDef);
    if (!b2Body_IsValid(bodyId))
    {
        mSpheres.pop_back();
        return;
    }

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = density;
    shapeDef.enableContactEvents = true;
    shapeDef.enableHitEvents = true;

    b2Circle circle = {{0.0f, 0.0f}, radius};
    b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
    b2Shape_SetFriction(shapeId, 0.6f);
    b2Shape_SetRestitution(shapeId, matType == MaterialType::DIELECTRIC ? 0.45f : 0.18f);

    const size_t sphereIndex = mSpheres.size() - 1;
    b2Body_SetUserData(bodyId, reinterpret_cast<void *>(kBodyTagSphereBase + sphereIndex));
    b2Shape_SetUserData(shapeId, reinterpret_cast<void *>(kBodyTagSphereBase + sphereIndex));

    glm::vec2 towardPlayer = glm::vec2(mRunnerPlayerPosition.x - center.x, mRunnerPlayerPosition.z - center.z);
    if (glm::length(towardPlayer) < 0.001f)
    {
        towardPlayer = glm::vec2(-1.0f, 0.0f);
    }
    else
    {
        towardPlayer = glm::normalize(towardPlayer);
    }

    const float speed = mRunnerSphereSpeed + randomFloat(-5.0f, 7.0f);
    b2Body_SetLinearVelocity(bodyId, {towardPlayer.x * speed, towardPlayer.y * speed});
    b2Body_SetAwake(bodyId, true);

    mSphereBodyIds.push_back(bodyId);
}

void World::processRunnerContactEvents() noexcept
{
    if (!b2World_IsValid(mWorldId))
    {
        return;
    }

    const b2ContactEvents contactEvents = b2World_GetContactEvents(mWorldId);

    auto isPlayerBody = [](std::uintptr_t tag) noexcept
    {
        return tag == kBodyTagRunnerPlayer;
    };

    auto sphereIndexFromTag = [](std::uintptr_t tag) noexcept -> size_t
    {
        return static_cast<size_t>(tag - kBodyTagSphereBase);
    };

    for (int i = 0; i < contactEvents.beginCount; ++i)
    {
        const b2ContactBeginTouchEvent &event = contactEvents.beginEvents[i];

        if (!b2Shape_IsValid(event.shapeIdA) || !b2Shape_IsValid(event.shapeIdB))
        {
            continue;
        }

        const b2BodyId bodyA = b2Shape_GetBody(event.shapeIdA);
        const b2BodyId bodyB = b2Shape_GetBody(event.shapeIdB);
        const std::uintptr_t tagA = reinterpret_cast<std::uintptr_t>(b2Body_GetUserData(bodyA));
        const std::uintptr_t tagB = reinterpret_cast<std::uintptr_t>(b2Body_GetUserData(bodyB));

        bool playerA = isPlayerBody(tagA);
        bool playerB = isPlayerBody(tagB);
        if (playerA == playerB)
        {
            continue;
        }

        const std::uintptr_t sphereTag = playerA ? tagB : tagA;
        if (sphereTag < kBodyTagSphereBase)
        {
            continue;
        }

        const size_t sphereIndex = sphereIndexFromTag(sphereTag);
        if (sphereIndex >= mSpheres.size())
        {
            continue;
        }

        RunnerCollisionEvent runnerEvent;
        runnerEvent.materialType = mSpheres[sphereIndex].getMaterialType();
        runnerEvent.impactSpeed = event.manifold.pointCount > 0 ? event.manifold.points[0].normalVelocity : 0.0f;
        runnerEvent.worldPosition = mSpheres[sphereIndex].getCenter();
        mRunnerCollisionEvents.push_back(runnerEvent);
    }
}

void World::pruneRunnerSpheres() noexcept
{
    size_t idx = RUNNER_PERSISTENT_SPHERES;
    while (idx < mSpheres.size() && idx < mSphereBodyIds.size())
    {
        b2BodyId bodyId = mSphereBodyIds[idx];
        if (!b2Body_IsValid(bodyId))
        {
            const size_t lastIdx = mSpheres.size() - 1;
            if (idx != lastIdx)
            {
                mSpheres[idx] = mSpheres[lastIdx];
                mSphereBodyIds[idx] = mSphereBodyIds[lastIdx];
                if (b2Body_IsValid(mSphereBodyIds[idx]))
                {
                    b2Body_SetUserData(mSphereBodyIds[idx], reinterpret_cast<void *>(kBodyTagSphereBase + idx));
                }
            }
            mSpheres.pop_back();
            mSphereBodyIds.pop_back();
            continue;
        }

        const b2Vec2 pos = b2Body_GetPosition(bodyId);
        const bool behindPlayer = pos.x < (mRunnerPlayerPosition.x - RUNNER_DESPAWN_BEHIND_DISTANCE);
        const bool outsideZBounds = std::fabs(pos.y) > (mRunnerStrafeLimit + 12.0f);

        if (behindPlayer || outsideZBounds)
        {
            b2DestroyBody(bodyId);

            const size_t lastIdx = mSpheres.size() - 1;
            if (idx != lastIdx)
            {
                mSpheres[idx] = mSpheres[lastIdx];
                mSphereBodyIds[idx] = mSphereBodyIds[lastIdx];
                if (b2Body_IsValid(mSphereBodyIds[idx]))
                {
                    b2Body_SetUserData(mSphereBodyIds[idx], reinterpret_cast<void *>(kBodyTagSphereBase + idx));
                }
            }

            mSpheres.pop_back();
            mSphereBodyIds.pop_back();
            continue;
        }

        ++idx;
    }
}

World::ChunkCoord World::getChunkCoord(const glm::vec3 &position) const noexcept
{
    return ChunkCoord{
        static_cast<int>(std::floor(position.x / CHUNK_SIZE)),
        static_cast<int>(std::floor(position.z / CHUNK_SIZE))};
}

void World::updateSphereChunks(const glm::vec3 &cameraPosition) noexcept
{
    ChunkCoord currentChunk = getChunkCoord(cameraPosition);
    ChunkCoord lastChunk = getChunkCoord(mLastChunkUpdatePosition);

    // Update if camera moved to a different chunk OR this is the first call
    bool firstCall = (mLastChunkUpdatePosition.x == std::numeric_limits<float>::max());
    bool chunkChanged = (currentChunk.x != lastChunk.x || currentChunk.z != lastChunk.z);

    if (!firstCall && !chunkChanged)
    {
        return; // Still in same chunk, no update needed
    }

    mLastChunkUpdatePosition = cameraPosition;

    // Determine chunks that should be loaded
    std::unordered_set<ChunkCoord, ChunkCoordHash> desiredChunks;
    for (int dx = -CHUNK_LOAD_RADIUS; dx <= CHUNK_LOAD_RADIUS; ++dx)
    {
        for (int dz = -CHUNK_LOAD_RADIUS; dz <= CHUNK_LOAD_RADIUS; ++dz)
        {
            desiredChunks.insert(ChunkCoord{currentChunk.x + dx, currentChunk.z + dz});
        }
    }

    // Unload chunks that are out of range
    std::vector<ChunkCoord> chunksToUnload;
    for (const auto &chunk : mLoadedChunks)
    {
        if (desiredChunks.find(chunk) == desiredChunks.end())
        {
            chunksToUnload.push_back(chunk);
        }
    }

    int unloadedCount = 0;
    for (const auto &chunk : chunksToUnload)
    {
        unloadChunk(chunk);
        unloadedCount++;
    }

    // Load new chunks
    int loadedCount = 0;
    for (const auto &chunk : desiredChunks)
    {
        if (mLoadedChunks.find(chunk) == mLoadedChunks.end())
        {
            loadChunk(chunk);
            loadedCount++;
        }
    }
}

void World::loadChunk(const ChunkCoord &coord) noexcept
{
    // Don't submit if already loaded or pending
    if (mLoadedChunks.find(coord) != mLoadedChunks.end())
    {
        return;
    }

    // Check if already pending
    {
        std::lock_guard<std::mutex> lock(mCompletedChunksMutex);
        for (const auto &[pendingCoord, _] : mPendingChunks)
        {
            if (pendingCoord == coord)
            {
                return; // Already pending generation
            }
        }
    }

    // Submit chunk for async generation
    submitChunkForGeneration(coord);
}

void World::unloadChunk(const ChunkCoord &coord) noexcept
{
    auto it = mChunkSphereIndices.find(coord);
    if (it == mChunkSphereIndices.end())
    {
        return;
    }

    // Get indices to remove
    std::vector<size_t> indicesToRemove = it->second;

    // CRITICAL FIX: Track which indices get remapped during swap-and-pop
    std::unordered_map<size_t, size_t> indexRemapping;

    // Sort in reverse to remove from back to front
    std::sort(indicesToRemove.rbegin(), indicesToRemove.rend());

    for (size_t idx : indicesToRemove)
    {
        if (idx >= mSpheres.size() || idx >= mSphereBodyIds.size())
        {
            continue;
        }

        // Destroy physics body if valid
        b2BodyId bodyId = mSphereBodyIds[idx];
        if (b2Body_IsValid(bodyId))
        {
            b2DestroyBody(bodyId);
        }

        // Swap with last element
        size_t lastIdx = mSpheres.size() - 1;
        if (idx < lastIdx)
        {
            // Remember that the element at lastIdx is now at idx
            indexRemapping[lastIdx] = idx;

            mSpheres[idx] = mSpheres[lastIdx];
            mSphereBodyIds[idx] = mSphereBodyIds[lastIdx];

            if (b2Body_IsValid(mSphereBodyIds[idx]))
            {
                b2Body_SetUserData(mSphereBodyIds[idx], reinterpret_cast<void *>(kBodyTagSphereBase + idx));
            }
        }

        // Remove last element
        mSpheres.pop_back();
        mSphereBodyIds.pop_back();
    }

    // Update ALL chunk indices based on remapping
    for (auto &[chunkCoord, indices] : mChunkSphereIndices)
    {
        if (chunkCoord == coord)
        {
            continue;
        }

        for (size_t &sphereIdx : indices)
        {
            // If this index was remapped, update it
            auto remapIt = indexRemapping.find(sphereIdx);
            if (remapIt != indexRemapping.end())
            {
                sphereIdx = remapIt->second;
            }
        }
    }

    // Remove this chunk's entry
    mChunkSphereIndices.erase(it);
    mLoadedChunks.erase(coord);
}

std::string World::generateMazeForChunk(const ChunkCoord &coord) const noexcept
{
    try
    {
        // Thread-safe cache access
        {
            std::lock_guard<std::mutex> lock(mMazeCacheMutex);
            auto it = mChunkMazes.find(coord);
            if (it != mChunkMazes.end())
            {
                return it->second;
            }
        }

        unsigned int seed = static_cast<unsigned int>(coord.x * 73856093 ^ coord.z * 19349663);

        auto mazeStr = mazes::create(
            mazes::configurator()
                .rows(MAZE_ROWS)
                .columns(MAZE_COLS)
                .distances(false)
                .seed(seed));

        // Cache the result (thread-safe)
        {
            std::lock_guard<std::mutex> lock(mMazeCacheMutex);
            mChunkMazes[coord] = mazeStr;
        }

        return mazeStr;
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "World: Failed to generate maze: %s", e.what());
        return "";
    }
}

std::vector<World::MazeCell> World::parseMazeCells(const std::string &mazeStr, const ChunkCoord &coord,
                                                   glm::vec3 &outSpawnPosition, bool &outHasSpawn) const noexcept
{
    std::vector<MazeCell> cells;
    outHasSpawn = false;

    if (mazeStr.empty())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "World: Empty maze string for chunk (%d, %d)", coord.x, coord.z);
        return cells;
    }

    float chunkWorldX = coord.x * CHUNK_SIZE;
    float chunkWorldZ = coord.z * CHUNK_SIZE;

    for (int row = 0; row < MAZE_ROWS; ++row)
    {
        for (int col = 0; col < MAZE_COLS; ++col)
        {
            int centerRow = MAZE_ROWS / 2;
            int centerCol = MAZE_COLS / 2;
            int distance = std::abs(row - centerRow) + std::abs(col - centerCol);

            float worldX = chunkWorldX + (col * CELL_SIZE) + (CELL_SIZE * 0.5f);
            float worldZ = chunkWorldZ + (row * CELL_SIZE) + (CELL_SIZE * 0.5f);

            cells.push_back(MazeCell{row, col, distance, worldX, worldZ});

            // Track spawn in work item instead of const_cast
            if (row == centerRow && col == centerCol && coord.x == 0 && coord.z == 0)
            {
                outSpawnPosition = glm::vec3(worldX, 10.0f, worldZ);
                outHasSpawn = true;
            }
        }
    }

    return cells;
}

Material::MaterialType World::getMaterialForDistance(int distance) const noexcept
{
    using MaterialType = Material::MaterialType;

    if (distance % 4 == 0 && distance != 0)
    {
        return MaterialType::METAL;
    }
    else if (distance % 6 == 0 && distance != 0)
    {
        return MaterialType::DIELECTRIC;
    }
    else
    {
        return MaterialType::LAMBERTIAN;
    }
}

// ============================================================================
// Character rendering for third-person mode
// ============================================================================

#include "Player.hpp"
#include "Camera.hpp"
#include "GLSDLHelper.hpp"
#include "Shader.hpp"

void World::renderPlayerCharacter(const Player &player, const Camera &camera) const noexcept
{
    // Only render in third-person mode
    if (camera.getMode() != CameraMode::THIRD_PERSON)
    {
        return;
    }

    // Get the character sprite sheet texture
    const Texture *spriteSheet = getCharacterSpriteSheet();
    if (!spriteSheet || spriteSheet->get() == 0)
    {
        static bool loggedOnce = false;
        if (!loggedOnce)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "World: Character sprite sheet not loaded (ID: %d)",
                        static_cast<int>(Textures::ID::CHARACTER_SPRITE_SHEET));
            loggedOnce = true;
        }
        return;
    }

    // Get billboard shader
    Shader *billboardShader = nullptr;
    try
    {
        billboardShader = &mShaders.get(Shaders::ID::GLSL_BILLBOARD_SPRITE);
    }
    catch (const std::exception &e)
    {
        static bool loggedOnce = false;
        if (!loggedOnce)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "World: Billboard shader not available: %s", e.what());
            loggedOnce = true;
        }
        return;
    }

    // Get animation frame from player
    AnimationRect frame = player.getCurrentAnimationFrame();

    // Get player position - this is where the character sprite will be rendered
    glm::vec3 playerPos = player.getPosition();

    // Offset the billboard slightly in front of the player position
    // so it's visible when camera is looking at player
    // The camera looks at mFollowTarget + (0,1,0), so render at same position

    // Billboard half-size (character size in world units)
    float halfSize = 3.0f;

    // Get window dimensions from SDL
    int windowWidth = 1280;
    int windowHeight = 720;
    SDL_Window *sdlWindow = mWindow.getSDLWindow();
    if (sdlWindow)
    {
        SDL_GetWindowSize(sdlWindow, &windowWidth, &windowHeight);
    }

    // Get view and projection matrices from camera
    float aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
    glm::mat4 viewMatrix = camera.getLookAt();
    glm::mat4 projMatrix = camera.getPerspective(aspectRatio);

    // Render the character as a billboard sprite using geometry shader
    GLSDLHelper::renderBillboardSprite(
        *billboardShader,
        spriteSheet->get(),
        frame,
        playerPos,
        halfSize,
        viewMatrix,
        projMatrix,
        spriteSheet->getWidth(),
        spriteSheet->getHeight());
}

void World::renderCharacterFromState(const glm::vec3 &position, float facing,
                                     const AnimationRect &frame, const Camera &camera) const noexcept
{
    // Only render in third-person mode (or always for remote players)
    // For remote players, we always want to render them

    // Get the character sprite sheet texture
    const Texture *spriteSheet = getCharacterSpriteSheet();
    if (!spriteSheet || spriteSheet->get() == 0)
    {
        return;
    }

    // Get billboard shader
    Shader *billboardShader = nullptr;
    try
    {
        billboardShader = &mShaders.get(Shaders::ID::GLSL_BILLBOARD_SPRITE);
    }
    catch (const std::exception &)
    {
        return;
    }

    // Billboard half-size
    float halfSize = 3.0f;

    // Get window dimensions
    int windowWidth = 1280;
    int windowHeight = 720;
    SDL_Window *sdlWindow = mWindow.getSDLWindow();
    if (sdlWindow)
    {
        SDL_GetWindowSize(sdlWindow, &windowWidth, &windowHeight);
    }

    // Get view and projection matrices from camera
    float aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
    glm::mat4 viewMatrix = camera.getLookAt();
    glm::mat4 projMatrix = camera.getPerspective(aspectRatio);

    // Render the character as a billboard sprite
    GLSDLHelper::renderBillboardSprite(
        *billboardShader,
        spriteSheet->get(),
        frame,
        position,
        halfSize,
        viewMatrix,
        projMatrix,
        spriteSheet->getWidth(),
        spriteSheet->getHeight());
}

const Texture *World::getCharacterSpriteSheet() const noexcept
{
    try
    {
        return &mTextures.get(Textures::ID::CHARACTER_SPRITE_SHEET);
    }
    catch (const std::exception &)
    {
        return nullptr;
    }
}
