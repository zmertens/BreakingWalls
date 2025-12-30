#ifndef MAZE_LAYOUT_HPP
#define MAZE_LAYOUT_HPP

#include <cstdint>
#include <string_view>
#include <vector>

#include <MazeBuilder/enums.h>
#include <MazeBuilder/grid_interface.h>

struct SDL_Surface;

class MazeLayout
{
public:
    enum class CellType : std::uint8_t
    {
        Empty,
        Wall,
        Other
    };

    enum class BarrierType : std::uint8_t
    {
        None,
        Corner,
        Horizontal,
        Vertical
    };

    struct Cell
    {
        CellType type{CellType::Empty};
        BarrierType barrier{BarrierType::None};
        std::uint8_t r{255};
        std::uint8_t g{255};
        std::uint8_t b{255};
        std::uint8_t a{255};
    };

    MazeLayout() = default;

    static MazeLayout fromString(std::string_view mazeStr, int cellSize);
    
    // Create maze layout from MazeBuilder grid interface with pixel data
    static MazeLayout fromGrid(mazes::grid_interface* grid);

    [[nodiscard]] int getRows() const noexcept { return mRows; }
    [[nodiscard]] int getColumns() const noexcept { return mColumns; }
    [[nodiscard]] int getCellSize() const noexcept { return mCellSize; }

    [[nodiscard]] int getPixelWidth() const noexcept { return mColumns * mCellSize; }
    [[nodiscard]] int getPixelHeight() const noexcept { return mRows * mCellSize; }

    [[nodiscard]] const Cell& at(const int row, const int col) const noexcept
    {
        return mCells[static_cast<std::size_t>(row) * mColumns + col];
    }
    
    // Get all wall cell positions (in pixels)
    [[nodiscard]] std::vector<std::pair<int, int>> getWallPositions() const noexcept;

    [[nodiscard]] SDL_Surface* buildSurface() const noexcept;

private:
    int mRows{0};
    int mColumns{0};
    int mCellSize{0};
    std::vector<Cell> mCells;
    
    // Direct pixel data from MazeBuilder (RGBA format)
    std::vector<std::uint8_t> mPixelData;
    int mPixelWidth{0};
    int mPixelHeight{0};
};

#endif // MAZE_LAYOUT_HPP
