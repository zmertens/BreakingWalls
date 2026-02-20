#include "SettingsState.hpp"

#include <SDL3/SDL.h>

#include <dearimgui/imgui.h>

#include <algorithm>
#include <cctype>
#include <unordered_map>

#include "Font.hpp"
#include "JsonUtils.hpp"
#include "MusicPlayer.hpp"
#include "Options.hpp"
#include "ResourceIdentifiers.hpp"
#include "ResourceManager.hpp"
#include "SoundPlayer.hpp"
#include "StateStack.hpp"

SettingsState::SettingsState(StateStack &stack, Context context)
    : State(stack, context), mShowText{true}, mShowSettingsWindow(true)
{
}

void SettingsState::draw() const noexcept
{
    // Draw the game background FIRST, before any ImGui calls
    const auto &window = *getContext().window;

    ImGui::PushFont(getContext().fonts->get(Fonts::ID::COUSINE_REGULAR).get());

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
    auto &optionsManager = *getContext().options;

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

    if (!initialized)
    {
        // Load values from OptionsManager (using default option ID)
        try
        {
            const auto &opts = optionsManager.get(GUIOptions::ID::DE_FACTO);
            masterVolume = opts.mMasterVolume;
            musicVolume = opts.mMusicVolume;
            sfxVolume = opts.mSfxVolume;
            vsync = opts.mVsync;
            fullscreen = opts.mFullscreen;
            antialiasing = opts.mAntiAliasing;
            enableMusic = opts.mEnableMusic;
            enableSound = opts.mEnableSound;
            showDebugOverlay = opts.mShowDebugOverlay;
            arcadeModeEnabled = opts.mArcadeModeEnabled;
            runnerSpeed = opts.mRunnerSpeed;
            runnerStrafeLimit = opts.mRunnerStrafeLimit;
            runnerStartingPoints = opts.mRunnerStartingPoints;
            runnerPickupMinValue = opts.mRunnerPickupMinValue;
            runnerPickupMaxValue = opts.mRunnerPickupMaxValue;
            runnerPickupSpacing = opts.mRunnerPickupSpacing;
            runnerObstaclePenalty = opts.mRunnerObstaclePenalty;
            runnerCollisionCooldown = opts.mRunnerCollisionCooldown;
        }
        catch (const std::exception &)
        {
            // Options not loaded yet, use defaults
        }

        // Try to load arcade defaults from JSON (physics.json)
        try
        {
            std::unordered_map<std::string, std::string> resources;
            JSONUtils::loadConfiguration("physics.json", resources);

            const auto readStringValue = [&resources](const char *key) -> std::string
            {
                auto it = resources.find(key);
                if (it == resources.end())
                {
                    return "";
                }
                return JSONUtils::extractJsonValue(it->second);
            };

            const auto readFloatValue = [&readStringValue](const char *key, float fallback) -> float
            {
                const std::string raw = readStringValue(key);
                if (raw.empty())
                {
                    return fallback;
                }

                try
                {
                    return std::stof(raw);
                }
                catch (...)
                {
                    return fallback;
                }
            };

            const auto readIntValue = [&readStringValue](const char *key, int fallback) -> int
            {
                const std::string raw = readStringValue(key);
                if (raw.empty())
                {
                    return fallback;
                }

                try
                {
                    return std::stoi(raw);
                }
                catch (...)
                {
                    return fallback;
                }
            };

            const auto readBoolValue = [&readStringValue](const char *key, bool fallback) -> bool
            {
                std::string raw = readStringValue(key);
                std::transform(raw.begin(), raw.end(), raw.begin(), [](unsigned char c)
                               { return static_cast<char>(std::tolower(c)); });
                if (raw == "true" || raw == "1")
                {
                    return true;
                }
                if (raw == "false" || raw == "0")
                {
                    return false;
                }
                return fallback;
            };

            arcadeModeEnabled = readBoolValue("arcade_mode_enabled", arcadeModeEnabled);
            runnerSpeed = readFloatValue("runner_speed", runnerSpeed);
            runnerStrafeLimit = readFloatValue("runner_strafe_limit", runnerStrafeLimit);
            runnerStartingPoints = readIntValue("runner_starting_points", runnerStartingPoints);
            runnerPickupMinValue = readIntValue("runner_pickup_min_value", runnerPickupMinValue);
            runnerPickupMaxValue = readIntValue("runner_pickup_max_value", runnerPickupMaxValue);
            runnerPickupSpacing = readFloatValue("runner_pickup_spacing", runnerPickupSpacing);
            runnerObstaclePenalty = readIntValue("runner_obstacle_penalty", runnerObstaclePenalty);
            runnerCollisionCooldown = readFloatValue("runner_collision_cooldown", runnerCollisionCooldown);
        }
        catch (const std::exception &)
        {
            // JSON unavailable - keep current defaults
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

        if (runnerPickupMinValue > runnerPickupMaxValue)
        {
            runnerPickupMinValue = runnerPickupMaxValue;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Action buttons
        if (ImGui::Button("Apply Settings", ImVec2(150, 40)))
        {
            log("Settings applied");
            applySettings(Options{
                .mAntiAliasing = antialiasing,
                .mEnableMusic = enableMusic,
                .mEnableSound = enableSound,
                .mFullscreen = fullscreen,
                .mShowDebugOverlay = showDebugOverlay,
                .mVsync = vsync,
                .mMasterVolume = masterVolume,
                .mMusicVolume = musicVolume,
                .mSfxVolume = sfxVolume,
                .mArcadeModeEnabled = arcadeModeEnabled,
                .mRunnerSpeed = runnerSpeed,
                .mRunnerStrafeLimit = runnerStrafeLimit,
                .mRunnerStartingPoints = runnerStartingPoints,
                .mRunnerPickupMinValue = runnerPickupMinValue,
                .mRunnerPickupMaxValue = runnerPickupMaxValue,
                .mRunnerPickupSpacing = runnerPickupSpacing,
                .mRunnerObstaclePenalty = runnerObstaclePenalty,
                .mRunnerCollisionCooldown = runnerCollisionCooldown});
        }

        ImGui::SameLine();

        if (ImGui::Button("Reset to Default", ImVec2(150, 40)))
        {
            log("Settings reset to default");
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
        }

        ImGui::SameLine();

        if (ImGui::Button("Back to Menu", ImVec2(150, 40)))
        {
            log("Returning to menu");
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
    auto *window = getContext().window;
    auto *sounds = getContext().sounds;
    auto *musicManager = getContext().music;

    // Apply fullscreen
    if (window)
    {
        window->setFullscreen(options.mFullscreen);
        window->setVsync(options.mVsync);
    }

    // Apply sound settings
    if (sounds)
    {
        sounds->setEnabled(options.mEnableSound);
        // Apply master volume * sfx volume
        sounds->setVolume(options.mMasterVolume * options.mSfxVolume / 100.0f);
    }

    // Apply music settings to all music players
    if (musicManager)
    {
        try
        {
            // Apply to game music
            auto &gameMusic = musicManager->get(Music::ID::GAME_MUSIC);
            if (options.mEnableMusic)
            {
                gameMusic.setVolume(options.mMasterVolume * options.mMusicVolume / 100.0f);
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
    auto *optionsManager = getContext().options;
    if (optionsManager)
    {
        try
        {
            auto &opts = optionsManager->get(GUIOptions::ID::DE_FACTO);
            opts.mMasterVolume = options.mMasterVolume;
            opts.mMusicVolume = options.mMusicVolume;
            opts.mSfxVolume = options.mSfxVolume;
            opts.mVsync = options.mVsync;
            opts.mFullscreen = options.mFullscreen;
            opts.mAntiAliasing = options.mAntiAliasing;
            opts.mEnableMusic = options.mEnableMusic;
            opts.mEnableSound = options.mEnableSound;
            opts.mShowDebugOverlay = options.mShowDebugOverlay;
            opts.mArcadeModeEnabled = options.mArcadeModeEnabled;
            opts.mRunnerSpeed = options.mRunnerSpeed;
            opts.mRunnerStrafeLimit = options.mRunnerStrafeLimit;
            opts.mRunnerStartingPoints = options.mRunnerStartingPoints;
            opts.mRunnerPickupMinValue = options.mRunnerPickupMinValue;
            opts.mRunnerPickupMaxValue = options.mRunnerPickupMaxValue;
            opts.mRunnerPickupSpacing = options.mRunnerPickupSpacing;
            opts.mRunnerObstaclePenalty = options.mRunnerObstaclePenalty;
            opts.mRunnerCollisionCooldown = options.mRunnerCollisionCooldown;
        }
        catch (const std::exception &)
        {
            // Options not available
        }
    }

    log("Applied settings: Fullscreen=" + std::to_string(options.mFullscreen) +
        ", VSync=" + std::to_string(options.mVsync) +
        ", Music=" + std::to_string(options.mEnableMusic) +
        ", Sound=" + std::to_string(options.mEnableSound));
}

bool SettingsState::update(float dt, unsigned int subSteps) noexcept
{
    if (mShowSettingsWindow)
    {
        return true;
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
            mShowSettingsWindow = false;
        }
    }

    return true;
}
