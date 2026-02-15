#ifndef LEVEL_HPP
#define LEVEL_HPP

#include <memory>
#include <string>
#include <vector>

namespace mazes
{
    class configurator;
}

class Level
{
public:
    /// @brief Loads a level using internal library
    /// @param configs Vector of configurator references
    /// @param appendResults Whether to append results to existing data
    /// @return True if the level was loaded successfully, false otherwise
    bool load(const std::vector<mazes::configurator> &configs, bool appendResults = false) noexcept;

private:
    std::string mData;
};

#endif // LEVEL_HPP
