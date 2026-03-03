#ifndef STATE_IDENTIFIERS_HPP
#define STATE_IDENTIFIERS_HPP

namespace States
{
    enum class ID : unsigned int
    {
        DONE = 0,
        ENTANGLEMENT = 1,
        GAME = 2,
        LIGHTSPEED = 3,
        LOADING = 4,
        MENU = 5,
        MULTIPLAYER_GAME = 6,
        PAUSE = 7,
        SPLASH = 8,
        WORMHOLE = 9
    };
}

#endif // STATE_IDENTIFIERS_HPP
