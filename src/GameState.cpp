#include "GameState.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <functional>

#include <glm/glm.hpp>
#include <glad/glad.h>

#include "CommandQueue.hpp"
#include "Player.hpp"
#include "StateStack.hpp"
#include "GLUtils.hpp"
#include "Sphere.hpp"

GameState::GameState(StateStack& stack, Context context)
    : State{stack, context}
      , mWorld{*context.window, *context.fonts, *context.textures}
      , mPlayer{*context.player}
      , mDisplayShader{nullptr}
      , mComputeShader{nullptr}
      // Initialize camera matching Compute.cpp: above and in front of sphere circle
      , mCamera{glm::vec3(0.0f, 50.0f, 200.0f), -90.0f, -10.0f, 65.0f, 0.1f, 500.0f}
{
    mPlayer.setActive(true);
    mWorld.init();  // This now initializes both 2D physics and 3D path tracer scene
    mWorld.setPlayer(context.player);
    
    // Initialize camera tracking
    mLastCameraPosition = mCamera.getPosition();
    mLastCameraYaw = mCamera.getYaw();
    mLastCameraPitch = mCamera.getPitch();
    
    // Initialize GPU graphics pipeline following Compute.cpp approach
    initializeGraphicsResources();
}

GameState::~GameState()
{
    cleanupResources();
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
    SDL_Log("GameState: Initializing OpenGL 4.3 graphics pipeline...");
    
    // Enable OpenGL features (following Compute.cpp)
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    
#if defined(BREAKING_WALLS_DEBUG)
    // Register debug callback if available (OpenGL 4.3+)
    glDebugMessageCallback(GLUtils::GlDebugCallback, nullptr);
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    SDL_Log("GameState: OpenGL Debug Output enabled");
#endif
    
    // Initialize shaders
    if (initializeShaders())
    {
        // Create VAO for fullscreen quad (like Compute.cpp)
        glGenVertexArrays(1, &mVAO);
        glBindVertexArray(mVAO);
        
        // Create SSBO for sphere data
        glGenBuffers(1, &mShapeSSBO);
        
        // Create textures for path tracing
        createPathTracerTextures();
        
        // Upload sphere data from World to GPU
        const auto& spheres = mWorld.getSpheres();
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mShapeSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, spheres.size() * sizeof(Sphere), spheres.data(), GL_DYNAMIC_DRAW);
        SDL_Log("GameState: Uploaded %zu spheres to GPU", spheres.size());
        
        mShadersInitialized = true;
        SDL_Log("GameState: Shaders and OpenGL resources initialized successfully");
    }
    else
    {
        SDL_Log("GameState: Shader initialization failed, using fallback rendering");
        mShadersInitialized = false;
    }
    
    SDL_Log("GameState: Graphics pipeline initialization complete");
}

bool GameState::initializeShaders() noexcept
{
    try
    {
        // Create display shader (vertex + fragment) following Compute.cpp
        mDisplayShader = std::make_unique<Shader>();
        mDisplayShader->compileAndAttachShader(ShaderType::VERTEX, "./shaders/raytracer.vert.glsl");
        mDisplayShader->compileAndAttachShader(ShaderType::FRAGMENT, "./shaders/raytracer.frag.glsl");
        mDisplayShader->linkProgram();
        
        SDL_Log("GameState: Display shader compiled and linked");
        SDL_Log("%s", mDisplayShader->getGlslUniforms().c_str());
        SDL_Log("%s", mDisplayShader->getGlslAttribs().c_str());
        
        // Create compute shader for path tracing (following Compute.cpp)
        mComputeShader = std::make_unique<Shader>();
        mComputeShader->compileAndAttachShader(ShaderType::COMPUTE, "./shaders/pathtracer.cs.glsl");
        mComputeShader->linkProgram();
        
        SDL_Log("GameState: Compute shader compiled and linked");
        SDL_Log("%s", mComputeShader->getGlslUniforms().c_str());
        
        return true;
    }
    catch (const std::exception& e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Shader initialization failed: %s", e.what());
        mDisplayShader.reset();
        mComputeShader.reset();
        return false;
    }
}

void GameState::createPathTracerTextures() noexcept
{
    // Create accumulation texture for progressive rendering (following Compute.cpp)
    glGenTextures(1, &mAccumTex);
    glBindTexture(GL_TEXTURE_2D, mAccumTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F,
                   static_cast<GLsizei>(mWindowWidth),
                   static_cast<GLsizei>(mWindowHeight));

    // Create display texture for final output
    glGenTextures(1, &mDisplayTex);
    glBindTexture(GL_TEXTURE_2D, mDisplayTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F,
                   static_cast<GLsizei>(mWindowWidth),
                   static_cast<GLsizei>(mWindowHeight));

    SDL_Log("GameState: Path tracer textures created (%dx%d)", mWindowWidth, mWindowHeight);
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
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mShapeSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, spheres.size() * sizeof(Sphere), spheres.data());
    
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
        
        // Log progress periodically
        if (mCurrentBatch % 50 == 0 || mCurrentBatch == mTotalBatches) {
            uint32_t totalSamples = mCurrentBatch * mSamplesPerBatch;
            float progress = static_cast<float>(mCurrentBatch) / static_cast<float>(mTotalBatches) * 100.0f;
            SDL_Log("Path tracing progress: %.1f%% (%u/%u batches, %u samples)",
                    progress, mCurrentBatch, mTotalBatches, totalSamples);
        }
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
    if (mVAO != 0)
    {
        glDeleteVertexArrays(1, &mVAO);
        mVAO = 0;
    }
    
    if (mShapeSSBO != 0)
    {
        glDeleteBuffers(1, &mShapeSSBO);
        mShapeSSBO = 0;
    }
    
    if (mAccumTex != 0)
    {
        glDeleteTextures(1, &mAccumTex);
        mAccumTex = 0;
    }
    
    if (mDisplayTex != 0)
    {
        glDeleteTextures(1, &mDisplayTex);
        mDisplayTex = 0;
    }
    
    mDisplayShader.reset();
    mComputeShader.reset();
    
    SDL_Log("GameState: OpenGL resources cleaned up");
}

bool GameState::update(float dt, unsigned int subSteps) noexcept
{
    mWorld.update(dt);

    auto& commands = mWorld.getCommandQueue();
    mPlayer.handleRealtimeInput(std::ref(commands));

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
        
        // Arrow keys for camera rotation
        const float rotateSpeed = 90.0f * dt;  // 90 degrees per second
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
            mCamera.setPosition(glm::vec3(0.0f, 50.0f, 200.0f));
            mCamera.rotate(0.0f, 0.0f);  // Reset to default angles
            SDL_Log("Camera reset to initial position");
        }
        
        // Toggle progressive rendering reset with SPACE
        if (event.key.scancode == SDL_SCANCODE_SPACE)
        {
            mCurrentBatch = 0;
            SDL_Log("Path tracing accumulation reset");
        }
    }
    
    // Handle mouse motion for camera rotation (right mouse button)
    if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        Uint32 mouseState = SDL_GetMouseState(nullptr, nullptr);
        if (mouseState & SDL_BUTTON_RMASK)
        {
            const float sensitivity = 0.1f;
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
