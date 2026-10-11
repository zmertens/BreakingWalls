#ifndef GAME_WORLD_H
#define GAME_WORLD_H

#include "world.h"
#include "maze_game_model.h"
#include "voxel_board_layout.h"
#include "game_environment.h"
#include "game_balls.h"
#include "game_swarm.h"
#include "game_ai.h"
#include "soundfx.h"
#include <map>

class game_world final : public world
{
public:
    game_world(SDL_Window *window, font_manager &fonts, player *p,
               shader_manager &shaders, texture_manager &textures,
               const sdl_gl_helper *sdl, bloom_pass *bloom);
    ~game_world() override;

    void init() noexcept override;
    void handle_event(const SDL_Event &event) noexcept override;
    void update(float delta_time, mazes::randomizer &rng) noexcept override;
    void draw() const noexcept override;
    [[nodiscard]] bool tracing_running() const noexcept;
    [[nodiscard]] const maze_game_model *tracing_board() const noexcept;
    [[nodiscard]] const voxel_board_layout *tracing_layout() const noexcept;
    [[nodiscard]] const game_balls *tracing_balls() const noexcept;
    void configure_tracing_board(std::size_t side);
    void suspend_input() noexcept;
    void draw_menu_options();

private:
    struct tracing_state
    {
        maze_game_model board{std::random_device{}(), true};
        voxel_board_layout layout;
        std::vector<maze_game_model::cell_kind> rendered;
        std::size_t rendered_side{};
        std::size_t rendered_level{};
        std::optional<std::int64_t> finger;
        using point = std::pair<float, float>;
        std::optional<point> pointer, pan_origin;
        std::uint8_t pan_button{};
        std::map<std::pair<std::int64_t, std::int64_t>, point> touches;
        point gesture_midpoint{};
        float gesture_distance{}, gesture_angle{};
        bool gesture_lockout{}, gesture_rotation_latched{};
        bool mouse_down{}, running{true};
        std::uint32_t vao{};
        shader glass_shader;
        game_environment environment;
        game_balls balls;
        game_swarm swarm;
        std::optional<point> swipe_pointer;
        bool environment_enabled{true};
        std::string environment_error;
        int glass_environment{-1}, glass_environment_strength{-1};
        int glass_environment_center{-1}, glass_environment_extent{-1};
        sdl_gl_helper::attrib glass_attrib{};
        std::uint32_t glass_buffer{}, falling_buffer{}, line_buffer{};
        std::vector<float> glass_data;
        int glass_vertices{}, falling_vertices{}, line_vertices{};
        int glass_time{-1}, glass_fall{-1}, glass_taper{-1}, glass_center{-1};
        int block_taper{-1}, block_center{-1};
        int block_mirror_mask{-1}, block_ball_count{-1}, block_ball_spheres{-1}, block_ball_surfaces{-1};
        int glass_ball_count{-1}, glass_ball_spheres{-1}, glass_ball_surfaces{-1};
        float effect_time{}, fall_started{};
        static constexpr float TRANSITION_SECONDS = 1.2f;
        TransitionEffect transition_effect{TransitionEffect::BLUR};
        float transition_started{-TRANSITION_SECONDS};
        int selected_side{8};
        std::string configuration_error;
        int bloom_emission{-1};
        int light_attenuation{-1};
        int goal_block{static_cast<int>(item::BlockType::GRASS)};
        game_ai ai;
        std::mt19937 rng{std::random_device{}()};
    };
    std::unique_ptr<tracing_state> m_tracing;
    std::shared_ptr<soundfx> m_sounds;
    void sync_tracing_board();
    void draw_tracing() const;
    void handle_tracing_event(const SDL_Event &event);
    void refresh_tracing_layout() const;
    void cancel_tracing_input();
    void play_boink() const;
    void trace_pointer(float x, float y, bool begin = false, bool release = false);
    void latch_camera_gesture();
    void update_camera_gesture();
};

#endif // GAME_WORLD_H
