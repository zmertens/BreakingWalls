set(GLAD_SOURCES ${CMAKE_SOURCE_DIR}/Extlibs/glad_3_0/src/gl.c)
set(NOISE_SOURCES ${CMAKE_SOURCE_DIR}/Extlibs/noise/noise.c)
set(SQLITE3_SOURCES ${CMAKE_SOURCE_DIR}/Extlibs/sqlite/sqlite3.c)
set(DEAR_IMGUI_DEP_PATH ${CMAKE_SOURCE_DIR}/Extlibs/dearimgui)

set(DEAR_IMGUI_SRC_FILES
    ${DEAR_IMGUI_DEP_PATH}/imgui.cpp
    ${DEAR_IMGUI_DEP_PATH}/imgui_draw.cpp
    ${DEAR_IMGUI_DEP_PATH}/imgui_tables.cpp
    ${DEAR_IMGUI_DEP_PATH}/imgui_widgets.cpp
    ${DEAR_IMGUI_DEP_PATH}/backends/imgui_impl_sdl3.cpp
    ${DEAR_IMGUI_DEP_PATH}/backends/imgui_impl_opengl3.cpp)

set(CRAFT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/artifact_exporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bloom_pass.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/chunk.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/chunk_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/craft.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/craft_map.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/db.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/db_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_world.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_world_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/font.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/game_environment.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/geometries.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/heightmap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/item.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/matrix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/player.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sdl_gl_helper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/state_management.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/soundfx.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/world.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/world_blocks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/world_workers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/world_renderer.cpp)

add_executable(${BREAKING_WALLS_APP_NAME} ${GLAD_SOURCES} ${NOISE_SOURCES} ${SQLITE3_SOURCES} ${DEAR_IMGUI_SRC_FILES} ${CRAFT_SOURCES})

target_compile_definitions(${BREAKING_WALLS_APP_NAME} PRIVATE "$<$<OR:$<STREQUAL:$<CONFIG>,Debug>,$<STREQUAL:$<CONFIG>,RelWithDebInfo>>:BREAKING_WALLS_DEBUG_DEF>")

target_compile_features(${BREAKING_WALLS_APP_NAME} PRIVATE cxx_std_20)

target_include_directories(${BREAKING_WALLS_APP_NAME} PRIVATE $<INSTALL_INTERFACE:Extlibs/glad_3_0/include> $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Extlibs/glad_3_0/include>)
target_include_directories(${BREAKING_WALLS_APP_NAME} PRIVATE $<INSTALL_INTERFACE:Extlibs/bvhv2> $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Extlibs/bvhv2>)
target_include_directories(${BREAKING_WALLS_APP_NAME} PRIVATE $<INSTALL_INTERFACE:Extlibs/dearimgui> $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Extlibs/dearimgui>)
target_include_directories(${BREAKING_WALLS_APP_NAME} PRIVATE $<INSTALL_INTERFACE:Extlibs> $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Extlibs>)

include(FetchContent)

# Get SDL
set(SDL_SHARED FALSE CACHE BOOL "Build a SDL shared library (if available)")
set(SDL_STATIC TRUE CACHE BOOL "Build a SDL static library (if available)")
FetchContent_Declare(SDL GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-3.4.18)
FetchContent_MakeAvailable(SDL)
set_property(DIRECTORY "${CMAKE_BINARY_DIR}/_deps/sdl-src" PROPERTY EXCLUDE_FROM_ALL TRUE)

# Get SFML
set(GET_SFML_REPO_URL https://github.com/zmertens/SFML.git)
set(GET_SFML_VERSION almostv3)
set(SFML_BUILD_AUDIO ON)
set(SFML_BUILD_NETWORK OFF)
set(SFML_BUILD_GRAPHICS OFF)
set(SFML_BUILD_WINDOW OFF)
set(SFML_USE_SYSTEM_DEPS OFF)
# Disable SFML's internal thread finding since we provide our own
set(SFML_DISABLE_FIND_THREADS ON)
# Disable problematic audio dependencies for builds
set(SFML_USE_DRM OFF CACHE BOOL "Use DRM for audio")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries")
FetchContent_Declare(SFML GIT_REPOSITORY ${GET_SFML_REPO_URL} GIT_TAG ${GET_SFML_VERSION})
FetchContent_MakeAvailable(SFML)

# Get MazeBuilder
set(MAZE_BUILDER_BUILD_SHARED OFF CACHE BOOL "Build MazeBuilder as a shared library")
FetchContent_Declare(MazeBuilder GIT_REPOSITORY https://github.com/zmertens/MazeBuilder.git GIT_TAG v8.8.13)
FetchContent_MakeAvailable(MazeBuilder)

# Get box3d
set(BOX3D_REPO_URL https://github.com/erincatto/box3d.git)
set(BOX3D_VERSION v0.1.0)
FetchContent_Declare(box3d GIT_REPOSITORY ${BOX3D_REPO_URL} GIT_TAG ${BOX3D_VERSION})
FetchContent_MakeAvailable(box3d)
set_property(TARGET box3d PROPERTY COMPILE_WARNING_AS_ERROR OFF)
# Unoptimized Box3D is ~7x slower per step; keep it optimized even in Debug.
if(NOT MSVC)
    target_compile_options(box3d PRIVATE "$<$<CONFIG:Debug>:-O2>")
endif()

target_link_libraries(${BREAKING_WALLS_APP_NAME} PRIVATE MazeBuilder::MazeBuilder SDL3::SDL3 box3d::box3d SFML::Audio)

if(EMSCRIPTEN)
    message(INFO ": Building ${BREAKING_WALLS_APP_NAME} for the Web!")

    set(EMSCRIPTEN_DEP_PATH ${CMAKE_SOURCE_DIR}/Extlibs/emscripten_local)
    target_include_directories(${BREAKING_WALLS_APP_NAME} PRIVATE $<INSTALL_INTERFACE:${EMSCRIPTEN_DEP_PATH}> $<BUILD_INTERFACE:${EMSCRIPTEN_DEP_PATH}>)

    target_link_options(${BREAKING_WALLS_APP_NAME} PRIVATE
            "SHELL:-pthread -s PTHREAD_POOL_SIZE=20 -s PROXY_TO_PTHREAD=1 -s OFFSCREENCANVAS_SUPPORT=1"
            "SHELL:-s STACK_SIZE=5242880 -s DEFAULT_PTHREAD_STACK_SIZE=2097152"
            "SHELL:-s INITIAL_MEMORY=268435456 -s ALLOW_MEMORY_GROWTH=1 -s MAXIMUM_MEMORY=1073741824"
            "SHELL:-s FULL_ES3=1 -s MAX_WEBGL_VERSION=2 -s MODULARIZE=1 -s EXPORT_ES6=1 -s WASM=1"
            "SHELL:-matomics -mbulk-memory -s ENVIRONMENT=web,worker"
            "SHELL:-s EXPORTED_RUNTIME_METHODS=HEAPF32,HEAPU8,HEAP32"
            "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/Extlibs/emscripten_local/shell_minimal.html"
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/Shaders@/Shaders"
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/SoundFX@/SoundFX"
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/Textures@/Textures")
    target_link_options(${BREAKING_WALLS_APP_NAME} PRIVATE "$<$<CONFIG:Debug>:SHELL:-s STACK_OVERFLOW_CHECK=2 -s ASSERTIONS=2 -gsource-map --profile>")
    target_link_options(${BREAKING_WALLS_APP_NAME} PRIVATE "$<$<CONFIG:Release>:SHELL:-O3>")

    set_target_properties(${BREAKING_WALLS_APP_NAME} PROPERTIES SUFFIX ".html" LINK_FLAGS "-lembind")

    add_custom_command(TARGET ${BREAKING_WALLS_APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/Textures/favicon.ico"
            "$<TARGET_FILE_DIR:${BREAKING_WALLS_APP_NAME}>/favicon.ico"
        VERBATIM)


    add_custom_command(TARGET ${BREAKING_WALLS_APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/secure_http_server.py"
            "$<TARGET_FILE_DIR:${BREAKING_WALLS_APP_NAME}>/secure_http_server.py"
        VERBATIM)
else()

    message(INFO ": Building ${BREAKING_WALLS_APP_NAME} for the Desktop!")

    add_custom_command(TARGET ${BREAKING_WALLS_APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Shaders"
            "$<TARGET_FILE_DIR:${BREAKING_WALLS_APP_NAME}>/Shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Textures"
            "$<TARGET_FILE_DIR:${BREAKING_WALLS_APP_NAME}>/Textures"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/SoundFX"
            "$<TARGET_FILE_DIR:${BREAKING_WALLS_APP_NAME}>/SoundFX"
        VERBATIM)

    # Copy shared/dlls on Windows only
    if (WIN32)
        # If the SDL3 imported target exists, copy its DLL into the target directory.
        if (TARGET SDL3::SDL3-shared)
            add_custom_command(TARGET ${BREAKING_WALLS_APP_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${BREAKING_WALLS_APP_NAME}>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:SDL3::SDL3-shared>"
                    "$<TARGET_FILE_DIR:${BREAKING_WALLS_APP_NAME}>/$<TARGET_FILE_NAME:SDL3::SDL3-shared>"
                VERBATIM)
        endif()

    endif()

endif()
