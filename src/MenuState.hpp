#ifndef MENU_STATE_HPP
#define MENU_STATE_HPP

#include "State.hpp"

#include <array>

class MusicPlayer;

class MenuState : public State
{
public:
    explicit MenuState(StateStack& stack, Context context);

    void draw() const noexcept override;
    bool update(float dt, unsigned int subSteps) noexcept override;
    bool handleEvent(const SDL_Event& event) noexcept override;

private:
    enum class MenuItem : unsigned int
    {
        CONTINUE = 0,
        NEW_GAME = 1,
        NETWORK_GAME = 2,
        SETTINGS = 3,
        SPLASH = 4,
        QUIT = 5,
        COUNT = 6
    };

    MusicPlayer* mMusic;

    // Navigation state variables
    mutable MenuItem mSelectedMenuItem;

    mutable bool mShowMainMenu;

    mutable std::array<bool, static_cast<size_t>(MenuItem::COUNT)> mItemSelectedFlags;
};

#endif // MENU_STATE_HPP
