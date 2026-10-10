#ifndef VOXEL_BOARD_LAYOUT_H
#define VOXEL_BOARD_LAYOUT_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>

struct voxel_board_layout
{
    static constexpr float HUD_HEIGHT = 0.0f;
    float left{}, top{}, extent{};
    float width{}, height{};
    std::size_t side{};
    int ortho{};
    float camera_x{}, camera_z{}, zoom{1.0f}, rotation{};

    static constexpr float ZOOM_MIN = 0.45f;
    static constexpr float ZOOM_MAX = 3.0f;
    static constexpr float EDGE_MARGIN = 72.0f;
    static constexpr float EDGE_SPEED = 260.0f;

    static voxel_board_layout fit(float width, float height, std::size_t side)
    {
        voxel_board_layout layout;
        layout.side = side;
        layout.ortho = static_cast<int>((side + 1) / 2 + 1);
        layout.resize(width, height);
        layout.reset_camera();
        return layout;
    }

    void resize(float window_width, float window_height)
    {
        width = std::max(0.0f, window_width);
        height = std::max(0.0f, window_height - HUD_HEIGHT);
        extent = std::min(width, height);
        left = 0;
        top = HUD_HEIGHT;
    }

    void reset_camera()
    {
        camera_x = camera_z = side > 0 ? static_cast<float>(side - 1) * 0.5f : 0.0f;
        zoom = 1.0f;
        rotation = 0.0f;
    }

    [[nodiscard]] float pitch() const { return extent * zoom / (2.0f * ortho); }

    [[nodiscard]] bool contains(float x, float y) const
    {
        return extent > 0 && x >= left && x < left + width &&
               y >= top && y < top + height;
    }

    [[nodiscard]] std::pair<float, float> to_world(float x, float y) const
    {
        const float dx = (x - left - width * 0.5f) / pitch();
        const float dz = (y - top - height * 0.5f) / pitch();
        const float c = std::cos(rotation), s = std::sin(rotation);
        return {camera_x + c * dx - s * dz, camera_z + s * dx + c * dz};
    }

    [[nodiscard]] std::pair<float, float> to_screen(float x, float z) const
    {
        const float dx = x - camera_x, dz = z - camera_z;
        const float c = std::cos(rotation), s = std::sin(rotation);
        return {left + width * 0.5f + pitch() * (c * dx + s * dz),
                top + height * 0.5f + pitch() * (-s * dx + c * dz)};
    }

    [[nodiscard]] std::pair<float, float> center(std::size_t cell) const
    {
        return to_screen(static_cast<float>(cell % side), static_cast<float>(cell / side));
    }

    void clamp_camera()
    {
        const float margin = static_cast<float>(side) * 0.5f;
        camera_x = std::clamp(camera_x, -0.5f - margin, static_cast<float>(side) - 0.5f + margin);
        camera_z = std::clamp(camera_z, -0.5f - margin, static_cast<float>(side) - 0.5f + margin);
    }

    void pan(float dx, float dy)
    {
        if (extent <= 0)
            return;
        const float c = std::cos(rotation), s = std::sin(rotation);
        camera_x -= (c * dx - s * dy) / pitch();
        camera_z -= (s * dx + c * dy) / pitch();
        clamp_camera();
    }

    void zoom_at(float factor, float x, float y)
    {
        if (extent <= 0 || !std::isfinite(factor) || factor <= 0)
            return;
        const auto [before_x, before_z] = to_world(x, y);
        zoom = std::clamp(zoom * factor, ZOOM_MIN, ZOOM_MAX);
        const auto [after_x, after_z] = to_world(x, y);
        camera_x += before_x - after_x;
        camera_z += before_z - after_z;
        clamp_camera();
    }

    void rotate_degrees(float degrees)
    {
        constexpr float pi = 3.14159265358979323846f;
        rotation = std::remainder(rotation + degrees * pi / 180.0f, 2.0f * pi);
    }

    [[nodiscard]] static float edge_factor(float position, float extent)
    {
        const float margin = std::min(EDGE_MARGIN, extent * 0.5f);
        if (margin <= 0)
            return 0;
        if (position < margin)
            return std::clamp((position - margin) / margin, -1.0f, 0.0f);
        if (position > extent - margin)
            return std::clamp((position - extent + margin) / margin, 0.0f, 1.0f);
        return 0;
    }

    [[nodiscard]] std::optional<std::size_t> pick(float x, float y) const
    {
        if (!contains(x, y) || side == 0 || ortho <= 0)
            return std::nullopt;
        const auto [world_x, world_z] = to_world(x, y);
        const float column = world_x + 0.5f;
        const float row = world_z + 0.5f;
        if (!std::isfinite(column) || !std::isfinite(row) ||
            column < 0 || row < 0 || column >= side || row >= side)
            return std::nullopt;
        return static_cast<std::size_t>(row) * side + static_cast<std::size_t>(column);
    }
};

#endif
