#include "GameState.hpp"

#include <SDL3/SDL.h>

#include <dearimgui/imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <ranges>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glad/glad.h>

#include "Font.hpp"
#include "GLSDLHelper.hpp"
#include "GLTFModel.hpp"
#include "MusicPlayer.hpp"
#include "Options.hpp"
#include "Player.hpp"
#include "ResourceManager.hpp"
#include "Shader.hpp"
#include "SoundPlayer.hpp"
#include "Sphere.hpp"
#include "StateStack.hpp"
#include "Texture.hpp"

namespace
{
    constexpr float kRunnerLockedSunYawDeg = 0.0f; // +X heading (toward sunset)
    constexpr float kTracksideBillboardBorderMargin = 7.5f;
    constexpr std::size_t kMaxPathTracerSpheres = 200;
    constexpr std::size_t kMaxPathTracerTriangles = 192;
    constexpr bool kEnableTrianglePathTraceProxy = true;
    constexpr float kPlayerProxyRadius = 1.35f;
    constexpr float kPlayerShadowCenterYOffset = 1.35f;
    constexpr float kCharacterRasterScale = 1.42f;
    constexpr float kCharacterPathTraceProxyScale = 1.42f;
    constexpr float kCharacterModelYOffset = 0.25f;

    glm::vec3 computeSunDirection(float /*timeSeconds*/) noexcept
    {
        return glm::normalize(glm::vec3(-1.0f, -0.125f, 0.0f));
    }

    std::pair<int, int> computeRenderResolution(int windowWidth, int windowHeight, std::size_t targetPixels, float minScale) noexcept
    {
        if (windowWidth <= 0 || windowHeight <= 0)
        {
            return {1, 1};
        }

        const std::size_t windowPixels = static_cast<std::size_t>(windowWidth) * static_cast<std::size_t>(windowHeight);
        float scale = 1.0f;

        if (windowPixels > targetPixels)
        {
            scale = std::sqrt(static_cast<float>(targetPixels) / static_cast<float>(windowPixels));
            scale = std::max(scale, minScale);
        }

        const int renderWidth = std::max(1, static_cast<int>(std::lround(static_cast<float>(windowWidth) * scale)));
        const int renderHeight = std::max(1, static_cast<int>(std::lround(static_cast<float>(windowHeight) * scale)));
        return {renderWidth, renderHeight};
    }

    bool projectWorldToScreen(const glm::vec3 &worldPos,
                              const glm::mat4 &view,
                              const glm::mat4 &projection,
                              int windowWidth,
                              int windowHeight,
                              ImVec2 &outScreenPos) noexcept
    {
        if (windowWidth <= 0 || windowHeight <= 0)
        {
            return false;
        }

        const glm::vec4 clip = projection * view * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.0001f)
        {
            return false;
        }

        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z < -1.0f || ndc.z > 1.0f)
        {
            return false;
        }

        outScreenPos.x = (ndc.x * 0.5f + 0.5f) * static_cast<float>(windowWidth);
        outScreenPos.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(windowHeight);
        return true;
    }
}

GameState::GameState(StateStack &stack, Context context)
    : State{stack, context}, mWorld{*context.getRenderWindow(), *context.getFontManager(), *context.getTextureManager(), *context.getShaderManager()}, mPlayer{*context.getPlayer()}, mGameMusic{nullptr}, mDisplayShader{nullptr}, mComputeShader{nullptr}, mCompositeShader{nullptr}, mOITResolveShader{nullptr}, mSkinnedModelShader{nullptr}, mGameIsPaused{false}
      // Initialize camera at maze spawn position (will be updated after first chunk loads)
      ,
      mCamera{}
{
    mPlayer.setActive(true);
    mWorld.init(); // This now initializes both 2D physics and 3D path tracer scene

    // Initialize player animator with character index 0
    mPlayer.initializeAnimator(0);

    // Get shaders from context
    auto &shaders = *context.getShaderManager();
    try
    {
        mDisplayShader = &shaders.get(Shaders::ID::GLSL_FULLSCREEN_QUAD);
        mComputeShader = &shaders.get(Shaders::ID::GLSL_PATH_TRACER_COMPUTE);
        mCompositeShader = &shaders.get(Shaders::ID::GLSL_COMPOSITE_SCENE);
        mOITResolveShader = &shaders.get(Shaders::ID::GLSL_OIT_RESOLVE);
        mSkinnedModelShader = &shaders.get(Shaders::ID::GLSL_MODEL_WITH_SKINNING);
        mShadowShader = &shaders.get(Shaders::ID::GLSL_SHADOW_VOLUME);
        mWalkParticlesComputeShader = &shaders.get(Shaders::ID::GLSL_PARTICLES_COMPUTE);
        mWalkParticlesRenderShader = &shaders.get(Shaders::ID::GLSL_FULLSCREEN_QUAD_MVP);
        mShadersInitialized = true;
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Failed to get shaders from context: %s", e.what());
        mShadersInitialized = false;
    }

    // Get sound player from context
    try
    {
        mSoundPlayer = getContext().getSoundPlayer();
    }
    catch(const std::exception& e)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "GameState: Failed to get SoundPlayer from context: %s", e.what());
    }
    

    auto &textures = *context.getTextureManager();
    try
    {
        mAccumTex = &textures.get(Textures::ID::PATH_TRACER_ACCUM);
        mDisplayTex = &textures.get(Textures::ID::PATH_TRACER_DISPLAY);
        mNoiseTexture = &textures.get(Textures::ID::NOISE2D);
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Failed to get noise texture: %s", e.what());
        mNoiseTexture = nullptr;
    }

    // Get and play game music from context
    auto &music = *context.getMusicManager();
    try
    {
        mGameMusic = &music.get(Music::ID::GAME_MUSIC);
        if (mGameMusic)
        {

            // Start the music - the periodic health check in update() will handle restarts if needed
            bool wasPlaying = mGameMusic->isPlaying();

            if (!wasPlaying)
            {
                mGameMusic->play();

                // Check immediately after
                bool nowPlaying = mGameMusic->isPlaying();
            }
        }
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "GameState: Failed to get game music: %s", e.what());
        mGameMusic = nullptr;
    }

    // Trigger initial chunk load to get maze spawn position
    mWorld.updateSphereChunks(mCamera.getPosition());

    // Set initial player position at maze spawn
    glm::vec3 spawnPos = mWorld.getMazeSpawnPosition();
    mPlayer.setPosition(spawnPos);

    // Update camera to an elevated position behind the spawn
    glm::vec3 cameraSpawn = spawnPos + glm::vec3(0.0f, 50.0f, 50.0f);
    mCamera.setPosition(cameraSpawn);

    // Default to third-person for arcade runner readability
    mCamera.setMode(CameraMode::THIRD_PERSON);
    mCamera.setYawPitch(kRunnerLockedSunYawDeg, mCamera.getPitch());
    mCamera.setFollowTarget(spawnPos);
    mCamera.setThirdPersonDistance(15.0f);
    mCamera.setThirdPersonHeight(8.0f);
    mCamera.updateThirdPersonPosition();

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameState: Camera spawned at:\t%.2f, %.2f, %.2f",
        cameraSpawn.x, cameraSpawn.y, cameraSpawn.z);

    // Initialize camera tracking
    mLastCameraPosition = mCamera.getPosition();
    mLastCameraYaw = mCamera.getYaw();
    mLastCameraPitch = mCamera.getPitch();

    mRunnerRng.seed(static_cast<unsigned int>(std::abs(spawnPos.x) + std::abs(spawnPos.z) + 1337.0f));
    syncRunnerSettingsFromOptions();
    resetRunnerRun();
    mWorld.updateSphereChunks(mPlayer.getPosition());

    // Initialize GPU graphics pipeline following Compute.cpp approach
    if (mShadersInitialized)
    {
        initializeGraphicsResources();
        initializeWalkParticles();
        initializeRunnerBreakPlaneResources();

        // Initialize billboard rendering for character sprites
        GLSDLHelper::initializeBillboardRendering();
    }
}

GameState::~GameState()
{
    // Stop game music when leaving GameState
    if (mGameMusic)
    {
        mGameMusic->stop();
    }

    cleanupResources();
}

void GameState::draw() const noexcept
{
    if (mShadersInitialized)
    {
        // Use compute shader rendering pipeline (path tracing)
        renderWithComputeShaders();

        // Animate break-plane texture in an offscreen pass.
        renderRunnerBreakPlaneTexture();

        // Keep rasterized character overlay for readability even when triangle proxy is enabled.
        renderPlayerCharacter();

        // Render character shadow to shadow texture
        renderCharacterShadow();

        // Render player reflection on ground plane
        renderPlayerReflection();

        // Note: Voronoi planet is rendered into the billboard FBO in renderPlayerCharacter()

        // Reset viewport to main window before compositing
        glViewport(0, 0, mWindowWidth, mWindowHeight);

        if (mCompositeShader && mBillboardFBO != 0 && mBillboardColorTex != 0)
        {
            renderCompositeScene();
        }
    }

    drawRunnerHud();
}

void GameState::initializeGraphicsResources() noexcept
{
    if (auto *window = getContext().getRenderWindow(); window != nullptr)
    {
        if (SDL_Window *sdlWindow = window->getSDLWindow(); sdlWindow != nullptr)
        {
            int width = 0;
            int height = 0;
            SDL_GetWindowSizeInPixels(sdlWindow, &width, &height);
            if (width > 0 && height > 0)
            {
                mWindowWidth = width;
                mWindowHeight = height;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameState: Initial window size in pixels: %dx%d",
                    mWindowWidth, mWindowHeight);
            }
        }
    }

    // Enable OpenGL features using helper
    GLSDLHelper::enableRenderingFeatures();

    // Create VAO for fullscreen quad using helper
    mVAO = GLSDLHelper::createAndBindVAO();

    // Create SSBO for sphere data using helper
    mShapeSSBO = GLSDLHelper::createAndBindSSBO(1);
    mTriangleSSBO = GLSDLHelper::createAndBindSSBO(2);

    updateRenderResolution();

    // Create textures for path tracing
    createPathTracerTextures();
    createCompositeTargets();
    initializeShadowResources();     // Initialize shadow rendering
    initializeReflectionResources(); // Initialize reflection rendering

    // Upload sphere data from World to GPU with extra capacity for dynamic spawning
    const auto &spheres = mWorld.getSpheres();

    // Allocate buffer with 4x capacity to handle chunk-based spawning
    // This reduces frequent reallocations as spheres are loaded/unloaded
    size_t initialCapacity = std::max(spheres.size() * 4, size_t(1000));
    size_t bufferSize = initialCapacity * sizeof(Sphere);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mShapeSSBO);
    GLSDLHelper::allocateSSBOBuffer(static_cast<GLsizeiptr>(bufferSize), nullptr);
    mShapeSSBOCapacityBytes = bufferSize;

    // Upload initial sphere data
    if (!spheres.empty())
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, mShapeSSBO);
        GLSDLHelper::updateSSBOBuffer(0, static_cast<GLsizeiptr>(spheres.size() * sizeof(Sphere)), spheres.data());
    }

    const std::size_t triangleBufferSize = kMaxPathTracerTriangles * sizeof(GLTFModel::RayTraceTriangle);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, mTriangleSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mTriangleSSBO);
    GLSDLHelper::allocateSSBOBuffer(static_cast<GLsizeiptr>(triangleBufferSize), nullptr);
    mTriangleSSBOCapacityBytes = triangleBufferSize;

    // Create Voronoi planet shader and planet mesh
    try
    {
        // mVoronoiShaderOwned = std::make_unique<Shader>();
        // mVoronoiShaderOwned->compileAndAttachShader(Shader::ShaderType::VERTEX, "shaders/voronoi.vert.glsl");
        // mVoronoiShaderOwned->compileAndAttachShader(Shader::ShaderType::FRAGMENT, "shaders/voronoi.frag.glsl");
        // mVoronoiShaderOwned->linkProgram();
        // mVoronoiShader = mVoronoiShaderOwned.get();

        // Initialize planet with moderate seed count
        mVoronoiPlanet.initialize(2048, 128, 64);
        mVoronoiPlanet.uploadToGPU();
    }
    catch (const std::exception &e)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "GameState: Voronoi planet shader/mesh init failed: %s", e.what());
    }
}

void GameState::initializeRunnerBreakPlaneResources() noexcept
{
    if (mRunnerBreakPlaneTexture != 0 && mRunnerBreakPlaneFBO != 0)
    {
        return;
    }

    if (mRunnerBreakPlaneTexture == 0)
    {
        glGenTextures(1, &mRunnerBreakPlaneTexture);
    }

    glBindTexture(GL_TEXTURE_2D, mRunnerBreakPlaneTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA16F,
                 mRunnerBreakPlaneTextureWidth,
                 mRunnerBreakPlaneTextureHeight,
                 0,
                 GL_RGBA,
                 GL_FLOAT,
                 nullptr);

    if (mRunnerBreakPlaneFBO == 0)
    {
        glGenFramebuffers(1, &mRunnerBreakPlaneFBO);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, mRunnerBreakPlaneFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mRunnerBreakPlaneTexture, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "GameState: Break-plane texture framebuffer incomplete");
    }

    if (mRunnerBreakPlanePosSSBO == 0 || mRunnerBreakPlaneVelSSBO == 0 || mRunnerBreakPlaneVAO == 0)
    {
        constexpr int gridX = 32;
        constexpr int gridY = 16;
        constexpr int gridZ = 8;
        mRunnerBreakPlaneParticleCount = static_cast<GLuint>(gridX * gridY * gridZ);

        std::vector<GLfloat> initPos;
        initPos.reserve(static_cast<std::size_t>(mRunnerBreakPlaneParticleCount) * 4u);
        std::vector<GLfloat> initVel(static_cast<std::size_t>(mRunnerBreakPlaneParticleCount) * 4u, 0.0f);

        const GLfloat dx = 4.0f / static_cast<GLfloat>(gridX - 1);
        const GLfloat dy = 4.0f / static_cast<GLfloat>(gridY - 1);
        const GLfloat dz = 3.0f / static_cast<GLfloat>(gridZ - 1);

        for (int i = 0; i < gridX; ++i)
        {
            for (int j = 0; j < gridY; ++j)
            {
                for (int k = 0; k < gridZ; ++k)
                {
                    initPos.push_back(-2.0f + dx * static_cast<GLfloat>(i));
                    initPos.push_back(-2.0f + dy * static_cast<GLfloat>(j));
                    initPos.push_back(-1.5f + dz * static_cast<GLfloat>(k));
                    initPos.push_back(1.0f);
                }
            }
        }

        const GLsizeiptr bufferSize = static_cast<GLsizeiptr>(static_cast<std::size_t>(mRunnerBreakPlaneParticleCount) * 4u * sizeof(GLfloat));

        if (mRunnerBreakPlanePosSSBO == 0)
        {
            glGenBuffers(1, &mRunnerBreakPlanePosSSBO);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, mRunnerBreakPlanePosSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, initPos.data(), GL_DYNAMIC_DRAW);

        if (mRunnerBreakPlaneVelSSBO == 0)
        {
            glGenBuffers(1, &mRunnerBreakPlaneVelSSBO);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, mRunnerBreakPlaneVelSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, initVel.data(), GL_DYNAMIC_DRAW);

        if (mRunnerBreakPlaneVAO == 0)
        {
            glGenVertexArrays(1, &mRunnerBreakPlaneVAO);
        }
        glBindVertexArray(mRunnerBreakPlaneVAO);
        glBindBuffer(GL_ARRAY_BUFFER, mRunnerBreakPlanePosSSBO);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GameState::renderRunnerBreakPlaneTexture() const noexcept
{
    if (mRunnerBreakPlaneTexture == 0 || mRunnerBreakPlaneFBO == 0 || mRunnerBreakPlaneVAO == 0)
    {
        return;
    }

    if (!mWalkParticlesComputeShader || !mWalkParticlesRenderShader || mRunnerBreakPlaneParticleCount == 0)
    {
        return;
    }

    const float now = static_cast<float>(SDL_GetTicks()) * 0.001f;
    const float dt = (mRunnerBreakPlaneFxTime <= 0.0f) ? 0.0f : std::min(0.03f, now - mRunnerBreakPlaneFxTime);
    mRunnerBreakPlaneFxTime = now;

    const float phase = now * 0.85f;
    const glm::vec3 attractor1(1.7f * std::cos(phase), 1.2f * std::sin(phase * 1.9f), 0.0f);
    const glm::vec3 attractor2(-1.7f * std::cos(phase * 1.12f), -1.1f * std::sin(phase * 1.4f), 0.0f);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mRunnerBreakPlanePosSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mRunnerBreakPlaneVelSSBO);

    mWalkParticlesComputeShader->bind();
    mWalkParticlesComputeShader->setUniform("BlackHolePos1", attractor1);
    mWalkParticlesComputeShader->setUniform("BlackHolePos2", attractor2);
    mWalkParticlesComputeShader->setUniform("Gravity1", 185.0f);
    mWalkParticlesComputeShader->setUniform("Gravity2", 195.0f);
    mWalkParticlesComputeShader->setUniform("ParticleInvMass", 1.0f / 0.12f);
    mWalkParticlesComputeShader->setUniform("DeltaT", std::max(0.0001f, dt * 0.45f));
    mWalkParticlesComputeShader->setUniform("MaxDist", 5.1f);
    mWalkParticlesComputeShader->setUniform("ParticleCount", mRunnerBreakPlaneParticleCount);

    const GLuint groupsX = (mRunnerBreakPlaneParticleCount + 999u) / 1000u;
    glDispatchCompute(groupsX, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    glBindFramebuffer(GL_FRAMEBUFFER, mRunnerBreakPlaneFBO);
    glViewport(0, 0, mRunnerBreakPlaneTextureWidth, mRunnerBreakPlaneTextureHeight);
    glClearColor(0.18f, 0.28f, 0.40f, 0.58f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);

    GLboolean blendEnabled = GL_FALSE;
    glGetBooleanv(GL_BLEND, &blendEnabled);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const float safeHeight = static_cast<float>(std::max(1, mRunnerBreakPlaneTextureHeight));
    const float aspect = static_cast<float>(mRunnerBreakPlaneTextureWidth) / safeHeight;
    const glm::mat4 projection = glm::perspective(glm::radians(48.0f), aspect, 0.1f, 60.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 11.0f),
                                       glm::vec3(0.0f, 0.0f, 0.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 mvp = projection * view;

    mWalkParticlesRenderShader->bind();
    mWalkParticlesRenderShader->setUniform("MVP", mvp);
    mWalkParticlesRenderShader->setUniform("Color", glm::vec4(0.86f, 0.94f, 1.0f, 0.55f));

    glPointSize(2.6f);
    glBindVertexArray(mRunnerBreakPlaneVAO);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(mRunnerBreakPlaneParticleCount));

    glBindVertexArray(0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);
    glViewport(0, 0, mWindowWidth, mWindowHeight);

    if (!blendEnabled)
    {
        glDisable(GL_BLEND);
    }
}

void GameState::updateRenderResolution() noexcept
{
    const auto [newRenderWidth, newRenderHeight] =
        computeRenderResolution(mWindowWidth, mWindowHeight, kTargetRenderPixels, kMinRenderScale);

    mRenderWidth = newRenderWidth;
    mRenderHeight = newRenderHeight;
}

void GameState::createPathTracerTextures() noexcept
{
    // Update existing textures to new resolution
    // (They were initially created at 512x512 in LoadingState)
    if (!mAccumTex || !mDisplayTex)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Path tracer textures not initialized!");
        return;
    }

    bool accumSuccess = mAccumTex->loadRGBA32F(
        static_cast<int>(mRenderWidth),
        static_cast<int>(mRenderHeight),
        0);

    bool displaySuccess = mDisplayTex->loadRGBA32F(
        static_cast<int>(mRenderWidth),
        static_cast<int>(mRenderHeight),
        0);

    if (!accumSuccess || !displaySuccess)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                     "GameState: Failed to resize path tracer textures (accum: %d, display: %d)",
                     accumSuccess, displaySuccess);
        return;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameState: Path tracer textures recreated (window/internal):\t%dx%d / %dx%d (IDs: %u, %u)",
        mWindowWidth, mWindowHeight, mRenderWidth, mRenderHeight,
        mAccumTex->get(), mDisplayTex->get());
}

void GameState::handleWindowResize() noexcept
{
    // Window resize is now handled in handleEvent() with proper physical pixel detection
    // This method is kept for future use but resize handling is in event loop
}

void GameState::createCompositeTargets() noexcept
{
    if (mWindowWidth <= 0 || mWindowHeight <= 0)
    {
        return;
    }

    if (mBillboardColorTex == 0)
    {
        glGenTextures(1, &mBillboardColorTex);
    }
    glBindTexture(GL_TEXTURE_2D, mBillboardColorTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, mWindowWidth, mWindowHeight, 0, GL_RGBA, GL_FLOAT, nullptr);

    if (mBillboardDepthRbo == 0)
    {
        glGenRenderbuffers(1, &mBillboardDepthRbo);
    }
    glBindRenderbuffer(GL_RENDERBUFFER, mBillboardDepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mWindowWidth, mWindowHeight);

    if (mBillboardFBO == 0)
    {
        glGenFramebuffers(1, &mBillboardFBO);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, mBillboardFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mBillboardColorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mBillboardDepthRbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Billboard framebuffer incomplete");
    }
    else
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameState: Billboard FBO=%u, ColorTex=%u", mBillboardFBO, mBillboardColorTex);
    }

    if (mOITAccumTex == 0)
    {
        glGenTextures(1, &mOITAccumTex);
    }
    glBindTexture(GL_TEXTURE_2D, mOITAccumTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, mWindowWidth, mWindowHeight, 0, GL_RGBA, GL_FLOAT, nullptr);

    if (mOITRevealTex == 0)
    {
        glGenTextures(1, &mOITRevealTex);
    }
    glBindTexture(GL_TEXTURE_2D, mOITRevealTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, mWindowWidth, mWindowHeight, 0, GL_RED, GL_FLOAT, nullptr);

    if (mOITDepthRbo == 0)
    {
        glGenRenderbuffers(1, &mOITDepthRbo);
    }
    glBindRenderbuffer(GL_RENDERBUFFER, mOITDepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mWindowWidth, mWindowHeight);

    if (mOITFBO == 0)
    {
        glGenFramebuffers(1, &mOITFBO);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, mOITFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mOITAccumTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, mOITRevealTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mOITDepthRbo);
    constexpr GLenum oitDrawBuffers[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, oitDrawBuffers);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: OIT framebuffer incomplete");
        mOITInitialized = false;
    }
    else
    {
        mOITInitialized = true;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK); // CRITICAL: Reset to default for main framebuffer
    glReadBuffer(GL_BACK); // CRITICAL: Reset to default for main framebuffer
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void GameState::initializeShadowResources() noexcept
{
    if (mWindowWidth <= 0 || mWindowHeight <= 0)
    {
        return;
    }

    // Delete existing resources if resizing
    if (mShadowTexture != 0)
    {
        glDeleteTextures(1, &mShadowTexture);
        mShadowTexture = 0;
    }
    if (mShadowFBO != 0)
    {
        glDeleteFramebuffers(1, &mShadowFBO);
        mShadowFBO = 0;
    }

    // Create shadow map texture
    glGenTextures(1, &mShadowTexture);
    glBindTexture(GL_TEXTURE_2D, mShadowTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, mWindowWidth, mWindowHeight, 0, GL_RGBA, GL_FLOAT, nullptr);

    // Create shadow FBO
    glGenFramebuffers(1, &mShadowFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, mShadowFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mShadowTexture, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Shadow framebuffer incomplete");
    }

    // Clear the framebuffer to ensure no stale data
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Create shadow quad (single point that will be expanded to quad by geometry shader) - only once
    if (mShadowVAO == 0)
    {
        glGenVertexArrays(1, &mShadowVAO);
        glGenBuffers(1, &mShadowVBO);

        glBindVertexArray(mShadowVAO);
        glBindBuffer(GL_ARRAY_BUFFER, mShadowVBO);

        // Single point at origin - geometry shader will expand to quad
        float point = 0.0f;
        glBufferData(GL_ARRAY_BUFFER, sizeof(float), &point, GL_STATIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK); // CRITICAL: Reset to default for main framebuffer
    glReadBuffer(GL_BACK); // CRITICAL: Reset to default for main framebuffer
    glBindTexture(GL_TEXTURE_2D, 0);

    mShadowsInitialized = true;
}

void GameState::initializeReflectionResources() noexcept
{
    if (mWindowWidth <= 0 || mWindowHeight <= 0)
    {
        SDL_LogWarn(
            SDL_LOG_CATEGORY_APPLICATION,
            "GameState: initializeReflectionResources() early return - invalid dimensions: %dx%d",
            mWindowWidth, mWindowHeight);
        return;
    }

    // Always unbind and validate before reallocation
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // Delete existing resources if resizing
    if (mReflectionColorTex != 0)
    {
        glDeleteTextures(1, &mReflectionColorTex);
        mReflectionColorTex = 0;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameState: Deleted old reflection texture");
    }
    if (mReflectionDepthRbo != 0)
    {
        glDeleteRenderbuffers(1, &mReflectionDepthRbo);
        mReflectionDepthRbo = 0;
    }
    if (mReflectionFBO != 0)
    {
        glDeleteFramebuffers(1, &mReflectionFBO);
        mReflectionFBO = 0;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameState: Deleted old reflection FBO");
    }

    // Create reflection color texture
    glGenTextures(1, &mReflectionColorTex);
    glBindTexture(GL_TEXTURE_2D, mReflectionColorTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, mWindowWidth, mWindowHeight, 0, GL_RGBA, GL_FLOAT, nullptr);

    // Verify texture size was created correctly
    GLint texWidth = 0, texHeight = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &texWidth);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &texHeight);

    // Create reflection depth buffer
    glGenRenderbuffers(1, &mReflectionDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, mReflectionDepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mWindowWidth, mWindowHeight);

    // Create reflection FBO
    glGenFramebuffers(1, &mReflectionFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, mReflectionFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mReflectionColorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mReflectionDepthRbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fboStatus != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Reflection framebuffer incomplete: 0x%x", fboStatus);
        return;
    }

    // Clear the framebuffer to ensure no stale data
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Explicitly unbind before returning
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK); // CRITICAL: Reset to default for main framebuffer
    glReadBuffer(GL_BACK); // CRITICAL: Reset to default for main framebuffer
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    mReflectionsInitialized = true;
}

void GameState::initializeWalkParticles() noexcept
{
    if (mWalkParticlesInitialized)
    {
        return;
    }

    if (!mWalkParticlesComputeShader || !mWalkParticlesRenderShader || mWalkParticleCount == 0)
    {
        return;
    }

    std::vector<GLfloat> initPos;
    std::vector<GLfloat> initVel;
    initPos.reserve(static_cast<std::size_t>(mWalkParticleCount) * 4u);
    initVel.reserve(static_cast<std::size_t>(mWalkParticleCount) * 4u);

    const glm::vec3 playerPos = mPlayer.getPosition();
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

    glGenBuffers(1, &mWalkParticlesPosSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mWalkParticlesPosSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, initPos.data(), GL_DYNAMIC_DRAW);

    glGenBuffers(1, &mWalkParticlesVelSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mWalkParticlesVelSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, initVel.data(), GL_DYNAMIC_DRAW);

    glGenVertexArrays(1, &mWalkParticlesVAO);
    glBindVertexArray(mWalkParticlesVAO);
    glBindBuffer(GL_ARRAY_BUFFER, mWalkParticlesPosSSBO);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    mWalkParticlesInitialized = true;
}

void GameState::renderWalkParticles() const noexcept
{
    if (!mWalkParticlesInitialized || !mWalkParticlesComputeShader || !mWalkParticlesRenderShader || mWalkParticleCount == 0)
    {
        return;
    }

    if (mPlayerPlanarSpeedForFx < 1.0f)
    {
        return;
    }

    const float now = static_cast<float>(SDL_GetTicks()) * 0.001f;
    const float dt = (mWalkParticlesTime <= 0.0f) ? 0.0f : std::min(0.03f, now - mWalkParticlesTime);
    mWalkParticlesTime = now;

    const glm::vec3 playerPos = mPlayer.getPosition();
    const glm::vec3 footCenter = playerPos + glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 attractor1 = footCenter + glm::vec3(-0.65f, 0.1f, 0.0f);
    const glm::vec3 attractor2 = footCenter + glm::vec3(0.65f, 0.1f, 0.0f);
    const float scoreBoost = getScoreBracketBoost();

    // Higher score tiers intensify particle presence.
    const float particleMass = 0.35f + scoreBoost * 1.15f;
    const float gravityScale = 1.0f + scoreBoost * 1.10f;
    const float pointSize = 2.6f + scoreBoost * 2.2f;
    const float particleAlpha = 0.22f + scoreBoost * 0.22f;

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mWalkParticlesPosSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mWalkParticlesVelSSBO);

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

    const int safeHeight = std::max(1, mWindowHeight);
    const float aspectRatio = static_cast<float>(mWindowWidth) / static_cast<float>(safeHeight);
    const glm::mat4 mvp = mCamera.getPerspective(aspectRatio) * mCamera.getLookAt();

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
    glBindVertexArray(mWalkParticlesVAO);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(mWalkParticleCount));

    glBindVertexArray(0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glDepthMask(GL_TRUE);
    if (!blendEnabled)
    {
        glDisable(GL_BLEND);
    }
}

bool GameState::checkCameraMovement() const noexcept
{
    glm::vec3 currentPos = mCamera.getPosition();
    float currentYaw = mCamera.getYaw();
    float currentPitch = mCamera.getPitch();

    const float posEpsilon = 0.01f;
    const float angleEpsilon = 0.1f;

    if (glm::distance(currentPos, mLastCameraPosition) > posEpsilon ||
        std::abs(currentYaw - mLastCameraYaw) > angleEpsilon ||
        std::abs(currentPitch - mLastCameraPitch) > angleEpsilon)
    {
        // Camera moved - update tracking only.
        // Accumulation policy (including motion blur windowing) is handled in renderWithComputeShaders().
        mLastCameraPosition = currentPos;
        mLastCameraYaw = currentYaw;
        mLastCameraPitch = currentPitch;
        return true;
    }
    return false;
}

float GameState::getScoreBracketBoost() const noexcept
{
    if (mPlayerPoints >= mMotionBlurBracket4Points)
    {
        return mMotionBlurBracket4Boost;
    }
    if (mPlayerPoints >= mMotionBlurBracket3Points)
    {
        return mMotionBlurBracket3Boost;
    }
    if (mPlayerPoints >= mMotionBlurBracket2Points)
    {
        return mMotionBlurBracket2Boost;
    }
    if (mPlayerPoints >= mMotionBlurBracket1Points)
    {
        return mMotionBlurBracket1Boost;
    }
    return 0.0f;
}

void GameState::renderCharacterShadow() const noexcept
{
    if (!mShadowsInitialized || !mShadowShader || mShadowFBO == 0 || mShadowTexture == 0)
    {
        return;
    }

    if (mCamera.getMode() != CameraMode::THIRD_PERSON)
    {
        return;
    }

    // Render shadow to texture
    glBindFramebuffer(GL_FRAMEBUFFER, mShadowFBO);
    glViewport(0, 0, mWindowWidth, mWindowHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Enable blending for soft shadow edges
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Get camera matrices (same as main rendering)
    float aspectRatio = static_cast<float>(mWindowWidth) / static_cast<float>(mWindowHeight);
    glm::mat4 view = mCamera.getLookAt();
    glm::mat4 projection = mCamera.getPerspective(aspectRatio);
    const float timeSeconds = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    const glm::vec3 lightDir = computeSunDirection(timeSeconds);
    const float groundY = mWorld.getGroundPlane().getPoint().y;

    // Render billboard shadows by projecting sprite footprint along light direction onto the ground plane
    mShadowShader->bind();
    mShadowShader->setUniform("uViewMatrix", view);
    mShadowShader->setUniform("uProjectionMatrix", projection);
    mShadowShader->setUniform("uLightDir", lightDir);
    mShadowShader->setUniform("uGroundY", groundY);
    mShadowShader->setUniform("uInvResolution", glm::vec2(1.0f / static_cast<float>(mWindowWidth), 1.0f / static_cast<float>(mWindowHeight)));

    glBindVertexArray(mShadowVAO);

    auto drawBillboardShadow = [this](const glm::vec3 &spritePos, float billboardHalfSize)
    {
        mShadowShader->setUniform("uSpritePos", spritePos);
        mShadowShader->setUniform("uSpriteHalfSize", billboardHalfSize);
        glDrawArrays(GL_POINTS, 0, 1);
    };

    // Local character billboard shadow
    drawBillboardShadow(mPlayer.getPosition() + glm::vec3(0.0f, kPlayerShadowCenterYOffset, 0.0f), 3.0f);

    // Decorative trackside sprite shadows (same layout as renderTracksideBillboards)
    if (mArcadeModeEnabled)
    {
        const glm::vec3 playerPos = mPlayer.getPosition();
        constexpr int kBillboardsAhead = 9;
        constexpr int kBillboardsBehind = 4;
        constexpr float kBillboardSpacing = 28.0f;
        constexpr float kBillboardHeight = 2.9f;
        constexpr float kBorderMargin = 7.5f;

        const float borderZ = mRunnerStrafeLimit + kBorderMargin;

        for (int i = -kBillboardsBehind; i <= kBillboardsAhead; ++i)
        {
            const float x = playerPos.x + static_cast<float>(i) * kBillboardSpacing;
            const float stagger = (i & 1) == 0 ? 0.0f : 2.0f;

            drawBillboardShadow(glm::vec3(x, kBillboardHeight, borderZ + stagger), 3.0f);
            drawBillboardShadow(glm::vec3(x, kBillboardHeight, -borderZ - stagger), 3.0f);
        }
    }

    glBindVertexArray(0);

    // Disable blending
    glDisable(GL_BLEND);

    // Reset framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GameState::renderPlayerReflection() const noexcept
{
    if (!mReflectionsInitialized || mReflectionFBO == 0 || mReflectionColorTex == 0)
    {
        return;
    }

    // Validate reflection FBO is still valid and matches window size
    glBindFramebuffer(GL_FRAMEBUFFER, mReflectionFBO);
    GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (fboStatus != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "GameState: Reflection FBO invalid, skipping reflection: 0x%x", fboStatus);
        return;
    }

    // Verify texture size matches expected size
    glBindTexture(GL_TEXTURE_2D, mReflectionColorTex);
    GLint texWidth = 0, texHeight = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &texWidth);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &texHeight);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (texWidth != mWindowWidth || texHeight != mWindowHeight)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                    "GameState: Reflection texture size mismatch! Expected %dx%d, got %dx%d",
                    mWindowWidth, mWindowHeight, texWidth, texHeight);
    }

    // Ensure clean state before rendering
    glFlush();

    // Render reflection to texture
    glBindFramebuffer(GL_FRAMEBUFFER, mReflectionFBO);
    glViewport(0, 0, mWindowWidth, mWindowHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    // Get camera matrices
    const int safeHeight = std::max(mWindowHeight, 1);
    const float aspectRatio = static_cast<float>(mWindowWidth) / static_cast<float>(safeHeight);
    // Render reflected player - mirror model position across the actual ground plane
    const float groundY = mWorld.getGroundPlane().getPoint().y;
    const glm::vec3 modelPos = mPlayer.getPosition() + glm::vec3(0.0f, kCharacterModelYOffset, 0.0f);
    glm::vec3 reflectedModelPos = modelPos;
    reflectedModelPos.y = (2.0f * groundY) - modelPos.y;
    if (mSkinnedModelShader)
    {
        auto *models = getContext().getModelsManager();
        if (models)
        {
            try
            {
                const GLTFModel &characterModel = models->get(Models::ID::STYLIZED_CHARACTER);
                if (characterModel.isLoaded())
                {
                    const float timeSeconds = static_cast<float>(SDL_GetTicks()) / 1000.0f;

                    glm::mat4 modelMatrix(1.0f);
                    modelMatrix = glm::translate(modelMatrix, reflectedModelPos);
                    modelMatrix = glm::rotate(modelMatrix, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                    modelMatrix = glm::scale(modelMatrix, glm::vec3(kCharacterRasterScale));

                    // Scale Y negative for reflection
                    modelMatrix = glm::scale(modelMatrix, glm::vec3(1.0f, -1.0f, 1.0f));

                    mSkinnedModelShader->bind();
                    mSkinnedModelShader->setUniform("uSunDir", computeSunDirection(timeSeconds));

                    glm::mat4 viewMatrix = mCamera.getLookAt();
                    glm::mat4 projMatrix = mCamera.getPerspective(aspectRatio);

                    characterModel.render(
                        *mSkinnedModelShader,
                        modelMatrix,
                        viewMatrix,
                        projMatrix,
                        mModelAnimTimeSeconds);
                }
            }
            catch (const std::exception &e)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "GameState: Reflection render failed: %s", e.what());
            }
        }
    }

    // Ensure reflection rendering completes before unbinding
    glFlush();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GameState::renderWithComputeShaders() const noexcept
{
    if (!mComputeShader || !mDisplayShader)
    {
        return;
    }

    if (auto *window = getContext().getRenderWindow(); window != nullptr)
    {
        if (SDL_Window *sdlWindow = window->getSDLWindow(); sdlWindow != nullptr)
        {
            int width = 0;
            int height = 0;
            SDL_GetWindowSize(sdlWindow, &width, &height);
            // Window resize handling is done in update() for better state management
        }
    }

    // Keep camera tracking up to date for accumulation policy decisions
    checkCameraMovement();

    // Update sphere data on GPU every frame (physics may have changed positions)
    const auto &spheres = mWorld.getSpheres();

    std::vector<Sphere> renderSpheres;
    renderSpheres.reserve(std::min<std::size_t>(spheres.size() + 1, kMaxPathTracerSpheres));

    const std::size_t baseCount = std::min<std::size_t>(spheres.size(), kMaxPathTracerSpheres);
    renderSpheres.insert(renderSpheres.end(), spheres.begin(), spheres.begin() + static_cast<std::ptrdiff_t>(baseCount));

    const std::size_t totalSphereCount = renderSpheres.size();
    const std::size_t primarySphereCount = totalSphereCount;

    // Safety check: ensure we have spheres to render
    if (renderSpheres.empty())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "No spheres to render");
        return;
    }

    // Calculate required buffer size
    const std::size_t requiredSize = renderSpheres.size() * sizeof(Sphere);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mShapeSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mShapeSSBO);

    // Check if we need to reallocate the buffer (sphere count changed)
    if (mShapeSSBOCapacityBytes < requiredSize)
    {
        // Reallocating buffer with new size (add some headroom to avoid frequent reallocations)
        const std::size_t newSize = requiredSize * 2;
        GLSDLHelper::allocateSSBOBuffer(static_cast<GLsizeiptr>(newSize), renderSpheres.data());
        mShapeSSBOCapacityBytes = newSize;
    }
    else
    {
        // Update existing buffer
        GLSDLHelper::updateSSBOBuffer(0, static_cast<GLsizeiptr>(requiredSize), renderSpheres.data());
    }

    const float timeSeconds = static_cast<float>(SDL_GetTicks()) / 1000.0f;

    std::vector<GLTFModel::RayTraceTriangle> traceTriangles;
    if (kEnableTrianglePathTraceProxy)
    {
        traceTriangles.reserve(kMaxPathTracerTriangles);
    }

    auto *models = getContext().getModelsManager();
    if (kEnableTrianglePathTraceProxy && models)
    {
        try
        {
            const GLTFModel &characterModel = models->get(Models::ID::STYLIZED_CHARACTER);
            if (characterModel.isLoaded())
            {
                glm::mat4 modelMatrix(1.0f);
                modelMatrix = glm::translate(modelMatrix, mPlayer.getPosition() + glm::vec3(0.0f, kCharacterModelYOffset, 0.0f));
                modelMatrix = glm::rotate(modelMatrix, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                modelMatrix = glm::scale(modelMatrix, glm::vec3(kCharacterPathTraceProxyScale));

                characterModel.extractRayTraceTriangles(
                    traceTriangles,
                    modelMatrix,
                    mModelAnimTimeSeconds,
                    kMaxPathTracerTriangles);
            }
        }
        catch (const std::exception &)
        {
        }
    }

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, mTriangleSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mTriangleSSBO);

    const std::size_t triangleBytes = traceTriangles.size() * sizeof(GLTFModel::RayTraceTriangle);
    if (kEnableTrianglePathTraceProxy && triangleBytes > 0)
    {
        if (mTriangleSSBOCapacityBytes < triangleBytes)
        {
            const std::size_t newSize = std::max(triangleBytes * 2, mTriangleSSBOCapacityBytes * 2);
            GLSDLHelper::allocateSSBOBuffer(static_cast<GLsizeiptr>(newSize), traceTriangles.data());
            mTriangleSSBOCapacityBytes = newSize;
        }
        else
        {
            GLSDLHelper::updateSSBOBuffer(0, static_cast<GLsizeiptr>(triangleBytes), traceTriangles.data());
        }

        // Ensure SSBO data is visible to compute shader before dispatch
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
    }

    // Dynamic motion: keep a short rolling accumulation window to get temporal motion blur
    // without indefinitely smearing stale history.
    constexpr float kMotionBlurMinSpeed = 0.60f;
    constexpr float kMotionBlurFullSpeed = 9.0f;
    constexpr float kStaticHistoryBlend = 0.92f;
    constexpr float kMovingHistoryBlend = 0.78f;

    const float speed = mPlayerPlanarSpeedForFx;
    const float denom = std::max(0.001f, kMotionBlurFullSpeed - kMotionBlurMinSpeed);
    const float blurFactor = std::clamp((speed - kMotionBlurMinSpeed) / denom, 0.0f, 1.0f);

    // Score brackets: increase temporal blur as player points rise so movement feels faster.
    const float scoreBlurBoost = getScoreBracketBoost();

    const float effectiveBlurFactor = std::clamp(blurFactor + scoreBlurBoost, 0.0f, 1.0f);
    const float historyBlend = kStaticHistoryBlend + (kMovingHistoryBlend - kStaticHistoryBlend) * effectiveBlurFactor;

    // Always compute each frame; temporal history blending controls persistence.
    if (mTotalBatches > 0u)
    {
        mComputeShader->bind();

        // Calculate aspect ratio
        float ar = static_cast<float>(mRenderWidth) / static_cast<float>(mRenderHeight);

        // Set camera uniforms (following Compute.cpp renderPathTracer)
        mComputeShader->setUniform("uCamera.eye", mCamera.getActualPosition());
        mComputeShader->setUniform("uCamera.far", mCamera.getFar());
        mComputeShader->setUniform("uCamera.ray00", mCamera.getFrustumEyeRay(ar, -1, -1));
        mComputeShader->setUniform("uCamera.ray01", mCamera.getFrustumEyeRay(ar, -1, 1));
        mComputeShader->setUniform("uCamera.ray10", mCamera.getFrustumEyeRay(ar, 1, -1));
        mComputeShader->setUniform("uCamera.ray11", mCamera.getFrustumEyeRay(ar, 1, 1));

        const uint32_t samplesPerBatch = kEnableTrianglePathTraceProxy ? 1u : mSamplesPerBatch;


        // Set batch uniforms for progressive rendering
        mComputeShader->setUniform("uBatch", mCurrentBatch);
        mComputeShader->setUniform("uSamplesPerBatch", samplesPerBatch);

        // Bind Voronoi cell color/seed/painted SSBOs used by the compute shader.
        // Keep cell count at zero unless all buffers exist for safe access.
        mComputeShader->setUniform("uVoronoiCellCount", static_cast<GLuint>(0));
        if (mVoronoiCellColorSSBO != 0 && mVoronoiCellSeedSSBO != 0 && mVoronoiCellPaintedSSBO != 0) {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, mVoronoiCellColorSSBO);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, mVoronoiCellSeedSSBO);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, mVoronoiCellPaintedSSBO);
            mComputeShader->setUniform("uVoronoiCellCount", static_cast<GLuint>(mVoronoiPlanet.getCellCount()));
        }

        // Set Voronoi planet center and radius (planet under player, radius = 30)
        glm::vec3 planetCenter = mPlayer.getPosition();
        float planetRadius = 30.0f;
        mComputeShader->setUniform("uVoronoiPlanetCenter", planetCenter);
        mComputeShader->setUniform("uVoronoiPlanetRadius", planetRadius);

        // Set sphere count uniform (NEW - tells shader how many spheres to check)
        mComputeShader->setUniform("uSphereCount", static_cast<uint32_t>(totalSphereCount));
        mComputeShader->setUniform("uPrimaryRaySphereCount", static_cast<uint32_t>(primarySphereCount));
        mComputeShader->setUniform("uTriangleCount", kEnableTrianglePathTraceProxy ? static_cast<uint32_t>(traceTriangles.size()) : 0u);

        static constexpr auto NOISE_TEXTURE_UNIT = 2;

        // Set infinite reflective ground plane uniforms
        const Plane &groundPlane = mWorld.getGroundPlane();
        const Material &groundMaterial = groundPlane.getMaterial();
        mComputeShader->setUniform("uGroundPlanePoint", groundPlane.getPoint());
        mComputeShader->setUniform("uGroundPlaneNormal", groundPlane.getNormal());
        mComputeShader->setUniform("uGroundPlaneAlbedo", groundMaterial.getAlbedo());
        mComputeShader->setUniform("uGroundPlaneMaterialType", static_cast<GLuint>(groundMaterial.getType()));
        mComputeShader->setUniform("uGroundPlaneFuzz", groundMaterial.getFuzz());
        mComputeShader->setUniform("uGroundPlaneRefractiveIndex", groundMaterial.getRefractiveIndex());

        mComputeShader->setUniform("uTime", static_cast<GLfloat>(timeSeconds));
        mComputeShader->setUniform("uHistoryBlend", historyBlend);
        mComputeShader->setUniform("uNoiseTex", static_cast<GLint>(NOISE_TEXTURE_UNIT));

        // Shadow casting uniforms
        mComputeShader->setUniform("uPlayerPos", mPlayer.getPosition() + glm::vec3(0.0f, kPlayerShadowCenterYOffset, 0.0f));
        mComputeShader->setUniform("uPlayerRadius", kPlayerProxyRadius);

        mComputeShader->setUniform("uLightDir", computeSunDirection(timeSeconds));

        // Bind both textures as images for compute shader
        glBindImageTexture(0, mAccumTex->get(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
        glBindImageTexture(1, mDisplayTex->get(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);

        // Bind starfield noise texture sampler to texture unit 2
        glActiveTexture(GL_TEXTURE0 + NOISE_TEXTURE_UNIT);
        glBindTexture(GL_TEXTURE_2D, mNoiseTexture ? mNoiseTexture->get() : 0);

        // Dispatch compute shader with work groups (using 20x20 local work group size)
        GLuint groupsX = (mRenderWidth + 19) / 20;
        GLuint groupsY = (mRenderHeight + 19) / 20;
        glDispatchCompute(groupsX, groupsY, 1);

        // Memory barrier to ensure compute shader writes are visible
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        if (mCurrentBatch == 0xFFFFFFFFu)
        {
            mCurrentBatch = 0u;
        }
        else
        {
            mCurrentBatch++;
        }
    }

    // Display the current result
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, mWindowWidth, mWindowHeight);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    mDisplayShader->bind();
    mDisplayShader->setUniform("uTexture2D", 0);

    // DEBUG: show billboard target directly on-screen to verify Voronoi draw
    glActiveTexture(GL_TEXTURE0);
    if (mBillboardColorTex != 0)
    {
        glBindTexture(GL_TEXTURE_2D, mBillboardColorTex);
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, mDisplayTex->get());
    }
    glBindVertexArray(mVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDepthMask(GL_TRUE);
}

void GameState::renderCompositeScene() const noexcept
{
    if (!mCompositeShader || !mDisplayTex || mBillboardColorTex == 0)
    {
        return;
    }
    // Ensure we're rendering to the main framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, mWindowWidth, mWindowHeight);

    mCompositeShader->bind();
    mCompositeShader->setUniform("uSceneTex", 0);
    mCompositeShader->setUniform("uBillboardTex", 1);
    mCompositeShader->setUniform("uShadowTex", 2);
    mCompositeShader->setUniform("uReflectionTex", 3);
    mCompositeShader->setUniform("uInvResolution", glm::vec2(1.0f / static_cast<float>(mWindowWidth), 1.0f / static_cast<float>(mWindowHeight)));
    mCompositeShader->setUniform("uBloomThreshold", 0.65f);
    mCompositeShader->setUniform("uBloomStrength", 0.32f);
    mCompositeShader->setUniform("uSpriteAlpha", 1.0f);
    mCompositeShader->setUniform("uShadowStrength", 0.65f);
    mCompositeShader->setUniform("uEnableShadows", mShadowsInitialized && mShadowTexture != 0);
    mCompositeShader->setUniform("uEnableReflections", mReflectionsInitialized && mReflectionColorTex != 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mDisplayTex->get());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mBillboardColorTex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, mShadowTexture);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, mReflectionColorTex);

    glBindVertexArray(mVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void GameState::resolveOITToBillboardTarget() const noexcept
{
    if (!mOITResolveShader || mBillboardFBO == 0 || mOITAccumTex == 0 || mOITRevealTex == 0)
    {
        return;
    }

    GLboolean depthTestEnabled = GL_FALSE;
    GLboolean blendEnabled = GL_FALSE;
    glGetBooleanv(GL_DEPTH_TEST, &depthTestEnabled);
    glGetBooleanv(GL_BLEND, &blendEnabled);

    GLint prevActiveTexture = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);

    glBindFramebuffer(GL_FRAMEBUFFER, mBillboardFBO);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0, 0, mWindowWidth, mWindowHeight);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    mOITResolveShader->bind();
    mOITResolveShader->setUniform("uOITAccumTex", 0);
    mOITResolveShader->setUniform("uOITRevealTex", 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mOITAccumTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mOITRevealTex);

    glBindVertexArray(mVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glActiveTexture(static_cast<GLenum>(prevActiveTexture));

    if (!blendEnabled)
    {
        glDisable(GL_BLEND);
    }

    if (depthTestEnabled)
    {
        glEnable(GL_DEPTH_TEST);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(GL_TRUE);
}

void GameState::cleanupResources() noexcept
{
    GLSDLHelper::deleteVAO(mVAO);
    GLSDLHelper::deleteBuffer(mShapeSSBO);
    GLSDLHelper::deleteBuffer(mTriangleSSBO);
    mShapeSSBOCapacityBytes = 0;
    mTriangleSSBOCapacityBytes = 0;

    if (mBillboardFBO != 0)
    {
        glDeleteFramebuffers(1, &mBillboardFBO);
        mBillboardFBO = 0;
    }
    if (mBillboardColorTex != 0)
    {
        glDeleteTextures(1, &mBillboardColorTex);
        mBillboardColorTex = 0;
    }
    if (mBillboardDepthRbo != 0)
    {
        glDeleteRenderbuffers(1, &mBillboardDepthRbo);
        mBillboardDepthRbo = 0;
    }

    if (mOITFBO != 0)
    {
        glDeleteFramebuffers(1, &mOITFBO);
        mOITFBO = 0;
    }
    if (mOITAccumTex != 0)
    {
        glDeleteTextures(1, &mOITAccumTex);
        mOITAccumTex = 0;
    }
    if (mOITRevealTex != 0)
    {
        glDeleteTextures(1, &mOITRevealTex);
        mOITRevealTex = 0;
    }
    if (mOITDepthRbo != 0)
    {
        glDeleteRenderbuffers(1, &mOITDepthRbo);
        mOITDepthRbo = 0;
    }
    mOITInitialized = false;

    // Clean up shadow resources
    if (mShadowFBO != 0)
    {
        glDeleteFramebuffers(1, &mShadowFBO);
        mShadowFBO = 0;
    }
    if (mShadowTexture != 0)
    {
        glDeleteTextures(1, &mShadowTexture);
        mShadowTexture = 0;
    }
    GLSDLHelper::deleteVAO(mShadowVAO);
    GLSDLHelper::deleteBuffer(mShadowVBO);
    mShadowsInitialized = false;

    // Clean up reflection resources
    if (mReflectionFBO != 0)
    {
        glDeleteFramebuffers(1, &mReflectionFBO);
        mReflectionFBO = 0;
    }
    if (mReflectionColorTex != 0)
    {
        glDeleteTextures(1, &mReflectionColorTex);
        mReflectionColorTex = 0;
    }
    if (mReflectionDepthRbo != 0)
    {
        glDeleteRenderbuffers(1, &mReflectionDepthRbo);
        mReflectionDepthRbo = 0;
    }
    mReflectionsInitialized = false;

    if (mWalkParticlesVAO != 0)
    {
        glDeleteVertexArrays(1, &mWalkParticlesVAO);
        mWalkParticlesVAO = 0;
    }
    if (mWalkParticlesPosSSBO != 0)
    {
        glDeleteBuffers(1, &mWalkParticlesPosSSBO);
        mWalkParticlesPosSSBO = 0;
    }
    if (mWalkParticlesVelSSBO != 0)
    {
        glDeleteBuffers(1, &mWalkParticlesVelSSBO);
        mWalkParticlesVelSSBO = 0;
    }
    if (mRunnerBreakPlaneVAO != 0)
    {
        glDeleteVertexArrays(1, &mRunnerBreakPlaneVAO);
        mRunnerBreakPlaneVAO = 0;
    }
    if (mRunnerBreakPlanePosSSBO != 0)
    {
        glDeleteBuffers(1, &mRunnerBreakPlanePosSSBO);
        mRunnerBreakPlanePosSSBO = 0;
    }
    if (mRunnerBreakPlaneVelSSBO != 0)
    {
        glDeleteBuffers(1, &mRunnerBreakPlaneVelSSBO);
        mRunnerBreakPlaneVelSSBO = 0;
    }
    if (mRunnerBreakPlaneFBO != 0)
    {
        glDeleteFramebuffers(1, &mRunnerBreakPlaneFBO);
        mRunnerBreakPlaneFBO = 0;
    }
    if (mRunnerBreakPlaneTexture != 0)
    {
        glDeleteTextures(1, &mRunnerBreakPlaneTexture);
        mRunnerBreakPlaneTexture = 0;
    }
    mWalkParticlesInitialized = false;
    mWalkParticlesComputeShader = nullptr;
    mWalkParticlesRenderShader = nullptr;
    mShadowShader = nullptr;

    mAccumTex = nullptr;
    mDisplayTex = nullptr;
    mNoiseTexture = nullptr;

    // Shaders are now managed by ShaderManager - don't delete them here
    mDisplayShader = nullptr;
    mComputeShader = nullptr;
    mCompositeShader = nullptr;
    mOITResolveShader = nullptr;
    mShadowShader = nullptr;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameState: OpenGL resources cleaned up");
}

void GameState::updateSounds() noexcept
{
    // Set listener position based on camera position
    // Convert 3D camera position to 2D for the sound system
    // Using camera X and Z as the 2D position (Y is up in 3D, but we use X/Z plane)
    glm::vec3 camPos = mCamera.getPosition();
    mSoundPlayer->setListenerPosition(sf::Vector2f{camPos.x, camPos.z});

    // Remove mSoundPlayer that have finished playing
    mSoundPlayer->removeStoppedSounds();
}

bool GameState::update(float dt, unsigned int subSteps) noexcept
{
    // If GameState is updating, it is currently active (PauseState would block update propagation).
    // Ensure local paused flag is cleared when returning from pause/menu flows.
    mGameIsPaused = false;

    // Window resize is now handled in handleEvent() with DPI-aware pixel detection

    const glm::vec3 playerPosBeforeUpdate = mPlayer.getPosition();

    if (dt > 0.0f)
    {
        mModelAnimTimeSeconds += dt;
    }

    // Periodic music health check
    static float musicCheckTimer = 0.0f;
    musicCheckTimer += dt;

    if (musicCheckTimer >= 5.0f) // Check every 5 seconds
    {
        musicCheckTimer = 0.0f;

        if (mGameMusic)
        {
            if (!mGameMusic->isPlaying())
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                            "GameState: Music stopped unexpectedly! Attempting restart...");
                mGameMusic->play();
            }
        }
    }

    mWorld.update(dt);

    syncRunnerSettingsFromOptions();

    // Handle camera input through Player (A/D strafing remains useful in runner mode)
    mPlayer.handleRealtimeInput(mCamera, dt);

    updateRunnerGameplay(dt);

    // Keep chunks near player but biased forward for visible into-sun flow
    glm::vec3 chunkAnchor = mPlayer.getPosition();
    chunkAnchor.x += mRunnerPickupSpawnAhead;
    mWorld.updateSphereChunks(chunkAnchor);

    // Paint ground-plane Voronoi cell under player
    if (dt > 0.0f)
    {
        const glm::vec3 p = mPlayer.getPosition();
        mVoronoiPlanet.paintAtPosition(p, glm::vec3(1.0f));
        // Upload Voronoi cell colors, seeds, and painted states to SSBOs for compute shader
        if (mVoronoiCellColorSSBO == 0) {
            glGenBuffers(1, &mVoronoiCellColorSSBO);
        }
        if (mVoronoiCellSeedSSBO == 0) {
            glGenBuffers(1, &mVoronoiCellSeedSSBO);
        }
        if (mVoronoiCellPaintedSSBO == 0) {
            glGenBuffers(1, &mVoronoiCellPaintedSSBO);
        }
        mVoronoiPlanet.uploadCellColorsToSSBO(mVoronoiCellColorSSBO);
        mVoronoiPlanet.uploadCellSeedsToSSBO(mVoronoiCellSeedSSBO);
        mVoronoiPlanet.uploadPaintedStatesToSSBO(mVoronoiCellPaintedSSBO);
    }

    // Update mSoundPlayer: set listener position based on camera and remove stopped mSoundPlayer
    if (!mGameIsPaused)
    {
        updateSounds();
    }

    // Update player animation
    mPlayer.updateAnimation(dt);

    if (dt > 0.0f)
    {
        const glm::vec3 playerPosNow = mPlayer.getPosition();
        const glm::vec2 beforeXZ(playerPosBeforeUpdate.x, playerPosBeforeUpdate.z);
        const glm::vec2 nowXZ(playerPosNow.x, playerPosNow.z);

        if (mHasLastFxPosition)
        {
            const glm::vec2 lastXZ(mLastFxPlayerPosition.x, mLastFxPlayerPosition.z);
            mPlayerPlanarSpeedForFx = glm::distance(lastXZ, nowXZ) / dt;
        }
        else
        {
            mPlayerPlanarSpeedForFx = glm::distance(beforeXZ, nowXZ) / dt;
            mHasLastFxPosition = true;
        }

        mLastFxPlayerPosition = playerPosNow;
    }

    return true;
}

bool GameState::handleEvent(const SDL_Event &event) noexcept
{
    // Handle window resize event - query physical pixels to handle DPI scaling
    if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        // Always query the actual pixel size from SDL instead of trusting event data
        // This ensures we get physical pixels on DPI-scaled displays
        if (auto *window = getContext().getRenderWindow(); window != nullptr)
        {
            if (SDL_Window *sdlWindow = window->getSDLWindow(); sdlWindow != nullptr)
            {
                int newW = 0, newH = 0;
                SDL_GetWindowSizeInPixels(sdlWindow, &newW, &newH);

                if (newW > 0 && newH > 0 && (newW != mWindowWidth || newH != mWindowHeight))
                {
                    mWindowWidth = newW;
                    mWindowHeight = newH;
                    // Update OpenGL viewport to match new window size
                    glViewport(0, 0, mWindowWidth, mWindowHeight);
                    // Update internal rendering resolution for path tracer
                    updateRenderResolution();
                    // Recreate path tracer textures
                    createPathTracerTextures();
                    createCompositeTargets();
                    initializeShadowResources();
                    initializeReflectionResources();
                    // Reset accumulation for new size
                    mCurrentBatch = 0;
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameState: Window resized to physical pixels %dx%d with render resolution %dx%d",
                        mWindowWidth, mWindowHeight, mRenderWidth, mRenderHeight);
                }
            }
        }
    }

    // World still handles mouse panning for the 2D view
    mWorld.handleEvent(event);

    // Handle camera discrete events through Player
    mPlayer.handleEvent(event, mCamera);

    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        if (event.key.scancode == SDL_SCANCODE_RETURN && mRunLost)
        {
            resetRunnerRun();
            return true;
        }

        if (event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            mGameIsPaused = true;
            requestStackPush(States::ID::PAUSE);
        }

        // Jump with SPACE
        if (event.key.scancode == SDL_SCANCODE_SPACE)
        {
            if (mSoundPlayer && !mGameIsPaused)
            {
                mSoundPlayer->play(SoundEffect::ID::SELECT, sf::Vector2f{mPlayer.getPosition().x, mPlayer.getPosition().z});
            }
        }

        // Play sound when camera is reset (R key handled by Player now)
        if (event.key.scancode == SDL_SCANCODE_R)
        {
            glm::vec3 resetPos = glm::vec3(0.0f, 50.0f, 200.0f);
            if (mSoundPlayer && !mGameIsPaused)
            {
                mSoundPlayer->play(SoundEffect::ID::GENERATE, sf::Vector2f{resetPos.x, resetPos.z});
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameState: Camera reset to initial position");
        }
    }

    // Handle mouse motion for camera rotation (right mouse button)
    if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        std::uint32_t mouseState = SDL_GetMouseState(nullptr, nullptr);
        if (mouseState & SDL_BUTTON_RMASK)
        {
            static constexpr float SENSITIVITY = 0.35f;
            if (mArcadeModeEnabled && mCamera.getMode() == CameraMode::THIRD_PERSON)
            {
                mCamera.rotate(0.0f, -event.motion.yrel * SENSITIVITY);
            }
            else
            {
                mCamera.rotate(event.motion.xrel * SENSITIVITY, -event.motion.yrel * SENSITIVITY);
            }
        }
    }

    // Handle mouse wheel for field of view adjustment
    if (event.type == SDL_EVENT_MOUSE_WHEEL)
    {
        mCamera.updateFieldOfView(static_cast<float>(event.wheel.y));
    }

    return true;
}

void GameState::renderPlayerCharacter() const noexcept
{
    // Only render in third-person mode
    if (mCamera.getMode() != CameraMode::THIRD_PERSON)
    {
        return;
    }

    if (mCompositeShader && mBillboardFBO != 0)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, mBillboardFBO);
        glViewport(0, 0, mWindowWidth, mWindowHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
    }

    // The path tracer draws a fullscreen 2D quad which doesn't use depth properly.
    // Clear the depth buffer so our 3D billboard can render.
    glClear(GL_DEPTH_BUFFER_BIT);

    // Reset depth function to default
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    // Also ensure we have proper 3D projection set up
    glEnable(GL_DEPTH_TEST);
    GLSDLHelper::setBillboardOITPass(false);

    // Render Voronoi planet into the billboard target so it gets composited
    // (VoronoiPlanet overlay rendering removed)

    bool renderedModel = false;
    if (mSkinnedModelShader)
    {
        auto *models = getContext().getModelsManager();
        if (models)
        {
            GLboolean cullFaceWasEnabled = GL_FALSE;
            bool cullStateCaptured = false;
            try
            {
                const GLTFModel &characterModel = models->get(Models::ID::STYLIZED_CHARACTER);
                if (characterModel.isLoaded())
                {
                    const int safeHeight = std::max(mWindowHeight, 1);
                    const float aspectRatio = static_cast<float>(mWindowWidth) / static_cast<float>(safeHeight);
                    const float timeSeconds = static_cast<float>(SDL_GetTicks()) / 1000.0f;

                    glGetBooleanv(GL_CULL_FACE, &cullFaceWasEnabled);
                    cullStateCaptured = true;
                    glDisable(GL_CULL_FACE);

                    glm::mat4 modelMatrix(1.0f);
                    modelMatrix = glm::translate(modelMatrix, mPlayer.getPosition() + glm::vec3(0.0f, kCharacterModelYOffset, 0.0f));
                    modelMatrix = glm::rotate(modelMatrix, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                    modelMatrix = glm::scale(modelMatrix, glm::vec3(kCharacterRasterScale));

                    mSkinnedModelShader->bind();
                    mSkinnedModelShader->setUniform("uSunDir", computeSunDirection(timeSeconds));

                    characterModel.render(
                        *mSkinnedModelShader,
                        modelMatrix,
                        mCamera.getLookAt(),
                        mCamera.getPerspective(aspectRatio),
                        mModelAnimTimeSeconds);

                    renderedModel = true;
                }
            }
            catch (const std::exception &e)
            {
                static bool loggedOnce = false;
                if (!loggedOnce)
                {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "GameState: Skinned model render failed; using billboard fallback: %s", e.what());
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameState: Skinned model render failed; using billboard fallback");
                    loggedOnce = true;
                }
            }

            if (cullStateCaptured && cullFaceWasEnabled)
            {
                glEnable(GL_CULL_FACE);
            }
        }
    }

    if (!renderedModel)
    {
        mWorld.renderPlayerCharacter(mPlayer, mCamera);
    }

    renderWalkParticles();

    if (mOITInitialized && mOITFBO != 0 && mOITResolveShader)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, mOITFBO);
        constexpr GLenum oitDrawBuffers[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, oitDrawBuffers);
        glViewport(0, 0, mWindowWidth, mWindowHeight);

        const float zeroColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const float oneReveal[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        glClearBufferfv(GL_COLOR, 0, zeroColor);
        glClearBufferfv(GL_COLOR, 1, oneReveal);
        glClear(GL_DEPTH_BUFFER_BIT);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_FALSE);

        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunci(0, GL_ONE, GL_ONE);
        glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_COLOR);

        GLSDLHelper::setBillboardOITPass(true);
        renderRunnerBreakPlane();
        renderTracksideBillboards();
        drawRunnerScorePopups();
        GLSDLHelper::setBillboardOITPass(false);

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        resolveOITToBillboardTarget();
        glDisable(GL_BLEND);
    }
    else
    {
        GLSDLHelper::setBillboardOITPass(false);
        renderRunnerBreakPlane();
        renderTracksideBillboards();
        drawRunnerScorePopups();
    }

    if (mCompositeShader && mBillboardFBO != 0)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDrawBuffer(GL_BACK); // CRITICAL: Reset to default for main framebuffer
        glReadBuffer(GL_BACK); // CRITICAL: Reset to default for main framebuffer
    }
}

void GameState::renderTracksideBillboards() const noexcept
{
    if (!mArcadeModeEnabled || mCamera.getMode() != CameraMode::THIRD_PERSON)
    {
        return;
    }

    const AnimationRect frame = mPlayer.getCurrentAnimationFrame();
    const glm::vec3 playerPos = mPlayer.getPosition();

    constexpr int kBillboardsAhead = 9;
    constexpr int kBillboardsBehind = 4;
    constexpr float kBillboardSpacing = 28.0f;
    constexpr float kBillboardHeight = 2.9f;
    const float borderZ = mRunnerStrafeLimit + kTracksideBillboardBorderMargin;

    for (int i = -kBillboardsBehind; i <= kBillboardsAhead; ++i)
    {
        const float x = playerPos.x + static_cast<float>(i) * kBillboardSpacing;
        const float stagger = (i & 1) == 0 ? 0.0f : 2.0f;

        mWorld.renderCharacterFromState(
            glm::vec3(x, kBillboardHeight, borderZ + stagger),
            180.0f,
            frame,
            mCamera);

        mWorld.renderCharacterFromState(
            glm::vec3(x, kBillboardHeight, -borderZ - stagger),
            0.0f,
            frame,
            mCamera);
    }
}

void GameState::renderRunnerBreakPlane() const noexcept
{
    if (!mArcadeModeEnabled || mCamera.getMode() != CameraMode::THIRD_PERSON || mRunnerBreakPlaneTexture == 0)
    {
        return;
    }

    Shader *billboardShader = nullptr;
    try
    {
        billboardShader = &getContext().getShaderManager()->get(Shaders::ID::GLSL_BILLBOARD_SPRITE);
    }
    catch (const std::exception &)
    {
        billboardShader = nullptr;
    }

    if (!billboardShader)
    {
        return;
    }

    const int safeHeight = std::max(1, mWindowHeight);
    const float aspectRatio = static_cast<float>(mWindowWidth) / static_cast<float>(safeHeight);
    const glm::mat4 view = mCamera.getLookAt();
    const glm::mat4 projection = mCamera.getPerspective(aspectRatio);
    const glm::vec4 uvRect(0.0f, 0.0f, 1.0f, 1.0f);

    if (mRunnerBreakPlaneActive)
    {
        const float borderZ = mRunnerStrafeLimit + kTracksideBillboardBorderMargin;
        const glm::vec3 planeCenter(mRunnerBreakPlaneX, 4.4f, 0.0f);
        const glm::vec4 tint(0.78f, 0.95f, 1.0f, 0.85f);
        const glm::vec2 halfSizeXY(borderZ, 0.5f * mRunnerBreakPlaneHeight);

        GLSDLHelper::renderBillboardSpriteUV(
            *billboardShader,
            mRunnerBreakPlaneTexture,
            uvRect,
            planeCenter,
            1.0f,
            view,
            projection,
            tint,
            false,
            false,
            false,
            halfSizeXY);
    }

    for (const auto &shard : mRunnerBreakPlaneShards)
    {
        const float t = std::clamp(shard.age / std::max(0.001f, shard.lifetime), 0.0f, 1.0f);
        const float alpha = (1.0f - t) * 0.75f;
        if (alpha <= 0.01f)
        {
            continue;
        }

        GLSDLHelper::renderBillboardSpriteUV(
            *billboardShader,
            mRunnerBreakPlaneTexture,
            uvRect,
            shard.position,
            1.0f,
            view,
            projection,
            glm::vec4(0.76f, 0.95f, 1.0f, alpha),
            false,
            false,
            false,
            shard.halfSize);
    }
}

void GameState::syncRunnerSettingsFromOptions() noexcept
{
    auto *optionsManager = getContext().getOptionsManager();
    if (!optionsManager)
    {
        return;
    }

    try
    {
        const auto &opts = optionsManager->get(GUIOptions::ID::DE_FACTO);
        mArcadeModeEnabled = opts.getArcadeModeEnabled();
        mRunnerSpeed = std::max(5.0f, opts.getRunnerSpeed());
        mRunnerStrafeLimit = std::max(5.0f, opts.getRunnerStrafeLimit());
        mRunnerStartingPoints = std::max(1, opts.getRunnerStartingPoints());
        mRunnerPickupMinValue = std::min(opts.getRunnerPickupMinValue(), opts.getRunnerPickupMaxValue());
        mRunnerPickupMaxValue = std::max(opts.getRunnerPickupMinValue(), opts.getRunnerPickupMaxValue());
        mRunnerPickupSpacing = std::max(4.0f, opts.getRunnerPickupSpacing());
        mRunnerObstaclePenalty = std::max(1, opts.getRunnerObstaclePenalty());
        mRunnerCollisionCooldown = std::max(0.05f, opts.getRunnerCollisionCooldown());

        mMotionBlurBracket1Points = std::max(0, opts.getMotionBlurBracket1Points());
        mMotionBlurBracket2Points = std::max(mMotionBlurBracket1Points, opts.getMotionBlurBracket2Points());
        mMotionBlurBracket3Points = std::max(mMotionBlurBracket2Points, opts.getMotionBlurBracket3Points());
        mMotionBlurBracket4Points = std::max(mMotionBlurBracket3Points, opts.getMotionBlurBracket4Points());

        mMotionBlurBracket1Boost = std::clamp(opts.getMotionBlurBracket1Boost(), 0.0f, 1.0f);
        mMotionBlurBracket2Boost = std::clamp(opts.getMotionBlurBracket2Boost(), mMotionBlurBracket1Boost, 1.0f);
        mMotionBlurBracket3Boost = std::clamp(opts.getMotionBlurBracket3Boost(), mMotionBlurBracket2Boost, 1.0f);
        mMotionBlurBracket4Boost = std::clamp(opts.getMotionBlurBracket4Boost(), mMotionBlurBracket3Boost, 1.0f);

        mWorld.setRunnerTuning(mRunnerStrafeLimit, mRunnerPickupSpawnAhead, mRunnerSpeed + 8.0f);
    }
    catch (const std::exception &)
    {
    }

    mWorld.setRunnerTuning(mRunnerStrafeLimit, mRunnerPickupSpawnAhead, mRunnerSpeed + 8.0f);
}

void GameState::updateRunnerGameplay(float dt) noexcept
{
    if (!mArcadeModeEnabled || dt <= 0.0f)
    {
        return;
    }

    // Keep forward flow continuous; reset is optional when points drop below zero.
    mRunnerDistance += mRunnerSpeed * dt;

    glm::vec3 playerPos = mPlayer.getPosition();
    playerPos.z = std::clamp(playerPos.z, -mRunnerStrafeLimit, mRunnerStrafeLimit);
    playerPos.y = std::max(playerPos.y, 1.0f);
    playerPos.x = mRunnerDistance;

    mPlayer.setPosition(playerPos);
    mWorld.setRunnerPlayerPosition(playerPos);
    mCamera.setYawPitch(kRunnerLockedSunYawDeg, mCamera.getPitch());
    mCamera.setFollowTarget(playerPos);
    mCamera.updateThirdPersonPosition();

    updateRunnerBreakPlane(dt);
    processRunnerCollisions(dt);
    updateRunnerScorePopups(dt);
}

void GameState::updateRunnerBreakPlane(float dt) noexcept
{
    if (!mArcadeModeEnabled)
    {
        mRunnerBreakPlaneShards.clear();
        return;
    }

    const glm::vec3 playerPos = mPlayer.getPosition();

    if (!mRunnerBreakPlaneActive)
    {
        mRunnerBreakPlaneRespawnTimer = std::max(0.0f, mRunnerBreakPlaneRespawnTimer - dt);
        if (mRunnerBreakPlaneRespawnTimer <= 0.0f)
        {
            const float minAhead = playerPos.x + 55.0f;
            const float bySpacing = mRunnerBreakPlaneX + mRunnerBreakPlaneSpacing;
            mRunnerBreakPlaneX = std::max(minAhead, bySpacing);
            mRunnerBreakPlaneActive = true;
        }
    }

    if (mRunnerBreakPlaneActive && mRunnerBreakPlaneLastPlayerX < mRunnerBreakPlaneX && playerPos.x >= mRunnerBreakPlaneX)
    {
        shatterRunnerBreakPlane();
    }

    for (auto &shard : mRunnerBreakPlaneShards)
    {
        shard.age += dt;
        shard.velocity.y -= 17.5f * dt;
        shard.position += shard.velocity * dt;
    }

    std::erase_if(mRunnerBreakPlaneShards, [](const RunnerBreakPlaneShard &shard)
                  { return shard.age >= shard.lifetime; });

    mRunnerBreakPlaneLastPlayerX = playerPos.x;
}

void GameState::shatterRunnerBreakPlane() noexcept
{
    mRunnerBreakPlaneActive = false;
    mRunnerBreakPlaneRespawnTimer = mRunnerBreakPlaneRespawnDelay;
    mPlayerPoints += mRunnerBreakPlanePoints;
    mCurrentBatch = 0;

    if (auto *models = getContext().getModelsManager(); models)
    {
        try
        {
            GLTFModel &characterModel = models->get(Models::ID::STYLIZED_CHARACTER);
            const std::size_t animationCount = characterModel.getAnimationCount();
            if (animationCount >= 2)
            {
                const std::vector<std::string> names = characterModel.getAnimationNames();
                const std::string activeName = characterModel.getActiveAnimationName();

                int currentIndex = 0;
                if (!activeName.empty() && !names.empty())
                {
                    auto found = std::find(names.begin(), names.end(), activeName);
                    if (found != names.end())
                    {
                        currentIndex = static_cast<int>(std::distance(names.begin(), found));
                    }
                }

                const int toggledIndex = (currentIndex == 0) ? 1 : 0;
                if (characterModel.setPreferredAnimationIndex(toggledIndex))
                {
                    mModelAnimTimeSeconds = 0.0f;
                }
            }
        }
        catch (const std::exception &)
        {
        }
    }

    RunnerScorePopup popup;
    popup.worldPosition = glm::vec3(mRunnerBreakPlaneX, 5.4f, 0.0f);
    popup.value = mRunnerBreakPlanePoints;
    mRunnerScorePopups.push_back(popup);

    mPlayer.triggerCollisionAnimation(true);

    if (mSoundPlayer && !mGameIsPaused)
    {
        //const glm::vec3 playerPos = mPlayer.getPosition();
        //mSoundPlayer->play(SoundEffect::ID::GENERATE, sf::Vector2f{playerPos.x, playerPos.z});
    }

    std::uniform_real_distribution<float> randomZ(-mRunnerStrafeLimit * 0.85f, mRunnerStrafeLimit * 0.85f);
    std::uniform_real_distribution<float> randomY(2.6f, 7.4f);
    std::uniform_real_distribution<float> randomVX(-4.0f, 4.0f);
    std::uniform_real_distribution<float> randomVY(3.5f, 12.5f);
    std::uniform_real_distribution<float> randomVZ(-10.0f, 10.0f);
    std::uniform_real_distribution<float> randomSize(0.14f, 0.55f);
    std::uniform_real_distribution<float> randomLifetime(0.35f, 0.75f);

    constexpr int kShardCount = 18;
    mRunnerBreakPlaneShards.reserve(mRunnerBreakPlaneShards.size() + kShardCount);
    for (int i = 0; i < kShardCount; ++i)
    {
        RunnerBreakPlaneShard shard;
        shard.position = glm::vec3(mRunnerBreakPlaneX, randomY(mRunnerRng), randomZ(mRunnerRng));
        shard.velocity = glm::vec3(randomVX(mRunnerRng), randomVY(mRunnerRng), randomVZ(mRunnerRng));
        const float size = randomSize(mRunnerRng);
        shard.halfSize = glm::vec2(size, size);
        shard.lifetime = randomLifetime(mRunnerRng);
        mRunnerBreakPlaneShards.push_back(shard);
    }
}

void GameState::spawnPointEvents() noexcept
{
    // Deprecated in runner-v2: points now come directly from sphere collisions.
}

void GameState::processRunnerCollisions(float dt) noexcept
{
    if (mRunnerCollisionTimer > 0.0f)
    {
        mRunnerCollisionTimer = std::max(0.0f, mRunnerCollisionTimer - dt);
    }

    const glm::vec3 playerPos = mPlayer.getPosition();

    if (mRunnerCollisionTimer <= 0.0f)
    {
        int accumulatedScoreDelta = 0;
        bool hadPenalty = false;
        bool hadBonus = false;

        auto scoreForMaterial = [this](Material::MaterialType materialType) -> int
        {
            switch (materialType)
            {
            case Material::MaterialType::METAL:
                return -mRunnerObstaclePenalty;
            case Material::MaterialType::DIELECTRIC:
                return std::max(10, mRunnerPickupMaxValue);
            case Material::MaterialType::LAMBERTIAN:
            default:
                return std::max(5, mRunnerPickupMaxValue / 2);
            }
        };

        const auto collisionEvents = mWorld.consumeRunnerCollisionEvents();
        for (const auto &event : collisionEvents)
        {
            const int scoreDelta = scoreForMaterial(event.materialType);
            accumulatedScoreDelta += scoreDelta;

            RunnerScorePopup popup;
            popup.worldPosition = event.worldPosition + glm::vec3(0.0f, 1.65f, 0.0f);
            popup.value = scoreDelta;
            mRunnerScorePopups.push_back(popup);

            if (scoreDelta < 0)
            {
                hadPenalty = true;
            }
            else
            {
                hadBonus = true;
            }
        }

        if (accumulatedScoreDelta != 0)
        {
            mPlayerPoints += accumulatedScoreDelta;
            mRunnerCollisionTimer = mRunnerCollisionCooldown;

            if (hadPenalty)
            {
                mPlayer.triggerCollisionAnimation(false);
            }
            else if (hadBonus)
            {
                mPlayer.triggerCollisionAnimation(true);
            }

            if (mSoundPlayer && !mGameIsPaused)
            {
                mSoundPlayer->play(SoundEffect::ID::THROW, sf::Vector2f{playerPos.x, playerPos.z});
            }
        }
    }

    if (mPlayerPoints < 0)
    {
        mRunLost = true;
    }

    if (mPlayerPoints != mLastAnnouncedPoints)
    {
        mLastAnnouncedPoints = mPlayerPoints;
        mCurrentBatch = 0;
    }
}

void GameState::updateRunnerScorePopups(float dt) noexcept
{
    if (mRunnerScorePopups.empty() || dt <= 0.0f)
    {
        return;
    }

    for (auto &popup : mRunnerScorePopups)
    {
        popup.age += dt;
    }

    std::erase_if(mRunnerScorePopups, [](const RunnerScorePopup &popup)
                  { return popup.age >= popup.lifetime; });
}

void GameState::resetRunnerRun() noexcept
{
    mPlayerPoints = mRunnerStartingPoints;
    // Start at positive X for grid shader visibility
    mRunnerDistance = 100.0f;
    mRunnerCollisionTimer = 0.0f;
    mRunnerPointEvents.clear();
    mRunnerScorePopups.clear();
    mRunnerBreakPlaneShards.clear();
    mRunLost = false;

    glm::vec3 current = mPlayer.getPosition();
    current.x = 100.0f;
    current.y = 1.0f;
    current.z = 0.0f;

    mRunnerNextPickupZ = current.z + mRunnerPickupSpacing;
    mPlayer.setPosition(current);
    mWorld.setRunnerPlayerPosition(current);
    mWorld.consumeRunnerCollisionEvents();

    mRunnerBreakPlaneActive = true;
    mRunnerBreakPlaneX = current.x + 80.0f;
    mRunnerBreakPlaneRespawnTimer = 0.0f;
    mRunnerBreakPlaneLastPlayerX = current.x;

    mCamera.setYawPitch(kRunnerLockedSunYawDeg, mCamera.getPitch());
    mCamera.setFollowTarget(current);
    mCamera.updateThirdPersonPosition();

    mLastAnnouncedPoints = mPlayerPoints;
    mCurrentBatch = 0;
}

void GameState::drawRunnerHud() const noexcept
{
    if (!mArcadeModeEnabled)
    {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.42f);

    constexpr ImGuiWindowFlags hudFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav;

    if (ImGui::Begin("Arcade HUD", nullptr, hudFlags))
    {
        const int lambertBonus = std::max(5, mRunnerPickupMaxValue / 2);
        const int dielectricBonus = std::max(10, mRunnerPickupMaxValue);

        ImGui::Text("Points: %d", mPlayerPoints);
        ImGui::Text("Distance: %.0f", mRunnerDistance);
        ImGui::Text("Speed: %.1f", mRunnerSpeed);

        if (mRunLost)
        {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Points below zero!");
            ImGui::Text("Press Enter to reset run");
        }
    }
    ImGui::End();
}

void GameState::drawRunnerScorePopups() const noexcept
{
    if (mRunnerScorePopups.empty() || mCamera.getMode() != CameraMode::THIRD_PERSON)
    {
        return;
    }

    const int safeHeight = std::max(mWindowHeight, 1);
    const float aspectRatio = static_cast<float>(mWindowWidth) / static_cast<float>(safeHeight);
    const glm::mat4 view = mCamera.getLookAt();
    const glm::mat4 projection = mCamera.getPerspective(aspectRatio);

    Shader *billboardShader = nullptr;
    try
    {
        billboardShader = &getContext().getShaderManager()->get(Shaders::ID::GLSL_BILLBOARD_SPRITE);
    }
    catch (const std::exception &)
    {
        billboardShader = nullptr;
    }

    ImFont *font = nullptr;
    try
    {
        font = getContext().getFontManager()->get(Fonts::ID::COUSINE_REGULAR).get();
    }
    catch (const std::exception &)
    {
        font = nullptr;
    }

    GLuint atlasTextureId = 0;
    int atlasWidth = 0;
    int atlasHeight = 0;
    ImFontBaked *fontBaked = nullptr;
    if (font)
    {
        fontBaked = font->GetFontBaked(std::max(1.0f, font->LegacySize));
    }

    if (font && font->OwnerAtlas && font->OwnerAtlas->TexData)
    {
        const ImTextureID texId = font->OwnerAtlas->TexData->TexID;
        if (texId != ImTextureID_Invalid)
        {
            atlasTextureId = static_cast<GLuint>(static_cast<std::uintptr_t>(texId));
        }
        atlasWidth = font->OwnerAtlas->TexData->Width;
        atlasHeight = font->OwnerAtlas->TexData->Height;
    }

    const bool canRenderBillboards = (billboardShader != nullptr && atlasTextureId != 0 && atlasWidth > 0 && atlasHeight > 0 && fontBaked != nullptr);

    ImDrawList *drawList = nullptr;
    if (!canRenderBillboards)
    {
        drawList = ImGui::GetForegroundDrawList();
    }

    const glm::vec3 right = glm::normalize(mCamera.getRight());
    constexpr float kPopupDigitHalfSize = 0.62f;
    constexpr float kDigitSpacing = 0.85f;

    for (const auto &popup : mRunnerScorePopups)
    {
        const float lifeT = std::clamp(popup.age / std::max(0.01f, popup.lifetime), 0.0f, 1.0f);
        const float alpha = 1.0f - lifeT;
        if (alpha <= 0.01f)
        {
            continue;
        }

        const std::string label = (popup.value > 0 ? "+" : "") + std::to_string(popup.value);
        const glm::vec3 centerPos = popup.worldPosition + glm::vec3(0.0f, popup.riseSpeed * popup.age, 0.0f);

        if (canRenderBillboards)
        {
            const float billboardScale = 1.0f + (1.0f - lifeT) * 0.22f;
            const float halfSize = kPopupDigitHalfSize * billboardScale;
            const float totalWidth = static_cast<float>(label.size()) * kDigitSpacing;
            const glm::vec3 leftStart = centerPos - right * (totalWidth * 0.5f);

            for (std::size_t i = 0; i < label.size(); ++i)
            {
                const ImFontGlyph *glyph = fontBaked->FindGlyphNoFallback(static_cast<ImWchar>(label[i]));
                if (!glyph)
                {
                    continue;
                }

                const glm::vec3 charPos = leftStart + right * (static_cast<float>(i) * kDigitSpacing);
                const glm::vec4 uvRect(glyph->U0, glyph->V0, glyph->U1, glyph->V1);
                const glm::vec4 tintColor = (popup.value >= 0)
                                                ? glm::vec4(0.37f, 1.0f, 0.46f, alpha)
                                                : glm::vec4(1.0f, 0.40f, 0.40f, alpha);

                const float glyphWidth = std::max(0.001f, glyph->X1 - glyph->X0);
                const float glyphHeight = std::max(0.001f, glyph->Y1 - glyph->Y0);
                const float glyphAspect = std::clamp(glyphWidth / glyphHeight, 0.30f, 1.80f);
                glm::vec2 charHalfSize(halfSize * glyphAspect, halfSize);

                if (label[i] == '-')
                {
                    charHalfSize.x = halfSize * 0.95f;
                    charHalfSize.y = halfSize * 0.28f;
                }

                GLSDLHelper::renderBillboardSpriteUV(
                    *billboardShader,
                    atlasTextureId,
                    uvRect,
                    charPos,
                    halfSize,
                    view,
                    projection,
                    tintColor,
                    false,
                    false,
                    false,
                    charHalfSize);
            }

            continue;
        }

        if (!drawList)
        {
            continue;
        }

        ImVec2 screenPos{};
        if (!projectWorldToScreen(centerPos, view, projection, mWindowWidth, mWindowHeight, screenPos))
        {
            continue;
        }

        const float fontScale = 1.0f + (1.0f - lifeT) * 0.22f;
        const float fontSize = ImGui::GetFontSize() * fontScale;
        const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
        const ImVec2 textPos(screenPos.x - textSize.x * 0.5f, screenPos.y - textSize.y);

        const ImU32 shadowColor = IM_COL32(0, 0, 0, static_cast<int>(150.0f * alpha));
        const ImU32 textColor = popup.value >= 0
                                    ? IM_COL32(94, 255, 118, static_cast<int>(255.0f * alpha))
                                    : IM_COL32(255, 96, 96, static_cast<int>(255.0f * alpha));

        drawList->AddText(ImGui::GetFont(), fontSize, ImVec2(textPos.x + 1.0f, textPos.y + 1.0f), shadowColor, label.c_str());
        drawList->AddText(ImGui::GetFont(), fontSize, textPos, textColor, label.c_str());
    }
}

float GameState::getRenderScale() const noexcept
{
    return (mWindowWidth > 0 && mWindowHeight > 0)
               ? static_cast<float>(mRenderWidth) / static_cast<float>(mWindowWidth)
               : 1.0f;
}
