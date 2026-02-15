#ifndef STATE_IDENTIFIERS_HPP
#define STATE_IDENTIFIERS_HPP

namespace States
{
    enum class ID : unsigned int
    {
        DONE = 0,
        GAME = 1,
        LOADING = 2,
        MENU = 3,
        MULTIPLAYER_GAME = 4,
        PAUSE = 5,
        RESETTING = 6,
        SETTINGS = 7,
        SPLASH = 8
    };
}

#endif // STATE_IDENTIFIERS_HPP
