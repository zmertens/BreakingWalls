package com.jbreakingwalls.maze;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Queue;
import java.util.Random;
import java.util.Set;

/**
 * Generates perfect mazes using the Recursive Backtracker (DFS) algorithm,
 * then finds the shortest path via BFS.
 *
 * A "perfect" maze has exactly one path between any two cells — no cycles.
 * BFS on a perfect maze therefore always finds the shortest (and only) path.
 *
 * Algorithm:
 *   1. Mark every cell unvisited; all passages are initially closed.
 *   2. Start at (0,0) and push onto an explicit stack.
 *   3. While the stack is non-empty, choose a random unvisited neighbour,
 *      carve the passage (open the shared wall), mark visited, and push.
 *   4. Backtrack when no unvisited neighbours remain.
 *   5. Run BFS from (0,0) to (rows-1, cols-1) to label path cells.
 *
 * All randomness is seeded — the same seed always produces the same maze,
 * which is important for reproducibility in the async pre-generation pool.
 */
public final class MazeGenerator {

    private final Random rng;

    /**
     * @param seed 0 = use system nanotime (non-reproducible); any other value = fixed seed
     */
    public MazeGenerator(long seed) {
        this.rng = new Random(seed == 0L ? System.nanoTime() : seed);
    }

    // ── Public API ────────────────────────────────────────────────────────────

    /**
     * Generate a perfect maze of the given dimensions and annotate the shortest path.
     *
     * @param rows logical row count (>= 2)
     * @param cols logical column count (>= 2)
     * @return fully built {@link Maze} with path labelled
     */
    public Maze generate(int rows, int cols) {
        // 1. Start with all walls closed (openWalls=0 everywhere)
        int[][] walls = new int[rows][cols];

        // 2. Carve passages via iterative DFS (avoids stack overflow on large mazes)
        boolean[][] visited = new boolean[rows][cols];
        visited[0][0] = true;
        carveIterative(walls, visited, rows, cols);

        // 3. BFS shortest path from top-left to bottom-right
        List<Position> path = bfsPath(walls, rows, cols);

        // 4. Build immutable cell grid
        Set<Position> pathSet = new HashSet<>(path);
        MazeCell[][] grid = new MazeCell[rows][cols];
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                Position p = new Position(r, c);
                grid[r][c] = new MazeCell(p, pathSet.contains(p), walls[r][c]);
            }
        }

        // List.copyOf: immutable snapshot, no defensive copy needed at call sites
        return new Maze(grid, List.copyOf(path),
                new Position(0, 0), new Position(rows - 1, cols - 1), rows, cols);
    }

    // ── Private: maze carving ─────────────────────────────────────────────────

    // Iterative DFS: avoids stack overflow for large mazes (recursive depth = rows*cols)
    private void carveIterative(int[][] walls, boolean[][] visited, int rows, int cols) {
        // ArrayDeque as explicit stack: faster than Stack (no synchronized overhead)
        ArrayDeque<int[]> stack = new ArrayDeque<>();
        stack.push(new int[]{0, 0});

        int[] dirs = {MazeCell.N, MazeCell.E, MazeCell.S, MazeCell.W};

        while (!stack.isEmpty()) {
            int[] cur = stack.peek();
            int r = cur[0], c = cur[1];

            shuffle(dirs);
            boolean moved = false;

            for (int dir : dirs) {
                int nr = r + dr(dir), nc = c + dc(dir);
                if (nr >= 0 && nr < rows && nc >= 0 && nc < cols && !visited[nr][nc]) {
                    visited[nr][nc] = true;
                    walls[r][c]   |= dir;            // open wall on current cell
                    walls[nr][nc] |= opposite(dir);  // open matching wall on neighbour
                    stack.push(new int[]{nr, nc});
                    moved = true;
                    break; // continue DFS from the new cell
                }
            }
            if (!moved) stack.pop(); // backtrack
        }
    }

    // ── Private: BFS shortest path ────────────────────────────────────────────

    // BFS guarantees shortest path in an unweighted graph; perfect maze has unique path
    private List<Position> bfsPath(int[][] walls, int rows, int cols) {
        Position start = new Position(0, 0);
        Position end   = new Position(rows - 1, cols - 1);

        Map<Position, Position> parent = new HashMap<>(rows * cols * 2);
        Queue<Position> queue = new ArrayDeque<>(rows * cols);

        queue.add(start);
        parent.put(start, null); // null parent marks the origin

        while (!queue.isEmpty()) {
            Position cur = queue.poll();
            if (cur.equals(end)) break; // found; stop early

            int r = cur.row(), c = cur.col();
            for (int dir : new int[]{MazeCell.N, MazeCell.E, MazeCell.S, MazeCell.W}) {
                if ((walls[r][c] & dir) != 0) { // passage open in this direction
                    Position next = new Position(r + dr(dir), c + dc(dir));
                    if (!parent.containsKey(next)) {
                        parent.put(next, cur);
                        queue.add(next);
                    }
                }
            }
        }

        // Reconstruct path by following parent pointers back to start
        List<Position> path = new ArrayList<>();
        for (Position p = end; p != null; p = parent.get(p)) {
            path.add(p);
        }
        Collections.reverse(path);
        return path;
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    private static int dr(int dir) {
        return switch (dir) { case MazeCell.N -> -1; case MazeCell.S -> 1; default -> 0; };
    }

    private static int dc(int dir) {
        return switch (dir) { case MazeCell.E -> 1; case MazeCell.W -> -1; default -> 0; };
    }

    private static int opposite(int dir) {
        return switch (dir) {
            case MazeCell.N -> MazeCell.S; case MazeCell.S -> MazeCell.N;
            case MazeCell.E -> MazeCell.W; case MazeCell.W -> MazeCell.E;
            default -> 0;
        };
    }

    // Fisher-Yates in-place shuffle — O(n), avoids Collections.shuffle boxing overhead
    private void shuffle(int[] arr) {
        for (int i = arr.length - 1; i > 0; i--) {
            int j = rng.nextInt(i + 1);
            int t = arr[i]; arr[i] = arr[j]; arr[j] = t;
        }
    }
}
