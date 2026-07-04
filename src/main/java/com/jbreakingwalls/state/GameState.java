package com.jbreakingwalls.state;

import com.jbreakingwalls.maze.CellState;
import com.jbreakingwalls.maze.MazePool;
import com.jbreakingwalls.maze.MazeSession;
import com.jbreakingwalls.maze.Position;
import com.jbreakingwalls.maze.patrol.PatrolSystem;
import com.jbreakingwalls.render.MazeRenderer;
import com.jbreakingwalls.render.Renderer;
import imgui.ImGui;
import imgui.flag.ImGuiWindowFlags;

import static org.lwjgl.glfw.GLFW.*;

/**
 * Main gameplay state — hidden-maze reveal game.
 *
 * Game summary:
 * A maze is displayed as a uniform dark grid. The player drags the mouse over
 * cells; when the cursor passes over a cell that is on the BFS shortest path,
 * that cell glows green. Goal: illuminate the complete path from start (gold)
 * to end (gold).
 *
 * Iteration 1 — Drag to reveal:
 * Core mechanic. Cursor movement is polled each update tick via
 * glfwGetCursorPos. MazeSession.tryReveal marks path cells revealed.
 *
 * Iteration 2 — Wall obstacles:
 * Some path cells spawn as CellState.Walled. Hold LMB for BLAST_HOLD_SECONDS
 * to destroy one health charge; three charges clear the wall.
 *
 * Iteration 3 — Patrol sprites:
 * PatrolSystem moves red dots along path sub-sections. When a patrol crosses
 * a revealed cell it un-reveals it, forcing a re-drag.
 *
 * SOLID:
 *   S — MazeRenderer renders; MazeSession owns state; PatrolSystem moves
 *       patrols; MazePool supplies mazes.
 *   O — New CellState subtypes require no changes here (MazeRenderer's
 *       sealed switch covers them).
 *   D — Depends on session/pool/renderer interfaces, not algorithms.
 */
public final class GameState extends State {

    private static final int   MAX_SIZE             = 22;
    private static final int   PATROL_GRACE_LEVELS  = 2;
    private static final float PATROL_SPEED         = 1.4f;

    // MazePool is AutoCloseable: virtual-thread executor released in onExit()
    private final MazePool     mazePool;
    private final MazeRenderer mazeRenderer = new MazeRenderer();
    private final PatrolSystem patrolSystem = new PatrolSystem();

    private MazeSession session;
    private int   level     = 0;
    private float totalTime = 0f;
    private int   mazeSize  = 5;

    private double cursorX = 0.0, cursorY = 0.0;

    public GameState(StateStack stack, Context ctx) {
        super(stack, ctx);
        // MazePool: Java 21 virtual-thread executor keeps pre-generated mazes ready
        mazePool = new MazePool(ctx.config.randomnessSeed, ctx.config.mazePoolSize);
    }

    @Override
    public void onEnter() {
        level     = 0;
        totalTime = 0f;
        mazeSize  = ctx.config.mazeStartSize;
        startNextLevel();
    }

    @Override
    public void onExit() {
        mazePool.close();
    }

    @Override
    public boolean update(float dt) {
        totalTime += dt;
        session.addTime(dt);

        // Iteration 3: advance patrols; un-reveal cells they enter
        patrolSystem.update(dt, session);

        // Poll cursor + mouse button each tick — no callback needed
        double[] mx = {0.0}, my = {0.0};
        // Array-overload of glfwGetCursorPos avoids MemoryStack allocation per frame
        glfwGetCursorPos(ctx.window, mx, my);
        cursorX = mx[0]; cursorY = my[0];

        int[] fw = {0}, fh = {0};
        glfwGetFramebufferSize(ctx.window, fw, fh);

        if (fw[0] > 0 && fh[0] > 0) {
            float viewW  = Renderer.VIEW_HEIGHT * ((float) fw[0] / fh[0]);
            float worldX = screenToWorldX(cursorX, fw[0], viewW);
            float worldY = screenToWorldY(cursorY, fh[0]);

            boolean leftDown = glfwGetMouseButton(ctx.window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

            if (leftDown) {
                Position hovered = mazeRenderer.hitTest(worldX, worldY);
                if (hovered != null && session.getMaze().inBounds(hovered)) {
                    if (session.isWalled(hovered)) {
                        // Iteration 2: accumulate hold time; blast fires inside tryBlast
                        session.tryBlast(hovered, dt);
                    } else {
                        // Iteration 1: reveal on first contact (drag = poll every tick)
                        session.tryReveal(hovered);
                    }
                }
            }
        }

        if (session.isComplete()) {
            startNextLevel();
        }

        return false;
    }

    @Override
    public void draw() {
        int[] fw = {0}, fh = {0};
        glfwGetFramebufferSize(ctx.window, fw, fh);
        float aspect = (fw[0] > 0 && fh[0] > 0) ? (float) fw[0] / fh[0] : 16f / 9f;
        float viewW  = Renderer.VIEW_HEIGHT * aspect;

        // Centre camera on world X=0 so the maze is horizontally centred
        ctx.renderer.setCameraX(-viewW * 0.5f);
        ctx.renderer.setViewDimensions(viewW, Renderer.VIEW_HEIGHT);
        ctx.renderer.updateProjection(fw[0], fw[0] > 0 ? fh[0] : 1);
        ctx.renderer.setTime(totalTime);

        mazeRenderer.layout(viewW, Renderer.VIEW_CENTER_Y,
                session.getMaze().rows(), session.getMaze().cols());
        mazeRenderer.draw(session, ctx.renderer,
                patrolSystem.getPatrols(), session.getBlastProgress());

        drawHud();
    }

    private void drawHud() {
        int flags = ImGuiWindowFlags.NoTitleBar
                  | ImGuiWindowFlags.NoScrollbar
                  | ImGuiWindowFlags.NoCollapse
                  | ImGuiWindowFlags.AlwaysAutoResize
                  | ImGuiWindowFlags.NoSavedSettings
                  | ImGuiWindowFlags.NoFocusOnAppearing
                  | ImGuiWindowFlags.NoNav
                  | ImGuiWindowFlags.NoMove;

        ImGui.setNextWindowPos(10f, 10f);
        ImGui.setNextWindowBgAlpha(0.62f);

        if (ImGui.begin("##hud", flags)) {
            ImGui.text("Level %d  %dx%d".formatted(
                    level, session.getMaze().rows(), session.getMaze().cols()));
            float prog = session.progress();
            ImGui.progressBar(prog, 168f, 10f, "%d%%".formatted((int)(prog * 100f)));
            ImGui.textDisabled("%.1f s  |  total %.0f s".formatted(
                    session.getElapsed(), totalTime));

            // Iteration 2 hint: show only when walled cells are present
            boolean hasWalls = session.getMaze().path().stream()
                    .anyMatch(p -> session.getMaze().cell(p).state instanceof CellState.Walled);
            if (hasWalls) {
                ImGui.spacing();
                ImGui.textDisabled("Hold LMB on red cells to blast");
            }

            if (!patrolSystem.getPatrols().isEmpty()) {
                ImGui.textDisabled("Red dots un-reveal cells - re-trace!");
            }
        }
        ImGui.end();
    }

    @Override
    public boolean handleKey(int key, int action) {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            stack.pushState(State.ID.PAUSE);
            return false;
        }
        return true;
    }

    private void startNextLevel() {
        level++;
        mazeSize = Math.min(MAX_SIZE,
                ctx.config.mazeStartSize + (level - 1) * ctx.config.mazeGrowthPerLevel);

        var maze = mazePool.next(mazeSize, mazeSize);

        int wallPct = Math.max(0, (level - 1) * ctx.config.wallDensityGrowth);
        session = new MazeSession(maze, wallPct);

        int patrolCount = Math.max(0, level - PATROL_GRACE_LEVELS);
        patrolSystem.reset(session, patrolCount, PATROL_SPEED);
    }

    // Screen px X -> world X (camera left edge = -viewW/2)
    private static float screenToWorldX(double sx, int fbW, float viewW) {
        return (float)(sx / fbW) * viewW - viewW * 0.5f;
    }

    // Screen px Y -> world Y; screen Y=0 is top, world Y increases upward
    private static float screenToWorldY(double sy, int fbH) {
        float t = (float)(1.0 - sy / fbH);
        return -Renderer.VIEW_BELOW_GROUND
             + t * (Renderer.VIEW_HEIGHT + Renderer.VIEW_BELOW_GROUND);
    }
}
