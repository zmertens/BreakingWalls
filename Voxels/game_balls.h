#ifndef GAME_BALLS_H
#define GAME_BALLS_H

#include "maze_game_model.h"

#include <box3d/box3d.h>

#include <array>
#include <span>

class game_balls final
{
public:
    static constexpr std::size_t MAX_BALLS = 16;
    static constexpr float RADIUS = 0.28f;
    static constexpr float BOARD_FLOOR = 7.5f;
    static constexpr float FIXED_STEP = 1.0f / 120.0f;
    
    enum class material { diffuse, metal, dielectric };
    struct ball
    {
        b3BodyId body{b3_nullBodyId};
        material surface{};
        std::array<float, 3> color{};
    };

    game_balls() = default;
    ~game_balls();
    game_balls(const game_balls &) = delete;
    game_balls &operator=(const game_balls &) = delete;
    void reset(const maze_game_model &board, std::mt19937 &rng);
    void sync_bricks(const maze_game_model &board);
    bool update(float seconds);
    bool swipe(b3Vec3 from, b3Vec3 to);
    [[nodiscard]] b3Vec3 last_hit() const noexcept { return m_last_hit; }
    [[nodiscard]] std::span<const ball> balls() const noexcept { return m_balls; }
    // Fills sphere (xyz, radius) and surface (rgb, material) uniform arrays; returns the ball count.
    std::size_t pack(std::array<float, MAX_BALLS * 4> &spheres, std::array<float, MAX_BALLS * 4> &surfaces) const;

private:
    b3WorldId m_world{b3_nullWorldId};
    std::vector<ball> m_balls;
    std::vector<b3BodyId> m_bricks;
    float m_accumulator{};
    b3Vec3 m_last_hit{};
    b3BodyId box(b3Vec3 position, b3Vec3 half_extents);
};

#endif
