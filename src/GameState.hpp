#ifndef GAME_STATE_HPP
#define GAME_STATE_HPP

#include "State.hpp"
#include "World.hpp"
#include "Shader.hpp"
#include "Camera.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <memory>

class StateStack;
class MusicPlayer;
union SDL_Event;

/// @brief Main game state managing physics, rendering, and input
/// @details Orchestrates World updates, player input, and GPU-accelerated graphics pipeline
/// World now manages both 2D physics entities and 3D path tracer spheres with physics
class GameState : public State
{
public:
    explicit GameState(StateStack& stack, Context context);
    ~GameState() override;

    void draw() const noexcept override;
    bool update(float dt, unsigned int subSteps) noexcept override;
    bool handleEvent(const SDL_Event& event) noexcept override;

private:
    World mWorld;  // Manages both 2D physics and 3D sphere scene

    // Music player reference for game music
    MusicPlayer* mGameMusic{ nullptr };

    // Path tracer camera (for 3D scene navigation)
    Camera mCamera;

    // Shader references from context
    Shader* mDisplayShader{ nullptr };
    Shader* mComputeShader{ nullptr };

    // GPU resources
    GLuint mShapeSSBO{ 0 };     // Shader Storage Buffer Object for spheres
    GLuint mVAO{ 0 };           // Vertex Array Object for fullscreen quad
    GLuint mAccumTex{ 0 };      // Accumulation texture for progressive rendering
    GLuint mDisplayTex{ 0 };    // Display texture for final output

    // Progressive rendering state
    mutable uint32_t mCurrentBatch{ 0 };
    uint32_t mSamplesPerBatch{ 4 };
    uint32_t mTotalBatches{ 250 };

    // Camera movement tracking for accumulation reset
    mutable glm::vec3 mLastCameraPosition;
    mutable float mLastCameraYaw{ 0.0f };
    mutable float mLastCameraPitch{ 0.0f };

    bool mShadersInitialized{ false };
    int mWindowWidth{ 1280 };
    int mWindowHeight{ 720 };

    /// Initialize GPU graphics resources for compute shader rendering
    void initializeGraphicsResources() noexcept;

    /// Create textures for path tracing (accumulation + display)
    void createPathTracerTextures() noexcept;

    /// Render using compute shaders (path tracing)
    void renderWithComputeShaders() const noexcept;

    /// Check if camera moved and reset accumulation if needed
    bool checkCameraMovement() const noexcept;

    /// Update listener position and remove stopped sounds
    void updateSounds() noexcept;

    /// Clean up OpenGL resources
    void cleanupResources() noexcept;
};

#endif // GAME_STATE_HPP
