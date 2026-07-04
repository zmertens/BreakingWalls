package com.jbreakingwalls.maze;

/**
 * Immutable (row, col) grid coordinate.
 *
 * record auto-generates equals/hashCode using structural equality —
 * this makes Position safe as a HashMap key and Set member with zero boilerplate.
 */
// record: value semantics, stack-allocatable in JIT-optimised paths
public record Position(int row, int col) {

    /** True when this position is within a grid of the given dimensions. */
    public boolean inBounds(int rows, int cols) {
        return row >= 0 && row < rows && col >= 0 && col < cols;
    }

    @Override
    public String toString() {
        return "(%d,%d)".formatted(row, col);
    }
}
