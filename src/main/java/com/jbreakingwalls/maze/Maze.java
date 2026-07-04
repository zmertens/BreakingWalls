package com.jbreakingwalls.maze;

import java.util.List;

/**
 * Immutable structural description of one maze level.
 *
 * The grid cells themselves ({@link MazeCell#state}) are mutable so the
 * player's reveals are tracked in-place. The structure of the maze
 * (which cells exist, which passages are open, what the shortest path is)
 * never changes after generation.
 *
 * record: compact value type; List fields are wrapped with List.copyOf by the
 * generator so the path is an immutable, GC-friendly snapshot.
 */
// record provides auto-generated equals/hashCode/toString and a canonical constructor
public record Maze(
    MazeCell[][] grid,
    List<Position> path,   // ordered BFS shortest path from start to end (inclusive)
    Position start,
    Position end,
    int rows,
    int cols
) {
    // Convenience accessors — avoid grid[r][c] array access noise at call sites
    public MazeCell cell(int row, int col) { return grid[row][col]; }
    public MazeCell cell(Position p)       { return grid[p.row()][p.col()]; }

    /** True when {@code pos} is within the grid bounds. */
    public boolean inBounds(Position pos) {
        return pos.row() >= 0 && pos.row() < rows && pos.col() >= 0 && pos.col() < cols;
    }
}
