#ifndef STATE_MANAGEMENT_H
#define STATE_MANAGEMENT_H

#include "resource_identifiers.h"
#include "editor_world.h"
#include "heightmap.h"

#include <atomic>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;
union SDL_Event;

class player;
class sdl_gl_helper;

namespace mazes
{
    class randomizer;
}

enum class StackAction : unsigned int
{
    PUSH = 0,
    POP = 1,
    CLEAR = 2
};

enum class StateIdentifier : unsigned int
{
    DONE = 0,
    EDITOR = 1,
    LOADING = 3,
    MENU = 4,
    TOTAL = 5
};

class state_stack;

class state
{
public:
    using ptr = std::unique_ptr<state>;

    struct context
    {
        context(SDL_Window *window, font_manager &fonts, shader_manager &shaders,
                texture_manager &textures, bloom_pass_manager &bloom_passes,
                player &active_player, sdl_gl_helper &sdl, soundfx_manager &sounds, artifact_exporter_manager &artifacts);

        SDL_Window *ctx_window;
        font_manager *ctx_fonts;
        shader_manager *ctx_shaders;
        texture_manager *ctx_textures;
        bloom_pass_manager *ctx_bloom_passes;
        player *ctx_player;
        sdl_gl_helper *ctx_sdl;
        soundfx_manager *ctx_sounds;
        artifact_exporter_manager *ctx_artifacts;
    };

    state(state_stack &stack, const context &context);
    virtual ~state() = default;

    virtual void draw() const noexcept = 0;
    virtual bool update(float delta_time, mazes::randomizer &rng) noexcept = 0;
    virtual bool handle_event(SDL_Event &event) noexcept = 0;

protected:
    [[nodiscard]] bool draw_menu_button() const noexcept;
    void request_stack_push(StateIdentifier state_id) const;
    void request_stack_pop() const;
    void request_stack_clear() const;
    [[nodiscard]] const context &get_context() const noexcept;
    [[nodiscard]] state_stack &get_stack() const noexcept;

private:
    state_stack *active_stack;
    context craft_ctx;
};

class state_stack
{
    struct pending_change
    {
        explicit pending_change(StackAction action, StateIdentifier state_id = StateIdentifier::DONE);

        StackAction action;
        StateIdentifier state_id;
    };

public:
    explicit state_stack(const state::context &context);

    template <typename T>
    void register_state(StateIdentifier state_id)
    {
        change_factories.insert_or_assign(state_id, [this]()
                                          { return state::ptr(std::make_unique<T>(*this, craft_ctx)); });
    }

    template <typename Pointer>
    [[nodiscard]] Pointer peek_state() const noexcept
    {
        for (auto it = active_stack.rbegin(); it != active_stack.rend(); ++it)
        {
            if (auto result = dynamic_cast<Pointer>(it->get()); result != nullptr)
            {
                return result;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool is_top(const state *candidate) const noexcept;
    void update(float delta_time, mazes::randomizer &rng) noexcept;
    void draw() const noexcept;
    void handle_event(SDL_Event &event) noexcept;
    void push_state(StateIdentifier state_id);
    void pop_state();
    void clear_states();
    void mark_resources_loaded() noexcept;
    [[nodiscard]] bool resources_have_loaded() const noexcept;
    void apply_pending_changes();
    [[nodiscard]] bool is_empty() const noexcept;

private:
    state::ptr create_state(StateIdentifier state_id);

    std::vector<state::ptr> active_stack;
    std::vector<pending_change> pending_changes_list;
    state::context craft_ctx;
    std::map<StateIdentifier, std::function<state::ptr()>> change_factories;
    bool resources_loaded{false};
};

class loading_state final : public state
{
public:
    loading_state(state_stack &stack, const context &context);

    void draw() const noexcept override;
    bool update(float delta_time, mazes::randomizer &rng) noexcept override;
    bool handle_event(SDL_Event &event) noexcept override;
    [[nodiscard]] bool resources_have_loaded() const noexcept;

private:
    void load_resources() const noexcept;

    std::atomic<bool> resources_loaded{false};
};

class editor_state final : public state
{
public:
    editor_state(state_stack &stack, const context &context);

    void draw_preview3D(int fbo, int width, int height) const noexcept;
    void draw_physics_controls() const noexcept;
    void draw() const noexcept override;
    void rebuild_world() noexcept;
    bool update(float delta_time, mazes::randomizer &rng) noexcept override;
    bool handle_event(SDL_Event &event) noexcept override;

private:
    player &active_player;
    std::optional<editor_world> current_editor_world;
};

class menu_state : public state
{
public:
    menu_state(state_stack &stack, const context &context);
    ~menu_state() override;

    void draw() const noexcept override;
    bool update(float delta_time, mazes::randomizer &rng) noexcept override;
    bool handle_event(SDL_Event &event) noexcept override;
    [[nodiscard]] bool is_ready_to_rebuild_world() const noexcept;

protected:
    void draw_mode_sidebar(float available_height) const noexcept;
    void draw_mode_tabs() const noexcept;
    void draw_heightmap_preview() const noexcept;

private:
    std::list<std::string> algo_list;
    mutable std::string m_selected_algo;
    mutable std::string m_cached_artifacts;
    mutable bool m_export_in_progress{false};
    mutable bool rebuild_world_requested{false};
    mutable unsigned int m_preview_texture{0};
    mutable std::shared_ptr<const heightmap> m_preview_source;
};

#endif // STATE_MANAGEMENT_H
