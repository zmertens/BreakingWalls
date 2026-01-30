#ifndef GAME_STATE_HPP
#define GAME_STATE_HPP

#include "State.hpp"
#include "World.hpp"
#include "Shader.hpp"
#include "Camera.hpp"
#include "Sphere.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

class Player;
class StateStack;
union SDL_Event;

/// @brief Main game state managing physics, rendering, and input
/// @details Orchestrates World updates, player input, and GPU-accelerated graphics pipeline
/// Following OpenGL 4.3 compute shader approach from zmertens-compute
class GameState : public State
{
public:
    explicit GameState(StateStack& stack, Context context);
    ~GameState() override;

    void draw() const noexcept override;
    bool update(float dt, unsigned int subSteps) noexcept override;
    bool handleEvent(const SDL_Event& event) noexcept override;

private:
    World mWorld;
    Player& mPlayer;
    
    // OpenGL 4.3 pipeline resources (following Compute.cpp approach)
    std::unique_ptr<Shader> mDisplayShader;      // Vertex/Fragment shader for display
    std::unique_ptr<Shader> mComputeShader;      // Compute shader for path tracing
    
    // Path tracer camera (independent from player for 3D scene navigation)
    Camera mCamera;
    
    // Scene data
    std::vector<Sphere> mSpheres;
    GLuint mShapeSSBO{0};     // Shader Storage Buffer Object for spheres
    
    // Textures for progressive path tracing
    GLuint mVAO{0};           // Vertex Array Object for fullscreen quad
    GLuint mAccumTex{0};      // Accumulation texture for progressive rendering
    GLuint mDisplayTex{0};    // Display texture for final output
    
    // Progressive rendering state
    mutable uint32_t mCurrentBatch{0};
    uint32_t mSamplesPerBatch{4};
    uint32_t mTotalBatches{250};
    
    // Camera movement tracking for accumulation reset
    mutable glm::vec3 mLastCameraPosition;
    mutable float mLastCameraYaw{0.0f};
    mutable float mLastCameraPitch{0.0f};
    
    bool mShadersInitialized{false};
    int mWindowWidth{1280};
    int mWindowHeight{720};
    
    /// Initialize GPU graphics resources for compute shader rendering
    void initializeGraphicsResources() noexcept;
    
    /// Initialize shaders following Compute.cpp approach
    bool initializeShaders() noexcept;
    
    /// Create textures for path tracing (accumulation + display)
    void createPathTracerTextures() noexcept;
    
    /// Initialize path tracer scene with spheres
    void initPathTracerScene() noexcept;
    
    /// Render using compute shaders (path tracing)
    void renderWithComputeShaders() const noexcept;
    
    /// Check if camera moved and reset accumulation if needed
    bool checkCameraMovement() const noexcept;
    
    /// Clean up OpenGL resources
    void cleanupResources() noexcept;
    
    /// Helper to generate random float in range
    static float getRandomFloat(float low, float high) noexcept;
};

#endif // GAME_STATE_HPP
