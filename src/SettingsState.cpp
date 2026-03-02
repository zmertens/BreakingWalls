#include "SettingsState.hpp"

#include <SDL3/SDL.h>

#include <dearimgui/imgui.h>

#include <algorithm>
#include <cctype>
#include <unordered_map>

#include "Font.hpp"
#include "JSONUtils.hpp"
#include "MusicPlayer.hpp"
#include "Options.hpp"
#include "ResourceIdentifiers.hpp"
#include "ResourceManager.hpp"
#include "SoundPlayer.hpp"
#include "StateStack.hpp"

SettingsState::SettingsState(StateStack &stack, Context context)
    : State(stack, context), mShowText{true}, mShowSettingsWindow(true)
{
    // Load font with error handling
    try
    {
        mFont = &context.getFontManager()->get(Fonts::ID::NUNITO_SANS);
    }
    catch (const std::exception &e)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SettingsState: Failed to load font: %s", e.what());
        mFont = nullptr;
    }
}

void SettingsState::draw() const noexcept
{
    ImGui::PushFont(mFont->get());

    // Apply color schema (matching MenuState)
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.016f, 0.047f, 0.024f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.067f, 0.137f, 0.094f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.118f, 0.227f, 0.161f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.188f, 0.365f, 0.259f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.302f, 0.502f, 0.380f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.537f, 0.635f, 0.341f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.302f, 0.502f, 0.380f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.537f, 0.635f, 0.341f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.745f, 0.863f, 0.498f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.933f, 1.0f, 0.8f, 1.0f));

    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);

    // Get current options from global OptionsManager
    auto &optionsManager = *getContext().getOptionsManager();

    // Use static variables to persist UI state between frames
    // Initialize from options on first access using a flag
    static bool initialized = false;
    static float masterVolume = 50.0f;
    static float musicVolume = 75.0f;
    static float sfxVolume = 10.0f;
    static bool vsync = true;
    static bool fullscreen = false;
    static bool antialiasing = true;
    static bool enableMusic = true;
    static bool enableSound = true;
    static bool showDebugOverlay = false;
    static bool arcadeModeEnabled = true;
    static float runnerSpeed = 30.0f;
    static float runnerStrafeLimit = 35.0f;
    static int runnerStartingPoints = 100;
    static int runnerPickupMinValue = -25;
    static int runnerPickupMaxValue = 40;
    static float runnerPickupSpacing = 18.0f;
    static int runnerObstaclePenalty = 25;
    static float runnerCollisionCooldown = 0.40f;
    static int motionBlurBracket1Points = 300;
    static int motionBlurBracket2Points = 500;
    static int motionBlurBracket3Points = 800;
    static int motionBlurBracket4Points = 1200;
    static float motionBlurBracket1Boost = 0.10f;
    static float motionBlurBracket2Boost = 0.18f;
    static float motionBlurBracket3Boost = 0.28f;
    static float motionBlurBracket4Boost = 0.38f;

    if (!initialized)
    {
        // Load values from OptionsManager (using default option ID)
        try
        {
            const auto &opts = optionsManager.get(GUIOptions::ID::DE_FACTO);
            masterVolume = opts.getMasterVolume();
            musicVolume = opts.getMusicVolume();
            sfxVolume = opts.getSfxVolume();
            vsync = opts.getVsync();
            fullscreen = opts.getFullscreen();
            antialiasing = opts.getAntiAliasing();
            enableMusic = opts.getEnableMusic();
            enableSound = opts.getEnableSound();
            showDebugOverlay = opts.getShowDebugOverlay();
            arcadeModeEnabled = opts.getArcadeModeEnabled();
            runnerSpeed = opts.getRunnerSpeed();
            runnerStrafeLimit = opts.getRunnerStrafeLimit();
            runnerStartingPoints = opts.getRunnerStartingPoints();
            runnerPickupMinValue = opts.getRunnerPickupMinValue();
            runnerPickupMaxValue = opts.getRunnerPickupMaxValue();
            runnerPickupSpacing = opts.getRunnerPickupSpacing();
            runnerObstaclePenalty = opts.getRunnerObstaclePenalty();
            runnerCollisionCooldown = opts.getRunnerCollisionCooldown();
            motionBlurBracket1Points = opts.getMotionBlurBracket1Points();
            motionBlurBracket2Points = opts.getMotionBlurBracket2Points();
            motionBlurBracket3Points = opts.getMotionBlurBracket3Points();
            motionBlurBracket4Points = opts.getMotionBlurBracket4Points();
            motionBlurBracket1Boost = opts.getMotionBlurBracket1Boost();
            motionBlurBracket2Boost = opts.getMotionBlurBracket2Boost();
            motionBlurBracket3Boost = opts.getMotionBlurBracket3Boost();
            motionBlurBracket4Boost = opts.getMotionBlurBracket4Boost();
        }
        catch (const std::exception &)
        {
            // Options not loaded yet, use defaults
        }
        initialized = true;
    }

    bool windowOpen = mShowSettingsWindow;
    if (ImGui::Begin("Settings", &windowOpen, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::Text("Settings");
        ImGui::Separator();
        ImGui::Spacing();

        // Audio Settings Section
        ImGui::TextColored(ImVec4(0.745f, 0.863f, 0.498f, 1.0f), "Audio Settings:");
        ImGui::Spacing();

        ImGui::Checkbox("Enable Music", &enableMusic);
        ImGui::Checkbox("Enable Sound Effects", &enableSound);
        ImGui::SliderFloat("Master Volume", &masterVolume, 0.0f, 100.0f, "%.0f%%");
        ImGui::SliderFloat("Music Volume", &musicVolume, 0.0f, 100.0f, "%.0f%%");
        ImGui::SliderFloat("SFX Volume", &sfxVolume, 0.0f, 100.0f, "%.0f%%");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Graphics Settings Section
        ImGui::TextColored(ImVec4(0.745f, 0.863f, 0.498f, 1.0f), "Graphics Settings:");
        ImGui::Spacing();

        ImGui::Checkbox("VSync", &vsync);
        ImGui::Checkbox("Fullscreen", &fullscreen);
        ImGui::Checkbox("Anti-Aliasing", &antialiasing);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Debug Settings Section
        ImGui::TextColored(ImVec4(0.745f, 0.863f, 0.498f, 1.0f), "Debug Settings:");
        ImGui::Spacing();

        ImGui::Checkbox("Show Debug Overlay", &showDebugOverlay);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Arcade runner gameplay settings
        ImGui::TextColored(ImVec4(0.745f, 0.863f, 0.498f, 1.0f), "Arcade Runner Settings:");
        ImGui::Spacing();

        ImGui::Checkbox("Enable Arcade Runner", &arcadeModeEnabled);
        ImGui::SliderFloat("Runner Speed", &runnerSpeed, 5.0f, 100.0f, "%.1f");
        ImGui::SliderFloat("Strafe Limit", &runnerStrafeLimit, 5.0f, 100.0f, "%.1f");
        ImGui::SliderInt("Starting Points", &runnerStartingPoints, 1, 500);
        ImGui::SliderInt("Pickup Min Value", &runnerPickupMinValue, -100, 0);
        ImGui::SliderInt("Pickup Max Value", &runnerPickupMaxValue, 1, 150);
        ImGui::SliderFloat("Pickup Spacing", &runnerPickupSpacing, 4.0f, 50.0f, "%.1f");
        ImGui::SliderInt("Obstacle Penalty", &runnerObstaclePenalty, 1, 200);
        ImGui::SliderFloat("Collision Cooldown", &runnerCollisionCooldown, 0.05f, 2.0f, "%.2f s");

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.745f, 0.863f, 0.498f, 1.0f), "Motion Blur Score Brackets:");
        ImGui::SliderInt("Blur Bracket 1 Points", &motionBlurBracket1Points, 0, 3000);
        ImGui::SliderInt("Blur Bracket 2 Points", &motionBlurBracket2Points, 0, 3000);
        ImGui::SliderInt("Blur Bracket 3 Points", &motionBlurBracket3Points, 0, 3000);
        ImGui::SliderInt("Blur Bracket 4 Points", &motionBlurBracket4Points, 0, 3000);
        ImGui::SliderFloat("Blur Bracket 1 Boost", &motionBlurBracket1Boost, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Blur Bracket 2 Boost", &motionBlurBracket2Boost, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Blur Bracket 3 Boost", &motionBlurBracket3Boost, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Blur Bracket 4 Boost", &motionBlurBracket4Boost, 0.0f, 1.0f, "%.2f");

        if (runnerPickupMinValue > runnerPickupMaxValue)
        {
            runnerPickupMinValue = runnerPickupMaxValue;
        }

        if (motionBlurBracket2Points < motionBlurBracket1Points)
        {
            motionBlurBracket2Points = motionBlurBracket1Points;
        }
        if (motionBlurBracket3Points < motionBlurBracket2Points)
        {
            motionBlurBracket3Points = motionBlurBracket2Points;
        }
        if (motionBlurBracket4Points < motionBlurBracket3Points)
        {
            motionBlurBracket4Points = motionBlurBracket3Points;
        }

        if (motionBlurBracket2Boost < motionBlurBracket1Boost)
        {
            motionBlurBracket2Boost = motionBlurBracket1Boost;
        }
        if (motionBlurBracket3Boost < motionBlurBracket2Boost)
        {
            motionBlurBracket3Boost = motionBlurBracket2Boost;
        }
        if (motionBlurBracket4Boost < motionBlurBracket3Boost)
        {
            motionBlurBracket4Boost = motionBlurBracket3Boost;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Action buttons
        if (ImGui::Button("Apply Settings", ImVec2(150, 40)))
        {
            applySettings(Options().withAntiAliasing(antialiasing).withEnableMusic(enableMusic).withEnableSound(enableSound).withFullscreen(fullscreen).withShowDebugOverlay(showDebugOverlay).withVsync(vsync).withMasterVolume(masterVolume).withMusicVolume(musicVolume).withSfxVolume(sfxVolume).withArcadeModeEnabled(arcadeModeEnabled).withRunnerSpeed(runnerSpeed).withRunnerStrafeLimit(runnerStrafeLimit).withRunnerStartingPoints(runnerStartingPoints).withRunnerPickupMinValue(runnerPickupMinValue).withRunnerPickupMaxValue(runnerPickupMaxValue).withRunnerPickupSpacing(runnerPickupSpacing).withRunnerObstaclePenalty(runnerObstaclePenalty).withRunnerCollisionCooldown(runnerCollisionCooldown).withMotionBlurBracket1Points(motionBlurBracket1Points).withMotionBlurBracket2Points(motionBlurBracket2Points).withMotionBlurBracket3Points(motionBlurBracket3Points).withMotionBlurBracket4Points(motionBlurBracket4Points).withMotionBlurBracket1Boost(motionBlurBracket1Boost).withMotionBlurBracket2Boost(motionBlurBracket2Boost).withMotionBlurBracket3Boost(motionBlurBracket3Boost).withMotionBlurBracket4Boost(motionBlurBracket4Boost));
        }

        ImGui::SameLine();

        if (ImGui::Button("Reset to Default", ImVec2(150, 40)))
        {
            masterVolume = 100.0f;
            musicVolume = 80.0f;
            sfxVolume = 90.0f;
            vsync = true;
            fullscreen = false;
            antialiasing = true;
            enableMusic = true;
            enableSound = true;
            showDebugOverlay = false;
            arcadeModeEnabled = true;
            runnerSpeed = 30.0f;
            runnerStrafeLimit = 35.0f;
            runnerStartingPoints = 100;
            runnerPickupMinValue = -25;
            runnerPickupMaxValue = 40;
            runnerPickupSpacing = 18.0f;
            runnerObstaclePenalty = 25;
            runnerCollisionCooldown = 0.40f;
            motionBlurBracket1Points = 300;
            motionBlurBracket2Points = 500;
            motionBlurBracket3Points = 800;
            motionBlurBracket4Points = 1200;
            motionBlurBracket1Boost = 0.10f;
            motionBlurBracket2Boost = 0.18f;
            motionBlurBracket3Boost = 0.28f;
            motionBlurBracket4Boost = 0.38f;
        }

        ImGui::SameLine();

        if (ImGui::Button("Back to Menu", ImVec2(150, 40)))
        {
            mShowSettingsWindow = false;
        }
    }
    ImGui::End();

    // If user closed the window via the X button, update our state
    if (!windowOpen)
    {
        mShowSettingsWindow = false;
    }

    ImGui::PopStyleColor(10);

    ImGui::PopFont();
}

void SettingsState::applySettings(const Options &options) const noexcept
{
    // Apply fullscreen
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

    // Apply sound settings
    if (auto *sounds = getContext().getSoundPlayer(); sounds != nullptr)
    {
        sounds->setEnabled(options.getEnableSound());
        // Apply master volume * sfx volume
        sounds->setVolume(options.getMasterVolume() * options.getSfxVolume() / 100.0f);
    }

    // Apply music settings to all music players
    if (auto *musicManager = getContext().getMusicManager(); musicManager != nullptr)
    {
        try
        {
            // Apply to game music
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
            // Music not loaded yet
        }
    }

    // Store settings in OptionsManager for persistence
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
            // Options not available
        }
    }
}

bool SettingsState::update(float dt, unsigned int subSteps) noexcept
{
    if (mShowSettingsWindow)
    {
        // Block updates to underlying states while settings is open
        return false;
    }

    // User has closed the window, pop back to menu
    requestStackPop();

    // Return false to stop processing states below
    // This prevents MenuState from being updated in the same frame
    // before the pop actually happens
    return false;
}

bool SettingsState::handleEvent(const SDL_Event &event) noexcept
{
    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        if (event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SettingsState: Escape key pressed, returning to menu...");
            mShowSettingsWindow = false;
        }
    }

    // Consume events so gameplay does not react while settings is open
    return false;
}
