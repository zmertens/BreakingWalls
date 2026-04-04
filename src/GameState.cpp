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
#include <ranges>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glad/glad.h>

#include "Font.hpp"
#include "GLSDLHelper.hpp"
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
#include "VertexArrayObject.hpp"
#include "FramebufferObject.hpp"
#include "VertexBufferObject.hpp"

namespace
{
    constexpr unsigned int kSimpleMazeRows = 20u;
    constexpr unsigned int kSimpleMazeCols = 20u;
    constexpr float kSimpleCellSize = 2.4f;
    constexpr float kRasterBirdsEyeFovDeg = 52.0f;
    constexpr float kRasterZoomMinHeadroom = 1.6f;
    constexpr float kRasterZoomStep = 0.85f;
    constexpr float kRasterWallScreenMarginPx = 20.0f;

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
        mHighlightTileShader = &shaders.get(Shaders::ID::GLSL_HIGHLIGHT_TILE);
        mOITResolveShader = &shaders.get(Shaders::ID::GLSL_OIT_RESOLVE);
        mMotionBlurShader = &shaders.get(Shaders::ID::GLSL_MOTION_BLUR);
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
    catch (const std::exception &e)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "GameState: Failed to get SoundPlayer from context: %s", e.what());
    }

    auto &textures = *context.getTextureManager();
    try
    {
        mDisplayTex = &textures.get(Textures::ID::RUNNER_BREAK_PLANE);
        mNoiseTexture = &textures.get(Textures::ID::NOISE2D);
        mTestAlbedoTexture = &textures.get(Textures::ID::SDL_LOGO);
        mMotionBlurTex = &textures.get(Textures::ID::MOTION_BLUR_COLOR);
        mPrevFrameTex = &textures.get(Textures::ID::PREV_FRAME);
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

    // Get VAO manager from context
    mVAOManager = context.getVAOManager();
    if (!mVAOManager)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: VAOManager not available in context");
    }

    // Get FBO manager from context
    mFBOManager = context.getFBOManager();
    if (!mFBOManager)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: FBOManager not available in context");
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
        if (mVAOManager)
        {
            mVAOManager->get(VAOs::ID::FULLSCREEN_QUAD).bind();
        }
        initializeMotionBlur();
        GLSDLHelper::initializeBillboardRendering();

        // Initialize World rendering (shaders, textures, particles, FBOs)
        mWorld.initRendering(mVAOManager, mFBOManager, context.getVBOManager(), context.getModelsManager(), mWindowWidth, mWindowHeight, mPlayer);
        mWorld.buildMazeGeometry(mPlayer);

        // Camera setup (previously inside buildRasterMazeGeometry)
        mCamera.setFieldOfView(kRasterBirdsEyeFovDeg);
        updateRasterBirdsEyeZoomLimits();
        mRasterBirdsEyeDistance = mRasterBirdsEyeMaxDistance;
        applyRasterBirdsEyeCamera();
        mLastCameraPosition = mCamera.getActualPosition();
        mLastCameraYaw = mCamera.getYaw();
        mLastCameraPitch = mCamera.getPitch();

        // Respawn the player at the centre of the first open cell of the raster maze.
        // The maze is centred at the origin; cell (0,0) corner is at (-halfW, 0, -halfD).
        {
            const float halfW = 0.5f * static_cast<float>(kSimpleMazeCols) * kSimpleCellSize;
            const float halfD = 0.5f * static_cast<float>(kSimpleMazeRows) * kSimpleCellSize;
            // True cell-centre of cell (0,0): half a cell-width in from each corner.
            const glm::vec3 rasterSpawn(-halfW + kSimpleCellSize * 0.5f, 1.0f, -halfD + kSimpleCellSize * 0.5f);
            mPlayer.setPosition(rasterSpawn);
            mWorld.createPlayerBody(rasterSpawn); // safe: destroys any prior body first
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
    mWorld.drawScene(mCamera, mPlayer, mWindowWidth, mWindowHeight,
                     mModelAnimTimeSeconds, mPlayerPlanarSpeedForFx);
    renderMotionBlur();
    renderPlayerTileGradientHighlight();
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
                                  ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f)  // Green for positive
                                  : ImVec4(1.0f, 0.2f, 0.2f, 1.0f); // Red for negative

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

void GameState::initializeMotionBlur() noexcept
{
    // Resize managed render textures to current window dimensions
    mMotionBlurTex->loadRenderTarget(std::max(1, mWindowWidth), std::max(1, mWindowHeight),
                                     Texture::RenderTargetFormat::RGBA16F, 0);
    mPrevFrameTex->loadRenderTarget(std::max(1, mWindowWidth), std::max(1, mWindowHeight),
                                   Texture::RenderTargetFormat::RGBA16F, 0);

    // Motion blur FBO is managed by FBOManager (loaded in LoadingState)

    mMotionBlurInitialized = true;
}

void GameState::renderMotionBlur() const noexcept
{
    const glm::vec2 playerVel = mWorld.getPlayerVelocity();
    const float speed = glm::length(playerVel);

    // Only apply motion blur when moving fast enough
    if (speed < 2.0f)
        return;

    const GLsizei w = std::max(1, mWindowWidth);
    const GLsizei h = std::max(1, mWindowHeight);

    const GLuint motionBlurTex = mMotionBlurTex->get();
    const GLuint prevFrameTex = mPrevFrameTex->get();

    // Blit default framebuffer into motion blur texture via FBO (no CPU stall)
    FramebufferObject::unbind(GL_READ_FRAMEBUFFER);
    mFBOManager->get(FBOs::ID::MOTION_BLUR).bind(GL_DRAW_FRAMEBUFFER);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, motionBlurTex, 0);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // Apply motion blur as fullscreen pass back to default framebuffer
    FramebufferObject::unbind();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    mMotionBlurShader->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, motionBlurTex);
    mMotionBlurShader->setUniform("uCurrentFrame", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, prevFrameTex);
    mMotionBlurShader->setUniform("uPrevFrame", 1);
    mMotionBlurShader->setUniform("uVelocity", playerVel);
    mMotionBlurShader->setUniform("uBlurStrength", std::min(speed * 0.1f, 1.5f));

    mVAOManager->get(VAOs::ID::FULLSCREEN_QUAD).bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Store result as previous frame via FBO blit
    FramebufferObject::unbind(GL_READ_FRAMEBUFFER);
    mFBOManager->get(FBOs::ID::MOTION_BLUR).bind(GL_DRAW_FRAMEBUFFER);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, prevFrameTex, 0);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    FramebufferObject::unbind();
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
}

void GameState::renderPlayerTileGradientHighlight() const noexcept
{
    const auto& mazeCellGradientColors = mWorld.getMazeCellGradientColors();
    if (mazeCellGradientColors.empty())
    {
        return;
    }

    const glm::vec3 mazeCenter = mWorld.getRasterMazeCenter();
    const float mazeWidth = mWorld.getRasterMazeWidth();
    const float mazeDepth = mWorld.getRasterMazeDepth();
    const float mazeOriginX = mazeCenter.x - 0.5f * mazeWidth;
    const float mazeOriginZ = mazeCenter.z - 0.5f * mazeDepth;
    const glm::vec3 playerPos = mPlayer.getPosition();

    const int col = static_cast<int>(std::floor((playerPos.x - mazeOriginX) / kSimpleCellSize));
    const int row = static_cast<int>(std::floor((playerPos.z - mazeOriginZ) / kSimpleCellSize));
    if (row < 0 || row >= static_cast<int>(kSimpleMazeRows) || col < 0 || col >= static_cast<int>(kSimpleMazeCols))
    {
        return;
    }

    const int idx = row * static_cast<int>(kSimpleMazeCols) + col;
    if (idx < 0 || idx >= static_cast<int>(mazeCellGradientColors.size()))
    {
        return;
    }

    const float aspectRatio = static_cast<float>(std::max(1, mWindowWidth)) /
                              static_cast<float>(std::max(1, mWindowHeight));
    const glm::mat4 view = mCamera.getLookAt();
    const glm::mat4 proj = mCamera.getPerspective(aspectRatio);
    ImDrawList *drawList = ImGui::GetForegroundDrawList();
    if (!drawList)
    {
        return;
    }

    const float overlayY = mWorld.getRasterMazeTopY() + 0.45f;
    const float centerX = mazeOriginX + (static_cast<float>(col) + 0.5f) * kSimpleCellSize;
    const float centerZ = mazeOriginZ + (static_cast<float>(row) + 0.5f) * kSimpleCellSize;
    const float halfSpan = kSimpleCellSize * 1.5f;

    const glm::vec3 p0(centerX - halfSpan, overlayY, centerZ - halfSpan);
    const glm::vec3 p1(centerX + halfSpan, overlayY, centerZ - halfSpan);
    const glm::vec3 p2(centerX + halfSpan, overlayY, centerZ + halfSpan);
    const glm::vec3 p3(centerX - halfSpan, overlayY, centerZ + halfSpan);

    ImVec2 screenPts[4];
    if (!projectWorldToScreen(p0, view, proj, mWindowWidth, mWindowHeight, screenPts[0]) ||
        !projectWorldToScreen(p1, view, proj, mWindowWidth, mWindowHeight, screenPts[1]) ||
        !projectWorldToScreen(p2, view, proj, mWindowWidth, mWindowHeight, screenPts[2]) ||
        !projectWorldToScreen(p3, view, proj, mWindowWidth, mWindowHeight, screenPts[3]))
    {
        return;
    }

    const glm::vec3 cellColor = mazeCellGradientColors[static_cast<std::size_t>(idx)];
    const glm::vec3 borderColor = glm::mix(cellColor, glm::vec3(0.30f, 1.0f, 0.72f), 0.65f);
    const ImU32 border = ImGui::ColorConvertFloat4ToU32(ImVec4(borderColor.r, borderColor.g, borderColor.b, 0.92f));
    drawList->AddPolyline(screenPts, 4, border, ImDrawFlags_Closed, 4.0f);
}

void GameState::updateRasterBirdsEyeZoomLimits() noexcept
{
    const float safeWidth = static_cast<float>(std::max(1, mWindowWidth));
    const float safeHeight = static_cast<float>(std::max(1, mWindowHeight));
    const float halfW = 0.5f * safeWidth;
    const float halfH = 0.5f * safeHeight;

    const float marginX = std::min(kRasterWallScreenMarginPx, std::max(1.0f, halfW - 1.0f));
    const float marginY = std::min(kRasterWallScreenMarginPx, std::max(1.0f, halfH - 1.0f));

    const float halfMazeW = 0.5f * std::max(0.001f, mWorld.getRasterMazeWidth());
    const float halfMazeD = 0.5f * std::max(0.001f, mWorld.getRasterMazeDepth());

    const float scaleX = halfW / std::max(1.0f, halfW - marginX);
    const float scaleY = halfH / std::max(1.0f, halfH - marginY);
    const float requiredHalfW = halfMazeW * scaleX;
    const float requiredHalfD = halfMazeD * scaleY;

    const float aspect = safeWidth / safeHeight;
    const float verticalHalf = std::max(requiredHalfD, requiredHalfW / std::max(0.001f, aspect));
    const float tanHalfFov = std::tan(glm::radians(kRasterBirdsEyeFovDeg * 0.5f));
    const float fitDistance = verticalHalf / std::max(0.001f, tanHalfFov);

    mRasterBirdsEyeMaxDistance = std::max(fitDistance, 2.0f);
    const float minDistanceFromTop = std::max(2.0f, (mWorld.getRasterMazeTopY() - mWorld.getRasterMazeCenter().y) + kRasterZoomMinHeadroom);
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
    const float halfW = (mWorld.getRasterMazeWidth() > 0.0f) ? mWorld.getRasterMazeWidth() * 0.5f : 24.0f;
    const float halfD = (mWorld.getRasterMazeDepth() > 0.0f) ? mWorld.getRasterMazeDepth() * 0.5f : 24.0f;
    const glm::vec3 mazeCenter = mWorld.getRasterMazeCenter();
    const float camX = glm::clamp(playerPos.x, mazeCenter.x - halfW, mazeCenter.x + halfW);
    const float camZ = glm::clamp(playerPos.z, mazeCenter.z - halfD, mazeCenter.z + halfD);
    mCamera.setPosition(glm::vec3(camX, mRasterBirdsEyeDistance + playerPos.y, camZ));
    // yaw=-90° gives: screen-right = world+X, screen-up = world−Z (row 0 at top).
    // Pitch slightly off vertical avoids a singular camera basis.
    mCamera.setYawPitch(-90.0f, -89.9f, /*clampPitch=*/false, /*wrapYaw=*/true);
}

void GameState::resolvePlayerWallCollisions(glm::vec3 &pos) const noexcept
{
    // Treat the player as a circle in the XZ plane and push it out of each wall AABB.
    // wall AABB packed as glm::vec4(minX, minZ, maxX, maxZ).
    constexpr float kPlayerRadius = 0.42f; // fits through a corridor (cellSize - wallThickness ≈ 2.2)
    constexpr int kIterations = 6;         // multiple passes handle corner pile-ups

    for (int iter = 0; iter < kIterations; ++iter)
    {
        for (const auto &wall : mWorld.getMazeWallAABBs())
        {
            const float closestX = glm::clamp(pos.x, wall.x, wall.z);
            const float closestZ = glm::clamp(pos.z, wall.y, wall.w);
            const float dx = pos.x - closestX;
            const float dz = pos.z - closestZ;
            const float distSq = dx * dx + dz * dz;

            if (distSq >= kPlayerRadius * kPlayerRadius)
                continue; // no overlap

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
    const float mazeW = mWorld.getRasterMazeWidth();
    const float mazeD = mWorld.getRasterMazeDepth();
    if (mazeW > 0.0f && mazeD > 0.0f)
    {
        const glm::vec3 mazeCenter = mWorld.getRasterMazeCenter();
        const float limitW = mazeW * 0.5f - kPlayerRadius;
        const float limitD = mazeD * 0.5f - kPlayerRadius;
        pos.x = glm::clamp(pos.x, mazeCenter.x - limitW, mazeCenter.x + limitW);
        pos.z = glm::clamp(pos.z, mazeCenter.z - limitD, mazeCenter.z + limitD);
    }
}

void GameState::cleanupResources() noexcept
{
    // Scene rendering resources are now managed by World

    // Motion blur textures are managed by TextureManager — no manual cleanup needed

    mDisplayTex = nullptr;
    mNoiseTexture = nullptr;

    // Shaders are now managed by ShaderManager - don't delete them here
    mDisplayShader = nullptr;
    mCompositeShader = nullptr;
    mOITResolveShader = nullptr;

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
        mWorld.markPickupsDirty();

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
            mWorld.markPickupsDirty();
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
        mTouchDelta = glm::vec2(0.0f);
    }
    else if (event.type == SDL_EVENT_FINGER_MOTION && mTouchActive)
    {
        const glm::vec2 currentPos(event.tfinger.x, event.tfinger.y);
        mTouchDelta += currentPos - mTouchLastPos;
        mTouchLastPos = currentPos;
    }
    else if (event.type == SDL_EVENT_FINGER_UP)
    {
        mTouchActive = false;
        mTouchDelta = glm::vec2(0.0f);
    }

    return true;
}

World &GameState::getWorld() noexcept
{
    return mWorld;
}

const World &GameState::getWorld() const noexcept
{
    return mWorld;
}

/// Get current window dimensions (display output size)
glm::ivec2 GameState::getWindowDimensions() const noexcept
{
    return {mWindowWidth, mWindowHeight};
}

/// Get internal path tracer render dimensions (compute workload size)
glm::ivec2 GameState::getRenderDimensions() const noexcept
{ 
    return {mRenderWidth, mRenderHeight};
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
    constexpr float kKeyMoveSpeed = 6.0f;       // world units / second  (keyboard)
    constexpr float kMouseSensitivity = 0.018f; // world units / pixel   (relative mouse)
    constexpr float kTouchSensitivity = 12.0f;  // world units / normalised delta (0‥1)
    constexpr float kMouseDeadzone = 0.5f;      // pixels
    // Hard cap on XZ movement per frame — keeps substep count bounded even
    // during fast mouse flicks. Set to half a cell width (plenty fast).
    constexpr float kMaxMovePerFrame = 1.1f; // world units / frame

    glm::vec3 delta(0.0f);

    // --- Keyboard (WASD + arrow keys) ---
    {
        int numKeys = 0;
        const bool *keys = SDL_GetKeyboardState(&numKeys);
        if (keys)
        {
            if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])
                delta.z -= kKeyMoveSpeed * dt;
            if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])
                delta.z += kKeyMoveSpeed * dt;
            if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])
                delta.x -= kKeyMoveSpeed * dt;
            if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])
                delta.x += kKeyMoveSpeed * dt;
        }
    }

    // --- Relative mouse (cursor locked, so relX/Y accumulate freely) ---
    // relMouseX > 0 → mouse moved right → player moves +X (screen-right)
    // relMouseY > 0 → mouse moved down  → player moves +Z (screen-down)
    if (mCursorLocked)
    {
        if (std::abs(relMouseX) > kMouseDeadzone)
            delta.x += relMouseX * kMouseSensitivity;
        if (std::abs(relMouseY) > kMouseDeadzone)
            delta.z += relMouseY * kMouseSensitivity;
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
