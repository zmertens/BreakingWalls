#include "SplashState.hpp"

#include <SDL3/SDL.h>

#include <cmath>

#include <glm/glm.hpp>
#include <glad/glad.h>

#include "LoadingState.hpp"
#include "Player.hpp"
#include "ResourceIdentifiers.hpp"
#include "ResourceManager.hpp"
#include "StateStack.hpp"
#include "Sphere.hpp"

SplashState::SplashState(StateStack& stack, Context context)
    : State(stack, context)
    , mWorld{ *context.window, *context.fonts, *context.textures }
    // Start camera at a pleasant vantage point looking at the scene
    , mCamera{ glm::vec3(0.0f, 30.0f, 100.0f), -90.0f, -5.0f, 65.0f, 0.1f, 500.0f }
{
    getContext().player->setActive(false);

    // Initialize the world for sphere scene
    mWorld.init();

    // Get shaders from context
    auto& shaders = *context.shaders;
    mDisplayShader = &shaders.get(Shaders::ID::DISPLAY_QUAD_VERTEX);
    mComputeShader = &shaders.get(Shaders::ID::COMPUTE_PATH_TRACER_COMPUTE);
    mShadersInitialized = true;

    // Trigger initial chunk load
    mWorld.updateSphereChunks(mCamera.getPosition());

    // Position camera at a nice vantage point above the spawn
    glm::vec3 spawnPos = mWorld.getMazeSpawnPosition();
    spawnPos.y += 40.0f;  // Higher up for a sweeping view
    spawnPos.z += 80.0f;  // Back for a wider perspective
    mCamera.setPosition(spawnPos);

    log("SplashState: Camera positioned at (%.1f, %.1f, %.1f)",
        spawnPos.x, spawnPos.y, spawnPos.z);

    // Initialize camera tracking
    mLastCameraPosition = mCamera.getPosition();
    mLastCameraYaw = mCamera.getYaw();
    mLastCameraPitch = mCamera.getPitch();

    // Initialize GPU resources
    if (mShadersInitialized)
    {
        initializeGraphicsResources();
    }
}

SplashState::~SplashState()
{
    cleanupResources();
}

void SplashState::draw() const noexcept
{
    if (mShadersInitialized)
    {
        renderWithComputeShaders();
    }

    // Draw the world (physics objects, sprites, etc.)
    mWorld.draw();
}

void SplashState::initializeGraphicsResources() noexcept
{
    log("SplashState: Initializing OpenGL graphics pipeline...");

    glEnable(GL_MULTISAMPLE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glGenVertexArrays(1, &mVAO);
    glBindVertexArray(mVAO);

    glGenBuffers(1, &mShapeSSBO);

    createPathTracerTextures();

    // Upload sphere data from World to GPU
    const auto& spheres = mWorld.getSpheres();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mShapeSSBO);

    size_t initialCapacity = std::max(spheres.size() * 4, size_t(1000));
    size_t bufferSize = initialCapacity * sizeof(Sphere);

    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, nullptr, GL_DYNAMIC_DRAW);

    if (!spheres.empty())
    {
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, spheres.size() * sizeof(Sphere), spheres.data());
    }

    log("SplashState: Uploaded %zu spheres to GPU", spheres.size());
    log("SplashState: Graphics pipeline initialization complete");
}

void SplashState::createPathTracerTextures() noexcept
{
    glGenTextures(1, &mAccumTex);
    glBindTexture(GL_TEXTURE_2D, mAccumTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F,
        static_cast<GLsizei>(mWindowWidth),
        static_cast<GLsizei>(mWindowHeight));

    glGenTextures(1, &mDisplayTex);
    glBindTexture(GL_TEXTURE_2D, mDisplayTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F,
        static_cast<GLsizei>(mWindowWidth),
        static_cast<GLsizei>(mWindowHeight));

    log("SplashState: Path tracer textures created (%dx%d)", mWindowWidth, mWindowHeight);
}

bool SplashState::checkCameraMovement() const noexcept
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
        mCurrentBatch = 0;
        mLastCameraPosition = currentPos;
        mLastCameraYaw = currentYaw;
        mLastCameraPitch = currentPitch;
        return true;
    }
    return false;
}

void SplashState::renderWithComputeShaders() const noexcept
{
    if (!mComputeShader || !mDisplayShader)
    {
        return;
    }

    checkCameraMovement();

    const auto& spheres = mWorld.getSpheres();

    if (spheres.empty())
    {
        return;
    }

    size_t requiredSize = spheres.size() * sizeof(Sphere);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mShapeSSBO);

    GLint currentBufferSize = 0;
    glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &currentBufferSize);

    if (static_cast<size_t>(currentBufferSize) < requiredSize)
    {
        size_t newSize = requiredSize * 2;
        glBufferData(GL_SHADER_STORAGE_BUFFER, newSize, spheres.data(), GL_DYNAMIC_DRAW);
    }
    else
    {
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, requiredSize, spheres.data());
    }

    if (mCurrentBatch < mTotalBatches)
    {
        mComputeShader->bind();

        float ar = static_cast<float>(mWindowWidth) / static_cast<float>(mWindowHeight);

        mComputeShader->setUniform("uCamera.eye", mCamera.getPosition());
        mComputeShader->setUniform("uCamera.far", mCamera.getFar());
        mComputeShader->setUniform("uCamera.ray00", mCamera.getFrustumEyeRay(ar, -1, -1));
        mComputeShader->setUniform("uCamera.ray01", mCamera.getFrustumEyeRay(ar, -1, 1));
        mComputeShader->setUniform("uCamera.ray10", mCamera.getFrustumEyeRay(ar, 1, -1));
        mComputeShader->setUniform("uCamera.ray11", mCamera.getFrustumEyeRay(ar, 1, 1));

        mComputeShader->setUniform("uBatch", mCurrentBatch);
        mComputeShader->setUniform("uSamplesPerBatch", mSamplesPerBatch);
        mComputeShader->setUniform("uSphereCount", static_cast<uint32_t>(spheres.size()));

        glBindImageTexture(0, mAccumTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
        glBindImageTexture(1, mDisplayTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);

        GLuint groupsX = (mWindowWidth + 19) / 20;
        GLuint groupsY = (mWindowHeight + 19) / 20;
        glDispatchCompute(groupsX, groupsY, 1);

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        mCurrentBatch++;
    }

    mDisplayShader->bind();
    mDisplayShader->setUniform("uTexture2D", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mDisplayTex);
    glBindVertexArray(mVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void SplashState::cleanupResources() noexcept
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

    mDisplayShader = nullptr;
    mComputeShader = nullptr;

    log("SplashState: OpenGL resources cleaned up");
}

bool SplashState::update(float dt, unsigned int subSteps) noexcept
{
    // Update the world physics (spheres move gently)
    mWorld.update(dt);

    // Accumulate time for gentle floating motion
    mFloatTime += dt;

    // Gentle forward floating with subtle oscillation for a calm effect
    glm::vec3 currentPos = mCamera.getPosition();

    // Slow forward drift
    glm::vec3 forward = mCamera.getTarget();
    currentPos += forward * mFloatSpeed * dt;

    // Gentle vertical oscillation (breathing effect)
    float verticalOscillation = std::sin(mFloatTime * 0.5f) * 2.0f * dt;
    currentPos.y += verticalOscillation;

    // Subtle horizontal sway
    float horizontalSway = std::sin(mFloatTime * 0.3f) * 1.0f * dt;
    currentPos.x += horizontalSway;

    mCamera.setPosition(currentPos);

    // Update sphere chunks as camera moves forward
    mWorld.updateSphereChunks(mCamera.getPosition());

    return true;
}

bool SplashState::handleEvent(const SDL_Event& event) noexcept
{
    // Any key press or mouse click transitions to menu
    if (event.type == SDL_EVENT_KEY_DOWN ||
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        // Only allow transition if loading is complete
        if (!isLoadingComplete())
        {
            return true;
        }

        requestStateClear();
        requestStackPush(States::ID::MENU);
    }

    return true;
}

bool SplashState::isLoadingComplete() const noexcept
{
    // Check if the state below us (LoadingState) is finished
    if (const auto* loadingState = getStack().peekState<LoadingState*>())
    {
        return loadingState->isFinished();
    }

    // If there's no LoadingState below, assume loading is complete
    return true;
}
