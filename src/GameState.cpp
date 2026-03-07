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
#include "PathTraceScene.hpp"
#include "Player.hpp"
#include "ResourceManager.hpp"
#include "SceneRenderer.hpp"
#include "Shader.hpp"
#include "SoundPlayer.hpp"
#include "Sphere.hpp"
#include "StateStack.hpp"
#include "Texture.hpp"

namespace
{
    constexpr GLint kTestAlbedoTextureUnit = 3;
    constexpr std::size_t kMaxPathTracerSpheres = 200;
    constexpr std::size_t kMaxPathTracerTriangles = 192;
    constexpr bool kEnableTrianglePathTraceProxy = true;
    constexpr float kPlayerProxyRadius = 1.4175f;
    constexpr float kPlayerShadowCenterYOffset = 1.4175f;
    constexpr float kCharacterRasterScale = 1.491f;
    constexpr float kCharacterPathTraceProxyScale = 1.491f;
    constexpr float kCharacterModelYOffset = 0.25f;
    constexpr float kFragmentRenderScale = 0.75f;
    constexpr int kFragmentTileWidth = 320;
    constexpr int kFragmentTileHeight = 180;
    constexpr int kFragmentPassesPerFrame = 6;

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

    uint32_t computeAdaptiveVoronoiCellBudget(uint32_t availableCells, int renderWidth, int renderHeight) noexcept
    {
        if (availableCells == 0u)
        {
            return 0u;
        }

        const std::size_t renderPixels = static_cast<std::size_t>(std::max(1, renderWidth)) *
                                         static_cast<std::size_t>(std::max(1, renderHeight));

        uint32_t budget = 1024u;
        if (renderPixels > 1800ull * 1000ull)
        {
            budget = 448u;
        }
        else if (renderPixels > 1400ull * 1000ull)
        {
            budget = 576u;
        }
        else if (renderPixels > 1000ull * 1000ull)
        {
            budget = 768u;
        }

        budget = std::max(256u, budget);
        return std::min(availableCells, budget);
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

    uint32_t computeAdaptivePathTraceTileEdge(int renderWidth, int renderHeight) noexcept
    {
        const std::size_t renderPixels = static_cast<std::size_t>(std::max(1, renderWidth)) *
                                         static_cast<std::size_t>(std::max(1, renderHeight));

        // Keep tile sizes aligned with the 20x20 local workgroup while adapting
        // frame cost for higher resolutions.
        if (renderPixels > 1800ull * 1000ull)
        {
            return 220u;
        }
        if (renderPixels > 1400ull * 1000ull)
        {
            return 280u;
        }
        if (renderPixels > 1000ull * 1000ull)
        {
            return 340u;
        }
        return 420u;
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
    : State{stack, context}, mWorld{*context.getRenderWindow(), *context.getFontManager(), *context.getTextureManager(), *context.getShaderManager(), *context.getLevelsManager()}, mPlayer{*context.getPlayer()}, mGameMusic{nullptr}, mDisplayShader{nullptr}, mComputeShader{nullptr}, mPathTracerOutputShader{nullptr}, mPathTracerTonemapShader{nullptr}, mCompositeShader{nullptr}, mOITResolveShader{nullptr}, mSkinnedModelShader{nullptr}, mGameIsPaused{false}
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
        mPathTracerOutputShader = &shaders.get(Shaders::ID::GLSL_PATH_TRACER_OUTPUT);
        mPathTracerTonemapShader = &shaders.get(Shaders::ID::GLSL_PATH_TRACER_TONEMAP);
        mCompositeShader = &shaders.get(Shaders::ID::GLSL_COMPOSITE_SCENE);
        mOITResolveShader = &shaders.get(Shaders::ID::GLSL_OIT_RESOLVE);
        mSkinnedModelShader = &shaders.get(Shaders::ID::GLSL_MODEL_WITH_SKINNING);
        mStencilOutlineShader = &shaders.get(Shaders::ID::GLSL_STENCIL_OUTLINE);
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
        mPathTraceOutputTex = &textures.get(Textures::ID::PATH_TRACER_OUTPUT);
        mPathTraceStageTex = &textures.get(Textures::ID::PATH_TRACER_STAGE);
        mDisplayTex = &textures.get(Textures::ID::PATH_TRACER_DISPLAY);
        mPreviewAccumTex = &textures.get(Textures::ID::PATH_TRACER_PREVIEW_ACCUM);
        mPreviewOutputTex = &textures.get(Textures::ID::PATH_TRACER_PREVIEW_OUTPUT);
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
        mAccumTex = nullptr;
        mPathTraceOutputTex = nullptr;
        mPathTraceStageTex = nullptr;
        mDisplayTex = nullptr;
        mPreviewAccumTex = nullptr;
        mPreviewOutputTex = nullptr;
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

    // Initialize GPU graphics pipeline following Compute.cpp approach
    if (mShadersInitialized)
    {
        initializeGraphicsResources();
        initializeWalkParticles();

        // Initialize billboard rendering for character sprites
        GLSDLHelper::initializeBillboardRendering();

        // â”€â”€ Fragment-shader path tracer (GLSLPT-style) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        try
        {
            auto &shaders = *context.getShaderManager();
            Shader *tileShader    = &shaders.get(Shaders::ID::GLSL_TILE_TEST);
            Shader *previewShader = &shaders.get(Shaders::ID::GLSL_PREVIEW_TEST);
            Shader *outputShader  = &shaders.get(Shaders::ID::GLSL_OUTPUT_TEST);
            Shader *tonemapShader = &shaders.get(Shaders::ID::GLSL_TONE_TEST);

            mPathTraceScene = std::make_unique<PathTraceScene>();
            buildPathTraceScene();

            auto &opts = mPathTraceScene->getRenderOptions();
            const int fragmentRenderWidth = std::max(1, static_cast<int>(std::lround(static_cast<float>(mRenderWidth) * kFragmentRenderScale)));
            const int fragmentRenderHeight = std::max(1, static_cast<int>(std::lround(static_cast<float>(mRenderHeight) * kFragmentRenderScale)));
            opts.renderResolution = {fragmentRenderWidth, fragmentRenderHeight};
            opts.windowResolution = {mWindowWidth, mWindowHeight};
            opts.tileWidth = kFragmentTileWidth;
            opts.tileHeight = kFragmentTileHeight;

            mSceneRenderer = std::make_unique<SceneRenderer>(mPathTraceScene.get());
            mSceneRenderer->setShaders(tileShader, previewShader, outputShader, tonemapShader);
            mSceneRenderer->rebuildShadersWithDefines();
            mSceneRenderer->initShaderUniforms();

            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameState: Fragment path tracer ready");
        }
        catch (const std::exception &e)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "GameState: Fragment path tracer init failed: %s", e.what());
            mSceneRenderer.reset();
            mPathTraceScene.reset();
        }
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
    if (mShadersInitialized)
    {
        if (mRenderMode == RenderMode::FRAGMENT && mSceneRenderer && mSceneRenderer->isInitialized())
        {
            renderWithFragmentShaders();
        }
        else
        {
            // Use compute shader rendering pipeline (path tracing)
            renderWithComputeShaders();


            // Keep rasterized character overlay for readability even when triangle proxy is enabled.
            renderPlayerCharacter();

            // Render character shadow to shadow texture
            renderCharacterShadow();

            // Render player reflection on ground plane
            renderPlayerReflection();

            // Note: Voronoi planet is rendered into the billboard FBO in renderPlayerCharacter()

            // Reset viewport to main window before compositing
            glViewport(0, 0, mWindowWidth, mWindowHeight);

            if (mCompositeShader && mBillboardFBO != 0 && mBillboardColorTex)
            {
                renderCompositeScene();
            }
        }
    }

    // â”€â”€ Render mode toggle (ImGui) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    {
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(mWindowWidth) - 260.0f, 16.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.7f);
        if (ImGui::Begin("Renderer", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            int mode = static_cast<int>(mRenderMode);
            ImGui::RadioButton("Compute", &mode, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Fragment (GLSLPT)", &mode, 1);
            if (static_cast<int>(mRenderMode) != mode)
            {
                // const_cast is safe here â€“ mRenderMode is mutable UI state
                const_cast<GameState *>(this)->mRenderMode = static_cast<RenderMode>(mode);
                if (const_cast<GameState *>(this)->mRenderMode == RenderMode::FRAGMENT)
                {
                    const_cast<GameState *>(this)->frameFragmentCornellCamera();
                }
                if (mSceneRenderer)
                    mSceneRenderer->markDirty();
            }
            if (mRenderMode == RenderMode::FRAGMENT && mSceneRenderer)
            {
                ImGui::Text("Samples: %d", mSceneRenderer->getSampleCount());
            }
        }
        ImGui::End();
    }
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
    if (mPathTracePostFBO == 0)
    {
        glGenFramebuffers(1, &mPathTracePostFBO);
    }
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

        // Initialize planet with spherical parameters: radius 50, center at origin
        mVoronoiPlanet.initialize(50.0f, glm::vec3(0.0f), 1024, 128, 64);
        mVoronoiPlanet.uploadToGPU();
    }
    catch (const std::exception &e)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "GameState: Voronoi planet shader/mesh init failed: %s", e.what());
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
    if (!mAccumTex || !mPathTraceOutputTex || !mPathTraceStageTex || !mDisplayTex || !mPreviewAccumTex || !mPreviewOutputTex)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Path tracer textures not initialized!");
        return;
    }

    mPreviewRenderWidth = std::max(1, mRenderWidth / 3);
    mPreviewRenderHeight = std::max(1, mRenderHeight / 3);

    bool accumSuccess = mAccumTex->loadRGBA32F(
        static_cast<int>(mRenderWidth),
        static_cast<int>(mRenderHeight),
        0);

    bool outputSuccess = mPathTraceOutputTex->loadRGBA32F(
        static_cast<int>(mRenderWidth),
        static_cast<int>(mRenderHeight),
        0);

    bool stageSuccess = mPathTraceStageTex->loadRGBA32F(
        static_cast<int>(mRenderWidth),
        static_cast<int>(mRenderHeight),
        0);

    bool displaySuccess = mDisplayTex->loadRGBA32F(
        static_cast<int>(mRenderWidth),
        static_cast<int>(mRenderHeight),
        0);

    bool previewAccumSuccess = mPreviewAccumTex->loadRGBA32F(
        static_cast<int>(mPreviewRenderWidth),
        static_cast<int>(mPreviewRenderHeight),
        0);

    bool previewOutputSuccess = mPreviewOutputTex->loadRGBA32F(
        static_cast<int>(mPreviewRenderWidth),
        static_cast<int>(mPreviewRenderHeight),
        0);

    if (!accumSuccess || !outputSuccess || !stageSuccess || !displaySuccess || !previewAccumSuccess || !previewOutputSuccess)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                     "GameState: Failed to resize path tracer textures (accum:%d output:%d stage:%d display:%d pAccum:%d pOutput:%d)",
                     accumSuccess, outputSuccess, stageSuccess, displaySuccess, previewAccumSuccess, previewOutputSuccess);
        return;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameState: Path tracer textures recreated (window/internal/preview):\t%dx%d / %dx%d / %dx%d",
        mWindowWidth, mWindowHeight, mRenderWidth, mRenderHeight,
        mPreviewRenderWidth, mPreviewRenderHeight);
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
        // Camera moved - update tracking only.
        // Accumulation policy (including motion blur windowing) is handled in renderWithComputeShaders().
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
    if (!mComputeShader || !mPathTracerOutputShader || !mPathTracerTonemapShader)
    {
        return;
    }

    if (!mComputeShader->isLinked())
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
    const bool cameraMoved = checkCameraMovement();

    // Update sphere data on GPU every frame (physics may have changed positions)
    const auto &spheres = mWorld.getSpheres();

    std::vector<Sphere> renderSpheres;
    renderSpheres.reserve(std::min<std::size_t>(spheres.size() + 1, kMaxPathTracerSpheres));

    const std::size_t baseCount = std::min<std::size_t>(spheres.size(), kMaxPathTracerSpheres);
    renderSpheres.insert(renderSpheres.end(), spheres.begin(), spheres.begin() + static_cast<std::ptrdiff_t>(baseCount));

    const std::size_t totalSphereCount = renderSpheres.size();
    const std::size_t primarySphereCount = totalSphereCount;

    // Handle case with zero spheres gracefully (can happen during arcade mode when spheres spawn/despawn)
    // The compute shader will simply render nothing, which is fine
    if (renderSpheres.empty())
    {
        // Return early - no spheres to process
        // This is normal during gameplay as spheres spawn ahead and despawn behind the player
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

    // Balanced profile: preserve clarity while avoiding full-frame compute every frame.
    constexpr bool kEnablePreviewDuringMotion = true;
    constexpr bool kQualityFirstDispatch = false;

    // Dynamic motion history blending.
    constexpr float kMotionBlurMinSpeed = 0.60f;
    constexpr float kMotionBlurFullSpeed = 9.0f;
    constexpr float kStaticHistoryBlend = 0.95f;
    constexpr float kMovingHistoryBlend = 0.86f;
    constexpr float kPreviewTriggerBlur = 0.30f;

    const float speed = mPlayerPlanarSpeedForFx;
    const float denom = std::max(0.001f, kMotionBlurFullSpeed - kMotionBlurMinSpeed);
    const float blurFactor = std::clamp((speed - kMotionBlurMinSpeed) / denom, 0.0f, 1.0f);

    const float effectiveBlurFactor = std::clamp(blurFactor, 0.0f, 1.0f);
    const float historyBlend = kStaticHistoryBlend + (kMovingHistoryBlend - kStaticHistoryBlend) * effectiveBlurFactor;

    constexpr uint32_t kStaticTilesPerFrame = 6u;
    constexpr uint32_t kMovingTilesPerFrame = 12u;
    const bool usePreviewMode = kEnablePreviewDuringMotion && (cameraMoved || effectiveBlurFactor > kPreviewTriggerBlur);
    const bool preferFullFrameDispatch = kQualityFirstDispatch || cameraMoved;

    Texture *targetAccumTex = usePreviewMode ? mPreviewAccumTex : mAccumTex;
    Texture *targetOutputTex = usePreviewMode ? mPreviewOutputTex : mPathTraceOutputTex;
    const int targetWidth = usePreviewMode ? mPreviewRenderWidth : mRenderWidth;
    const int targetHeight = usePreviewMode ? mPreviewRenderHeight : mRenderHeight;

    if (!targetAccumTex || !targetOutputTex || !mPathTraceStageTex || !mDisplayTex || mPathTracePostFBO == 0)
    {
        return;
    }

    // Always compute each frame; temporal history blending controls persistence.
    if (mTotalBatches > 0u)
    {
        mComputeShader->bind();

        // Calculate aspect ratio
        float ar = static_cast<float>(targetWidth) / static_cast<float>(targetHeight);

        // Set camera uniforms (following Compute.cpp renderPathTracer)
        mComputeShader->setUniform("uCamera.eye", mCamera.getActualPosition());
        mComputeShader->setUniform("uCamera.far", mCamera.getFar());
        mComputeShader->setUniform("uCamera.ray00", mCamera.getFrustumEyeRay(ar, -1, -1));
        mComputeShader->setUniform("uCamera.ray01", mCamera.getFrustumEyeRay(ar, -1, 1));
        mComputeShader->setUniform("uCamera.ray10", mCamera.getFrustumEyeRay(ar, 1, -1));
        mComputeShader->setUniform("uCamera.ray11", mCamera.getFrustumEyeRay(ar, 1, 1));

        const uint32_t samplesPerBatch = usePreviewMode
            ? std::max(2u, mSamplesPerBatch / 2u)
            : mSamplesPerBatch;


        // Set batch uniforms for progressive rendering
        mComputeShader->setUniform("uBatch", mCurrentBatch);
        mComputeShader->setUniform("uSamplesPerBatch", samplesPerBatch);

        const uint32_t adaptiveVoronoiCellCount = computeAdaptiveVoronoiCellBudget(
            static_cast<uint32_t>(mVoronoiPlanet.getCellCount()),
            mRenderWidth,
            mRenderHeight);
        const uint32_t adaptiveTriangleShadowBudget = computeAdaptiveTriangleShadowBudget(
            mRenderWidth,
            mRenderHeight);
        const uint32_t adaptiveTileEdge = computeAdaptivePathTraceTileEdge(
            mRenderWidth,
            mRenderHeight);
        // Bind Voronoi cell color/seed/painted SSBOs used by the compute shader.
        // Keep cell count at zero unless all buffers exist for safe access.
        mComputeShader->setUniform("uVoronoiCellCount", static_cast<GLuint>(0));
        mComputeShader->setUniform("uPlanetRadius", mVoronoiPlanet.getRadius());
        mComputeShader->setUniform("uPlanetCenter", mVoronoiPlanet.getCenter());
        mComputeShader->setUniform("uTriangleShadowTestBudget", adaptiveTriangleShadowBudget);
        if (mVoronoiCellColorSSBO != 0 && mVoronoiCellSeedSSBO != 0 && mVoronoiCellPaintedSSBO != 0) {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, mVoronoiCellColorSSBO);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, mVoronoiCellSeedSSBO);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, mVoronoiCellPaintedSSBO);
            mComputeShader->setUniform("uVoronoiCellCount", adaptiveVoronoiCellCount);
        }

        // Set sphere count uniform (NEW - tells shader how many spheres to check)
        mComputeShader->setUniform("uSphereCount", static_cast<uint32_t>(totalSphereCount));
        mComputeShader->setUniform("uPrimaryRaySphereCount", static_cast<uint32_t>(primarySphereCount));
        mComputeShader->setUniform("uTriangleCount", kEnableTrianglePathTraceProxy ? static_cast<uint32_t>(traceTriangles.size()) : 0u);

        static constexpr auto NOISE_TEXTURE_UNIT = 2;

        mComputeShader->setUniform("uTime", static_cast<GLfloat>(timeSeconds));
        mComputeShader->setUniform("uHistoryBlend", historyBlend);
        mComputeShader->setUniform("uNoiseTex", static_cast<GLint>(NOISE_TEXTURE_UNIT));
        mComputeShader->setUniform("uTestAlbedoTex", kTestAlbedoTextureUnit);
        mComputeShader->setUniform("uTestTextureStrength", mTestAlbedoTexture ? 1.0f : 0.0f);
        mComputeShader->setUniform("uSphereTexScale", glm::vec2(2.2f, 1.0f));
        mComputeShader->setUniform("uTriangleTexScale", glm::vec2(3.0f, 3.0f));
        // Shadow casting uniforms
        mComputeShader->setUniform("uPlayerPos", mPlayer.getPosition() + glm::vec3(0.0f, kPlayerShadowCenterYOffset, 0.0f));
        mComputeShader->setUniform("uPlayerRadius", kPlayerProxyRadius);

        mComputeShader->setUniform("uLightDir", computeSunDirection(timeSeconds));

        // Bind both textures as images for compute shader
        glBindImageTexture(0, targetAccumTex->get(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
        glBindImageTexture(1, targetOutputTex->get(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);

        // Bind starfield noise texture sampler to texture unit 2
        glActiveTexture(GL_TEXTURE0 + NOISE_TEXTURE_UNIT);
        glBindTexture(GL_TEXTURE_2D, mNoiseTexture ? mNoiseTexture->get() : 0);

        glActiveTexture(GL_TEXTURE0 + kTestAlbedoTextureUnit);
        glBindTexture(GL_TEXTURE_2D, mTestAlbedoTexture ? mTestAlbedoTexture->get() : 0);

        // Dispatch one or more adaptive tiles this frame.
        // Movement/camera changes force full-frame updates to avoid patchy temporal lag.
        const uint32_t targetTilesX = std::max(1u, (static_cast<uint32_t>(targetWidth) + adaptiveTileEdge - 1u) / adaptiveTileEdge);
        const uint32_t targetTilesY = std::max(1u, (static_cast<uint32_t>(targetHeight) + adaptiveTileEdge - 1u) / adaptiveTileEdge);
        const uint32_t targetTotalTiles = std::max(1u, targetTilesX * targetTilesY);
        const uint32_t desiredTilesPerFrame = usePreviewMode ? kMovingTilesPerFrame : kStaticTilesPerFrame;
        const uint32_t targetDispatchTileCount = preferFullFrameDispatch
            ? targetTotalTiles
            : std::min(targetTotalTiles, desiredTilesPerFrame);

        for (uint32_t tileOffset = 0u; tileOffset < targetDispatchTileCount; ++tileOffset)
        {
            const uint32_t tileIndex = (mCurrentTileIndex + tileOffset) % targetTotalTiles;
            const uint32_t tileX = tileIndex % targetTilesX;
            const uint32_t tileY = tileIndex / targetTilesX;

            const uint32_t tileOriginX = tileX * adaptiveTileEdge;
            const uint32_t tileOriginY = tileY * adaptiveTileEdge;
            const uint32_t tileWidth = std::max(1u, std::min(adaptiveTileEdge, static_cast<uint32_t>(targetWidth) - tileOriginX));
            const uint32_t tileHeight = std::max(1u, std::min(adaptiveTileEdge, static_cast<uint32_t>(targetHeight) - tileOriginY));

            mComputeShader->setUniform("uTileOrigin", glm::uvec2(tileOriginX, tileOriginY));
            mComputeShader->setUniform("uTileSize", glm::uvec2(tileWidth, tileHeight));

            GLuint groupsX = (tileWidth + 19) / 20;
            GLuint groupsY = (tileHeight + 19) / 20;
            glDispatchCompute(groupsX, groupsY, 1);
        }

        // Memory barrier to ensure compute shader writes are visible
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        const uint32_t advancedTiles = targetDispatchTileCount;
        const uint32_t nextTile = (mCurrentTileIndex + advancedTiles) % targetTotalTiles;
        const uint32_t wraps = (mCurrentTileIndex + advancedTiles) / targetTotalTiles;
        mCurrentTileIndex = nextTile;
        for (uint32_t i = 0u; i < wraps; ++i)
        {
            if (mCurrentBatch == 0xFFFFFFFFu)
            {
                mCurrentBatch = 0u;
            }
            else
            {
                mCurrentBatch++;
            }
        }
    }

    // GLSLPT-style output workflow:
    // 1) Copy current tracing result into an intermediate full-resolution stage
    // 2) Tonemap stage into the display texture used by final scene compositing
    glBindFramebuffer(GL_FRAMEBUFFER, mPathTracePostFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mPathTraceStageTex->get(), 0);
    glViewport(0, 0, mRenderWidth, mRenderHeight);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    mPathTracerOutputShader->bind();
    mPathTracerOutputShader->setUniform("uInputTex", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, targetOutputTex->get());
    glBindVertexArray(mVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mDisplayTex->get(), 0);
    glViewport(0, 0, mRenderWidth, mRenderHeight);
    mPathTracerTonemapShader->bind();
    mPathTracerTonemapShader->setUniform("uInputTex", 0);
    mPathTracerTonemapShader->setUniform("uExposure", 1.0f);
    mPathTracerTonemapShader->setUniform("uEnableTonemap", static_cast<GLint>(1));
    mPathTracerTonemapShader->setUniform("uPreviewMode", static_cast<GLint>(usePreviewMode ? 1 : 0));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mPathTraceStageTex->get());
    glBindVertexArray(mVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, mWindowWidth, mWindowHeight);

    glDepthMask(GL_TRUE);
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Fragment-shader path tracing (GLSLPT-style)
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void GameState::renderWithFragmentShaders() const noexcept
{
    if (!mSceneRenderer || !mSceneRenderer->isInitialized())
        return;

    const bool isDirty = (mPathTraceScene && mPathTraceScene->dirty);
    const int passesThisFrame = isDirty ? 1 : kFragmentPassesPerFrame;

    for (int i = 0; i < passesThisFrame; ++i)
    {
        mSceneRenderer->update(0.0f);
        mSceneRenderer->render();
    }

    mSceneRenderer->present();
}

void GameState::syncPathTraceCamera() noexcept
{
    if (!mPathTraceScene)
        return;

    auto &ptCam = mPathTraceScene->getCamera();

    ptCam.position = mCamera.getActualPosition();

    const glm::vec3 fwd = glm::normalize(mCamera.getTarget());
    const glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
    const glm::vec3 right = glm::normalize(glm::cross(fwd, worldUp));
    const glm::vec3 up = glm::normalize(glm::cross(right, fwd));

    ptCam.forward = fwd;
    ptCam.right = right;
    ptCam.up = up;

    ptCam.fov = glm::radians(45.0f);

    // Mark scene dirty when camera moves so accumulation resets
    const glm::vec3 actualCameraPos = mCamera.getActualPosition();

    const bool cameraMoved =
        (glm::distance(mLastCameraPosition, actualCameraPos) > 0.001f) ||
        (std::abs(mLastCameraYaw - mCamera.getYaw()) > 0.01f) ||
        (std::abs(mLastCameraPitch - mCamera.getPitch()) > 0.01f);

    if (cameraMoved)
        mPathTraceScene->dirty = true;

    // Update movement baseline every frame so we only reset accumulation on actual deltas.
    mLastCameraPosition = actualCameraPos;
    mLastCameraYaw = mCamera.getYaw();
    mLastCameraPitch = mCamera.getPitch();
}

void GameState::frameFragmentCornellCamera() noexcept
{
    // Cornell scene is centered near origin and open toward +Z.
    mCamera.setMode(CameraMode::FIRST_PERSON);
    mCamera.setPosition(glm::vec3(0.0f, 0.0f, 12.0f));
    mCamera.setYawPitch(-90.0f, 0.0f);

    mLastCameraPosition = mCamera.getActualPosition();
    mLastCameraYaw = mCamera.getYaw();
    mLastCameraPitch = mCamera.getPitch();

    if (mPathTraceScene)
    {
        mPathTraceScene->dirty = true;
    }
    if (mSceneRenderer)
    {
        mSceneRenderer->markDirty();
    }
}

void GameState::buildPathTraceScene() noexcept
{
    if (!mPathTraceScene)
        return;

    // â”€â”€ Cornell Box â€“ classic test scene â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Box dimensions: 10Ã—10Ã—10, centered at origin

    // Meshes: each wall is two triangles (a quad)
    auto makeQuad = [](const glm::vec3 &v0, const glm::vec3 &v1,
                       const glm::vec3 &v2, const glm::vec3 &v3,
                       const glm::vec3 &normal) -> PTMesh
    {
        PTMesh mesh;
        // Triangle 1: v0, v1, v2
        mesh.verticesUVX.push_back(glm::vec4(v0, 0.0f));
        mesh.verticesUVX.push_back(glm::vec4(v1, 1.0f));
        mesh.verticesUVX.push_back(glm::vec4(v2, 1.0f));
        mesh.normalsUVY.push_back(glm::vec4(normal, 0.0f));
        mesh.normalsUVY.push_back(glm::vec4(normal, 0.0f));
        mesh.normalsUVY.push_back(glm::vec4(normal, 1.0f));
        // Triangle 2: v0, v2, v3
        mesh.verticesUVX.push_back(glm::vec4(v0, 0.0f));
        mesh.verticesUVX.push_back(glm::vec4(v2, 1.0f));
        mesh.verticesUVX.push_back(glm::vec4(v3, 0.0f));
        mesh.normalsUVY.push_back(glm::vec4(normal, 0.0f));
        mesh.normalsUVY.push_back(glm::vec4(normal, 1.0f));
        mesh.normalsUVY.push_back(glm::vec4(normal, 1.0f));
        return mesh;
    };

    const float H = 5.0f; // half-extent

    // â”€â”€ Materials â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    PTMaterial whiteMat;
    whiteMat.baseColor = glm::vec3(0.73f);
    whiteMat.roughness = 1.0f;
    int whiteID = mPathTraceScene->addMaterial(whiteMat);

    PTMaterial redMat;
    redMat.baseColor = glm::vec3(0.65f, 0.05f, 0.05f);
    redMat.roughness = 1.0f;
    int redID = mPathTraceScene->addMaterial(redMat);

    PTMaterial greenMat;
    greenMat.baseColor = glm::vec3(0.12f, 0.45f, 0.15f);
    greenMat.roughness = 1.0f;
    int greenID = mPathTraceScene->addMaterial(greenMat);

    PTMaterial lightMat;
    lightMat.baseColor = glm::vec3(0.0f);
    // Use neutral white emitter to avoid warm/yellow scene bias.
    lightMat.emission = glm::vec3(15.0f);
    int lightID = mPathTraceScene->addMaterial(lightMat);

    // â”€â”€ Walls â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Floor (white)
    PTMesh floor = makeQuad(
        {-H, -H, -H}, {H, -H, -H}, {H, -H, H}, {-H, -H, H},
        {0, 1, 0});
    floor.name = "floor";
    int floorMeshID = mPathTraceScene->addMesh(std::move(floor));
    mPathTraceScene->addMeshInstance({"floor", floorMeshID, whiteID, glm::mat4(1.0f)});

    // Ceiling (white)
    PTMesh ceiling = makeQuad(
        {-H, H, H}, {H, H, H}, {H, H, -H}, {-H, H, -H},
        {0, -1, 0});
    ceiling.name = "ceiling";
    int ceilingMeshID = mPathTraceScene->addMesh(std::move(ceiling));
    mPathTraceScene->addMeshInstance({"ceiling", ceilingMeshID, whiteID, glm::mat4(1.0f)});

    // Back wall (white)
    PTMesh backWall = makeQuad(
        {-H, -H, -H}, {-H, H, -H}, {H, H, -H}, {H, -H, -H},
        {0, 0, 1});
    backWall.name = "back";
    int backMeshID = mPathTraceScene->addMesh(std::move(backWall));
    mPathTraceScene->addMeshInstance({"back", backMeshID, whiteID, glm::mat4(1.0f)});

    // Left wall (red)
    PTMesh leftWall = makeQuad(
        {-H, -H, H}, {-H, H, H}, {-H, H, -H}, {-H, -H, -H},
        {1, 0, 0});
    leftWall.name = "left";
    int leftMeshID = mPathTraceScene->addMesh(std::move(leftWall));
    mPathTraceScene->addMeshInstance({"left", leftMeshID, redID, glm::mat4(1.0f)});

    // Right wall (green)
    PTMesh rightWall = makeQuad(
        {H, -H, -H}, {H, H, -H}, {H, H, H}, {H, -H, H},
        {-1, 0, 0});
    rightWall.name = "right";
    int rightMeshID = mPathTraceScene->addMesh(std::move(rightWall));
    mPathTraceScene->addMeshInstance({"right", rightMeshID, greenID, glm::mat4(1.0f)});

    // â”€â”€ Light (small quad on ceiling) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    const float L = 1.3f; // light half-size
    PTMesh lightQuad = makeQuad(
        {-L, H - 0.01f, -L}, {L, H - 0.01f, -L},
        {L, H - 0.01f, L}, {-L, H - 0.01f, L},
        {0, -1, 0});
    lightQuad.name = "light";
    int lightMeshID = mPathTraceScene->addMesh(std::move(lightQuad));
    mPathTraceScene->addMeshInstance({"light", lightMeshID, lightID, glm::mat4(1.0f)});

    // Add area light descriptor for shader sampling
    PTLight areaLight;
    areaLight.position = glm::vec3(0.0f, H - 0.01f, 0.0f);
    areaLight.emission = glm::vec3(15.0f);
    areaLight.u = glm::vec3(L * 2.0f, 0.0f, 0.0f);
    areaLight.v = glm::vec3(0.0f, 0.0f, L * 2.0f);
    areaLight.area = (L * 2.0f) * (L * 2.0f);
    areaLight.type = 0.0f; // quad light
    mPathTraceScene->addLight(areaLight);

    // â”€â”€ Camera â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    PTCamera cam;
    cam.position = glm::vec3(0.0f, 0.0f, 3.0f);
    cam.forward = glm::vec3(0.0f, 0.0f, -1.0f);
    cam.up = glm::vec3(0.0f, 1.0f, 0.0f);
    cam.right = glm::vec3(1.0f, 0.0f, 0.0f);
    cam.fov = glm::radians(45.0f);
    cam.focalDist = 8.0f;
    cam.aperture = 0.0f;
    mPathTraceScene->setCamera(cam);

    // â”€â”€ Render options â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    auto &opts = mPathTraceScene->getRenderOptions();
    opts.maxDepth = 4;
    opts.enableUniformLight = false;
    opts.uniformLightCol = glm::vec3(0.3f);
    opts.enableEnvMap = false;
    opts.enableBackground = false;
    opts.backgroundCol = glm::vec3(0.0f);

    mPathTraceScene->processScene();

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "GameState: Cornell Box scene built  meshes=%d tris=%zu lights=%zu",
                static_cast<int>(mPathTraceScene->getMeshInstances().size()),
                mPathTraceScene->getVertIndices().size(),
                mPathTraceScene->getLights().size());
}

void GameState::renderCompositeScene() const noexcept
{
    if (!mCompositeShader || !mDisplayTex || !mBillboardColorTex || mBillboardColorTex->get() == 0)
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
    mCompositeShader->setUniform("uEnableShadows", mShadowsInitialized && mShadowTexture && mShadowTexture->get() != 0);
    mCompositeShader->setUniform("uEnableReflections", mReflectionsInitialized && mReflectionColorTex && mReflectionColorTex->get() != 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mDisplayTex->get());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mBillboardColorTex->get());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, mShadowTexture ? mShadowTexture->get() : 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, mReflectionColorTex ? mReflectionColorTex->get() : 0);

    glBindVertexArray(mVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void GameState::resolveOITToBillboardTarget() const noexcept
{
    if (!mOITResolveShader || mBillboardFBO == 0 || !mOITAccumTex || !mOITRevealTex || mOITAccumTex->get() == 0 || mOITRevealTex->get() == 0)
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
    glBindTexture(GL_TEXTURE_2D, mOITAccumTex->get());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mOITRevealTex->get());

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
    // Clean up fragment path tracer resources
    if (mSceneRenderer)
    {
        mSceneRenderer->cleanup();
        mSceneRenderer.reset();
    }
    mPathTraceScene.reset();

        if (mPathTracePostFBO != 0)
        {
            glDeleteFramebuffers(1, &mPathTracePostFBO);
            mPathTracePostFBO = 0;
        }

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

    mAccumTex = nullptr;
    mPathTraceOutputTex = nullptr;
    mPathTraceStageTex = nullptr;
    mDisplayTex = nullptr;
    mPreviewAccumTex = nullptr;
    mPreviewOutputTex = nullptr;
    mNoiseTexture = nullptr;

    // Shaders are now managed by ShaderManager - don't delete them here
    mDisplayShader = nullptr;
    mComputeShader = nullptr;
    mPathTracerOutputShader = nullptr;
    mPathTracerTonemapShader = nullptr;
    mCompositeShader = nullptr;
    mOITResolveShader = nullptr;
    mStencilOutlineShader = nullptr;
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

    mPlayer.handleRealtimeInput(mCamera, dt);
    
    updateJoystickInput(dt);

    glm::vec3 chunkAnchor = mPlayer.getPosition();
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

    // Update fragment path tracer camera (render() advances tile/sample state)
    if (mRenderMode == RenderMode::FRAGMENT && mSceneRenderer && mPathTraceScene)
    {
        syncPathTraceCamera();
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

                    if (mPathTraceScene && mSceneRenderer)
                    {
                        auto &opts = mPathTraceScene->getRenderOptions();
                        const int fragmentRenderWidth = std::max(1, static_cast<int>(std::lround(static_cast<float>(mRenderWidth) * kFragmentRenderScale)));
                        const int fragmentRenderHeight = std::max(1, static_cast<int>(std::lround(static_cast<float>(mRenderHeight) * kFragmentRenderScale)));
                        opts.renderResolution = {fragmentRenderWidth, fragmentRenderHeight};
                        opts.windowResolution = {mWindowWidth, mWindowHeight};
                        opts.tileWidth = kFragmentTileWidth;
                        opts.tileHeight = kFragmentTileHeight;
                        mSceneRenderer->initFBOs();
                        mSceneRenderer->initShaderUniforms();
                        mSceneRenderer->markDirty();
                    }

                    // Recreate path tracer textures
                    createPathTracerTextures();
                    createCompositeTargets();
                    initializeShadowResources();
                    initializeReflectionResources();
                    // Reset accumulation for new size
                    mCurrentBatch = 0;
                    mCurrentTileIndex = 0;
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

        // Test haptic pulse with A key while keeping A strafe behavior unchanged.
        if (event.key.scancode == SDL_SCANCODE_A)
        {
            triggerHapticTest(0.65f, 0.08f);
        }

        // Toggle render mode with F5
        if (event.key.scancode == SDL_SCANCODE_F5)
        {
            mRenderMode = (mRenderMode == RenderMode::COMPUTE)
                              ? RenderMode::FRAGMENT
                              : RenderMode::COMPUTE;
            if (mRenderMode == RenderMode::FRAGMENT)
            {
                frameFragmentCornellCamera();
            }
            if (mSceneRenderer)
                mSceneRenderer->markDirty();
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameState: Render mode â†’ %s",
                        mRenderMode == RenderMode::COMPUTE ? "Compute" : "Fragment");
        }
    }

    // Handle mouse motion for camera rotation (right mouse button)
    if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        std::uint32_t mouseState = SDL_GetMouseState(nullptr, nullptr);
        if (mouseState & SDL_BUTTON_RMASK)
        {
            static constexpr float SENSITIVITY = 0.35f;
            if (mCamera.getMode() == CameraMode::THIRD_PERSON)
            {
                mCamera.rotate(event.motion.xrel * SENSITIVITY, -event.motion.yrel * SENSITIVITY);
                mCamera.updateThirdPersonPosition();
            }
            else
            {
                // First-person camera rotation
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

void GameState::updateJoystickInput(float dt) noexcept
{
    if (!mJoystick || dt <= 0.0f)
    {
        return;
    }

    constexpr int kLeftStickXAxis = 0;
    const Sint16 axisRaw = SDL_GetJoystickAxis(mJoystick, kLeftStickXAxis);
    const float axisNorm = std::clamp(static_cast<float>(axisRaw) / 32767.0f, -1.0f, 1.0f);
    if (std::abs(axisNorm) <= mJoystickDeadzone)
    {
        return;
    }

    const float sign = (axisNorm >= 0.0f) ? 1.0f : -1.0f;
    const float mag = (std::abs(axisNorm) - mJoystickDeadzone) / std::max(0.001f, 1.0f - mJoystickDeadzone);
    const float strafe = sign * std::clamp(mag, 0.0f, 1.0f) * mJoystickStrafeSpeed * dt;

    glm::vec3 playerPos = mPlayer.getPosition();
    playerPos.z += strafe;
    mPlayer.setPosition(playerPos);
    mCamera.setFollowTarget(playerPos);
    if (mCamera.getMode() == CameraMode::THIRD_PERSON)
    {
        mCamera.updateThirdPersonPosition();
    }
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
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
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
    glDisable(GL_STENCIL_TEST);
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
                    const glm::mat4 viewMatrix = mCamera.getLookAt();
                    const glm::mat4 projMatrix = mCamera.getPerspective(aspectRatio);
                    const bool drawStencilOutline =
                        mStencilOutlineEnabled &&
                        mStencilOutlineShader &&
                        mStencilOutlineShader->isLinked() &&
                        mStencilOutlineWidth > 0.0005f;

                    glGetBooleanv(GL_CULL_FACE, &cullFaceWasEnabled);
                    cullStateCaptured = true;
                    glDisable(GL_CULL_FACE);

                    if (drawStencilOutline)
                    {
                        glEnable(GL_STENCIL_TEST);
                        glStencilMask(0xFF);
                        glClear(GL_STENCIL_BUFFER_BIT);
                        glStencilFunc(GL_ALWAYS, 1, 0xFF);
                        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                    }

                    // Build model matrix with player oriented to planet surface
                    glm::mat4 modelMatrix(1.0f);
                    const glm::vec3 playerPos = mPlayer.getPosition();
                    modelMatrix = glm::translate(modelMatrix, playerPos);
                    
                    // Build local coordinate frame aligned with planet surface
                    const glm::vec3 upAxis = mPlayer.getSurfaceNormal();
                    glm::vec3 forwardDir = mPlayer.getForwardTangent();  // Forward along sphere surface
                    
                    // Apply yaw rotation around the surface normal
                    const float playerYaw = glm::radians(mPlayer.getFacingDirection());
                    
                    // Calculate right vector (perpendicular to both forward and up)
                    glm::vec3 rightDir = glm::normalize(glm::cross(forwardDir, upAxis));
                    if (glm::length(rightDir) < 0.001f)
                    {
                        // Fallback if forward and up are parallel
                        rightDir = glm::vec3(1.0f, 0.0f, 0.0f);
                    }
                    
                    // Rotate forward and right by player yaw around surface normal
                    const float cosYaw = std::cos(playerYaw);
                    const float sinYaw = std::sin(playerYaw);
                    forwardDir = forwardDir * cosYaw + rightDir * sinYaw;
                    rightDir = rightDir * cosYaw - glm::normalize(mPlayer.getForwardTangent()) * sinYaw;
                    
                    // Correct any numerical drift and ensure orthogonality
                    rightDir = glm::normalize(rightDir);
                    forwardDir = glm::normalize(forwardDir);
                    if (glm::length(glm::cross(rightDir, upAxis)) < 0.001f)
                    {
                        rightDir = glm::normalize(glm::cross(upAxis, forwardDir));
                    }
                    
                    // Build rotation matrix from world axes to local axes
                    glm::mat4 rotationMatrix = glm::mat4(
                        glm::vec4(rightDir, 0.0f),
                        glm::vec4(upAxis, 0.0f),
                        glm::vec4(-forwardDir, 0.0f),
                        glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)
                    );
                    
                    modelMatrix = modelMatrix * rotationMatrix;
                    modelMatrix = glm::scale(modelMatrix, glm::vec3(kCharacterRasterScale));

                    mSkinnedModelShader->bind();
                    mSkinnedModelShader->setUniform("uSunDir", computeSunDirection(timeSeconds));
                    mSkinnedModelShader->setUniform("uAlbedoTex", kTestAlbedoTextureUnit);
                    mSkinnedModelShader->setUniform("uUseAlbedoTex", mTestAlbedoTexture ? 1 : 0);
                    mSkinnedModelShader->setUniform("uAlbedoUVScale", glm::vec2(3.0f, 3.0f));

                    glActiveTexture(GL_TEXTURE0 + kTestAlbedoTextureUnit);
                    glBindTexture(GL_TEXTURE_2D, mTestAlbedoTexture ? mTestAlbedoTexture->get() : 0);

                    characterModel.render(
                        *mSkinnedModelShader,
                        modelMatrix,
                        viewMatrix,
                        projMatrix,
                        mModelAnimTimeSeconds);

                    if (drawStencilOutline)
                    {
                        const float outlineScale = 1.0f + mStencilOutlineWidth;
                        glm::mat4 outlineModel = modelMatrix;
                        outlineModel = glm::scale(outlineModel, glm::vec3(outlineScale));

                        glm::vec3 outlineColor = mStencilOutlineColor;
                        if (mStencilOutlinePulseEnabled)
                        {
                            const float pulse = 0.5f + 0.5f * std::sin(timeSeconds * mStencilOutlinePulseSpeed * 6.2831853f);
                            const float pulseMix = std::clamp(mStencilOutlinePulseAmount * pulse, 0.0f, 0.85f);
                            outlineColor = glm::mix(outlineColor, glm::vec3(1.0f), pulseMix);
                        }

                        glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
                        glStencilMask(0x00);
                        glDisable(GL_DEPTH_TEST);

                        mStencilOutlineShader->bind();
                        mStencilOutlineShader->setUniform("uOutlineColor", glm::vec4(outlineColor, 1.0f));

                        characterModel.render(
                            *mStencilOutlineShader,
                            outlineModel,
                            viewMatrix,
                            projMatrix,
                            mModelAnimTimeSeconds);

                        glStencilMask(0xFF);
                        glStencilFunc(GL_ALWAYS, 0, 0xFF);
                        glEnable(GL_DEPTH_TEST);
                        glDisable(GL_STENCIL_TEST);
                    }

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
        GLSDLHelper::setBillboardOITPass(false);

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        resolveOITToBillboardTarget();
        glDisable(GL_BLEND);
    }
    else
    {
        GLSDLHelper::setBillboardOITPass(false);
    }

    if (mCompositeShader && mBillboardFBO != 0)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDrawBuffer(GL_BACK); // CRITICAL: Reset to default for main framebuffer
        glReadBuffer(GL_BACK); // CRITICAL: Reset to default for main framebuffer
    }
}

float GameState::getRenderScale() const noexcept
{
    return (mWindowWidth > 0 && mWindowHeight > 0)
               ? static_cast<float>(mRenderWidth) / static_cast<float>(mWindowWidth)
               : 1.0f;
}
