include(BuildInfo)

include(FetchGitRepo)
set(DEPENDENCY_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install")
if(NOT CMAKE_INSTALL_PREFIX_INITIALIZED_BY_USER)
    set(CMAKE_INSTALL_PREFIX "${DEPENDENCY_INSTALL_PREFIX}" CACHE PATH "Install path for dependencies")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries")

# Get SFML (Graphics + Window)
set(SFML_REPO_URL "https://github.com/zmertens/SFML.git")
set(SFML_GIT_TAG "sfml3_with_tls")
message(STATUS "Fetching SFML from ${SFML_REPO_URL} at tag ${SFML_GIT_TAG}")
set(SFML_BUILD_AUDIO OFF)
set(SFML_BUILD_NETWORK OFF)
set(SFML_BUILD_GRAPHICS ON)
set(SFML_BUILD_WINDOW ON)
set(SFML_USE_SYSTEM_DEPS OFF)
set(SFML_DISABLE_FIND_THREADS ON)
set(SFML_USE_DRM OFF CACHE BOOL "Use DRM")
FetchGitRepo(SFML ${SFML_REPO_URL} ${SFML_GIT_TAG})

# Get MazeBuilder
set(MAZE_BUILDER_REPO_URL "https://github.com/zmertens/MazeBuilder")
set(MAZE_BUILDER_GIT_TAG "v7.9.0")
message(STATUS "Fetching MazeBuilder from ${MAZE_BUILDER_REPO_URL} at tag ${MAZE_BUILDER_GIT_TAG}")
FetchGitRepo(MazeBuilder ${MAZE_BUILDER_REPO_URL} ${MAZE_BUILDER_GIT_TAG})

add_executable(${BREAKING_WALLS_APP_NAME}
    ${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp
)

target_compile_features(${BREAKING_WALLS_APP_NAME} PRIVATE cxx_std_20)

target_link_libraries(${BREAKING_WALLS_APP_NAME} PRIVATE
    MazeBuilder::MazeBuilder
    SFML::Graphics
    SFML::Window
)

message(STATUS "Configuring ${BREAKING_WALLS_APP_NAME} (AmazingSFML maze demo)")
