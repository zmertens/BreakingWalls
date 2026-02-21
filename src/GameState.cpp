#include "GameState.hpp"

#include <SDL3/SDL.h>

#include <dearimgui/imgui.h>

#include <cmath>
#include <random>
#include <vector>
#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glad/glad.h>

#include "GLTFModel.hpp"
#include "GLSDLHelper.hpp"
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
    constexpr std::size_t kMaxPathTracerSpheres = 200;
    constexpr std::size_t kMaxPathTracerTriangles = 192;
    constexpr bool kEnableTrianglePathTraceProxy = true;
    constexpr float kPlayerProxyRadius = 1.35f;
    constexpr float kPlayerShadowCenterYOffset = 1.35f;
    constexpr float kCharacterRasterScale = 1.42f;
    constexpr float kCharacterPathTraceProxyScale = 1.42f;
    constexpr float kCharacterModelYOffset = 1.0f;

    constexpr const char *kSkinnedModelVertexShader = R"GLSL(
#version 430 core

layout (location = 0) in vec3 Position;
layout (location = 1) in vec2 TexCoord;
layout (location = 2) in vec3 Normal;
layout (location = 3) in ivec4 BoneIDs;
layout (location = 4) in vec4 BoneWeights;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uBones[200];
uniform uint uBoneCount;

out vec3 vWorldPos;
out vec3 vWorldNormal;

void main()
{
    mat4 skin = mat4(0.0);
    float totalWeight = 0.0;

    for (int i = 0; i < 4; ++i)
    {
        int boneId = BoneIDs[i];
        float weight = BoneWeights[i];
        if (weight <= 0.0)
        {
            continue;
        }

        if (boneId >= 0 && uint(boneId) < uBoneCount)
        {
            skin += uBones[boneId] * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight <= 1e-6)
    {
        skin = mat4(1.0);
    }
    else
    {
        skin *= (1.0 / totalWeight);
    }

    vec4 localPos = skin * vec4(Position, 1.0);
    vec3 localNormal = normalize((skin * vec4(Normal, 0.0)).xyz);

    vec4 worldPos = uModel * localPos;
    mat3 normalMat = mat3(transpose(inverse(uModel)));
    vWorldNormal = normalize(normalMat * localNormal);
    vWorldPos = worldPos.xyz;

    gl_Position = uProjection * uView * worldPos;
}
)GLSL";

    constexpr const char *kSkinnedModelFragmentShader = R"GLSL(
#version 430 core

in vec3 vWorldPos;
in vec3 vWorldNormal;

out vec4 FragColor;

uniform vec3 uSunDir;

void main()
{
    vec3 N = normalize(vWorldNormal);
    vec3 L = normalize(-uSunDir);
    float ndotl = max(dot(N, L), 0.0);

    float region = 0.5 + 0.5 * sin(vWorldPos.y * 2.4 + vWorldPos.x * 0.9);
    float grain = 0.5 + 0.5 * sin(vWorldPos.x * 8.7 + vWorldPos.z * 6.3 + vWorldPos.y * 3.1);

    vec3 base = vec3(0.34, 0.24, 0.17);
    vec3 shadowTint = vec3(0.12, 0.08, 0.06);
    vec3 litTint = vec3(0.68, 0.50, 0.33);

    vec3 leatherTone = vec3(0.42, 0.27, 0.16);
    vec3 clothTone = vec3(0.28, 0.20, 0.15);
    vec3 skinTone = vec3(0.52, 0.34, 0.24);

    vec3 detailTone = mix(clothTone, leatherTone, region);
    detailTone = mix(detailTone, skinTone, clamp((vWorldPos.y - 2.0) * 0.35, 0.0, 1.0));
    base = mix(base, detailTone, 0.45 + 0.18 * grain);

    vec3 color = base * mix(shadowTint, litTint, ndotl);

    color.r *= 0.96;
    color.g *= 0.88;
    color.b *= 0.76;

    float fresnel = pow(1.0 - max(dot(N, vec3(0.0, 1.0, 0.0)), 0.0), 2.0);
    color += vec3(0.12, 0.07, 0.04) * fresnel;

    color *= 0.86;

    FragColor = vec4(color, 1.0);
}
)GLSL";

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
}

GameState::GameState(StateStack &stack, Context context)
    : State{stack, context}, mWorld{*context.window, *context.fonts, *context.textures, *context.shaders}, mPlayer{*context.player}, mGameMusic{nullptr}, mDisplayShader{nullptr}, mComputeShader{nullptr}, mCompositeShader{nullptr}, mSkinnedModelShader{nullptr}
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

    if (mShadersInitialized)
    {
        try
        {
            mCompositeShader = &shaders.get(Shaders::ID::GLSL_COMPOSITE_SCENE);
        }
        catch (const std::exception &e)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "GameState: Composite shader unavailable: %s", e.what());
            mCompositeShader = nullptr;
        }
    }

    auto &textures = *context.textures;
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

    log("GameState: Camera spawned at:\t" +
        std::to_string(cameraSpawn.x) + ", " +
        std::to_string(cameraSpawn.y) + ", " +
        std::to_string(cameraSpawn.z));

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
        initializeSkinnedModelShader();
        initializeWalkParticles();

        // Initialize billboard rendering for character sprites
        GLSDLHelper::initializeBillboardRendering();
    }
}

void GameState::initializeSkinnedModelShader() noexcept
{
    try
    {
        mSkinnedModelShader = std::make_unique<Shader>();
        mSkinnedModelShader->compileAndAttachShader(Shader::ShaderType::VERTEX, "skinned_model.vs", kSkinnedModelVertexShader);
        mSkinnedModelShader->compileAndAttachShader(Shader::ShaderType::FRAGMENT, "skinned_model.fs", kSkinnedModelFragmentShader);
        mSkinnedModelShader->linkProgram();
    }
    catch (const std::exception &e)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "GameState: Failed to initialize skinned model shader: %s", e.what());
        mSkinnedModelShader.reset();
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

        // Keep rasterized character overlay for readability even when triangle proxy is enabled.
        renderPlayerCharacter();

        if (mCompositeShader && mBillboardFBO != 0 && mBillboardColorTex != 0)
        {
            renderCompositeScene();
        }
    }

    // REMOVED: mWorld.draw() - World no longer handles rendering
    // All rendering is now done via compute shaders above

    drawRunnerHud();
}

void GameState::initializeGraphicsResources() noexcept
{
    log("GameState: Initializing OpenGL 4.3 graphics pipeline...");

    if (auto *window = getContext().window; window != nullptr)
    {
        if (SDL_Window *sdlWindow = window->getSDLWindow(); sdlWindow != nullptr)
        {
            int width = 0;
            int height = 0;
            SDL_GetWindowSize(sdlWindow, &width, &height);
            if (width > 0 && height > 0)
            {
                mWindowWidth = width;
                mWindowHeight = height;
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

    log("GameState: Path tracer textures recreated (window/internal):\t" +
        std::to_string(mWindowWidth) + "x" + std::to_string(mWindowHeight) + " / " +
        std::to_string(mRenderWidth) + "x" + std::to_string(mRenderHeight) +
        " (IDs: " + std::to_string(mAccumTex->get()) + ", " + std::to_string(mDisplayTex->get()) + ")");
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

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void GameState::initializeWalkParticles() noexcept
{
    if (mWalkParticlesInitialized)
    {
        return;
    }

    try
    {
        mWalkParticlesComputeShader = &getContext().shaders->get(Shaders::ID::GLSL_PARTICLES_COMPUTE);
        mWalkParticlesRenderShader = &getContext().shaders->get(Shaders::ID::GLSL_FULLSCREEN_QUAD_MVP);
    }
    catch (const std::exception &e)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "GameState: Walk particles unavailable: %s", e.what());
        mWalkParticlesComputeShader = nullptr;
        mWalkParticlesRenderShader = nullptr;
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

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mWalkParticlesPosSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mWalkParticlesVelSSBO);

    mWalkParticlesComputeShader->bind();
    mWalkParticlesComputeShader->setUniform("BlackHolePos1", attractor1);
    mWalkParticlesComputeShader->setUniform("BlackHolePos2", attractor2);
    mWalkParticlesComputeShader->setUniform("Gravity1", 210.0f);
    mWalkParticlesComputeShader->setUniform("Gravity2", 210.0f);
    mWalkParticlesComputeShader->setUniform("ParticleInvMass", 1.0f / 0.35f);
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
    glPointSize(2.6f);

    mWalkParticlesRenderShader->bind();
    mWalkParticlesRenderShader->setUniform("MVP", mvp);
    mWalkParticlesRenderShader->setUniform("Color", glm::vec4(0.46f, 0.30f, 0.17f, 0.22f));
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

void GameState::renderWithComputeShaders() const noexcept
{
    if (!mComputeShader || !mDisplayShader)
    {
        return;
    }

    if (auto *window = getContext().window; window != nullptr)
    {
        if (SDL_Window *sdlWindow = window->getSDLWindow(); sdlWindow != nullptr)
        {
            int width = 0;
            int height = 0;
            SDL_GetWindowSize(sdlWindow, &width, &height);
            if (width > 0 && height > 0 && (width != mWindowWidth || height != mWindowHeight))
            {
                mWindowWidth = width;
                mWindowHeight = height;
                glViewport(0, 0, mWindowWidth, mWindowHeight);
                const_cast<GameState *>(this)->updateRenderResolution();
                const_cast<GameState *>(this)->createPathTracerTextures();
                const_cast<GameState *>(this)->createCompositeTargets();
                mCurrentBatch = 0;
            }
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

    auto *models = getContext().models;
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

                static bool loggedFirstTriangle = false;
                if (!loggedFirstTriangle && !traceTriangles.empty())
                {
                    loggedFirstTriangle = true;
                    const glm::vec3 playerPos = mPlayer.getPosition();
                    const glm::vec3 cameraPos = mCamera.getActualPosition();
                    const glm::vec3 v0 = glm::vec3(traceTriangles.front().v0);
                    SDL_Log("PathTracer: Model extraction test - player=(%.2f,%.2f,%.2f) camera=(%.2f,%.2f,%.2f) tri_v0=(%.2f,%.2f,%.2f)",
                            playerPos.x, playerPos.y, playerPos.z,
                            cameraPos.x, cameraPos.y, cameraPos.z,
                            v0.x, v0.y, v0.z);
                }
            }

            static bool loggedOnce = false;
            if (!loggedOnce)
            {
                loggedOnce = true;
                SDL_Log("PathTracer model: loaded=%d triangles=%zu", characterModel.isLoaded() ? 1 : 0, traceTriangles.size());
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
    const float historyBlend = kStaticHistoryBlend + (kMovingHistoryBlend - kStaticHistoryBlend) * blurFactor;

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

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mDisplayTex->get());
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

    mCompositeShader->bind();
    mCompositeShader->setUniform("uSceneTex", 0);
    mCompositeShader->setUniform("uBillboardTex", 1);
    mCompositeShader->setUniform("uInvResolution", glm::vec2(1.0f / static_cast<float>(mWindowWidth), 1.0f / static_cast<float>(mWindowHeight)));
    mCompositeShader->setUniform("uBloomThreshold", 0.65f);
    mCompositeShader->setUniform("uBloomStrength", 0.32f);
    mCompositeShader->setUniform("uSpriteAlpha", 1.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mDisplayTex->get());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mBillboardColorTex);

    glBindVertexArray(mVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
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
    
    mAccumTex = nullptr;
    mDisplayTex = nullptr;
    mNoiseTexture = nullptr;

    // Shaders are now managed by ShaderManager - don't delete them here
    mDisplayShader = nullptr;
    mComputeShader = nullptr;
    mCompositeShader = nullptr;

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

    // Update sounds: set listener position based on camera and remove stopped sounds
    updateSounds();

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
    if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
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
            createCompositeTargets();
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
        if (event.key.scancode == SDL_SCANCODE_RETURN && mRunLost)
        {
            resetRunnerRun();
            return true;
        }

        if (event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            requestStackPush(States::ID::PAUSE);
        }

        // Jump with SPACE
        if (event.key.scancode == SDL_SCANCODE_SPACE)
        {
            if (auto *sounds = getContext().sounds)
            {
                sounds->play(SoundEffect::ID::SELECT, sf::Vector2f{mPlayer.getPosition().x, mPlayer.getPosition().z});
            }
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
            if (mArcadeModeEnabled && mCamera.getMode() == CameraMode::THIRD_PERSON)
            {
                mCamera.rotate(0.0f, -event.motion.yrel * sensitivity);
            }
            else
            {
                mCamera.rotate(event.motion.xrel * sensitivity, -event.motion.yrel * sensitivity);
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

    bool renderedModel = false;
    if (mSkinnedModelShader)
    {
        auto *models = getContext().models;
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

    renderTracksideBillboards();
    renderWalkParticles();

    if (mCompositeShader && mBillboardFBO != 0)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
    constexpr float kBorderMargin = 7.5f;

    const float borderZ = mRunnerStrafeLimit + kBorderMargin;

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

void GameState::syncRunnerSettingsFromOptions() noexcept
{
    auto *optionsManager = getContext().options;
    if (!optionsManager)
    {
        return;
    }

    try
    {
        const auto &opts = optionsManager->get(GUIOptions::ID::DE_FACTO);
        mArcadeModeEnabled = opts.mArcadeModeEnabled;
        mRunnerSpeed = std::max(5.0f, opts.mRunnerSpeed);
        mRunnerStrafeLimit = std::max(5.0f, opts.mRunnerStrafeLimit);
        mRunnerStartingPoints = std::max(1, opts.mRunnerStartingPoints);
        mRunnerPickupMinValue = std::min(opts.mRunnerPickupMinValue, opts.mRunnerPickupMaxValue);
        mRunnerPickupMaxValue = std::max(opts.mRunnerPickupMinValue, opts.mRunnerPickupMaxValue);
        mRunnerPickupSpacing = std::max(4.0f, opts.mRunnerPickupSpacing);
        mRunnerObstaclePenalty = std::max(1, opts.mRunnerObstaclePenalty);
        mRunnerCollisionCooldown = std::max(0.05f, opts.mRunnerCollisionCooldown);
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

    processRunnerCollisions(dt);
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

            if (auto *sounds = getContext().sounds)
            {
                sounds->play(SoundEffect::ID::THROW, sf::Vector2f{playerPos.x, playerPos.z});
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

void GameState::resetRunnerRun() noexcept
{
    mPlayerPoints = mRunnerStartingPoints;
    mRunnerDistance = 0.0f;
    mRunnerCollisionTimer = 0.0f;
    mRunnerPointEvents.clear();
    mRunLost = false;

    glm::vec3 current = mPlayer.getPosition();
    current.x = 0.0f;
    current.y = 1.0f;
    current.z = 0.0f;

    mRunnerNextPickupZ = current.z + mRunnerPickupSpacing;
    mPlayer.setPosition(current);
    mWorld.setRunnerPlayerPosition(current);
    mWorld.consumeRunnerCollisionEvents();
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
