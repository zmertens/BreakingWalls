# Breaking Walls

Breaking Walls is a 2D game application with voxel-engine-like features. It requires C++20 features and uses SDL3 and OpenGL on desktop; the web build targets WebGL 2 through Emscripten. See [Configure and build](#configure-and-build) for more info.

## Graphics

- **Game:** Trace paths in realtime towards randomized goals and gain points
- **Editor:** Build and edit voxel terrain, configure and generate levels, and use a dashboard to view level details. Open the menu's **Physics** tab to experiment with a Box3D simulation: exposed heightmap voxels in a camera-facing slice become dynamic, and falling spheres can knock them loose. Editor data is stored in `breakingwalls.db` on desktop; web builds use an in-memory database.
- **Common menu:** Resume or switch modes; adjust VSync, fullscreen, bloom, exposure, and font scale - graphics and audio settings as well.
- **Input:** In game, hold the left mouse button or one finger or joystick (A) from a random block and trace continuously toward the goal. Escape opens the menu. The menu includes the mode-specific control reminders.
  - **Camera**: Right/middle drag or arrow keys pan; the wheel zooms; Ctrl+wheel or Q/E rotates; R resets the camera.

## Configure and build

The CMake presets use Ninja Multi-Config for desktop and web builds. CMake fetches SDL3, SFML, MazeBuilder, and Box3D during configuration, so the first configure needs network access.

### Desktop

```sh
cmake --preset build-ninja
cmake --build build-ninja --config Debug
```

Run `Breaking_Walls_App` from the matching configuration output directory under `build-ninja/Voxels/` so the copied `Shaders`, `Textures`, and `SoundFX` assets are available.

### WebAssembly

The `build-wasmjs` preset expects Emscripten at `../emsdk` relative to the repository.

```sh
cmake --preset build-wasmjs
cmake --build build-wasmjs --config Release
```

Serve the generated files over HTTP rather than opening the HTML file directly. Run the copied `secure_http_server.py` from the app's output directory and open `http://localhost:8000`. The server sets the cross-origin isolation headers required by the web build's pthreads.

The `build-msvc` configure/build presets are also provided for Visual Studio 2022.

## Configuration notes

- There are no project-specific CMake feature switches or test targets currently defined. The `build-ninja`, `build-wasmjs`, and `build-msvc` presets are the supported build configurations.
- Apply a board-side change with **Apply / reset board**; this starts a fresh game board. The **Maze environment** checkbox toggles the generated maze backdrop. If environment generation fails, the game logs the error and remains playable with its neon background.
- Desktop editor saves use `breakingwalls.db` in the process working directory. Keep that location writable. Web builds use `:memory:`, so editor database changes do not persist across page reloads.
- The web build uses shared-memory threads and requires a browser context with cross-origin isolation. Use the included server instead of `file://` or a basic static server.
- The maze cubemap is generated at runtime in a temporary directory and cleaned up when leaving game mode. The temporary directory must be writable.
