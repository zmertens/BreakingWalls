#include "World.hpp"

#include "Animation.hpp"
#include "Camera.hpp"
#include "GLSDLHelper.hpp"
#include "GLTFModel.hpp"
#include "JSONUtils.hpp"
#include "Level.hpp"
#include "Material.hpp"
#include "Player.hpp"
#include "RenderWindow.hpp"
#include "ResourceManager.hpp"
#include "Shader.hpp"
#include "Sphere.hpp"
#include "Texture.hpp"
#include "VertexArrayObject.hpp"
#include "FramebufferObject.hpp"
#include "VertexBufferObject.hpp"

#include <MazeBuilder/maze_builder.h>
#include <MazeBuilder/colored_grid.h>
#include <MazeBuilder/cell.h>
#include <MazeBuilder/dfs.h>
#include <MazeBuilder/grid_operations.h>
#include <MazeBuilder/randomizer.h>

#include <box2d/box2d.h>

#include <SFML/Window/Event.hpp>
#include <iostream>
#include <chrono>

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
    float getTimeSecs() noexcept
    {
        static auto start = std::chrono::steady_clock::now();
        return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
    }

    constexpr std::uintptr_t kBodyTagSphereBase = 10;
    constexpr std::uintptr_t kBodyTagMazeWall = 4;

    // Raster maze geometry constants
    constexpr float kPlayerShadowCenterYOffset = 1.4175f;
    constexpr float kCharacterModelYOffset = 0.25f;

    constexpr unsigned int kSimpleMazeRows = 20u;
    constexpr unsigned int kSimpleMazeCols = 20u;
    constexpr unsigned int kSimpleMazeLevels = 3u;
    constexpr float kSimpleCellSize = 2.4f;
    constexpr float kSimpleWallHeight = 1.8f;
    constexpr float kSimpleWallThickness = 0.20f;
    constexpr float kSimpleLevelSpacing = 2.7f;
    constexpr float kSimpleFloorY = 0.0f;
    constexpr float kBoundarySpriteHeight = 3.6f;
    constexpr float kBoundarySpriteWidth = 2.1f;
    constexpr int kBoundaryAnimFrameCount = 9;
    constexpr int kBoundaryTileSizePx = 128;
    constexpr float kBoundaryAnimFps = 10.0f;
    constexpr int kBoundaryFloatingSpriteCount = 5;
    constexpr float kGoalPathLineYOffset = 0.045f;

    struct RasterVertex
    {
        glm::vec3 position;
        glm::vec3 color;
    };

    glm::vec3 computeSunDirection(float /*timeSeconds*/) noexcept
    {
        return glm::normalize(glm::vec3(-1.0f, -0.125f, 0.0f));
    }

    glm::vec3 packedRGBToLinear(std::uint32_t packed)
    {
        const float r = static_cast<float>((packed >> 16) & 0xFFu) / 255.0f;
        const float g = static_cast<float>((packed >> 8) & 0xFFu) / 255.0f;
        const float b = static_cast<float>(packed & 0xFFu) / 255.0f;
        const glm::vec3 srgb(r, g, b);
        return glm::pow(srgb, glm::vec3(2.2f));
    }

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
                    std::cerr << "Worker thread  timed out waiting for shutdown\n";
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "Worker thread  shutdown exception:\n";
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
            std::cerr << "Worker: Empty maze for chunk (, )\n";
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
        std::cerr << "Worker: Exception generating chunk (, ):\n";
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
                std::cerr << "World: Exception integrating chunk (, ):\n";
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

// ============================================================================
// Scene rendering implementation (moved from GameState)
// ============================================================================

void World::initRendering(VAOManager *vaos, FBOManager *fbos, VBOManager *vbos,
                          ModelsManager *models, int windowWidth, int windowHeight,
                          const Player &player) noexcept
{
    mVAOManager = vaos;
    mFBOManager = fbos;
    mVBOManager = vbos;
    mModelsManager = models;

    try
    {
        mMazeShader = &mShaders.get(Shaders::ID::GLSL_MAZE);
        mSkyShader = &mShaders.get(Shaders::ID::GLSL_SKY);
        mGoalPathStencilShader = &mShaders.get(Shaders::ID::GLSL_GOAL_PATH_STENCIL);
        mBoundarySpriteShader = &mShaders.get(Shaders::ID::GLSL_BILLBOARD_SPRITE);
        mShadowShader = &mShaders.get(Shaders::ID::GLSL_SHADOW_VOLUME);
        mSkinnedCharacterShader = &mShaders.get(Shaders::ID::GLSL_SKINNED_MODEL);
        mWalkParticlesComputeShader = &mShaders.get(Shaders::ID::GLSL_PARTICLES_COMPUTE);
        mWalkParticlesRenderShader = &mShaders.get(Shaders::ID::GLSL_FULLSCREEN_QUAD_MVP);
    }
    catch (const std::exception &e)
    {
        std::cerr << "World::initRendering: Failed to get shaders:\n";
        return;
    }

    try
    {
        mBillboardColorTex = &mTextures.get(Textures::ID::BILLBOARD_COLOR);
        mOITAccumTex = &mTextures.get(Textures::ID::OIT_ACCUM);
        mOITRevealTex = &mTextures.get(Textures::ID::OIT_REVEAL);
        mShadowTexture = &mTextures.get(Textures::ID::SHADOW_MAP);
        mReflectionColorTex = &mTextures.get(Textures::ID::REFLECTION_COLOR);
    }
    catch (const std::exception &e)
    {
        std::cerr << "World::initRendering: Failed to get textures:\n";
        return;
    }

    initializeWalkParticles(player);
    mRenderInitialized = true;
}

void World::drawScene(const Camera &camera, const Player &player,
                      int windowWidth, int windowHeight,
                      float modelAnimTime, float playerPlanarSpeed) const noexcept
{
    if (!mRenderInitialized)
        return;

    renderRasterMaze(camera, player, windowWidth, windowHeight);
    renderGoalPathStencil(camera, windowWidth, windowHeight);
    renderBoundaryCharacterBillboards(camera, windowWidth, windowHeight);
    renderPickupSpheres(camera, windowWidth, windowHeight);
    renderPlayerCharacterModel(camera, player, windowWidth, windowHeight, modelAnimTime);
    renderWalkParticles(camera, player, windowWidth, windowHeight, playerPlanarSpeed);
}

void World::createCompositeTargets(int windowWidth, int windowHeight) noexcept
{
    if (windowWidth <= 0 || windowHeight <= 0)
        return;

    if (!mBillboardColorTex || !mOITAccumTex || !mOITRevealTex)
    {
        std::cerr << "World: Composite textures not initialized in manager\n";
        return;
    }

    if (!mBillboardColorTex->loadRenderTarget(windowWidth, windowHeight, Texture::RenderTargetFormat::RGBA16F, 0))
    {
        std::cerr << "World: Failed to allocate billboard texture\n";
        return;
    }

    auto &billboardFBO = mFBOManager->get(FBOs::ID::BILLBOARD);
    billboardFBO.bindRenderbuffer();
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, windowWidth, windowHeight);

    billboardFBO.bind();
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mBillboardColorTex->get(), 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, billboardFBO.getRenderbuffer());
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "World: Billboard framebuffer incomplete\n";

    if (!mOITAccumTex->loadRenderTarget(windowWidth, windowHeight, Texture::RenderTargetFormat::RGBA16F, 0))
    {
        std::cerr << "World: Failed to allocate OIT accum texture\n";
        return;
    }

    if (!mOITRevealTex->loadRenderTarget(windowWidth, windowHeight, Texture::RenderTargetFormat::R16F, 0))
    {
        std::cerr << "World: Failed to allocate OIT reveal texture\n";
        return;
    }

    auto &oitFBO = mFBOManager->get(FBOs::ID::OIT);
    oitFBO.bindRenderbuffer();
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, windowWidth, windowHeight);

    oitFBO.bind();
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mOITAccumTex->get(), 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, mOITRevealTex->get(), 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, oitFBO.getRenderbuffer());
    constexpr GLenum oitDrawBuffers[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, oitDrawBuffers);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        mOITInitialized = false;
    else
        mOITInitialized = true;

    FramebufferObject::unbind();
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);
    glBindTexture(GL_TEXTURE_2D, 0);
    FramebufferObject::unbindRenderbuffer();
}

void World::initializeShadowResources(int windowWidth, int windowHeight) noexcept
{
    if (windowWidth <= 0 || windowHeight <= 0)
        return;

    if (!mShadowTexture)
    {
        std::cerr << "World: Shadow texture not initialized in manager\n";
        return;
    }

    if (!mShadowTexture->loadRenderTarget(windowWidth, windowHeight, Texture::RenderTargetFormat::RGBA16F, 0))
    {
        std::cerr << "World: Failed to allocate shadow texture\n";
        return;
    }

    auto &shadowFBO = mFBOManager->get(FBOs::ID::SHADOW);
    shadowFBO.bind();
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mShadowTexture->get(), 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "World: Shadow framebuffer incomplete\n";

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (mVAOManager && mVBOManager)
    {
        auto &shadowVBO = mVBOManager->get(VBOs::ID::SHADOW);
        mVAOManager->get(VAOs::ID::SHADOW_QUAD).bind();
        shadowVBO.bind(GL_ARRAY_BUFFER);

        float point = 0.0f;
        glBufferData(GL_ARRAY_BUFFER, sizeof(float), &point, GL_STATIC_DRAW);

        VertexBufferObject::unbind(GL_ARRAY_BUFFER);
        VertexArrayObject::unbind();
    }

    FramebufferObject::unbind();
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);
    glBindTexture(GL_TEXTURE_2D, 0);

    mShadowsInitialized = true;
}

void World::initializeReflectionResources(int windowWidth, int windowHeight) noexcept
{
    if (windowWidth <= 0 || windowHeight <= 0)
        return;

    FramebufferObject::unbind();
    glBindTexture(GL_TEXTURE_2D, 0);
    FramebufferObject::unbindRenderbuffer();

    if (!mReflectionColorTex)
    {
        std::cerr << "World: Reflection texture not initialized in manager\n";
        return;
    }

    if (!mReflectionColorTex->loadRenderTarget(windowWidth, windowHeight, Texture::RenderTargetFormat::RGBA16F, 0))
    {
        std::cerr << "World: Failed to allocate reflection texture\n";
        return;
    }

    auto &reflectionFBO = mFBOManager->get(FBOs::ID::REFLECTION);
    reflectionFBO.bindRenderbuffer();
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, windowWidth, windowHeight);

    reflectionFBO.bind();
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mReflectionColorTex->get(), 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, reflectionFBO.getRenderbuffer());
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fboStatus != GL_FRAMEBUFFER_COMPLETE)
    {
        std::cerr << "World: Reflection framebuffer incomplete: 0x\n";
        return;
    }

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    FramebufferObject::unbind();
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);
    glBindTexture(GL_TEXTURE_2D, 0);
    FramebufferObject::unbindRenderbuffer();

    mReflectionsInitialized = true;
}

void World::initializeWalkParticles(const Player &player) noexcept
{
    if (mWalkParticlesInitialized)
        return;

    if (!mWalkParticlesComputeShader || !mWalkParticlesRenderShader || mWalkParticleCount == 0)
        return;

    std::vector<GLfloat> initPos;
    std::vector<GLfloat> initVel;
    initPos.reserve(static_cast<std::size_t>(mWalkParticleCount) * 4u);
    initVel.reserve(static_cast<std::size_t>(mWalkParticleCount) * 4u);

    const glm::vec3 playerPos = player.getPosition();
    const glm::vec3 anchor = playerPos + glm::vec3(0.0f, 1.0f, 0.0f);

    for (GLuint i = 0; i < mWalkParticleCount; ++i)
    {
        const float t = static_cast<float>(i) * 0.0618f;
        const float u = static_cast<float>(i % 37u) / 37.0f;
        const float radius = 0.15f + 0.95f * u;
        const float x = anchor.x + std::cos(t * 6.28318f) * radius;
        const float z = anchor.z + std::sin(t * 6.28318f) * radius;
        const float y = anchor.y + (static_cast<float>(i % 17u) / 17.0f) * 0.75f;

        initPos.push_back(x);
        initPos.push_back(y);
        initPos.push_back(z);
        initPos.push_back(1.0f);

        initVel.push_back(0.0f);
        initVel.push_back(0.0f);
        initVel.push_back(0.0f);
        initVel.push_back(0.0f);
    }

    const GLsizeiptr bufferSize = static_cast<GLsizeiptr>(static_cast<std::size_t>(mWalkParticleCount) * 4u * sizeof(GLfloat));

    auto &posSSBO = mVBOManager->get(VBOs::ID::WALK_PARTICLES_POS_SSBO);
    posSSBO.bind(GL_SHADER_STORAGE_BUFFER);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, initPos.data(), GL_DYNAMIC_DRAW);

    auto &velSSBO = mVBOManager->get(VBOs::ID::WALK_PARTICLES_VEL_SSBO);
    velSSBO.bind(GL_SHADER_STORAGE_BUFFER);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, initVel.data(), GL_DYNAMIC_DRAW);

    if (mVAOManager)
    {
        mVAOManager->get(VAOs::ID::WALK_PARTICLES).bind();
        posSSBO.bind(GL_ARRAY_BUFFER);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);
        VertexArrayObject::unbind();
    }

    VertexBufferObject::unbind(GL_ARRAY_BUFFER);
    VertexBufferObject::unbind(GL_SHADER_STORAGE_BUFFER);

    mWalkParticlesInitialized = true;
}

void World::buildMazeGeometry(const Player & /*player*/) noexcept
{
    std::vector<RasterVertex> vertices;
    vertices.reserve(50000);
    std::vector<glm::vec3> goalPathLines;
    goalPathLines.reserve(4000);
    mMazeWallAABBs.clear();
    mMazeCellGradientColors.assign(static_cast<std::size_t>(kSimpleMazeRows) * static_cast<std::size_t>(kSimpleMazeCols), glm::vec3(0.5f, 0.8f, 0.6f));

    auto pushTri = [&vertices](const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &color)
    {
        vertices.push_back({a, color});
        vertices.push_back({b, color});
        vertices.push_back({c, color});
    };

    auto pushQuad = [&pushTri](const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d, const glm::vec3 &color)
    {
        pushTri(a, b, c, color);
        pushTri(a, c, d, color);
    };

    auto pushBox = [&pushQuad](const glm::vec3 &center, const glm::vec3 &size, const glm::vec3 &color)
    {
        const glm::vec3 h = 0.5f * size;
        const glm::vec3 p000 = center + glm::vec3(-h.x, -h.y, -h.z);
        const glm::vec3 p001 = center + glm::vec3(-h.x, -h.y, h.z);
        const glm::vec3 p010 = center + glm::vec3(-h.x, h.y, -h.z);
        const glm::vec3 p011 = center + glm::vec3(-h.x, h.y, h.z);
        const glm::vec3 p100 = center + glm::vec3(h.x, -h.y, -h.z);
        const glm::vec3 p101 = center + glm::vec3(h.x, -h.y, h.z);
        const glm::vec3 p110 = center + glm::vec3(h.x, h.y, -h.z);
        const glm::vec3 p111 = center + glm::vec3(h.x, h.y, h.z);

        pushQuad(p001, p101, p111, p011, color);
        pushQuad(p100, p000, p010, p110, color);
        pushQuad(p000, p001, p011, p010, color);
        pushQuad(p101, p100, p110, p111, color);
        pushQuad(p010, p011, p111, p110, color);
        pushQuad(p000, p100, p101, p001, color);
    };

    auto mazeGrid = std::make_unique<mazes::colored_grid>(kSimpleMazeRows, kSimpleMazeCols, 1u);
    mazes::randomizer rng{};
    rng.seed(rng(0u, 4'200'000u));
    mazes::dfs dfsAlgo{};
    dfsAlgo.run(mazeGrid.get(), rng);
    mazeGrid->initialize_distance_coloring(0, mazeGrid->operations().num_cells() - 1);

    const float mazeWidth = static_cast<float>(kSimpleMazeCols) * kSimpleCellSize;
    const float mazeDepth = static_cast<float>(kSimpleMazeRows) * kSimpleCellSize;
    const glm::vec3 mazeOrigin(-mazeWidth * 0.5f, kSimpleFloorY, -mazeDepth * 0.5f);
    const glm::vec3 mazeCenter(0.0f, kSimpleFloorY + 0.5f * (kSimpleMazeLevels - 1u) * kSimpleLevelSpacing, 0.0f);
    const float mazeTopY = kSimpleFloorY + static_cast<float>(kSimpleMazeLevels - 1u) * kSimpleLevelSpacing + kSimpleWallHeight;

    auto &&gridOps = mazeGrid->operations();
    for (unsigned int level = 0u; level < kSimpleMazeLevels; ++level)
    {
        const float levelBaseY = kSimpleFloorY + static_cast<float>(level) * kSimpleLevelSpacing;

        for (unsigned int row = 0u; row < kSimpleMazeRows; ++row)
        {
            for (unsigned int col = 0u; col < kSimpleMazeCols; ++col)
            {
                const int idx = static_cast<int>(row * kSimpleMazeCols + col);
                const auto cellPtr = gridOps.search(idx);
                if (!cellPtr)
                    continue;

                const float cx = mazeOrigin.x + (static_cast<float>(col) + 0.5f) * kSimpleCellSize;
                const float cz = mazeOrigin.z + (static_cast<float>(row) + 0.5f) * kSimpleCellSize;

                glm::vec3 tileColor = packedRGBToLinear(mazeGrid->background_color_for(cellPtr));
                mMazeCellGradientColors[static_cast<std::size_t>(idx)] = tileColor;
                const float levelTint = 0.88f + 0.12f *
                                                    (kSimpleMazeLevels > 1u ? static_cast<float>(level) / static_cast<float>(kSimpleMazeLevels - 1u) : 0.0f);
                tileColor *= levelTint;

                const float y = levelBaseY + 0.01f;
                const glm::vec3 a(cx - 0.5f * kSimpleCellSize, y, cz - 0.5f * kSimpleCellSize);
                const glm::vec3 b(cx + 0.5f * kSimpleCellSize, y, cz - 0.5f * kSimpleCellSize);
                const glm::vec3 c(cx + 0.5f * kSimpleCellSize, y, cz + 0.5f * kSimpleCellSize);
                const glm::vec3 d(cx - 0.5f * kSimpleCellSize, y, cz + 0.5f * kSimpleCellSize);
                pushQuad(a, b, c, d, tileColor);

                {
                    const float py = levelBaseY + kGoalPathLineYOffset;
                    const float x0 = cx - 0.5f * kSimpleCellSize;
                    const float x1 = cx + 0.5f * kSimpleCellSize;
                    const float z0 = cz - 0.5f * kSimpleCellSize;
                    const float z1 = cz + 0.5f * kSimpleCellSize;

                    const auto northCell = gridOps.get_north(cellPtr);
                    const auto southCell = gridOps.get_south(cellPtr);
                    const auto westCell = gridOps.get_west(cellPtr);
                    const auto eastCell = gridOps.get_east(cellPtr);

                    const bool northLinked = northCell && cellPtr->is_linked(northCell);
                    const bool southLinked = southCell && cellPtr->is_linked(southCell);
                    const bool westLinked = westCell && cellPtr->is_linked(westCell);
                    const bool eastLinked = eastCell && cellPtr->is_linked(eastCell);

                    if (!northCell && !northLinked)
                    {
                        goalPathLines.emplace_back(x0, py, z0);
                        goalPathLines.emplace_back(x1, py, z0);
                    }
                    if (!southLinked)
                    {
                        goalPathLines.emplace_back(x0, py, z1);
                        goalPathLines.emplace_back(x1, py, z1);
                    }
                    if (!westCell && !westLinked)
                    {
                        goalPathLines.emplace_back(x0, py, z0);
                        goalPathLines.emplace_back(x0, py, z1);
                    }
                    if (!eastLinked)
                    {
                        goalPathLines.emplace_back(x1, py, z0);
                        goalPathLines.emplace_back(x1, py, z1);
                    }
                }

                const glm::vec3 wallColor(0.22f, 0.07f, 0.34f);

                int dist = static_cast<int>(std::abs(static_cast<int>(row) - static_cast<int>(kSimpleMazeRows / 2u))) +
                           static_cast<int>(std::abs(static_cast<int>(col) - static_cast<int>(kSimpleMazeCols / 2u)));

                glm::vec3 actualWallColor = wallColor;
                if (dist > 0 && dist % 3 == 0)
                    actualWallColor = glm::vec3(0.45f, 0.40f, 0.55f);
                else if (dist > 0 && dist % 5 == 0)
                    actualWallColor = glm::vec3(0.18f, 0.30f, 0.55f);

                const auto east = gridOps.get_east(cellPtr);
                if (!(east && cellPtr->is_linked(east)))
                {
                    pushBox(glm::vec3(cx + 0.5f * kSimpleCellSize, levelBaseY + 0.5f * kSimpleWallHeight, cz),
                            glm::vec3(kSimpleWallThickness, kSimpleWallHeight, kSimpleCellSize + kSimpleWallThickness),
                            actualWallColor);
                    if (level == 0u)
                    {
                        const float hw = kSimpleWallThickness * 0.5f;
                        const float hz = (kSimpleCellSize + kSimpleWallThickness) * 0.5f;
                        mMazeWallAABBs.emplace_back(cx + 0.5f * kSimpleCellSize - hw, cz - hz,
                                                    cx + 0.5f * kSimpleCellSize + hw, cz + hz);
                    }
                }

                const auto south = gridOps.get_south(cellPtr);
                if (!(south && cellPtr->is_linked(south)))
                {
                    pushBox(glm::vec3(cx, levelBaseY + 0.5f * kSimpleWallHeight, cz + 0.5f * kSimpleCellSize),
                            glm::vec3(kSimpleCellSize + kSimpleWallThickness, kSimpleWallHeight, kSimpleWallThickness),
                            actualWallColor);
                    if (level == 0u)
                    {
                        const float hx = (kSimpleCellSize + kSimpleWallThickness) * 0.5f;
                        const float hw = kSimpleWallThickness * 0.5f;
                        mMazeWallAABBs.emplace_back(cx - hx, cz + 0.5f * kSimpleCellSize - hw,
                                                    cx + hx, cz + 0.5f * kSimpleCellSize + hw);
                    }
                }

                if (col == 0u)
                {
                    pushBox(glm::vec3(cx - 0.5f * kSimpleCellSize, levelBaseY + 0.5f * kSimpleWallHeight, cz),
                            glm::vec3(kSimpleWallThickness, kSimpleWallHeight, kSimpleCellSize + kSimpleWallThickness),
                            actualWallColor);
                    if (level == 0u)
                    {
                        const float hw = kSimpleWallThickness * 0.5f;
                        const float hz = (kSimpleCellSize + kSimpleWallThickness) * 0.5f;
                        mMazeWallAABBs.emplace_back(cx - 0.5f * kSimpleCellSize - hw, cz - hz,
                                                    cx - 0.5f * kSimpleCellSize + hw, cz + hz);
                    }
                }

                if (row == 0u)
                {
                    pushBox(glm::vec3(cx, levelBaseY + 0.5f * kSimpleWallHeight, cz - 0.5f * kSimpleCellSize),
                            glm::vec3(kSimpleCellSize + kSimpleWallThickness, kSimpleWallHeight, kSimpleWallThickness),
                            actualWallColor);
                    if (level == 0u)
                    {
                        const float hx = (kSimpleCellSize + kSimpleWallThickness) * 0.5f;
                        const float hw = kSimpleWallThickness * 0.5f;
                        mMazeWallAABBs.emplace_back(cx - hx, cz - 0.5f * kSimpleCellSize - hw,
                                                    cx + hx, cz - 0.5f * kSimpleCellSize + hw);
                    }
                }
            }
        }
    }

    // Floating boundary sprite anchors
    {
        mBoundarySprites.clear();
        mBoundarySprites.reserve(static_cast<std::size_t>(kBoundaryFloatingSpriteCount));

        std::mt19937 spriteRng{std::random_device{}()};
        std::uniform_real_distribution<float> xDist(mazeOrigin.x - 6.0f, mazeOrigin.x + mazeWidth + 6.0f);
        std::uniform_real_distribution<float> zDist(mazeOrigin.z - 10.0f, mazeOrigin.z + mazeDepth + 10.0f);
        std::uniform_real_distribution<float> yDist(mazeTopY + 5.0f, mazeTopY + 16.0f);
        std::uniform_real_distribution<float> phaseDist(0.0f, static_cast<float>(kBoundaryAnimFrameCount));
        std::uniform_real_distribution<float> scaleDist(1.45f, 2.15f);

        for (int i = 0; i < kBoundaryFloatingSpriteCount; ++i)
        {
            mBoundarySprites.push_back(BoundarySpriteData{
                glm::vec3(xDist(spriteRng), yDist(spriteRng), zDist(spriteRng)),
                phaseDist(spriteRng),
                scaleDist(spriteRng)});
        }
    }

    const glm::vec3 floorCol(0.18f, 0.13f, 0.28f);
    const float floorY = kSimpleFloorY;
    pushQuad(glm::vec3(-2400.0f, floorY, -2400.0f),
             glm::vec3(2400.0f, floorY, -2400.0f),
             glm::vec3(2400.0f, floorY, 2400.0f),
             glm::vec3(-2400.0f, floorY, 2400.0f),
             floorCol);

    mVAOManager->get(VAOs::ID::RASTER_MAZE).bind();
    mVBOManager->get(VBOs::ID::RASTER_MAZE).bind(GL_ARRAY_BUFFER);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(RasterVertex)),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RasterVertex), reinterpret_cast<void *>(offsetof(RasterVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RasterVertex), reinterpret_cast<void *>(offsetof(RasterVertex, color)));
    VertexArrayObject::unbind();

    mRasterMazeVertexCount = static_cast<GLsizei>(vertices.size());

    mVAOManager->get(VAOs::ID::GOAL_PATH).bind();
    mVBOManager->get(VBOs::ID::GOAL_PATH).bind(GL_ARRAY_BUFFER);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(goalPathLines.size() * sizeof(glm::vec3)),
                 goalPathLines.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), reinterpret_cast<void *>(0));
    VertexArrayObject::unbind();
    VertexBufferObject::unbind(GL_ARRAY_BUFFER);
    mGoalPathVertexCount = static_cast<GLsizei>(goalPathLines.size());

    mRasterMazeCenter = mazeCenter;
    mRasterMazeWidth = mazeWidth;
    mRasterMazeDepth = mazeDepth;
    mRasterMazeTopY = mazeTopY;

    std::cerr << "World: Raster maze built - rows= cols= levels= vertices=\n";
}

void World::renderRasterMaze(const Camera &camera, const Player &player,
                             int windowWidth, int windowHeight) const noexcept
{
    FramebufferObject::unbind();
    glViewport(0, 0, windowWidth, windowHeight);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    mSkyShader->bind();
    mVAOManager->get(VAOs::ID::FULLSCREEN_QUAD).bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    const float aspectRatio = static_cast<float>(std::max(1, windowWidth)) / static_cast<float>(std::max(1, windowHeight));
    const glm::mat4 mvp = camera.getPerspective(aspectRatio) * camera.getLookAt();
    mMazeShader->bind();
    mMazeShader->setUniform("uMVP", mvp);
    mMazeShader->setUniform("uTime", getTimeSecs());

    const float mazeOriginX = mRasterMazeCenter.x - 0.5f * mRasterMazeWidth;
    const float mazeOriginZ = mRasterMazeCenter.z - 0.5f * mRasterMazeDepth;
    const glm::vec3 playerPos = player.getPosition();
    const int highlightCol = static_cast<int>(std::floor((playerPos.x - mazeOriginX) / kSimpleCellSize));
    const int highlightRow = static_cast<int>(std::floor((playerPos.z - mazeOriginZ) / kSimpleCellSize));
    mMazeShader->setUniform("uPlayerXZ", glm::vec2(playerPos.x, playerPos.z));
    mMazeShader->setUniform("uMazeOriginXZ", glm::vec2(mazeOriginX, mazeOriginZ));
    mMazeShader->setUniform("uCellSize", kSimpleCellSize);
    if (highlightRow >= 0 && highlightRow < static_cast<int>(kSimpleMazeRows) &&
        highlightCol >= 0 && highlightCol < static_cast<int>(kSimpleMazeCols))
    {
        mMazeShader->setUniform("uHighlightEnabled", 1);
    }
    else
    {
        mMazeShader->setUniform("uHighlightEnabled", 0);
    }

    const Texture *spriteSheet = getCharacterSpriteSheet();
    if (spriteSheet && spriteSheet->get() != 0)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, spriteSheet->get());
        mMazeShader->setUniform("uSpriteSheet", 0);
        mMazeShader->setUniform("uHasTexture", 1);
    }
    else
    {
        mMazeShader->setUniform("uHasTexture", 0);
    }

    mVAOManager->get(VAOs::ID::RASTER_MAZE).bind();
    glDrawArrays(GL_TRIANGLES, 0, mRasterMazeVertexCount);
}

void World::renderGoalPathStencil(const Camera &camera,
                                  int windowWidth, int windowHeight) const noexcept
{
    const float aspectRatio = static_cast<float>(std::max(1, windowWidth)) /
                              static_cast<float>(std::max(1, windowHeight));
    const glm::mat4 mvp = camera.getPerspective(aspectRatio) * camera.getLookAt();
    const float now = getTimeSecs();

    GLboolean stencilWasEnabled = glIsEnabled(GL_STENCIL_TEST);
    GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLfloat prevLineWidth = 1.0f;
    glGetFloatv(GL_LINE_WIDTH, &prevLineWidth);

    glEnable(GL_STENCIL_TEST);
    glClear(GL_STENCIL_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    glStencilMask(0xFF);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

    mGoalPathStencilShader->bind();
    mGoalPathStencilShader->setUniform("uMVP", mvp);
    mGoalPathStencilShader->setUniform("uColor", glm::vec3(0.72f, 1.0f, 0.84f));
    mGoalPathStencilShader->setUniform("uIntensity", 1.0f);
    mGoalPathStencilShader->setUniform("uTime", now);
    mVAOManager->get(VAOs::ID::GOAL_PATH).bind();
    glLineWidth(3.0f);
    glDrawArrays(GL_LINES, 0, mGoalPathVertexCount);

    glStencilMask(0x00);
    glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    mGoalPathStencilShader->setUniform("uColor", glm::vec3(0.70f, 1.0f, 0.90f));
    mGoalPathStencilShader->setUniform("uIntensity", 0.52f);
    glLineWidth(7.0f);
    glDrawArrays(GL_LINES, 0, mGoalPathVertexCount);

    VertexArrayObject::unbind();
    glLineWidth(prevLineWidth);
    glStencilMask(0xFF);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glDepthMask(GL_TRUE);

    if (!stencilWasEnabled)
        glDisable(GL_STENCIL_TEST);
    if (!blendWasEnabled)
        glDisable(GL_BLEND);
    if (depthWasEnabled)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
}

void World::renderBoundaryCharacterBillboards(const Camera &camera,
                                              int windowWidth, int windowHeight) const noexcept
{
    if (!mBoundarySpriteShader || !mBoundarySpriteShader->isLinked() ||
        mBoundarySprites.empty())
        return;

    const Texture *spriteSheet = getCharacterSpriteSheet();
    if (!spriteSheet || spriteSheet->get() == 0)
        return;

    const int texW = std::max(1, spriteSheet->getWidth());
    const int texH = std::max(1, spriteSheet->getHeight());
    const int rows = std::max(1, texH / kBoundaryTileSizePx);

    const float aspectRatio = static_cast<float>(std::max(1, windowWidth)) /
                              static_cast<float>(std::max(1, windowHeight));
    const glm::mat4 view = camera.getLookAt();
    const glm::mat4 proj = camera.getPerspective(aspectRatio);

    GLboolean wasBlendEnabled = GL_FALSE;
    GLboolean wasCullEnabled = GL_FALSE;
    glGetBooleanv(GL_BLEND, &wasBlendEnabled);
    glGetBooleanv(GL_CULL_FACE, &wasCullEnabled);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    glDrawBuffer(GL_BACK);

    const float now = getTimeSecs();

    for (const auto &sprite : mBoundarySprites)
    {
        const int frame = static_cast<int>(std::floor(now * kBoundaryAnimFps + sprite.phaseOffset)) % std::max(1, kBoundaryAnimFrameCount);
        const int clampedFrame = std::clamp(frame, 0, rows - 1);
        const int rowFromBottom = rows - 1 - clampedFrame;

        const float tileU = static_cast<float>(kBoundaryTileSizePx) / static_cast<float>(texW);
        const float tileV = static_cast<float>(kBoundaryTileSizePx) / static_cast<float>(texH);
        const float vMin = static_cast<float>(rowFromBottom) * tileV;

        const glm::vec4 uvRect(0.0f, vMin, tileU, vMin + tileV);
        const float effectiveScale = std::max(sprite.scale, 0.1f);
        const glm::vec2 halfSizeXY(kBoundarySpriteWidth * effectiveScale * 0.5f,
                                   kBoundarySpriteHeight * effectiveScale * 0.5f);

        GLSDLHelper::renderBillboardSpriteUV(
            *mBoundarySpriteShader,
            spriteSheet->get(),
            uvRect,
            sprite.center,
            0.0f,
            view,
            proj,
            glm::vec4(1.0f),
            false,
            false,
            false,
            halfSizeXY);
    }

    if (!wasBlendEnabled)
        glDisable(GL_BLEND);
    if (wasCullEnabled)
        glEnable(GL_CULL_FACE);
}

void World::renderPickupSpheres(const Camera &camera,
                                int windowWidth, int windowHeight) const noexcept
{
    if (mPickupSpheres.empty())
        return;

    // Configure pickup VAO vertex attributes on first use
    if (mPickupVertexCount == 0 && mVAOManager && mVBOManager)
    {
        mVAOManager->get(VAOs::ID::PICKUP_SPHERES).bind();
        mVBOManager->get(VBOs::ID::PICKUP).bind(GL_ARRAY_BUFFER);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RasterVertex),
                              reinterpret_cast<void *>(offsetof(RasterVertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RasterVertex),
                              reinterpret_cast<void *>(offsetof(RasterVertex, color)));
        VertexArrayObject::unbind();
        mPickupsDirty = true;
    }

    if (mPickupsDirty)
    {
        std::vector<RasterVertex> pickupVerts;
        pickupVerts.reserve(mPickupSpheres.size() * 36);

        auto pushQuad = [&pickupVerts](const glm::vec3 &a, const glm::vec3 &b,
                                       const glm::vec3 &c, const glm::vec3 &d,
                                       const glm::vec3 &color)
        {
            pickupVerts.push_back({a, color});
            pickupVerts.push_back({b, color});
            pickupVerts.push_back({c, color});
            pickupVerts.push_back({a, color});
            pickupVerts.push_back({c, color});
            pickupVerts.push_back({d, color});
        };

        for (const auto &pickup : mPickupSpheres)
        {
            if (pickup.collected)
                continue;

            const glm::vec3 &pos = pickup.position;

            glm::vec3 color;
            if (pickup.value > 0)
                color = glm::vec3(0.15f, 0.85f, 0.25f);
            else if (pickup.value < 0)
                color = glm::vec3(0.90f, 0.15f, 0.15f);
            else
                color = glm::vec3(0.90f, 0.85f, 0.15f);

            const float h = 0.4f;
            const glm::vec3 p000 = pos + glm::vec3(-h, -h, -h);
            const glm::vec3 p001 = pos + glm::vec3(-h, -h, h);
            const glm::vec3 p010 = pos + glm::vec3(-h, h, -h);
            const glm::vec3 p011 = pos + glm::vec3(-h, h, h);
            const glm::vec3 p100 = pos + glm::vec3(h, -h, -h);
            const glm::vec3 p101 = pos + glm::vec3(h, -h, h);
            const glm::vec3 p110 = pos + glm::vec3(h, h, -h);
            const glm::vec3 p111 = pos + glm::vec3(h, h, h);

            pushQuad(p001, p101, p111, p011, color);
            pushQuad(p100, p000, p010, p110, color * 0.8f);
            pushQuad(p000, p001, p011, p010, color * 0.9f);
            pushQuad(p101, p100, p110, p111, color * 0.85f);
            pushQuad(p010, p011, p111, p110, color * 1.1f);
            pushQuad(p000, p100, p101, p001, color * 0.7f);
        }

        mVBOManager->get(VBOs::ID::PICKUP).bind(GL_ARRAY_BUFFER);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(pickupVerts.size() * sizeof(RasterVertex)),
                     pickupVerts.data(), GL_DYNAMIC_DRAW);
        const_cast<World *>(this)->mPickupVertexCount = static_cast<GLsizei>(pickupVerts.size());
        mPickupsDirty = false;
    }

    if (mPickupVertexCount == 0)
        return;

    const float aspectRatio = static_cast<float>(std::max(1, windowWidth)) /
                              static_cast<float>(std::max(1, windowHeight));
    const glm::mat4 mvp = camera.getPerspective(aspectRatio) * camera.getLookAt();

    glEnable(GL_DEPTH_TEST);
    mMazeShader->bind();
    mMazeShader->setUniform("uMVP", mvp);
    mMazeShader->setUniform("uHasTexture", 0);
    mVAOManager->get(VAOs::ID::PICKUP_SPHERES).bind();
    glDrawArrays(GL_TRIANGLES, 0, mPickupVertexCount);
}

void World::renderPlayerCharacterModel(const Camera &camera, const Player &player,
                                       int windowWidth, int windowHeight,
                                       float modelAnimTime) const noexcept
{
    if (!mSkinnedCharacterShader || !mSkinnedCharacterShader->isLinked())
        return;

    if (!mModelsManager)
        return;

    GLTFModel *model = nullptr;
    try
    {
        model = &mModelsManager->get(Models::ID::STYLIZED_CHARACTER);
    }
    catch (const std::exception &)
    {
        return;
    }

    if (!model || !model->isLoaded())
        return;

    const float aspectRatio = static_cast<float>(std::max(1, windowWidth)) /
                              static_cast<float>(std::max(1, windowHeight));
    const glm::mat4 view = camera.getLookAt();
    const glm::mat4 proj = camera.getPerspective(aspectRatio);

    const glm::vec3 playerPos = player.getPosition() + glm::vec3(0.0f, kCharacterModelYOffset, 0.0f);
    const float facingDeg = player.getFacingDirection();

    glm::mat4 modelMat = glm::translate(glm::mat4(1.0f), playerPos);
    modelMat = glm::rotate(modelMat, glm::radians(facingDeg), glm::vec3(0.0f, 1.0f, 0.0f));
    modelMat = glm::scale(modelMat, glm::vec3(1.0f));

    GLboolean prevDepthTest = GL_FALSE;
    GLboolean prevCullFace = GL_FALSE;
    GLboolean prevBlend = GL_FALSE;
    GLint prevDepthFunc = GL_LESS;
    glGetBooleanv(GL_DEPTH_TEST, &prevDepthTest);
    glGetBooleanv(GL_CULL_FACE, &prevCullFace);
    glGetBooleanv(GL_BLEND, &prevBlend);
    glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    mSkinnedCharacterShader->bind();
    mSkinnedCharacterShader->setUniform("uCameraPos", camera.getPosition());
    mSkinnedCharacterShader->setUniform("uTime", getTimeSecs());

    // Contact shadow
    {
        glm::mat4 shadowMat = glm::translate(glm::mat4(1.0f), glm::vec3(playerPos.x, kSimpleFloorY + 0.03f, playerPos.z));
        shadowMat = glm::rotate(shadowMat, glm::radians(facingDeg), glm::vec3(0.0f, 1.0f, 0.0f));
        shadowMat = glm::scale(shadowMat, glm::vec3(1.03f, 0.02f, 1.03f));

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);

        mSkinnedCharacterShader->setUniform("uShadowPass", 1);
        model->render(*mSkinnedCharacterShader, shadowMat, view, proj, modelAnimTime);

        glDepthMask(GL_TRUE);
        if (prevCullFace)
            glEnable(GL_CULL_FACE);
        else
            glDisable(GL_CULL_FACE);
    }

    mSkinnedCharacterShader->setUniform("uShadowPass", 0);
    model->render(*mSkinnedCharacterShader, modelMat, view, proj, modelAnimTime);

    if (!prevDepthTest)
        glDisable(GL_DEPTH_TEST);
    else
        glDepthFunc(static_cast<GLenum>(prevDepthFunc));
    if (!prevCullFace)
        glDisable(GL_CULL_FACE);
    if (!prevBlend)
        glDisable(GL_BLEND);
}

void World::renderWalkParticles(const Camera &camera, const Player &player,
                                int windowWidth, int windowHeight,
                                float playerPlanarSpeed) const noexcept
{
    if (!mWalkParticlesInitialized || !mWalkParticlesComputeShader || !mWalkParticlesRenderShader || mWalkParticleCount == 0)
        return;

    if (!mWalkParticlesComputeShader->isLinked())
        return;

    if (playerPlanarSpeed < 1.0f)
        return;

    const float now = getTimeSecs();
    const float dt = (mWalkParticlesTime <= 0.0f) ? 0.0f : std::min(0.03f, now - mWalkParticlesTime);
    mWalkParticlesTime = now;

    const glm::vec3 playerPos = player.getPosition();
    const glm::vec3 footCenter = playerPos + glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 attractor1 = footCenter + glm::vec3(-0.65f, 0.1f, 0.0f);
    const glm::vec3 attractor2 = footCenter + glm::vec3(0.65f, 0.1f, 0.0f);

    const float particleMass = 0.35f;
    const float gravityScale = 1.0f;
    const float pointSize = 2.6f;
    const float particleAlpha = 0.22f;

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mVBOManager->get(VBOs::ID::WALK_PARTICLES_POS_SSBO).get());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mVBOManager->get(VBOs::ID::WALK_PARTICLES_VEL_SSBO).get());

    mWalkParticlesComputeShader->bind();
    mWalkParticlesComputeShader->setUniform("BlackHolePos1", attractor1);
    mWalkParticlesComputeShader->setUniform("BlackHolePos2", attractor2);
    mWalkParticlesComputeShader->setUniform("Gravity1", 210.0f * gravityScale);
    mWalkParticlesComputeShader->setUniform("Gravity2", 210.0f * gravityScale);
    mWalkParticlesComputeShader->setUniform("ParticleInvMass", 1.0f / std::max(0.05f, particleMass));
    mWalkParticlesComputeShader->setUniform("DeltaT", std::max(0.0001f, dt * 0.8f));
    mWalkParticlesComputeShader->setUniform("MaxDist", 4.5f);
    mWalkParticlesComputeShader->setUniform("ParticleCount", mWalkParticleCount);

    const GLuint groupsX = (mWalkParticleCount + 999u) / 1000u;
    glDispatchCompute(groupsX, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    const int safeHeight = std::max(1, windowHeight);
    const float aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(safeHeight);
    const glm::mat4 mvp = camera.getPerspective(aspectRatio) * camera.getLookAt();

    GLboolean blendEnabled = GL_FALSE;
    glGetBooleanv(GL_BLEND, &blendEnabled);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glPointSize(pointSize);

    mWalkParticlesRenderShader->bind();
    mWalkParticlesRenderShader->setUniform("MVP", mvp);
    mWalkParticlesRenderShader->setUniform("Color", glm::vec4(0.46f, 0.30f, 0.17f, std::clamp(particleAlpha, 0.0f, 1.0f)));
    mVAOManager->get(VAOs::ID::WALK_PARTICLES).bind();
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(mWalkParticleCount));

    VertexArrayObject::unbind();
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glDepthMask(GL_TRUE);
    if (!blendEnabled)
        glDisable(GL_BLEND);
}

void World::renderCharacterShadow(const Camera &camera, const Player &player,
                                  int windowWidth, int windowHeight) const noexcept
{
    if (!mShadowsInitialized || !mShadowShader || !mFBOManager || !mShadowTexture || mShadowTexture->get() == 0)
        return;

    if (camera.getMode() != CameraMode::THIRD_PERSON)
        return;

    mFBOManager->get(FBOs::ID::SHADOW).bind();
    glViewport(0, 0, windowWidth, windowHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
    glm::mat4 view = camera.getLookAt();
    glm::mat4 projection = camera.getPerspective(aspectRatio);
    const float timeSeconds = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    const glm::vec3 lightDir = computeSunDirection(timeSeconds);
    const float groundY = mGroundPlane.getPoint().y;

    mShadowShader->bind();
    mShadowShader->setUniform("uViewMatrix", view);
    mShadowShader->setUniform("uProjectionMatrix", projection);
    mShadowShader->setUniform("uLightDir", lightDir);
    mShadowShader->setUniform("uGroundY", groundY);
    mShadowShader->setUniform("uSphereCenter", glm::vec3(0.0f, 0.0f, 0.0f));
    mShadowShader->setUniform("uSphereRadius", 50.0f);

    mVAOManager->get(VAOs::ID::SHADOW_QUAD).bind();

    auto drawBillboardShadow = [this](const glm::vec3 &spritePos, float billboardHalfSize)
    {
        mShadowShader->setUniform("uSpritePos", spritePos);
        mShadowShader->setUniform("uSpriteHalfSize", billboardHalfSize);
        glDrawArrays(GL_POINTS, 0, 1);
    };

    drawBillboardShadow(player.getPosition() + glm::vec3(0.0f, kPlayerShadowCenterYOffset, 0.0f), 3.0f);

    VertexArrayObject::unbind();
    glDisable(GL_BLEND);
    FramebufferObject::unbind();
}

void World::renderPlayerReflection(const Camera &camera, const Player &player,
                                   int windowWidth, int windowHeight) const noexcept
{
    if (!mReflectionsInitialized || !mFBOManager || !mReflectionColorTex || mReflectionColorTex->get() == 0)
        return;

    auto &reflFBO = mFBOManager->get(FBOs::ID::REFLECTION);
    reflFBO.bind();
    GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    FramebufferObject::unbind();

    if (fboStatus != GL_FRAMEBUFFER_COMPLETE)
        return;

    glBindTexture(GL_TEXTURE_2D, mReflectionColorTex->get());
    GLint texWidth = 0, texHeight = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &texWidth);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &texHeight);
    glBindTexture(GL_TEXTURE_2D, 0);

    glFlush();

    reflFBO.bind();
    glViewport(0, 0, windowWidth, windowHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    const int safeHeight = std::max(windowHeight, 1);
    const float aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(safeHeight);
    const float groundY = mGroundPlane.getPoint().y;
    const glm::vec3 modelPos = player.getPosition() + glm::vec3(0.0f, kCharacterModelYOffset, 0.0f);
    glm::vec3 reflectedModelPos = modelPos;
    reflectedModelPos.y = (2.0f * groundY) - modelPos.y;
    (void)reflectedModelPos;
    (void)aspectRatio;

    glFlush();
    FramebufferObject::unbind();
}

void World::handleEvent(const sf::Event &event)
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
                std::cerr << "Error destroying body:\n";
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
            std::cerr << "Error destroying physics world:\n";
        }
        catch (...)
        {
            std::cerr << "Unknown error destroying physics world\n";
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
        std::cerr << "World: Cannot init path tracer - physics world invalid\n";
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
        std::cerr << "World: Failed to generate maze:\n";
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
        std::cerr << "World: Empty maze string for chunk (, )\n";
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
