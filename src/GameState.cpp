#include "GameState.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <random>
#include <vector>

#include <glm/glm.hpp>
#include <glad/glad.h>

#include "GLSDLHelper.hpp"
#include "MusicPlayer.hpp"
#include "Player.hpp"
#include "ResourceManager.hpp"
#include "Shader.hpp"
#include "SoundPlayer.hpp"
#include "Sphere.hpp"
#include "StateStack.hpp"
#include "Texture.hpp"

namespace
{
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
}

GameState::GameState(StateStack &stack, Context context)
    : State{stack, context}, mWorld{*context.window, *context.fonts, *context.textures, *context.shaders}, mPlayer{*context.player}, mGameMusic{nullptr}, mDisplayShader{nullptr}, mComputeShader{nullptr}
      // Initialize camera at maze spawn position (will be updated after first chunk loads)
      ,
      mCamera{glm::vec3(0.0f, 50.0f, 200.0f), -90.0f, -10.0f, 65.0f, 0.1f, 500.0f}
{
    mPlayer.setActive(true);
    mWorld.init(); // This now initializes both 2D physics and 3D path tracer scene

    // Initialize player animator with character index 0
    mPlayer.initializeAnimator(0);

    // Get shaders from context
    auto &shaders = *context.shaders;
    try
    {
        mDisplayShader = &shaders.get(Shaders::ID::GLSL_FULLSCREEN_QUAD);
        mComputeShader = &shaders.get(Shaders::ID::GLSL_PATH_TRACER_COMPUTE);
        mShadersInitialized = true;
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Failed to get shaders from context: %s", e.what());
        mShadersInitialized = false;
    }

    auto &textures = *context.textures;
    try
    {
        mNoiseTexture = &textures.get(Textures::ID::NOISE2D);
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Failed to get noise texture: %s", e.what());
        mNoiseTexture = nullptr;
    }

    // Get and play game music from context
    auto &music = *context.music;
    try
    {
        log("GameState: Attempting to get music from manager...");
        mGameMusic = &music.get(Music::ID::GAME_MUSIC);
        if (mGameMusic)
        {

            // Start the music - the periodic health check in update() will handle restarts if needed
            bool wasPlaying = mGameMusic->isPlaying();
            log("GameState: Music isPlaying before play(): " + std::string(wasPlaying ? "true" : "false"));
            
            if (!wasPlaying)
            {
                log("GameState: Starting game music...");
                mGameMusic->play();
                
                // Check immediately after
                bool nowPlaying = mGameMusic->isPlaying();
                log("GameState: Music isPlaying after play(): " + std::string(nowPlaying ? "true" : "false"));
            }
            else
            {
                log("GameState: Music already playing - keeping it running");
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

    // Update camera to maze spawn position (position "0" in the maze)
    glm::vec3 spawnPos = mWorld.getMazeSpawnPosition();
    spawnPos.y += 50.0f; // Place camera above spawn
    spawnPos.z += 50.0f; // Move back a bit
    mCamera.setPosition(spawnPos);

    // Set initial player position
    mPlayer.setPosition(spawnPos);

    log("GameState: Camera spawned at:\t" +
        std::to_string(spawnPos.x) + ", " +
        std::to_string(spawnPos.y) + ", " +
        std::to_string(spawnPos.z));

    // Initialize camera tracking
    mLastCameraPosition = mCamera.getPosition();
    mLastCameraYaw = mCamera.getYaw();
    mLastCameraPitch = mCamera.getPitch();

    // Initialize GPU graphics pipeline following Compute.cpp approach
    if (mShadersInitialized)
    {
        initializeGraphicsResources();

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

        // Render player character in third-person mode AFTER path tracer
        // The billboard uses depth testing so it will appear in front
        renderPlayerCharacter();
    }

    // REMOVED: mWorld.draw() - World no longer handles rendering
    // All rendering is now done via compute shaders above
}

void GameState::initializeGraphicsResources() noexcept
{
    log("GameState: Initializing OpenGL 4.3 graphics pipeline...");

    // Enable OpenGL features using helper
    GLSDLHelper::enableRenderingFeatures();

    // Create VAO for fullscreen quad using helper
    mVAO = GLSDLHelper::createAndBindVAO();

    // Create SSBO for sphere data using helper
    mShapeSSBO = GLSDLHelper::createAndBindSSBO(1);

    updateRenderResolution();

    // Create textures for path tracing
    createPathTracerTextures();

    // Upload sphere data from World to GPU with extra capacity for dynamic spawning
    const auto &spheres = mWorld.getSpheres();

    // Allocate buffer with 4x capacity to handle chunk-based spawning
    // This reduces frequent reallocations as spheres are loaded/unloaded
    size_t initialCapacity = std::max(spheres.size() * 4, size_t(1000));
    size_t bufferSize = initialCapacity * sizeof(Sphere);

    GLSDLHelper::allocateSSBOBuffer(static_cast<GLsizeiptr>(bufferSize), nullptr);
    mShapeSSBOCapacityBytes = bufferSize;

    // Upload initial sphere data
    if (!spheres.empty())
    {
        GLSDLHelper::updateSSBOBuffer(0, static_cast<GLsizeiptr>(spheres.size() * sizeof(Sphere)), spheres.data());
    }
    log("GameState: Graphics pipeline initialization complete");
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
    if (mAccumTex != 0)
    {
        GLSDLHelper::deleteTexture(mAccumTex);
        mAccumTex = 0;
    }
    if (mDisplayTex != 0)
    {
        GLSDLHelper::deleteTexture(mDisplayTex);
        mDisplayTex = 0;
    }

    // Create accumulation texture for progressive rendering using helper
    mAccumTex = GLSDLHelper::createPathTracerTexture(
        static_cast<GLsizei>(mRenderWidth),
        static_cast<GLsizei>(mRenderHeight));

    // Create display texture for final output using helper
    mDisplayTex = GLSDLHelper::createPathTracerTexture(
        static_cast<GLsizei>(mRenderWidth),
        static_cast<GLsizei>(mRenderHeight));

    log("GameState: Path tracer textures created (window/internal):\t" +
        std::to_string(mWindowWidth) + "x" + std::to_string(mWindowHeight) + " / " +
        std::to_string(mRenderWidth) + "x" + std::to_string(mRenderHeight));
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
        // Camera moved - reset accumulation
        mCurrentBatch = 0;
        mLastCameraPosition = currentPos;
        mLastCameraYaw = currentYaw;
        mLastCameraPitch = currentPitch;
        return true;
    }
    return false;
}

void GameState::renderWithComputeShaders() const noexcept
{
    if (!mComputeShader || !mDisplayShader)
    {
        return;
    }

    // Check if camera has moved - if so, reset accumulation
    checkCameraMovement();

    // Animated starfield: reset progressive accumulation each frame to prevent ghost trails
    mCurrentBatch = 0;

    // Update sphere data on GPU every frame (physics may have changed positions)
    const auto &spheres = mWorld.getSpheres();

    // Safety check: ensure we have spheres to render
    if (spheres.empty())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "No spheres to render");
        return;
    }

    // Calculate required buffer size
    const std::size_t requiredSize = spheres.size() * sizeof(Sphere);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mShapeSSBO);

    // Check if we need to reallocate the buffer (sphere count changed)
    if (mShapeSSBOCapacityBytes < requiredSize)
    {
        // Reallocating buffer with new size (add some headroom to avoid frequent reallocations)
        const std::size_t newSize = requiredSize * 2;
        GLSDLHelper::allocateSSBOBuffer(static_cast<GLsizeiptr>(newSize), spheres.data());
        mShapeSSBOCapacityBytes = newSize;
    }
    else
    {
        // Update existing buffer
        GLSDLHelper::updateSSBOBuffer(0, static_cast<GLsizeiptr>(requiredSize), spheres.data());
    }

    // Only compute if we haven't finished all batches
    if (mCurrentBatch < mTotalBatches)
    {
        mComputeShader->bind();

        // Calculate aspect ratio
        float ar = static_cast<float>(mRenderWidth) / static_cast<float>(mRenderHeight);

        // Set camera uniforms (following Compute.cpp renderPathTracer)
        mComputeShader->setUniform("uCamera.eye", mCamera.getPosition());
        mComputeShader->setUniform("uCamera.far", mCamera.getFar());
        mComputeShader->setUniform("uCamera.ray00", mCamera.getFrustumEyeRay(ar, -1, -1));
        mComputeShader->setUniform("uCamera.ray01", mCamera.getFrustumEyeRay(ar, -1, 1));
        mComputeShader->setUniform("uCamera.ray10", mCamera.getFrustumEyeRay(ar, 1, -1));
        mComputeShader->setUniform("uCamera.ray11", mCamera.getFrustumEyeRay(ar, 1, 1));

        // Set batch uniforms for progressive rendering
        mComputeShader->setUniform("uBatch", mCurrentBatch);
        mComputeShader->setUniform("uSamplesPerBatch", mSamplesPerBatch);

        // Set sphere count uniform (NEW - tells shader how many spheres to check)
        mComputeShader->setUniform("uSphereCount", static_cast<uint32_t>(spheres.size()));

        // Set infinite reflective ground plane uniforms
        const Plane &groundPlane = mWorld.getGroundPlane();
        const Material &groundMaterial = groundPlane.getMaterial();
        mComputeShader->setUniform("uGroundPlanePoint", groundPlane.getPoint());
        mComputeShader->setUniform("uGroundPlaneNormal", groundPlane.getNormal());
        mComputeShader->setUniform("uGroundPlaneAlbedo", groundMaterial.getAlbedo());
        mComputeShader->setUniform("uGroundPlaneMaterialType", static_cast<GLuint>(groundMaterial.getType()));
        mComputeShader->setUniform("uGroundPlaneFuzz", groundMaterial.getFuzz());
        mComputeShader->setUniform("uGroundPlaneRefractiveIndex", groundMaterial.getRefractiveIndex());

        mComputeShader->setUniform("uTime", static_cast<GLfloat>(SDL_GetTicks()) / 1000.0f);
        mComputeShader->setUniform("uNoiseTex", static_cast<GLint>(2));

        // Bind both textures as images for compute shader
        glBindImageTexture(0, mAccumTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
        glBindImageTexture(1, mDisplayTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);

        // Bind starfield noise texture sampler
        glBindTexture(GL_TEXTURE_2D, mNoiseTexture ? mNoiseTexture->get() : 0);

        // Dispatch compute shader with work groups (using 20x20 local work group size)
        GLuint groupsX = (mRenderWidth + 19) / 20;
        GLuint groupsY = (mRenderHeight + 19) / 20;
        glDispatchCompute(groupsX, groupsY, 1);

        // Memory barrier to ensure compute shader writes are visible
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        mCurrentBatch++;
    }

    // Display the current result
    mDisplayShader->bind();
    mDisplayShader->setUniform("uTexture2D", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mDisplayTex);
    glBindVertexArray(mVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void GameState::cleanupResources() noexcept
{
    GLSDLHelper::deleteVAO(mVAO);
    GLSDLHelper::deleteBuffer(mShapeSSBO);
    GLSDLHelper::deleteTexture(mAccumTex);
    GLSDLHelper::deleteTexture(mDisplayTex);
    // Noise texture is now managed by TextureManager - don't delete it here
    mNoiseTexture = nullptr;

    // Shaders are now managed by ShaderManager - don't delete them here
    mDisplayShader = nullptr;
    mComputeShader = nullptr;

    log("GameState: OpenGL resources cleaned up");
}

void GameState::updateSounds() noexcept
{
    // Get sound player from context
    auto *sounds = getContext().sounds;
    if (!sounds)
    {
        return;
    }

    // Set listener position based on camera position
    // Convert 3D camera position to 2D for the sound system
    // Using camera X and Z as the 2D position (Y is up in 3D, but we use X/Z plane)
    glm::vec3 camPos = mCamera.getPosition();
    sounds->setListenerPosition(sf::Vector2f{camPos.x, camPos.z});

    // Remove sounds that have finished playing
    sounds->removeStoppedSounds();
}

bool GameState::update(float dt, unsigned int subSteps) noexcept
{
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

    // Update sphere chunks based on camera position for dynamic spawning
    mWorld.updateSphereChunks(mCamera.getPosition());

    // Update sounds: set listener position based on camera and remove stopped sounds
    updateSounds();

    // Handle camera input through Player (using action bindings)
    mPlayer.handleRealtimeInput(mCamera, dt);

    // Update player animation
    mPlayer.updateAnimation(dt);

    // Log progress periodically
    if (mCurrentBatch % 50 == 0 || mCurrentBatch == mTotalBatches)
    {
        uint32_t totalSamples = mCurrentBatch * mSamplesPerBatch;
        // stats
        // auto progress = static_cast<float>(mCurrentBatch) / static_cast<float>(mTotalBatches) * 100.0f;
        // log("Progress: " + std::to_string(totalSamples) + " samples (" +
        // std::to_string(mCurrentBatch) + "/" + std::to_string(mTotalBatches) + " batches)");
    }

    return true;
}

bool GameState::handleEvent(const SDL_Event &event) noexcept {
    // Handle window resize event
    if (event.type == SDL_EVENT_WINDOW_RESIZED) {
        int newW = event.window.data1;
        int newH = event.window.data2;
        if (newW > 0 && newH > 0 && (newW != mWindowWidth || newH != mWindowHeight)) {
            mWindowWidth = newW;
            mWindowHeight = newH;
            // Update OpenGL viewport to match new window size
            glViewport(0, 0, mWindowWidth, mWindowHeight);
            // Update internal rendering resolution for path tracer
            updateRenderResolution();
            // Recreate path tracer textures
            createPathTracerTextures();
            // Reset accumulation for new size
            mCurrentBatch = 0;
            log("GameState: Window resized, path tracer textures recreated (window/internal): " +
                std::to_string(mWindowWidth) + "x" + std::to_string(mWindowHeight) + " / " +
                std::to_string(mRenderWidth) + "x" + std::to_string(mRenderHeight));
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
            requestStackPush(States::ID::PAUSE);
        }

        // Reset accumulation with SPACE (handled separately since it's not a camera action)
        if (event.key.scancode == SDL_SCANCODE_SPACE)
        {
            mCurrentBatch = 0;

            // Play non-spatialized UI sound
            if (auto *sounds = getContext().sounds)
            {
                sounds->play(SoundEffect::ID::SELECT);
            }
            log("Path tracing accumulation reset");
        }

        // Play sound when camera is reset (R key handled by Player now)
        if (event.key.scancode == SDL_SCANCODE_R)
        {
            glm::vec3 resetPos = glm::vec3(0.0f, 50.0f, 200.0f);
            if (auto *sounds = getContext().sounds)
            {
                sounds->play(SoundEffect::ID::GENERATE, sf::Vector2f{resetPos.x, resetPos.z});
            }
            log("Camera reset to initial position");
        }
    }

    // Handle mouse motion for camera rotation (right mouse button)
    if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        Uint32 mouseState = SDL_GetMouseState(nullptr, nullptr);
        if (mouseState & SDL_BUTTON_RMASK)
        {
            const float sensitivity = 0.35f;
            mCamera.rotate(event.motion.xrel * sensitivity, -event.motion.yrel * sensitivity);
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

    // The path tracer draws a fullscreen 2D quad which doesn't use depth properly.
    // Clear the depth buffer so our 3D billboard can render.
    glClear(GL_DEPTH_BUFFER_BIT);

    // Reset depth function to default
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    // Also ensure we have proper 3D projection set up
    glEnable(GL_DEPTH_TEST);

    // Delegate to World for actual rendering
    mWorld.renderPlayerCharacter(mPlayer, mCamera);
}
