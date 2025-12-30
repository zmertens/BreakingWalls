#pragma once

#ifndef PHYSICS_HPP
#define PHYSICS_HPP

#include <box2d/math_functions.h>

namespace physics
{
    // Pixels per meter conversion - using smaller scale for better maze physics
    // In the working example, this equals the cell size (8-12 pixels)
    static constexpr float PIXELS_PER_METER = 10.0f;

    inline float toPixels(float meters) noexcept { return meters * PIXELS_PER_METER; }
    inline float toMeters(float pixels) noexcept { return pixels / PIXELS_PER_METER; }

    inline b2Vec2 toMetersVec(const b2Vec2& pixels) noexcept { return { toMeters(pixels.x), toMeters(pixels.y) }; }
    inline b2Vec2 toPixelsVec(const b2Vec2& meters) noexcept { return { toPixels(meters.x), toPixels(meters.y) }; }
}

#endif // PHYSICS_HPP

