#ifndef EVENTS_HPP
#define EVENTS_HPP

#include <cstdint>

namespace Events
{
    enum class Cause : std::int32_t
    {
        LIGHTSPEED = 1 << 0,
        COLLISION = 1 << 1,
        ENTANGLEMENT = 1 << 2,
        BREAKING_WALLS = 1 << 3
    };
}

#endif // EVENTS_HPP
