package com.jbreakingwalls.maze;

/**
 * Mutable state for one play-through of a single {@link Maze} level.
 *
 * Responsibilities:
 *   Iteration 1 — track which path cells have been revealed by the player's drag.
 *   Iteration 2 — place wall segments on path cells; handle long-press blast
 *                 with a per-target hold timer.
 *   Iteration 3 — expose unreveal(Position) so the PatrolSystem can revert
 *                 revealed cells when a patrol crosses them.
 *
 * All mutation happens on the GL/update thread; no synchronisation is needed.
 */
public final class MazeSession {

    /** Seconds of continuous hold required to remove one wall-health charge. */
    public static final float BLAST_HOLD_SECONDS = 1.2f;

    /** Initial health for a walled path cell. */
    public static final int WALLED_HEALTH = 3;

    private final Maze maze;

    private int   revealedCount  = 0;
    private float elapsedSeconds = 0f;
    private boolean complete     = false;

    // Iteration 2: long-press blast state
    private Position blastTarget   = null;
    private float    blastProgress = 0f;

    /**
     * @param maze       the maze for this level
     * @param wallPct    percentage [0-100] of non-endpoint path cells that become walled
     */
    public MazeSession(Maze maze, int wallPct) {
        this.maze = maze;
        if (wallPct > 0) placeWalls(wallPct);
    }

    // Space walls evenly along the path; skip start and end cells
    private void placeWalls(int pct) {
        var path = maze.path();
        int n = path.size();
        for (int i = 2; i < n - 1; i += 3) {
            if (i * 100 / n < pct) {
                maze.cell(path.get(i)).state = new CellState.Walled(WALLED_HEALTH);
            }
        }
    }

    // ── Reveal API (Iteration 1 + 3) ─────────────────────────────────────────

    /**
     * Attempt to reveal the path cell at {@code pos} via cursor drag.
     *
     * @return {@code true} if the cell was newly revealed (was Hidden)
     */
    public boolean tryReveal(Position pos) {
        if (!maze.inBounds(pos)) return false;
        MazeCell cell = maze.cell(pos);
        if (!cell.onPath) return false;
        if (cell.state instanceof CellState.Hidden) {
            cell.state = CellState.REVEALED;
            revealedCount++;
            checkComplete();
            return true;
        }
        return false;
    }

    /**
     * Attempt to blast a walled cell via long-press (iteration 2).
     * Must be called every frame while the cursor is held over {@code pos}.
     *
     * @return {@code true} when the wall is fully destroyed
     */
    public boolean tryBlast(Position pos, float dt) {
        if (!maze.inBounds(pos)) return false;
        MazeCell cell = maze.cell(pos);
        // Java 21 pattern variable — extracts health in one expression
        if (!(cell.state instanceof CellState.Walled w)) return false;

        if (!pos.equals(blastTarget)) {
            blastTarget   = pos;
            blastProgress = 0f;
        }
        blastProgress += dt;

        if (blastProgress >= BLAST_HOLD_SECONDS) {
            blastProgress = 0f;
            int newHealth = w.health() - 1;
            if (newHealth <= 0) {
                cell.state = CellState.REVEALED;
                revealedCount++;
                blastTarget = null;
                checkComplete();
                return true;
            } else {
                cell.state = new CellState.Walled(newHealth);
            }
        }
        return false;
    }

    /** True when {@code pos} refers to a currently-walled path cell. */
    public boolean isWalled(Position pos) {
        return maze.inBounds(pos) && maze.cell(pos).state instanceof CellState.Walled;
    }

    /**
     * Revert a revealed cell to Hidden (called by patrol system, iteration 3).
     */
    public void unreveal(Position pos) {
        if (!maze.inBounds(pos)) return;
        MazeCell cell = maze.cell(pos);
        if (cell.state instanceof CellState.Revealed) {
            cell.state = CellState.HIDDEN;
            revealedCount = Math.max(0, revealedCount - 1);
            complete = false;
        }
    }

    public void addTime(float dt) { elapsedSeconds += dt; }

    public Maze     getMaze()           { return maze; }
    public boolean  isComplete()        { return complete; }
    public float    progress()          { return maze.path().isEmpty() ? 1f : (float) revealedCount / maze.path().size(); }
    public float    getElapsed()        { return elapsedSeconds; }
    public Position getBlastTarget()    { return blastTarget; }
    /** Blast charge progress [0..1] for the current target. */
    public float    getBlastProgress()  { return blastProgress / BLAST_HOLD_SECONDS; }

    private void checkComplete() {
        if (revealedCount >= maze.path().size()) complete = true;
    }
}
