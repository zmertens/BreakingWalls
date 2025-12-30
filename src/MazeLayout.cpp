#include "MazeLayout.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <SDL3/SDL.h>
#include <MazeBuilder/pixels.h>
#include <MazeBuilder/randomizer.h>
#include <MazeBuilder/grid_operations.h>

MazeLayout MazeLayout::fromString(std::string_view mazeStr, int cellSize)
{
    MazeLayout layout;

    if (mazeStr.empty() || cellSize <= 0)
    {
        return layout;
    }

    int currentLineWidth = 0;
    int rows = 0;
    int maxWidth = 0;

    for (char c : mazeStr)
    {
        if (c == '\n')
        {
            ++rows;
            if (currentLineWidth > maxWidth)
            {
                maxWidth = currentLineWidth;
            }
            currentLineWidth = 0;
        }
        else
        {
            ++currentLineWidth;
        }
    }

    if (currentLineWidth > 0)
    {
        ++rows;
        if (currentLineWidth > maxWidth)
        {
            maxWidth = currentLineWidth;
        }
    }

    if (rows == 0 || maxWidth == 0)
    {
        return layout;
    }

    layout.mRows = rows;
    layout.mColumns = maxWidth;
    layout.mCellSize = cellSize;
    layout.mCells.resize(static_cast<std::size_t>(rows * maxWidth));

    int row = 0;
    int col = 0;

    for (char c : mazeStr)
    {
        if (c == '\n')
        {
            ++row;
            col = 0;
            continue;
        }

        if (row >= rows || col >= maxWidth)
        {
            ++col;
            continue;
        }

        std::size_t index = static_cast<std::size_t>(row) * maxWidth + col;
        Cell& cell = layout.mCells[index];

        // Default to transparent for empty cells
        cell.r = 0;
        cell.g = 0;
        cell.b = 0;
        cell.a = 0;

        if (c == static_cast<char>(mazes::barriers::CORNER))
        {
            cell.type = CellType::Wall;
            cell.barrier = BarrierType::Corner;
            cell.r = cell.g = cell.b = 0;
            cell.a = 255; // Opaque black for walls
        }
        else if (c == static_cast<char>(mazes::barriers::HORIZONTAL))
        {
            cell.type = CellType::Wall;
            cell.barrier = BarrierType::Horizontal;
            cell.r = cell.g = cell.b = 0;
            cell.a = 255; // Opaque black for walls
        }
        else if (c == static_cast<char>(mazes::barriers::VERTICAL))
        {
            cell.type = CellType::Wall;
            cell.barrier = BarrierType::Vertical;
            cell.r = cell.g = cell.b = 0;
            cell.a = 255; // Opaque black for walls
        }
        else if (c == ' ')
        {
            cell.type = CellType::Empty;
            cell.barrier = BarrierType::None;
            // Already transparent from default
        }
        else
        {
            cell.type = CellType::Other;
            cell.barrier = BarrierType::None;
            // Keep transparent for other/unknown cell types
        }

        ++col;
    }

    return layout;
}

MazeLayout MazeLayout::fromGrid(mazes::grid_interface* grid)
{
    MazeLayout layout;
    
    if (!grid)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Grid interface is null");
        return layout;
    }
    
    auto& gridOps = grid->operations();
    
    // Use MazeBuilder's pixels algorithm to generate pixel data
    mazes::randomizer rng;
    mazes::pixels pixelGenerator;
    
    if (!pixelGenerator.run(grid, rng))
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to generate pixel data from grid");
        return layout;
    }
    
    // Get the generated pixel data (RGBA format)
    layout.mPixelData = gridOps.get_pixels();
    
    if (layout.mPixelData.empty())
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Pixel data is empty after generation");
        return layout;
    }
    
    // Get grid dimensions to calculate pixel dimensions
    auto [rows, columns, cellSize] = gridOps.get_dimensions();
    
    // Calculate scale (same logic as in pixels.h)
    constexpr unsigned int MIN_SCALE = 1;
    constexpr unsigned int MAX_SCALE = 10;
    auto calculated_scale = static_cast<unsigned int>(std::sqrt(static_cast<double>(rows * columns)));
    auto scale = std::clamp(calculated_scale, MIN_SCALE, MAX_SCALE);
    
    // Get ASCII dimensions from stringified maze
    std::string mazeStr = gridOps.get_str();
    size_t ascii_width = 0;
    size_t ascii_height = 0;
    size_t current_line_width = 0;
    
    for (char c : mazeStr)
    {
        if (c == '\n')
        {
            ascii_height++;
            ascii_width = std::max(ascii_width, current_line_width);
            current_line_width = 0;
        }
        else
        {
            current_line_width++;
        }
    }
    
    if (current_line_width > 0)
    {
        ascii_height++;
        ascii_width = std::max(ascii_width, current_line_width);
    }
    
    layout.mPixelWidth = static_cast<int>(ascii_width * scale);
    layout.mPixelHeight = static_cast<int>(ascii_height * scale);
    layout.mRows = static_cast<int>(ascii_height);
    layout.mColumns = static_cast<int>(ascii_width);
    layout.mCellSize = static_cast<int>(scale);
    
    SDL_Log("Generated maze: %dx%d cells, %dx%d pixels, scale=%d",
            layout.mRows, layout.mColumns, layout.mPixelWidth, layout.mPixelHeight, layout.mCellSize);
    
    return layout;
}

std::vector<std::pair<int, int>> MazeLayout::getWallPositions() const noexcept
{
    std::vector<std::pair<int, int>> wallPositions;
    
    for (int row = 0; row < mRows; ++row)
    {
        for (int col = 0; col < mColumns; ++col)
        {
            const Cell& cell = at(row, col);
            if (cell.type == CellType::Wall)
            {
                // Return center position in pixels
                int centerX = col * mCellSize + mCellSize / 2;
                int centerY = row * mCellSize + mCellSize / 2;
                wallPositions.emplace_back(centerX, centerY);
            }
        }
    }
    
    return wallPositions;
}

SDL_Surface* MazeLayout::buildSurface() const noexcept
{
    // If we have direct pixel data from MazeBuilder, use it
    if (!mPixelData.empty() && mPixelWidth > 0 && mPixelHeight > 0)
    {
        SDL_Surface* surface = SDL_CreateSurface(mPixelWidth, mPixelHeight, SDL_PIXELFORMAT_RGBA8888);
        
        if (!surface)
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create maze surface from pixel data: %s", SDL_GetError());
            return nullptr;
        }
        
        // Copy pixel data to surface
        if (SDL_LockSurface(surface) == 0)
        {
            const size_t expectedSize = static_cast<size_t>(mPixelWidth) * mPixelHeight * 4; // RGBA = 4 bytes
            const size_t copySize = std::min(expectedSize, mPixelData.size());
            
            std::memcpy(surface->pixels, mPixelData.data(), copySize);
            
            SDL_UnlockSurface(surface);
            
            SDL_Log("Created surface from pixel data: %dx%d (%zu bytes)", 
                    mPixelWidth, mPixelHeight, copySize);
            
            return surface;
        }
        else
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to lock surface: %s", SDL_GetError());
            SDL_DestroySurface(surface);
            return nullptr;
        }
    }
    
    // Fallback: use old cell-based rendering
    if (mRows <= 0 || mColumns <= 0 || mCellSize <= 0)
    {
        return nullptr;
    }

    const int width = getPixelWidth();
    const int height = getPixelHeight();

    SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA8888);

    if (!surface)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create maze surface: %s", SDL_GetError());
        return nullptr;
    }

    // Fill with transparent background to enable blending with parallax layers
    SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGBA(surface, 0, 0, 0, 0));

    for (int row = 0; row < mRows; ++row)
    {
        for (int col = 0; col < mColumns; ++col)
        {
            const Cell& cell = at(row, col);

            SDL_Rect rect{col * mCellSize, row * mCellSize, mCellSize, mCellSize};

            SDL_FillSurfaceRect(surface, &rect, SDL_MapSurfaceRGBA(surface, cell.r, cell.g, cell.b, cell.a));
        }
    }

    return surface;
}
