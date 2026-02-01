#include "World.hpp"

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
#include "Material.hpp"

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
    
    // Initialize player spawn position (will be set when chunk 0,0 loads)
    mPlayerSpawnPosition = glm::vec3(0.0f, 10.0f, 0.0f);
    
    // Initialize 3D path tracer scene with physics-enabled spheres
    initPathTracerScene();
    
    // Reserve capacity for dynamic sphere spawning
    mSpheres.reserve(TOTAL_SPHERES * 4);
    mSphereBodyIds.reserve(TOTAL_SPHERES * 4);
    
    // Initialize chunk system - set to invalid position to force first update
    mLastChunkUpdatePosition = glm::vec3(std::numeric_limits<float>::max());
    
    SDL_Log("World: Initialization complete - maze-based chunk system ready");
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
        bodyDef.position = {0.0f, 1.0f};
        bodyDef.linearDamping = 0.1f;
        bodyDef.angularDamping = 0.3f;
        
        b2BodyId bodyId = b2CreateBody(mWorldId, &bodyDef);
        
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = 0.8f;
        
        b2Circle circle = {{0.0f, 0.0f}, 1.0f};
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
        bodyDef.position = {4.0f, 1.0f};
        bodyDef.linearDamping = 0.1f;
        bodyDef.angularDamping = 0.3f;
        
        b2BodyId bodyId = b2CreateBody(mWorldId, &bodyDef);
        
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = 2.5f;
        
        b2Circle circle = {{0.0f, 0.0f}, 1.0f};
        b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
        b2Shape_SetFriction(shapeId, 0.2f);
        b2Shape_SetRestitution(shapeId, 0.5f);
        b2Body_SetAwake(bodyId, true);
        
        mSphereBodyIds.push_back(bodyId);
    }

    SDL_Log("World: Path tracer scene initialized with %zu hero spheres (chunk-based spawning enabled)", 
            mSpheres.size());
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
    
    if (firstCall) {
        SDL_Log("World: First chunk update at camera position (%.1f, %.1f, %.1f)", 
                cameraPosition.x, cameraPosition.y, cameraPosition.z);
    } else {
        SDL_Log("World: Camera moved to new chunk (%d, %d) from (%d, %d)", 
                currentChunk.x, currentChunk.z, lastChunk.x, lastChunk.z);
    }
    
    mLastChunkUpdatePosition = cameraPosition;
    
    // Determine chunks that should be loaded
    std::unordered_set<ChunkCoord, ChunkCoordHash> desiredChunks;
    for (int dx = -CHUNK_LOAD_RADIUS; dx <= CHUNK_LOAD_RADIUS; ++dx) {
        for (int dz = -CHUNK_LOAD_RADIUS; dz <= CHUNK_LOAD_RADIUS; ++dz) {
            desiredChunks.insert(ChunkCoord{currentChunk.x + dx, currentChunk.z + dz});
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
    
    SDL_Log("World: Chunk update complete - Loaded: %d, Unloaded: %d, Total chunks: %zu, Total spheres: %zu", 
            loadedCount, unloadedCount, mLoadedChunks.size(), mSpheres.size());
}

void World::loadChunk(const ChunkCoord& coord) noexcept
{
    if (mLoadedChunks.find(coord) != mLoadedChunks.end()) {
        return; // Already loaded
    }
    
    mLoadedChunks.insert(coord);
    generateSpheresInChunkFromMaze(coord);
}

void World::unloadChunk(const ChunkCoord& coord) noexcept
{
    auto it = mChunkSphereIndices.find(coord);
    if (it == mChunkSphereIndices.end()) {
        return;
    }
    
    // Mark spheres and bodies for removal
    std::vector<size_t> indicesToRemove = it->second;
    
    // Sort in reverse to remove from back to front
    std::sort(indicesToRemove.rbegin(), indicesToRemove.rend());
    
    for (size_t idx : indicesToRemove) {
        if (idx < mSpheres.size() && idx < mSphereBodyIds.size()) {
            // Destroy physics body if valid
            b2BodyId bodyId = mSphereBodyIds[idx];
            if (b2Body_IsValid(bodyId)) {
                b2DestroyBody(bodyId);
            }
            
            // Remove from vectors (swap with last element for O(1) removal)
            if (idx < mSpheres.size() - 1) {
                mSpheres[idx] = mSpheres.back();
                mSphereBodyIds[idx] = mSphereBodyIds.back();
            }
            mSpheres.pop_back();
            mSphereBodyIds.pop_back();
        }
    }
    
    mChunkSphereIndices.erase(it);
    mLoadedChunks.erase(coord);
}

std::string World::generateMazeForChunk(const ChunkCoord& coord) const noexcept
{
    try {
        // Check if maze is cached
        auto it = mChunkMazes.find(coord);
        if (it != mChunkMazes.end()) {
            return it->second;
        }
        
        // Use chunk coordinates as seed for deterministic maze generation
        unsigned int seed = static_cast<unsigned int>(coord.x * 73856093 ^ coord.z * 19349663);
        
        // Create maze WITHOUT distances first to see the format
        auto mazeStr = mazes::create(
            mazes::configurator()
                .rows(MAZE_ROWS)
                .columns(MAZE_COLS)
                .distances(false)  // Try WITHOUT distances first
                .seed(seed)
        );
        
        // Debug: Print first maze to see format
        if (coord.x == 0 && coord.z == 0) {
            SDL_Log("World: DEBUG - Maze string for chunk (0,0) - First 500 chars:");
            SDL_Log("%s", mazeStr.substr(0, std::min(size_t(500), mazeStr.length())).c_str());
            SDL_Log("World: DEBUG - Maze string length: %zu", mazeStr.length());
        }
        
        return mazeStr;
    }
    catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "World: Failed to generate maze: %s", e.what());
        return "";
    }
}

int World::parseBase36(const std::string& str) const noexcept
{
    if (str.empty()) return -1;
    
    int result = 0;
    for (char c : str) {
        result *= 36;
        if (c >= '0' && c <= '9') {
            result += (c - '0');
        } else if (c >= 'A' && c <= 'Z') {
            result += (c - 'A' + 10);
        } else if (c >= 'a' && c <= 'z') {
            result += (c - 'a' + 10);
        }
    }
    return result;
}

std::vector<World::MazeCell> World::parseMazeCells(const std::string& mazeStr, const ChunkCoord& coord) const noexcept
{
    std::vector<MazeCell> cells;
    
    if (mazeStr.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "World: Empty maze string for chunk (%d, %d)", coord.x, coord.z);
        return cells;
    }
    
    // Since we're not using distances for now, just create a simple pattern
    // based on row/column to test the system
    float chunkWorldX = coord.x * CHUNK_SIZE;
    float chunkWorldZ = coord.z * CHUNK_SIZE;
    
    // Generate cells for each position in the maze grid
    for (int row = 0; row < MAZE_ROWS; ++row) {
        for (int col = 0; col < MAZE_COLS; ++col) {
            // Calculate distance from center for now (as a placeholder)
            int centerRow = MAZE_ROWS / 2;
            int centerCol = MAZE_COLS / 2;
            int distance = std::abs(row - centerRow) + std::abs(col - centerCol);
            
            // Calculate world position for this cell
            float worldX = chunkWorldX + (col * CELL_SIZE) + (CELL_SIZE * 0.5f);
            float worldZ = chunkWorldZ + (row * CELL_SIZE) + (CELL_SIZE * 0.5f);
            
            cells.push_back(MazeCell{row, col, distance, worldX, worldZ});
            
            // Track player spawn (center cell of chunk 0,0)
            if (row == centerRow && col == centerCol && coord.x == 0 && coord.z == 0) {
                const_cast<World*>(this)->mPlayerSpawnPosition = glm::vec3(worldX, 10.0f, worldZ);
                SDL_Log("World: Player spawn set to (%.1f, %.1f, %.1f)", worldX, 10.0f, worldZ);
            }
        }
    }
    
    SDL_Log("World: Generated %zu cells for maze chunk (%d, %d) using Manhattan distance", 
            cells.size(), coord.x, coord.z);
    return cells;
}

MaterialType World::getMaterialForDistance(int distance) const noexcept
{
    // Metal spheres at positions divisible by 4
    if (distance % 4 == 0 && distance != 0) {
        return MaterialType::METAL;
    }
    // Dielectric (glass) spheres at positions divisible by 6
    else if (distance % 6 == 0 && distance != 0) {
        return MaterialType::DIELECTRIC;
    }
    // Lambertian (diffuse) for everything else
    else {
        return MaterialType::LAMBERTIAN;
    }
}

void World::generateSpheresInChunkFromMaze(const ChunkCoord& coord) noexcept
{
    if (!b2World_IsValid(mWorldId)) {
        return;
    }
    
    // Generate maze for this chunk
    std::string mazeStr = generateMazeForChunk(coord);
    if (mazeStr.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "World: Empty maze string for chunk (%d, %d)", coord.x, coord.z);
        return;
    }
    
    // Cache the maze
    mChunkMazes[coord] = mazeStr;
    
    // Parse maze cells
    auto cells = parseMazeCells(mazeStr, coord);
    
    std::vector<size_t> sphereIndices;
    
    // Use chunk coordinates as seed for random properties
    std::mt19937 generator(coord.x * 73856093 ^ coord.z * 19349663);
    
    auto getRandomFloat = [&generator](float low, float high) -> float {
        std::uniform_real_distribution<float> distribution(low, high);
        return distribution(generator);
    };
    
    // Generate spheres at maze cell positions
    for (const auto& cell : cells) {
        // PERFORMANCE FIX: Only spawn 5% of cells (was 30%)
        // This gives us ~20 spheres per chunk instead of ~120
        if (getRandomFloat(0.0f, 1.0f) > 0.05f) {
            continue;
        }
        
        // Determine material based on distance
        MaterialType matType = getMaterialForDistance(cell.distance);
        
        // Set properties based on material type
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
        } else { // LAMBERTIAN
            density = 1.0f;
            albedo = glm::vec3(
                getRandomFloat(0.2f, 0.8f),
                getRandomFloat(0.2f, 0.8f),
                getRandomFloat(0.2f, 0.8f)
            );
        }
        
        // Vary height based on distance (creates interesting vertical patterns)
        float ypos = 5.0f + (cell.distance % 10) * 2.0f;
        float radius = getRandomFloat(2.0f, 5.0f);
        
        glm::vec3 center(cell.worldX, ypos, cell.worldZ);
        
        // Add sphere
        size_t sphereIndex = mSpheres.size();
        sphereIndices.push_back(sphereIndex);
        
        mSpheres.emplace_back(center, radius, albedo, matType, fuzz, refractIdx);
        
        // Create physics body
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position = {cell.worldX, cell.worldZ};
        bodyDef.linearDamping = 0.2f;
        bodyDef.angularDamping = 0.3f;
        bodyDef.isBullet = radius < 4.0f;
        
        b2BodyId bodyId = b2CreateBody(mWorldId, &bodyDef);
        
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = density;
        
        b2Circle circle = {{0.0f, 0.0f}, radius};
        b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
        
        float friction = matType == MaterialType::METAL ? 0.2f : 0.4f;
        float restitution = matType == MaterialType::DIELECTRIC ? 0.6f : 0.3f;
        b2Shape_SetFriction(shapeId, friction);
        b2Shape_SetRestitution(shapeId, restitution);
        b2Body_SetAwake(bodyId, true);
        
        b2Filter filter = b2Shape_GetFilter(shapeId);
        filter.categoryBits = 0x0004;
        filter.maskBits = 0xFFFF;
        b2Shape_SetFilter(shapeId, filter);
        
        mSphereBodyIds.push_back(bodyId);
    }
    
    mChunkSphereIndices[coord] = sphereIndices;
    
    SDL_Log("World: Generated %zu spheres from maze in chunk (%d, %d)", 
            sphereIndices.size(), coord.x, coord.z);
}

