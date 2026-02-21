// Basic application of Maze Builder as a level generator in a game setting
// Includes most game engine features like graphics and window management,
// input handling, state management, and resource loading, audio, and network
// Player verses computer AI gameplay with physics simulation
// Scoring system based on survivability (time) and efficiency (resources)

#include <exception>
#include <iostream>
#include <string>

#include "buildinfo.h"
#include "PhysicsGame.hpp"

#include <MazeBuilder/maze_builder.h>

const auto WINDOW_TITLE{"Breaking Walls " + std::string{bw::buildinfo::Version}};

static constexpr auto WINDOW_WIDTH = 1280;
static constexpr auto WINDOW_HEIGHT = 720;

int main(int argc, char *argv[])
{
    std::string configPath{};

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

    try
    {
        auto inst = mazes::singleton_base<PhysicsGame>::instance(WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT, configPath);

        if (mazes::randomizer rng; !inst->run(nullptr, std::ref(rng)))
        {
            throw std::runtime_error("Error: " + std::string(argv[0]) + " encountered an error during execution");
        }
    }
    catch (std::exception &ex)
    {
        std::cerr << ex.what() << std::endl;
    }

    return EXIT_SUCCESS;
}
