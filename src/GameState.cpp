#include "GameState.hpp"

#include <SDL3/SDL.h>

#include <functional>
#include <vector>

#include <glm/glm.hpp>
#include <glad/glad.h>

#include <MazeBuilder/create.h>

#include "CommandQueue.hpp"
#include "Player.hpp"
#include "StateStack.hpp"
#include "Texture.hpp"
#include "Shader.hpp"
#include "Camera.hpp"
#include "Material.hpp"
#include "Light.hpp"
#include "Sphere.hpp"
#include "Plane.hpp"
#include "GLUtils.hpp"

GameState::GameState(StateStack& stack, Context context)
    : State{stack, context}
      , mWorld{*context.window, *context.fonts, *context.textures}
      , mPlayer{*context.player}
{
    mPlayer.setActive(true);
    mWorld.init();
    mWorld.setPlayer(context.player);
    
    // Initialize GPU graphics pipeline for compute shader rendering
    initializeGraphicsResources();
}

void GameState::draw() const noexcept
{
    mWorld.draw();
}

void GameState::initializeGraphicsResources() noexcept
{
    // Enable multisampling for better visual quality
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    
    // Register debug callback if available (OpenGL 4.3+)
#if APP_OPENGL_MAJOR >= 4 && APP_OPENGL_MINOR >= 3
    glDebugMessageCallback(GLUtils::GlDebugCallback, nullptr);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    SDL_Log("OpenGL Debug Output enabled");
#endif
    
    SDL_Log("Graphics resources initialized for compute shader rendering");
}

bool GameState::update(float dt, unsigned int subSteps) noexcept
{
    mWorld.update(dt);

    auto& commands = mWorld.getCommandQueue();
    mPlayer.handleRealtimeInput(std::ref(commands));

    return true;
}

bool GameState::handleEvent(const SDL_Event& event) noexcept
{
    auto& commands = mWorld.getCommandQueue();

    mPlayer.handleEvent(event, std::ref(commands));
    mWorld.handleEvent(event);

    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        if (event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            requestStackPush(States::ID::PAUSE);
        }
    }

    return true;
}

void GameState::renderWithComputeShaders() noexcept
{
    // This method demonstrates how to integrate compute shader rendering
    // with the existing World/Physics rendering pipeline
    
    // Optional: Create compute shaders for path tracing or raytracing effects
    // The shaders would be loaded from: shaders/pathtracer.cs.glsl or raytracer.cs.glsl
    
    // Example compute shader setup (when shaders are created):
    // Shader computeShader;
    // computeShader.compileAndAttachShader(ShaderType::COMPUTE, "./shaders/pathtracer.cs.glsl");
    // computeShader.linkProgram();
    
    // For now, continue with traditional rendering
    mWorld.draw();
}
