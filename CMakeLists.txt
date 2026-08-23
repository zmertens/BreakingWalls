cmake_minimum_required(VERSION 3.25...4.1)

project(BreakingWalls VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BREAKING_WALLS_APP_NAME "Breaking_Walls_App")

add_subdirectory(Voxels)
