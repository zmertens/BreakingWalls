// Showcases basic voxel editing capabilities in gameplay
// Follows game development patterns like State Stacks
// Creates mazes with Maze Builder library
// Physics using box3d
// Originally written in C99, and then it was ported to C++20

#include "craft.h"

#include <dearimgui/imgui.h>
#include <dearimgui/backends/imgui_impl_sdl3.h>
#include <dearimgui/backends/imgui_impl_opengl3.h>

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#include <emscripten_local/emscripten_mainloop_stub.h>
#else
#include <glad/gl.h>
#endif

#include <SDL3/SDL.h>

#include "db_manager.h"
#include "font.h"
#include "geometries.h"
#include "player.h"
#include "resource_identifiers.h"
#include "resource_manager.h"
#include "sdl_gl_helper.h"
#include "shader.h"
#include "state_management.h"
#include "soundfx.h"
#include "texture.h"
#include "world.h"

#include <MazeBuilder/buildinfo.h>
#include <MazeBuilder/randomizer.h>
#include <MazeBuilder/runtime_app.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

struct craft::craft_impl
{
    player active_player;
    font_manager game_fonts;
    sdl_gl_helper simple_direct_medialayer;
    shader_manager world_shaders;
    soundfx_manager world_sounds;
    // Declared after the SDL layer so it is destroyed first; run() also closes it explicitly.
    db_manager world_db;
    artifact_exporter_manager artifact_exporters;
    texture_manager world_textures;
    bloom_pass_manager world_bloom_effects;

    std::unique_ptr<state_stack> crafting_states;

    craft_impl(std::string_view title, const int w, const int h)
    {
        start_SDL(title, w, h);

        if (!simple_direct_medialayer.gl_context)
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "No OpenGL context available; skipping ImGui setup (%s)\n", SDL_GetError());
            return;
        }

        setup_imgui();

        crafting_states = std::make_unique<state_stack>(state::context{
            std::ref(game_fonts),
            std::ref(world_shaders),
            std::ref(world_textures),
            std::ref(world_bloom_effects),
            std::ref(active_player),
            std::ref(simple_direct_medialayer),
            std::ref(world_sounds),
            std::ref(artifact_exporters),
            std::ref(world_db)});

        register_states();

        crafting_states->push_state(StateIdentifier::EDITOR);
        crafting_states->push_state(StateIdentifier::LOADING);
    }

    void register_states() const noexcept
    {
        crafting_states->register_state<editor_state>(StateIdentifier::EDITOR);
        crafting_states->register_state<loading_state>(StateIdentifier::LOADING);
        crafting_states->register_state<menu_state>(StateIdentifier::MENU);
    }

    void start_SDL(std::string_view title, const int w, const int h) noexcept
    {
        if (simple_direct_medialayer.initialize(title, w, h))
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL initialized successfully.\n");
        }
    }

    // DEAR IMGUI INIT - Setup Dear ImGui context
    void setup_imgui() const noexcept
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
        ImGui::GetIO().IniFilename = nullptr;

        ImGui_ImplSDL3_InitForOpenGL(simple_direct_medialayer.window, simple_direct_medialayer.gl_context);

        std::string glsl_version;

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
        glsl_version = "#version 100";
#else
        glsl_version = "#version 130";
#endif

        ImGui_ImplOpenGL3_Init(glsl_version.c_str());
    }

    void render_FPS(const double elapsed) const noexcept
    {
        // Calculate instantaneous FPS and frame time from real render frame timing
        const auto safe_elapsed = elapsed > 0.0 ? elapsed : 1.0;
        const auto fps = static_cast<int>(1000.0 / safe_elapsed);
        const auto frame_time = static_cast<float>(safe_elapsed);

        static auto fps_timer{0.0};
        static auto smoothed_fps_counter{0};
        static auto fps_timer_smoothed{0.0f};

        // Update smoothed values periodically for display
        fps_timer += safe_elapsed;
        if (constexpr double FPS_UPDATE_INTERVAL = 250.0; fps_timer >= FPS_UPDATE_INTERVAL)
        {
            smoothed_fps_counter = fps;
            fps_timer_smoothed = frame_time;
            fps_timer = 0.0;
        }

        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10.0f, 10.0f), ImGuiCond_Always,
                                ImVec2(1.0f, 0.0f));

        ImGui::SetNextWindowBgAlpha(0.65f);

        constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration |
                                                 ImGuiWindowFlags_AlwaysAutoResize |
                                                 ImGuiWindowFlags_NoSavedSettings |
                                                 ImGuiWindowFlags_NoFocusOnAppearing |
                                                 ImGuiWindowFlags_NoNav |
                                                 ImGuiWindowFlags_NoMove;

        if (this->active_player.get_configs().show_stats_window() && ImGui::Begin("FPS Stats", nullptr, windowFlags))
        {
            ImGui::Text("FPS: %d", smoothed_fps_counter);
            ImGui::Text("ms / frame: %.2f ms", fps_timer_smoothed);
            ImGui::End();
        }
    }

    void process_input() const noexcept
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT)
            {
                crafting_states->handle_event(event);
                break;
            }

            // Resize and focus loss must reach the world even when ImGui captures input.
            if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
                event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE)
            {
                crafting_states->handle_event(event);
                continue;
            }

            // Check if ImGui wants to capture this event
            const ImGuiIO &io = ImGui::GetIO();
            const bool imgui_wants_keyboard = io.WantCaptureKeyboard;
            const bool imgui_wants_mouse = io.WantCaptureMouse;
            const bool should_forward_event =
                (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP &&
                 event.type != SDL_EVENT_TEXT_INPUT && !imgui_wants_mouse) ||
                (!imgui_wants_keyboard && !imgui_wants_mouse);

            if (should_forward_event)
            {
                crafting_states->handle_event(event);
            }
        }
    }

    void update(const float delta_time, mazes::randomizer &rng) const noexcept
    {
        crafting_states->update(delta_time, std::ref(rng));
    }

    void render(const double elapsed) const noexcept
    {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui_ImplSDL3_NewFrame();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        ImGui::GetIO().FontGlobalScale = active_player.get_configs().gui_font_scale();
        ImGui::PushFont(game_fonts.get(static_cast<FontIdentifier>(active_player.get_font_index())).get());

        crafting_states->draw();

        render_FPS(elapsed);

        ImGui::PopFont();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(this->simple_direct_medialayer.window);
    }
}; // craft_impl

craft::craft() = default;
craft::~craft() = default;

void craft::initialize(std::string_view title, const unsigned int w, const unsigned int h) noexcept
{
    if (!crafting_impl)
    {
        crafting_impl = std::make_unique<craft_impl>(title, w, h);
    }
    else
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Craft already initialized\n");
    }
}

// @brief Main run loop for the SDL app
bool craft::run(mazes::randomizer &rng) const noexcept
{
    auto &&app = this->crafting_impl;
    if (!app || !app->simple_direct_medialayer.window || !app->simple_direct_medialayer.gl_context || !app->crafting_states)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL did not initialize (%s)\n", SDL_GetError());
        return false;
    }

    SDL_Log("Using Maze Builder library v%s\n", get_version().c_str());
    SDL_Log("%s\n", maze("--rows=8 --columns=8 --algo=dfs -d[0:7]").c_str());

    // LOCAL VARIABLES
    const auto perf_frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    auto previous = SDL_GetPerformanceCounter();
    auto last_commit = SDL_GetPerformanceCounter();
    double time_step = 0.0, accumulator = 0.0;

    rng.seed(previous);

    // Trigger an initial update to set states, render pipeline ready
    app->crafting_states->update(time_step, std::ref(rng));

    // BEGIN EVENT LOOP
#if defined(__EMSCRIPTEN__)
    EMSCRIPTEN_MAINLOOP_BEGIN
#else
    while (!app->crafting_states->is_empty())
#endif
    {
        // FRAME RATE
        static constexpr auto FIXED_TIME_STEP = 1000.0 / 60.0;
        const auto current = SDL_GetPerformanceCounter();
        const auto elapsed = std::clamp((static_cast<double>(current - previous) * 1000.0) / perf_frequency,
                                        0.0,
                                        50.0);
        previous = current;
        accumulator += elapsed;

        app->process_input();

        // FLUSH DATABASE
        static constexpr auto COMMIT_INTERVAL = 5000u;
        const auto commit_elapsed = static_cast<double>(current - last_commit) * 1000.0 / perf_frequency;
        if (commit_elapsed > COMMIT_INTERVAL)
        {
            app->world_db.commit();
            last_commit = current;
        }

        while (accumulator >= FIXED_TIME_STEP)
        {
            time_step += FIXED_TIME_STEP;
            accumulator -= FIXED_TIME_STEP;

            app->update(FIXED_TIME_STEP, std::ref(rng));
        }

        app->render(elapsed);

        time_step = time_step >= 1000.0 ? 0.0 : time_step;
    } // EVENT LOOP

#if defined(__EMSCRIPTEN__)
    EMSCRIPTEN_MAINLOOP_END;
    emscripten_cancel_main_loop();
#endif

    SDL_Log("Game ended, beginning cleanup...\n");
    SDL_Log("Clearing state stack...\n");
    if (!app->crafting_states->is_empty())
    {
        app->crafting_states->clear_states();
        app->crafting_states->apply_pending_changes();
    }

    // Cleanup ImGui
    app->world_bloom_effects.clear();
    app->world_textures.clear();
    app->world_shaders.clear();

    SDL_Log("Shutting down ImGui...\n");
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    // States are gone, so nothing else touches the database; close it before SDL goes away.
    SDL_Log("Closing database...\n");
    app->world_db.close();

    // Cleanup SDL (this must be last)
    SDL_Log("Shutting down SDL...\n");
    app->simple_direct_medialayer.destroy_and_quit();

    SDL_Log("Cleanup complete, exiting gracefully.\n");

    return true;
} // run

std::string craft::artifacts() const noexcept
{
    return this->crafting_impl->active_player.artifacts();
}

bool craft::is_download_ready() const noexcept
{
    const auto &exporters = this->crafting_impl->artifact_exporters;
    return !exporters.is_empty() && exporters.get(ArtifactIdentifier::WORLD_OBJ_1).is_download_ready();
}

void craft::reset_download_flag() const noexcept
{
    auto &exporters = this->crafting_impl->artifact_exporters;
    if (!exporters.is_empty())
    {
        exporters.get(ArtifactIdentifier::WORLD_OBJ_1).set_download_ready(false);
    }
}

// ── Async export ─────────────────────────────────────────────────────────────

void craft::begin_export() const noexcept
{
    // Kick off the async OBJ-generation worker; safe to call from JS event handler.
    this->crafting_impl->active_player.start_async_artifact_export();
}

bool craft::is_export_ready() const noexcept
{
    // Non-blocking poll: returns true once the background OBJ worker has finished.
    return this->crafting_impl->active_player.is_artifact_export_ready();
}

std::string craft::get_export() noexcept
{
    // Retrieve the finished OBJ string; empty if called before is_export_ready().
    const std::string result = this->crafting_impl->active_player.get_artifact_export_result();
    // Mark download flag so legacy is_download_ready() path is also satisfied.
    if (!result.empty() && !this->crafting_impl->artifact_exporters.is_empty())
    {
        this->crafting_impl->artifact_exporters.get(ArtifactIdentifier::WORLD_OBJ_1).set_download_ready(true);
    }
    return result;
}

std::string craft::get_export_status() const noexcept
{
    // Returns a plain string the JS layer can show in a status overlay.
    // "idle"    – no export requested yet
    // "running" – worker thread active
    // "ready"   – result available, call get_export()
    if (this->crafting_impl->active_player.is_artifact_export_ready())
    {
        return "ready";
    }
    // Check whether a future is in-flight (valid but not yet ready).
    // We reuse is_artifact_export_ready() for the ready check and infer running
    // from the download flag being false and an export having been requested.
    if (this->is_download_ready() && !this->crafting_impl->active_player.is_artifact_export_ready())
    {
        return "ready";
    }
    return "idle";
}

std::string craft::maze(const std::string &args) const noexcept
{
    return std::string{mazes::runtime_app::instance()->apply(args)};
}

std::string craft::get_version() const noexcept
{
    return mazes::buildinfo::VERSION + " - " + mazes::buildinfo::COMMIT_SHA;
}
