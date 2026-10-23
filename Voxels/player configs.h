#ifndef PLAYER_CONFIGS_H
#define PLAYER_CONFIGS_H

#include <algorithm>
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
        ORTHO = 2,
        THIRD_PERSON = 3
    };

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

    player_configs &master_volume(float volume)
    {
        opt_master_volume = std::clamp(volume, 0.0f, 1.0f);
        return *this;
    }

    player_configs &music_volume(float volume)
    {
        opt_music_volume = std::clamp(volume, 0.0f, 1.0f);
        return *this;
    }

    player_configs &sfx_volume(float volume)
    {
        opt_sfx_volume = std::clamp(volume, 0.0f, 1.0f);
        return *this;
    }

    player_configs &mute_audio(bool muted)
    {
        opt_mute_audio = muted;
        return *this;
    }

    float master_volume() const noexcept { return opt_master_volume.value_or(1.0f); }
    float music_volume() const noexcept { return opt_music_volume.value_or(1.0f); }
    float sfx_volume() const noexcept { return opt_sfx_volume.value_or(1.0f); }
    bool mute_audio() const noexcept { return opt_mute_audio.value_or(false); }

    bool fullscreen() const noexcept
    {
        return opt_fullscreen.value();
    }

    bool invert_mouse() const noexcept
    {
        return opt_invert_mouse.value();
    }

    // Terrain is always generated from a heightmap.
    static constexpr bool show_heightmap() noexcept
    {
        return true;
    }

    const mazes::configurator &maze() const noexcept
    {
        return opt_maze.value();
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
    std::optional<bool> opt_fullscreen{false};
    std::optional<bool> opt_invert_mouse{false};
    std::optional<bool> opt_show_stats_window{true};
    std::optional<bool> opt_use_bloom_effect{true};
    std::optional<bool> opt_vsync{true};

    std::optional<float> opt_exposure_val{0.182f};
    std::optional<float> opt_fov{60.0f};
    std::optional<float> opt_grid_opacity{1.0f};
    std::optional<float> opt_gui_font_scale{1.0f};

    std::optional<int> opt_day_length{600};
    std::optional<int> opt_ortho_scaling{1};
    std::optional<mazes::configurator> opt_maze{mazes::configurator{}
                                                    .rows(8)
                                                    .columns(8)
                                                    .levels(3)};

    std::optional<std::string> opt_tag{"Tag 1"};

    std::optional<std::uint64_t> opt_start_time{0};
    std::optional<std::uint64_t> opt_start_ticks{0};
    std::optional<bool> opt_mute_audio{false};
    std::optional<float> opt_master_volume{1.0f};
    std::optional<float> opt_music_volume{1.0f};
    std::optional<float> opt_sfx_volume{1.0f};

    std::optional<ViewMode> opt_view_mode{ViewMode::ORTHO};
}; // player_configs

#endif // PLAYER_CONFIGS_H
