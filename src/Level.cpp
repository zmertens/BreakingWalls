#include "Level.hpp"

#include <MazeBuilder/configurator.h>
#include <MazeBuilder/create.h>
#include <MazeBuilder/create2.h>

/// @brief Loads a level using internal library
/// @param configs Vector of configurator references
/// @param appendResults Whether to append results to existing data
/// @return True if the level was loaded successfully, false otherwise
bool Level::load(const std::vector<mazes::configurator> &configs, bool appendResults) noexcept
{
    if (appendResults)
    {
        std::string newData = mazes::create2(configs);
        if (newData.empty())
        {
            return false;
        }

        if (!mData.empty())
        {
            mData += "\n\n";
        }
        mData += newData;
    }
    else
    {
        mData = mazes::create2(configs);
    }
    return mData.empty() ? false : true;
}
