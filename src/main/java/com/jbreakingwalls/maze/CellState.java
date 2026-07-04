package com.jbreakingwalls.maze;

/**
 * Sealed hierarchy of cell display-states for a path cell.
 *
 * Iteration 1: {@link Hidden} -> {@link Revealed} by cursor drag.
 * Iteration 2: {@link Walled} -> long-press blast -> {@link Revealed}.
 * Iteration 3: patrol may revert {@link Revealed} -> {@link Hidden}.
 *
 * {@code sealed} means the compiler guarantees exhaustive coverage in
 * pattern-matching switch expressions — no {@code default} branch needed.
 */
// sealed: closed type hierarchy; enables O(1) JVM tableswitch dispatch
public sealed interface CellState
        permits CellState.Hidden, CellState.Revealed, CellState.Walled {

    /** Path cell the player has not yet traced over. */
    record Hidden()  implements CellState {}

    /** Path cell successfully traced; glows to show progress. */
    record Revealed() implements CellState {}

    /**
     * Path cell blocked by a wall segment (iteration 2).
     *
     * @param health remaining blast charges; reaches 0 -> becomes Revealed
     */
    // record stores health immutably: each blast creates a new Walled(health-1)
    record Walled(int health) implements CellState {}

    // Singleton constants: re-use instances for Hidden/Revealed to avoid small-object churn
    CellState HIDDEN   = new Hidden();
    CellState REVEALED = new Revealed();
}
