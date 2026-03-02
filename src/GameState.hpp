<<<<<<< HEAD
#ifndef GAME_STATE_HPP
#define GAME_STATE_HPP

#include "Camera.hpp"
#include "State.hpp"
#include "World.hpp"
#include "VoronoiPlanet.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <random>
#include <memory>
#include <vector>

class MusicPlayer;
class Shader;
class StateStack;
class SoundPlayer;
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
    float getRenderScale() const noexcept;

private:
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

    /// Render using compute shaders (path tracing)
    void renderWithComputeShaders() const noexcept;

    /// Composite billboard and path traced scene
    void renderCompositeScene() const noexcept;

    /// Resolve weighted-blended OIT buffers and blend into billboard texture
    void resolveOITToBillboardTarget() const noexcept;

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

    /// Advance and cull floating score popup labels
    void updateRunnerScorePopups(float dt) noexcept;

    /// Reset the active run after point loss
    void resetRunnerRun() noexcept;

    /// Draw lightweight arcade HUD
    void drawRunnerHud() const noexcept;

    /// Draw floating score labels above collision points
    void drawRunnerScorePopups() const noexcept;

    /// Create tiny texture/resources used by runner break plane billboard rendering
    void initializeRunnerBreakPlaneResources() noexcept;

    /// Update offscreen plane texture using particle compute + point render pass
    void renderRunnerBreakPlaneTexture() const noexcept;

    /// Update break-plane lifecycle and check pass-through shatter events
    void updateRunnerBreakPlane(float dt) noexcept;

    /// Trigger break-plane shatter effects and scoring
    void shatterRunnerBreakPlane() noexcept;

    /// Render active break-plane and flying shards in runner lane
    void renderRunnerBreakPlane() const noexcept;

    /// Clean up OpenGL resources
    void cleanupResources() noexcept;

    struct RunnerPointEvent
    {
        glm::vec3 position{};
        int value{0};
        bool consumed{false};
    };

    struct RunnerScorePopup
    {
        glm::vec3 worldPosition{};
        int value{0};
        float age{0.0f};
        float lifetime{1.05f};
        float riseSpeed{3.2f};
    };

    struct RunnerBreakPlaneShard
    {
        glm::vec3 position{};
        glm::vec3 velocity{};
        glm::vec2 halfSize{0.35f, 0.35f};
        float age{0.0f};
        float lifetime{0.55f};
    };

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
    Shader *mComputeShader{nullptr};
    Shader *mCompositeShader{nullptr};
    Shader *mOITResolveShader{nullptr};
    Shader *mSkinnedModelShader{nullptr};
    Shader *mWalkParticlesComputeShader{nullptr};
    Shader *mWalkParticlesRenderShader{nullptr};
    Shader *mShadowShader{nullptr};  // Shadow volume + stencil rendering
    Shader *mVoronoiShader{nullptr}; // Voronoi planet shader
    std::unique_ptr<Shader> mVoronoiShaderOwned;
    VoronoiPlanet mVoronoiPlanet;
    // SSBOs for Voronoi cell data (for compute shader)
    GLuint mVoronoiCellColorSSBO{0};
    GLuint mVoronoiCellSeedSSBO{0};
    GLuint mVoronoiCellPaintedSSBO{0};

    Texture *mAccumTex{nullptr};
    Texture *mDisplayTex{nullptr};
    Texture *mNoiseTexture{nullptr};

    // GPU resources
    GLuint mShapeSSBO{0};  // Shader Storage Buffer Object for spheres
    GLuint mTriangleSSBO{0};
    GLuint mVAO{0};        // Vertex Array Object for fullscreen quad
    GLuint mBillboardFBO{0};
    GLuint mBillboardColorTex{0};
    GLuint mBillboardDepthRbo{0};
    GLuint mOITFBO{0};
    GLuint mOITAccumTex{0};
    GLuint mOITRevealTex{0};
    GLuint mOITDepthRbo{0};
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
    bool mOITInitialized{false};

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
    mutable GLuint mRunnerBreakPlaneTexture{0};
    mutable GLuint mRunnerBreakPlaneFBO{0};
    mutable GLuint mRunnerBreakPlaneVAO{0};
    mutable GLuint mRunnerBreakPlanePosSSBO{0};
    mutable GLuint mRunnerBreakPlaneVelSSBO{0};
    mutable GLuint mRunnerBreakPlaneParticleCount{4096};
    mutable float mRunnerBreakPlaneFxTime{0.0f};
    int mRunnerBreakPlaneTextureWidth{512};
    int mRunnerBreakPlaneTextureHeight{512};
    bool mRunnerBreakPlaneActive{true};
    float mRunnerBreakPlaneX{0.0f};
    float mRunnerBreakPlaneRespawnTimer{0.0f};
    float mRunnerBreakPlaneRespawnDelay{0.60f};
    float mRunnerBreakPlaneSpacing{170.0f};
    float mRunnerBreakPlaneHeight{8.5f};
    int mRunnerBreakPlanePoints{100};
    float mRunnerBreakPlaneLastPlayerX{0.0f};
    std::vector<RunnerBreakPlaneShard> mRunnerBreakPlaneShards;
    std::vector<RunnerPointEvent> mRunnerPointEvents;
    std::vector<RunnerScorePopup> mRunnerScorePopups;
    std::mt19937 mRunnerRng{1337u};

    bool mGameIsPaused{true};
};

#endif // GAME_STATE_HPP
=======
#ifndef GAME_STATE_HPP
#define GAME_STATE_HPP

#include "Camera.hpp"
#include "State.hpp"
#include "World.hpp"
#include "VoronoiPlanet.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <random>
#include <memory>
#include <vector>

class MusicPlayer;
class Shader;
class StateStack;
class SoundPlayer;
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
    float getRenderScale() const noexcept;

private:
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

    /// Render using compute shaders (path tracing)
    void renderWithComputeShaders() const noexcept;

    /// Composite billboard and path traced scene
    void renderCompositeScene() const noexcept;

    /// Resolve weighted-blended OIT buffers and blend into billboard texture
    void resolveOITToBillboardTarget() const noexcept;

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

    /// Advance and cull floating score popup labels
    void updateRunnerScorePopups(float dt) noexcept;

    /// Reset the active run after point loss
    void resetRunnerRun() noexcept;

    /// Draw lightweight arcade HUD
    void drawRunnerHud() const noexcept;

    /// Draw floating score labels above collision points
    void drawRunnerScorePopups() const noexcept;

    /// Create tiny texture/resources used by runner break plane billboard rendering
    void initializeRunnerBreakPlaneResources() noexcept;

    /// Update offscreen plane texture using particle compute + point render pass
    void renderRunnerBreakPlaneTexture() const noexcept;

    /// Update break-plane lifecycle and check pass-through shatter events
    void updateRunnerBreakPlane(float dt) noexcept;

    /// Trigger break-plane shatter effects and scoring
    void shatterRunnerBreakPlane() noexcept;

    /// Render active break-plane and flying shards in runner lane
    void renderRunnerBreakPlane() const noexcept;

    /// Clean up OpenGL resources
    void cleanupResources() noexcept;

    struct RunnerPointEvent
    {
        glm::vec3 position{};
        int value{0};
        bool consumed{false};
    };

    struct RunnerScorePopup
    {
        glm::vec3 worldPosition{};
        int value{0};
        float age{0.0f};
        float lifetime{1.05f};
        float riseSpeed{3.2f};
    };

    struct RunnerBreakPlaneShard
    {
        glm::vec3 position{};
        glm::vec3 velocity{};
        glm::vec2 halfSize{0.35f, 0.35f};
        float age{0.0f};
        float lifetime{0.55f};
    };

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
    Shader *mComputeShader{nullptr};
    Shader *mCompositeShader{nullptr};
    Shader *mOITResolveShader{nullptr};
    Shader *mSkinnedModelShader{nullptr};
    Shader *mWalkParticlesComputeShader{nullptr};
    Shader *mWalkParticlesRenderShader{nullptr};
    Shader *mShadowShader{nullptr};  // Shadow volume + stencil rendering
    Shader *mVoronoiShader{nullptr}; // Voronoi planet shader
    std::unique_ptr<Shader> mVoronoiShaderOwned;
    VoronoiPlanet mVoronoiPlanet;
    // SSBOs for Voronoi cell data (for compute shader)
    GLuint mVoronoiCellColorSSBO{0};
    GLuint mVoronoiCellSeedSSBO{0};
    GLuint mVoronoiCellPaintedSSBO{0};

    Texture *mAccumTex{nullptr};
    Texture *mDisplayTex{nullptr};
    Texture *mNoiseTexture{nullptr};

    // GPU resources
    GLuint mShapeSSBO{0};  // Shader Storage Buffer Object for spheres
    GLuint mTriangleSSBO{0};
    GLuint mVAO{0};        // Vertex Array Object for fullscreen quad
    GLuint mBillboardFBO{0};
    GLuint mBillboardColorTex{0};
    GLuint mBillboardDepthRbo{0};
    GLuint mOITFBO{0};
    GLuint mOITAccumTex{0};
    GLuint mOITRevealTex{0};
    GLuint mOITDepthRbo{0};
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
    bool mOITInitialized{false};

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
    mutable GLuint mRunnerBreakPlaneTexture{0};
    mutable GLuint mRunnerBreakPlaneFBO{0};
    mutable GLuint mRunnerBreakPlaneVAO{0};
    mutable GLuint mRunnerBreakPlanePosSSBO{0};
    mutable GLuint mRunnerBreakPlaneVelSSBO{0};
    mutable GLuint mRunnerBreakPlaneParticleCount{4096};
    mutable float mRunnerBreakPlaneFxTime{0.0f};
    int mRunnerBreakPlaneTextureWidth{512};
    int mRunnerBreakPlaneTextureHeight{512};
    bool mRunnerBreakPlaneActive{true};
    float mRunnerBreakPlaneX{0.0f};
    float mRunnerBreakPlaneRespawnTimer{0.0f};
    float mRunnerBreakPlaneRespawnDelay{0.60f};
    float mRunnerBreakPlaneSpacing{170.0f};
    float mRunnerBreakPlaneHeight{8.5f};
    int mRunnerBreakPlanePoints{100};
    float mRunnerBreakPlaneLastPlayerX{0.0f};
    std::vector<RunnerBreakPlaneShard> mRunnerBreakPlaneShards;
    std::vector<RunnerPointEvent> mRunnerPointEvents;
    std::vector<RunnerScorePopup> mRunnerScorePopups;
    std::mt19937 mRunnerRng{1337u};

    bool mGameIsPaused{true};
};

#endif // GAME_STATE_HPP
>>>>>>> d3122ee0e58222ba762f9edf23a88344c9a14b0d
