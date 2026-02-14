#include "World.hpp"

#include "JsonUtils.hpp"
#include "RenderWindow.hpp"
#include "ResourceManager.hpp"
#include "Texture.hpp"

#include "Sphere.hpp"
#include "Material.hpp"

#include <MazeBuilder/maze_builder.h>

#include <box2d/box2d.h>

#include <SDL3/SDL_log.h>

#include <SFML/Network.hpp>

#include <glm/glm.hpp>
#include <glad/glad.h>

#include <random>
#include <limits>
#include <algorithm>
#include <sstream>
#include <cctype>

World::World(RenderWindow& window, FontManager& fonts, TextureManager& textures, ShaderManager& shaders)
    : mWindow{ window }
    , mWorldView{ window.getView() }
    , mFonts{ fonts }
    , mTextures{ textures }
    , mShaders{ shaders }
    , mWorldId{ b2_nullWorldId }
    , mMazeWallsBodyId{ b2_nullBodyId }
    , mIsPanning{ false }
    , mLastMousePosition{ 0.f, 0.f }
    , mLastChunkUpdatePosition{}
    , mPlayerSpawnPosition{}
{
}

World::~World()
{
    destroyWorld();
}

void World::init() noexcept
{
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = { 0.0f, FORCE_DUE_TO_GRAVITY };

    mWorldId = b2CreateWorld(&worldDef);

    mPlayerSpawnPosition = glm::vec3(0.0f, 10.0f, 0.0f);

    initPathTracerScene();

    mSpheres.reserve(TOTAL_SPHERES * 4);
    mSphereBodyIds.reserve(TOTAL_SPHERES * 4);

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
                }
            }));
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
        auto& worker = mWorkerThreads[i];
        if (worker.valid())
        {
            try {
                // Wait with timeout
                auto status = worker.wait_for(std::chrono::seconds(5));
                if (status == std::future_status::timeout)
                {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Worker thread %zu timed out waiting for shutdown", i);
                }
            } catch (const std::exception& e) {
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

void World::submitChunkForGeneration(const ChunkCoord& coord) noexcept
{
    // Create packaged task for chunk generation
    std::packaged_task<ChunkWorkItem()> task(
        [this, coord]() -> ChunkWorkItem {
            return generateChunkAsync(coord);
        }
    );

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

World::ChunkWorkItem World::generateChunkAsync(const ChunkCoord& coord) const noexcept
{
    ChunkWorkItem result;
    result.coord = coord;
    result.hasSpawnPosition = false;

    try {
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

        // Generate spheres (without physics bodies)
        std::mt19937 generator(coord.x * 73856093 ^ coord.z * 19349663);

        auto getRandomFloat = [&generator](float low, float high) -> float {
            std::uniform_real_distribution<float> distribution(low, high);
            return distribution(generator);
            };

        for (const auto& cell : result.cells)
        {
            // Use SPHERE_SPAWN_RATE constant (1%)
            if (getRandomFloat(0.0f, 1.0f) > SPHERE_SPAWN_RATE) {
                continue;
            }

            MaterialType matType = getMaterialForDistance(cell.distance);

            float density, fuzz = 0.0f, refractIdx = 1.5f;
            glm::vec3 albedo;

            if (matType == MaterialType::METAL) {
                density = 2.5f;
                albedo = glm::vec3(
                    getRandomFloat(0.6f, 1.0f),
                    getRandomFloat(0.6f, 1.0f),
                    getRandomFloat(0.6f, 1.0f)
                );
                fuzz = getRandomFloat(0.0f, 0.3f);
            } else if (matType == MaterialType::DIELECTRIC) {
                density = 0.8f;
                albedo = glm::vec3(1.0f);
                refractIdx = 1.5f;
            } else {
                density = 1.0f;
                albedo = glm::vec3(
                    getRandomFloat(0.2f, 0.8f),
                    getRandomFloat(0.2f, 0.8f),
                    getRandomFloat(0.2f, 0.8f)
                );
            }

            float ypos = 5.0f + (cell.distance % 10) * 2.0f;
            float radius = getRandomFloat(2.0f, 5.0f);

            glm::vec3 center(cell.worldX, ypos, cell.worldZ);

            result.spheres.emplace_back(center, radius, albedo, matType, fuzz, refractIdx);
        }
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR,
            "Worker: Exception generating chunk (%d, %d): %s",
            coord.x, coord.z, e.what());
    }

    return result;
}

void World::processCompletedChunks() noexcept
{
    std::lock_guard<std::mutex> lock(mCompletedChunksMutex);

    auto it = mPendingChunks.begin();
    while (it != mPendingChunks.end())
    {
        auto& [coord, future] = *it;

        if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        {
            try {
                ChunkWorkItem workItem = future.get();

                // Update spawn position if found
                if (workItem.hasSpawnPosition)
                {
                    mPlayerSpawnPosition = workItem.spawnPosition;
                }

                std::vector<size_t> sphereIndices;

                // Create physics bodies for all spheres
                for (const auto& sphere : workItem.spheres)
                {
                    size_t sphereIndex = mSpheres.size();
                    sphereIndices.push_back(sphereIndex);

                    mSpheres.push_back(sphere);

                    if (b2World_IsValid(mWorldId))
                    {
                        b2BodyDef bodyDef = b2DefaultBodyDef();
                        bodyDef.type = b2_dynamicBody;
                        bodyDef.position = { sphere.center.x, sphere.center.z };
                        bodyDef.linearDamping = 0.2f;
                        bodyDef.angularDamping = 0.3f;
                        bodyDef.isBullet = sphere.radius < 4.0f;

                        b2BodyId bodyId = b2CreateBody(mWorldId, &bodyDef);

                        if (b2Body_IsValid(bodyId))
                        {
                            b2ShapeDef shapeDef = b2DefaultShapeDef();
                            shapeDef.density = sphere.materialType == static_cast<uint32_t>(MaterialType::METAL) ? 2.5f :
                                (sphere.materialType == static_cast<uint32_t>(MaterialType::DIELECTRIC) ? 0.8f : 1.0f);

                            b2Circle circle = { {0.0f, 0.0f}, sphere.radius };
                            b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);

                            float friction = sphere.materialType == static_cast<uint32_t>(MaterialType::METAL) ? 0.2f : 0.4f;
                            float restitution = sphere.materialType == static_cast<uint32_t>(MaterialType::DIELECTRIC) ? 0.6f : 0.3f;
                            b2Shape_SetFriction(shapeId, friction);
                            b2Shape_SetRestitution(shapeId, restitution);
                            b2Body_SetAwake(bodyId, true);

                            b2Filter filter = b2Shape_GetFilter(shapeId);
                            filter.categoryBits = 0x0004;
                            filter.maskBits = 0xFFFF;
                            b2Shape_SetFilter(shapeId, filter);
                        }

                        mSphereBodyIds.push_back(bodyId);
                    } else
                    {
                        mSphereBodyIds.push_back(b2_nullBodyId);
                    }
                }

                // IMPORTANT: Mark chunk as loaded BEFORE updating indices
                // This prevents race condition where unloadChunk is called before we're done
                mLoadedChunks.insert(coord);
                mChunkSphereIndices[coord] = sphereIndices;
            } catch (const std::exception& e) {
                SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                    "World: Exception integrating chunk (%d, %d): %s",
                    coord.x, coord.z, e.what());
            }

            it = mPendingChunks.erase(it);
        } else
        {
            ++it;
        }
    }
}

void World::update(float dt)
{
    // Process any completed chunk generation work
    processCompletedChunks();

    mWindow.setView(mWorldView);

    if (b2World_IsValid(mWorldId))
    {
        b2World_Step(mWorldId, dt, 4);

        // Sync physics body positions to 3D sphere positions for path tracer
        syncPhysicsToSpheres();
    }
}

void World::draw() const noexcept
{
    // REMOVED: Scene graph drawing - not used
    // Rendering is now fully handled in GameState via compute shaders
    // World only manages physics and sphere data
}

void World::handleEvent(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_KEY_DOWN:
        // Testing: Launch balls with number keys 1-4
        switch (event.key.key)
        {

        }
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
            mLastMousePosition = { static_cast<float>(event.button.x), static_cast<float>(event.button.y) };
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
            SDL_FPoint currentMousePosition = { static_cast<float>(event.motion.x), static_cast<float>(event.motion.y) };
            SDL_FPoint delta = { currentMousePosition.x - mLastMousePosition.x, currentMousePosition.y - mLastMousePosition.y };
            mLastMousePosition = currentMousePosition;

            if (SDL_GetModState() & SDL_KMOD_SHIFT)
            {
                mWorldView.rotate(delta.x);
            } else
            {
                mWorldView.move(-delta.x, -delta.y);
            }
        }
        break;
    }
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
            try {
                b2DestroyBody(*it);
            } catch (const std::exception& e) {
                SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error destroying body: %s", e.what());
            } catch (...) {
                // Ignore unknown errors during cleanup
            }
        }
    }
    mSphereBodyIds.clear();

    // Destroy physics world
    if (b2World_IsValid(mWorldId))
    {
        try {
            b2DestroyWorld(mWorldId);
        } catch (const std::exception& e) {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error destroying physics world: %s", e.what());
        } catch (...) {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Unknown error destroying physics world");
        }
        mWorldId = b2_nullWorldId;
    }

    // Clear all data structures
    mSpheres.clear();
    mChunkSphereIndices.clear();
    mLoadedChunks.clear();
}

void World::initPathTracerScene() noexcept
{
    mSpheres.clear();
    mSphereBodyIds.clear();
    mLoadedChunks.clear();
    mChunkSphereIndices.clear();

    if (!b2World_IsValid(mWorldId))
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "World: Cannot init path tracer - physics world invalid");
        return;
    }

    // Ground sphere (large Lambertian) - static body, always present
    mSpheres.emplace_back(
        glm::vec3(0.0f, -1000.0f, 0.0f),
        1000.0f,
        glm::vec3(0.5f, 0.5f, 0.5f),
        MaterialType::LAMBERTIAN,
        0.0f,
        0.0f
    );
    mSphereBodyIds.push_back(b2_nullBodyId);

    // Center glass sphere - dynamic physics body (hero sphere, always present)
    mSpheres.emplace_back(
        glm::vec3(0.0f, 1.0f, 0.0f),
        1.0f,
        glm::vec3(1.0f, 1.0f, 1.0f),
        MaterialType::DIELECTRIC,
        0.0f,
        1.5f
    );

    {
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position = { 0.0f, 1.0f };
        bodyDef.linearDamping = 0.1f;
        bodyDef.angularDamping = 0.3f;

        b2BodyId bodyId = b2CreateBody(mWorldId, &bodyDef);

        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = 0.8f;

        b2Circle circle = { {0.0f, 0.0f}, 1.0f };
        b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
        b2Shape_SetFriction(shapeId, 0.3f);
        b2Shape_SetRestitution(shapeId, 0.4f);
        b2Body_SetAwake(bodyId, true);

        mSphereBodyIds.push_back(bodyId);
    }

    // Left diffuse sphere (hero sphere, always present)
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
        bodyDef.position = { -4.0f, 1.0f };
        bodyDef.linearDamping = 0.2f;
        bodyDef.angularDamping = 0.3f;

        b2BodyId bodyId = b2CreateBody(mWorldId, &bodyDef);

        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = 1.0f;

        b2Circle circle = { {0.0f, 0.0f}, 1.0f };
        b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
        b2Shape_SetFriction(shapeId, 0.5f);
        b2Shape_SetRestitution(shapeId, 0.3f);
        b2Body_SetAwake(bodyId, true);

        mSphereBodyIds.push_back(bodyId);
    }

    // Right metal sphere (hero sphere, always present)
    mSpheres.emplace_back(
        glm::vec3(4.0f, 1.0f, 0.0f),
        1.0f,
        glm::vec3(0.7f, 0.6f, 0.5f),
        MaterialType::METAL,
        0.0f,
        0.0f
    );

    {
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position = { 4.0f, 1.0f };
        bodyDef.linearDamping = 0.1f;
        bodyDef.angularDamping = 0.3f;

        b2BodyId bodyId = b2CreateBody(mWorldId, &bodyDef);

        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = 2.5f;

        b2Circle circle = { {0.0f, 0.0f}, 1.0f };
        b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
        b2Shape_SetFriction(shapeId, 0.2f);
        b2Shape_SetRestitution(shapeId, 0.5f);
        b2Body_SetAwake(bodyId, true);

        mSphereBodyIds.push_back(bodyId);
    }
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

World::ChunkCoord World::getChunkCoord(const glm::vec3& position) const noexcept
{
    return ChunkCoord{
        static_cast<int>(std::floor(position.x / CHUNK_SIZE)),
        static_cast<int>(std::floor(position.z / CHUNK_SIZE))
    };
}

void World::updateSphereChunks(const glm::vec3& cameraPosition) noexcept
{
    ChunkCoord currentChunk = getChunkCoord(cameraPosition);
    ChunkCoord lastChunk = getChunkCoord(mLastChunkUpdatePosition);

    // Update if camera moved to a different chunk OR this is the first call
    bool firstCall = (mLastChunkUpdatePosition.x == std::numeric_limits<float>::max());
    bool chunkChanged = (currentChunk.x != lastChunk.x || currentChunk.z != lastChunk.z);

    if (!firstCall && !chunkChanged) {
        return; // Still in same chunk, no update needed
    }

    mLastChunkUpdatePosition = cameraPosition;

    // Determine chunks that should be loaded
    std::unordered_set<ChunkCoord, ChunkCoordHash> desiredChunks;
    for (int dx = -CHUNK_LOAD_RADIUS; dx <= CHUNK_LOAD_RADIUS; ++dx) {
        for (int dz = -CHUNK_LOAD_RADIUS; dz <= CHUNK_LOAD_RADIUS; ++dz) {
            desiredChunks.insert(ChunkCoord{ currentChunk.x + dx, currentChunk.z + dz });
        }
    }

    // Unload chunks that are out of range
    std::vector<ChunkCoord> chunksToUnload;
    for (const auto& chunk : mLoadedChunks) {
        if (desiredChunks.find(chunk) == desiredChunks.end()) {
            chunksToUnload.push_back(chunk);
        }
    }

    int unloadedCount = 0;
    for (const auto& chunk : chunksToUnload) {
        unloadChunk(chunk);
        unloadedCount++;
    }

    // Load new chunks
    int loadedCount = 0;
    for (const auto& chunk : desiredChunks) {
        if (mLoadedChunks.find(chunk) == mLoadedChunks.end()) {
            loadChunk(chunk);
            loadedCount++;
        }
    }
}

void World::loadChunk(const ChunkCoord& coord) noexcept
{
    // Don't submit if already loaded or pending
    if (mLoadedChunks.find(coord) != mLoadedChunks.end()) {
        return;
    }

    // Check if already pending
    {
        std::lock_guard<std::mutex> lock(mCompletedChunksMutex);
        for (const auto& [pendingCoord, _] : mPendingChunks)
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

void World::unloadChunk(const ChunkCoord& coord) noexcept
{
    auto it = mChunkSphereIndices.find(coord);
    if (it == mChunkSphereIndices.end()) {
        return;
    }

    // Get indices to remove
    std::vector<size_t> indicesToRemove = it->second;

    // CRITICAL FIX: Track which indices get remapped during swap-and-pop
    std::unordered_map<size_t, size_t> indexRemapping;

    // Sort in reverse to remove from back to front
    std::sort(indicesToRemove.rbegin(), indicesToRemove.rend());

    for (size_t idx : indicesToRemove) {
        if (idx >= mSpheres.size() || idx >= mSphereBodyIds.size()) {
            continue;
        }

        // Destroy physics body if valid
        b2BodyId bodyId = mSphereBodyIds[idx];
        if (b2Body_IsValid(bodyId)) {
            b2DestroyBody(bodyId);
        }

        // Swap with last element
        size_t lastIdx = mSpheres.size() - 1;
        if (idx < lastIdx) {
            // Remember that the element at lastIdx is now at idx
            indexRemapping[lastIdx] = idx;

            mSpheres[idx] = mSpheres[lastIdx];
            mSphereBodyIds[idx] = mSphereBodyIds[lastIdx];
        }

        // Remove last element
        mSpheres.pop_back();
        mSphereBodyIds.pop_back();
    }

    // Update ALL chunk indices based on remapping
    for (auto& [chunkCoord, indices] : mChunkSphereIndices)
    {
        if (chunkCoord == coord) {
            continue;
        }

        for (size_t& sphereIdx : indices)
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

std::string World::generateMazeForChunk(const ChunkCoord& coord) const noexcept
{
    try {
        // Thread-safe cache access
        {
            std::lock_guard<std::mutex> lock(mMazeCacheMutex);
            auto it = mChunkMazes.find(coord);
            if (it != mChunkMazes.end()) {
                return it->second;
            }
        }

        unsigned int seed = static_cast<unsigned int>(coord.x * 73856093 ^ coord.z * 19349663);

        auto mazeStr = mazes::create(
            mazes::configurator()
            .rows(MAZE_ROWS)
            .columns(MAZE_COLS)
            .distances(false)
            .seed(seed)
        );

        // Cache the result (thread-safe)
        {
            std::lock_guard<std::mutex> lock(mMazeCacheMutex);
            mChunkMazes[coord] = mazeStr;
        }

        return mazeStr;
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "World: Failed to generate maze: %s", e.what());
        return "";
    }
}

std::vector<World::MazeCell> World::parseMazeCells(const std::string& mazeStr, const ChunkCoord& coord,
    glm::vec3& outSpawnPosition, bool& outHasSpawn) const noexcept
{
    std::vector<MazeCell> cells;
    outHasSpawn = false;

    if (mazeStr.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "World: Empty maze string for chunk (%d, %d)", coord.x, coord.z);
        return cells;
    }

    float chunkWorldX = coord.x * CHUNK_SIZE;
    float chunkWorldZ = coord.z * CHUNK_SIZE;

    for (int row = 0; row < MAZE_ROWS; ++row) {
        for (int col = 0; col < MAZE_COLS; ++col) {
            int centerRow = MAZE_ROWS / 2;
            int centerCol = MAZE_COLS / 2;
            int distance = std::abs(row - centerRow) + std::abs(col - centerCol);

            float worldX = chunkWorldX + (col * CELL_SIZE) + (CELL_SIZE * 0.5f);
            float worldZ = chunkWorldZ + (row * CELL_SIZE) + (CELL_SIZE * 0.5f);

            cells.push_back(MazeCell{ row, col, distance, worldX, worldZ });

            // Track spawn in work item instead of const_cast
            if (row == centerRow && col == centerCol && coord.x == 0 && coord.z == 0) {
                outSpawnPosition = glm::vec3(worldX, 10.0f, worldZ);
                outHasSpawn = true;
            }
        }
    }

    return cells;
}

MaterialType World::getMaterialForDistance(int distance) const noexcept
{
    if (distance % 4 == 0 && distance != 0) {
        return MaterialType::METAL;
    } else if (distance % 6 == 0 && distance != 0) {
        return MaterialType::DIELECTRIC;
    } else {
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

void World::renderPlayerCharacter(const Player& player, const Camera& camera) const noexcept
{
    // Only render in third-person mode
    if (camera.getMode() != CameraMode::THIRD_PERSON)
    {
        return;
    }

    // Get the character sprite sheet texture
    const Texture* spriteSheet = getCharacterSpriteSheet();
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
    Shader* billboardShader = nullptr;
    try
    {
        billboardShader = &mShaders.get(Shaders::ID::GLSL_BILLBOARD_SPRITE);
    }
    catch (const std::exception& e)
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
    SDL_Window* sdlWindow = mWindow.getSDLWindow();
    if (sdlWindow)
    {
        SDL_GetWindowSize(sdlWindow, &windowWidth, &windowHeight);
    }

    // Get view and projection matrices from camera
    float aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
    glm::mat4 viewMatrix = camera.getLookAt();
    glm::mat4 projMatrix = camera.getPerspective(aspectRatio);

    // Debug log first render
    static bool firstRender = true;
    if (firstRender)
    {
        glm::vec3 camPos = camera.getActualPosition();
        SDL_Log("World: Rendering player billboard:");
        SDL_Log("  Player pos: (%.1f, %.1f, %.1f)", playerPos.x, playerPos.y, playerPos.z);
        SDL_Log("  Camera pos: (%.1f, %.1f, %.1f)", camPos.x, camPos.y, camPos.z);
        SDL_Log("  Frame: %d,%d,%d,%d, texture: %u (%dx%d)",
            frame.left, frame.top, frame.width, frame.height,
            spriteSheet->get(), spriteSheet->getWidth(), spriteSheet->getHeight());
        firstRender = false;
    }

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
        spriteSheet->getHeight()
    );
}

const Texture* World::getCharacterSpriteSheet() const noexcept
{
    try
    {
        return &mTextures.get(Textures::ID::CHARACTER_SPRITE_SHEET);
    }
    catch (const std::exception&)
    {
        return nullptr;
    }
}

