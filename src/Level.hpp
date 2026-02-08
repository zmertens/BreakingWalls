#ifndef LEVEL_HPP
#define LEVEL_HPP

#include <memory>
#include <string>

namespace mazes
{
    class configurator;
}

class Level
{
public:

    /// @brief Loads a level using internal library
    /// @param config 
    /// @return 
    bool load(const mazes::configurator& config) noexcept;

    /// @brief Loads a level using a network library
    /// @param config 
    /// @return 
    bool loadFromNetwork(const mazes::configurator& config) noexcept;

private:
    std::string mData;
};

#endif // LEVEL_HPP
