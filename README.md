# Breaking Walls

Breaking Walls is a C++20 voxel application with two modes: a stacked-maze tracing game and a voxel editor. It uses SDL3 and OpenGL on desktop; the web build targets WebGL 2 through Emscripten.

## Current behavior

- **Game:** Trace from the blue start through hidden rewards and hazards to the green goal. Activate five yellow lights to descend to the next board. Board side defaults to 8 and can be set from 4 to 40 in the Game tab. Game score, strokes, and level history are in memory and reset when starting a new game or switching modes.
- **Editor:** Build and edit voxel terrain, configure and generate maze ghosts, and use the perspective, isometric, or orthographic camera. Its sky now has a time-of-day palette with a sun, moon, and soft halos. Editor data is stored in `breakingwalls.db` on desktop; web builds use an in-memory database.
- **Common menu:** Resume or switch modes; adjust VSync, fullscreen, bloom, exposure, and font scale. The Game tab also has the board and maze-environment controls; editor tools and settings are shown only in editor mode.
- **Input:** In game, hold the left mouse button or one finger from a blue start and trace toward the goal. Right/middle drag or arrow keys pan; the wheel zooms; Ctrl+wheel or Q/E rotates; R resets the camera. Escape opens the menu. The menu includes the mode-specific control reminders.

## Configure and build

The CMake presets use Ninja Multi-Config for desktop and web builds. The project requires CMake 3.22 or newer and a C++20 toolchain. CMake fetches SDL3, SFML Audio, MazeBuilder, and Box3D during configuration, so the first configure needs network access.

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
