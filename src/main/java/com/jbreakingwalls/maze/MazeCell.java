package com.jbreakingwalls.maze;

/**
 * One cell in the maze grid.
 *
 * Not a record because {@link #state} is mutable — it transitions as the
 * player reveals cells. All structural data ({@link #pos}, {@link #onPath},
 * {@link #openWalls}) is final and set once at maze-creation time.
 *
 * Wall bitmask (openWalls): each bit = direction; 1 = passage open, 0 = wall.
 *   N = 1 (0001)   north (row-1)
 *   E = 2 (0010)   east  (col+1)
 *   S = 4 (0100)   south (row+1)
 *   W = 8 (1000)   west  (col-1)
 */
public final class MazeCell {

    // Directional constants — powers-of-two so isOpen() uses a single bitwise AND
    public static final int N = 0b0001;
    public static final int E = 0b0010;
    public static final int S = 0b0100;
    public static final int W = 0b1000;

    public final Position pos;

    /** True when this cell is on the BFS shortest path from start to end. */
    public final boolean onPath;

    /** Bitmask of open passages out of this cell. */
    public final int openWalls;

    /**
     * Mutable display state.
     * Non-path cells stay {@link CellState#HIDDEN} permanently.
     * Path cells transition Hidden -> Revealed (or Walled -> Revealed in iter 2).
     */
    public CellState state;

    // Package-private — only MazeGenerator creates cells
    MazeCell(Position pos, boolean onPath, int openWalls) {
        this.pos       = pos;
        this.onPath    = onPath;
        this.openWalls = openWalls;
        this.state     = CellState.HIDDEN;
    }

    /** True when the passage in direction {@code dir} is open. */
    // bitwise AND: single cycle on any JVM; JIT inlines trivially
    public boolean isOpen(int dir) { return (openWalls & dir) != 0; }
}
