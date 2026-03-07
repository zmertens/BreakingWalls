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
class PathTraceScene;
class SceneRenderer;
class Shader;
class StateStack;
class SoundPlayer;
class Texture;

/// Rendering mode selection
enum class RenderMode : int
{
    COMPUTE = 0, // Existing compute-shader path tracer
    FRAGMENT = 1 // Fragment-shader path tracer (GLSLPT-style)
};

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

    [[nodiscard]] bool isVoronoiPlanetComplete() const noexcept { return mVoronoiPlanet.isPlanetComplete(); }

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

    /// Render using fragment-shader path tracer (GLSLPT-style)
    void renderWithFragmentShaders() const noexcept;

    /// Synchronise BreakingWalls Camera → PTCamera each frame
    void syncPathTraceCamera() noexcept;

    /// Place gameplay camera at a stable Cornell framing for fragment mode.
    void frameFragmentCornellCamera() noexcept;

    /// Build a hardcoded test scene for the fragment path tracer
    void buildPathTraceScene() noexcept;

    /// Composite billboard and path traced scene
    void renderCompositeScene() const noexcept;

    /// Resolve weighted-blended OIT buffers and blend into billboard texture
    void resolveOITToBillboardTarget() const noexcept;

    /// Render player character (third-person mode only)
    void renderPlayerCharacter() const noexcept;

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
    Shader *mComputeShader{nullptr};
    Shader *mPathTracerOutputShader{nullptr};
    Shader *mPathTracerTonemapShader{nullptr};
    Shader *mCompositeShader{nullptr};
    Shader *mOITResolveShader{nullptr};
    Shader *mSkinnedModelShader{nullptr};
    Shader *mStencilOutlineShader{nullptr};
    Shader *mWalkParticlesComputeShader{nullptr};
    Shader *mWalkParticlesRenderShader{nullptr};
    Shader *mShadowShader{nullptr};  // Shadow volume + stencil rendering
    Shader *mVoronoiShader{nullptr}; // Voronoi planet shader

    // Fragment-shader path tracer (GLSLPT-style)
    std::unique_ptr<PathTraceScene> mPathTraceScene;
    std::unique_ptr<SceneRenderer> mSceneRenderer;
    RenderMode mRenderMode{RenderMode::COMPUTE};
    std::unique_ptr<Shader> mVoronoiShaderOwned;
    VoronoiPlanet mVoronoiPlanet;
    // SSBOs for Voronoi cell data (for compute shader)
    GLuint mVoronoiCellColorSSBO{0};
    GLuint mVoronoiCellSeedSSBO{0};
    GLuint mVoronoiCellPaintedSSBO{0};

    Texture *mAccumTex{nullptr};
    Texture *mPathTraceOutputTex{nullptr};
    Texture *mPathTraceStageTex{nullptr};
    Texture *mDisplayTex{nullptr};
    Texture *mPreviewAccumTex{nullptr};
    Texture *mPreviewOutputTex{nullptr};
    Texture *mNoiseTexture{nullptr};
    Texture *mTestAlbedoTexture{nullptr};

    // GPU resources
    GLuint mShapeSSBO{0};  // Shader Storage Buffer Object for spheres
    GLuint mTriangleSSBO{0};
    GLuint mVAO{0};        // Vertex Array Object for fullscreen quad
    GLuint mPathTracePostFBO{0};
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
    GLuint mShadowFBO{0};              // Shadow render target
    Texture *mShadowTexture{nullptr};  // Shadow map texture
    GLuint mShadowVAO{0};              // Shadow quad VAO
    GLuint mShadowVBO{0};              // Shadow quad VBO
    bool mShadowsInitialized{false};

    // Reflection rendering resources
    GLuint mReflectionFBO{0};          // Reflection render target
    Texture *mReflectionColorTex{nullptr}; // Reflection color texture
    GLuint mReflectionDepthRbo{0};     // Reflection depth buffer
    bool mReflectionsInitialized{false};
    bool mOITInitialized{false};

    // Progressive rendering state
    mutable uint32_t mCurrentBatch{0};
    mutable uint32_t mCurrentTileIndex{0};
    uint32_t mSamplesPerBatch{5};
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

    mutable std::size_t mShapeSSBOCapacityBytes{0};
    mutable std::size_t mTriangleSSBOCapacityBytes{0};

    static constexpr std::size_t kTargetRenderPixels = 1600ull * 900ull;
    static constexpr float kMinRenderScale = 0.82f;

    bool mStencilOutlineEnabled{true};
    bool mStencilOutlinePulseEnabled{false};
    float mStencilOutlineWidth{0.05f};
    float mStencilOutlinePulseSpeed{2.4f};
    float mStencilOutlinePulseAmount{0.28f};
    glm::vec3 mStencilOutlineColor{0.38f, 0.94f, 1.0f};

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

    bool mGameIsPaused{true};
};

#endif // GAME_STATE_HPP
