#include "SettingsState.hpp"

#include <SDL3/SDL.h>

#include <dearimgui/imgui.h>

#include "Font.hpp"
#include "MusicPlayer.hpp"
#include "Options.hpp"
#include "ResourceIdentifiers.hpp"
#include "ResourceManager.hpp"
#include "SoundPlayer.hpp"
#include "StateStack.hpp"

SettingsState::SettingsState(StateStack& stack, Context context)
    : State(stack, context)
    , mShowText{ true }
    , mShowSettingsWindow(true)
{
}

SettingsState::~SettingsState()
{
    SDL_Log("%s\n", view().data());
}

void SettingsState::draw() const noexcept
{
    // Draw the game background FIRST, before any ImGui calls
    const auto& window = *getContext().window;

    ImGui::PushFont(getContext().fonts->get(Fonts::ID::LIMELIGHT).get());

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
    auto& optionsManager = *getContext().options;

    // Use static variables to persist UI state between frames
    // Initialize from options on first access using a flag
    static bool initialized = false;
    static float masterVolume = 100.0f;
    static float musicVolume = 80.0f;
    static float sfxVolume = 90.0f;
    static bool vsync = true;
    static bool fullscreen = false;
    static bool antialiasing = true;
    static bool enableMusic = true;
    static bool enableSound = true;
    static bool showDebugOverlay = false;

    if (!initialized)
    {
        // Load values from OptionsManager (using default option ID)
        try
        {
            const auto& opts = optionsManager.get(GUIOptions::ID::FULLSCREEN);
            masterVolume = opts.mMasterVolume;
            musicVolume = opts.mMusicVolume;
            sfxVolume = opts.mSfxVolume;
            vsync = opts.mVsync;
            fullscreen = opts.mFullscreen;
            antialiasing = opts.mAntiAliasing;
            enableMusic = opts.mEnableMusic;
            enableSound = opts.mEnableSound;
            showDebugOverlay = opts.mShowDebugOverlay;
        } catch (const std::exception&)
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

        // Action buttons
        if (ImGui::Button("Apply Settings", ImVec2(150, 40)))
        {
            log("Settings applied");
            applySettings(masterVolume, musicVolume, sfxVolume, vsync, fullscreen,
                antialiasing, enableMusic, enableSound, showDebugOverlay);
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

void SettingsState::applySettings(float masterVolume, float musicVolume, float sfxVolume,
    bool vsync, bool fullscreen, bool antialiasing,
    bool enableMusic, bool enableSound, bool showDebugOverlay) const noexcept
{
    auto* window = getContext().window;
    auto* sounds = getContext().sounds;
    auto* musicManager = getContext().music;

    // Apply fullscreen
    if (window)
    {
        window->setFullscreen(fullscreen);
        window->setVsync(vsync);
    }

    // Apply sound settings
    if (sounds)
    {
        sounds->setEnabled(enableSound);
        // Apply master volume * sfx volume
        sounds->setVolume(masterVolume * sfxVolume / 100.0f);
    }

    // Apply music settings to all music players
    if (musicManager)
    {
        try
        {
            // Apply to game music
            auto& gameMusic = musicManager->get(Music::ID::GAME_MUSIC);
            if (enableMusic)
            {
                gameMusic.setVolume(masterVolume * musicVolume / 100.0f);
            } else
            {
                gameMusic.setVolume(0.0f);
            }
        } catch (const std::exception&)
        {
            // Music not loaded yet
        }
    }

    // Store settings in OptionsManager for persistence
    auto* optionsManager = getContext().options;
    if (optionsManager)
    {
        try
        {
            auto& opts = optionsManager->get(GUIOptions::ID::FULLSCREEN);
            opts.mMasterVolume = masterVolume;
            opts.mMusicVolume = musicVolume;
            opts.mSfxVolume = sfxVolume;
            opts.mVsync = vsync;
            opts.mFullscreen = fullscreen;
            opts.mAntiAliasing = antialiasing;
            opts.mEnableMusic = enableMusic;
            opts.mEnableSound = enableSound;
            opts.mShowDebugOverlay = showDebugOverlay;
        } catch (const std::exception&)
        {
            // Options not available
        }
    }

    log("Applied settings: Fullscreen=" + std::to_string(fullscreen) +
        ", VSync=" + std::to_string(vsync) +
        ", Music=" + std::to_string(enableMusic) +
        ", Sound=" + std::to_string(enableSound));
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

bool SettingsState::handleEvent(const SDL_Event& event) noexcept
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

