/// @file Main.cpp
/// @brief AmazingSFML - 2D maze generation and visualization example
/// @details Generates a 2D maze using the MazeBuilder library and visualizes it
///          using the SFML Graphics module.  Walls are drawn as the window background
///          colour and passages are carved out as lighter filled rectangles.
///
/// Controls:
///   Escape / close window  → exit

#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <string>

#include <SFML/Graphics.hpp>

#include <MazeBuilder/colored_grid.h>
#include <MazeBuilder/cell.h>
#include <MazeBuilder/dfs.h>
#include <MazeBuilder/grid_operations.h>
#include <MazeBuilder/randomizer.h>

#include "buildinfo.h"

namespace
{

constexpr unsigned int kMazeRows    = 25u;
constexpr unsigned int kMazeCols    = 25u;
constexpr float        kCellSize    = 22.0f;
constexpr float        kWallSize    = 2.0f;
constexpr float        kStride      = kCellSize + kWallSize;

/// Passage (carved-out) colour – a light warm grey
const sf::Color kPassageColor{220, 215, 210};

/// Wall / background colour – a dark charcoal
const sf::Color kWallColor{30, 30, 35};

/// Compute the required window size for the given maze dimensions.
sf::Vector2u windowSize()
{
    const auto w = static_cast<unsigned int>(kWallSize + kMazeCols * kStride);
    const auto h = static_cast<unsigned int>(kWallSize + kMazeRows * kStride);
    return {w, h};
}

} // anonymous namespace

int main()
{
    // ── Generate maze ───────────────────────────────────────────────
    auto grid = std::make_unique<mazes::colored_grid>(kMazeRows, kMazeCols, 1u);
    mazes::randomizer rng{};
    rng.seed(std::random_device{}());
    mazes::dfs algo{};
    algo.run(grid.get(), rng);

    auto&& ops = grid->operations();

    // ── Pre-build passage geometry ──────────────────────────────────
    // Each cell gets one rectangle; linked neighbours get a small
    // connecting rectangle to remove the wall between them.
    std::vector<sf::RectangleShape> passages;
    passages.reserve(static_cast<std::size_t>(ops.num_cells()) * 3u);

    for (unsigned int row = 0u; row < kMazeRows; ++row)
    {
        for (unsigned int col = 0u; col < kMazeCols; ++col)
        {
            const int idx = static_cast<int>(row * kMazeCols + col);
            const auto cell = ops.search(idx);
            if (!cell)
                continue;

            const float x = kWallSize + col * kStride;
            const float y = kWallSize + row * kStride;

            // Cell body
            sf::RectangleShape rect({kCellSize, kCellSize});
            rect.setPosition({x, y});
            rect.setFillColor(kPassageColor);
            passages.push_back(rect);

            // Passage east  → fill the wall gap to the right
            if (const auto east = ops.get_east(cell);
                east && cell->is_linked(east))
            {
                sf::RectangleShape bridge({kWallSize, kCellSize});
                bridge.setPosition({x + kCellSize, y});
                bridge.setFillColor(kPassageColor);
                passages.push_back(bridge);
            }

            // Passage south → fill the wall gap below
            if (const auto south = ops.get_south(cell);
                south && cell->is_linked(south))
            {
                sf::RectangleShape bridge({kCellSize, kWallSize});
                bridge.setPosition({x, y + kCellSize});
                bridge.setFillColor(kPassageColor);
                passages.push_back(bridge);
            }
        }
    }

    // ── Create window ───────────────────────────────────────────────
    const auto winTitle = "AmazingSFML – Breaking Walls " +
                          std::string{bw::buildinfo::Version};

    sf::RenderWindow window(
        sf::VideoMode(windowSize()),
        winTitle,
        sf::Style::Titlebar | sf::Style::Close);

    window.setVerticalSyncEnabled(true);

    // ── Main loop ───────────────────────────────────────────────────
    while (window.isOpen())
    {
        // Event handling
        while (const auto event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }
            else if (const auto* key = event->getIf<sf::Event::KeyPressed>())
            {
                if (key->code == sf::Keyboard::Key::Escape)
                    window.close();
            }
        }

        // Draw
        window.clear(kWallColor);
        for (const auto& shape : passages)
            window.draw(shape);
        window.display();
    }

    return EXIT_SUCCESS;
}
