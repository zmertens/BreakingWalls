#include "state_management.h"

#include <dearimgui/imgui.h>

#include "../Fonts/Cousine_Regular.h"
#include "../Fonts/Karla_Regular.h"
#include "../Fonts/Limelight_Regular.h"
#include "../Fonts/ProggyClean.h"
#include "../Fonts/Roboto_Medium.h"
#include "../Fonts/nunito_sans.h"

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <glad/gl.h>
#endif

#include <SDL3/SDL.h>

#include "db.h"
#include "font.h"
#include "player.h"
#include "resource_manager.h"
#include "shader.h"
#include "soundfx.h"
#include "texture.h"

#include <MazeBuilder/algos.h>
#include <MazeBuilder/configurator.h>
#include <MazeBuilder/io_utils.h>
#include <MazeBuilder/progress.h>
#include <MazeBuilder/string_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace
{
    constexpr std::array selectable_fonts{
        std::pair{FontIdentifier::COUSINE_REGULAR, std::string_view{"Cousine Regular"}},
        std::pair{FontIdentifier::KARLA_REGULAR, std::string_view{"Karla Regular"}},
        std::pair{FontIdentifier::LIMELIGHT, std::string_view{"Limelight Regular"}},
        std::pair{FontIdentifier::NUNITO_SANS, std::string_view{"Nunito Sans"}},
        std::pair{FontIdentifier::PROGGY_CLEAN, std::string_view{"Proggy Clean"}},
        std::pair{FontIdentifier::ROBOTO_MEDIUM, std::string_view{"Roboto Medium"}}};
    static_assert(selectable_fonts.size() == static_cast<std::size_t>(FontIdentifier::TOTAL));

    const char *action_label(const PlayerAction action) noexcept
    {
        switch (action)
        {
        case PlayerAction::MOVE_AUTO:
            return "Auto Run";
        case PlayerAction::MOVE_LEFT:
            return "Move Left";
        case PlayerAction::MOVE_RIGHT:
            return "Move Right";
        case PlayerAction::MOVE_FORWARD:
            return "Move Forward";
        case PlayerAction::MOVE_BACKWARD:
            return "Move Backward";
        case PlayerAction::MOVE_UP:
            return "Fly Up";
        case PlayerAction::MOVE_DOWN:
            return "Fly Down";
        case PlayerAction::JUMP:
            return "Jump";
        case PlayerAction::FLY:
            return "Toggle Fly";
        case PlayerAction::TAG_SIGN:
            return "Tag Sign";
        case PlayerAction::BUILD_BLOCK:
            return "Build Block";
        case PlayerAction::COPY_BLOCK:
            return "Copy Block";
        case PlayerAction::DESTROY_BLOCK:
            return "Destroy Block";
        case PlayerAction::PLACE_LIGHT:
            return "Place Light";
        case PlayerAction::PLACE_GHOST:
            return "Build Maze";
        case PlayerAction::PREVIEW_GHOST:
            return "Preview Maze";
        case PlayerAction::CHANGE_PERSPECTIVE:
            return "Change Perspective";
        case PlayerAction::ZOOM_IN_ISO_VIEW:
            return "Zoom In Isometric View";
        case PlayerAction::ZOOM_OUT_ISO_VIEW:
            return "Zoom Out Isometric View";
        default:
            return "Unknown";
        }
    }

    std::string make_filename(const player *active_player)
    {
        const auto &maze = active_player->get_configs().maze();
        return std::to_string(maze.rows()) + "x" + std::to_string(maze.columns()) + "x" +
               std::to_string(maze.levels()) + "_" + std::to_string(SDL_GetTicks()) + "_" +
               active_player->get_name() + ".obj";
    }

    void handle_artifacts(player *active_player, const std::string &artifacts) noexcept
    {
        if (artifacts.empty())
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to generate artifacts.\n");
            return;
        }

#if !defined(__EMSCRIPTEN__)
        constexpr mazes::io_utils io{};
        const auto filename = make_filename(active_player);
        const auto success = io.write_file(filename, artifacts);
        active_player->get_configs().artifacts_ready(false);
        SDL_Log("Write file '%s': %s (%zu bytes)\n", filename.c_str(),
                success ? "SUCCESS" : "FAILED", artifacts.size());
#else
        SDL_Log("Web build: Artifacts ready for download via JavaScript\n");
#endif
    }
}

state::context::context(SDL_Window *window, font_manager &fonts, shader_manager &shaders,
                        texture_manager &textures, bloom_pass_manager &bloom_passes,
                        player &active_player, sdl_gl_helper &sdl, soundfx_manager &sounds)
    : ctx_window{window}, ctx_fonts{&fonts}, ctx_shaders{&shaders},
      ctx_textures{&textures}, ctx_bloom_passes{&bloom_passes},
      ctx_player{&active_player}, ctx_sdl{&sdl}, ctx_sounds{&sounds}
{
}

state::state(state_stack &stack, const context &context)
    : active_stack{&stack}, craft_ctx{context}
{
}

bool state::draw_menu_button() const noexcept
{
    ImGui::SetNextWindowPos(ImVec2(12.f, 12.f), ImGuiCond_Always);
    if (ImGui::Begin("##mode_menu_button", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav))
    {
        const bool open = ImGui::Button("MENU (ESC)");
        ImGui::End();
        return open;
    }
    ImGui::End();
    return false;
}

void state::request_stack_push(const StateIdentifier state_id) const { active_stack->push_state(state_id); }
void state::request_stack_pop() const { active_stack->pop_state(); }
void state::request_stack_clear() const { active_stack->clear_states(); }
const state::context &state::get_context() const noexcept { return craft_ctx; }
state_stack &state::get_stack() const noexcept { return *active_stack; }

state_stack::pending_change::pending_change(const StackAction action, const StateIdentifier state_id)
    : action{action}, state_id{state_id}
{
}

state_stack::state_stack(const state::context &context) : craft_ctx{context}
{
    auto &colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(.016f, .047f, .024f, .97f);
    colors[ImGuiCol_ChildBg] = ImVec4(.022f, .058f, .032f, .90f);
    colors[ImGuiCol_TitleBg] = ImVec4(.067f, .137f, .094f, 1.f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(.118f, .227f, .161f, 1.f);
    colors[ImGuiCol_Button] = ImVec4(.188f, .365f, .259f, 1.f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(.302f, .502f, .380f, 1.f);
    colors[ImGuiCol_ButtonActive] = ImVec4(.537f, .635f, .341f, 1.f);
    colors[ImGuiCol_Header] = ImVec4(.302f, .502f, .380f, 1.f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(.537f, .635f, .341f, 1.f);
    colors[ImGuiCol_HeaderActive] = ImVec4(.745f, .863f, .498f, 1.f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(.745f, .863f, .498f, 1.f);
    colors[ImGuiCol_SliderGrab] = ImVec4(.537f, .635f, .341f, 1.f);
    colors[ImGuiCol_Tab] = ImVec4(.302f, .502f, .380f, 1.f);
    colors[ImGuiCol_TabActive] = ImVec4(.745f, .863f, .498f, 1.f);
    colors[ImGuiCol_TabDimmed] = ImVec4(.188f, .365f, .259f, 1.f);
    colors[ImGuiCol_TabHovered] = ImVec4(.302f, .502f, .380f, 1.f);
    colors[ImGuiCol_TabSelected] = ImVec4(.537f, .635f, .341f, 1.f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(.188f, .365f, .259f, 1.f);
    colors[ImGuiCol_Text] = ImVec4(.933f, 1.f, .8f, 1.f);
}

bool state_stack::is_top(const state *candidate) const noexcept
{
    return !active_stack.empty() && active_stack.back().get() == candidate;
}

void state_stack::update(const float delta_time, mazes::randomizer &rng) noexcept
{
    apply_pending_changes();
    std::ranges::reverse_view reverse_active_stack{active_stack};
    std::ranges::for_each(reverse_active_stack, [&](const state::ptr &s)
                          { s->update(delta_time, std::ref(rng)); });
}

void state_stack::draw() const noexcept
{
    for (const auto &active_state : active_stack)
        active_state->draw();
}

void state_stack::handle_event(SDL_Event &event) noexcept
{
    for (auto it = active_stack.rbegin(); it != active_stack.rend(); ++it)
    {
        if (!(*it)->handle_event(event))
            break;
    }
    apply_pending_changes();
}

void state_stack::push_state(const StateIdentifier state_id) { pending_changes_list.emplace_back(StackAction::PUSH, state_id); }
void state_stack::pop_state() { pending_changes_list.emplace_back(StackAction::POP); }
void state_stack::clear_states() { pending_changes_list.emplace_back(StackAction::CLEAR); }

void state_stack::switch_mode(const StateIdentifier state_id)
{
    if (state_id != StateIdentifier::GAME && state_id != StateIdentifier::EDITOR)
    {
        throw std::invalid_argument("Switch state failed - expected game or editor state");
    }

    clear_states();
    push_state(state_id);
}

void state_stack::mark_resources_loaded() noexcept { resources_loaded = true; }
bool state_stack::resources_have_loaded() const noexcept { return resources_loaded; }

void state_stack::apply_pending_changes()
{
    for (const auto &change : pending_changes_list)
    {
        switch (change.action)
        {
        case StackAction::PUSH:
            active_stack.push_back(create_state(change.state_id));
            break;
        case StackAction::POP:
            active_stack.pop_back();
            break;
        case StackAction::CLEAR:
            active_stack.clear();
            break;
        }
    }
    pending_changes_list.clear();
}

bool state_stack::is_empty() const noexcept { return active_stack.empty(); }

state::ptr state_stack::create_state(const StateIdentifier state_id)
{
    if (const auto found = change_factories.find(state_id); found != change_factories.cend())
    {
        return found->second();
    }
    throw std::runtime_error("Create state failed - No factory found for state ID");
}

loading_state::loading_state(state_stack &stack, const context &context) : state{stack, context} {}

void loading_state::load_resources() const noexcept
{
    constexpr float font_size = 18.f;
    const auto &ctx = get_context();
    ctx.ctx_fonts->load(FontIdentifier::COUSINE_REGULAR, Cousine_Regular_compressed_data, Cousine_Regular_compressed_size, font_size);
    ctx.ctx_fonts->load(FontIdentifier::LIMELIGHT, Limelight_Regular_compressed_data, Limelight_Regular_compressed_size, font_size);
    ctx.ctx_fonts->load(FontIdentifier::NUNITO_SANS, NunitoSans_compressed_data, NunitoSans_compressed_size, font_size);
    ctx.ctx_fonts->load(FontIdentifier::KARLA_REGULAR, Karla_Regular_compressed_data, Karla_Regular_compressed_size, font_size);
    ctx.ctx_fonts->load(FontIdentifier::ROBOTO_MEDIUM, Roboto_Medium_compressed_data, Roboto_Medium_compressed_size, font_size);
    ctx.ctx_fonts->load(FontIdentifier::PROGGY_CLEAN, ProggyClean_compressed_data, ProggyClean_compressed_size, font_size);

#if defined(__EMSCRIPTEN__)
    const std::vector<std::tuple<ShaderIdentifier, std::string, std::string>> shaders{
        {ShaderIdentifier::BLOCK_SHADER, "Shaders/es/block_vertex.es.glsl", "Shaders/es/block_fragment.es.glsl"},
        {ShaderIdentifier::LINE_SHADER, "Shaders/es/line_vertex.es.glsl", "Shaders/es/line_fragment.es.glsl"},
        {ShaderIdentifier::SKY_SHADER, "Shaders/es/sky_vertex.es.glsl", "Shaders/es/sky_fragment.es.glsl"},
        {ShaderIdentifier::TEXT_SHADER, "Shaders/es/text_vertex.es.glsl", "Shaders/es/text_fragment.es.glsl"},
        {ShaderIdentifier::BLOOM_BLUR_SHADER, "Shaders/es/bloom_quad_vertex.es.glsl", "Shaders/es/bloom_blur_fragment.es.glsl"},
        {ShaderIdentifier::BLOOM_COMPOSITE_SHADER, "Shaders/es/bloom_quad_vertex.es.glsl", "Shaders/es/bloom_composite_fragment.es.glsl"},
        {ShaderIdentifier::TRANSITION_BLUR_SHADER, "Shaders/es/bloom_quad_vertex.es.glsl", "Shaders/es/transition_blur_fragment.es.glsl"},
        {ShaderIdentifier::TRANSITION_PIXELATE_SHADER, "Shaders/es/bloom_quad_vertex.es.glsl", "Shaders/es/transition_pixelate_fragment.es.glsl"},
        {ShaderIdentifier::TRANSITION_WAVE_SHADER, "Shaders/es/bloom_quad_vertex.es.glsl", "Shaders/es/transition_wave_fragment.es.glsl"}};
#else
    const std::vector<std::tuple<ShaderIdentifier, std::string, std::string>> shaders{
        {ShaderIdentifier::BLOCK_SHADER, "Shaders/block_vertex.glsl", "Shaders/block_fragment.glsl"},
        {ShaderIdentifier::LINE_SHADER, "Shaders/line_vertex.glsl", "Shaders/line_fragment.glsl"},
        {ShaderIdentifier::SKY_SHADER, "Shaders/sky_vertex.glsl", "Shaders/sky_fragment.glsl"},
        {ShaderIdentifier::TEXT_SHADER, "Shaders/text_vertex.glsl", "Shaders/text_fragment.glsl"},
        {ShaderIdentifier::BLOOM_BLUR_SHADER, "Shaders/bloom_quad_vertex.glsl", "Shaders/bloom_blur_fragment.glsl"},
        {ShaderIdentifier::BLOOM_COMPOSITE_SHADER, "Shaders/bloom_quad_vertex.glsl", "Shaders/bloom_composite_fragment.glsl"},
        {ShaderIdentifier::TRANSITION_BLUR_SHADER, "Shaders/bloom_quad_vertex.glsl", "Shaders/transition_blur_fragment.glsl"},
        {ShaderIdentifier::TRANSITION_PIXELATE_SHADER, "Shaders/bloom_quad_vertex.glsl", "Shaders/transition_pixelate_fragment.glsl"},
        {ShaderIdentifier::TRANSITION_WAVE_SHADER, "Shaders/bloom_quad_vertex.glsl", "Shaders/transition_wave_fragment.glsl"}};
#endif
    for (const auto &[id, vertex, fragment] : shaders)
    {
        ctx.ctx_shaders->load(id, vertex, fragment);
    }

    const auto [width, height] = ctx.ctx_sdl->get_window_size_in_pixels();
    const auto blur = ctx.ctx_shaders->get(ShaderIdentifier::BLOOM_BLUR_SHADER).get();
    const auto composite = ctx.ctx_shaders->get(ShaderIdentifier::BLOOM_COMPOSITE_SHADER).get();
    ctx.ctx_bloom_passes->load(BloomPassIdentifier::EDITOR, width, height, blur, composite);
    ctx.ctx_bloom_passes->load(BloomPassIdentifier::GAME, 1, 1, blur, composite, true);
    ctx.ctx_bloom_passes->get(BloomPassIdentifier::GAME).set_transition_programs({ctx.ctx_shaders->get(ShaderIdentifier::TRANSITION_BLUR_SHADER).get(), ctx.ctx_shaders->get(ShaderIdentifier::TRANSITION_PIXELATE_SHADER).get(), ctx.ctx_shaders->get(ShaderIdentifier::TRANSITION_WAVE_SHADER).get()});

    ctx.ctx_textures->load(TextureIdentifier::ATLAS, "Textures/atlas.png", static_cast<unsigned int>(TextureIdentifier::ATLAS));
    ctx.ctx_textures->load(TextureIdentifier::BITMAP_FONT, "Textures/bitmap_font.png", static_cast<unsigned int>(TextureIdentifier::BITMAP_FONT));
    ctx.ctx_textures->load(TextureIdentifier::SIGNS, "Textures/signs.png", static_cast<unsigned int>(TextureIdentifier::SIGNS));
    ctx.ctx_textures->load(TextureIdentifier::SKY, "Textures/sky.png", static_cast<unsigned int>(TextureIdentifier::SKY));
    ctx.ctx_textures->load(ctx.ctx_window, TextureIdentifier::WINDOW_ICON, "Textures/icon.bmp");
    ctx.ctx_textures->load(TextureIdentifier::FLIPS_AND_ALE_ICON, "Textures/FlipsAndAleIcon.png", static_cast<unsigned int>(TextureIdentifier::FLIPS_AND_ALE_ICON));

    ctx.ctx_sounds->load(SoundIdentifier::GAME_THEME_SONG_1, "SoundFX/GameThemeSong.ogg", 0.f, 1.f, 98.6f, true);
    ctx.ctx_sounds->load(SoundIdentifier::LOADING_THEME_SONG, "SoundFX/loading.ogg", 0.35f, 1.f, 87.215f, true);
    ctx.ctx_sounds->load(SoundIdentifier::BOINK, "SoundFX/boink.ogg", 0.f, 1.f, 23.035f, false);
    {
        const auto &configs = ctx.ctx_player->get_configs();
        ctx.ctx_sounds->get(SoundIdentifier::BOINK)
            .set_volumes(configs.master_volume(), configs.music_volume(), configs.sfx_volume(), configs.mute_audio());
    }
}

void loading_state::draw() const noexcept
{
    const auto center = ImVec2(ImGui::GetIO().DisplaySize.x * .5f, ImGui::GetIO().DisplaySize.y * .5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(.5f, .5f));
    ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_Always);
    if (ImGui::Begin("Loading", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
    {
        ImGui::Spacing();
        ImGui::Spacing();
        constexpr auto text = "Loading Resources...";
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(text).x) * .5f);
        ImGui::Text("%s", text);
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::ProgressBar(-static_cast<float>(ImGui::GetTime()), ImVec2(-1, 0), "");
        const char *status = resources_loaded ? "Complete!" : "Please wait...";
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(status).x) * .5f);
        ImGui::Text("%s", status);
    }
    ImGui::End();
}

bool loading_state::update(float, mazes::randomizer &) noexcept
{
    if (!resources_loaded)
    {
        if (!get_stack().resources_have_loaded())
        {
            const auto prog{mazes::progress<>::duration([this]()
                                                        { load_resources(); })};
            SDL_Log("Resource loading took: %.7f ms",
                    mazes::progress<>::to_double_from_duration(prog));
            get_stack().mark_resources_loaded();
        }
        resources_loaded = true;
    }
    if (resources_loaded)
    {
        request_stack_pop();
    }
    return true;
}

bool loading_state::handle_event(SDL_Event &) noexcept { return true; }
bool loading_state::resources_have_loaded() const noexcept { return resources_loaded; }

editor_state::editor_state(state_stack &stack, const context &context)
    : state{stack, context}, active_player{*context.ctx_player}
{
}

void editor_state::draw() const noexcept
{
    if (!get_stack().is_top(this))
    {
        return;
    }
    if (current_editor_world)
    {
        current_editor_world->draw();
    }
    if (draw_menu_button())
    {
        SDL_SetWindowRelativeMouseMode(get_context().ctx_window, false);
        request_stack_push(StateIdentifier::MENU);
    }
}
void editor_state::rebuild_world() noexcept
{
    // Only an explicit "Make New World" discards saved edits; normal teardown keeps them.
    current_editor_world.reset();
    db::flush();
}

bool editor_state::update(const float delta_time, mazes::randomizer &rng) noexcept
{
    const auto &ctx = get_context();
    auto &&configs{active_player.get_configs()};
    auto &stack = get_stack();
    const bool active = stack.is_top(this);
    if (!current_editor_world)
    {
        if (stack.resources_have_loaded())
        {
            configs.show_ghosts(true).show_crosshair_details(true);
            current_editor_world.emplace(ctx.ctx_window, *ctx.ctx_fonts, &active_player,
                                         *ctx.ctx_shaders, *ctx.ctx_textures, ctx.ctx_sdl,
                                         &ctx.ctx_bloom_passes->get(BloomPassIdentifier::EDITOR));
            current_editor_world->init();
            if (active)
            {
                SDL_SetWindowRelativeMouseMode(ctx.ctx_window, true);
            }
            SDL_Log("Editor: World initialized after loading completed\n");
        }
    }
    if (current_editor_world && active)
    {
        current_editor_world->update(delta_time, rng);
        if (!SDL_GetWindowRelativeMouseMode(ctx.ctx_window))
        {
            SDL_SetWindowRelativeMouseMode(ctx.ctx_window, true);
        }
        ctx.ctx_player->update(delta_time, rng);
        ctx.ctx_player->handle_realtime_input(current_editor_world->get_command_queue());
    }
    return true;
}

bool editor_state::handle_event(SDL_Event &event) noexcept
{
    if (event.type == SDL_EVENT_QUIT)
    {
        active_player.set_active(false);
        request_stack_clear();
        return false;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE && !event.key.repeat)
    {
        SDL_SetWindowRelativeMouseMode(get_context().ctx_window, false);
        request_stack_push(StateIdentifier::MENU);
        return false;
    }
    if (current_editor_world)
    {
        active_player.handle_event(event, current_editor_world->get_command_queue());
        current_editor_world->handle_event(event);
    }
    return true;
}

menu_state::menu_state(state_stack &stack, const context &context)
    : state{stack, context},
      mode{stack.peek_state<editor_state *>() ? StateIdentifier::EDITOR : StateIdentifier::GAME},
      m_selected_algo{mazes::to_sv_from_algo(context.ctx_player->get_configs().maze().algo_id())}
{
    algo_list.emplace_back(mazes::to_sv_from_algo(mazes::algo::BINARY_TREE));
    algo_list.emplace_back(mazes::to_sv_from_algo(mazes::algo::DFS));
    algo_list.emplace_back(mazes::to_sv_from_algo(mazes::algo::SIDEWINDER));
    algo_list.emplace_back(mazes::to_sv_from_algo(mazes::algo::PRIMS));

    // The loading song plays while the menu is open; the game theme resumes when it closes.
    auto *sounds = context.ctx_sounds;
    auto &fx = sounds->get(SoundIdentifier::LOADING_THEME_SONG);
    const auto &configs = context.ctx_player->get_configs();
    fx.set_volumes(configs.master_volume(), configs.music_volume(), configs.sfx_volume(), configs.mute_audio());
    sounds->get(SoundIdentifier::GAME_THEME_SONG_1).stop(SoundIdentifier::GAME_THEME_SONG_1);
    fx.play(SoundIdentifier::LOADING_THEME_SONG);
}

menu_state::~menu_state()
{
    get_context().ctx_sounds->get(SoundIdentifier::LOADING_THEME_SONG).stop(SoundIdentifier::LOADING_THEME_SONG);
}

void menu_state::draw() const noexcept
{
    const auto &ctx = get_context();
    auto *active_player = ctx.ctx_player;
    auto &&configs = active_player->get_configs();
    ImGui::GetIO().FontGlobalScale = configs.gui_font_scale();
    ImGui::PushFont(ctx.ctx_fonts->get(static_cast<FontIdentifier>(active_player->get_font_index())).get());

    constexpr ImVec4 heading_color{.745f, .863f, .498f, 1.f};
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(display, ImGuiCond_Always);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##menu", nullptr, flags))
    {
        const auto &style = ImGui::GetStyle();
        const float scale = ImGui::GetIO().FontGlobalScale;
        const float row_height = ImGui::GetFrameHeightWithSpacing();
        const float sidebar_width = std::min(std::clamp(std::round(290.f * scale), 200.f, 420.f),
                                             ImGui::GetContentRegionAvail().x * .4f);
        const float content_width = std::max(1.f, ImGui::GetContentRegionAvail().x - sidebar_width - style.ItemSpacing.x);

        static constexpr auto FLIPS_AND_ALE_STR{"{ Flips and Ale gamedev }"};
        ImGui::TextColored(heading_color, "Options");
        ImGui::Separator();
        const float available_height = ImGui::GetContentRegionAvail().y;
        ImGui::BeginChild("##sidebar", ImVec2(sidebar_width, available_height), ImGuiChildFlags_Borders);
        if (ImGui::Button("Resume", ImVec2(-1.f, 0)))
        {
            request_stack_pop();
        }
        ImGui::Separator();
        draw_mode_sidebar(available_height);
        if (ImGui::Button("Close Application", ImVec2(-1.f, row_height)))
        {
            request_stack_clear();
        }

        const auto &icon = ctx.ctx_textures->get(TextureIdentifier::FLIPS_AND_ALE_ICON);
        if (icon.gl_texture != 0 && icon.width > 0 && icon.height > 0)
        {
            ImGui::Separator();
            const float max_width = ImGui::GetContentRegionAvail().x;
            const float max_height = ImGui::GetContentRegionAvail().y;
            const float aspect = static_cast<float>(icon.width) / static_cast<float>(icon.height);
            float img_w = std::min(max_width, static_cast<float>(icon.width));
            float img_h = img_w / aspect;
            if (img_h > max_height)
            {
                img_h = max_height;
                img_w = img_h * aspect;
            }
            if (img_w > 1.f && img_h > 1.f)
            {
                // Pin to the bottom of the sidebar, centered horizontally
                ImGui::TextColored(ImVec4(1.f, 1.f, 1.f, 1.f), FLIPS_AND_ALE_STR);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y - img_h);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (max_width - img_w) * .5f);
                ImGui::Image(static_cast<ImTextureID>(icon.gl_texture), ImVec2(img_w, img_h));
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::BeginChild("##content", ImVec2(content_width, available_height), ImGuiChildFlags_Borders);
        if (ImGui::BeginTabBar("##tabs"))
        {
            if (ImGui::BeginTabItem("Graphics"))
            {
                bool vsync = configs.vsync(), fullscreen = configs.fullscreen();
                float exposure = configs.exposure_val(), font_scale = configs.gui_font_scale();
                bool bloom = configs.use_bloom_effect();
                bool show_stats = configs.show_stats_window();
                ImGui::Checkbox("Show FPS Stats", &show_stats);
                ImGui::Checkbox("Enable VSync", &vsync);
                ImGui::Checkbox("Enable Fullscreen", &fullscreen);
                ImGui::Checkbox("Enable Bloom Effect", &bloom);
                ImGui::SliderFloat("Exposure", &exposure, .1f, 2.f);
                ImGui::SliderFloat("Font Scale", &font_scale, .6f, 1.4f);
                if (ImGui::BeginListBox("##FontList"))
                {
                    for (const auto &[font_id, font_name] : selectable_fonts)
                    {
                        if (ImGui::Selectable(font_name.data(),
                                              active_player->get_font_index() == static_cast<int>(font_id)))
                            active_player->set_font_index(static_cast<int>(font_id));
                    }
                    ImGui::EndListBox();
                }
                if (vsync != configs.vsync())
                {
                    if (!SDL_GL_SetSwapInterval(vsync ? 1 : 0))
                    {
                        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "VSync change failed: %s", SDL_GetError());
                    }
                }
                if (fullscreen != configs.fullscreen())
                {
                    if (!SDL_SetWindowFullscreen(ctx.ctx_window, fullscreen ? SDL_WINDOW_FULLSCREEN : 0))
                    {
                        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Fullscreen change failed: %s", SDL_GetError());
                    }
                }
                configs.vsync(vsync).fullscreen(fullscreen).use_bloom_effect(bloom).exposure_val(exposure).gui_font_scale(font_scale).show_stats_window(show_stats);
                ImGui::EndTabItem();
            }

            draw_mode_tabs();
            if (ImGui::BeginTabItem("Audio"))
            {
                float master = configs.master_volume(), music = configs.music_volume(), sfx = configs.sfx_volume();
                bool muted = configs.mute_audio();
                ImGui::Checkbox("Mute All", &muted);
                ImGui::SliderFloat("Master Volume", &master, 0.f, 1.f);
                ImGui::SliderFloat("Music Volume", &music, 0.f, 1.f);
                ImGui::SliderFloat("Effects Volume", &sfx, 0.f, 1.f);
                configs.master_volume(master).music_volume(music).sfx_volume(sfx).mute_audio(muted);
                ctx.ctx_sounds->get(SoundIdentifier::LOADING_THEME_SONG)
                    .set_volumes(configs.master_volume(), configs.music_volume(), configs.sfx_volume(), configs.mute_audio());
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopFont();
}

bool menu_state::update(float, mazes::randomizer &) noexcept
{
    if (mode == StateIdentifier::EDITOR && rebuild_world_requested)
    {
        if (auto *editor = get_stack().peek_state<editor_state *>())
        {
            editor->rebuild_world();
            rebuild_world_requested = false;
        }
    }
    return false;
}

void menu_state::draw_mode_sidebar(const float available_height) const noexcept
{
    auto *active_player = get_context().ctx_player;
    if (mode == StateIdentifier::EDITOR)
    {
        if (ImGui::Button("Play Game", ImVec2(-1.f, 0)))
        {
            get_stack().switch_mode(StateIdentifier::GAME);
        }
        ImGui::TextWrapped("Switching modes resets the current world.");
        ImGui::Separator();

        ImGui::TextUnformatted("Key Bindings");
        const float row_height = ImGui::GetFrameHeightWithSpacing();
        if (ImGui::BeginTable("##keybinds", 2,
                              ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(-1.f, std::max(3.f * row_height, available_height - 300.f))))
        {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 80.f);
            ImGui::TableHeadersRow();
            for (int index = 0; index < static_cast<int>(PlayerAction::COUNT); ++index)
            {
                const auto action = static_cast<PlayerAction>(index);
                const auto key = static_cast<SDL_Scancode>(active_player->get_assigned_key(action));
                if (key == SDL_SCANCODE_UNKNOWN)
                {
                    continue;
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(action_label(action));
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(SDL_GetScancodeName(key));
            }
            ImGui::EndTable();
        }
        ImGui::TextUnformatted("Mouse Actions");
        ImGui::BulletText("Left Click: Destroy block");
        ImGui::BulletText("Right Click: Build block");
        ImGui::BulletText("Middle Click: Copy block type");
        ImGui::BulletText("Scroll: Cycle block types");
    }
    else
    {
        if (ImGui::Button("Voxel Editor", ImVec2(-1.f, 0)))
        {
            get_stack().switch_mode(StateIdentifier::EDITOR);
        }
        ImGui::TextWrapped("Switching modes resets the current world.");
    }
}

void menu_state::draw_mode_tabs() const noexcept
{
    auto *active_player = get_context().ctx_player;
    auto &&configs = active_player->get_configs();
    if (mode == StateIdentifier::GAME)
    {
        if (ImGui::BeginTabItem("Game"))
        {
            if (auto *game = get_stack().peek_state<game_state *>())
                game->draw_menu_options();
            ImGui::EndTabItem();
        }
        return;
    }
    if (ImGui::BeginTabItem("Builder"))
    {
        if (ImGui::Button("Make New World"))
        {
            rebuild_world_requested = true;
        }
        static char tag[256]{};
        if (tag[0] == '\0')
        {
            SDL_strlcpy(tag, configs.tag().c_str(), std::size(tag));
        }
        if (ImGui::InputText("Tag Message", tag, std::size(tag)))
        {
            configs.tag(std::string{tag});
        }
        bool heightmap = configs.show_heightmap();
        if (ImGui::Checkbox("Build more mountains (requires new world)", &heightmap))
        {
            configs.show_heightmap(heightmap);
        }
        auto maze = configs.maze();
        static constexpr auto ROWS{maze.MAX_ROWS / 4}, COLUMNS{maze.MAX_COLUMNS / 4};
        static constexpr auto LEVELS{maze.MAX_LEVELS / 2};
        static constexpr auto SEED{maze.DEFAULT_SEED_VALUE};
        static int rows = maze.rows(), columns = maze.columns(), levels = maze.levels(), seed = maze.seed();
        ImGui::SliderInt("Rows", &rows, 2, ROWS);
        ImGui::SliderInt("Columns", &columns, 2, COLUMNS);
        ImGui::SliderInt("Levels", &levels, 1, LEVELS);
        ImGui::SliderInt("Seed", &seed, 0, SEED);
        if (ImGui::BeginCombo("Algorithm", m_selected_algo.c_str()))
        {
            for (const auto &algorithm : algo_list)
            {
                if (ImGui::Selectable(algorithm.c_str()))
                {
                    m_selected_algo = algorithm;
                }
            }
            ImGui::EndCombo();
        }

        configs.maze(mazes::configurator{}
                         .algo_id(mazes::to_algo_from_sv(m_selected_algo))
                         .rows(rows)
                         .columns(columns)
                         .levels(levels)
                         .seed(seed));
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("CAD Tools"))
    {
        bool invert = configs.invert_mouse(), details = configs.show_crosshair_details();
        bool ghost = configs.show_ghosts();
        bool grid = configs.show_grid_overlay(), snap = configs.enable_grid_snap();
        float fov = configs.fov();
        int ortho = configs.ortho_scaling(), day = configs.day_length();
        ImGui::Checkbox("Invert Mouse Y-Axis", &invert);
        ImGui::SliderFloat("Field of View", &fov, 30, 120);
        ImGui::SliderInt("Orthographic Scale", &ortho, 0, 64);
        ImGui::Checkbox("Show Hover Info [H]", &details);
        ImGui::Checkbox("Show Maze Ghost [M]", &ghost);
        ImGui::Checkbox("Show CAD Grid", &grid);
        ImGui::Checkbox("CAD Grid Snap", &snap);
        ImGui::SliderInt("Day Length (s)", &day, 60, 1800);
        configs.invert_mouse(invert).fov(fov).ortho_scaling(ortho).day_length(day).show_crosshair_details(details).show_ghosts(ghost).show_grid_overlay(grid).enable_grid_snap(snap);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Player"))
    {
        ImGui::Text("Name: %s", active_player->get_name().c_str());
        ImGui::Text("Local time: %s", active_player->get_local_time().data());
        ImGui::Text("X: %.2f   Y: %.2f   Z: %.2f", active_player->pos.x, active_player->pos.y, active_player->pos.z);
        ImGui::Text("Selected block type: %d", active_player->get_item());
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Exports"))
    {
        if (m_export_in_progress && active_player->is_artifact_export_ready())
        {
            m_cached_artifacts = active_player->get_artifact_export_result();
            m_export_in_progress = false;
        }
        if (ImGui::Button("Generate Artifacts (Async)"))
        {
            active_player->start_async_artifact_export();
            m_cached_artifacts.clear();
            m_export_in_progress = true;
        }
        ImGui::BeginDisabled(m_cached_artifacts.empty() || m_export_in_progress);
        if (ImGui::Button("Download File"))
        {
#if defined(__EMSCRIPTEN__)
            configs.artifacts_ready(true);
#endif
            handle_artifacts(active_player, m_cached_artifacts);
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped("%s", m_cached_artifacts.empty() ? "No artifacts generated yet." : m_cached_artifacts.substr(0, 512).c_str());
        ImGui::EndTabItem();
    }
}

bool menu_state::handle_event(SDL_Event &event) noexcept
{
    if (event.type == SDL_EVENT_QUIT)
    {
        get_context().ctx_player->set_active(false);
        request_stack_clear();
    }
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE && !event.key.repeat)
    {
        request_stack_pop();
    }
    return event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
}

bool menu_state::is_ready_to_rebuild_world() const noexcept { return rebuild_world_requested; }

game_state::game_state(state_stack &s, const context &ctx)
    : state{s, ctx}
{
}

void game_state::open_menu()
{
    if (current_game_world)
    {
        current_game_world->suspend_input();
    }
    request_stack_push(StateIdentifier::MENU);
}

bool game_state::update(float dt, mazes::randomizer &rng) noexcept
{
    if (!current_game_world)
    {
        if (get_stack().resources_have_loaded())
        {
            const auto &ctx = get_context();
            SDL_SetWindowRelativeMouseMode(ctx.ctx_window, false);
            current_game_world.emplace(ctx.ctx_window, *ctx.ctx_fonts, ctx.ctx_player,
                                       *ctx.ctx_shaders, *ctx.ctx_textures, ctx.ctx_sdl,
                                       &ctx.ctx_bloom_passes->get(BloomPassIdentifier::GAME));
            current_game_world->init();
            SDL_Log("Game world initializing completed\n");
        }
    }
    if (current_game_world && get_stack().is_top(this))
    {
        current_game_world->update(dt, std::ref(rng));
        if (!current_game_world->tracing_running())
        {
            request_stack_clear();
        }
        return true;
    }
    return false;
}

bool game_state::handle_event(SDL_Event &event) noexcept
{
    if (event.type == SDL_EVENT_QUIT)
    {
        get_context().ctx_player->set_active(false);
        request_stack_clear();
        return true;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE && !event.key.repeat)
    {
        open_menu();
        return false;
    }
    if (current_game_world)
    {
        current_game_world->handle_event(event);
    }
    return true;
}

void game_state::draw() const noexcept
{
    if (current_game_world)
    {
        if (get_stack().is_top(this))
        {
            current_game_world->draw();
        }
    }
    if (get_stack().is_top(this) && draw_menu_button())
    {
        const_cast<game_state *>(this)->open_menu();
    }
}

void game_state::draw_menu_options()
{
    if (current_game_world)
    {
        current_game_world->draw_menu_options();
    }
    else
    {
        ImGui::TextUnformatted("Game settings will be available when the world is ready.");
    }
}
