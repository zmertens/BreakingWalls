#ifndef SETTINGS_STATE_HPP
#define SETTINGS_STATE_HPP

#include "Sprite.hpp"
#include "State.hpp"

class SettingsState : public State
{
public:
    explicit SettingsState(StateStack& stack, Context context);

    ~SettingsState();

    // Delete copy constructor and copy assignment operator
    // because SettingsState contains std::unique_ptr which is not copyable
    SettingsState(const SettingsState&) = delete;
    SettingsState& operator=(const SettingsState&) = delete;

    // Allow move constructor and move assignment operator
    SettingsState(SettingsState&&) = default;
    SettingsState& operator=(SettingsState&&) = default;

    void draw() const noexcept override;
    bool update(float dt, unsigned int subSteps) noexcept override;
    bool handleEvent(const SDL_Event& event) noexcept override;

private:
    /// Apply all settings to the game systems
    void applySettings(float masterVolume, float musicVolume, float sfxVolume,
                       bool vsync, bool fullscreen, bool antialiasing,
                       bool enableMusic, bool enableSound, bool showDebugOverlay) const noexcept;

    Sprite mBackgroundSprite;

    bool mShowText;

    // Settings UI state variables
    mutable bool mShowSettingsWindow;
};

#endif // SETTINGS_STATE_HPP

