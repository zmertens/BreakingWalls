include(BuildInfo)

include(FetchGitRepo)

find_package(OpenGL REQUIRED)

# Get box2d
FetchGitRepo(box2d https://github.com/erincatto/box2d.git v3.1.1)
# Add Box2D include directory if not already included by find_package
if(TARGET box2d)
    set_property(TARGET box2d PROPERTY COMPILE_WARNING_AS_ERROR OFF)
    get_target_property(BOX2D_INCLUDE_DIRS box2d INTERFACE_INCLUDE_DIRECTORIES)
endif()

# Get SDL
set(SDL_SHARED FALSE CACHE BOOL "Build a SDL shared library (if available)")
set(SDL_STATIC TRUE CACHE BOOL "Build a SDL static library (if available)")
FetchGitRepo(SDL https://github.com/libsdl-org/SDL.git release-3.2.10)
set_property(DIRECTORY "${CMAKE_BINARY_DIR}/_deps/sdl-src" PROPERTY EXCLUDE_FROM_ALL TRUE)

# Get SFML with Emscripten-specific settings
set(SFML_REPO_URL "https://github.com/zmertens/SFML.git")
set(SFML_GIT_TAG "almostv3")
message(STATUS "Fetching SFML from ${SFML_REPO_URL} at tag ${SFML_GIT_TAG}")
set(SFML_BUILD_AUDIO ON)
set(SFML_BUILD_NETWORK ON)
set(SFML_BUILD_GRAPHICS OFF)
set(SFML_BUILD_WINDOW OFF)
set(SFML_USE_SYSTEM_DEPS OFF)
# Disable SFML's internal thread finding since we provide our own
set(SFML_DISABLE_FIND_THREADS ON)
# Disable problematic audio dependencies for Web builds
set(SFML_USE_DRM OFF CACHE BOOL "Use DRM for audio")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries")
FetchGitRepo(SFML ${SFML_REPO_URL} ${SFML_GIT_TAG})

# Get GLM
set(GLM_DIR deps/glm-0.9.7)
message(STATUS "GLM included at ${GLM_DIR}")

# Get MazeBuilder
set(MAZE_BUILDER_REPO_URL "https://github.com/zmertens/MazeBuilder")
set(MAZE_BUILDER_GIT_TAG "v7.8.2")
message(STATUS "Fetching MazeBuilder from ${MAZE_BUILDER_REPO_URL} at tag ${MAZE_BUILDER_GIT_TAG}")
FetchGitRepo(MazeBuilder ${MAZE_BUILDER_REPO_URL} ${MAZE_BUILDER_GIT_TAG})

set(BREAKING_WALLS_SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/AudioHelper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ball.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Camera.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CommandQueue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Entity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Font.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GameState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GLUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HttpClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LoadingState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MenuState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Pathfinder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PauseState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PhysicsGame.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Player.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PostProcessing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PostProcessingManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RenderTexture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RenderWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SceneNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SDLHelper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SettingsState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SplashState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Sprite.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SpriteNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/State.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StateStack.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Transformable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Wall.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/WorkerConcurrent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/World.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/View.cpp)

set(DEAR_IMGUI_DEP_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../deps/dearimgui)

set(DEAR_IMGUI_SRC_FILES
    ${DEAR_IMGUI_DEP_PATH}/imgui.cpp
    ${DEAR_IMGUI_DEP_PATH}/imgui_demo.cpp
    ${DEAR_IMGUI_DEP_PATH}/imgui_draw.cpp
    ${DEAR_IMGUI_DEP_PATH}/imgui_tables.cpp
    ${DEAR_IMGUI_DEP_PATH}/imgui_widgets.cpp
    ${DEAR_IMGUI_DEP_PATH}/backends/imgui_impl_sdl3.cpp
    ${DEAR_IMGUI_DEP_PATH}/backends/imgui_impl_opengl3.cpp)

set(DEAR_IMGUI_HEADERS
    ${DEAR_IMGUI_DEP_PATH}/imgui.h
    ${DEAR_IMGUI_DEP_PATH}/imconfig.h
    ${DEAR_IMGUI_DEP_PATH}/imgui_internal.h
    ${DEAR_IMGUI_DEP_PATH}/imstb_rectpack.h
    ${DEAR_IMGUI_DEP_PATH}/imstb_textedit.h
    ${DEAR_IMGUI_DEP_PATH}/imstb_truetype.h
    ${DEAR_IMGUI_DEP_PATH}/backends/imgui_impl_sdl3.h
    ${DEAR_IMGUI_DEP_PATH}/backends/imgui_impl_opengl3.h
    ${DEAR_IMGUI_DEP_PATH}/backends/imgui_impl_opengl3_loader.h)

set(GLAD_SOURCES ${CMAKE_SOURCE_DIR}/deps/glad/src/glad_4_3.c)
set(NOISE_SOURCES ${CMAKE_SOURCE_DIR}/deps/noise/noise.c)

add_executable(${BREAKING_WALLS_APP_NAME} ${DEAR_IMGUI_SRC_FILES} ${GLAD_SOURCES} ${NOISE_SOURCES} ${BREAKING_WALLS_SRC_FILES})

target_compile_definitions(${BREAKING_WALLS_APP_NAME} PRIVATE "$<$<OR:$<STREQUAL:$<CONFIG>,Debug>,$<STREQUAL:$<CONFIG>,RelWithDebInfo>>:${BREAKING_WALLS_DEBUG_DEF}>")
target_compile_features(${BREAKING_WALLS_APP_NAME} PRIVATE cxx_std_20)
target_compile_definitions(${BREAKING_WALLS_APP_NAME} PRIVATE GLM_FORCE_RADIANS)

target_include_directories(${BREAKING_WALLS_APP_NAME} PRIVATE $<INSTALL_INTERFACE:deps/fonts> $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/deps/fonts>)
target_include_directories(${BREAKING_WALLS_APP_NAME} PRIVATE $<INSTALL_INTERFACE:deps/glad/include> $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/deps/glad/include>)
target_include_directories(${BREAKING_WALLS_APP_NAME} PRIVATE $<INSTALL_INTERFACE:deps/fonts> $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/deps/fonts>)
target_include_directories(${BREAKING_WALLS_APP_NAME} PRIVATE $<INSTALL_INTERFACE:deps/glm-0.9.7> $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/deps/glm-0.9.7>)
target_include_directories(${BREAKING_WALLS_APP_NAME} PRIVATE $<INSTALL_INTERFACE:deps/dearimgui> $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/deps>)
target_include_directories(${BREAKING_WALLS_APP_NAME} PRIVATE $<INSTALL_INTERFACE:deps/dearimgui> $<BUILD_INTERFACE:${DEAR_IMGUI_DEP_PATH}>)
target_include_directories(${BREAKING_WALLS_APP_NAME} PRIVATE $<INSTALL_INTERFACE:deps/stb> $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/deps/stb>)

target_link_libraries(${BREAKING_WALLS_APP_NAME} PRIVATE ${CMAKE_THREAD_LIBS_INIT} OpenGL::GL box2d::box2d MazeBuilder::MazeBuilder SDL3::SDL3 SFML::Audio SFML::Network)
message(INFO ": Configuring ${BREAKING_WALLS_APP_NAME} for Desktop platform")

file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../textures" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../deps/fonts" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/fonts")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../shaders" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../audio" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${CMAKE_SOURCE_DIR}/physics.json" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

# Copy shared/dlls on Windows only
if(WIN32)
    add_custom_command(
        TARGET ${BREAKING_WALLS_APP_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy $<TARGET_FILE:SDL3::SDL3-static> $<TARGET_FILE_DIR:${BREAKING_WALLS_APP_NAME}>
        VERBATIM
    )
    add_custom_command(
        TARGET ${BREAKING_WALLS_APP_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy $<TARGET_FILE:SFML::Audio> $<TARGET_FILE_DIR:${BREAKING_WALLS_APP_NAME}>
        VERBATIM
    )
    add_custom_command(
        TARGET ${BREAKING_WALLS_APP_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy $<TARGET_FILE:SFML::Network> $<TARGET_FILE_DIR:${BREAKING_WALLS_APP_NAME}>
        VERBATIM
    )

    # Copy shaders directory to output directory (next to executable)
    add_custom_command(
        TARGET ${BREAKING_WALLS_APP_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/shaders" $<TARGET_FILE_DIR:${BREAKING_WALLS_APP_NAME}>/shaders
        VERBATIM
    )

    add_custom_command(
        TARGET ${BREAKING_WALLS_APP_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/textures" $<TARGET_FILE_DIR:${BREAKING_WALLS_APP_NAME}>/textures
        VERBATIM
    )

    add_custom_command(
        TARGET ${BREAKING_WALLS_APP_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/audio" $<TARGET_FILE_DIR:${BREAKING_WALLS_APP_NAME}>/audio
        VERBATIM
    )

    add_custom_command(
        TARGET ${BREAKING_WALLS_APP_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/physics.json" $<TARGET_FILE_DIR:${BREAKING_WALLS_APP_NAME}>/physics.json
        VERBATIM
    )
endif()
