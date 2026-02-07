//
// PhysicsGame class implementation
// Simple 2D physics simulation with bouncy balls that break walls
// Navigate from start to finish in a time-sensitive race
//

#include "PhysicsGame.hpp"

#include <cmath>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include <dearimgui/imgui.h>
#include <dearimgui/backends/imgui_impl_sdl3.h>
#include <dearimgui/backends/imgui_impl_opengl3.h>

#include "Font.hpp"
#include "GameState.hpp"
#include "LoadingState.hpp"
#include "MenuState.hpp"
#include "MusicPlayer.hpp"
#include "Player.hpp"
#include "PauseState.hpp"
#include "SettingsState.hpp"
#include "RenderWindow.hpp"
#include "ResourceIdentifiers.hpp"
#include "ResourceManager.hpp"
#include "GLSDLHelper.hpp"
#include "SplashState.hpp"
#include "State.hpp"
#include "StateStack.hpp"
#include "Texture.hpp"
#include "SoundPlayer.hpp"

struct PhysicsGame::PhysicsGameImpl
{
    Player p1;

    std::unique_ptr<RenderWindow> window;
    std::unique_ptr<StateStack> stateStack;

    GLSDLHelper glSdlHelper;

    FontManager fonts;
    MusicManager music;
    SoundBufferManager soundBuffers;
    std::unique_ptr<SoundPlayer> sounds;
    ShaderManager shaders;
    TextureManager textures;

    // FPS smoothing variables
    mutable double fpsUpdateTimer = 0.0;
    mutable int smoothedFps = 0;
    mutable float smoothedFrameTime = 0.0f;
    static constexpr double FPS_UPDATE_INTERVAL = 250.0;

    std::string windowTitle;
    std::string resourcePath;
    const int INIT_WINDOW_W, INIT_WINDOW_H;

    PhysicsGameImpl(std::string_view title, int w, int h, std::string_view resourcePath = "")
        : windowTitle{ title }
        , resourcePath{ resourcePath }
        , INIT_WINDOW_W{ w }, INIT_WINDOW_H{ h }
        , window{ nullptr }
        , glSdlHelper{}
        , stateStack{ nullptr }
        , sounds{ nullptr }
    {
        initSDL();
        if (!glSdlHelper.mWindow)
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create SDL window - cannot continue");
            // Don't initialize further objects if SDL failed
            return;
        }

        window = std::make_unique<RenderWindow>(glSdlHelper.mWindow);

        initDearImGui();

        // Create SoundPlayer with reference to soundBuffers
        sounds = std::make_unique<SoundPlayer>(soundBuffers);

        stateStack = std::make_unique<StateStack>(State::Context{
            *window,
            std::ref(fonts),
            std::ref(music),
            std::ref(soundBuffers),
            *sounds,
            std::ref(shaders),
            std::ref(textures),
            std::ref(p1)
            });

        registerStates();

        stateStack->pushState(States::ID::LOADING);
        stateStack->pushState(States::ID::SPLASH);
    }

    ~PhysicsGameImpl()
    {
        if (auto& sdl = this->glSdlHelper; sdl.mWindow)
        {
            this->stateStack->clearStates();
            this->fonts.clear();
            this->music.clear();
            this->soundBuffers.clear();
            this->sounds.reset();
            this->shaders.clear();
            this->textures.clear();

            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();

            sdl.destroyAndQuit();
        }
    }

    void initSDL() noexcept
    {
        auto title = windowTitle.empty() ? "Breaking Walls" : windowTitle.c_str();
        glSdlHelper.init(title, INIT_WINDOW_W, INIT_WINDOW_H);
    }

    void initDearImGui() const noexcept
    {
        // Initialize ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // Setup ImGui style
        ImGui::StyleColorsDark();

        // Initialize ImGui SDL3 and OpenGL3 backends
        ImGui_ImplSDL3_InitForOpenGL(this->glSdlHelper.mWindow, this->glSdlHelper.mGLContext);
        ImGui_ImplOpenGL3_Init("#version 430");
    }

    void processInput() const noexcept
    {
        SDL_Event event;

        while (SDL_PollEvent(&event))
        {
            // Let ImGui process the event first
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT)
            {
                SDL_Log("SDL_EVENT_QUIT received - clearing state stack");
                stateStack->clearStates();
                // Don't process any more events after quit
                return;
            }

            // Only handle events if state stack is not empty
            if (!stateStack->isEmpty())
            {
                stateStack->handleEvent(event);
            }
        }
    }

    void update(const float dt, int subSteps = 4) const noexcept
    {
        stateStack->update(dt, subSteps);
    }

    void render(const double elapsed) const noexcept
    {
        // Only render if state stack has states
        if (stateStack->isEmpty())
        {
            return;
        }

        // Clear, draw, and present (like SFML)
        window->clear();
        window->beginFrame();
        stateStack->draw();

#if defined(BREAKING_WALLS_DEBUG)
        // Window might be closed during draw calls/events
        if (window->isOpen())
        {
            this->handleFPS(elapsed);
        }
#endif

        window->display();
    }

    void registerStates() noexcept
    {
        stateStack->registerState<GameState>(States::ID::GAME);
        stateStack->registerState<LoadingState>(States::ID::LOADING, resourcePath);
        stateStack->registerState<MenuState>(States::ID::MENU);
        stateStack->registerState<PauseState>(States::ID::PAUSE);
        stateStack->registerState<SettingsState>(States::ID::SETTINGS);
        stateStack->registerState<SplashState>(States::ID::SPLASH);
    }

    void handleFPS(const double elapsed) const noexcept
    {
        // Calculate instantaneous FPS and frame time
        const auto fps = static_cast<int>(1000.0 / elapsed);
        const auto frameTime = static_cast<float>(elapsed);

        // Update smoothed values periodically for display
        fpsUpdateTimer += elapsed;
        if (fpsUpdateTimer >= FPS_UPDATE_INTERVAL)
        {
            smoothedFps = fps;
            smoothedFrameTime = frameTime;
            fpsUpdateTimer = 0.0;
        }

        // Create ImGui overlay window positioned at top-right corner
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10.0f, 10.0f), ImGuiCond_Always,
            ImVec2(1.0f, 0.0f));

        // Set window background to be semi-transparent
        ImGui::SetNextWindowBgAlpha(0.65f);

        // Create window with no title bar, no resize, no move, auto-resize
        constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoMove;

        if (ImGui::Begin("FPS Overlay", nullptr, windowFlags))
        {
            ImGui::Text("FPS: %d", smoothedFps);
            ImGui::Text("Frame Time: %.2f ms", smoothedFrameTime);
            ImGui::End();
        }
    }
}; // impl

// resourcePath = ""
PhysicsGame::PhysicsGame(std::string_view title, int w, int h, std::string_view resourcePath)
    : mImpl{ std::make_unique<PhysicsGameImpl>(title, w, h, resourcePath) }
{
}

PhysicsGame::~PhysicsGame() = default;

// Main game loop
bool PhysicsGame::run([[maybe_unused]] mazes::grid_interface* g, mazes::randomizer& rng) const noexcept
{
    auto&& gamePtr = this->mImpl;

    // Check if initialization succeeded before entering game loop
    if (!gamePtr->window || !gamePtr->stateStack)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Game initialization failed - cannot run");
        return false;
    }

    auto previous = static_cast<double>(SDL_GetTicks());
    double accumulator = 0.0, currentTimeStep = 0.0;

    SDL_Log("Entering game loop...");

    // States already pushed in constructor - no need to push again

    while (gamePtr->window && gamePtr->window->isOpen())
    {
        // Expected milliseconds per frame (16.67ms)
        static constexpr auto FIXED_TIME_STEP = 1000.0 / 60.0;
        const auto current = static_cast<double>(SDL_GetTicks());
        const auto elapsed = current - previous;
        previous = current;
        accumulator += elapsed;

        // Handle events and update physics at a fixed time step
        while (accumulator >= FIXED_TIME_STEP)
        {
            gamePtr->processInput();

            accumulator -= FIXED_TIME_STEP;

            currentTimeStep += FIXED_TIME_STEP;

            gamePtr->update(static_cast<float>(FIXED_TIME_STEP) / 1000.f);

            // Check if state stack became empty during update
            if (gamePtr->stateStack->isEmpty())
            {
                SDL_Log("State stack is empty after update - closing application");
                gamePtr->window->close();
                break;
            }
        }

        // Exit outer loop if window was closed in update loop
        if (!gamePtr->window->isOpen())
        {
            break;
        }

        gamePtr->render(elapsed);
    }

    SDL_Log("Exiting game loop...");
    return true;
}

