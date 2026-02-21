#ifndef GAME_STATE_HPP
#define GAME_STATE_HPP

#include "Camera.hpp"
#include "State.hpp"
#include "World.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <random>
#include <memory>
#include <vector>

class MusicPlayer;
class Shader;
class StateStack;
class Texture;

union SDL_Event;

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
    float getRenderScale() const noexcept
    {
        return (mWindowWidth > 0 && mWindowHeight > 0)
                   ? static_cast<float>(mRenderWidth) / static_cast<float>(mWindowWidth)
                   : 1.0f;
    }

private:
    struct RunnerPointEvent
    {
        glm::vec3 position{};
        int value{0};
        bool consumed{false};
    };

    World mWorld;    // Manages both 2D physics and 3D sphere scene
    Player &mPlayer; // Restored for camera input handling

    // Music player reference for game music
    MusicPlayer *mGameMusic{nullptr};

    // Path tracer camera (for 3D scene navigation)
    // Supports both first-person and third-person modes
    Camera mCamera;

    // Shader references from context
    Shader *mDisplayShader{nullptr};
    Shader *mComputeShader{nullptr};
    Shader *mCompositeShader{nullptr};
    std::unique_ptr<Shader> mSkinnedModelShader;
    mutable Shader *mWalkParticlesComputeShader{nullptr};
    mutable Shader *mWalkParticlesRenderShader{nullptr};
    mutable Shader *mShadowShader{nullptr};  // Shadow volume + stencil rendering

    Texture *mAccumTex{nullptr};
    Texture *mDisplayTex{nullptr};
    Texture *mNoiseTexture{nullptr};
    Texture *mSoftShadowKernel{nullptr};    // Soft shadow gaussian kernel

    // GPU resources
    GLuint mShapeSSBO{0};  // Shader Storage Buffer Object for spheres
    GLuint mTriangleSSBO{0};
    GLuint mVAO{0};        // Vertex Array Object for fullscreen quad
    GLuint mBillboardFBO{0};
    GLuint mBillboardColorTex{0};
    GLuint mBillboardDepthRbo{0};
    GLuint mWalkParticlesVAO{0};
    GLuint mWalkParticlesPosSSBO{0};
    GLuint mWalkParticlesVelSSBO{0};
    
    // Shadow rendering resources
    GLuint mShadowFBO{0};              // Shadow render target
    GLuint mShadowTexture{0};          // Shadow map texture
    GLuint mShadowVAO{0};              // Shadow quad VAO
    GLuint mShadowVBO{0};              // Shadow quad VBO
    bool mShadowsInitialized{false};

    // Reflection rendering resources
    GLuint mReflectionFBO{0};          // Reflection render target
    GLuint mReflectionColorTex{0};     // Reflection color texture
    GLuint mReflectionDepthRbo{0};     // Reflection depth buffer
    bool mReflectionsInitialized{false};

    // Progressive rendering state
    mutable uint32_t mCurrentBatch{0};
    uint32_t mSamplesPerBatch{4};
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

    mutable std::size_t mShapeSSBOCapacityBytes{0};
    mutable std::size_t mTriangleSSBOCapacityBytes{0};

    static constexpr std::size_t kTargetRenderPixels = 1600ull * 900ull;
    static constexpr float kMinRenderScale = 0.60f;

    // Single-player endless runner arcade state
    bool mArcadeModeEnabled{false};
    int mPlayerPoints{0};
    float mRunnerDistance{0.0f};
    float mRunnerSpeed{30.0f};
    float mRunnerStrafeLimit{35.0f};
    float mRunnerPlayerRadius{1.0f};
    float mRunnerCollisionCooldown{0.40f};
    float mRunnerCollisionTimer{0.0f};
    float mRunnerPickupSpacing{18.0f};
    float mRunnerNextPickupZ{0.0f};
    float mRunnerPickupSpawnAhead{120.0f};
    float mRunnerPickupCaptureRadius{3.2f};
    int mRunnerPickupMinValue{-25};
    int mRunnerPickupMaxValue{40};
    int mRunnerObstaclePenalty{25};
    int mRunnerStartingPoints{100};
    int mMotionBlurBracket1Points{300};
    int mMotionBlurBracket2Points{500};
    int mMotionBlurBracket3Points{800};
    int mMotionBlurBracket4Points{1200};
    float mMotionBlurBracket1Boost{0.10f};
    float mMotionBlurBracket2Boost{0.18f};
    float mMotionBlurBracket3Boost{0.28f};
    float mMotionBlurBracket4Boost{0.38f};
    bool mRunLost{false};
    mutable int mLastAnnouncedPoints{0};
    mutable float mHudUpdateTimer{0.0f};
    mutable float mModelAnimTimeSeconds{0.0f};
    mutable float mWalkParticlesTime{0.0f};
    mutable bool mWalkParticlesInitialized{false};
    mutable bool mHasLastFxPosition{false};
    mutable glm::vec3 mLastFxPlayerPosition{0.0f};
    mutable float mPlayerPlanarSpeedForFx{0.0f};
    mutable GLuint mWalkParticleCount{1600};
    std::vector<RunnerPointEvent> mRunnerPointEvents;
    std::mt19937 mRunnerRng{1337u};

    /// Initialize GPU graphics resources for compute shader rendering
    void initializeGraphicsResources() noexcept;

    /// Recompute internal render resolution from current window size
    void updateRenderResolution() noexcept;

    /// Check and handle window resize events
    void handleWindowResize() noexcept;

    /// Create textures for path tracing (accumulation + display)
    void createPathTracerTextures() noexcept;

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

    /// Compile shader program used for skeletal model rendering
    void initializeSkinnedModelShader() noexcept;

    /// Render using compute shaders (path tracing)
    void renderWithComputeShaders() const noexcept;

    /// Composite billboard and path traced scene
    void renderCompositeScene() const noexcept;

    /// Render player character (third-person mode only)
    void renderPlayerCharacter() const noexcept;

    /// Render decorative billboard sprites along runner lane borders
    void renderTracksideBillboards() const noexcept;

    /// Ensure movement particle resources are initialized
    void initializeWalkParticles() noexcept;

    /// Render compute-driven particles around the player while walking
    void renderWalkParticles() const noexcept;

    /// Check if camera moved and reset accumulation if needed
    bool checkCameraMovement() const noexcept;

    /// Compute score-based boost using configurable runner brackets
    float getScoreBracketBoost() const noexcept;

    /// Update listener position and remove stopped sounds
    void updateSounds() noexcept;

    /// Pull latest runner settings from OptionsManager
    void syncRunnerSettingsFromOptions() noexcept;

    /// Advance endless-runner gameplay and points
    void updateRunnerGameplay(float dt) noexcept;

    /// Spawn point events in front of the player
    void spawnPointEvents() noexcept;

    /// Resolve point pickups and obstacle penalties
    void processRunnerCollisions(float dt) noexcept;

    /// Reset the active run after point loss
    void resetRunnerRun() noexcept;

    /// Draw lightweight arcade HUD
    void drawRunnerHud() const noexcept;

    /// Clean up OpenGL resources
    void cleanupResources() noexcept;
};

#endif // GAME_STATE_HPP
