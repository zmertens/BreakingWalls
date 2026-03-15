#include "GameState.hpp"

#include <SDL3/SDL.h>

#include <dearimgui/imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>
#include <queue>
#include <random>
#include <ranges>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glad/glad.h>

#include <MazeBuilder/colored_grid.h>
#include <MazeBuilder/cell.h>
#include <MazeBuilder/dfs.h>
#include <MazeBuilder/grid_operations.h>
#include <MazeBuilder/randomizer.h>

#include "Font.hpp"
#include "GLSDLHelper.hpp"
#include "GLTFModel.hpp"
#include "Level.hpp"
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
    constexpr float kRasterBirdsEyeFovDeg = 52.0f;
    constexpr float kRasterZoomMinHeadroom = 1.6f;
    constexpr float kRasterZoomStep = 0.85f;
    constexpr float kRasterWallScreenMarginPx = 20.0f;

    struct RasterVertex
    {
        glm::vec3 position;
        glm::vec3 color;
    };

    // Maze scene constants
    constexpr int kMazeRows = 50;
    constexpr int kMazeCols = 50;
    constexpr float kMazeCellSize = 2.0f;
    constexpr float kMazeWallHeight = 2.5f;
    constexpr float kMazeWallThickness = 0.18f;
    constexpr float kMazeFloorY = 0.0f;

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

    uint32_t computeAdaptiveTriangleShadowBudget(int renderWidth, int renderHeight) noexcept
    {
        const std::size_t renderPixels = static_cast<std::size_t>(std::max(1, renderWidth)) *
                                         static_cast<std::size_t>(std::max(1, renderHeight));

        if (renderPixels > 1800ull * 1000ull)
        {
            return 40u;
        }
        if (renderPixels > 1400ull * 1000ull)
        {
            return 48u;
        }
        if (renderPixels > 1000ull * 1000ull)
        {
            return 56u;
        }
        return 64u;
    }

    glm::vec3 packedRGBToLinear(std::uint32_t packed)
    {
        const float r = static_cast<float>((packed >> 16) & 0xFFu) / 255.0f;
        const float g = static_cast<float>((packed >> 8) & 0xFFu) / 255.0f;
        const float b = static_cast<float>(packed & 0xFFu) / 255.0f;
        const glm::vec3 srgb(r, g, b);
        return glm::pow(srgb, glm::vec3(2.2f));
    }

    uint32_t computeAdaptiveSphereShadowBudget(int renderWidth, int renderHeight) noexcept
    {
        const std::size_t renderPixels = static_cast<std::size_t>(std::max(1, renderWidth)) *
                                         static_cast<std::size_t>(std::max(1, renderHeight));

        if (renderPixels > 1800ull * 1000ull)
        {
            return 28u;
        }
        if (renderPixels > 1400ull * 1000ull)
        {
            return 36u;
        }
        if (renderPixels > 1000ull * 1000ull)
        {
            return 44u;
        }
        return 56u;
    }

    uint32_t computeAdaptivePathTraceTileEdge(int renderWidth, int renderHeight) noexcept
    {
        const std::size_t renderPixels = static_cast<std::size_t>(std::max(1, renderWidth)) *
                                         static_cast<std::size_t>(std::max(1, renderHeight));

        // Keep tile sizes aligned with the 16x16 local workgroup while adapting
        // frame cost for higher resolutions.  BVH-accelerated intersection
        // makes each tile cheaper, so use smaller tiles for smoother coverage.
        if (renderPixels > 1800ull * 1000ull)
        {
            return 160u;
        }
        if (renderPixels > 1400ull * 1000ull)
        {
            return 192u;
        }
        if (renderPixels > 1000ull * 1000ull)
        {
            return 256u;
        }
        return 320u;
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
    : State{stack, context}, mWorld{*context.getRenderWindow(), *context.getFontManager(), *context.getTextureManager(), *context.getShaderManager(), *context.getLevelsManager()}, mPlayer{*context.getPlayer()}, mGameMusic{nullptr}, mDisplayShader{nullptr}, mCompositeShader{nullptr}, mOITResolveShader{nullptr}, mGameIsPaused{false}
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
        mCompositeShader = &shaders.get(Shaders::ID::GLSL_COMPOSITE_SCENE);
        mOITResolveShader = &shaders.get(Shaders::ID::GLSL_OIT_RESOLVE);
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
        mDisplayTex = &textures.get(Textures::ID::RUNNER_BREAK_PLANE);
        mNoiseTexture = &textures.get(Textures::ID::NOISE2D);
        mTestAlbedoTexture = &textures.get(Textures::ID::SDL_LOGO);
        mBillboardColorTex = &textures.get(Textures::ID::BILLBOARD_COLOR);
        mOITAccumTex = &textures.get(Textures::ID::OIT_ACCUM);
        mOITRevealTex = &textures.get(Textures::ID::OIT_REVEAL);
        mShadowTexture = &textures.get(Textures::ID::SHADOW_MAP);
        mReflectionColorTex = &textures.get(Textures::ID::REFLECTION_COLOR);
        if (mTestAlbedoTexture)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "GameState: Test albedo texture (SDL_logo) ready id=%u size=%dx%d",
                mTestAlbedoTexture->get(),
                mTestAlbedoTexture->getWidth(),
                mTestAlbedoTexture->getHeight());
        }
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Failed to get texture resources: %s", e.what());
        mDisplayTex = nullptr;
        mNoiseTexture = nullptr;
        mTestAlbedoTexture = nullptr;
        mBillboardColorTex = nullptr;
        mOITAccumTex = nullptr;
        mOITRevealTex = nullptr;
        mShadowTexture = nullptr;
        mReflectionColorTex = nullptr;
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

    // Create player physics body for Box2D interaction
    mWorld.createPlayerBody(spawnPos);

    // Update camera to an elevated position behind the spawn
    glm::vec3 cameraSpawn = spawnPos + glm::vec3(0.0f, 50.0f, 50.0f);
    mCamera.setPosition(cameraSpawn);

    // Default to third-person for arcade runner readability
    mCamera.setMode(CameraMode::THIRD_PERSON);
    mCamera.setYawPitch(0.0f, mCamera.getPitch());
    mCamera.setFollowTarget(spawnPos);
    mCamera.setThirdPersonDistance(15.0f);
    mCamera.setThirdPersonHeight(8.0f);
    mCamera.updateThirdPersonPosition();
    configureCursorLock(true);
    initializeJoystickAndHaptics();

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameState: Camera spawned at:\t%.2f, %.2f, %.2f",
        cameraSpawn.x, cameraSpawn.y, cameraSpawn.z);

    // Initialize camera tracking
    mLastCameraPosition = mCamera.getPosition();
    mLastCameraYaw = mCamera.getYaw();
    mLastCameraPitch = mCamera.getPitch();

    mWorld.updateSphereChunks(mPlayer.getPosition());

    if (mShadersInitialized)
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
                }
            }
        }

        GLSDLHelper::enableRenderingFeatures();
        mVAO = GLSDLHelper::createAndBindVAO();
        initializeRasterMazeResources();
        buildRasterMazeGeometry();
        initializeWalkParticles();
        initializeMotionBlur();
        GLSDLHelper::initializeBillboardRendering();

        // Respawn the player at the centre of the first open cell of the raster maze.
        // The maze is centred at the origin; cell (0,0) corner is at (-halfW, 0, -halfD).
        {
            const float halfW = 0.5f * static_cast<float>(kSimpleMazeCols) * kSimpleCellSize;
            const float halfD = 0.5f * static_cast<float>(kSimpleMazeRows) * kSimpleCellSize;
            // True cell-centre of cell (0,0): half a cell-width in from each corner.
            const glm::vec3 rasterSpawn(-halfW + kSimpleCellSize * 0.5f, 1.0f, -halfD + kSimpleCellSize * 0.5f);
            mPlayer.setPosition(rasterSpawn);
            mWorld.createPlayerBody(rasterSpawn);   // safe: destroys any prior body first
            mCamera.setFollowTarget(rasterSpawn);
            // Re-apply birds-eye camera NOW so the very first draw() sees the correct view.
            applyRasterBirdsEyeCamera();
        }

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameState: Pure raster maze mode ready");
    }
}

GameState::~GameState()
{
    configureCursorLock(false);
    cleanupJoystickAndHaptics();

    // Stop game music when leaving GameState
    if (mGameMusic)
    {
        mGameMusic->stop();
    }

    cleanupResources();
}

void GameState::draw() const noexcept
{
    renderRasterMaze();
    renderPickupSpheres();
    renderPlayerCharacter();
    renderWalkParticles();
    renderMotionBlur();
    renderScoreBillboards();
}

void GameState::updateRenderResolution() noexcept
{
    const auto [newRenderWidth, newRenderHeight] =
        computeRenderResolution(mWindowWidth, mWindowHeight, kTargetRenderPixels, kMinRenderScale);

    mRenderWidth = newRenderWidth;
    mRenderHeight = newRenderHeight;
}

void GameState::handleWindowResize() noexcept
{
    auto *window = getContext().getRenderWindow();
    if (!window)
    {
        return;
    }

    SDL_Window *sdlWindow = window->getSDLWindow();
    if (!sdlWindow)
    {
        return;
    }

    int newW = 0;
    int newH = 0;
    SDL_GetWindowSizeInPixels(sdlWindow, &newW, &newH);

    if (newW <= 0 || newH <= 0 || (newW == mWindowWidth && newH == mWindowHeight))
    {
        return;
    }

    mWindowWidth = newW;
    mWindowHeight = newH;
    glViewport(0, 0, mWindowWidth, mWindowHeight);
    updateRenderResolution();
    updateRasterBirdsEyeZoomLimits();
    applyRasterBirdsEyeCamera();
}

void GameState::createCompositeTargets() noexcept
{
    if (mWindowWidth <= 0 || mWindowHeight <= 0)
    {
        return;
    }

    if (!mBillboardColorTex || !mOITAccumTex || !mOITRevealTex)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Composite textures not initialized in manager");
        return;
    }

    if (!mBillboardColorTex->loadRenderTarget(mWindowWidth, mWindowHeight, Texture::RenderTargetFormat::RGBA16F, 0))
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Failed to allocate billboard texture");
        return;
    }

    if (mBillboardDepthRbo == 0)
    {
        glGenRenderbuffers(1, &mBillboardDepthRbo);
    }
    glBindRenderbuffer(GL_RENDERBUFFER, mBillboardDepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, mWindowWidth, mWindowHeight);

    if (mBillboardFBO == 0)
    {
        glGenFramebuffers(1, &mBillboardFBO);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, mBillboardFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mBillboardColorTex->get(), 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, mBillboardDepthRbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Billboard framebuffer incomplete");
    }
    else
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameState: Billboard FBO=%u, ColorTex=%u", mBillboardFBO, mBillboardColorTex->get());
    }

    if (!mOITAccumTex->loadRenderTarget(mWindowWidth, mWindowHeight, Texture::RenderTargetFormat::RGBA16F, 0))
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Failed to allocate OIT accum texture");
        return;
    }

    if (!mOITRevealTex->loadRenderTarget(mWindowWidth, mWindowHeight, Texture::RenderTargetFormat::R16F, 0))
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Failed to allocate OIT reveal texture");
        return;
    }

    if (mOITDepthRbo == 0)
    {
        glGenRenderbuffers(1, &mOITDepthRbo);
    }
    glBindRenderbuffer(GL_RENDERBUFFER, mOITDepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, mWindowWidth, mWindowHeight);

    if (mOITFBO == 0)
    {
        glGenFramebuffers(1, &mOITFBO);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, mOITFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mOITAccumTex->get(), 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, mOITRevealTex->get(), 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, mOITDepthRbo);
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

    if (!mShadowTexture)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "GameState: Shadow texture not initialized in manager");
        return;
    }

    // Delete existing non-texture resources if resizing
    if (mShadowFBO != 0)
    {
        glDeleteFramebuffers(1, &mShadowFBO);
        mShadowFBO = 0;
    }

    if (!mShadowTexture->loadRenderTarget(mWindowWidth, mWindowHeight, Texture::RenderTargetFormat::RGBA16F, 0))
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Failed to allocate shadow texture");
        return;
    }

    // Create shadow FBO
    glGenFramebuffers(1, &mShadowFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, mShadowFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mShadowTexture->get(), 0);
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

    if (!mReflectionColorTex)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "GameState: Reflection texture not initialized in manager");
        return;
    }

    // Delete existing non-texture resources if resizing
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

    if (!mReflectionColorTex->loadRenderTarget(mWindowWidth, mWindowHeight, Texture::RenderTargetFormat::RGBA16F, 0))
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Failed to allocate reflection texture");
        return;
    }

    // Verify texture size was created correctly
    GLint texWidth = 0, texHeight = 0;
    glBindTexture(GL_TEXTURE_2D, mReflectionColorTex->get());
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &texWidth);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &texHeight);

    // Create reflection depth buffer
    glGenRenderbuffers(1, &mReflectionDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, mReflectionDepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mWindowWidth, mWindowHeight);

    // Create reflection FBO
    glGenFramebuffers(1, &mReflectionFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, mReflectionFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mReflectionColorTex->get(), 0);
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

    if (!mWalkParticlesComputeShader->isLinked())
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

    const float particleMass = 0.35f;
    const float gravityScale = 1.0f;
    const float pointSize = 2.6f;
    const float particleAlpha = 0.22f;

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
        // Camera moved - update tracking.
        mLastCameraPosition = currentPos;
        mLastCameraYaw = currentYaw;
        mLastCameraPitch = currentPitch;
        return true;
    }
    return false;
}

void GameState::renderCharacterShadow() const noexcept
{
    if (!mShadowsInitialized || !mShadowShader || mShadowFBO == 0 || !mShadowTexture || mShadowTexture->get() == 0)
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

    // Render billboard shadows by projecting sprite footprint along light direction onto the spherical terrain
    mShadowShader->bind();
    mShadowShader->setUniform("uViewMatrix", view);
    mShadowShader->setUniform("uProjectionMatrix", projection);
    mShadowShader->setUniform("uLightDir", lightDir);
    mShadowShader->setUniform("uGroundY", groundY);
    // Set sphere parameters for shadow projection onto spherical terrain
    mShadowShader->setUniform("uSphereCenter", glm::vec3(0.0f, 0.0f, 0.0f));
    mShadowShader->setUniform("uSphereRadius", 50.0f);

    glBindVertexArray(mShadowVAO);

    auto drawBillboardShadow = [this](const glm::vec3 &spritePos, float billboardHalfSize)
    {
        mShadowShader->setUniform("uSpritePos", spritePos);
        mShadowShader->setUniform("uSpriteHalfSize", billboardHalfSize);
        glDrawArrays(GL_POINTS, 0, 1);
    };

    // Local character billboard shadow
    drawBillboardShadow(mPlayer.getPosition() + glm::vec3(0.0f, kPlayerShadowCenterYOffset, 0.0f), 3.0f);

    glBindVertexArray(0);

    // Disable blending
    glDisable(GL_BLEND);

    // Reset framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GameState::renderPlayerReflection() const noexcept
{
    if (!mReflectionsInitialized || mReflectionFBO == 0 || !mReflectionColorTex || mReflectionColorTex->get() == 0)
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
    glBindTexture(GL_TEXTURE_2D, mReflectionColorTex->get());
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
    (void)reflectedModelPos;
    (void)aspectRatio;

    // Ensure reflection rendering completes before unbinding
    glFlush();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GameState::initializeRasterMazeResources() noexcept
{
    static const char *kMazeVS = R"(#version 430 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aColor;
uniform mat4 uMVP;
out vec3 vColor;
out vec3 vWorldPos;
out vec2 vTexCoord;
void main()
{
    vColor = aColor;
    vWorldPos = aPosition;
    // Generate texture coordinates from world position for sprite sheet mapping
    vTexCoord = aPosition.xz * 0.15;
    gl_Position = uMVP * vec4(aPosition, 1.0);
}
)";

    static const char *kMazeFS = R"(#version 430 core
in vec3 vColor;
in vec3 vWorldPos;
in vec2 vTexCoord;
uniform sampler2D uSpriteSheet;
uniform int uHasTexture;
uniform float uTime;
layout (location = 0) out vec4 FragColor;
void main()
{
    vec3 color = vColor;

    // Apply sprite sheet texture to walls (vertical surfaces have y > floor level)
    if (uHasTexture != 0 && vWorldPos.y > 0.1)
    {
        // Tile the sprite sheet across walls
        vec2 uv = fract(vTexCoord * 2.0);
        // Select a region of the sprite sheet (first character frame area)
        uv = uv * 0.125; // Use 1/8 of the sheet
        vec4 texColor = texture(uSpriteSheet, uv);
        color = mix(color, texColor.rgb, 0.35);
    }

    // Add reflection effect - simulate environment reflection on walls
    float reflectiveness = 0.0;
    if (vWorldPos.y > 0.1)
    {
        // Fresnel-like reflection based on view angle approximation
        float wallFacing = abs(sin(vWorldPos.x * 0.5) * cos(vWorldPos.z * 0.5));
        reflectiveness = wallFacing * 0.25;
        
        // Animated reflection shimmer (refractive caustics)
        float caustic = sin(vWorldPos.x * 3.0 + uTime * 1.5) *
                        cos(vWorldPos.z * 2.7 + uTime * 1.2) * 0.5 + 0.5;
        vec3 reflectionColor = mix(vec3(0.3, 0.4, 0.8), vec3(0.8, 0.6, 1.0), caustic);
        color = mix(color, reflectionColor, reflectiveness);
    }

    // Add refraction-like distortion on floor
    if (vWorldPos.y <= 0.1)
    {
        float refract = sin(vWorldPos.x * 5.0 + uTime * 0.8) *
                        cos(vWorldPos.z * 4.5 + uTime * 0.6) * 0.08;
        color += vec3(refract * 0.3, refract * 0.2, refract * 0.5);
    }

    FragColor = vec4(color, 1.0);
}
)";

    static const char *kSkyVS = R"(#version 430 core
out vec2 vNDC;
void main()
{
    const vec2 p[4] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
    vNDC = p[gl_VertexID];
    gl_Position = vec4(p[gl_VertexID], 0.0, 1.0);
}
)";

    static const char *kSkyFS = R"(#version 430 core
in vec2 vNDC;
layout (location = 0) out vec4 FragColor;
void main()
{
    float t = clamp(vNDC.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 zenith = vec3(0.06, 0.04, 0.18);
    vec3 horizon = vec3(0.92, 0.24, 0.55);
    vec3 nadir = vec3(0.03, 0.02, 0.08);
    vec3 sky = mix(nadir, mix(horizon, zenith, t), smoothstep(0.0, 0.35, t));

    vec2 sunPos = vec2(0.35, 0.28);
    float d = length(vNDC - sunPos);
    float sun = exp(-d * 12.0);
    sky += vec3(1.0, 0.55, 0.16) * sun * 0.6;

    FragColor = vec4(sky, 1.0);
}
)";

    static const char *kSkinnedCharVS = R"(
#version 430 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in ivec4 aBoneIds;
layout(location = 4) in vec4 aBoneWeights;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uBones[200];
uniform uint uBoneCount;
uniform int  uHasTexCoord;
out vec3 vWorldNormal;
out vec3 vWorldPos;
void main()
{
    float wt = aBoneWeights.x + aBoneWeights.y + aBoneWeights.z + aBoneWeights.w;
    vec4 skinnedPos;
    vec3 skinnedNorm;
    if (wt > 0.001 && uBoneCount > 1u)
    {
        skinnedPos  = vec4(0.0);
        skinnedNorm = vec3(0.0);
        for (int i = 0; i < 4; ++i)
        {
            int   id = aBoneIds[i];
            float w  = aBoneWeights[i];
            if (w <= 0.0 || id < 0 || uint(id) >= uBoneCount) continue;
            skinnedPos  += w * (uBones[id] * vec4(aPosition, 1.0));
            skinnedNorm += w * (mat3(uBones[id]) * aNormal);
        }
    }
    else
    {
        skinnedPos  = vec4(aPosition, 1.0);
        skinnedNorm = aNormal;
    }
    vec4 worldPos  = uModel * skinnedPos;
    vWorldNormal = normalize(transpose(inverse(mat3(uModel))) * normalize(skinnedNorm));
    vWorldPos    = worldPos.xyz;
    gl_Position  = uProjection * uView * worldPos;
}
)";

    static const char *kSkinnedCharFS = R"(
#version 430 core
in vec3 vWorldNormal;
in vec3 vWorldPos;
layout(location = 0) out vec4 FragColor;
void main()
{
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 N = normalize(vWorldNormal);
    float diff = max(dot(N, lightDir), 0.0);
    float amb  = 0.40;
    vec3 baseColor = vec3(0.80, 0.60, 0.45);
    vec3 color = baseColor * (amb + diff * 0.60);
    FragColor = vec4(color, 1.0);
}
)";

    mRasterMazeShader = std::make_unique<Shader>();
    mRasterMazeShader->compileAndAttachShader(Shader::ShaderType::VERTEX, "raster_maze_vs", kMazeVS);
    mRasterMazeShader->compileAndAttachShader(Shader::ShaderType::FRAGMENT, "raster_maze_fs", kMazeFS);
    mRasterMazeShader->linkProgram();

    mRasterSkyShader = std::make_unique<Shader>();
    mRasterSkyShader->compileAndAttachShader(Shader::ShaderType::VERTEX, "raster_sky_vs", kSkyVS);
    mRasterSkyShader->compileAndAttachShader(Shader::ShaderType::FRAGMENT, "raster_sky_fs", kSkyFS);
    mRasterSkyShader->linkProgram();

    mSkinnedCharacterShader = std::make_unique<Shader>();
    mSkinnedCharacterShader->compileAndAttachShader(Shader::ShaderType::VERTEX, "skinned_char_vs", kSkinnedCharVS);
    mSkinnedCharacterShader->compileAndAttachShader(Shader::ShaderType::FRAGMENT, "skinned_char_fs", kSkinnedCharFS);
    mSkinnedCharacterShader->linkProgram();

    if (mRasterMazeVAO == 0)
        glGenVertexArrays(1, &mRasterMazeVAO);
    if (mRasterMazeVBO == 0)
        glGenBuffers(1, &mRasterMazeVBO);
}

void GameState::buildRasterMazeGeometry() noexcept
{
    std::vector<RasterVertex> vertices;
    vertices.reserve(50000);
    mMazeWallAABBs.clear();

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
        const glm::vec3 p001 = center + glm::vec3(-h.x, -h.y,  h.z);
        const glm::vec3 p010 = center + glm::vec3(-h.x,  h.y, -h.z);
        const glm::vec3 p011 = center + glm::vec3(-h.x,  h.y,  h.z);
        const glm::vec3 p100 = center + glm::vec3( h.x, -h.y, -h.z);
        const glm::vec3 p101 = center + glm::vec3( h.x, -h.y,  h.z);
        const glm::vec3 p110 = center + glm::vec3( h.x,  h.y, -h.z);
        const glm::vec3 p111 = center + glm::vec3( h.x,  h.y,  h.z);

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
                const float levelTint = 0.88f + 0.12f *
                    (kSimpleMazeLevels > 1u ? static_cast<float>(level) / static_cast<float>(kSimpleMazeLevels - 1u) : 0.0f);
                tileColor *= levelTint;

                const float y = levelBaseY + 0.01f;
                const glm::vec3 a(cx - 0.5f * kSimpleCellSize, y, cz - 0.5f * kSimpleCellSize);
                const glm::vec3 b(cx + 0.5f * kSimpleCellSize, y, cz - 0.5f * kSimpleCellSize);
                const glm::vec3 c(cx + 0.5f * kSimpleCellSize, y, cz + 0.5f * kSimpleCellSize);
                const glm::vec3 d(cx - 0.5f * kSimpleCellSize, y, cz + 0.5f * kSimpleCellSize);
                pushQuad(a, b, c, d, tileColor);

                const glm::vec3 wallColor(0.22f, 0.07f, 0.34f);

                // Distance-based material: reflective (metal) walls are brighter,
                // refractive (glass) walls get a bluish tint
                int dist = static_cast<int>(std::abs(static_cast<int>(row) - static_cast<int>(kSimpleMazeRows / 2u))) +
                           static_cast<int>(std::abs(static_cast<int>(col) - static_cast<int>(kSimpleMazeCols / 2u)));

                glm::vec3 actualWallColor = wallColor;
                if (dist > 0 && dist % 3 == 0)
                {
                    // Metal/reflective walls - brighter, slight chrome tint
                    actualWallColor = glm::vec3(0.45f, 0.40f, 0.55f);
                }
                else if (dist > 0 && dist % 5 == 0)
                {
                    // Glass/refractive walls - bluish transparent look
                    actualWallColor = glm::vec3(0.18f, 0.30f, 0.55f);
                }

                const auto east = gridOps.get_east(cellPtr);
                if (!(east && cellPtr->is_linked(east)))
                {
                    pushBox(glm::vec3(cx + 0.5f * kSimpleCellSize, levelBaseY + 0.5f * kSimpleWallHeight, cz),
                            glm::vec3(kSimpleWallThickness, kSimpleWallHeight, kSimpleCellSize + kSimpleWallThickness),
                            actualWallColor);
                    if (level == 0u)  // Collect XZ AABB once (same footprint across all levels)
                    {
                        const float hw = kSimpleWallThickness * 0.5f;
                        const float hz = (kSimpleCellSize + kSimpleWallThickness) * 0.5f;
                        mMazeWallAABBs.emplace_back(cx + 0.5f*kSimpleCellSize - hw, cz - hz,
                                                    cx + 0.5f*kSimpleCellSize + hw, cz + hz);
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
                        mMazeWallAABBs.emplace_back(cx - hx, cz + 0.5f*kSimpleCellSize - hw,
                                                    cx + hx, cz + 0.5f*kSimpleCellSize + hw);
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
                        mMazeWallAABBs.emplace_back(cx - 0.5f*kSimpleCellSize - hw, cz - hz,
                                                    cx - 0.5f*kSimpleCellSize + hw, cz + hz);
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
                        mMazeWallAABBs.emplace_back(cx - hx, cz - 0.5f*kSimpleCellSize - hw,
                                                    cx + hx, cz - 0.5f*kSimpleCellSize + hw);
                    }
                }
            }
        }
    }

    // Boundary billboard pillars: tall bright-orange posts just outside the outer
    // perimeter wall, one per cell interval.  Visible from the bird's-eye camera
    // as an orange ring delimiting the playable area.
    {
        const glm::vec3 pillarColor(0.95f, 0.55f, 0.05f);
        constexpr float kPillarH = 3.5f;
        constexpr float kPillarW = 0.30f;
        const float pillarY  = kSimpleFloorY + kPillarH * 0.5f;
        const float margin   = kSimpleCellSize * 0.5f + kSimpleWallThickness + 0.25f;

        // North / South rows (full width including corners)
        for (unsigned int c = 0u; c <= kSimpleMazeCols; ++c)
        {
            const float px = mazeOrigin.x + static_cast<float>(c) * kSimpleCellSize;
            pushBox(glm::vec3(px, pillarY, mazeOrigin.z - margin),
                    glm::vec3(kPillarW, kPillarH, kPillarW), pillarColor);
            pushBox(glm::vec3(px, pillarY, mazeOrigin.z + mazeDepth + margin),
                    glm::vec3(kPillarW, kPillarH, kPillarW), pillarColor);
        }
        // West / East columns (skip corners already placed)
        for (unsigned int r = 1u; r < kSimpleMazeRows; ++r)
        {
            const float pz = mazeOrigin.z + static_cast<float>(r) * kSimpleCellSize;
            pushBox(glm::vec3(mazeOrigin.x - margin, pillarY, pz),
                    glm::vec3(kPillarW, kPillarH, kPillarW), pillarColor);
            pushBox(glm::vec3(mazeOrigin.x + mazeWidth + margin, pillarY, pz),
                    glm::vec3(kPillarW, kPillarH, kPillarW), pillarColor);
        }
    }

    const glm::vec3 floorCol(0.18f, 0.13f, 0.28f);
    const float floorY = kSimpleFloorY;
    pushQuad(glm::vec3(-2400.0f, floorY, -2400.0f),
             glm::vec3(2400.0f, floorY, -2400.0f),
             glm::vec3(2400.0f, floorY, 2400.0f),
             glm::vec3(-2400.0f, floorY, 2400.0f),
             floorCol);

    glBindVertexArray(mRasterMazeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, mRasterMazeVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(RasterVertex)),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RasterVertex), reinterpret_cast<void *>(offsetof(RasterVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RasterVertex), reinterpret_cast<void *>(offsetof(RasterVertex, color)));
    glBindVertexArray(0);

    mRasterMazeVertexCount = static_cast<GLsizei>(vertices.size());

    mRasterMazeCenter = mazeCenter;
    mRasterMazeWidth = mazeWidth;
    mRasterMazeDepth = mazeDepth;
    mRasterMazeTopY = kSimpleFloorY + static_cast<float>(kSimpleMazeLevels - 1u) * kSimpleLevelSpacing + kSimpleWallHeight;
    mCamera.setFieldOfView(kRasterBirdsEyeFovDeg);
    updateRasterBirdsEyeZoomLimits();
    mRasterBirdsEyeDistance = mRasterBirdsEyeMaxDistance;
    applyRasterBirdsEyeCamera();
    // NOTE: cursor lock intentionally NOT disabled here; GameState keeps it
    // locked for relative-mouse top-down movement during gameplay.

    mLastCameraPosition = mCamera.getActualPosition();
    mLastCameraYaw = mCamera.getYaw();
    mLastCameraPitch = mCamera.getPitch();

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "GameState: Raster maze built ? rows=%u cols=%u levels=%u vertices=%d zoom=[%.2f, %.2f]",
                kSimpleMazeRows,
                kSimpleMazeCols,
                kSimpleMazeLevels,
                mRasterMazeVertexCount,
                mRasterBirdsEyeMinDistance,
                mRasterBirdsEyeMaxDistance);
}

void GameState::renderPlayerCharacter() const noexcept
{
    if (!mSkinnedCharacterShader || !mSkinnedCharacterShader->isLinked())
        return;

    auto *modelsManager = getContext().getModelsManager();
    if (!modelsManager)
        return;

    GLTFModel *model = nullptr;
    try
    {
        model = &modelsManager->get(Models::ID::STYLIZED_CHARACTER);
    }
    catch (const std::exception &)
    {
        return;
    }

    if (!model || !model->isLoaded())
        return;

    const float aspectRatio = static_cast<float>(std::max(1, mWindowWidth)) /
                              static_cast<float>(std::max(1, mWindowHeight));
    const glm::mat4 view = mCamera.getLookAt();
    const glm::mat4 proj = mCamera.getPerspective(aspectRatio);

    const glm::vec3 playerPos = mPlayer.getPosition() + glm::vec3(0.0f, kCharacterModelYOffset, 0.0f);
    const float facingDeg = mPlayer.getFacingDirection();

    glm::mat4 modelMat = glm::translate(glm::mat4(1.0f), playerPos);
    modelMat = glm::rotate(modelMat, glm::radians(facingDeg), glm::vec3(0.0f, 1.0f, 0.0f));
    // Scale can be adjusted if the exported model units differ from world units
    modelMat = glm::scale(modelMat, glm::vec3(1.0f));

    GLboolean prevDepthTest = GL_FALSE;
    GLboolean prevCullFace  = GL_FALSE;
    GLint prevDepthFunc = GL_LESS;
    glGetBooleanv(GL_DEPTH_TEST, &prevDepthTest);
    glGetBooleanv(GL_CULL_FACE,  &prevCullFace);
    glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    model->render(*mSkinnedCharacterShader, modelMat, view, proj, mModelAnimTimeSeconds);

    if (!prevDepthTest) glDisable(GL_DEPTH_TEST);
    else glDepthFunc(static_cast<GLenum>(prevDepthFunc));
    if (!prevCullFace)  glDisable(GL_CULL_FACE);
}

void GameState::renderScoreBillboards() const noexcept
{
    // Render score HUD using ImGui
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("##ScoreHUD", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav);

    // Get font from FontManager for score display
    Font *scoreFont = nullptr;
    try
    {
        scoreFont = &const_cast<FontManager &>(*getContext().getFontManager()).get(Fonts::ID::LIMELIGHT);
    }
    catch (const std::exception &)
    {
    }

    if (scoreFont && scoreFont->get())
    {
        ImGui::PushFont(scoreFont->get());
    }

    const int score = mWorld.getScore();
    const ImVec4 scoreColor = (score >= 0)
        ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f)   // Green for positive
        : ImVec4(1.0f, 0.2f, 0.2f, 1.0f);   // Red for negative

    ImGui::TextColored(scoreColor, "SCORE: %d", score);

    if (scoreFont && scoreFont->get())
    {
        ImGui::PopFont();
    }

    ImGui::End();

    // Render floating score popups in world space using ImGui overlays
    const float aspectRatio = static_cast<float>(std::max(1, mWindowWidth)) /
                              static_cast<float>(std::max(1, mWindowHeight));
    const glm::mat4 view = mCamera.getLookAt();
    const glm::mat4 proj = mCamera.getPerspective(aspectRatio);

    for (size_t i = 0; i < mActiveScorePopups.size(); ++i)
    {
        const auto &[worldPos, value] = mActiveScorePopups[i];
        const float timer = mScorePopupTimers[i];

        // Animate popup floating upward
        glm::vec3 animPos = worldPos + glm::vec3(0.0f, (2.0f - timer) * 2.0f, 0.0f);

        ImVec2 screenPos;
        if (projectWorldToScreen(animPos, view, proj, mWindowWidth, mWindowHeight, screenPos))
        {
            const float alpha = std::min(1.0f, timer);
            const ImVec4 popupColor = (value >= 0)
                ? ImVec4(0.1f, 1.0f, 0.2f, alpha)
                : ImVec4(1.0f, 0.15f, 0.15f, alpha);

            char label[64];
            snprintf(label, sizeof(label), "##popup%zu", i);
            ImGui::SetNextWindowPos(screenPos, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowBgAlpha(0.0f);
            ImGui::Begin(label, nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav);

            if (scoreFont && scoreFont->get())
                ImGui::PushFont(scoreFont->get());

            ImGui::TextColored(popupColor, "%s%d", (value >= 0) ? "+" : "", value);

            if (scoreFont && scoreFont->get())
                ImGui::PopFont();

            ImGui::End();
        }
    }
}

void GameState::renderPickupSpheres() const noexcept
{
    const auto &pickups = mWorld.getPickupSpheres();
    if (pickups.empty())
        return;

    if (!mRasterMazeShader)
        return;

    // Lazily create cached VAO/VBO
    if (mPickupVAO == 0)
    {
        glGenVertexArrays(1, &const_cast<GameState *>(this)->mPickupVAO);
        glGenBuffers(1, &const_cast<GameState *>(this)->mPickupVBO);
        glBindVertexArray(mPickupVAO);
        glBindBuffer(GL_ARRAY_BUFFER, mPickupVBO);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RasterVertex),
                              reinterpret_cast<void *>(offsetof(RasterVertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RasterVertex),
                              reinterpret_cast<void *>(offsetof(RasterVertex, color)));
        glBindVertexArray(0);
        mPickupsDirty = true;
    }

    // Rebuild geometry only when pickups change (collected) or first time
    if (mPickupsDirty)
    {
        std::vector<RasterVertex> pickupVerts;
        pickupVerts.reserve(pickups.size() * 36);

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

        for (const auto &pickup : pickups)
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
            const glm::vec3 p001 = pos + glm::vec3(-h, -h,  h);
            const glm::vec3 p010 = pos + glm::vec3(-h,  h, -h);
            const glm::vec3 p011 = pos + glm::vec3(-h,  h,  h);
            const glm::vec3 p100 = pos + glm::vec3( h, -h, -h);
            const glm::vec3 p101 = pos + glm::vec3( h, -h,  h);
            const glm::vec3 p110 = pos + glm::vec3( h,  h, -h);
            const glm::vec3 p111 = pos + glm::vec3( h,  h,  h);

            pushQuad(p001, p101, p111, p011, color);
            pushQuad(p100, p000, p010, p110, color * 0.8f);
            pushQuad(p000, p001, p011, p010, color * 0.9f);
            pushQuad(p101, p100, p110, p111, color * 0.85f);
            pushQuad(p010, p011, p111, p110, color * 1.1f);
            pushQuad(p000, p100, p101, p001, color * 0.7f);
        }

        glBindBuffer(GL_ARRAY_BUFFER, mPickupVBO);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(pickupVerts.size() * sizeof(RasterVertex)),
                     pickupVerts.data(), GL_DYNAMIC_DRAW);
        const_cast<GameState *>(this)->mPickupVertexCount = static_cast<GLsizei>(pickupVerts.size());
        mPickupsDirty = false;
    }

    if (mPickupVertexCount == 0)
        return;

    const float aspectRatio = static_cast<float>(std::max(1, mWindowWidth)) /
                              static_cast<float>(std::max(1, mWindowHeight));
    const glm::mat4 mvp = mCamera.getPerspective(aspectRatio) * mCamera.getLookAt();

    glEnable(GL_DEPTH_TEST);
    mRasterMazeShader->bind();
    mRasterMazeShader->setUniform("uMVP", mvp);
    mRasterMazeShader->setUniform("uHasTexture", 0);
    glBindVertexArray(mPickupVAO);
    glDrawArrays(GL_TRIANGLES, 0, mPickupVertexCount);
}

void GameState::initializeMotionBlur() noexcept
{
    if (mMotionBlurInitialized)
        return;

    // Create motion blur compositing shader
    static const char *kMotionBlurVS = R"(#version 430 core
out vec2 vUV;
void main()
{
    const vec2 p[4] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
    vUV = p[gl_VertexID] * 0.5 + 0.5;
    gl_Position = vec4(p[gl_VertexID], 0.0, 1.0);
}
)";

    static const char *kMotionBlurFS = R"(#version 430 core
in vec2 vUV;
uniform sampler2D uCurrentFrame;
uniform sampler2D uPrevFrame;
uniform vec2 uVelocity;
uniform float uBlurStrength;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec4 current = texture(uCurrentFrame, vUV);
    
    // Directional motion blur based on player velocity
    vec2 blurDir = uVelocity * uBlurStrength * 0.01;
    vec4 blurred = vec4(0.0);
    const int samples = 4;
    for (int i = 0; i < samples; ++i)
    {
        float t = float(i) / float(samples - 1) - 0.5;
        blurred += texture(uCurrentFrame, vUV + blurDir * t);
    }
    blurred /= float(samples);

    // Blend between sharp and blurred based on velocity magnitude
    float speed = length(uVelocity);
    float blendFactor = clamp(speed * 0.02, 0.0, 0.6);
    
    // Also blend with previous frame for temporal smoothing
    vec4 prev = texture(uPrevFrame, vUV);
    vec4 result = mix(current, blurred, blendFactor);
    result = mix(result, prev, 0.1 * blendFactor);
    
    FragColor = result;
}
)";

    mMotionBlurShader = std::make_unique<Shader>();
    mMotionBlurShader->compileAndAttachShader(Shader::ShaderType::VERTEX, "motion_blur_vs", kMotionBlurVS);
    mMotionBlurShader->compileAndAttachShader(Shader::ShaderType::FRAGMENT, "motion_blur_fs", kMotionBlurFS);
    mMotionBlurShader->linkProgram();

    // Create render textures for motion blur ping-pong
    glGenTextures(1, &mMotionBlurTex);
    glBindTexture(GL_TEXTURE_2D, mMotionBlurTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
                 std::max(1, mWindowWidth), std::max(1, mWindowHeight),
                 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &mPrevFrameTex);
    glBindTexture(GL_TEXTURE_2D, mPrevFrameTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
                 std::max(1, mWindowWidth), std::max(1, mWindowHeight),
                 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &mMotionBlurFBO);

    mMotionBlurInitialized = true;
}

void GameState::renderMotionBlur() const noexcept
{
    if (!mMotionBlurInitialized || !mMotionBlurShader)
        return;

    const glm::vec2 playerVel = mWorld.getPlayerVelocity();
    const float speed = glm::length(playerVel);

    // Only apply motion blur when moving fast enough
    if (speed < 2.0f)
        return;

    const GLsizei w = std::max(1, mWindowWidth);
    const GLsizei h = std::max(1, mWindowHeight);

    // Blit default framebuffer into mMotionBlurTex via FBO (no CPU stall)
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mMotionBlurFBO);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mMotionBlurTex, 0);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // Apply motion blur as fullscreen pass back to default framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    mMotionBlurShader->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mMotionBlurTex);
    mMotionBlurShader->setUniform("uCurrentFrame", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mPrevFrameTex);
    mMotionBlurShader->setUniform("uPrevFrame", 1);
    mMotionBlurShader->setUniform("uVelocity", playerVel);
    mMotionBlurShader->setUniform("uBlurStrength", std::min(speed * 0.1f, 1.5f));

    glBindVertexArray(mVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Store result as previous frame via FBO blit
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mMotionBlurFBO);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mPrevFrameTex, 0);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
}

void GameState::renderRasterMaze() const noexcept
{
    if (!mRasterMazeShader || !mRasterSkyShader)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, mWindowWidth, mWindowHeight);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    mRasterSkyShader->bind();
    glBindVertexArray(mVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    const float aspectRatio = static_cast<float>(std::max(1, mWindowWidth)) / static_cast<float>(std::max(1, mWindowHeight));
    const glm::mat4 mvp = mCamera.getPerspective(aspectRatio) * mCamera.getLookAt();
    mRasterMazeShader->bind();
    mRasterMazeShader->setUniform("uMVP", mvp);
    mRasterMazeShader->setUniform("uTime", static_cast<float>(SDL_GetTicks()) * 0.001f);

    // Bind the sprite sheet for texture mapping on maze walls
    const Texture *spriteSheet = mWorld.getCharacterSpriteSheet();
    if (spriteSheet && spriteSheet->get() != 0)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, spriteSheet->get());
        mRasterMazeShader->setUniform("uSpriteSheet", 0);
        mRasterMazeShader->setUniform("uHasTexture", 1);
    }
    else
    {
        mRasterMazeShader->setUniform("uHasTexture", 0);
    }

    glBindVertexArray(mRasterMazeVAO);
    glDrawArrays(GL_TRIANGLES, 0, mRasterMazeVertexCount);
}

void GameState::updateRasterBirdsEyeZoomLimits() noexcept
{
    const float safeWidth = static_cast<float>(std::max(1, mWindowWidth));
    const float safeHeight = static_cast<float>(std::max(1, mWindowHeight));
    const float halfW = 0.5f * safeWidth;
    const float halfH = 0.5f * safeHeight;

    const float marginX = std::min(kRasterWallScreenMarginPx, std::max(1.0f, halfW - 1.0f));
    const float marginY = std::min(kRasterWallScreenMarginPx, std::max(1.0f, halfH - 1.0f));

    const float halfMazeW = 0.5f * std::max(0.001f, mRasterMazeWidth);
    const float halfMazeD = 0.5f * std::max(0.001f, mRasterMazeDepth);

    const float scaleX = halfW / std::max(1.0f, halfW - marginX);
    const float scaleY = halfH / std::max(1.0f, halfH - marginY);
    const float requiredHalfW = halfMazeW * scaleX;
    const float requiredHalfD = halfMazeD * scaleY;

    const float aspect = safeWidth / safeHeight;
    const float verticalHalf = std::max(requiredHalfD, requiredHalfW / std::max(0.001f, aspect));
    const float tanHalfFov = std::tan(glm::radians(kRasterBirdsEyeFovDeg * 0.5f));
    const float fitDistance = verticalHalf / std::max(0.001f, tanHalfFov);

    mRasterBirdsEyeMaxDistance = std::max(fitDistance, 2.0f);
    const float minDistanceFromTop = std::max(2.0f, (mRasterMazeTopY - mRasterMazeCenter.y) + kRasterZoomMinHeadroom);
    mRasterBirdsEyeMinDistance = std::min(minDistanceFromTop, mRasterBirdsEyeMaxDistance);
    mRasterBirdsEyeDistance = glm::clamp(mRasterBirdsEyeDistance, mRasterBirdsEyeMinDistance, mRasterBirdsEyeMaxDistance);
}

void GameState::applyRasterBirdsEyeCamera() noexcept
{
    mCamera.setMode(CameraMode::FIRST_PERSON);
    mCamera.setFieldOfView(kRasterBirdsEyeFovDeg);
    // Track the player's XZ position so the overhead camera follows the character.
    const glm::vec3 playerPos = mPlayer.getPosition();
    // Clamp camera XZ to maze extents so the maze stays on screen even if the
    // player has not yet been collision-resolved (e.g., the first frame).
    const float halfW = (mRasterMazeWidth  > 0.0f) ? mRasterMazeWidth  * 0.5f : 24.0f;
    const float halfD = (mRasterMazeDepth  > 0.0f) ? mRasterMazeDepth  * 0.5f : 24.0f;
    const float camX  = glm::clamp(playerPos.x, mRasterMazeCenter.x - halfW, mRasterMazeCenter.x + halfW);
    const float camZ  = glm::clamp(playerPos.z, mRasterMazeCenter.z - halfD, mRasterMazeCenter.z + halfD);
    mCamera.setPosition(glm::vec3(camX, mRasterBirdsEyeDistance + playerPos.y, camZ));
    // yaw=-90° gives: screen-right = world+X, screen-up = world−Z (row 0 at top).
    // Pitch slightly off vertical avoids a singular camera basis.
    mCamera.setYawPitch(-90.0f, -89.9f, /*clampPitch=*/false, /*wrapYaw=*/true);
}

void GameState::resolvePlayerWallCollisions(glm::vec3 &pos) const noexcept
{
    // Treat the player as a circle in the XZ plane and push it out of each wall AABB.
    // wall AABB packed as glm::vec4(minX, minZ, maxX, maxZ).
    constexpr float kPlayerRadius = 0.42f;  // fits through a corridor (cellSize - wallThickness ≈ 2.2)
    constexpr int   kIterations   = 6;      // multiple passes handle corner pile-ups

    for (int iter = 0; iter < kIterations; ++iter)
    {
        for (const auto &wall : mMazeWallAABBs)
        {
            const float closestX = glm::clamp(pos.x, wall.x, wall.z);
            const float closestZ = glm::clamp(pos.z, wall.y, wall.w);
            const float dx = pos.x - closestX;
            const float dz = pos.z - closestZ;
            const float distSq = dx * dx + dz * dz;

            if (distSq >= kPlayerRadius * kPlayerRadius)
                continue;   // no overlap

            if (distSq > 0.0f)
            {
                const float dist = std::sqrt(distSq);
                const float push = kPlayerRadius - dist;
                pos.x += (dx / dist) * push;
                pos.z += (dz / dist) * push;
            }
            else
            {
                // Centre inside the AABB — push along the axis of least penetration
                const float ox = std::min(pos.x - wall.x, wall.z - pos.x);
                const float oz = std::min(pos.z - wall.y, wall.w - pos.z);
                if (ox < oz)
                    pos.x += (pos.x < (wall.x + wall.z) * 0.5f) ? -(ox + kPlayerRadius) : (ox + kPlayerRadius);
                else
                    pos.z += (pos.z < (wall.y + wall.w) * 0.5f) ? -(oz + kPlayerRadius) : (oz + kPlayerRadius);
            }
        }
    }

    // Hard clamp to maze outer boundary as a safety net.
    if (mRasterMazeWidth > 0.0f && mRasterMazeDepth > 0.0f)
    {
        const float limitW = mRasterMazeWidth  * 0.5f - kPlayerRadius;
        const float limitD = mRasterMazeDepth  * 0.5f - kPlayerRadius;
        pos.x = glm::clamp(pos.x, mRasterMazeCenter.x - limitW, mRasterMazeCenter.x + limitW);
        pos.z = glm::clamp(pos.z, mRasterMazeCenter.z - limitD, mRasterMazeCenter.z + limitD);
    }
}

void GameState::cleanupResources() noexcept
{
    if (mRasterMazeShader)
    {
        mRasterMazeShader->cleanUp();
        mRasterMazeShader.reset();
    }
    if (mRasterSkyShader)
    {
        mRasterSkyShader->cleanUp();
        mRasterSkyShader.reset();
    }
    if (mRasterMazeVBO != 0)
    {
        glDeleteBuffers(1, &mRasterMazeVBO);
        mRasterMazeVBO = 0;
    }
    if (mRasterMazeVAO != 0)
    {
        glDeleteVertexArrays(1, &mRasterMazeVAO);
        mRasterMazeVAO = 0;
    }
    mRasterMazeVertexCount = 0;

    // Clean up motion blur resources
    if (mPickupVBO != 0)
    {
        glDeleteBuffers(1, &mPickupVBO);
        mPickupVBO = 0;
    }
    if (mPickupVAO != 0)
    {
        glDeleteVertexArrays(1, &mPickupVAO);
        mPickupVAO = 0;
    }
    mPickupVertexCount = 0;
    if (mMotionBlurFBO != 0)
    {
        glDeleteFramebuffers(1, &mMotionBlurFBO);
        mMotionBlurFBO = 0;
    }
    if (mMotionBlurTex != 0)
    {
        glDeleteTextures(1, &mMotionBlurTex);
        mMotionBlurTex = 0;
    }
    if (mPrevFrameTex != 0)
    {
        glDeleteTextures(1, &mPrevFrameTex);
        mPrevFrameTex = 0;
    }
    if (mMotionBlurShader)
    {
        mMotionBlurShader->cleanUp();
        mMotionBlurShader.reset();
    }
    mMotionBlurInitialized = false;

    GLSDLHelper::deleteVAO(mVAO);

    if (mBillboardFBO != 0)
    {
        glDeleteFramebuffers(1, &mBillboardFBO);
        mBillboardFBO = 0;
    }
    mBillboardColorTex = nullptr;
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
    mOITAccumTex = nullptr;
    mOITRevealTex = nullptr;
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
    mShadowTexture = nullptr;
    GLSDLHelper::deleteVAO(mShadowVAO);
    GLSDLHelper::deleteBuffer(mShadowVBO);
    mShadowsInitialized = false;

    // Clean up reflection resources
    if (mReflectionFBO != 0)
    {
        glDeleteFramebuffers(1, &mReflectionFBO);
        mReflectionFBO = 0;
    }
    mReflectionColorTex = nullptr;
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
    mWalkParticlesInitialized = false;
    mWalkParticlesComputeShader = nullptr;
    mWalkParticlesRenderShader = nullptr;
    mShadowShader = nullptr;

    mDisplayTex = nullptr;
    mNoiseTexture = nullptr;

    // Shaders are now managed by ShaderManager - don't delete them here
    mDisplayShader = nullptr;
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

    // Keep render targets synchronized with physical pixel size even if a resize
    // event was dropped or coalesced by the platform.
    handleWindowResize();

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

    const auto prevPickupCount = mWorld.getPickupSpheres().size();
    mWorld.update(dt);

    // Pre-read relative mouse delta BEFORE handleRealtimeInput consumes the accumulator.
    // This lets handleBirdsEyeInput receive the actual mouse movement for this frame.
    float relMouseX = 0.0f, relMouseY = 0.0f;
    SDL_GetRelativeMouseState(&relMouseX, &relMouseY);

    // Run Player logic for gravity / animation state / jump – but discard the
    // camera-relative XZ position change it produces (broken for straight-down camera).
    const glm::vec3 preMoveXZ = mPlayer.getPosition();
    mPlayer.handleRealtimeInput(mCamera, dt);
    {
        // Restore X/Z: keep only the Y (gravity) change from handleRealtimeInput.
        glm::vec3 afterGravityPos = mPlayer.getPosition();
        afterGravityPos.x = preMoveXZ.x;
        afterGravityPos.z = preMoveXZ.z;
        mPlayer.setPosition(afterGravityPos);
    }

    // Apply top-down XZ movement from keyboard, mouse, and touch.
    handleBirdsEyeInput(dt, relMouseX, relMouseY);
    // Joystick left-stick XZ movement.
    updateJoystickInput(dt);

    // Resolve against static maze wall geometry (circle vs AABB, multi-pass).
    {
        glm::vec3 pos = mPlayer.getPosition();
        resolvePlayerWallCollisions(pos);
        mPlayer.setPosition(pos);
    }

    // Sphere chunk streaming is for the path-tracer mode; skip it in the
    // raster maze to avoid triggering background work items every frame.
    // mWorld.update() still runs processCompletedChunks() internally.

    // If new pickups appeared (new chunks loaded), mark GPU buffer dirty
    if (mWorld.getPickupSpheres().size() != prevPickupCount)
        mPickupsDirty = true;

    // Update mSoundPlayer: set listener position based on camera and remove stopped mSoundPlayer
    if (!mGameIsPaused)
    {
        updateSounds();
    }

    // Update player animation
    mPlayer.updateAnimation(dt);

    // Collect nearby pickups for scoring
    {
        int pointsGained = mWorld.collectNearbyPickups(mPlayer.getPosition(), 3.5f);
        if (pointsGained != 0)
        {
            mPickupsDirty = true;
            // Add score popup
            mActiveScorePopups.emplace_back(mPlayer.getPosition() + glm::vec3(0.0f, 3.0f, 0.0f), pointsGained);
            mScorePopupTimers.push_back(2.0f);

            if (mSoundPlayer)
            {
                mSoundPlayer->play(SoundEffect::ID::SELECT,
                    sf::Vector2f{mPlayer.getPosition().x, mPlayer.getPosition().z});
            }
        }
    }

    // Update score popup timers
    for (size_t i = 0; i < mScorePopupTimers.size();)
    {
        mScorePopupTimers[i] -= dt;
        if (mScorePopupTimers[i] <= 0.0f)
        {
            mScorePopupTimers.erase(mScorePopupTimers.begin() + static_cast<ptrdiff_t>(i));
            mActiveScorePopups.erase(mActiveScorePopups.begin() + static_cast<ptrdiff_t>(i));
        }
        else
        {
            ++i;
        }
    }
    
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

    applyRasterBirdsEyeCamera();

    return true;
}

bool GameState::handleEvent(const SDL_Event &event) noexcept
{
    // Handle window resize event with DPI-aware pixel detection.
    if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        handleWindowResize();
    }

    // World still handles mouse panning for the 2D view
    mWorld.handleEvent(event);

    // Keep player event handling active for gameplay controls.
    mPlayer.handleEvent(event, mCamera);

    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        if (event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            mGameIsPaused = true;
            requestStackPush(States::ID::PAUSE);
        }

        // Jump with SPACE
        if (event.key.scancode == SDL_SCANCODE_SPACE)
        {
            // Apply jump impulse through Box2D physics
            mWorld.applyPlayerJumpImpulse(8.0f);
            mPlayer.jump();

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

        // Test haptic pulse with A key while keeping A strafe behavior unchanged.
        if (event.key.scancode == SDL_SCANCODE_A)
        {
            triggerHapticTest(0.65f, 0.08f);
        }
    }


    // Mouse wheel: bounded bird's-eye zoom
    if (event.type == SDL_EVENT_MOUSE_WHEEL)
    {
        mRasterBirdsEyeDistance -= static_cast<float>(event.wheel.y) * kRasterZoomStep;
        mRasterBirdsEyeDistance = glm::clamp(mRasterBirdsEyeDistance, mRasterBirdsEyeMinDistance, mRasterBirdsEyeMaxDistance);
        applyRasterBirdsEyeCamera();
    }

    // Touch events: single-finger drag moves the player (normalised 0‥1 coordinates).
    if (event.type == SDL_EVENT_FINGER_DOWN)
    {
        mTouchActive = true;
        mTouchLastPos = glm::vec2(event.tfinger.x, event.tfinger.y);
        mTouchDelta   = glm::vec2(0.0f);
    }
    else if (event.type == SDL_EVENT_FINGER_MOTION && mTouchActive)
    {
        const glm::vec2 currentPos(event.tfinger.x, event.tfinger.y);
        mTouchDelta  += currentPos - mTouchLastPos;
        mTouchLastPos = currentPos;
    }
    else if (event.type == SDL_EVENT_FINGER_UP)
    {
        mTouchActive = false;
        mTouchDelta  = glm::vec2(0.0f);
    }

    return true;
}

void GameState::configureCursorLock(bool enabled) noexcept
{
    if (mCursorLocked == enabled)
    {
        return;
    }

    auto *window = getContext().getRenderWindow();
    if (!window)
    {
        return;
    }

    SDL_Window *sdlWindow = window->getSDLWindow();
    if (!sdlWindow)
    {
        return;
    }

    if (enabled)
    {
        // Enable relative mouse mode first
        SDL_SetWindowRelativeMouseMode(sdlWindow, true);
        SDL_HideCursor();
        
        // Warp mouse to center (though not strictly necessary in relative mode)
        SDL_WarpMouseInWindow(sdlWindow,
                              static_cast<float>(mWindowWidth) * 0.5f,
                              static_cast<float>(mWindowHeight) * 0.5f);
        
        // Clear any accumulated relative motion state to prevent initial jump
        float dummyX = 0.0f, dummyY = 0.0f;
        SDL_GetRelativeMouseState(&dummyX, &dummyY);
    }
    else
    {
        SDL_SetWindowRelativeMouseMode(sdlWindow, false);
        SDL_ShowCursor();
    }

    mCursorLocked = enabled;
}

void GameState::initializeJoystickAndHaptics() noexcept
{
    if (mJoystick)
    {
        return;
    }

    int joystickCount = 0;
    SDL_JoystickID *joysticks = SDL_GetJoysticks(&joystickCount);
    if (!joysticks || joystickCount <= 0)
    {
        if (joysticks)
        {
            SDL_free(joysticks);
        }
        return;
    }

    mJoystick = SDL_OpenJoystick(joysticks[0]);
    SDL_free(joysticks);

    if (!mJoystick)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "GameState: Unable to open joystick: %s", SDL_GetError());
        return;
    }

    mJoystickRumbleSupported = SDL_RumbleJoystick(mJoystick, 0xFFFF, 0xFFFF, 1);
    if (!mJoystickRumbleSupported)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "GameState: Joystick connected without rumble support");
    }
    else
    {
        SDL_RumbleJoystick(mJoystick, 0, 0, 0);
        SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "GameState: Joystick and rumble initialized");
    }
}

void GameState::cleanupJoystickAndHaptics() noexcept
{
    if (mJoystick)
    {
        SDL_CloseJoystick(mJoystick);
        mJoystick = nullptr;
    }
    mJoystickRumbleSupported = false;
}

void GameState::handleBirdsEyeInput(float dt, float relMouseX, float relMouseY) noexcept
{
    if (!mPlayer.isActive() || mPlayer.isFrozen() || dt <= 0.0f)
        return;

    // World-space movement speeds for a birds-eye top-down view.
    // Screen-right = world +X, screen-up = world −Z (yaw=−90°).
    constexpr float kKeyMoveSpeed    = 6.0f;   // world units / second  (keyboard)
    constexpr float kMouseSensitivity = 0.018f;  // world units / pixel   (relative mouse)
    constexpr float kTouchSensitivity = 12.0f;  // world units / normalised delta (0‥1)
    constexpr float kMouseDeadzone    = 0.5f;   // pixels
    // Hard cap on XZ movement per frame — keeps substep count bounded even
    // during fast mouse flicks. Set to half a cell width (plenty fast).
    constexpr float kMaxMovePerFrame = 1.1f;    // world units / frame

    glm::vec3 delta(0.0f);

    // --- Keyboard (WASD + arrow keys) ---
    {
        int numKeys = 0;
        const bool *keys = SDL_GetKeyboardState(&numKeys);
        if (keys)
        {
            if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    delta.z -= kKeyMoveSpeed * dt;
            if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  delta.z += kKeyMoveSpeed * dt;
            if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  delta.x -= kKeyMoveSpeed * dt;
            if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) delta.x += kKeyMoveSpeed * dt;
        }
    }

    // --- Relative mouse (cursor locked, so relX/Y accumulate freely) ---
    // relMouseX > 0 → mouse moved right → player moves +X (screen-right)
    // relMouseY > 0 → mouse moved down  → player moves +Z (screen-down)
    if (mCursorLocked)
    {
        if (std::abs(relMouseX) > kMouseDeadzone) delta.x += relMouseX * kMouseSensitivity;
        if (std::abs(relMouseY) > kMouseDeadzone) delta.z += relMouseY * kMouseSensitivity;
    }

    // --- Touch (normalised 0‥1 finger coordinates) ---
    // Accumulated touch delta is consumed here each frame.
    if (mTouchActive && (std::abs(mTouchDelta.x) > 0.0001f || std::abs(mTouchDelta.y) > 0.0001f))
    {
        delta.x += mTouchDelta.x * kTouchSensitivity;
        delta.z += mTouchDelta.y * kTouchSensitivity;
        mTouchDelta = glm::vec2(0.0f);
    }

    if (glm::length(glm::vec2(delta.x, delta.z)) < 0.0001f)
        return;

    // Clamp total movement to kMaxMovePerFrame so a sudden large mouse delta
    // doesn't drive substep count into the hundreds (which spikes the CPU).
    {
        const float len = std::sqrt(delta.x * delta.x + delta.z * delta.z);
        if (len > kMaxMovePerFrame)
        {
            const float inv = kMaxMovePerFrame / len;
            delta.x *= inv;
            delta.z *= inv;
        }
    }

    // Advance in substeps no larger than kMaxSubstepDist (well under wall
    // thickness = 0.20) so the player can never skip past a wall in one frame.
    // After each substep the resolver sees the player approaching the wall
    // face-on, not already past it, so it always pushes back correctly.
    constexpr float kMaxSubstepDist = 0.07f;
    const float totalDist = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    const int numSteps = std::max(1, static_cast<int>(std::ceil(totalDist / kMaxSubstepDist)));
    const float sx = delta.x / static_cast<float>(numSteps);
    const float sz = delta.z / static_cast<float>(numSteps);

    glm::vec3 pos = mPlayer.getPosition();
    for (int i = 0; i < numSteps; ++i)
    {
        pos.x += sx;
        pos.z += sz;
        resolvePlayerWallCollisions(pos);
    }
    mPlayer.setPosition(pos);

    // Update facing direction to match the movement direction (top-down view).
    // atan2(dx, -dz) maps movement vector to a yaw angle in degrees.
    const float facingDeg = glm::degrees(std::atan2(delta.x, -delta.z));
    mPlayer.setFacingDirection(facingDeg);
}

void GameState::updateJoystickInput(float dt) noexcept
{
    if (!mJoystick || dt <= 0.0f || mPlayer.isFrozen())
    {
        return;
    }

    constexpr int kLeftStickXAxis = 0;
    constexpr int kLeftStickYAxis = 1;
    const Sint16 axisXRaw = SDL_GetJoystickAxis(mJoystick, kLeftStickXAxis);
    const Sint16 axisYRaw = SDL_GetJoystickAxis(mJoystick, kLeftStickYAxis);
    const float axisXNorm = std::clamp(static_cast<float>(axisXRaw) / 32767.0f, -1.0f, 1.0f);
    const float axisYNorm = std::clamp(static_cast<float>(axisYRaw) / 32767.0f, -1.0f, 1.0f);

    // Apply radial deadzone and normalise both axes together.
    const float magnitude = std::sqrt(axisXNorm * axisXNorm + axisYNorm * axisYNorm);
    if (magnitude <= mJoystickDeadzone)
        return;

    const float scale = (magnitude - mJoystickDeadzone) / std::max(0.001f, 1.0f - mJoystickDeadzone);
    const float normX = (axisXNorm / magnitude) * std::clamp(scale, 0.0f, 1.0f) * mJoystickStrafeSpeed * dt;
    const float normZ = (axisYNorm / magnitude) * std::clamp(scale, 0.0f, 1.0f) * mJoystickStrafeSpeed * dt;

    // Left-stick X → world +X (screen-right), left-stick Y → world +Z (screen-down).
    // Substep to prevent tunneling through thin walls (same approach as mouse input).
    constexpr float kMaxSubstepDist = 0.07f;
    const float totalDist = std::sqrt(normX * normX + normZ * normZ);
    const int numSteps = std::max(1, static_cast<int>(std::ceil(totalDist / kMaxSubstepDist)));
    const float sx = normX / static_cast<float>(numSteps);
    const float sz = normZ / static_cast<float>(numSteps);

    glm::vec3 playerPos = mPlayer.getPosition();
    for (int i = 0; i < numSteps; ++i)
    {
        playerPos.x += sx;
        playerPos.z += sz;
        resolvePlayerWallCollisions(playerPos);
    }
    mPlayer.setPosition(playerPos);
}

void GameState::triggerHapticTest(float strength, float seconds) noexcept
{
    if (!mJoystick || !mJoystickRumbleSupported)
    {
        return;
    }

    const float clampedStrength = std::clamp(strength, 0.0f, 1.0f);
    const Uint16 low = static_cast<Uint16>(clampedStrength * 65535.0f);
    const Uint16 high = static_cast<Uint16>(clampedStrength * 65535.0f);
    const Uint32 durationMs = static_cast<Uint32>(std::max(0.0f, seconds) * 1000.0f);

    SDL_RumbleJoystick(mJoystick, low, high, durationMs);
}

float GameState::getRenderScale() const noexcept
{
    return (mWindowWidth > 0 && mWindowHeight > 0)
               ? static_cast<float>(mRenderWidth) / static_cast<float>(mWindowWidth)
               : 1.0f;
}
