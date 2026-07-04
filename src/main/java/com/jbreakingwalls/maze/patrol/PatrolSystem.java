package com.jbreakingwalls.maze.patrol;

import com.jbreakingwalls.maze.MazeSession;
import com.jbreakingwalls.maze.Position;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Manages the set of {@link Patrol} entities for a level (iteration 3).
 *
 * Patrols walk sub-sections of the maze's shortest path. When a patrol
 * crosses into a cell, it calls {@link MazeSession#unreveal} on that cell —
 * hiding it again and forcing the player to re-drag over it.
 *
 * SOLID:
 *   S — owns patrol creation, movement, and the un-reveal side-effect only.
 *   O — adding a new patrol type requires only a new Patrol subclass.
 *   D — depends on MazeSession interface, not any rendering system.
 */
public final class PatrolSystem {

    private final List<Patrol> patrols = new ArrayList<>();

    // ── Setup ─────────────────────────────────────────────────────────────────

    /**
     * (Re)initialise patrols for a new level.
     *
     * @param session      the active maze session (provides path)
     * @param count        number of patrols (0 = iteration 1/2 mode, no patrols)
     * @param speed        cells per second for all patrols
     */
    public void reset(MazeSession session, int count, float speed) {
        patrols.clear();
        List<Position> path = session.getMaze().path();
        if (path.size() < count * 4 || count <= 0) return;

        int chunkSize = path.size() / count;
        for (int i = 0; i < count; i++) {
            int from = i * chunkSize;
            int to   = (i == count - 1) ? path.size() : from + chunkSize;
            patrols.add(new Patrol(path.subList(from, to), speed));
        }
    }

    // ── Per-frame update ──────────────────────────────────────────────────────

    /**
     * Advance all patrols and apply un-reveal side-effects for crossed cells.
     */
    public void update(float dt, MazeSession session) {
        for (Patrol p : patrols) {
            Position crossed = p.advance(dt);
            if (crossed != null) {
                session.unreveal(crossed);
            }
        }
    }

    /** Immutable view of patrols — safe to pass to the renderer. */
    // Collections.unmodifiableList: O(0) wrapper, no copy
    public List<Patrol> getPatrols() {
        return Collections.unmodifiableList(patrols);
    }
}
