#include "MenuState.hpp"

#include <SDL3/SDL.h>

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

#include <dearimgui/imgui.h>

#include <glm/gtc/matrix_transform.hpp>

#include "Font.hpp"
#include "GameState.hpp"
#include "MusicPlayer.hpp"
#include "Options.hpp"
#include "Player.hpp"
#include "ResourceIdentifiers.hpp"
#include "ResourceManager.hpp"
#include "Shader.hpp"
#include "SoundPlayer.hpp"
#include "StateStack.hpp"

MenuState::MenuState(StateStack &stack, Context context)
    : State(stack, context),
      mSelectedMenuItem(MenuItem::NEW_GAME),
      mConfirmedMenuItem(MenuItem::NEW_GAME),
      mActiveTab(MenuTab::NAVIGATION),
      mShowMainMenu(true),
      mPendingMenuAction(false),
      mItemSelectedFlags{},
      mSettingsUi{},
      mFont{nullptr},
      mMusic{nullptr}
{
    // Load font with error handling
    try
    {
        mFont = &context.getFontManager()->get(Fonts::ID::NUNITO_SANS);
    }
    catch (const std::exception &e)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "MenuState: Failed to load font: %s", e.what());
        mFont = nullptr;
    }
    
    // Load menu music with error handling
    try
    {
        mMusic = &context.getMusicManager()->get(Music::ID::MENU_MUSIC);
        mMusic->play();
    }
    catch (const std::exception &e)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "MenuState: Failed to load menu music: %s", e.what());
        mMusic = nullptr;
    }
    
    // initialize selection flags so UI shows correct selected item
    mItemSelectedFlags.fill(false);
    mItemSelectedFlags[static_cast<size_t>(mSelectedMenuItem)] = true;
}

MenuState::~MenuState()
{
    if (mMusic)
    {
        mMusic->stop();
    }
    cleanupParticleScene();
}

void MenuState::draw() const noexcept
{
    initializeParticleScene();

    if (!mShowMainMenu)
    {
        return;
    }

    initializeSettingsUiFromOptions();

    if (mFont)
    {
        ImGui::PushFont(mFont->get());
    }

    pushSynthwaveStyle();

    ImGuiIO &io = ImGui::GetIO();
    const ImVec2 displaySize = io.DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(displaySize, ImGuiCond_Always);

    if (ImGui::Begin("Main Menu", &mShowMainMenu,
                     ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove))
    {
        const ImVec2 contentAvail = ImGui::GetContentRegionAvail();
        const float leftWidth = contentAvail.x * 0.35f;
        const float rightWidth = std::max(1.0f, contentAvail.x - leftWidth - ImGui::GetStyle().ItemSpacing.x);

        if (ImGui::BeginChild("MenuTabContent", ImVec2(leftWidth, 0.0f), true))
        {
            switch (mActiveTab)
            {
            case MenuTab::NAVIGATION:
                drawNavigationTab();
                break;
            case MenuTab::SETTINGS:
                drawSettingsTab();
                break;
            case MenuTab::PARTICLE_SCENE:
                drawParticleSceneTab();
                break;
            case MenuTab::STENCILS:
                drawStencilsTab();
                break;
            default:
                drawNavigationTab();
                break;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        if (ImGui::BeginChild("MenuParticlePreview", ImVec2(0.0f, 0.0f), true))
        {
            const ImVec2 imageSize = ImGui::GetContentRegionAvail();
            constexpr float kCinematicAspect = 2.39f;
            const float containerWidth = std::max(1.0f, imageSize.x);
            const float containerHeight = std::max(1.0f, imageSize.y);

            float drawWidth = containerWidth;
            float drawHeight = drawWidth / kCinematicAspect;

            if (drawHeight > containerHeight)
            {
                drawHeight = containerHeight;
                drawWidth = drawHeight * kCinematicAspect;
            }

            drawWidth = std::max(1.0f, drawWidth);
            drawHeight = std::max(1.0f, drawHeight);

            ensureParticleRenderTarget(static_cast<int>(drawWidth), static_cast<int>(drawHeight));
            renderParticleScene();

            if (mParticlesRenderTexture != 0 && imageSize.x > 1.0f && imageSize.y > 1.0f)
            {
                const float offsetX = (containerWidth - drawWidth) * 0.5f;
                const float offsetY = (containerHeight - drawHeight) * 0.5f;
                ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + offsetX, ImGui::GetCursorPosY() + offsetY));

                ImGui::Image(
                    static_cast<ImTextureID>(mParticlesRenderTexture),
                    ImVec2(drawWidth, drawHeight),
                    ImVec2(0.0f, 1.0f),
                    ImVec2(1.0f, 0.0f));
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();

    popSynthwaveStyle();

    if (mFont)
    {
        ImGui::PopFont();
    }
}

bool MenuState::update(float dt, unsigned int subSteps) noexcept
{
    // Periodic music health check
    static float musicCheckTimer = 0.0f;
    musicCheckTimer += dt;

    if (musicCheckTimer >= 5.0f) // Check every 5 seconds
    {
        musicCheckTimer = 0.0f;

        if (mMusic)
        {
            if (!mMusic->isPlaying())
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                            "MenuState: Music stopped unexpectedly! Attempting restart...");
                mMusic->play();
            }
        }
    }

    if (mShowMainMenu)
    {
        return false;
    }

    if (!mPendingMenuAction)
    {
        mShowMainMenu = true;
        return false;
    }

    switch (mConfirmedMenuItem)
    {
    case MenuItem::CONTINUE:
        if (getStack().peekState<GameState *>() != nullptr)
        {
            requestStackPop();
        }
        else
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "MenuState: Cannot continue - no game in progress. Starting new game.");
            requestStackPop();
            requestStackPush(States::ID::GAME);
        }
        break;

    case MenuItem::NEW_GAME:
        requestStateClear();
        requestStackPush(States::ID::GAME);
        break;

    case MenuItem::NETWORK_GAME:
        requestStateClear();
        requestStackPush(States::ID::MULTIPLAYER_GAME);
        break;

    case MenuItem::SETTINGS:
        mActiveTab = MenuTab::SETTINGS;
        break;

    case MenuItem::PARTICLE_SCENE:
        mActiveTab = MenuTab::PARTICLE_SCENE;
        break;

    case MenuItem::STENCILS:
        mActiveTab = MenuTab::STENCILS;
        break;

    case MenuItem::SPLASH:
        requestStateClear();
        requestStackPush(States::ID::SPLASH);
        mPendingMenuAction = false;
        mShowMainMenu = true;
        return true;

    case MenuItem::QUIT:
        requestStateClear();
        break;

    default:
        break;
    }

    mPendingMenuAction = false;
    mShowMainMenu = true;
    return false;
}

bool MenuState::handleEvent(const SDL_Event &event) noexcept
{
    if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        const int newWidth = event.window.data1;
        const int newHeight = event.window.data2;
        if (newWidth > 0 && newHeight > 0)
        {
            mWindowWidth = newWidth;
            mWindowHeight = newHeight;
            updateParticleProjection();
        }
    }

    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        if (event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            if (mActiveTab != MenuTab::NAVIGATION)
            {
                mActiveTab = MenuTab::NAVIGATION;
            }
        }
    }

    return false;
}

void MenuState::pushSynthwaveStyle() const noexcept
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 7.0f));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.058f, 0.027f, 0.114f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.078f, 0.039f, 0.149f, 0.84f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.165f, 0.055f, 0.294f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.263f, 0.098f, 0.451f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.365f, 0.094f, 0.569f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.612f, 0.208f, 0.851f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.765f, 0.341f, 0.965f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.184f, 0.459f, 0.808f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.208f, 0.610f, 0.961f, 0.72f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.341f, 0.733f, 1.0f, 0.84f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.109f, 0.055f, 0.184f, 0.88f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.165f, 0.090f, 0.278f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.220f, 0.114f, 0.369f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.846f, 0.965f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.337f, 0.761f, 1.0f, 0.55f));
}

void MenuState::popSynthwaveStyle() const noexcept
{
    ImGui::PopStyleColor(15);
    ImGui::PopStyleVar(4);
}

void MenuState::initializeSettingsUiFromOptions() const noexcept
{
    if (mSettingsUi.initialized)
    {
        return;
    }

    auto *optionsManager = getContext().getOptionsManager();
    if (optionsManager == nullptr)
    {
        mSettingsUi.initialized = true;
        return;
    }

    try
    {
        const auto &opts = optionsManager->get(GUIOptions::ID::DE_FACTO);
        mSettingsUi.masterVolume = opts.getMasterVolume();
        mSettingsUi.musicVolume = opts.getMusicVolume();
        mSettingsUi.sfxVolume = opts.getSfxVolume();
        mSettingsUi.vsync = opts.getVsync();
        mSettingsUi.fullscreen = opts.getFullscreen();
        mSettingsUi.antialiasing = opts.getAntiAliasing();
        mSettingsUi.enableMusic = opts.getEnableMusic();
        mSettingsUi.enableSound = opts.getEnableSound();
        mSettingsUi.showDebugOverlay = opts.getShowDebugOverlay();
        mSettingsUi.stencilOutlineEnabled = opts.getStencilOutlineEnabled();
        mSettingsUi.stencilOutlineWidth = opts.getStencilOutlineWidth();
        mSettingsUi.stencilOutlinePulseEnabled = opts.getStencilOutlinePulseEnabled();
        mSettingsUi.stencilOutlinePulseSpeed = opts.getStencilOutlinePulseSpeed();
        mSettingsUi.stencilOutlinePulseAmount = opts.getStencilOutlinePulseAmount();
        mSettingsUi.stencilOutlineColor = glm::vec3(
            opts.getStencilOutlineColorR(),
            opts.getStencilOutlineColorG(),
            opts.getStencilOutlineColorB());
        mSettingsUi.arcadeModeEnabled = opts.getArcadeModeEnabled();
        mSettingsUi.runnerSpeed = opts.getRunnerSpeed();
        mSettingsUi.runnerStrafeLimit = opts.getRunnerStrafeLimit();
        mSettingsUi.runnerStartingPoints = opts.getRunnerStartingPoints();
        mSettingsUi.runnerPickupMinValue = opts.getRunnerPickupMinValue();
        mSettingsUi.runnerPickupMaxValue = opts.getRunnerPickupMaxValue();
        mSettingsUi.runnerPickupSpacing = opts.getRunnerPickupSpacing();
        mSettingsUi.runnerObstaclePenalty = opts.getRunnerObstaclePenalty();
        mSettingsUi.runnerCollisionCooldown = opts.getRunnerCollisionCooldown();
        mSettingsUi.motionBlurBracket1Points = opts.getMotionBlurBracket1Points();
        mSettingsUi.motionBlurBracket2Points = opts.getMotionBlurBracket2Points();
        mSettingsUi.motionBlurBracket3Points = opts.getMotionBlurBracket3Points();
        mSettingsUi.motionBlurBracket4Points = opts.getMotionBlurBracket4Points();
        mSettingsUi.motionBlurBracket1Boost = opts.getMotionBlurBracket1Boost();
        mSettingsUi.motionBlurBracket2Boost = opts.getMotionBlurBracket2Boost();
        mSettingsUi.motionBlurBracket3Boost = opts.getMotionBlurBracket3Boost();
        mSettingsUi.motionBlurBracket4Boost = opts.getMotionBlurBracket4Boost();
    }
    catch (const std::exception &)
    {
    }

    mSettingsUi.initialized = true;
}

void MenuState::drawNavigationTab() const noexcept
{
    using std::array;
    using std::size_t;
    using std::string;

    ImGui::TextUnformatted("Navigation");
    ImGui::Separator();

    ImGui::Spacing();

    const array<string, static_cast<size_t>(MenuItem::COUNT)> menuItems = {
        "Resume", "Just run endlessly", "Multiplayer Game", "Settings", "Particle Scene", "Stencils", "Return to Splash Screen", "Quit"};

    const auto active = static_cast<size_t>(getContext().getPlayer()->isActive());
    for (size_t i{static_cast<size_t>(active ? 0 : 1)}; i < menuItems.size(); ++i)
    {
        const bool selected = (mSelectedMenuItem == static_cast<MenuItem>(i));
        if (ImGui::Selectable(menuItems[i].c_str(), selected, ImGuiSelectableFlags_None, ImVec2(0.0f, 30.0f)))
        {
            for (auto &flag : mItemSelectedFlags)
            {
                flag = false;
            }
            mItemSelectedFlags[i] = true;
            mSelectedMenuItem = static_cast<MenuItem>(i);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted("Selected:");
    ImGui::SameLine();
    ImGui::Text("%s", menuItems.at(static_cast<unsigned int>(mSelectedMenuItem)).c_str());

    ImGui::Spacing();
    if (ImGui::Button("Confirm Selection", ImVec2(220.0f, 40.0f)))
    {
        if (auto *soundPlayer = getContext().getSoundPlayer(); soundPlayer != nullptr)
        {
            soundPlayer->play(SoundEffect::ID::SELECT);
        }

        if (mSelectedMenuItem == MenuItem::SETTINGS)
        {
            mActiveTab = MenuTab::SETTINGS;
            return;
        }

        if (mSelectedMenuItem == MenuItem::PARTICLE_SCENE)
        {
            mActiveTab = MenuTab::PARTICLE_SCENE;
            return;
        }

        if (mSelectedMenuItem == MenuItem::STENCILS)
        {
            mActiveTab = MenuTab::STENCILS;
            return;
        }

        mConfirmedMenuItem = mSelectedMenuItem;
        mPendingMenuAction = true;
        mShowMainMenu = false;
    }
}

void MenuState::drawParticleSceneTab() const noexcept
{
    ImGui::TextUnformatted("Particle Scene");
    ImGui::Separator();
    ImGui::TextWrapped("Tune the background particle simulation used by the main menu.");
    ImGui::Spacing();

    drawParticleControls();

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::Button("Reset Simulation", ImVec2(170.0f, 36.0f)))
    {
        resetParticleSimulation();
        mParticleResetAccumulator = 0.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Back to Navigation", ImVec2(170.0f, 36.0f)))
    {
        mActiveTab = MenuTab::NAVIGATION;
    }
}

void MenuState::drawStencilsTab() const noexcept
{
    ImGui::TextUnformatted("Stencils");
    ImGui::Separator();
    ImGui::TextWrapped("Tune player stencil outline visibility and appearance in real-time.");
    ImGui::Spacing();

    ImGui::Checkbox("Enable Player Outline", &mSettingsUi.stencilOutlineEnabled);
    ImGui::SliderFloat("Outline Width", &mSettingsUi.stencilOutlineWidth, 0.0f, 0.20f, "%.3f");
    ImGui::Checkbox("Pulse Outline", &mSettingsUi.stencilOutlinePulseEnabled);
    ImGui::SliderFloat("Pulse Speed", &mSettingsUi.stencilOutlinePulseSpeed, 0.2f, 10.0f, "%.2f");
    ImGui::SliderFloat("Pulse Amount", &mSettingsUi.stencilOutlinePulseAmount, 0.0f, 0.8f, "%.2f");
    ImGui::ColorPicker3("Outline Color", &mSettingsUi.stencilOutlineColor.x, ImGuiColorEditFlags_PickerHueWheel);

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Apply Stencil Settings", ImVec2(190.0f, 36.0f)))
    {
        applySettingsFromUi();
    }
    ImGui::SameLine();
    if (ImGui::Button("Back to Navigation", ImVec2(170.0f, 36.0f)))
    {
        mActiveTab = MenuTab::NAVIGATION;
    }
}

void MenuState::drawSettingsTab() const noexcept
{
    ImGui::TextUnformatted("Settings");
    ImGui::Separator();

    ImGui::Checkbox("Enable Music", &mSettingsUi.enableMusic);
    ImGui::Checkbox("Enable Sound Effects", &mSettingsUi.enableSound);
    ImGui::SliderFloat("Master Volume", &mSettingsUi.masterVolume, 0.0f, 100.0f, "%.0f%%");
    ImGui::SliderFloat("Music Volume", &mSettingsUi.musicVolume, 0.0f, 100.0f, "%.0f%%");
    ImGui::SliderFloat("SFX Volume", &mSettingsUi.sfxVolume, 0.0f, 100.0f, "%.0f%%");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Graphics");
    ImGui::Checkbox("VSync", &mSettingsUi.vsync);
    ImGui::Checkbox("Fullscreen", &mSettingsUi.fullscreen);
    ImGui::Checkbox("Anti-Aliasing", &mSettingsUi.antialiasing);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Gameplay");
    ImGui::Checkbox("Enable Arcade Runner", &mSettingsUi.arcadeModeEnabled);
    ImGui::Checkbox("Show Debug Overlay", &mSettingsUi.showDebugOverlay);
    ImGui::SliderFloat("Runner Speed", &mSettingsUi.runnerSpeed, 5.0f, 100.0f, "%.1f");
    ImGui::SliderFloat("Strafe Limit", &mSettingsUi.runnerStrafeLimit, 5.0f, 100.0f, "%.1f");
    ImGui::SliderInt("Starting Points", &mSettingsUi.runnerStartingPoints, 1, 500);
    ImGui::SliderInt("Pickup Min Value", &mSettingsUi.runnerPickupMinValue, -100, 0);
    ImGui::SliderInt("Pickup Max Value", &mSettingsUi.runnerPickupMaxValue, 1, 150);
    ImGui::SliderFloat("Pickup Spacing", &mSettingsUi.runnerPickupSpacing, 4.0f, 50.0f, "%.1f");
    ImGui::SliderInt("Obstacle Penalty", &mSettingsUi.runnerObstaclePenalty, 1, 200);
    ImGui::SliderFloat("Collision Cooldown", &mSettingsUi.runnerCollisionCooldown, 0.05f, 2.0f, "%.2f s");

    ImGui::Spacing();
    ImGui::TextUnformatted("Motion Blur Score Brackets");
    ImGui::SliderInt("Blur Bracket 1 Points", &mSettingsUi.motionBlurBracket1Points, 0, 3000);
    ImGui::SliderInt("Blur Bracket 2 Points", &mSettingsUi.motionBlurBracket2Points, 0, 3000);
    ImGui::SliderInt("Blur Bracket 3 Points", &mSettingsUi.motionBlurBracket3Points, 0, 3000);
    ImGui::SliderInt("Blur Bracket 4 Points", &mSettingsUi.motionBlurBracket4Points, 0, 3000);
    ImGui::SliderFloat("Blur Bracket 1 Boost", &mSettingsUi.motionBlurBracket1Boost, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Blur Bracket 2 Boost", &mSettingsUi.motionBlurBracket2Boost, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Blur Bracket 3 Boost", &mSettingsUi.motionBlurBracket3Boost, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Blur Bracket 4 Boost", &mSettingsUi.motionBlurBracket4Boost, 0.0f, 1.0f, "%.2f");

    if (mSettingsUi.runnerPickupMinValue > mSettingsUi.runnerPickupMaxValue)
    {
        mSettingsUi.runnerPickupMinValue = mSettingsUi.runnerPickupMaxValue;
    }
    if (mSettingsUi.motionBlurBracket2Points < mSettingsUi.motionBlurBracket1Points)
    {
        mSettingsUi.motionBlurBracket2Points = mSettingsUi.motionBlurBracket1Points;
    }
    if (mSettingsUi.motionBlurBracket3Points < mSettingsUi.motionBlurBracket2Points)
    {
        mSettingsUi.motionBlurBracket3Points = mSettingsUi.motionBlurBracket2Points;
    }
    if (mSettingsUi.motionBlurBracket4Points < mSettingsUi.motionBlurBracket3Points)
    {
        mSettingsUi.motionBlurBracket4Points = mSettingsUi.motionBlurBracket3Points;
    }
    if (mSettingsUi.motionBlurBracket2Boost < mSettingsUi.motionBlurBracket1Boost)
    {
        mSettingsUi.motionBlurBracket2Boost = mSettingsUi.motionBlurBracket1Boost;
    }
    if (mSettingsUi.motionBlurBracket3Boost < mSettingsUi.motionBlurBracket2Boost)
    {
        mSettingsUi.motionBlurBracket3Boost = mSettingsUi.motionBlurBracket2Boost;
    }
    if (mSettingsUi.motionBlurBracket4Boost < mSettingsUi.motionBlurBracket3Boost)
    {
        mSettingsUi.motionBlurBracket4Boost = mSettingsUi.motionBlurBracket3Boost;
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::Button("Apply Settings", ImVec2(150.0f, 36.0f)))
    {
        applySettingsFromUi();
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Reset to Default", ImVec2(160.0f, 36.0f)))
    {
        resetSettingsToDefaults();
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Back to Navigation", ImVec2(170.0f, 36.0f)))
    {
        mActiveTab = MenuTab::NAVIGATION;
    }
}

void MenuState::drawParticleControls() const noexcept
{
    if (!ImGui::CollapsingHeader("Particle Scene", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    ImGui::SliderFloat("Gravity #1", &mParticleGravity1, 10.0f, 3000.0f, "%.1f");
    ImGui::SliderFloat("Gravity #2", &mParticleGravity2, 10.0f, 3000.0f, "%.1f");
    ImGui::SliderFloat("Orbit Speed", &mParticleSpeed, 1.0f, 120.0f, "%.1f");
    ImGui::SliderFloat("Max Distance", &mParticleMaxDist, 5.0f, 120.0f, "%.1f");
    ImGui::SliderFloat("DeltaT Scale", &mParticleDtScale, 0.01f, 1.0f, "%.3f");
    ImGui::SliderFloat("Reset Interval (s)", &mParticleResetIntervalSeconds, 5.0f, 30.0f, "%.1f");
    ImGui::SliderFloat("Particle Size", &mParticlePointSize, 1.0f, 6.0f, "%.1f");
    ImGui::SliderFloat("Attractor Size", &mAttractorPointSize, 2.0f, 14.0f, "%.1f");
}

void MenuState::resetSettingsToDefaults() const noexcept
{
    mSettingsUi.masterVolume = 100.0f;
    mSettingsUi.musicVolume = 80.0f;
    mSettingsUi.sfxVolume = 90.0f;
    mSettingsUi.vsync = true;
    mSettingsUi.fullscreen = false;
    mSettingsUi.antialiasing = true;
    mSettingsUi.enableMusic = true;
    mSettingsUi.enableSound = true;
    mSettingsUi.showDebugOverlay = false;
    mSettingsUi.stencilOutlineEnabled = true;
    mSettingsUi.stencilOutlineWidth = 0.05f;
    mSettingsUi.stencilOutlinePulseEnabled = false;
    mSettingsUi.stencilOutlinePulseSpeed = 2.4f;
    mSettingsUi.stencilOutlinePulseAmount = 0.28f;
    mSettingsUi.stencilOutlineColor = glm::vec3(0.38f, 0.94f, 1.0f);
    mSettingsUi.arcadeModeEnabled = true;
    mSettingsUi.runnerSpeed = 30.0f;
    mSettingsUi.runnerStrafeLimit = 35.0f;
    mSettingsUi.runnerStartingPoints = 100;
    mSettingsUi.runnerPickupMinValue = -25;
    mSettingsUi.runnerPickupMaxValue = 40;
    mSettingsUi.runnerPickupSpacing = 18.0f;
    mSettingsUi.runnerObstaclePenalty = 25;
    mSettingsUi.runnerCollisionCooldown = 0.40f;
    mSettingsUi.motionBlurBracket1Points = 300;
    mSettingsUi.motionBlurBracket2Points = 500;
    mSettingsUi.motionBlurBracket3Points = 800;
    mSettingsUi.motionBlurBracket4Points = 1200;
    mSettingsUi.motionBlurBracket1Boost = 0.10f;
    mSettingsUi.motionBlurBracket2Boost = 0.18f;
    mSettingsUi.motionBlurBracket3Boost = 0.28f;
    mSettingsUi.motionBlurBracket4Boost = 0.38f;
}

void MenuState::applySettingsFromUi() const noexcept
{
    Options options;
    options.withAntiAliasing(mSettingsUi.antialiasing)
        .withEnableMusic(mSettingsUi.enableMusic)
        .withEnableSound(mSettingsUi.enableSound)
        .withFullscreen(mSettingsUi.fullscreen)
        .withShowDebugOverlay(mSettingsUi.showDebugOverlay)
        .withStencilOutlineEnabled(mSettingsUi.stencilOutlineEnabled)
        .withStencilOutlinePulseEnabled(mSettingsUi.stencilOutlinePulseEnabled)
        .withStencilOutlineWidth(mSettingsUi.stencilOutlineWidth)
        .withStencilOutlinePulse(
            mSettingsUi.stencilOutlinePulseSpeed,
            mSettingsUi.stencilOutlinePulseAmount)
        .withStencilOutlineColor(
            mSettingsUi.stencilOutlineColor.r,
            mSettingsUi.stencilOutlineColor.g,
            mSettingsUi.stencilOutlineColor.b)
        .withVsync(mSettingsUi.vsync)
        .withMasterVolume(mSettingsUi.masterVolume)
        .withMusicVolume(mSettingsUi.musicVolume)
        .withSfxVolume(mSettingsUi.sfxVolume)
        .withArcadeModeEnabled(mSettingsUi.arcadeModeEnabled)
        .withRunnerSpeed(mSettingsUi.runnerSpeed)
        .withRunnerStrafeLimit(mSettingsUi.runnerStrafeLimit)
        .withRunnerStartingPoints(mSettingsUi.runnerStartingPoints)
        .withRunnerPickupMinValue(mSettingsUi.runnerPickupMinValue)
        .withRunnerPickupMaxValue(mSettingsUi.runnerPickupMaxValue)
        .withRunnerPickupSpacing(mSettingsUi.runnerPickupSpacing)
        .withRunnerObstaclePenalty(mSettingsUi.runnerObstaclePenalty)
        .withRunnerCollisionCooldown(mSettingsUi.runnerCollisionCooldown)
        .withMotionBlurBracket1Points(mSettingsUi.motionBlurBracket1Points)
        .withMotionBlurBracket2Points(mSettingsUi.motionBlurBracket2Points)
        .withMotionBlurBracket3Points(mSettingsUi.motionBlurBracket3Points)
        .withMotionBlurBracket4Points(mSettingsUi.motionBlurBracket4Points)
        .withMotionBlurBracket1Boost(mSettingsUi.motionBlurBracket1Boost)
        .withMotionBlurBracket2Boost(mSettingsUi.motionBlurBracket2Boost)
        .withMotionBlurBracket3Boost(mSettingsUi.motionBlurBracket3Boost)
        .withMotionBlurBracket4Boost(mSettingsUi.motionBlurBracket4Boost);

    applySettings(options);
}

void MenuState::applySettings(const Options &options) const noexcept
{
    if (auto *window = getContext().getRenderWindow(); window != nullptr)
    {
        window->setFullscreen(options.getFullscreen());
        window->setVsync(options.getVsync());

        if (SDL_Window *sdlWindow = window->getSDLWindow(); sdlWindow != nullptr)
        {
            int width = 0;
            int height = 0;
            SDL_GetWindowSize(sdlWindow, &width, &height);

            if (width > 0 && height > 0)
            {
                SDL_Event resizeEvent{};
                resizeEvent.type = SDL_EVENT_WINDOW_RESIZED;
                resizeEvent.window.windowID = SDL_GetWindowID(sdlWindow);
                resizeEvent.window.data1 = width;
                resizeEvent.window.data2 = height;
                SDL_PushEvent(&resizeEvent);
            }
        }
    }

    if (auto *sounds = getContext().getSoundPlayer(); sounds != nullptr)
    {
        sounds->setEnabled(options.getEnableSound());
        sounds->setVolume(options.getMasterVolume() * options.getSfxVolume() / 100.0f);
    }

    if (auto *musicManager = getContext().getMusicManager(); musicManager != nullptr)
    {
        try
        {
            auto &gameMusic = musicManager->get(Music::ID::GAME_MUSIC);
            if (options.getEnableMusic())
            {
                gameMusic.setVolume(options.getMasterVolume() * options.getMusicVolume() / 100.0f);
            }
            else
            {
                gameMusic.setVolume(0.0f);
            }
        }
        catch (const std::exception &)
        {
        }
    }

    if (auto *optionsManager = getContext().getOptionsManager(); optionsManager != nullptr)
    {
        try
        {
            auto &opts = optionsManager->get(GUIOptions::ID::DE_FACTO);
            opts.withMasterVolume(options.getMasterVolume())
                .withMusicVolume(options.getMusicVolume())
                .withSfxVolume(options.getSfxVolume())
                .withVsync(options.getVsync())
                .withFullscreen(options.getFullscreen())
                .withAntiAliasing(options.getAntiAliasing())
                .withEnableMusic(options.getEnableMusic())
                .withEnableSound(options.getEnableSound())
                .withShowDebugOverlay(options.getShowDebugOverlay())
                .withStencilOutlineEnabled(options.getStencilOutlineEnabled())
                .withStencilOutlinePulseEnabled(options.getStencilOutlinePulseEnabled())
                .withStencilOutlineWidth(options.getStencilOutlineWidth())
                .withStencilOutlinePulse(
                    options.getStencilOutlinePulseSpeed(),
                    options.getStencilOutlinePulseAmount())
                .withStencilOutlineColor(
                    options.getStencilOutlineColorR(),
                    options.getStencilOutlineColorG(),
                    options.getStencilOutlineColorB())
                .withArcadeModeEnabled(options.getArcadeModeEnabled())
                .withRunnerSpeed(options.getRunnerSpeed())
                .withRunnerStrafeLimit(options.getRunnerStrafeLimit())
                .withRunnerStartingPoints(options.getRunnerStartingPoints())
                .withRunnerPickupMinValue(options.getRunnerPickupMinValue())
                .withRunnerPickupMaxValue(options.getRunnerPickupMaxValue())
                .withRunnerPickupSpacing(options.getRunnerPickupSpacing())
                .withRunnerObstaclePenalty(options.getRunnerObstaclePenalty())
                .withRunnerCollisionCooldown(options.getRunnerCollisionCooldown())
                .withMotionBlurBracket1Points(options.getMotionBlurBracket1Points())
                .withMotionBlurBracket2Points(options.getMotionBlurBracket2Points())
                .withMotionBlurBracket3Points(options.getMotionBlurBracket3Points())
                .withMotionBlurBracket4Points(options.getMotionBlurBracket4Points())
                .withMotionBlurBracket1Boost(options.getMotionBlurBracket1Boost())
                .withMotionBlurBracket2Boost(options.getMotionBlurBracket2Boost())
                .withMotionBlurBracket3Boost(options.getMotionBlurBracket3Boost())
                .withMotionBlurBracket4Boost(options.getMotionBlurBracket4Boost());
        }
        catch (const std::exception &)
        {
        }
    }
}

void MenuState::initializeParticleScene() const noexcept
{
    if (mParticlesInitialized)
    {
        return;
    }

    if (auto *window = getContext().getRenderWindow(); window != nullptr)
    {
        SDL_GetWindowSize(window->getSDLWindow(), &mWindowWidth, &mWindowHeight);
    }
    updateParticleProjection();

    try
    {
        mParticlesComputeShader = &getContext().getShaderManager()->get(Shaders::ID::GLSL_PARTICLES_COMPUTE);
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "MenuState: Failed to get particle compute shader: %s", e.what());
        mParticlesComputeShader = nullptr;
        return;
    }

    try
    {
        mParticlesRenderShader = &getContext().getShaderManager()->get(Shaders::ID::GLSL_FULLSCREEN_QUAD_MVP);
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "MenuState: Failed to build particle render shader: %s", e.what());
        mParticlesRenderShader = nullptr;
        return;
    }

    mTotalParticles = static_cast<GLuint>(mParticleGrid.x * mParticleGrid.y * mParticleGrid.z);
    if (mTotalParticles == 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "MenuState: Particle scene has zero particles");
        return;
    }

    std::vector<GLfloat> initPos;
    initPos.reserve(static_cast<size_t>(mTotalParticles) * 4u);
    std::vector<GLfloat> initVel(static_cast<size_t>(mTotalParticles) * 4u, 0.0f);

    const GLfloat dx = 2.0f / static_cast<GLfloat>(mParticleGrid.x - 1);
    const GLfloat dy = 2.0f / static_cast<GLfloat>(mParticleGrid.y - 1);
    const GLfloat dz = 2.0f / static_cast<GLfloat>(mParticleGrid.z - 1);
    const glm::mat4 centerTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, -1.0f, -1.0f));

    for (int i = 0; i < mParticleGrid.x; ++i)
    {
        for (int j = 0; j < mParticleGrid.y; ++j)
        {
            for (int k = 0; k < mParticleGrid.z; ++k)
            {
                glm::vec4 p(dx * static_cast<GLfloat>(i),
                            dy * static_cast<GLfloat>(j),
                            dz * static_cast<GLfloat>(k),
                            1.0f);
                p = centerTransform * p;
                initPos.push_back(p.x);
                initPos.push_back(p.y);
                initPos.push_back(p.z);
                initPos.push_back(p.w);
            }
        }
    }

    const GLsizeiptr bufferSize = static_cast<GLsizeiptr>(static_cast<size_t>(mTotalParticles) * 4u * sizeof(GLfloat));

    glGenBuffers(1, &mParticlesPosSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mParticlesPosSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, initPos.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mParticlesPosSSBO);

    glGenBuffers(1, &mParticlesVelSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mParticlesVelSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, initVel.data(), GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mParticlesVelSSBO);

    glGenVertexArrays(1, &mParticlesVAO);
    glBindVertexArray(mParticlesVAO);
    glBindBuffer(GL_ARRAY_BUFFER, mParticlesPosSSBO);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    glGenBuffers(1, &mParticlesAttractorVBO);
    glBindBuffer(GL_ARRAY_BUFFER, mParticlesAttractorVBO);
    const GLfloat attractorData[] = {
        mBlackHoleBase1.x, mBlackHoleBase1.y, mBlackHoleBase1.z, mBlackHoleBase1.w,
        mBlackHoleBase2.x, mBlackHoleBase2.y, mBlackHoleBase2.z, mBlackHoleBase2.w};
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(attractorData)), attractorData, GL_DYNAMIC_DRAW);

    glGenVertexArrays(1, &mParticlesAttractorVAO);
    glBindVertexArray(mParticlesAttractorVAO);
    glBindBuffer(GL_ARRAY_BUFFER, mParticlesAttractorVBO);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    mParticlesInitialized = true;
}

void MenuState::renderParticleScene() const noexcept
{
    if (!mParticlesInitialized || !mParticlesComputeShader || !mParticlesRenderShader || mTotalParticles == 0 ||
        mParticlesRenderFBO == 0 || mParticlesRenderTexture == 0 || mParticleRenderWidth <= 0 || mParticleRenderHeight <= 0)
    {
        return;
    }

    if (!mParticlesComputeShader->isLinked())
    {
        return;
    }

    const float now = static_cast<float>(SDL_GetTicks()) * 0.001f;
    mParticleDeltaT = (mParticleTime == 0.0f) ? 0.0f : std::min(0.033f, now - mParticleTime);
    mParticleTime = now;
    mParticleResetAccumulator += mParticleDeltaT;

    if (mParticleResetAccumulator >= mParticleResetIntervalSeconds)
    {
        resetParticleSimulation();
        mParticleResetAccumulator = 0.0f;
    }

    mParticleAngle += mParticleSpeed * mParticleDeltaT;
    if (mParticleAngle > 360.0f)
    {
        mParticleAngle -= 360.0f;
    }

    const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(mParticleAngle), glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 attractor1 = glm::vec3(rotation * mBlackHoleBase1);
    const glm::vec3 attractor2 = glm::vec3(rotation * mBlackHoleBase2);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mParticlesPosSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mParticlesVelSSBO);

    mParticlesComputeShader->bind();
    mParticlesComputeShader->setUniform("BlackHolePos1", attractor1);
    mParticlesComputeShader->setUniform("BlackHolePos2", attractor2);
    const float clampedMass = std::max(0.0001f, mParticleMass);
    mParticlesComputeShader->setUniform("Gravity1", mParticleGravity1);
    mParticlesComputeShader->setUniform("Gravity2", mParticleGravity2);
    // mParticlesComputeShader->setUniform("ParticleMass", clampedMass);
    mParticlesComputeShader->setUniform("ParticleInvMass", 1.0f / clampedMass);
    mParticlesComputeShader->setUniform("DeltaT", std::max(0.0001f, mParticleDeltaT * mParticleDtScale));
    mParticlesComputeShader->setUniform("MaxDist", mParticleMaxDist);
    mParticlesComputeShader->setUniform("ParticleCount", mTotalParticles);

    const GLuint groupsX = (mTotalParticles + 999u) / 1000u;
    glDispatchCompute(groupsX, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    GLint previousFbo = 0;
    GLint previousViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    glBindFramebuffer(GL_FRAMEBUFFER, mParticlesRenderFBO);
    glViewport(0, 0, mParticleRenderWidth, mParticleRenderHeight);
    glClearColor(0.015f, 0.025f, 0.035f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 view = glm::lookAt(glm::vec3(2.0f, 0.0f, 20.0f),
                                       glm::vec3(0.0f, 0.0f, 0.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 mvp = mParticleProjection * view;

    mParticlesRenderShader->bind();
    mParticlesRenderShader->setUniform("MVP", mvp);

    glEnable(GL_DEPTH_TEST);
    glPointSize(mParticlePointSize);
    mParticlesRenderShader->setUniform("Color", glm::vec4(0.92f, 0.98f, 1.0f, 0.16f));
    glBindVertexArray(mParticlesVAO);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(mTotalParticles));

    const GLfloat attractorData[] = {
        attractor1.x, attractor1.y, attractor1.z, 1.0f,
        attractor2.x, attractor2.y, attractor2.z, 1.0f};
    glBindBuffer(GL_ARRAY_BUFFER, mParticlesAttractorVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(sizeof(attractorData)), attractorData);

    glPointSize(mAttractorPointSize);
    mParticlesRenderShader->setUniform("Color", glm::vec4(1.0f, 0.9f, 0.35f, 1.0f));
    glBindVertexArray(mParticlesAttractorVAO);
    glDrawArrays(GL_POINTS, 0, 2);

    glUseProgram(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glDisable(GL_DEPTH_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
}

void MenuState::ensureParticleRenderTarget(int width, int height) const noexcept
{
    width = std::max(1, width);
    height = std::max(1, height);

    if (mParticlesRenderFBO != 0 && mParticlesRenderTexture != 0 &&
        mParticleRenderWidth == width && mParticleRenderHeight == height)
    {
        return;
    }

    if (mParticlesRenderDepthRBO != 0)
    {
        glDeleteRenderbuffers(1, &mParticlesRenderDepthRBO);
        mParticlesRenderDepthRBO = 0;
    }
    if (mParticlesRenderTexture != 0)
    {
        glDeleteTextures(1, &mParticlesRenderTexture);
        mParticlesRenderTexture = 0;
    }
    if (mParticlesRenderFBO != 0)
    {
        glDeleteFramebuffers(1, &mParticlesRenderFBO);
        mParticlesRenderFBO = 0;
    }

    glGenTextures(1, &mParticlesRenderTexture);
    glBindTexture(GL_TEXTURE_2D, mParticlesRenderTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenRenderbuffers(1, &mParticlesRenderDepthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, mParticlesRenderDepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

    glGenFramebuffers(1, &mParticlesRenderFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, mParticlesRenderFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mParticlesRenderTexture, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mParticlesRenderDepthRBO);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "MenuState: Particle render target framebuffer incomplete");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    mParticleRenderWidth = width;
    mParticleRenderHeight = height;
    updateParticleProjection();
}

void MenuState::resetParticleSimulation() const noexcept
{
    if (mTotalParticles == 0 || mParticlesPosSSBO == 0 || mParticlesVelSSBO == 0)
    {
        return;
    }

    std::vector<GLfloat> resetPositions;
    resetPositions.reserve(static_cast<size_t>(mTotalParticles) * 4u);

    const GLfloat dx = 2.0f / static_cast<GLfloat>(mParticleGrid.x - 1);
    const GLfloat dy = 2.0f / static_cast<GLfloat>(mParticleGrid.y - 1);
    const GLfloat dz = 2.0f / static_cast<GLfloat>(mParticleGrid.z - 1);
    const glm::mat4 centerTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, -1.0f, -1.0f));

    for (int i = 0; i < mParticleGrid.x; ++i)
    {
        for (int j = 0; j < mParticleGrid.y; ++j)
        {
            for (int k = 0; k < mParticleGrid.z; ++k)
            {
                glm::vec4 p(dx * static_cast<GLfloat>(i),
                            dy * static_cast<GLfloat>(j),
                            dz * static_cast<GLfloat>(k),
                            1.0f);
                p = centerTransform * p;
                resetPositions.push_back(p.x);
                resetPositions.push_back(p.y);
                resetPositions.push_back(p.z);
                resetPositions.push_back(p.w);
            }
        }
    }

    std::vector<GLfloat> resetVelocities(static_cast<size_t>(mTotalParticles) * 4u, 0.0f);
    const GLsizeiptr bufferSize = static_cast<GLsizeiptr>(static_cast<size_t>(mTotalParticles) * 4u * sizeof(GLfloat));

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mParticlesPosSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bufferSize, resetPositions.data());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mParticlesVelSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bufferSize, resetVelocities.data());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void MenuState::cleanupParticleScene() noexcept
{
    if (mParticlesVAO != 0)
    {
        glDeleteVertexArrays(1, &mParticlesVAO);
        mParticlesVAO = 0;
    }
    if (mParticlesAttractorVAO != 0)
    {
        glDeleteVertexArrays(1, &mParticlesAttractorVAO);
        mParticlesAttractorVAO = 0;
    }
    if (mParticlesPosSSBO != 0)
    {
        glDeleteBuffers(1, &mParticlesPosSSBO);
        mParticlesPosSSBO = 0;
    }
    if (mParticlesVelSSBO != 0)
    {
        glDeleteBuffers(1, &mParticlesVelSSBO);
        mParticlesVelSSBO = 0;
    }
    if (mParticlesAttractorVBO != 0)
    {
        glDeleteBuffers(1, &mParticlesAttractorVBO);
        mParticlesAttractorVBO = 0;
    }
    if (mParticlesRenderDepthRBO != 0)
    {
        glDeleteRenderbuffers(1, &mParticlesRenderDepthRBO);
        mParticlesRenderDepthRBO = 0;
    }
    if (mParticlesRenderTexture != 0)
    {
        glDeleteTextures(1, &mParticlesRenderTexture);
        mParticlesRenderTexture = 0;
    }
    if (mParticlesRenderFBO != 0)
    {
        glDeleteFramebuffers(1, &mParticlesRenderFBO);
        mParticlesRenderFBO = 0;
    }

    mParticleRenderWidth = 0;
    mParticleRenderHeight = 0;

    mParticlesRenderShader = nullptr;
    mParticlesComputeShader = nullptr;
    mParticlesInitialized = false;

    // Ensure all OpenGL commands are processed before state destruction completes
    // This prevents race conditions when transitioning to GameState
    glFlush();
}

void MenuState::updateParticleProjection() const noexcept
{
    int projectionWidth = (mParticleRenderWidth > 0) ? mParticleRenderWidth : mWindowWidth;
    int projectionHeight = (mParticleRenderHeight > 0) ? mParticleRenderHeight : mWindowHeight;

    if (projectionWidth <= 0 || projectionHeight <= 0)
    {
        projectionWidth = 1;
        projectionHeight = 1;
    }

    const float aspectRatio = static_cast<float>(projectionWidth) / static_cast<float>(projectionHeight);
    mParticleProjection = glm::perspective(glm::radians(50.0f), aspectRatio, 1.0f, 100.0f);
}
