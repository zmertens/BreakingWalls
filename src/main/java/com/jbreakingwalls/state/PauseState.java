package com.jbreakingwalls.state;

import imgui.ImGui;
import imgui.flag.ImGuiWindowFlags;

import static org.lwjgl.glfw.GLFW.*;

/**
 * Pause overlay — rendered on top of the live (frozen) game scene.
 *
 * Mirrors PauseState from the C++ source. The game state below is drawn
 * but NOT updated (GameState.update returns false for propagation).
 */
public final class PauseState extends State {

    private static final float PANEL_W = 190f;
    private static final float PANEL_H = 148f;

    public PauseState(StateStack stack, Context ctx) {
        super(stack, ctx);
    }

    @Override
    public boolean update(float dt) {
        return false; // freeze everything below
    }

    @Override
    public void draw() {
        float displayW = ImGui.getIO().getDisplaySizeX();
        float displayH = ImGui.getIO().getDisplaySizeY();

        ImGui.setNextWindowPos(displayW - 10f - PANEL_W, (displayH - PANEL_H) * 0.5f);
        ImGui.setNextWindowSize(PANEL_W, PANEL_H);
        ImGui.setNextWindowBgAlpha(0.90f);

        int flags = ImGuiWindowFlags.NoResize
                  | ImGuiWindowFlags.NoMove
                  | ImGuiWindowFlags.NoCollapse
                  | ImGuiWindowFlags.NoTitleBar
                  | ImGuiWindowFlags.NoSavedSettings;

        if (ImGui.begin("##pause", flags)) {
            ImGui.text("PAUSED");
            ImGui.separator();
            ImGui.spacing();

            float btnW = PANEL_W - 20f;
            if (ImGui.button("Resume", btnW, 32f)) {
                stack.popState();
            }
            ImGui.spacing();
            if (ImGui.button("Restart", btnW, 28f)) {
                stack.popState();
                stack.replaceState(State.ID.GAME);
            }
            ImGui.spacing();
            if (ImGui.button("Quit to Menu", btnW, 28f)) {
                stack.popState();
                stack.replaceState(State.ID.MENU);
            }
        }
        ImGui.end();
    }

    @Override
    public boolean handleKey(int key, int action) {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            stack.popState();
            return false;
        }
        return false;
    }
}
