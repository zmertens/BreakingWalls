// Main file for voxel editor with maze generation.

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

#include <MazeBuilder/randomizer.h>

#include "craft.h"

static constexpr auto WINDOW_WIDTH = 1200u, WINDOW_HEIGHT = 800u;

constexpr std::string_view TITLE{"Breaking Walls"};

auto VOXEL_ENGINE = std::make_shared<craft>();

// Setup for Emscripten/WebAssembly
// Bind a getter method from C++ so that it can be accessed in the frontend with JS
#if defined(__EMSCRIPTEN__)
#include <emscripten/bind.h>

std::shared_ptr<craft> get()
{
    return VOXEL_ENGINE;
}

EMSCRIPTEN_BINDINGS(craft_module)
{
    // Module.get() → returns the shared_ptr to the singleton engine instance.
    // Always call this first in onRuntimeInitialized before touching any other API.
    emscripten::function("get", &get, emscripten::allow_raw_pointers());

    emscripten::class_<craft>("craft")
        .smart_ptr<std::shared_ptr<craft>>("std::shared_ptr<craft>")
        // JS should use Module.get() rather than constructing a second instance.
        .constructor<>()
        // ── Legacy synchronous download path ─────────────────────────────────
        // inst.artifacts() → full Wavefront OBJ string; may block if called before ready.
        .function("artifacts", &craft::artifacts)
        // inst.is_download_ready() → true once the OBJ data is available for download.
        .function("is_download_ready", &craft::is_download_ready)
        // inst.reset_download_flag() → call after consuming artifacts() to re-arm the flag.
        .function("reset_download_flag", &craft::reset_download_flag)
        // ── Async (non-blocking) export path — preferred for web ─────────────
        // inst.begin_export() → starts the background OBJ-generation worker; returns immediately.
        .function("begin_export", &craft::begin_export)
        // inst.is_export_ready() → non-blocking poll; returns true when the worker is done.
        .function("is_export_ready", &craft::is_export_ready)
        // inst.get_export() → retrieve the finished OBJ string; empty if worker not done yet.
        .function("get_export", &craft::get_export)
        // inst.get_export_status() → "idle" | "running" | "ready" for UI status overlays.
        .function("get_export_status", &craft::get_export_status)
        // ── Maze configuration (set before calling begin_export) ─────────────
        .function("maze", &craft::maze)
        .function("get_version", &craft::get_version);
}
#endif // EMSCRIPTEN

int main()
{
    try
    {
        VOXEL_ENGINE->initialize(TITLE, WINDOW_WIDTH, WINDOW_HEIGHT);
        mazes::randomizer rng;
        if (!VOXEL_ENGINE->run(std::ref(rng)))
        {
            throw std::runtime_error("Running Voxels app failed.");
        }
    }
    catch (std::exception &ex)
    {
        std::cerr << ex.what() << std::endl;
    }

    return EXIT_SUCCESS;
}
