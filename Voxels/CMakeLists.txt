set(GLAD_SOURCES ${CMAKE_SOURCE_DIR}/Extlibs/glad/src/glad_3_0.c)
set(NOISE_SOURCES ${CMAKE_SOURCE_DIR}/Extlibs/noise/noise.c)
set(SQLITE3_SOURCES ${CMAKE_SOURCE_DIR}/Extlibs/sqlite/sqlite3.c)
set(DEAR_IMGUI_DEP_PATH ${CMAKE_SOURCE_DIR}/Extlibs/dearimgui)

set(DEAR_IMGUI_SRC_FILES
    ${DEAR_IMGUI_DEP_PATH}/imgui.cpp
    ${DEAR_IMGUI_DEP_PATH}/imgui_demo.cpp
    ${DEAR_IMGUI_DEP_PATH}/imgui_draw.cpp
    ${DEAR_IMGUI_DEP_PATH}/imgui_tables.cpp
    ${DEAR_IMGUI_DEP_PATH}/imgui_widgets.cpp
    ${DEAR_IMGUI_DEP_PATH}/backends/imgui_impl_sdl3.cpp
    ${DEAR_IMGUI_DEP_PATH}/backends/imgui_impl_opengl3.cpp
    ${DEAR_IMGUI_DEP_PATH}/backends/imgui_impl_sdlrenderer3.cpp)

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

set(CRAFT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/bloom_pass.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/craft.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/db.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/font.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/geometries.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/item.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/matrix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/player.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sdl_gl_helper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/world.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/voxels_map.cpp)

add_executable(${BREAKING_WALLS_APP_NAME} ${GLAD_SOURCES} ${NOISE_SOURCES} ${SQLITE3_SOURCES} ${DEAR_IMGUI_SRC_FILES} ${CRAFT_SOURCES})

target_compile_definitions(${BREAKING_WALLS_APP_NAME} PRIVATE "$<$<OR:$<STREQUAL:$<CONFIG>,Debug>,$<STREQUAL:$<CONFIG>,RelWithDebInfo>>:${BREAKING_WALLS_DEBUG_DEF}>")

target_compile_features(${BREAKING_WALLS_APP_NAME} PRIVATE cxx_std_20)

target_include_directories(${BREAKING_WALLS_APP_NAME} PRIVATE $<INSTALL_INTERFACE:Extlibs/glad/include> $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Extlibs/glad/include>)
target_include_directories(${BREAKING_WALLS_APP_NAME} PRIVATE $<INSTALL_INTERFACE:Extlibs/dearimgui> $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Extlibs/dearimgui>)
target_include_directories(${BREAKING_WALLS_APP_NAME} PRIVATE $<INSTALL_INTERFACE:Extlibs> $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Extlibs>)

include(FetchContent)

# Get SDL
set(SDL_SHARED FALSE CACHE BOOL "Build a SDL shared library (if available)")
set(SDL_STATIC TRUE CACHE BOOL "Build a SDL static library (if available)")
FetchContent_Declare(SDL GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-3.4.14)
FetchContent_MakeAvailable(SDL)
set_property(DIRECTORY "${CMAKE_BINARY_DIR}/_deps/sdl-src" PROPERTY EXCLUDE_FROM_ALL TRUE)

# Get MazeBuilder
FetchContent_Declare(MazeBuilder GIT_REPOSITORY https://github.com/zmertens/MazeBuilder.git GIT_TAG v8.6.2)
FetchContent_MakeAvailable(MazeBuilder)

target_link_libraries(${BREAKING_WALLS_APP_NAME} PRIVATE MazeBuilder::MazeBuilder SDL3::SDL3)

if(EMSCRIPTEN)
    message(INFO " Building ${BREAKING_WALLS_APP_NAME} for the Web!")

    set(EMSCRIPTEN_DEP_PATH ${CMAKE_SOURCE_DIR}/Extlibs/emscripten_local)
    target_include_directories(${BREAKING_WALLS_APP_NAME} PRIVATE $<INSTALL_INTERFACE:${EMSCRIPTEN_DEP_PATH}> $<BUILD_INTERFACE:${EMSCRIPTEN_DEP_PATH}>)

    target_link_options(${BREAKING_WALLS_APP_NAME} PRIVATE
            "SHELL:-pthread -s PTHREAD_POOL_SIZE=12"
            "SHELL:-s STACK_SIZE=5242880 -s DEFAULT_PTHREAD_STACK_SIZE=2097152"
            "SHELL:-s INITIAL_MEMORY=268435456 -s ALLOW_MEMORY_GROWTH=1 -s MAXIMUM_MEMORY=1073741824"
            "SHELL:-s FULL_ES3=1 -s MAX_WEBGL_VERSION=2 -s MODULARIZE=1 -s EXPORT_ES6=1 -s WASM=1"
            "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/Extlibs/emscripten_local/shell_minimal.html"
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/Shaders@/Shaders"
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/Textures@/Textures")
    target_link_options(${BREAKING_WALLS_APP_NAME} PRIVATE "$<$<CONFIG:Debug>:SHELL:-s STACK_OVERFLOW_CHECK=2 -s ASSERTIONS=2 -gsource-map --profile>")
    target_link_options(${BREAKING_WALLS_APP_NAME} PRIVATE "$<$<CONFIG:Release>:SHELL:-O3>")

    set_target_properties(${BREAKING_WALLS_APP_NAME} PROPERTIES SUFFIX ".html" LINK_FLAGS "-lembind")

    add_custom_command(TARGET ${BREAKING_WALLS_APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/Textures/favicon.ico"
            "$<TARGET_FILE_DIR:${BREAKING_WALLS_APP_NAME}>/favicon.ico"
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
