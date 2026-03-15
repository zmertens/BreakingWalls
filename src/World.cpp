#include "World.hpp"

#include "Animation.hpp"
#include "Camera.hpp"
#include "JSONUtils.hpp"
#include "Level.hpp"
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
#include <array>

namespace
{
    constexpr std::uintptr_t kBodyTagSphereBase = 10;
    constexpr std::uintptr_t kBodyTagMazeWall = 4;

    float randomFloat(float low, float high)
    {
        static thread_local std::mt19937 rng{13371337u};
        std::uniform_real_distribution<float> distribution(low, high);
        return distribution(rng);
    }
}

World::World(RenderWindow &window, FontManager &fonts, TextureManager &textures, ShaderManager &shaders, LevelsManager &levels)
    : mWindow{window},
      mFonts{fonts},
      mTextures{textures},
      mShaders{shaders},
      mLevels{levels},
      mWorldId{b2_nullWorldId},
      mMazeWallsBodyId{b2_nullBodyId},
      mIsPanning{false},
      mLastMousePosition{0.f, 0.f},
      mGroundPlane{
          glm::vec3(0.0f, 0.0f, 0.0f),
          glm::vec3(0.0f, 1.0f, 0.0f),
          Material(glm::vec3(0.0f, 0.0f, 0.0f), Material::MaterialType::LAMBERTIAN, 0.0f, 1.0f)}, // Transparent/black ground to reveal pathtraced planet
      mLastChunkUpdatePosition{},
      mPlayerSpawnPosition{}
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

    // Spawn in positive X/Z for grid shader visibility
    mPlayerSpawnPosition = glm::vec3(100.0f, 1.0f, 0.0f);

    initPathTracerScene();

    mSpheres.reserve(TOTAL_SPHERES * 4);
    mSphereBodyIds.reserve(TOTAL_SPHERES * 4);
    mWallBreakQueue.reserve(16);

    mLastChunkUpdatePosition = glm::vec3(std::numeric_limits<float>::max());

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

        buildMazeWallSpheres(result.cells, result.spheres);

        // Generate pickup spheres at cells where distance % 5 == 0
        buildPickupSpheres(result.cells, result.pickupSpheres, coord);
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

                // Create physics bodies for chunk-generated maze walls
                for (const auto &sphere : workItem.spheres)
                {
                    size_t sphereIndex = mSpheres.size();
                    sphereIndices.push_back(sphereIndex);

                    mSpheres.push_back(sphere);

                    if (b2World_IsValid(mWorldId))
                    {
                        b2BodyDef bodyDef = b2DefaultBodyDef();
                        bodyDef.type = b2_staticBody;
                        bodyDef.position = {sphere.getCenter().x, sphere.getCenter().z};

                        b2BodyId bodyId = b2CreateBody(mWorldId, &bodyDef);

                        if (b2Body_IsValid(bodyId))
                        {
                            b2ShapeDef shapeDef = b2DefaultShapeDef();
                            shapeDef.density = 0.0f;
                            shapeDef.enableContactEvents = true;
                            shapeDef.enableHitEvents = false;

                            b2Circle circle = {{0.0f, 0.0f}, sphere.getRadius()};
                            b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);

                            b2Shape_SetFriction(shapeId, 0.9f);
                            b2Shape_SetRestitution(shapeId, 0.0f);
                            b2Body_SetAwake(bodyId, false);

                            b2Filter filter = b2Shape_GetFilter(shapeId);
                            filter.categoryBits = 0x0004;
                            filter.maskBits = 0xFFFF;
                            b2Shape_SetFilter(shapeId, filter);

                            b2Body_SetUserData(bodyId, reinterpret_cast<void *>(kBodyTagMazeWall));
                            b2Shape_SetUserData(shapeId, reinterpret_cast<void *>(kBodyTagMazeWall));
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
                mPersistentSphereCount += sphereIndices.size();

                // Integrate pickup spheres from the work item
                for (auto &pickup : workItem.pickupSpheres)
                {
                    mPickupSpheres.push_back(pickup);
                }
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
        b2World_Step(mWorldId, dt, 4);
        breakQueuedWalls();

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

    // Destroy player body before other bodies
    if (b2Body_IsValid(mPlayerBodyId))
    {
        b2DestroyBody(mPlayerBodyId);
        mPlayerBodyId = b2_nullBodyId;
    }

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
    mWallBreakQueue.clear();
    mPickupSpheres.clear();
    mPersistentSphereCount = 0;
    mScore = 0;
}

void World::initPathTracerScene() noexcept
{
    mSpheres.clear();
    mSphereBodyIds.clear();
    mWallBreakQueue.clear();
    mLoadedChunks.clear();
    mChunkSphereIndices.clear();
    mPersistentSphereCount = 0;

    if (!b2World_IsValid(mWorldId))
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "World: Cannot init path tracer - physics world invalid");
        return;
    }
}

void World::syncPhysicsToSpheres() noexcept
{
    // Sync physics body positions (in 2D theta-phi space) back to 3D sphere positions on planet surface
    for (size_t i = 0; i < mSpheres.size() && i < mSphereBodyIds.size(); ++i)
    {
        b2BodyId bodyId = mSphereBodyIds[i];
        if (b2Body_IsValid(bodyId))
        {
            b2Vec2 pos = b2Body_GetPosition(bodyId);
            
            // pos.x = theta * radius (arc length around planet)
            // pos.y = lateral arc offset
            const float theta = pos.x / mPlanetRadius;
            const float lateralArc = pos.y;
            const float phi = lateralArc / mPlanetRadius;
            
            const float sphereRadius = mSpheres[i].getRadius();
            const float r = mPlanetRadius + sphereRadius;
            
            const float cosPhi = std::cos(phi);
            const float sinPhi = std::sin(phi);
            const float cosTheta = std::cos(theta);
            const float sinTheta = std::sin(theta);
            
            // Calculate 3D position on planet surface
            glm::vec3 newCenter = mPlanetCenter + glm::vec3(
                r * cosPhi * cosTheta,
                r * sinPhi,
                r * cosPhi * sinTheta
            );
            
            mSpheres[i].setCenter(newCenter);
        }
    }
}

void World::breakQueuedWalls() noexcept
{
    for (const b2BodyId &bodyToRemove : mWallBreakQueue)
    {
        // Find this body in the persistent wall zone [0, mPersistentSphereCount)
        size_t wallIdx = SIZE_MAX;
        for (size_t i = 0; i < mPersistentSphereCount; ++i)
        {
            if (B2_ID_EQUALS(mSphereBodyIds[i], bodyToRemove))
            {
                wallIdx = i;
                break;
            }
        }
        if (wallIdx == SIZE_MAX || mPersistentSphereCount == 0)
            continue;

        // Destroy the physics body
        if (b2Body_IsValid(mSphereBodyIds[wallIdx]))
            b2DestroyBody(mSphereBodyIds[wallIdx]);

        const size_t lastPersistent = mPersistentSphereCount - 1;

        // Swap the hit wall with the last persistent (wall) sphere so the dead
        // body ends up at index lastPersistent, just past the new boundary.
        if (wallIdx != lastPersistent)
        {
            std::swap(mSpheres[wallIdx], mSpheres[lastPersistent]);
            std::swap(mSphereBodyIds[wallIdx], mSphereBodyIds[lastPersistent]);
        }

        // Update chunk sphere index lists:
        //   - remove wallIdx (broken wall leaves the chunk)
        //   - remap lastPersistent â†’ wallIdx (the wall that moved to fill the gap)
        for (auto &[coord, indices] : mChunkSphereIndices)
        {
            for (size_t &sidx : indices)
            {
                if (sidx == wallIdx)
                    sidx = SIZE_MAX; // mark for erasure
                else if (sidx == lastPersistent && wallIdx != lastPersistent)
                    sidx = wallIdx;  // sphere moved here
            }
            auto eraseIt = std::remove(indices.begin(), indices.end(), SIZE_MAX);
            indices.erase(eraseIt, indices.end());
        }

        // Shrink persistent zone; dead body at lastPersistent will be cleaned
        // up by a subsequent cleanup pass (invalid body check).
        --mPersistentSphereCount;
    }
    mWallBreakQueue.clear();
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

    size_t removedCount = 0;

    for (size_t idx : indicesToRemove)
    {
        if (idx >= mSpheres.size() || idx >= mSphereBodyIds.size())
        {
            continue;
        }

        ++removedCount;

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
                const std::uintptr_t prevTag = reinterpret_cast<std::uintptr_t>(b2Body_GetUserData(mSphereBodyIds[idx]));
                if (prevTag != kBodyTagMazeWall)
                {
                    b2Body_SetUserData(mSphereBodyIds[idx], reinterpret_cast<void *>(kBodyTagSphereBase + idx));
                }
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
    mPersistentSphereCount = (removedCount >= mPersistentSphereCount) ? 0 : (mPersistentSphereCount - removedCount);
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

        std::size_t baseSeed = 0x9E3779B97F4A7C15ull;
        std::string levelData;
        try
        {
            levelData = mLevels.get(Levels::ID::LEVEL_ONE).getData();
            if (!levelData.empty())
            {
                baseSeed ^= std::hash<std::string>{}(levelData);
            }
        }
        catch (const std::exception &)
        {
            // Fall back to generated maze text when level resources are unavailable.
        }

        const std::size_t chunkHash =
            (static_cast<std::size_t>(coord.x) * 73856093u) ^
            (static_cast<std::size_t>(coord.z) * 19349663u);
        const unsigned int seed = static_cast<unsigned int>(baseSeed ^ chunkHash);

        auto mazeStr = (coord.x == 0 && coord.z == 0 && !levelData.empty())
            ? levelData
            : mazes::create(
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

    const int cellCount = MAZE_ROWS * MAZE_COLS;
    std::vector<bool> visited(static_cast<std::size_t>(cellCount), false);
    std::vector<bool> wallNorth(static_cast<std::size_t>(cellCount), true);
    std::vector<bool> wallSouth(static_cast<std::size_t>(cellCount), true);
    std::vector<bool> wallEast(static_cast<std::size_t>(cellCount), true);
    std::vector<bool> wallWest(static_cast<std::size_t>(cellCount), true);

    auto cellIndex = [](int row, int col) noexcept
    {
        return row * MAZE_COLS + col;
    };

    const std::size_t coordHash =
        (static_cast<std::size_t>(coord.x) * 73856093u) ^
        (static_cast<std::size_t>(coord.z) * 19349663u);
    const std::size_t mazeHash = std::hash<std::string>{}(mazeStr);
    std::mt19937 rng(static_cast<uint32_t>(mazeHash ^ coordHash));

    std::vector<int> stack;
    stack.reserve(static_cast<std::size_t>(cellCount));
    const int start = static_cast<int>((mazeHash ^ coordHash) % static_cast<std::size_t>(cellCount));
    stack.push_back(start);
    visited[static_cast<std::size_t>(start)] = true;

    while (!stack.empty())
    {
        const int current = stack.back();
        const int row = current / MAZE_COLS;
        const int col = current % MAZE_COLS;

        std::array<int, 4> dirs{-1, -1, -1, -1};
        int count = 0;

        if (row > 0)
        {
            const int idx = cellIndex(row - 1, col);
            if (!visited[static_cast<std::size_t>(idx)])
            {
                dirs[count++] = 0; // north
            }
        }
        if (row < MAZE_ROWS - 1)
        {
            const int idx = cellIndex(row + 1, col);
            if (!visited[static_cast<std::size_t>(idx)])
            {
                dirs[count++] = 1; // south
            }
        }
        if (col < MAZE_COLS - 1)
        {
            const int idx = cellIndex(row, col + 1);
            if (!visited[static_cast<std::size_t>(idx)])
            {
                dirs[count++] = 2; // east
            }
        }
        if (col > 0)
        {
            const int idx = cellIndex(row, col - 1);
            if (!visited[static_cast<std::size_t>(idx)])
            {
                dirs[count++] = 3; // west
            }
        }

        if (count == 0)
        {
            stack.pop_back();
            continue;
        }

        std::uniform_int_distribution<int> pick(0, count - 1);
        const int dir = dirs[pick(rng)];

        int nextRow = row;
        int nextCol = col;
        if (dir == 0)
        {
            nextRow -= 1;
        }
        else if (dir == 1)
        {
            nextRow += 1;
        }
        else if (dir == 2)
        {
            nextCol += 1;
        }
        else
        {
            nextCol -= 1;
        }

        const int next = cellIndex(nextRow, nextCol);
        if (dir == 0)
        {
            wallNorth[static_cast<std::size_t>(current)] = false;
            wallSouth[static_cast<std::size_t>(next)] = false;
        }
        else if (dir == 1)
        {
            wallSouth[static_cast<std::size_t>(current)] = false;
            wallNorth[static_cast<std::size_t>(next)] = false;
        }
        else if (dir == 2)
        {
            wallEast[static_cast<std::size_t>(current)] = false;
            wallWest[static_cast<std::size_t>(next)] = false;
        }
        else
        {
            wallWest[static_cast<std::size_t>(current)] = false;
            wallEast[static_cast<std::size_t>(next)] = false;
        }

        visited[static_cast<std::size_t>(next)] = true;
        stack.push_back(next);
    }

    for (int row = 0; row < MAZE_ROWS; ++row)
    {
        for (int col = 0; col < MAZE_COLS; ++col)
        {
            int centerRow = MAZE_ROWS / 2;
            int centerCol = MAZE_COLS / 2;
            int distance = std::abs(row - centerRow) + std::abs(col - centerCol);
            const int idx = cellIndex(row, col);

            float worldX = chunkWorldX + (col * CELL_SIZE) + (CELL_SIZE * 0.5f);
            float worldZ = chunkWorldZ + (row * CELL_SIZE) + (CELL_SIZE * 0.5f);

            cells.push_back(MazeCell{row, col, distance, worldX, worldZ,
                                     wallNorth[static_cast<std::size_t>(idx)],
                                     wallSouth[static_cast<std::size_t>(idx)],
                                     wallEast[static_cast<std::size_t>(idx)],
                                     wallWest[static_cast<std::size_t>(idx)]});

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

void World::buildMazeWallSpheres(const std::vector<MazeCell> &cells, std::vector<Sphere> &outSpheres) const noexcept
{
    using MaterialType = Material::MaterialType;

    if (cells.empty())
    {
        return;
    }

    constexpr std::size_t kMaxMazeWallSpheres = 260;
    constexpr float kWallRadius = 1.45f;
    constexpr float kWallSpacing = 2.2f;

    auto appendSegment = [&](float x0, float z0, float x1, float z1, const glm::vec3 &baseColor,
                             Material::MaterialType matType = MaterialType::LAMBERTIAN,
                             float fuzz = 0.0f, float ior = 1.5f)
    {
        auto toRunnerArc = [this](float x, float z) noexcept -> glm::vec2
        {
            const float forwardArc = x + 60.0f;
            const float centeredZ = z - (CHUNK_SIZE * 0.5f);
            const float lateralScale = (2.0f * std::max(8.0f, 35.0f)) / CHUNK_SIZE;
            const float lateralArc = centeredZ * lateralScale;
            return glm::vec2(forwardArc, lateralArc);
        };

        const glm::vec2 arc0 = toRunnerArc(x0, z0);
        const glm::vec2 arc1 = toRunnerArc(x1, z1);

        const float dx = arc1.x - arc0.x;
        const float dz = arc1.y - arc0.y;
        const float length = std::sqrt(dx * dx + dz * dz);
        const int steps = std::max(1, static_cast<int>(std::ceil(length / kWallSpacing)));

        for (int step = 0; step <= steps; ++step)
        {
            if (outSpheres.size() >= kMaxMazeWallSpheres)
            {
                return;
            }

            const float t = static_cast<float>(step) / static_cast<float>(steps);
            const float x = arc0.x + dx * t;
            const float z = arc0.y + dz * t;
            outSpheres.emplace_back(
                glm::vec3(x, 0.0f, z),
                kWallRadius,
                baseColor,
                matType,
                fuzz,
                ior);
            outSpheres.back().setTextureBlend(0.0f);
        }
    };

    for (const MazeCell &cell : cells)
    {
        if (outSpheres.size() >= kMaxMazeWallSpheres)
        {
            break;
        }

        const float halfCell = CELL_SIZE * 0.5f;
        const bool checkerCell = ((cell.row + cell.col) % 2 == 0);
        const glm::vec3 wallColor = checkerCell
            ? glm::vec3(0.14f, 0.16f, 0.20f)
            : glm::vec3(0.23f, 0.25f, 0.30f);

        // Assign material type based on distance for reflection/refraction
        const Material::MaterialType matType = getMaterialForDistance(cell.distance);
        const float fuzz = (matType == Material::MaterialType::METAL) ? 0.1f : 0.0f;
        const float ior = (matType == Material::MaterialType::DIELECTRIC) ? 1.52f : 1.5f;

        if (cell.wallNorth)
        {
            appendSegment(cell.worldX - halfCell, cell.worldZ - halfCell,
                          cell.worldX + halfCell, cell.worldZ - halfCell,
                          wallColor, matType, fuzz, ior);
        }

        if (cell.wallWest)
        {
            appendSegment(cell.worldX - halfCell, cell.worldZ - halfCell,
                          cell.worldX - halfCell, cell.worldZ + halfCell,
                          wallColor, matType, fuzz, ior);
        }

        if (cell.col == (MAZE_COLS - 1) && cell.wallEast)
        {
            appendSegment(cell.worldX + halfCell, cell.worldZ - halfCell,
                          cell.worldX + halfCell, cell.worldZ + halfCell,
                          wallColor, matType, fuzz, ior);
        }

        if (cell.row == (MAZE_ROWS - 1) && cell.wallSouth)
        {
            appendSegment(cell.worldX - halfCell, cell.worldZ + halfCell,
                          cell.worldX + halfCell, cell.worldZ + halfCell,
                          wallColor, matType, fuzz, ior);
        }
    }
}

Material::MaterialType World::getMaterialForDistance(int distance) const noexcept
{
    using MaterialType = Material::MaterialType;

    // Enhanced material assignment for reflection/refraction effects
    if (distance % 3 == 0 && distance != 0)
    {
        return MaterialType::METAL;  // Reflective walls
    }
    else if (distance % 5 == 0 && distance != 0)
    {
        return MaterialType::DIELECTRIC;  // Refractive/glass walls
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
    // Only render in third-person mode (billboard fallback for non-GLTF views)
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
                                     const AnimationRect &frame,
                                     const Camera &camera,
                                     bool useWorldAxes,
                                     const glm::vec3 &rightAxisWS,
                                     const glm::vec3 &upAxisWS,
                                     bool doubleSided) const noexcept
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

    const float sheetWidth = static_cast<float>(std::max(1, spriteSheet->getWidth()));
    const float sheetHeight = static_cast<float>(std::max(1, spriteSheet->getHeight()));
    const float uMin = static_cast<float>(frame.left) / sheetWidth;
    const float vMin = static_cast<float>(frame.top) / sheetHeight;
    const float uMax = static_cast<float>(frame.left + frame.width) / sheetWidth;
    const float vMax = static_cast<float>(frame.top + frame.height) / sheetHeight;

    const bool flipX = (facing >= 90.0f);

    GLSDLHelper::renderBillboardSpriteUV(
        *billboardShader,
        spriteSheet->get(),
        glm::vec4(uMin, vMin, uMax, vMax),
        position,
        halfSize,
        viewMatrix,
        projMatrix,
        glm::vec4(1.0f),
        flipX,
        true,
        false,
        glm::vec2(0.0f),
        useWorldAxes,
        rightAxisWS,
        upAxisWS,
        doubleSided);
}

void World::renderTexturedBillboard(const glm::vec3 &position,
                                    float halfSize,
                                    const glm::vec2 &halfSizeXY,
                                    Textures::ID textureId,
                                    bool useWorldAxes,
                                    const glm::vec3 &rightAxisWS,
                                    const glm::vec3 &upAxisWS,
                                    bool doubleSided,
                                    const Camera &camera) const noexcept
{
    const Texture *texture = nullptr;
    try
    {
        texture = &mTextures.get(textureId);
    }
    catch (const std::exception &)
    {
        return;
    }

    if (!texture || texture->get() == 0)
    {
        return;
    }

    Shader *billboardShader = nullptr;
    try
    {
        billboardShader = &mShaders.get(Shaders::ID::GLSL_BILLBOARD_SPRITE);
    }
    catch (const std::exception &)
    {
        return;
    }

    int windowWidth = 1280;
    int windowHeight = 720;
    SDL_Window *sdlWindow = mWindow.getSDLWindow();
    if (sdlWindow)
    {
        SDL_GetWindowSize(sdlWindow, &windowWidth, &windowHeight);
    }

    const int safeHeight = std::max(1, windowHeight);
    const float aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(safeHeight);
    const glm::mat4 viewMatrix = camera.getLookAt();
    const glm::mat4 projMatrix = camera.getPerspective(aspectRatio);

    GLSDLHelper::renderBillboardSpriteUV(
        *billboardShader,
        texture->get(),
        glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
        position,
        halfSize,
        viewMatrix,
        projMatrix,
        glm::vec4(1.0f),
        false,
        false,
        false,
        halfSizeXY,
        useWorldAxes,
        rightAxisWS,
        upAxisWS,
        doubleSided);
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

glm::vec3 World::projectOntoSphere(glm::vec2 flatPos) const noexcept
{
    // flatPos.x: arc length along circumference
    // flatPos.y: forward/back position perpendicular to X (becomes Z on sphere surface)
    // Convert arc length to angle
    float angle = flatPos.x / mPlanetRadius;
    
    // Position on sphere surface:
    // X = center.x + radius * cos(angle), Z = center.z + radius * sin(angle)
    // Y = center.y + forward offset clamped to sphere edges
    // (simplified: Y stays roughly constant as we run forward/back around sphere)
    float x = mPlanetCenter.x + mPlanetRadius * glm::cos(angle);
    float z = mPlanetCenter.z + mPlanetRadius * glm::sin(angle);
    float y = mPlanetCenter.y; // Keep at center height
    
    return glm::vec3(x, y, z);
}

glm::vec2 World::projectFromSphere(const glm::vec3 &spherePos) const noexcept
{
    // Extract angle from sphere position
    glm::vec2 relPos = glm::vec2(spherePos.x - mPlanetCenter.x, spherePos.z - mPlanetCenter.z);
    float angle = glm::atan(relPos.y, relPos.x);
    float arcLength = angle * mPlanetRadius;
    
    // Keep Z component as-is (forward/back strafe dimension)
    return glm::vec2(arcLength, spherePos.z);
}

void World::buildPickupSpheres(const std::vector<MazeCell> &cells, std::vector<PickupSphere> &outPickups, const ChunkCoord &coord) const noexcept
{
    // Seed RNG deterministically per chunk
    const std::size_t coordHash =
        (static_cast<std::size_t>(coord.x) * 73856093u) ^
        (static_cast<std::size_t>(coord.z) * 19349663u);
    std::mt19937 rng(static_cast<uint32_t>(coordHash ^ 0xDEADBEEFu));
    std::uniform_int_distribution<int> valueDist(-25, 40);

    for (const MazeCell &cell : cells)
    {
        // Use the distance map: cells with distance divisible by 5 get a pickup sphere
        if (cell.distance > 0 && cell.distance % 5 == 0)
        {
            // Use the sphere center position as the pickup location
            // Convert from maze 2D coordinates to world position
            float toRunnerArcX = cell.worldX + 60.0f;
            float centeredZ = cell.worldZ - (CHUNK_SIZE * 0.5f);
            float lateralScale = (2.0f * std::max(8.0f, 35.0f)) / CHUNK_SIZE;
            float lateralArc = centeredZ * lateralScale;

            PickupSphere pickup;
            pickup.position = glm::vec3(toRunnerArcX, 1.5f, lateralArc);
            pickup.value = valueDist(rng);
            pickup.collected = false;
            outPickups.push_back(pickup);
        }
    }
}

int World::collectNearbyPickups(const glm::vec3 &playerPos, float collectRadius) noexcept
{
    int totalPoints = 0;
    const float radiusSq = collectRadius * collectRadius;

    for (auto &pickup : mPickupSpheres)
    {
        if (pickup.collected)
            continue;

        const glm::vec3 diff = pickup.position - playerPos;
        const float distSq = glm::dot(diff, diff);

        if (distSq < radiusSq)
        {
            pickup.collected = true;
            totalPoints += pickup.value;
            mScore += pickup.value;
        }
    }

    return totalPoints;
}

void World::createPlayerBody(const glm::vec3 &position) noexcept
{
    if (!b2World_IsValid(mWorldId))
        return;

    // Destroy existing player body if present
    if (b2Body_IsValid(mPlayerBodyId))
    {
        b2DestroyBody(mPlayerBodyId);
        mPlayerBodyId = b2_nullBodyId;
    }

    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = {position.x, position.z};
    bodyDef.linearDamping = 2.0f;
    bodyDef.fixedRotation = true;

    mPlayerBodyId = b2CreateBody(mWorldId, &bodyDef);

    if (b2Body_IsValid(mPlayerBodyId))
    {
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = 1.0f;
        shapeDef.enableContactEvents = true;

        b2Circle circle = {{0.0f, 0.0f}, 1.0f};
        b2CreateCircleShape(mPlayerBodyId, &shapeDef, &circle);
    }
}

void World::applyPlayerJumpImpulse(float impulse) noexcept
{
    if (b2Body_IsValid(mPlayerBodyId))
    {
        // Apply upward impulse in 2D (mapped to forward in the game)
        b2Vec2 imp = {0.0f, impulse};
        b2Body_ApplyLinearImpulseToCenter(mPlayerBodyId, imp, true);
    }
}

glm::vec2 World::getPlayerVelocity() const noexcept
{
    if (b2Body_IsValid(mPlayerBodyId))
    {
        b2Vec2 vel = b2Body_GetLinearVelocity(mPlayerBodyId);
        return glm::vec2(vel.x, vel.y);
    }
    return glm::vec2(0.0f);
}
