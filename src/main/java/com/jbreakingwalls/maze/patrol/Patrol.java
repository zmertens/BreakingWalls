package com.jbreakingwalls.maze.patrol;

import com.jbreakingwalls.maze.Position;

import java.util.List;

/**
 * A patrol entity that moves back-and-forth along a sub-section of the
 * maze's shortest path (iteration 3).
 *
 * When a patrol's discrete cell position changes, it triggers
 * {@link com.jbreakingwalls.maze.MazeSession#unreveal} on the newly
 * entered cell — forcing the player to re-trace that cell.
 *
 * Movement model: progress is a float in [0, route.size()-1].
 * The integer part is the current route index; fractional part enables
 * interpolated rendering (smooth sprite movement between cells).
 * The patrol bounces direction at each endpoint (ping-pong).
 */
public final class Patrol {

    /** Ordered list of maze positions this patrol walks along. Immutable. */
    public final List<Position> route;

    /** Movement speed in cells per second. */
    public final float speed;

    /** Fractional position along the route; integer part = current cell index. */
    public float progress   = 0f;
    /** +1 = moving forward along route, -1 = moving backward. */
    public int   direction  = 1;
    /** Cell this patrol is currently occupying (snapped to integer progress). */
    public Position currentCell;

    /**
     * @param route ordered maze positions; must have at least 2 entries
     * @param speed cells per second
     */
    public Patrol(List<Position> route, float speed) {
        // List.copyOf: defensive copy, avoids unexpected external mutation
        this.route       = List.copyOf(route);
        this.speed       = speed;
        this.currentCell = route.isEmpty() ? null : route.get(0);
    }

    /**
     * Advance the patrol by {@code dt} seconds (ping-pong movement).
     *
     * @return the newly occupied {@link Position} if the patrol crossed a cell
     *         boundary this tick, {@code null} if it stayed in the same cell
     */
    public Position advance(float dt) {
        if (route.size() < 2) return null;

        int prevIndex = (int) progress;
        progress += speed * dt * direction;

        // Ping-pong: reverse direction at endpoints
        if (progress >= route.size() - 1) {
            progress  = route.size() - 1;
            direction = -1;
        } else if (progress < 0f) {
            progress  = 0f;
            direction = 1;
        }

        int newIndex = (int) progress;
        if (newIndex != prevIndex) {
            currentCell = route.get(newIndex);
            return currentCell;
        }
        return null;
    }

    /** Smoothly-interpolated row between the two surrounding route cells. */
    public float interpRow() { return interp(true); }

    /** Smoothly-interpolated column. */
    public float interpCol() { return interp(false); }

    private float interp(boolean useRow) {
        int n = route.size();
        if (n == 0) return 0f;
        if (n == 1) return useRow ? route.get(0).row() : route.get(0).col();
        int   i0   = Math.min((int) progress, n - 2);
        int   i1   = i0 + 1;
        float frac = progress - i0;
        float v0   = useRow ? route.get(i0).row() : route.get(i0).col();
        float v1   = useRow ? route.get(i1).row() : route.get(i1).col();
        return v0 + frac * (v1 - v0); // lerp — no allocation, single multiply+add
    }
}
