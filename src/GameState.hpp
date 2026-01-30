#ifndef GAME_STATE_HPP
#define GAME_STATE_HPP

#include "State.hpp"
#include "World.hpp"
#include "Shader.hpp"

#include <glad/glad.h>
#include <memory>

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
    std::unique_ptr<Shader> mComputeShader;      // Compute shader for effects
    
    GLuint mVAO{0};           // Vertex Array Object for fullscreen quad
    GLuint mScreenTex{0};     // Screen texture for compute shader output
    
    bool mShadersInitialized{false};
    int mWindowWidth{1280};
    int mWindowHeight{720};
    
    /// Initialize GPU graphics resources for compute shader rendering
    void initializeGraphicsResources() noexcept;
    
    /// Initialize shaders following Compute.cpp approach
    bool initializeShaders() noexcept;
    
    /// Create screen texture for compute shader output
    void createScreenTexture() noexcept;
    
    /// Render using compute shaders (path tracing, raytracing, etc.)
    void renderWithComputeShaders() const noexcept;
    
    /// Clean up OpenGL resources
    void cleanupResources() noexcept;
};

#endif // GAME_STATE_HPP
