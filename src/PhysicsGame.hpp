#ifndef PHYSICS_GAME_HPP
#define PHYSICS_GAME_HPP

#include <memory>
#include <string_view>

#include <MazeBuilder/algo_interface.h>
#include <MazeBuilder/singleton_base.h>

namespace mazes
{
    class grid_interface;
    class randomizer;
}

class PhysicsGame : public mazes::algo_interface, public mazes::singleton_base<PhysicsGame>
{
    friend class singleton_base;

public:
    PhysicsGame(std::string_view title, int w, int h, std::string_view resourcePath = "");

    ~PhysicsGame() override;

    bool run(mazes::grid_interface* g, mazes::randomizer& rng) const noexcept override;

private:
    struct PhysicsGameImpl;
    std::unique_ptr<PhysicsGameImpl> mImpl;
};

#endif // PHYSICS_GAME_HPP
