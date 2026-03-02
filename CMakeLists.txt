cmake_minimum_required(VERSION 3.5...4.1)

set(BREAKING_WALLS_PROJECT_VERSION 0.5.4)

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
############################################################
# Install rules for packaging with CPack
include(GNUInstallDirs)

# Install main executable
install(TARGETS breakingwalls
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install DLLs (add all required DLLs here)
install(FILES
    $<TARGET_FILE:SDL3::SDL3-static>
    $<TARGET_FILE:SFML::Audio>
    $<TARGET_FILE:SFML::Network>
    $<TARGET_FILE:assimp::assimp>
    $<TARGET_FILE:box2d::box2d>
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES "${CMAKE_SOURCE_DIR}/deps/run-breakingwalls.bat" DESTINATION "${CMAKE_INSTALL_BINDIR}")   

# Install assets
install(DIRECTORY audio/ DESTINATION ${CMAKE_INSTALL_BINDIR}/audio)
install(DIRECTORY models/ DESTINATION ${CMAKE_INSTALL_BINDIR}/models)
install(DIRECTORY shaders/ DESTINATION ${CMAKE_INSTALL_BINDIR}/shaders)
install(DIRECTORY textures/ DESTINATION ${CMAKE_INSTALL_BINDIR}/textures)
install(FILES physics.json DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

# Enable CPack
include(InstallRequiredSystemLibraries)
include(CPack)
set(CPACK_PACKAGE_NAME "BreakingWalls")
set(CPACK_PACKAGE_VERSION ${BREAKING_WALLS_PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "Your Name <your@email.com>")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
