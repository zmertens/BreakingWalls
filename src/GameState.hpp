#ifndef GAME_STATE_HPP
#define GAME_STATE_HPP

#include "Camera.hpp"
#include "GLTFModel.hpp"
#include "State.hpp"
#include "World.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <random>
#include <string>
#include <vector>

class MusicPlayer;
class Shader;
class StateStack;
class SoundPlayer;
class Texture;

union SDL_Event;
struct SDL_Joystick;

/// @brief Main game state managing physics, rendering, and input
/// @details Orchestrates World updates, player input, and GPU-accelerated graphics pipeline
/// Supports both first-person and third-person camera modes with character animation
class GameState : public State
{
public:
    explicit GameState(StateStack &stack, Context context);
    ~GameState() override;

    void draw() const noexcept override;
    bool update(float dt, unsigned int subSteps) noexcept override;
    bool handleEvent(const SDL_Event &event) noexcept override;

    /// Get reference to World (for multiplayer remote player rendering)
    World &getWorld() noexcept { return mWorld; }
    const World &getWorld() const noexcept { return mWorld; }

    /// Get reference to Camera (for multiplayer rendering)
    Camera &getCamera() noexcept { return mCamera; }
    const Camera &getCamera() const noexcept { return mCamera; }

    /// Get current window dimensions (display output size)
    glm::ivec2 getWindowDimensions() const noexcept { return {mWindowWidth, mWindowHeight}; }

    /// Get internal path tracer render dimensions (compute workload size)
    glm::ivec2 getRenderDimensions() const noexcept { return {mRenderWidth, mRenderHeight}; }

    /// Get render scale relative to window size
    float getRenderScale() const noexcept;

private:
    /// Recompute internal render resolution from current window size
    void updateRenderResolution() noexcept;

    /// Check and handle window resize events
    void handleWindowResize() noexcept;

    /// Create or resize composite render targets for billboard blending
    void createCompositeTargets() noexcept;

    /// Initialize shadow rendering resources (FBO, textures, shaders)
    void initializeShadowResources() noexcept;

    /// Render character shadow to shadow map with soft-shadow blur
    void renderCharacterShadow() const noexcept;

    /// Initialize player reflection rendering resources
    void initializeReflectionResources() noexcept;

    /// Render reflected player character on ground plane
    void renderPlayerReflection() const noexcept;


    /// Initialize standalone raster shaders/buffers for pure maze rendering.
    void initializeRasterMazeResources() noexcept;

    /// Build static 3D maze mesh (with per-vertex colors) from MazeBuilder data.
    void buildRasterMazeGeometry() noexcept;

    /// Recompute min/max bird's-eye zoom distance from maze bounds and window size.
    void updateRasterBirdsEyeZoomLimits() noexcept;

    /// Apply locked bird's-eye camera transform from current zoom distance.
    void applyRasterBirdsEyeCamera() noexcept;

    /// Render sky + maze using the pure raster pipeline.
    void renderRasterMaze() const noexcept;

    /// Render player character billboard on the raster maze
    void renderPlayerCharacter() const noexcept;

    /// Render scoring billboards (pickup values) using ImGui
    void renderScoreBillboards() const noexcept;

    /// Render pickup spheres as colored orbs in the maze
    void renderPickupSpheres() const noexcept;

    /// Apply motion blur effect using the compute pipeline
    void renderMotionBlur() const noexcept;

    /// Initialize motion blur compute resources
    void initializeMotionBlur() noexcept;

    /// Ensure movement particle resources are initialized
    void initializeWalkParticles() noexcept;

    /// Render compute-driven particles around the player while walking
    void renderWalkParticles() const noexcept;

    /// Check if camera moved and reset accumulation if needed
    bool checkCameraMovement() const noexcept;

    /// Update listener position and remove stopped sounds
    void updateSounds() noexcept;

    /// Clean up OpenGL resources
    void cleanupResources() noexcept;

    /// Hide/show cursor and configure relative mouse mode.
    void configureCursorLock(bool enabled) noexcept;

    /// Open first available joystick and initialize rumble support if available.
    void initializeJoystickAndHaptics() noexcept;

    /// Release joystick resources.
    void cleanupJoystickAndHaptics() noexcept;

    /// Apply joystick left-stick horizontal movement as strafe.
    void updateJoystickInput(float dt) noexcept;

    /// Push the player out of any overlapping maze wall AABBs (circle vs AABB, XZ plane).
    void resolvePlayerWallCollisions(glm::vec3 &pos) const noexcept;

    /// Apply birds-eye top-down XZ movement from keyboard, mouse, and touch.
    /// relMouseX/Y must be pre-read from SDL_GetRelativeMouseState before calling handleRealtimeInput.
    void handleBirdsEyeInput(float dt, float relMouseX, float relMouseY) noexcept;

    /// Trigger a short haptic rumble pulse (used for input testing).
    void triggerHapticTest(float strength, float seconds) noexcept;

    World mWorld;    // Manages both 2D physics and 3D sphere scene
    Player &mPlayer; // Restored for camera input handling

    // Music player reference for game music
    MusicPlayer *mGameMusic{nullptr};
    SoundPlayer *mSoundPlayer{nullptr};

    // Path tracer camera (for 3D scene navigation)
    // Supports both first-person and third-person modes
    Camera mCamera;

    // Shader references from context
    Shader *mDisplayShader{nullptr};
    Shader *mCompositeShader{nullptr};
    Shader *mOITResolveShader{nullptr};
    Shader *mWalkParticlesComputeShader{nullptr};
    Shader *mWalkParticlesRenderShader{nullptr};
    Shader *mShadowShader{nullptr};  // Shadow volume + stencil rendering

    Texture *mDisplayTex{nullptr};
    Texture *mNoiseTexture{nullptr};
    Texture *mTestAlbedoTexture{nullptr};

    // GPU resources
    GLuint mVAO{0}; // Vertex Array Object for fullscreen quad
    GLuint mBillboardFBO{0};
    Texture *mBillboardColorTex{nullptr};
    GLuint mBillboardDepthRbo{0};
    GLuint mOITFBO{0};
    Texture *mOITAccumTex{nullptr};
    Texture *mOITRevealTex{nullptr};
    GLuint mOITDepthRbo{0};
    GLuint mWalkParticlesVAO{0};
    GLuint mWalkParticlesPosSSBO{0};
    GLuint mWalkParticlesVelSSBO{0};

    // Shadow rendering resources
    GLuint mShadowFBO{0};             // Shadow render target
    Texture *mShadowTexture{nullptr}; // Shadow map texture
    GLuint mShadowVAO{0};             // Shadow quad VAO
    GLuint mShadowVBO{0};             // Shadow quad VBO
    bool mShadowsInitialized{false};

    // Reflection rendering resources
    GLuint mReflectionFBO{0};              // Reflection render target
    Texture *mReflectionColorTex{nullptr}; // Reflection color texture
    GLuint mReflectionDepthRbo{0};         // Reflection depth buffer
    bool mReflectionsInitialized{false};
    bool mOITInitialized{false};

    // Progressive rendering state
    mutable uint32_t mCurrentBatch{0};
    mutable uint32_t mCurrentTileIndex{0};
    uint32_t mSamplesPerBatch{12};
    uint32_t mTotalBatches{250};

    // Camera movement tracking for accumulation reset
    mutable glm::vec3 mLastCameraPosition;
    mutable float mLastCameraYaw{0.0f};
    mutable float mLastCameraPitch{0.0f};

    bool mShadersInitialized{false};
    mutable int mWindowWidth{1280};
    mutable int mWindowHeight{720};
    int mRenderWidth{1280};
    int mRenderHeight{720};
    int mPreviewRenderWidth{640};
    int mPreviewRenderHeight{360};

    static constexpr std::size_t kTargetRenderPixels = 960ull * 540ull;
    static constexpr float kMinRenderScale = 0.60f;

    mutable float mModelAnimTimeSeconds{0.0f};
    mutable float mWalkParticlesTime{0.0f};
    mutable bool mWalkParticlesInitialized{false};
    mutable bool mHasLastFxPosition{false};
    mutable glm::vec3 mLastFxPlayerPosition{0.0f};
    mutable float mPlayerPlanarSpeedForFx{0.0f};
    mutable GLuint mWalkParticleCount{1600};

    SDL_Joystick *mJoystick{nullptr};
    bool mJoystickRumbleSupported{false};
    bool mCursorLocked{false};
    float mJoystickDeadzone{0.22f};
    float mJoystickStrafeSpeed{55.0f};

    // Touch-screen state for top-down directional movement
    bool mTouchActive{false};
    glm::vec2 mTouchLastPos{0.0f};
    glm::vec2 mTouchDelta{0.0f};

    bool mGameIsPaused{true};

    std::unique_ptr<Shader> mRasterMazeShader;
    std::unique_ptr<Shader> mRasterSkyShader;
    std::unique_ptr<Shader> mSkinnedCharacterShader;
    GLuint mRasterMazeVAO{0};
    GLuint mRasterMazeVBO{0};
    GLsizei mRasterMazeVertexCount{0};
    glm::vec3 mRasterMazeCenter{0.0f};
    float mRasterMazeWidth{1.0f};
    float mRasterMazeDepth{1.0f};
    float mRasterMazeTopY{1.0f};
    float mRasterBirdsEyeDistance{10.0f};
    float mRasterBirdsEyeMinDistance{4.0f};
    float mRasterBirdsEyeMaxDistance{20.0f};

    // XZ axis-aligned bounding boxes of every maze wall, packed as (minX, minZ, maxX, maxZ).
    // Built once by buildRasterMazeGeometry() and used for player collision each frame.
    std::vector<glm::vec4> mMazeWallAABBs;

    // Pickup sphere rendering (cached GPU buffers)
    GLuint mPickupVAO{0};
    GLuint mPickupVBO{0};
    GLsizei mPickupVertexCount{0};
    mutable bool mPickupsDirty{true};

    // Motion blur resources
    GLuint mMotionBlurFBO{0};
    GLuint mMotionBlurTex{0};
    GLuint mPrevFrameTex{0};
    std::unique_ptr<Shader> mMotionBlurShader;
    bool mMotionBlurInitialized{false};

    // Score display
    mutable std::vector<std::pair<glm::vec3, int>> mActiveScorePopups;   // position, value
    mutable std::vector<float> mScorePopupTimers;
    mutable int mLastDisplayedScore{0};
};

#endif // GAME_STATE_HPP
