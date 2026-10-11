#ifndef GAME_ENVIRONMENT_H
#define GAME_ENVIRONMENT_H

#include "shader.h"

#include <array>
#include <cstdint>

class game_balls;

class game_environment final
{
public:
    game_environment() = default;
    ~game_environment() noexcept;
    game_environment(const game_environment &) = delete;
    game_environment &operator=(const game_environment &) = delete;
    game_environment(game_environment &&) = delete;
    game_environment &operator=(game_environment &&) = delete;

    // Builds the six cubemap faces procedurally from 3D simplex noise (no files, no temp images).
    void init(std::uint32_t seed);
    void init_balls();
    void draw_balls(const game_balls &balls, const float *projection, bool use_environment,
                    float seconds = 0.0f) const noexcept;
    // Draws balls mirrored about the plane y = plane_y wherever the stencil buffer equals 1.
    void draw_reflected_balls(const game_balls &balls, const float *projection, bool use_environment,
                              float plane_y, float strength, float seconds = 0.0f) const noexcept;
    void bind() const noexcept;
    void draw(float rotation, float zoom, float pan_x, float pan_y,
              float scale_x = 1.0f, float scale_y = 1.0f,
              float seconds = 0.0f, float level = 1.0f,
              float lights = 0.0f, float board_radius = 0.8f) const noexcept;
    [[nodiscard]] bool ready() const noexcept { return m_ready; }

private:
    shader m_background;
    shader m_spheres;
    std::uint32_t m_cubemap{}, m_vao{};
    int m_sampler{-1}, m_rotation{-1}, m_zoom{-1}, m_pan{-1}, m_viewport_scale{-1};
    int m_animation{-1};
    int m_ball_matrix{-1}, m_ball_count{-1}, m_ball_spheres{-1}, m_ball_surfaces{-1};
    int m_ball_environment{-1}, m_ball_environment_enabled{-1}, m_ball_mirrored{-1}, m_ball_seconds{-1};
    int m_ball_index{-1};
    bool m_ready{};
};

#endif
