# Breaking Walls

Breaking Walls is a 3D climbing game with voxel-engine-like features. It requires the C++20 standard and uses SDL3 and OpenGL on desktop; the web build targets WebGL 2 with translation done by Emscripten. See [Configure and build](#configure-and-build) for more info.

## Gameplay

### World Navigation

### Scoring and Obstacles

### Penalties

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

