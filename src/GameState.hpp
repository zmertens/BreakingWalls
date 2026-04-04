#ifndef GAME_STATE_HPP
#define GAME_STATE_HPP

#include "Camera.hpp"
#include "GLTFModel.hpp"
#include "State.hpp"
#include "World.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <random>
#include <string>
#include <vector>

class MusicPlayer;
class Shader;
class StateStack;
class SoundPlayer;
class Texture;

namespace sf { class Event; }

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
    bool handleEvent(const sf::Event &event) noexcept override;

    /// Get reference to World (for multiplayer remote player rendering)
    World &getWorld() noexcept;
    const World &getWorld() const noexcept;

    /// Get current window dimensions (display output size)
    glm::ivec2 getWindowDimensions() const noexcept;

    /// Get internal path tracer render dimensions (compute workload size)
    glm::ivec2 getRenderDimensions() const noexcept;

    /// Get render scale relative to window size
    float getRenderScale() const noexcept;

private:
    /// Recompute internal render resolution from current window size
    void updateRenderResolution() noexcept;

    /// Check and handle window resize events
    void handleWindowResize() noexcept;

    /// Recompute min/max bird's-eye zoom distance from maze bounds and window size.
    void updateRasterBirdsEyeZoomLimits() noexcept;

    /// Apply locked bird's-eye camera transform from current zoom distance.
    void applyRasterBirdsEyeCamera() noexcept;

    /// Render gradient highlight on the tile currently under the player.
    void renderPlayerTileGradientHighlight() const noexcept;

    /// Render scoring billboards (pickup values) using ImGui
    void renderScoreBillboards() const noexcept;

    /// Apply motion blur effect using the compute pipeline
    void renderMotionBlur() const noexcept;

    /// Initialize motion blur compute resources
    void initializeMotionBlur() noexcept;

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

    World mWorld;
    Player &mPlayer;

    // Music player reference for game music
    MusicPlayer *mGameMusic{nullptr};
    SoundPlayer *mSoundPlayer{nullptr};

    Camera mCamera;

    // Shader references from context (post-process / UI only)
    Shader *mDisplayShader{nullptr};
    Shader *mCompositeShader{nullptr};
    Shader *mHighlightTileShader{nullptr};
    Shader *mOITResolveShader{nullptr};
    Shader *mMotionBlurShader{nullptr};

    Texture *mDisplayTex{nullptr};
    Texture *mNoiseTexture{nullptr};
    Texture *mTestAlbedoTexture{nullptr};
    Texture *mMotionBlurTex{nullptr};
    Texture *mPrevFrameTex{nullptr};

    // VAO/FBO managers needed for post-process (motion blur)
    VAOManager *mVAOManager{nullptr};
    FBOManager *mFBOManager{nullptr};

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
    mutable bool mHasLastFxPosition{false};
    mutable glm::vec3 mLastFxPlayerPosition{0.0f};
    mutable float mPlayerPlanarSpeedForFx{0.0f};

    unsigned int mJoystickIndex{0};
    bool mJoystickConnected{false};
    bool mCursorLocked{false};
    glm::vec2 mLastMousePos{0.0f};
    float mJoystickDeadzone{0.22f};
    float mJoystickStrafeSpeed{55.0f};

    // Touch-screen state for top-down directional movement
    bool mTouchActive{false};
    glm::vec2 mTouchLastPos{0.0f};
    glm::vec2 mTouchDelta{0.0f};

    bool mGameIsPaused{true};

    float mRasterBirdsEyeDistance{10.0f};
    float mRasterBirdsEyeMinDistance{4.0f};
    float mRasterBirdsEyeMaxDistance{20.0f};

    // Motion blur resources
    bool mMotionBlurInitialized{false};

    // Score display
    mutable std::vector<std::pair<glm::vec3, int>> mActiveScorePopups;   // position, value
    mutable std::vector<float> mScorePopupTimers;
    mutable int mLastDisplayedScore{0};
};

#endif // GAME_STATE_HPP
