cmake_minimum_required(VERSION 3.11...4.1)

set(BREAKING_WALLS_PROJECT_VERSION 0.3.0)

project(BreakingWalls VERSION ${BREAKING_WALLS_PROJECT_VERSION} LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BREAKING_WALLS_APP_NAME "breakingwalls")
set(BREAKING_WALLS_DEBUG_DEF "BW_DEBUG")

option(BREAKING_WALLS_PLATFORM_WEB "Build for Web platform using Emscripten" OFF)

include(NoInSourceBuilds)

if (BREAKING_WALLS_PLATFORM_WEB)
    if (NOT EMSCRIPTEN)
        message(FATAL_ERROR "Emscripten toolchain is required to build for Web platform. Please set up Emscripten and use the appropriate CMake toolchain file.")
    endif()
endif()

add_subdirectory(src)
