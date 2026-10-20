#ifndef GAME_ENVIRONMENT_H
#define GAME_ENVIRONMENT_H

#include "shader.h"

#include <array>
#include <cstdint>
#include <cstddef>

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
    void bind() const noexcept;
    void draw(float rotation, float zoom, float pan_x, float pan_y,
              float scale_x = 1.0f, float scale_y = 1.0f,
              float seconds = 0.0f, float level = 1.0f,
              float lights = 0.0f, float board_radius = 0.8f,
              bool static_horizon = false,
              const float *view = nullptr) const noexcept;
    [[nodiscard]] bool ready() const noexcept { return m_ready; }

private:
    shader m_background;
    std::uint32_t m_cubemap{}, m_vao{};
    int m_sampler{-1}, m_rotation{-1}, m_zoom{-1}, m_pan{-1}, m_viewport_scale{-1};
    int m_animation{-1}, m_static_horizon{-1}, m_view{-1}, m_use_view{-1};
    bool m_ready{};
};

#endif
