package com.jbreakingwalls.render;

import com.jbreakingwalls.maze.CellState;
import com.jbreakingwalls.maze.Maze;
import com.jbreakingwalls.maze.MazeCell;
import com.jbreakingwalls.maze.MazeSession;
import com.jbreakingwalls.maze.Position;
import com.jbreakingwalls.maze.patrol.Patrol;

import java.util.List;

/**
 * Renders the maze grid using the existing {@link Renderer} quad pipeline.
 *
 * Single Responsibility: compute grid layout and draw cells, walls, patrol
 * sprites. Independent of game logic — reads from MazeSession but does not
 * mutate it (Dependency Inversion).
 *
 * Coordinate system:
 * OpenGL Y-axis points up; maze row 0 is the visual top row.
 * Conversion: worldY(row) = originY + (rows-1-row) * cellH
 */
public final class MazeRenderer {

    // ── Cell colour palette ───────────────────────────────────────────────────

    private static final float[] COL_FLOOR     = {0.11f, 0.11f, 0.14f, 1f};
    private static final float[] COL_HIDDEN    = {0.14f, 0.14f, 0.19f, 1f};
    private static final float[] COL_REVEALED  = {0.15f, 0.80f, 0.55f, 1f};
    private static final float[] COL_WALLED_1  = {0.60f, 0.20f, 0.10f, 1f}; // health 3
    private static final float[] COL_WALLED_2  = {0.75f, 0.35f, 0.08f, 1f}; // health 2
    private static final float[] COL_WALLED_3  = {0.90f, 0.55f, 0.05f, 1f}; // health 1
    private static final float[] COL_ENDPOINT  = {0.95f, 0.80f, 0.10f, 1f};
    private static final float[] COL_PATROL    = {0.90f, 0.15f, 0.15f, 1f};
    private static final float[] COL_BLAST_RING = {1.00f, 0.90f, 0.20f, 1f};
    private static final float[] COL_WALL_LINE  = {0.06f, 0.06f, 0.09f, 1f};

    private static final float WALL_GAP = 0.07f;

    // ── Layout state ──────────────────────────────────────────────────────────

    private float originX, originY;
    private float cellW,   cellH;
    private int   rows,    cols;

    // ── Layout ───────────────────────────────────────────────────────────────

    /**
     * Compute cell size and grid origin to fit within the viewport.
     * Call once per frame before {@link #draw}.
     *
     * @param viewW    orthographic viewport width (world-units)
     * @param centerY  world-Y of the viewport centre
     * @param rows     maze row count
     * @param cols     maze column count
     */
    public void layout(float viewW, float centerY, int rows, int cols) {
        this.rows = rows;
        this.cols = cols;

        final float PAD = 1.8f;
        float maxW  = viewW - PAD * 2f;
        float maxH  = Renderer.VIEW_HEIGHT - PAD * 2f;
        // Square cells: choose the size that fits in both dimensions
        float cellSz = Math.min(maxW / cols, maxH / rows);
        cellW = cellH = cellSz;

        float gridW = cellSz * cols;
        float gridH = cellSz * rows;
        originX = -gridW / 2f;
        originY = centerY - gridH / 2f;
    }

    // ── Hit-test ──────────────────────────────────────────────────────────────

    /**
     * Convert a world-space cursor position to a maze grid {@link Position}.
     *
     * @return the cell under the cursor, or {@code null} if outside the grid
     */
    public Position hitTest(float worldX, float worldY) {
        if (cellW <= 0f || cellH <= 0f) return null;
        int col = (int) ((worldX - originX) / cellW);
        // Y is flipped: row 0 is visual top -> highest worldY
        int row = rows - 1 - (int) ((worldY - originY) / cellH);
        if (col < 0 || col >= cols || row < 0 || row >= rows) return null;
        return new Position(row, col);
    }

    // ── Drawing ───────────────────────────────────────────────────────────────

    /**
     * Draw all cells, structural walls, patrol sprites, and the blast charge ring.
     */
    public void draw(MazeSession session, Renderer r,
                     List<Patrol> patrols, float blastProg) {
        Maze maze = session.getMaze();

        for (int row = 0; row < rows; row++) {
            for (int col = 0; col < cols; col++) {
                drawCell(r, maze.cell(row, col), maze, row, col);
            }
        }

        // Blast charge ring on currently-held walled cell (iteration 2)
        Position bt = session.getBlastTarget();
        if (bt != null) drawBlastRing(r, bt, blastProg);

        // Patrol sprites on top of the grid (iteration 3)
        for (Patrol p : patrols) {
            if (p.currentCell != null) drawPatrol(r, p);
        }
    }

    // ── Private draw helpers ──────────────────────────────────────────────────

    private void drawCell(Renderer r, MazeCell cell, Maze maze, int row, int col) {
        float x = cellX(col);
        float y = cellY(row);
        float w = cellW - WALL_GAP;
        float h = cellH - WALL_GAP;

        // Java 21 pattern-matching switch — exhaustive over sealed CellState hierarchy
        float[] colour = switch (cell.state) {
            case CellState.Hidden   hid -> cell.onPath ? COL_HIDDEN : COL_FLOOR;
            case CellState.Revealed rev -> COL_REVEALED;
            case CellState.Walled   wal -> switch (wal.health()) {
                case 1  -> COL_WALLED_3;
                case 2  -> COL_WALLED_2;
                default -> COL_WALLED_1;
            };
        };

        if (cell.pos.equals(maze.start()) || cell.pos.equals(maze.end())) {
            colour = COL_ENDPOINT;
        }

        float glow = (cell.state instanceof CellState.Revealed) ? 0.65f : 0f;
        r.drawRectGlow(x, y, w, h, colour, glow);

        drawStructuralWalls(r, cell, x, y);
    }

    private void drawStructuralWalls(Renderer r, MazeCell cell, float x, float y) {
        float lw = WALL_GAP;
        if (!cell.isOpen(MazeCell.S)) {
            r.drawRect(x, y - lw, cellW - WALL_GAP, lw, COL_WALL_LINE);
        }
        if (!cell.isOpen(MazeCell.E)) {
            r.drawRect(x + cellW - lw, y, lw, cellH - WALL_GAP, COL_WALL_LINE);
        }
    }

    private void drawBlastRing(Renderer r, Position pos, float prog) {
        float x   = cellX(pos.col());
        float y   = cellY(pos.row());
        float sz  = Math.min(cellW, cellH);
        float pad = sz * 0.08f;
        float[] col = {COL_BLAST_RING[0], COL_BLAST_RING[1], COL_BLAST_RING[2], 0.4f + prog * 0.6f};
        r.drawRectGlow(x - pad, y - pad, cellW - WALL_GAP + 2*pad, sz * prog, col, 1.5f);
    }

    private void drawPatrol(Renderer r, Patrol p) {
        float worldX = originX + p.interpCol() * cellW;
        float worldY = originY + (rows - 1 - p.interpRow()) * cellH;
        float dot    = Math.min(cellW, cellH) * 0.55f;
        r.drawRectGlow(worldX + (cellW - dot) * 0.5f - WALL_GAP * 0.5f,
                       worldY + (cellH - dot) * 0.5f - WALL_GAP * 0.5f,
                       dot, dot, COL_PATROL, 1.3f);
    }

    // ── Coordinate helpers ────────────────────────────────────────────────────

    private float cellX(int col) { return originX + col * cellW; }

    // Row 0 = top visual row -> highest Y
    private float cellY(int row) { return originY + (rows - 1 - row) * cellH; }
}
