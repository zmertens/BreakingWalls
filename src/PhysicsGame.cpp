//
// PhysicsGame class implementation
// Simple 2D physics simulation with bouncy balls that break walls
// Navigate from start to finish in a time-sensitive race
//

#include "PhysicsGame.hpp"

#include <cmath>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <string>
#include <vector>
#include <unordered_map>

#include <SFML/Window.hpp>

#include <dearimgui/imgui.h>
#include <dearimgui/backends/imgui_impl_sfml.h>
#include <dearimgui/backends/imgui_impl_opengl3.h>

#include "Font.hpp"
#include "GameState.hpp"
#include "GLSDLHelper.hpp"
#include "HttpClient.hpp"
#include "JSONUtils.hpp"
#include "Level.hpp"
#include "LoadingState.hpp"
#include "MenuState.hpp"
#include "MultiplayerGameState.hpp"
#include "MusicPlayer.hpp"
#include "Options.hpp"
#include "PauseState.hpp"
#include "Player.hpp"
#include "RenderWindow.hpp"
#include "ResourceIdentifiers.hpp"
#include "ResourceManager.hpp"
#include "Shader.hpp"
#include "SoundPlayer.hpp"
#include "SplashState.hpp"
#include "State.hpp"
#include "StateStack.hpp"
#include "Texture.hpp"

struct PhysicsGame::PhysicsGameImpl
{
    Player mPlayer1;
    HttpClient mHttpClient;

    std::unique_ptr<RenderWindow> mRenderWindow;
    std::unique_ptr<StateStack> mStateStack;

    GLSDLHelper mGLSDLHelper;
    ModelsManager mModels;
    FontManager mFonts;
    LevelsManager mLevels;
    MusicManager mMusic;
    OptionsManager mOptions;
    SoundBufferManager mSoundBuffers;
    std::unique_ptr<SoundPlayer> mSounds;
    ShaderManager mShaders;
    TextureManager mTextures;
    VAOManager mVAOs;
    FBOManager mFBOs;
    VBOManager mVBOs;

    // FPS smoothing variables
    mutable double mFPSUpdateTimer = 0.0;
    mutable int mSmoothedFPS = 0;
    mutable float mSmoothedFrameTime = 0.0f;
    static constexpr double FPS_UPDATE_INTERVAL = 250.0;

    std::string mWindowTitle;
    std::string mResourcePath;
    const int INIT_WINDOW_W, INIT_WINDOW_H;
    PhysicsGameImpl(std::string_view title, int w, int h, std::string_view resourcePath = "")
        : mWindowTitle{title}, mResourcePath{resourcePath}, INIT_WINDOW_W{w}, INIT_WINDOW_H{h}, mRenderWindow{nullptr}, mGLSDLHelper{}, mStateStack{nullptr}, mOptions{}, mSounds{nullptr}, mPlayer1{}, mHttpClient{}
    {
        initSDL();
        if (!mGLSDLHelper.getWindow())
        {
            std::cerr << "GLSDLHelper: Failed to create window - cannot continue\n";
            // Don't initialize further objects if SDL failed
            return;
        }

        mRenderWindow = std::make_unique<RenderWindow>(mGLSDLHelper.getWindow());
        mSounds = std::make_unique<SoundPlayer>(mSoundBuffers);

        initDearImGui();
        initOptions();

        mStateStack = std::make_unique<StateStack>(
            State::Context()
                .withRenderWindow(*mRenderWindow)
                .withFontManager(mFonts)
                .withLevelsManager(mLevels)
                .withModelsManager(mModels)
                .withMusicManager(mMusic)
                .withOptionsManager(mOptions)
                .withSoundBufferManager(mSoundBuffers)
                .withSoundPlayer(*mSounds)
                .withShaderManager(mShaders)
                .withTextureManager(mTextures)
                .withVAOManager(mVAOs)
                .withFBOManager(mFBOs)
                .withVBOManager(mVBOs)
                .withPlayer(mPlayer1)
                .withHttpClient(mHttpClient));

        registerStates();

        mStateStack->pushState(States::ID::LOADING);
    }

    ~PhysicsGameImpl()
    {
        if (auto &&sdl = mGLSDLHelper; sdl.getWindow())
        {
            if (mStateStack)
            {
                mStateStack.reset();
            }

            mFonts.clear();
            mLevels.clear();
            mMusic.clear();
            mSoundBuffers.clear();
            mSounds.reset();
            mShaders.clear();
            mTextures.clear();
            mVAOs.clear();
            mFBOs.clear();
            mVBOs.clear();

            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplSFML_Shutdown();
            ImGui::DestroyContext();

            sdl.destroyAndQuit();
        }
    }

    void initSDL() noexcept
    {
        auto title = mWindowTitle.empty() ? "Breaking Walls" : mWindowTitle.c_str();
        mGLSDLHelper.init(title, INIT_WINDOW_W, INIT_WINDOW_H);
    }

    void initDearImGui() const noexcept
    {
        // Initialize ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // Setup ImGui style
        ImGui::StyleColorsDark();

        // Initialize ImGui SDL3 and OpenGL3 backends
        ImGui_ImplSFML_Init(mGLSDLHelper.getWindow());
        ImGui_ImplOpenGL3_Init("#version 430");
    }

    void initOptions() noexcept
    {
        auto defaultOptions = std::make_unique<Options>();
        mOptions.insert(GUIOptions::ID::DE_FACTO, std::move(defaultOptions));
    }

    void processInput() const noexcept
    {
        if (!mStateStack || !mGLSDLHelper.getWindow()) return;

        while (auto event = mGLSDLHelper.getWindow()->pollEvent())
        {
            ImGui_ImplSFML_ProcessEvent(*event);
            if (event->is<sf::Event::Closed>())
            {
                std::cout << "Window close requested\n";
                mStateStack->clearStates();
                return;
            }
            if (!mStateStack->isEmpty())
                mStateStack->handleEvent(*event);
        }
    }

    void update(const float dt, int subSteps = 4) const noexcept
    {
        if (!mStateStack)
        {
            return;
        }

        mStateStack->update(dt, subSteps);
    }

    void render(const double elapsed) const noexcept
    {
        // Only render if state stack has states
        if (!mRenderWindow || !mStateStack || !mRenderWindow->isOpen() || mStateStack->isEmpty())
        {
            return;
        }

        // Clear, draw, and present (like SFML)
        mRenderWindow->clear();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSFML_NewFrame();
        ImGui::NewFrame();

        mStateStack->draw();

        // Window might be closed during draw calls/events
        if (mOptions.get(GUIOptions::ID::DE_FACTO).getShowDebugOverlay())
        {
            handleFPS(elapsed);
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        mRenderWindow->display();
    }

    void registerStates() noexcept
    {
        if (!mStateStack)
        {
            return;
        }

        mStateStack->registerState<GameState>(States::ID::GAME);
        mStateStack->registerState<LoadingState>(States::ID::LOADING, mResourcePath);
        mStateStack->registerState<MenuState>(States::ID::MENU);
        mStateStack->registerState<MultiplayerGameState>(States::ID::MULTIPLAYER_GAME);
        mStateStack->registerState<PauseState>(States::ID::PAUSE);
        mStateStack->registerState<SplashState>(States::ID::SPLASH);
    }

    void handleFPS(const double elapsed) const noexcept
    {
        // Calculate instantaneous FPS and frame time
        const auto fps = static_cast<int>(1000.0 / elapsed);
        const auto frameTime = static_cast<float>(elapsed);

        // Update smoothed values periodically for display
        mFPSUpdateTimer += elapsed;
        if (mFPSUpdateTimer >= FPS_UPDATE_INTERVAL)
        {
            mSmoothedFPS = fps;
            mSmoothedFrameTime = frameTime;
            mFPSUpdateTimer = 0.0;
        }

        if (auto *nunitoFont = mFonts.get(Fonts::ID::NUNITO_SANS).get())
        {
            ImGui::PushFont(nunitoFont);
        }

        // Create ImGui overlay mRenderWindow positioned at top-right corner
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10.0f, 10.0f), ImGuiCond_Always,
                                ImVec2(1.0f, 0.0f));

        // Set mRenderWindow background to be semi-transparent
        ImGui::SetNextWindowBgAlpha(0.65f);

        // Create mRenderWindow with no title bar, no resize, no move, auto-resize
        constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration |
                                                 ImGuiWindowFlags_AlwaysAutoResize |
                                                 ImGuiWindowFlags_NoSavedSettings |
                                                 ImGuiWindowFlags_NoFocusOnAppearing |
                                                 ImGuiWindowFlags_NoNav |
                                                 ImGuiWindowFlags_NoMove;

        if (ImGui::Begin("FPS Overlay", nullptr, windowFlags))
        {
            ImGui::Text("FPS: %d", mSmoothedFPS);
            ImGui::Text("Frame Time: %.2f ms", mSmoothedFrameTime);
            ImGui::Separator();

            if (mStateStack && mStateStack->peekState<GameState *>())
            {
                if (auto *gameState = mStateStack->peekState<GameState *>())
                {
                    const auto windowSize = gameState->getWindowDimensions();
                    const auto renderSize = gameState->getRenderDimensions();
                    const float renderScale = gameState->getRenderScale();

                    ImGui::Separator();
                    ImGui::Text("Window: %d x %d", windowSize.x, windowSize.y);
                    ImGui::Text("Render: %d x %d", renderSize.x, renderSize.y);
                    ImGui::Text("Scale: %.2fx", renderScale);
                }
            }

            ImGui::End();
        }
        ImGui::PopFont();
    }
}; // impl

// mResourcePath = ""
PhysicsGame::PhysicsGame(std::string_view title, int w, int h, std::string_view mResourcePath)
    : mImpl{std::make_unique<PhysicsGameImpl>(title, w, h, mResourcePath)}
{
}

PhysicsGame::~PhysicsGame() = default;

// Main game loop
bool PhysicsGame::run([[maybe_unused]] mazes::grid_interface *g, mazes::randomizer &rng) const noexcept
{
    auto &&gamePtr = mImpl;

    // Check if initialization succeeded before entering game loop
    if (!gamePtr->mRenderWindow || !gamePtr->mStateStack)
    {
        std::cerr << "Game initialization failed - cannot run\n";
        return false;
    }

    sf::Clock gameClock; auto previousTime = gameClock.getElapsedTime();
    double accumulator = 0.0, currentTimeStep = 0.0;

    std::cout << "Entering game loop...\n";

    // States already pushed in constructor - no need to push again

    while (gamePtr->mRenderWindow && gamePtr->mRenderWindow->isOpen())
    {
        // Expected milliseconds per frame (16.67ms)
        static constexpr auto FIXED_TIME_STEP = 1000.0 / 60.0;
        auto currentTime = gameClock.getElapsedTime();
        const auto elapsed = static_cast<double>((currentTime - previousTime).asMilliseconds());
        previousTime = currentTime;
        accumulator += elapsed;

        // Handle events and update physics at a fixed time step
        while (accumulator >= FIXED_TIME_STEP)
        {
            gamePtr->processInput();

            accumulator -= FIXED_TIME_STEP;

            currentTimeStep += FIXED_TIME_STEP;

            gamePtr->update(static_cast<float>(FIXED_TIME_STEP) / 1000.f);

            // Check if state stack became empty during update
            if (gamePtr->mStateStack->isEmpty())
            {
                std::cout << "State stack is empty after update - closing application\n";
                gamePtr->mRenderWindow->close();
                break;
            }
        }
        if (gamePtr->mRenderWindow->isOpen())
        {
            gamePtr->render(elapsed);
        }
    }

    std::cout << "Exiting game loop...\n";
    return true;
}
