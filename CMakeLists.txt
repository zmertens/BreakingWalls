cmake_minimum_required(VERSION 3.22...4.1)

project(BreakingWalls VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emscripten: Enable atomics and bulk-memory for shared memory support
if(EMSCRIPTEN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -matomics -mbulk-memory")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -matomics -mbulk-memory")
endif()

set(BREAKING_WALLS_APP_NAME "Breaking_Walls_App")

add_subdirectory(Voxels)
