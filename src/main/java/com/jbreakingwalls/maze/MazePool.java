package com.jbreakingwalls.maze;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingDeque;

/**
 * Async pre-generation pool: keeps a queue of ready-to-use {@link Maze} objects
 * so that level transitions are instant for the player.
 *
 * Java 21 concurrency highlights:
 *   - Single virtual-thread executor (Thread.ofVirtual) — near-zero overhead per task.
 *   - LinkedBlockingDeque is the shared data structure; generation pushes, game-thread pops.
 *   - CompletableFuture chains let us replenish without blocking the GL/update thread.
 *
 * When the difficulty tier changes (maze grows), cached mazes of the wrong size
 * are cleared. next() generates synchronously as a fallback — fast enough (<10ms
 * for 20x20) that a single missed pre-generation is invisible.
 */
public final class MazePool implements AutoCloseable {

    private final MazeGenerator            generator;
    private final int                       capacity;
    // LinkedBlockingDeque: thread-safe, ordered — generator pushes tail, game pops head
    private final LinkedBlockingDeque<Maze> queue;
    // Java 21 virtual thread: no OS thread per task; JVM schedules on shared carrier pool
    private final ExecutorService           worker;

    private volatile int targetRows;
    private volatile int targetCols;

    /**
     * @param seed     RNG seed for maze generation (0 = random each run)
     * @param capacity number of mazes to keep pre-built ahead of demand
     */
    public MazePool(long seed, int capacity) {
        this.generator  = new MazeGenerator(seed);
        this.capacity   = capacity;
        this.queue      = new LinkedBlockingDeque<>(capacity);
        // Virtual thread executor: replaces raw new Thread() with managed lifecycle
        this.worker     = Executors.newSingleThreadExecutor(
                Thread.ofVirtual().name("maze-gen").factory());
        this.targetRows = 5;
        this.targetCols = 5;
        replenish();
    }

    // ── Public API ────────────────────────────────────────────────────────────

    /** Notify the pool that the desired maze size has changed. */
    public void setTargetSize(int rows, int cols) {
        if (rows == targetRows && cols == targetCols) return;
        targetRows = rows;
        targetCols = cols;
        queue.clear();
        replenish();
    }

    /**
     * Return the next maze matching the current target size.
     * Pulls from the pre-generated queue if available; generates synchronously otherwise.
     */
    public Maze next(int rows, int cols) {
        setTargetSize(rows, cols);

        while (!queue.isEmpty()) {
            Maze m = queue.poll();
            if (m != null && m.rows() == rows && m.cols() == cols) {
                replenish();
                return m;
            }
        }

        // Synchronous fallback — rare, only on the first call or after a size change
        replenish();
        return generator.generate(rows, cols);
    }

    @Override
    public void close() {
        worker.shutdownNow();
    }

    // ── Private ───────────────────────────────────────────────────────────────

    private void replenish() {
        int needed = capacity - queue.size();
        for (int i = 0; i < needed; i++) {
            int r = targetRows, c = targetCols; // capture for lambda
            CompletableFuture
                    .supplyAsync(() -> generator.generate(r, c), worker)
                    .thenAccept(maze -> queue.offerLast(maze));
        }
    }
}
