#ifndef PLAYER_CONFIGS_H
#define PLAYER_CONFIGS_H

#include <cstdint>
#include <optional>
#include <string>
#include <MazeBuilder/configurator.h>

struct player_configs
{
    enum class ViewMode : unsigned int
    {
        PERSPECTIVE = 0,
        ISOMETRIC = 1,
        ORTHO = 2
    };

    enum class CADTool : unsigned int
    {
        NONE = 0,
        SELECT_BOX = 1
    };

    player_configs &artifacts_ready(bool ready)
    {
        opt_artifacts_ready = ready;
        return *this;
    }

    player_configs &enable_grid_snap(bool enabled)
    {
        opt_enable_grid_snap = enabled;
        return *this;
    }

    player_configs &fullscreen(bool enabled)
    {
        opt_fullscreen = enabled;
        return *this;
    }

    player_configs &invert_mouse(bool enabled)
    {
        opt_invert_mouse = enabled;
        return *this;
    }

    player_configs &show_grid_overlay(bool enabled)
    {
        opt_show_grid_overlay = enabled;
        return *this;
    }

    player_configs &show_heightmap(bool enabled)
    {
        opt_show_heightmap = enabled;
        return *this;
    }

    player_configs &show_crosshair_details(bool enabled)
    {
        opt_show_crosshair_details = enabled;
        return *this;
    }

    player_configs &show_ghosts(bool enabled)
    {
        opt_show_ghosts = enabled;
        return *this;
    }

    player_configs &maze(const mazes::configurator &value)
    {
        opt_maze = value;
        return *this;
    }

    player_configs &show_stats_window(bool enabled)
    {
        opt_show_stats_window = enabled;
        return *this;
    }

    player_configs &use_bloom_effect(bool enabled)
    {
        opt_use_bloom_effect = enabled;
        return *this;
    }

    player_configs &vsync(bool enabled)
    {
        opt_vsync = enabled;
        return *this;
    }

    player_configs &exposure_val(float exposure)
    {
        opt_exposure_val = exposure;
        return *this;
    }

    player_configs &fov(float value)
    {
        opt_fov = value;
        return *this;
    }

    player_configs &gui_font_scale(float scale)
    {
        opt_gui_font_scale = scale;
        return *this;
    }

    player_configs &day_length(int length)
    {
        opt_day_length = length;
        return *this;
    }

    player_configs &grid_spacing(int spacing)
    {
        opt_grid_spacing = spacing;
        return *this;
    }

    player_configs &grid_opacity(float opacity)
    {
        opt_grid_opacity = opacity;
        return *this;
    }

    player_configs &ortho_scaling(int scaling)
    {
        opt_ortho_scaling = scaling;
        return *this;
    }

    player_configs &view_mode(ViewMode view_mode)
    {
        opt_view_mode = view_mode;
        return *this;
    }

    player_configs &tag(const std::string &value)
    {
        opt_tag = value;
        return *this;
    }

    player_configs &start_time(std::uint64_t time)
    {
        opt_start_time = time;
        return *this;
    }

    player_configs &start_ticks(std::uint64_t ticks)
    {
        opt_start_ticks = ticks;
        return *this;
    }

    bool artifacts_ready() const noexcept
    {
        return opt_artifacts_ready.value();
    }

    bool enable_grid_snap() const noexcept
    {
        return opt_enable_grid_snap.value();
    }

    bool fullscreen() const noexcept
    {
        return opt_fullscreen.value();
    }

    bool invert_mouse() const noexcept
    {
        return opt_invert_mouse.value();
    }

    bool show_grid_overlay() const noexcept
    {
        return opt_show_grid_overlay.value();
    }

    bool show_heightmap() const noexcept
    {
        return opt_show_heightmap.value();
    }

    bool show_crosshair_details() const noexcept
    {
        return opt_show_crosshair_details.value();
    }

    bool show_ghosts() const noexcept
    {
        return opt_show_ghosts.value();
    }

    const mazes::configurator &maze() const noexcept
    {
        return opt_maze.value();
    }

    int grid_spacing() const noexcept
    {
        return opt_grid_spacing.value();
    }

    float grid_opacity() const noexcept
    {
        return opt_grid_opacity.value();
    }

    bool show_stats_window() const noexcept
    {
        return opt_show_stats_window.value();
    }

    bool use_bloom_effect() const noexcept
    {
        return opt_use_bloom_effect.value();
    }

    bool vsync() const noexcept
    {
        return opt_vsync.value();
    }

    float exposure_val() const noexcept
    {
        return opt_exposure_val.value();
    }

    float fov() const noexcept
    {
        return opt_fov.value();
    }

    float gui_font_scale() const noexcept
    {
        return opt_gui_font_scale.value();
    }

    int day_length() const noexcept
    {
        return opt_day_length.value();
    }

    int ortho_scaling() const noexcept
    {
        return static_cast<int>(opt_ortho_scaling.value());
    }

    std::string tag() const noexcept
    {
        return opt_tag.value();
    }

    std::uint64_t start_time() const noexcept
    {
        return opt_start_time.value();
    }

    std::uint64_t start_ticks() const noexcept
    {
        return opt_start_ticks.value();
    }

    ViewMode view_mode() const noexcept
    {
        return opt_view_mode.value();
    }

private:
    std::optional<bool> opt_artifacts_ready{false};
    std::optional<bool> opt_enable_grid_snap{false};
    std::optional<bool> opt_fullscreen{false};
    std::optional<bool> opt_show_crosshair_details{true};
    std::optional<bool> opt_invert_mouse{false};
    std::optional<bool> opt_show_grid_overlay{false};
    std::optional<bool> opt_show_heightmap{false};
    std::optional<bool> opt_show_ghosts{true};
    std::optional<bool> opt_show_stats_window{true};
    std::optional<bool> opt_use_bloom_effect{true};
    std::optional<bool> opt_vsync{true};

    std::optional<float> opt_exposure_val{0.182f};
    std::optional<float> opt_fov{60.0f};
    std::optional<float> opt_grid_opacity{1.0f};
    std::optional<float> opt_gui_font_scale{1.0f};

    std::optional<int> opt_day_length{600};
    std::optional<int> opt_grid_spacing{8};
    std::optional<int> opt_ortho_scaling{1};
    std::optional<mazes::configurator> opt_maze{mazes::configurator{}};

    std::optional<std::string> opt_tag{"Tag 1"};

    std::optional<std::uint64_t> opt_start_time{0};
    std::optional<std::uint64_t> opt_start_ticks{0};

    std::optional<ViewMode> opt_view_mode{ViewMode::ORTHO};
}; // player_configs

#endif // PLAYER_CONFIGS_H
