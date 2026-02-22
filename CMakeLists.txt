cmake_minimum_required(VERSION 3.5...4.1)

set(BREAKING_WALLS_PROJECT_VERSION 0.5.3)

project(BreakingWalls VERSION ${BREAKING_WALLS_PROJECT_VERSION} LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BREAKING_WALLS_APP_NAME "breakingwalls")

# Automatically define BREAKING_WALLS_DEBUG for Debug and RelWithDebInfo builds
add_compile_definitions(
    $<$<CONFIG:Debug>:BREAKING_WALLS_DEBUG>
    $<$<CONFIG:RelWithDebInfo>:BREAKING_WALLS_DEBUG>
)

set(BREAKING_WALLS_DEMO_DEF "BREAKING_WALLS_DEMO")
option(BREAKING_WALLS_DEMO_DEF "Define BREAKING_WALLS_DEMO macro" OFF)

include(NoInSourceBuilds)

add_subdirectory(src bin)
