#include "Level.hpp"

#include <MazeBuilder/create.h>

#include <SFML/Network.hpp>

/// @brief Loads a level using internal library
/// @param config 
/// @return 
bool Level::load(const mazes::configurator& config) noexcept
{
    mData = mazes::create(config);

    return mData.empty() ? false : true;
}

/// @brief Loads a level using a network library
/// @param config 
/// @return 
bool Level::loadFromNetwork(const mazes::configurator& config) noexcept
{
    return false;
}
