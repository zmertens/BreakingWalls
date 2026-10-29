add_library(bvh INTERFACE)
find_package(Threads)
if (Threads_FOUND)
    # Link with the threading library of the system, which may
    # be required by standard header <thread> on some systems
    target_link_libraries(bvh INTERFACE Threads::Threads)
endif()

target_include_directories(bvh INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Extlibs/bvhv2>
    $<INSTALL_INTERFACE:include>)

set_target_properties(bvh PROPERTIES CXX_STANDARD 20)

target_compile_features(bvh INTERFACE cxx_std_20)
