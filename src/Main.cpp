// Basic application of Maze Builder as a level generator in a game setting
// Includes most game engine features like graphics and window management,
// input handling, state management, and resource loading, audio, and network
// Player verses computer AI gameplay with physics simulation
// Scoring system based on survivability (time) and efficiency (resources)

#include <iostream>
#include <exception>
#include <string>

#include "PhysicsGame.hpp"

#include <MazeBuilder/maze_builder.h>

const auto WINDOW_TITLE{"Breaking Walls " + mazes::VERSION};

static constexpr auto WINDOW_WIDTH = 1280;
static constexpr auto WINDOW_HEIGHT = 720;

#if defined(__EMSCRIPTEN__)
#include <emscripten/bind.h>

std::shared_ptr<PhysicsGame> get()
{
    return mazes::singleton_base<PhysicsGame>::instance(WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT);
}

// bind a getter method from C++ so that it can be accessed in the frontend with JS
EMSCRIPTEN_BINDINGS (maze_builder_module)
{
    emscripten::function("get", &get, emscripten::allow_raw_pointers());
    emscripten::class_<PhysicsGame>("PhysicsGame")
        .smart_ptr<std::shared_ptr<PhysicsGame>>("std::shared_ptr<PhysicsGame>")
        .constructor<const std::string&, int, int>();
}
#endif

int main(int argc, char* argv[])
{
    std::string configPath{};

#if !defined(__EMSCRIPTEN__)

    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <path_to_config.json>" << std::endl;

        return EXIT_FAILURE;
    }

    if (!mazes::string_utils::contains(argv[1], ".json"))
    {
        std::cerr << "Error: Configuration file must be a .json file" << std::endl;

        return EXIT_FAILURE;
    }

    configPath = argv[1];
#else

    configPath = "resources/physics.json";
#endif

    try
    {
        const auto inst = mazes::singleton_base<PhysicsGame>::instance(WINDOW_TITLE, configPath, WINDOW_WIDTH, WINDOW_HEIGHT);

        if (mazes::randomizer rng; !inst->run(nullptr, std::ref(rng)))
        {
            throw std::runtime_error("Error: " + WINDOW_TITLE + " encountered an error during execution");
        }

#if defined(MAZE_DEBUG)

        std::cout << WINDOW_TITLE << " ran successfully (DEBUG MODE)" << std::endl;
#endif
    }
    catch (std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
    }

    return EXIT_SUCCESS;
}
