![Sample](resources/icon.bmp)

# Breaking Walls

A 2D top-down physics game.

  - Simple scoring system that includes maintaining a string of highlighted bricks
  - Balls interact with the bricks and break them after repeated bounces
  - Updated event handling (mouse, touch, keyboard)
  - Maintains high scores via a network connection with [Corners](https://github.com/zmertens/Corners) or locally on harddrive
  - Spatialized sound effects in 2D

---

## CMake Configuration

[CMake](https://cmake.org) is used for project configuration.

Here are the external dependencies which can be pulled from the Web:

  - [box2d](https://box2d.org/documentation/hello.html)
  - [emscripten](https://emscripten.org/index.html)
  - [Maze Builder](https://zmertens.github.io/mazebuilder.github.io/index.html)
  - [SDL](https://libsdl.org)
  - [SFML](https://sfml-dev.org)

Use the following CMake options to configure the project:

| CMake Option | Default | Description |
|--------------|---------|------------ |
| PLATFORM_WEB | OFF | Build for Web platform using Emscripten |

### Example Builds

`cmake -G"Visual Studio 17 2022" -S . -B build-multi -DBW_PLATFORM_WEB:BOOL=OFF`

`cmake -G"Ninja Multi-Config" -S . -B build-web -DBW_PLATFORM_WEB:BOOL=ON -DCMAKE_TOOLCHAIN_FILE:FILEPATH=${my/emsdk/repo}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake`

---
