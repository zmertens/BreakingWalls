package com.jbreakingwalls.state;

import imgui.ImGui;
import imgui.flag.ImGuiWindowFlags;

import static org.lwjgl.glfw.GLFW.*;
import static org.lwjgl.opengl.GL33C.*;

/**
 * Main menu state — the first state pushed after the splash screen.
 *
 * Renders a centred ImGui panel with "Start", config summary, and "Quit".
 * The rendered scene behind is just the OpenGL clear colour.
 */
public final class MenuState extends State {

    private static final float PANEL_W = 400f;
    private static final float PANEL_H = 280f;

    public MenuState(StateStack stack, Context ctx) {
        super(stack, ctx);
    }

    @Override
    public void onEnter() {
        ctx.renderer.setCameraX(0f);
    }

    @Override
    public void draw() {
        float displayW = ImGui.getIO().getDisplaySizeX();
        float displayH = ImGui.getIO().getDisplaySizeY();

        ImGui.setNextWindowPos((displayW - PANEL_W) * 0.5f, (displayH - PANEL_H) * 0.5f);
        ImGui.setNextWindowSize(PANEL_W, PANEL_H);
        ImGui.setNextWindowBgAlpha(0.90f);

        int flags = ImGuiWindowFlags.NoResize
                  | ImGuiWindowFlags.NoMove
                  | ImGuiWindowFlags.NoCollapse
                  | ImGuiWindowFlags.NoTitleBar;

        if (ImGui.begin("##menu", flags)) {
            ImGui.setCursorPosX((PANEL_W - ImGui.calcTextSize("JBreaking Walls").x) * 0.5f);
            ImGui.text("JBreaking Walls");
            ImGui.separator();
            ImGui.spacing();

            // Maze config summary
            ImGui.textDisabled("Maze start: %dx%d  |  grows +%d per level".formatted(
                    ctx.config.mazeStartSize, ctx.config.mazeStartSize,
                    ctx.config.mazeGrowthPerLevel));
            ImGui.textDisabled("Wall growth: +%d%% per level  |  Patrols from level 3".formatted(
                    ctx.config.wallDensityGrowth));
            ImGui.textDisabled("Seed: %s  |  Pool: %d mazes pre-built".formatted(
                    ctx.config.randomnessSeed == 0 ? "random" : String.valueOf(ctx.config.randomnessSeed),
                    ctx.config.mazePoolSize));
            ImGui.spacing();
            ImGui.separator();
            ImGui.spacing();

            float btnW = PANEL_W - 32f;
            ImGui.setCursorPosX(16f);
            if (ImGui.button("Start", btnW, 40f)) {
                stack.pushState(State.ID.GAME);
            }
            ImGui.spacing();
            ImGui.setCursorPosX(16f);
            if (ImGui.button("Quit", btnW, 32f)) {
                stack.clearAll();
            }
        }
        ImGui.end();
    }

    @Override
    public boolean update(float dt) {
        return false;
    }

    @Override
    public boolean handleKey(int key, int action) {
        if (isPressed(key, action)) {
            if (key == GLFW_KEY_ENTER || key == GLFW_KEY_SPACE) {
                stack.pushState(State.ID.GAME);
                return false;
            }
            if (key == GLFW_KEY_ESCAPE) {
                stack.clearAll();
                return false;
            }
        }
        return true;
    }
}
