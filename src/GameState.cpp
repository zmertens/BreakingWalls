#include "GameState.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <functional>

#include <glm/glm.hpp>
#include <glad/glad.h>

#include "CommandQueue.hpp"
#include "GLSDLHelper.hpp"
#include "MusicPlayer.hpp"
#include "Player.hpp"
#include "ResourceManager.hpp"
#include "Shader.hpp"
#include "SoundPlayer.hpp"
#include "StateStack.hpp"
#include "Sphere.hpp"

GameState::GameState(StateStack& stack, Context context)
    : State{ stack, context }
    , mWorld{ *context.window, *context.fonts, *context.textures }
    , mPlayer{ *context.player }
    , mGameMusic{ nullptr }
    , mDisplayShader{ nullptr }
    , mComputeShader{ nullptr }
    // Initialize camera at maze spawn position (will be updated after first chunk loads)
    , mCamera{ glm::vec3(0.0f, 50.0f, 200.0f), -90.0f, -10.0f, 65.0f, 0.1f, 500.0f }
{
    mPlayer.setActive(true);
    mWorld.init();  // This now initializes both 2D physics and 3D path tracer scene
    mWorld.setPlayer(context.player);

    // Get shaders from context
    auto& shaders = *context.shaders;
    try
    {
        mDisplayShader = &shaders.get(Shaders::ID::DISPLAY_QUAD_VERTEX);
        mComputeShader = &shaders.get(Shaders::ID::COMPUTE_PATH_TRACER_COMPUTE);
        mShadersInitialized = true;
    } catch (const std::exception& e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Failed to get shaders from context: %s", e.what());
        mShadersInitialized = false;
    }

    // Get and play game music from context
    auto& music = *context.music;
    try
    {
        mGameMusic = &music.get(Music::ID::GAME_MUSIC);
        mGameMusic->play();
    } catch (const std::exception& e)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "GameState: Failed to get game music: %s", e.what());
        mGameMusic = nullptr;
    }

    // Trigger initial chunk load to get maze spawn position
    mWorld.updateSphereChunks(mCamera.getPosition());

    // Update camera to maze spawn position (position "0" in the maze)
    glm::vec3 spawnPos = mWorld.getMazeSpawnPosition();
    spawnPos.y += 50.0f;  // Place camera above spawn
    spawnPos.z += 50.0f;  // Move back a bit
    mCamera.setPosition(spawnPos);
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

    SDL_Log("%s\n", view().data());
}

void GameState::draw() const noexcept
{
    if (mShadersInitialized)
    {
        // Use compute shader rendering pipeline
        renderWithComputeShaders();
    }

    // Always draw the world (physics objects, sprites, etc.)
    mWorld.draw();
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

    // Create textures for path tracing
    createPathTracerTextures();

    // Upload sphere data from World to GPU with extra capacity for dynamic spawning
    const auto& spheres = mWorld.getSpheres();

    // Allocate buffer with 4x capacity to handle chunk-based spawning
    // This reduces frequent reallocations as spheres are loaded/unloaded
    size_t initialCapacity = std::max(spheres.size() * 4, size_t(1000));
    size_t bufferSize = initialCapacity * sizeof(Sphere);

    GLSDLHelper::allocateSSBOBuffer(static_cast<GLsizeiptr>(bufferSize), nullptr);

    // Upload initial sphere data
    if (!spheres.empty())
    {
        GLSDLHelper::updateSSBOBuffer(0, static_cast<GLsizeiptr>(spheres.size() * sizeof(Sphere)), spheres.data());
    }
    log("GameState: Graphics pipeline initialization complete");
}

void GameState::createPathTracerTextures() noexcept
{
    // Create accumulation texture for progressive rendering using helper
    mAccumTex = GLSDLHelper::createPathTracerTexture(
        static_cast<GLsizei>(mWindowWidth),
        static_cast<GLsizei>(mWindowHeight));

    // Create display texture for final output using helper
    mDisplayTex = GLSDLHelper::createPathTracerTexture(
        static_cast<GLsizei>(mWindowWidth),
        static_cast<GLsizei>(mWindowHeight));

    log("GameState: Path tracer textures created:\t" +
        std::to_string(mWindowWidth) + ", " + std::to_string(mWindowHeight));
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

    // Update sphere data on GPU every frame (physics may have changed positions)
    const auto& spheres = mWorld.getSpheres();

    // Safety check: ensure we have spheres to render
    if (spheres.empty())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "No spheres to render");
        return;
    }

    // Calculate required buffer size
    size_t requiredSize = spheres.size() * sizeof(Sphere);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mShapeSSBO);

    // Check if we need to reallocate the buffer (sphere count changed)
    GLint currentBufferSize = 0;
    glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &currentBufferSize);

    if (static_cast<size_t>(currentBufferSize) < requiredSize)
    {
        // Reallocate buffer with new size (add some headroom to avoid frequent reallocations)
        size_t newSize = requiredSize * 2;
        GLSDLHelper::allocateSSBOBuffer(static_cast<GLsizeiptr>(newSize), spheres.data());
    } else
    {
        // Update existing buffer
        GLSDLHelper::updateSSBOBuffer(0, static_cast<GLsizeiptr>(requiredSize), spheres.data());
    }

    // Only compute if we haven't finished all batches
    if (mCurrentBatch < mTotalBatches)
    {
        mComputeShader->bind();

        // Calculate aspect ratio
        float ar = static_cast<float>(mWindowWidth) / static_cast<float>(mWindowHeight);

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

        // Bind both textures as images for compute shader
        glBindImageTexture(0, mAccumTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
        glBindImageTexture(1, mDisplayTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);

        // Dispatch compute shader with work groups (using 20x20 local work group size)
        GLuint groupsX = (mWindowWidth + 19) / 20;
        GLuint groupsY = (mWindowHeight + 19) / 20;
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

    // Shaders are now managed by ShaderManager - don't delete them here
    mDisplayShader = nullptr;
    mComputeShader = nullptr;

    log("GameState: OpenGL resources cleaned up");
}

void GameState::updateSounds() noexcept
{
    // Get sound player from context
    auto* sounds = getContext().sounds;
    if (!sounds)
    {
        return;
    }

    // Set listener position based on camera position
    // Convert 3D camera position to 2D for the sound system
    // Using camera X and Z as the 2D position (Y is up in 3D, but we use X/Z plane)
    glm::vec3 camPos = mCamera.getPosition();
    sounds->setListenerPosition(sf::Vector2f{ camPos.x, camPos.z });

    // Remove sounds that have finished playing
    sounds->removeStoppedSounds();
}

bool GameState::update(float dt, unsigned int subSteps) noexcept
{
    mWorld.update(dt);

    auto& commands = mWorld.getCommandQueue();
    mPlayer.handleRealtimeInput(std::ref(commands));

    // Update sphere chunks based on camera position for dynamic spawning
    mWorld.updateSphereChunks(mCamera.getPosition());

    // Update sounds: set listener position based on camera and remove stopped sounds
    updateSounds();

    // Handle camera movement with WASD for 3D path tracer scene
    int numKeys = 0;
    const auto* keyState = SDL_GetKeyboardState(&numKeys);

    if (keyState)
    {
        const float cameraMoveSpeed = 50.0f * dt;  // 50 units per second
        glm::vec3 movement(0.0f);

        // WASD controls camera position
        if (keyState[SDL_SCANCODE_W])
        {
            movement += mCamera.getTarget() * cameraMoveSpeed;  // Forward
        }
        if (keyState[SDL_SCANCODE_S])
        {
            movement -= mCamera.getTarget() * cameraMoveSpeed;  // Backward
        }
        if (keyState[SDL_SCANCODE_A])
        {
            movement -= mCamera.getRight() * cameraMoveSpeed;   // Left
        }
        if (keyState[SDL_SCANCODE_D])
        {
            movement += mCamera.getRight() * cameraMoveSpeed;   // Right
        }

        // Q/E for vertical movement
        if (keyState[SDL_SCANCODE_Q])
        {
            movement.y += cameraMoveSpeed;  // Up
        }
        if (keyState[SDL_SCANCODE_E])
        {
            movement.y -= cameraMoveSpeed;  // Down
        }

        // Apply movement if any key was pressed
        if (glm::length(movement) > 0.001f)
        {
            glm::vec3 newPos = mCamera.getPosition() + movement;
            mCamera.setPosition(newPos);
        }

        // Arrow keys for camera rotation - INCREASED SPEED
        const float rotateSpeed = 180.0f * dt;  // 180 degrees per second (doubled from 90)
        float yawDelta = 0.0f;
        float pitchDelta = 0.0f;

        if (keyState[SDL_SCANCODE_LEFT])
        {
            yawDelta -= rotateSpeed;
        }
        if (keyState[SDL_SCANCODE_RIGHT])
        {
            yawDelta += rotateSpeed;
        }
        if (keyState[SDL_SCANCODE_UP])
        {
            pitchDelta += rotateSpeed;
        }
        if (keyState[SDL_SCANCODE_DOWN])
        {
            pitchDelta -= rotateSpeed;
        }

        if (std::abs(yawDelta) > 0.001f || std::abs(pitchDelta) > 0.001f)
        {
            mCamera.rotate(yawDelta, pitchDelta);
        }
    }

    // Log progress periodically
    if (mCurrentBatch % 50 == 0 || mCurrentBatch == mTotalBatches) {
        uint32_t totalSamples = mCurrentBatch * mSamplesPerBatch;
        float progress = static_cast<float>(mCurrentBatch) / static_cast<float>(mTotalBatches) * 100.0f;
        log("Path tracing progress: " + std::to_string(progress) + " | " +
            std::to_string(mCurrentBatch) + " / " + std::to_string(mTotalBatches) + " batches | " +
            std::to_string(totalSamples) + " samples.");
    }

    return true;
}

bool GameState::handleEvent(const SDL_Event& event) noexcept
{
    auto& commands = mWorld.getCommandQueue();

    // Let player handle events for 2D physics (if needed)
    mPlayer.handleEvent(event, std::ref(commands));
    mWorld.handleEvent(event);

    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        if (event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            requestStackPush(States::ID::PAUSE);
        }

        // Reset camera to initial position with R key
        if (event.key.scancode == SDL_SCANCODE_R)
        {
            glm::vec3 resetPos = glm::vec3(0.0f, 50.0f, 200.0f);
            mCamera.setPosition(resetPos);
            mCamera.rotate(0.0f, 0.0f);  // Reset to default angles

            // Play spatialized sound at the reset position
            if (auto* sounds = getContext().sounds)
            {
                sounds->play(SoundEffect::ID::GENERATE, sf::Vector2f{ resetPos.x, resetPos.z });
            }
            log("Camera reset to initial position");
        }

        // Toggle progressive rendering reset with SPACE
        if (event.key.scancode == SDL_SCANCODE_SPACE)
        {
            mCurrentBatch = 0;

            // Play non-spatialized UI sound (at listener position)
            if (auto* sounds = getContext().sounds)
            {
                sounds->play(SoundEffect::ID::SELECT);
            }
            log("Path tracing accumulation reset");
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
