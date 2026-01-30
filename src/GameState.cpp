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
#include "GLUtils.hpp"

GameState::GameState(StateStack& stack, Context context)
    : State{stack, context}
      , mWorld{*context.window, *context.fonts, *context.textures}
      , mPlayer{*context.player}
      , mDisplayShader{nullptr}
      , mComputeShader{nullptr}
{
    mPlayer.setActive(true);
    mWorld.init();
    mWorld.setPlayer(context.player);
    
    // Initialize GPU graphics pipeline following Compute.cpp approach
    initializeGraphicsResources();
}

GameState::~GameState()
{
    cleanupResources();
}

void GameState::draw() const noexcept
{
    if (mShadersInitialized)
    {
        // Use compute shader rendering pipeline
        renderWithComputeShaders();
    }
    
    // Always draw the world (physics objects, sprites, etc.)
    mWorld.draw();
}

void GameState::initializeGraphicsResources() noexcept
{
    SDL_Log("GameState: Initializing OpenGL 4.3 graphics pipeline...");
    
    // Enable OpenGL features (following Compute.cpp)
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    
    // Register debug callback if available (OpenGL 4.3+)
    glDebugMessageCallback(GLUtils::GlDebugCallback, nullptr);
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    SDL_Log("GameState: OpenGL Debug Output enabled");
    
    // Initialize shaders
    if (initializeShaders())
    {
        // Create VAO for fullscreen quad (like Compute.cpp)
        glGenVertexArrays(1, &mVAO);
        glBindVertexArray(mVAO);
        
        // Create screen texture for compute shader output
        createScreenTexture();
        
        mShadersInitialized = true;
        SDL_Log("GameState: Shaders and OpenGL resources initialized successfully");
    }
    else
    {
        SDL_Log("GameState: Shader initialization failed, using fallback rendering");
        mShadersInitialized = false;
    }
    
    SDL_Log("GameState: Graphics pipeline initialization complete");
}

bool GameState::initializeShaders() noexcept
{
    try
    {
        // Create display shader (vertex + fragment) following Compute.cpp
        mDisplayShader = std::make_unique<Shader>();
        mDisplayShader->compileAndAttachShader(ShaderType::VERTEX, "./shaders/raytracer.vert.glsl");
        mDisplayShader->compileAndAttachShader(ShaderType::FRAGMENT, "./shaders/raytracer.frag.glsl");
        mDisplayShader->linkProgram();
        
        SDL_Log("GameState: Display shader compiled and linked");
        SDL_Log("%s", mDisplayShader->getGlslUniforms().c_str());
        SDL_Log("%s", mDisplayShader->getGlslAttribs().c_str());
        
        // Create compute shader for path tracing (following Compute.cpp)
        mComputeShader = std::make_unique<Shader>();
        mComputeShader->compileAndAttachShader(ShaderType::COMPUTE, "./shaders/pathtracer.cs.glsl");
        mComputeShader->linkProgram();
        
        SDL_Log("GameState: Compute shader compiled and linked");
        SDL_Log("%s", mComputeShader->getGlslUniforms().c_str());
        
        return true;
    }
    catch (const std::exception& e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GameState: Shader initialization failed: %s", e.what());
        mDisplayShader.reset();
        mComputeShader.reset();
        return false;
    }
}

void GameState::createScreenTexture() noexcept
{
    // Create screen texture for compute shader output (following Compute.cpp)
    glGenTextures(1, &mScreenTex);
    glBindTexture(GL_TEXTURE_2D, mScreenTex);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Allocate texture storage for compute shader output
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, mWindowWidth, mWindowHeight, 0,
                 GL_RGBA, GL_FLOAT, nullptr);
    
    SDL_Log("GameState: Screen texture created (%dx%d)", mWindowWidth, mWindowHeight);
}

void GameState::renderWithComputeShaders() const noexcept
{
    if (!mComputeShader || !mDisplayShader)
    {
        return;
    }
    
    // Bind compute shader and dispatch (following Compute.cpp render pattern)
    mComputeShader->bind();
    
    // Set compute shader uniforms
    double time = static_cast<double>(SDL_GetTicks()) / 1000.0;
    mComputeShader->setUniform("uTime", time);
    
    // Bind screen texture as image for compute shader output
    glBindImageTexture(0, mScreenTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    
    // Dispatch compute shader with work groups (following Compute.cpp)
    // Using 20x20 local work group size
    GLuint groupsX = (mWindowWidth + 19) / 20;
    GLuint groupsY = (mWindowHeight + 19) / 20;
    glDispatchCompute(groupsX, groupsY, 1);
    
    // Memory barrier to ensure compute shader writes are visible
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    
    // Render fullscreen quad with the computed texture
    mDisplayShader->bind();
    mDisplayShader->setUniform("uTexture2D", 0);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mScreenTex);
    glBindVertexArray(mVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void GameState::cleanupResources() noexcept
{
    if (mVAO != 0)
    {
        glDeleteVertexArrays(1, &mVAO);
        mVAO = 0;
    }
    
    if (mScreenTex != 0)
    {
        glDeleteTextures(1, &mScreenTex);
        mScreenTex = 0;
    }
    
    mDisplayShader.reset();
    mComputeShader.reset();
    
    SDL_Log("GameState: OpenGL resources cleaned up");
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
