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
#include "heightmap.h"
#include "item.h"
#include "player.h"
#include "resource_manager.h"
#include "sdl_gl_helper.h"
#include "shader.h"
#include "soundfx.h"
#include "texture.h"
#include "world_builder.h"

#include <MazeBuilder/algos.h>
#include <MazeBuilder/configurator.h>
#include <MazeBuilder/io_utils.h>
#include <MazeBuilder/progress.h>
#include <MazeBuilder/randomizer.h>
#include <MazeBuilder/string_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>

namespace
{
    constexpr float golden_ratio = 1.61803398875f;
    constexpr float golden_minor_ratio = 1.f / golden_ratio;

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

    void handle_artifacts(player *active_player, artifact_exporter_manager &exporters, const std::string &artifacts) noexcept
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
        exporters.get(ArtifactIdentifier::WORLD_OBJ_1).set_download_ready(false);
        SDL_Log("Write file '%s': %s (%zu bytes)\n", filename.c_str(),
                success ? "SUCCESS" : "FAILED", artifacts.size());
#else
        SDL_Log("Web build: Artifacts ready for download via JavaScript\n");
#endif
    }
}

state::context::context(font_manager &fonts, shader_manager &shaders,
                        texture_manager &textures, bloom_pass_manager &bloom_passes,
                        player &active_player, sdl_gl_helper &sdl, soundfx_manager &sounds, artifact_exporter_manager &artifacts)
    : ctx_fonts{&fonts}, ctx_shaders{&shaders},
      ctx_textures{&textures}, ctx_bloom_passes{&bloom_passes},
      ctx_player{&active_player}, ctx_sdl{&sdl}, ctx_sounds{&sounds}, ctx_artifacts{&artifacts}
{
}

state::state(state_stack &stack, const context &context)
    : active_stack{&stack}, craft_ctx{context}
{
}

bool state::draw_menu_button() const noexcept
{
    bool clicked = false;
    ImGui::SetNextWindowPos(ImVec2(12.f, 12.f), ImGuiCond_Always);
    if (ImGui::Begin("##mode_menu_button", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav))
    {
        clicked = ImGui::Button("MENU (PAUSE)");
    }
    ImGui::End();
    return clicked;
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
    std::ranges::for_each(active_stack, [&](const state::ptr &s)
                          { s->draw(); });
}

void state_stack::handle_event(SDL_Event &event) noexcept
{
    std::ranges::for_each(std::ranges::reverse_view{active_stack}, [&](const state::ptr &s)
                          { if (!s->handle_event(event))
                            { return;} });
    apply_pending_changes();
}

void state_stack::push_state(const StateIdentifier state_id) { pending_changes_list.emplace_back(StackAction::PUSH, state_id); }
void state_stack::pop_state() { pending_changes_list.emplace_back(StackAction::POP); }
void state_stack::clear_states() { pending_changes_list.emplace_back(StackAction::CLEAR); }

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

void loading_state::load_resources(mazes::randomizer &rng) const noexcept
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
        {ShaderIdentifier::BLOOM_DEPTH_FIELD_SHADER, "Shaders/es/bloom_quad_vertex.es.glsl", "Shaders/es/depth_field_fragment.es.glsl"},
        {ShaderIdentifier::BLOOM_GLASS_RESOLVE_SHADER, "Shaders/es/bloom_quad_vertex.es.glsl", "Shaders/es/glass_resolve_fragment.es.glsl"},
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
        {ShaderIdentifier::BLOOM_DEPTH_FIELD_SHADER, "Shaders/bloom_quad_vertex.glsl", "Shaders/depth_field_fragment.glsl"},
        {ShaderIdentifier::BLOOM_GLASS_RESOLVE_SHADER, "Shaders/bloom_quad_vertex.glsl", "Shaders/glass_resolve_fragment.glsl"},
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
    const auto depth_field = ctx.ctx_shaders->get(ShaderIdentifier::BLOOM_DEPTH_FIELD_SHADER).get();
    const auto glass_resolve = ctx.ctx_shaders->get(ShaderIdentifier::BLOOM_GLASS_RESOLVE_SHADER).get();
    const auto coin_is_heads{static_cast<bool>(rng.get_int(0, 1))};
    ctx.ctx_bloom_passes->load(BloomPassIdentifier::EDITOR, blur, composite, depth_field, glass_resolve, width, height, coin_is_heads);

    ctx.ctx_textures->load(TextureIdentifier::ATLAS, "Textures/atlas.png", static_cast<unsigned int>(TextureIdentifier::ATLAS));
    ctx.ctx_textures->load(TextureIdentifier::BITMAP_FONT, "Textures/bitmap_font.png", static_cast<unsigned int>(TextureIdentifier::BITMAP_FONT));
    ctx.ctx_textures->load(TextureIdentifier::SIGNS, "Textures/signs.png", static_cast<unsigned int>(TextureIdentifier::SIGNS));
    ctx.ctx_textures->load(TextureIdentifier::SKY, "Textures/sky.png", static_cast<unsigned int>(TextureIdentifier::SKY));
    ctx.ctx_textures->load(ctx.ctx_sdl->window, TextureIdentifier::WINDOW_ICON, "Textures/icon.bmp");
    ctx.ctx_textures->load(TextureIdentifier::FLIPS_AND_ALE_ICON, "Textures/FlipsAndAleIcon.png", static_cast<unsigned int>(TextureIdentifier::FLIPS_AND_ALE_ICON));

    ctx.ctx_artifacts->load(ArtifactIdentifier::WORLD_OBJ_1);
    ctx.ctx_artifacts->load(ArtifactIdentifier::WORLD_IMG_1);

    ctx.ctx_sounds->load(SoundIdentifier::GAME_THEME_SONG_1, "SoundFX/GameThemeSong.ogg", 0.f, 1.f, 98.6f, true);
    ctx.ctx_sounds->load(SoundIdentifier::LOADING_THEME_SONG, "SoundFX/loading.ogg", 0.35f, 1.f, 87.215f, true);
    ctx.ctx_sounds->load(SoundIdentifier::BOINK, "SoundFX/boink.ogg", 0.f, 1.f, 23.035f, false);
    // The physics arena is ~29 units wide: full volume within 4 units, gentle falloff beyond.
    ctx.ctx_sounds->get(SoundIdentifier::BOINK).set_spatialization(4.0f, 0.5f);
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

bool loading_state::update(float, mazes::randomizer &rng) noexcept
{
    if (!resources_loaded)
    {
        if (!get_stack().resources_have_loaded())
        {
            const auto prog{mazes::progress<>::duration([this, &rng]()
                                                        { load_resources(std::ref(rng)); })};
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
        // No logner in editor_state , stop drawing menu button
        SDL_SetWindowRelativeMouseMode(get_context().ctx_sdl->window, false);
        request_stack_push(StateIdentifier::MENU);
    }
}

void editor_state::draw_physics_controls() const noexcept
{
    if (current_editor_world)
    {
        current_editor_world->draw_physics_controls();
    }
    else
    {
        ImGui::TextUnformatted("Physics sandbox is still loading.");
    }
}

void editor_state::rebuild_world() noexcept
{
    current_editor_world.reset();
    db::flush();
}

bool editor_state::update(const float delta_time, mazes::randomizer &rng) noexcept
{
    const auto &ctx = get_context();
    auto &&window{ctx.ctx_sdl->window};
    auto &&p{*ctx.ctx_player};
    auto &&configs{active_player.get_configs()};
    auto &stack = get_stack();
    const bool active = stack.is_top(this);
    if (!current_editor_world)
    {
        if (stack.resources_have_loaded())
        {
            const auto new_world{
                world_builder{}.with_sdl(*ctx.ctx_sdl).with_fonts(*ctx.ctx_fonts).with_shaders(*ctx.ctx_shaders).with_textures(*ctx.ctx_textures).with_bloom_passes(*ctx.ctx_bloom_passes).with_player(active_player)};
            current_editor_world.emplace(new_world);

            current_editor_world->init();
            SDL_Log("Editor: World initialized after loading completed\n");
        }
    }
    if (current_editor_world && active)
    {
        current_editor_world->update(delta_time, rng);
        p.update(delta_time, std::ref(rng));
        p.handle_realtime_input(current_editor_world->get_command_queue());
    }
    if (current_editor_world)
    {
        current_editor_world->update_physics(delta_time);
        const auto impact = current_editor_world->take_pending_impact();
        if (active)
        {
            auto *sounds = ctx.ctx_sounds;
            const auto &sound_configs = ctx.ctx_player->get_configs();
            auto &theme = sounds->get(SoundIdentifier::GAME_THEME_SONG_1);
            if (!theme.is_playing(SoundIdentifier::GAME_THEME_SONG_1))
            {
                theme.set_volumes(sound_configs.master_volume(), sound_configs.music_volume(),
                                  sound_configs.sfx_volume(), sound_configs.mute_audio());
                theme.play(SoundIdentifier::GAME_THEME_SONG_1);
            }
            // One boink per cooldown window, at the impact nearest the player.
            m_boink_cooldown_ms = std::max(0.0f, m_boink_cooldown_ms - delta_time);
            if (impact && m_boink_cooldown_ms <= 0.0f)
            {
                const auto &eye = ctx.ctx_player->pos;
                sounds->get(SoundIdentifier::BOINK).set_listener_position(eye.x, eye.y, eye.z);
                auto &boink = sounds->get(SoundIdentifier::BOINK);
                boink.set_volumes(sound_configs.master_volume(), sound_configs.music_volume(),
                                  sound_configs.sfx_volume(), sound_configs.mute_audio());
                boink.play_at(SoundIdentifier::BOINK, (*impact)[0], (*impact)[1], (*impact)[2]);
                m_boink_cooldown_ms = 80.0f;
            }
        }
    }
    return true;
}

bool editor_state::handle_event(SDL_Event &event) noexcept
{
    if (event.type == SDL_EVENT_QUIT)
    {
        request_stack_clear();
    }

    auto *p{get_context().ctx_player};
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE && !event.key.repeat)
    {
        auto *win{&get_context().ctx_sdl->window};
        const auto next_mouse_mode{!SDL_GetWindowRelativeMouseMode(*win)};
        SDL_SetWindowRelativeMouseMode(*win, next_mouse_mode);
        p->set_active(next_mouse_mode);
    }

    if (current_editor_world)
    {
        p->handle_event(event, current_editor_world->get_command_queue());
        current_editor_world->handle_event(event);
    }
    return true;
}

menu_state::menu_state(state_stack &stack, const context &context)
    : state{stack, context},
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
    if (m_preview_texture)
    {
        glDeleteTextures(1, &m_preview_texture);
    }
    get_context().ctx_sounds->get(SoundIdentifier::LOADING_THEME_SONG).stop(SoundIdentifier::LOADING_THEME_SONG);
}

// Uniform rectangular button size: tracks the available width (so it resizes with the window)
// but is capped so buttons never get overly wide, and always fits within the region.
static ImVec2 menu_button_size() noexcept
{
    const float scale = ImGui::GetIO().FontGlobalScale;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float max_width = std::max(160.f, std::round(320.f * scale));
    const float width = std::max(1.f, std::min(avail, max_width));
    const float height = std::round(ImGui::GetFrameHeight() * golden_ratio);
    return ImVec2(width, height);
}

void menu_state::draw_heading() const noexcept
{
    if (ImGui::Button("Resume", menu_button_size()))
    {
        SDL_SetWindowRelativeMouseMode(get_context().ctx_sdl->window, true);
        get_context().ctx_player->set_active(true);
        request_stack_pop();
    }
    ImGui::SameLine();
    ImGui::NextColumn();
    if (ImGui::Button("Close", menu_button_size()))
    {
        request_stack_clear();
    }
}

void menu_state::draw_content(player *p, player_configs &configs, float content_width, float available_height) const noexcept
{
    ImGui::BeginChild("##content", ImVec2(content_width, available_height), ImGuiChildFlags_Borders);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * golden_minor_ratio);
    if (ImGui::BeginTabBar("##tabs"))
    {
        draw_mode_tabs();
        if (ImGui::BeginTabItem("Physics"))
        {
            if (auto *editor = get_stack().peek_state<editor_state *>())
                editor->draw_physics_controls();
            else
                ImGui::TextUnformatted("Physics sandbox is only available in the editor.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Audio"))
        {
            float master = configs.master_volume(), music = configs.music_volume(), sfx = configs.sfx_volume();
            bool muted = configs.mute_audio();
            ImGui::Checkbox("Mute All", &muted);
            ImGui::SliderFloat("Master Volume", &master, 0.f, 1.f);
            ImGui::SliderFloat("Music Volume", &music, 0.f, 1.f);
            ImGui::SliderFloat("Effects Volume", &sfx, 0.f, 1.f);
            configs.master_volume(master).music_volume(music).sfx_volume(sfx).mute_audio(muted);
            get_context().ctx_sounds->get(SoundIdentifier::LOADING_THEME_SONG).set_volumes(configs.master_volume(), configs.music_volume(), configs.sfx_volume(), configs.mute_audio());
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopItemWidth();
    ImGui::EndChild();
}

void menu_state::draw_menu(player *p, player_configs &configs) const noexcept
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(display, ImGuiCond_Always);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##menu", nullptr, flags))
    {
        draw_heading();
        const auto style{ImGui::GetStyle()};
        const float available_width = ImGui::GetContentRegionAvail().x;
        const float sidebar_width = std::min(std::clamp(std::round(available_width * golden_minor_ratio), 200.f, 420.f),
                                             available_width * .4f);
        const float content_width = std::max(1.f, available_width - sidebar_width - style.ItemSpacing.x);
        const float available_height = ImGui::GetContentRegionAvail().y;
        ImGui::BeginChild("##sidebar", ImVec2(sidebar_width, available_height), ImGuiChildFlags_Borders);
        draw_mode_sidebar(available_height);
        ImGui::Separator();
        ImGui::EndChild();
        ImGui::SameLine();

        draw_content(p, configs, content_width, available_height);
    }
    ImGui::End();
}

void menu_state::draw() const noexcept
{
    const auto &ctx = get_context();
    auto *active_player = ctx.ctx_player;
    auto &&configs = active_player->get_configs();

    ImGui::GetIO().FontGlobalScale = configs.gui_font_scale();
    ImGui::PushFont(ctx.ctx_fonts->get(static_cast<FontIdentifier>(active_player->get_font_index())).get());

    draw_menu(active_player, configs);

    ImGui::PopFont();
}

bool menu_state::update(float, mazes::randomizer &) noexcept
{
    if (rebuild_world_requested)
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
    {
        constexpr ImVec4 heading_color{.745f, .863f, .498f, 1.f};
        constexpr auto TITLE{"{Breaking Walls {gamedev}}"};
        ImGui::TextColored(heading_color, "%s\n", TITLE);
        ImGui::NewLine();
        ImGui::Separator();
        ImGui::TextUnformatted("Key Bindings");
        const float row_height = ImGui::GetFrameHeightWithSpacing();
        if (ImGui::BeginTable("##keybinds", 2,
                              ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(-1.f, std::max(3.f * row_height, available_height - 300.f))))
        {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 100.f);
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
        ImGui::BulletText("L Click: Destroy block");
        ImGui::BulletText("R Click: Build block");
        ImGui::BulletText("M Click: Copy block type");
        ImGui::BulletText("Scroll: Cycle block types");
    }
}

void menu_state::draw_heightmap_preview() const noexcept
{
    const auto map = heightmap_library::instance().current();
    if (!map || map->side <= 0)
    {
        ImGui::TextUnformatted("No heightmap available.");
        return;
    }
    if (map != m_preview_source)
    {
        // Top-down view: material colour of each column, shaded by elevation.
        std::vector<unsigned char> pixels(static_cast<std::size_t>(map->side) * static_cast<std::size_t>(map->side) * 4);
        for (int z = 0; z < map->side; ++z)
        {
            for (int x = 0; x < map->side; ++x)
            {
                const float e = map->elevation(x, z);
                const auto &kd = heightmap::material_for(e).kd;
                const float shade = 0.65f + 0.35f * e;
                auto *px = &pixels[(static_cast<std::size_t>(z) * static_cast<std::size_t>(map->side) + static_cast<std::size_t>(x)) * 4];
                for (int c = 0; c < 3; ++c)
                {
                    px[c] = static_cast<unsigned char>(std::clamp(kd[c] * shade, 0.f, 1.f) * 255.f);
                }
                px[3] = 255;
            }
        }
        if (!m_preview_texture)
        {
            glGenTextures(1, &m_preview_texture);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_preview_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, map->side, map->side, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_preview_source = map;
    }
    const float size = std::min(ImGui::GetContentRegionAvail().x, 256.f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - size) * 0.5f);
    ImGui::Image(static_cast<ImTextureID>(m_preview_texture), ImVec2(size, size));
}

void menu_state::draw_builder_tab() const noexcept
{
    auto &&configs = get_context().ctx_player->get_configs();
    draw_heightmap_preview();
    ImGui::Separator();
    static int island_size = 128, octaves = 3;
    static float frequency = 3.0f;
    static bool apply_maze = false;
    static char tag[256]{};
    if (tag[0] == '\0')
    {
        SDL_strlcpy(tag, configs.tag().c_str(), std::size(tag));
    }
    if (ImGui::InputText("Tag Message", tag, std::size(tag)))
    {
        configs.tag(std::string{tag});
    }
    ImGui::SliderInt("Island Size", &island_size, heightmap::MIN_SIDE, heightmap::MAX_SIDE);
    ImGui::SliderInt("Noise Octaves", &octaves, 1, 6);
    ImGui::SliderFloat("Noise Frequency", &frequency, 1.f, 8.f);
    ImGui::Checkbox("Apply maze to heightmap", &apply_maze);

    thread_local mazes::randomizer rng{};
    auto maze = configs.maze();
    static constexpr auto ROWS{maze.MAX_ROWS / 4}, COLUMNS{maze.MAX_COLUMNS / 4};
    static constexpr auto LEVELS{maze.MAX_LEVELS / 2};
    static constexpr auto SEED{1'000};
    static int rows = maze.rows(), columns = maze.columns(), levels = maze.levels(), seed = rng.get_int(0, SEED);
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
                     .levels(levels));

    ImGui::Separator();
    if (ImGui::Button("Make New World", menu_button_size()))
    {
        heightmap_params params;
        params.side = island_size;
        params.octaves = octaves;
        params.frequency = frequency;
        params.apply_maze = apply_maze;
        params.maze = configs.maze();
        params.seed = rng.get_int(0, SEED);
        heightmap_library::instance().add(params);
        rebuild_world_requested = true;
    }
}

void menu_state::draw_graphics_tab() const noexcept
{
    auto &&p = get_context().ctx_player;
    auto &&configs = p->get_configs();

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
                                  p->get_font_index() == static_cast<int>(font_id)))
            {
                p->set_font_index(static_cast<int>(font_id));
            }
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
        if (!SDL_SetWindowFullscreen(get_context().ctx_sdl->window, fullscreen ? SDL_WINDOW_FULLSCREEN : 0))
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Fullscreen change failed: %s", SDL_GetError());
        }
    }
    configs.vsync(vsync).fullscreen(fullscreen).use_bloom_effect(bloom).exposure_val(exposure).gui_font_scale(font_scale).show_stats_window(show_stats);
}

void menu_state::draw_camera_tab() const noexcept
{
    auto &&configs = get_context().ctx_player->get_configs();
    bool invert = configs.invert_mouse();
    float fov = configs.fov();
    int ortho = configs.ortho_scaling(), day = configs.day_length();
    ImGui::Checkbox("Invert Mouse Y-Axis", &invert);
    ImGui::SliderFloat("Field of View", &fov, 30, 120);
    ImGui::SliderInt("Orthographic Scale", &ortho, 0, 64);
    ImGui::SliderInt("Day Length (s)", &day, 60, 1800);
    configs.invert_mouse(invert).fov(fov).ortho_scaling(ortho).day_length(day);
}

void menu_state::draw_player_tab() const noexcept
{
    auto *active_player = get_context().ctx_player;
    ImGui::Text("Name: %s", active_player->get_name().c_str());
    ImGui::Text("Local time: %s", active_player->get_local_time().data());
    ImGui::Text("pos.x: %.2f\npos.y: %.2f\npos.z: %.2f\n",
                active_player->pos.x, active_player->pos.y, active_player->pos.z);
    ImGui::Text("Selected block: %s", item::to_block_name(
                                          static_cast<item::BlockType>(active_player->get_selected_item_index()))
                                          .data());

    const auto &icon = get_context().ctx_textures->get(TextureIdentifier::FLIPS_AND_ALE_ICON);
    if (icon.gl_texture != 0 && icon.width > 0 && icon.height > 0)
    {
        auto &&font_size{ImGui::GetFontSize()};
        ImGui::Image(reinterpret_cast<void *>(static_cast<intptr_t>(icon.gl_texture)), ImVec2(icon.width >> 1, icon.height >> 1));
    }
}

void menu_state::draw_exports_tab() const noexcept
{
    auto *active_player = get_context().ctx_player;
    auto &library = heightmap_library::instance();
    ImGui::TextUnformatted("Heightmaps");
    if (ImGui::BeginListBox("##heightmaps", ImVec2(-1.f, 6 * ImGui::GetTextLineHeightWithSpacing())))
    {
        const auto &entries = library.entries();
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const auto &map = *entries[i].map;
            const std::string label = map.name + " (" + std::to_string(map.side) + "x" +
                                      std::to_string(map.side) + ")##" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), i == library.selected()) && i != library.selected())
            {
                library.select(i);
                rebuild_world_requested = true;
            }
        }
        ImGui::EndListBox();
    }
    ImGui::BeginDisabled(library.entries().empty());
    if (ImGui::Button("Delete Selected", menu_button_size()))
    {
        library.remove(library.selected());
        if (!library.current())
        {
            library.add(heightmap_params{});
        }
        rebuild_world_requested = true;
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    if (m_export_in_progress && active_player->is_artifact_export_ready())
    {
        m_cached_artifacts = active_player->get_artifact_export_result();
        m_export_in_progress = false;
    }
    if (ImGui::Button("Generate Artifacts (Async)", menu_button_size()))
    {
        active_player->start_async_artifact_export();
        m_cached_artifacts.clear();
        m_export_in_progress = true;
    }
    ImGui::BeginDisabled(m_cached_artifacts.empty() || m_export_in_progress);
    if (ImGui::Button("Download File", menu_button_size()))
    {
#if defined(__EMSCRIPTEN__)
        get_context().ctx_artifacts->get(ArtifactIdentifier::WORLD_OBJ_1).set_download_ready(true);
#endif
        handle_artifacts(active_player, *get_context().ctx_artifacts, m_cached_artifacts);
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s", m_cached_artifacts.empty() ? "No artifacts generated yet." : m_cached_artifacts.substr(0, 512).c_str());
}

void menu_state::draw_mode_tabs() const noexcept
{
    if (ImGui::BeginTabItem("Builder"))
    {
        draw_builder_tab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Graphics"))
    {
        draw_graphics_tab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Camera"))
    {
        draw_camera_tab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Player"))
    {
        draw_player_tab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Exports"))
    {
        draw_exports_tab();
        ImGui::EndTabItem();
    }
}

bool menu_state::handle_event(SDL_Event &event) noexcept
{
    if (event.type == SDL_EVENT_QUIT)
    {
        request_stack_clear();
    }
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        get_context().ctx_player->set_active(false);
    }
    return event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
}

bool menu_state::is_ready_to_rebuild_world() const noexcept { return rebuild_world_requested; }
