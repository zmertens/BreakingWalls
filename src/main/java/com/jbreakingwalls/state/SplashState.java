package com.jbreakingwalls.state;

import imgui.ImGui;
import imgui.flag.ImGuiWindowFlags;

import static org.lwjgl.glfw.GLFW.*;

/**
 * Intro splash screen — auto-advances to the main menu after a brief hold.
 * Mirrors SplashState from the C++ source.
 *
 * Any key press skips directly to the menu.
 */
public final class SplashState extends State {

    private static final float FADE_IN  = 0.55f;
    private static final float HOLD     = 1.60f;
    private static final float FADE_OUT = 0.50f;
    private static final float TOTAL    = FADE_IN + HOLD + FADE_OUT;

    private static final String TITLE    = "Breaking Walls";
    private static final String SUBTITLE = "an endless slalom runner";

    // float field — primitive, zero GC pressure for per-frame accumulation
    private float elapsed = 0f;

    public SplashState(StateStack stack, Context ctx) {
        super(stack, ctx);
    }

    @Override
    public void onEnter() {
        elapsed = 0f;
    }

    @Override
    public boolean update(float dt) {
        elapsed += dt;
        if (elapsed >= TOTAL) {
            stack.replaceState(State.ID.MENU);
        }
        return false;
    }

    @Override
    public void draw() {
        float alpha = computeAlpha();
        float w = ImGui.getIO().getDisplaySizeX();
        float h = ImGui.getIO().getDisplaySizeY();

        int flags = ImGuiWindowFlags.NoDecoration
                  | ImGuiWindowFlags.NoMove
                  | ImGuiWindowFlags.NoSavedSettings
                  | ImGuiWindowFlags.NoFocusOnAppearing;

        ImGui.setNextWindowPos(0f, 0f);
        ImGui.setNextWindowSize(w, h);
        ImGui.setNextWindowBgAlpha(alpha * 0.94f);

        if (ImGui.begin("##splash", flags)) {
            float fontSize = ImGui.getFontSize();

            float titleW = ImGui.calcTextSize(TITLE).x;
            ImGui.setCursorPos((w - titleW) * 0.5f, h * 0.42f);
            ImGui.pushStyleColor(0, 0.95f, 0.95f, 1.00f, alpha);
            ImGui.text(TITLE);
            ImGui.popStyleColor();

            float subW = ImGui.calcTextSize(SUBTITLE).x;
            ImGui.setCursorPos((w - subW) * 0.5f, h * 0.42f + fontSize * 1.8f);
            ImGui.pushStyleColor(0, 0.70f, 0.72f, 0.90f, alpha * 0.72f);
            ImGui.text(SUBTITLE);
            ImGui.popStyleColor();

            if (elapsed > FADE_IN) {
                String hint = "press any key";
                float hintW = ImGui.calcTextSize(hint).x;
                ImGui.setCursorPos((w - hintW) * 0.5f, h * 0.72f);
                float hintAlpha = Math.min(1f, (elapsed - FADE_IN) / 0.40f) * alpha;
                ImGui.pushStyleColor(0, 0.55f, 0.55f, 0.65f, hintAlpha);
                ImGui.text(hint);
                ImGui.popStyleColor();
            }
        }
        ImGui.end();
    }

    @Override
    public boolean handleKey(int key, int action) {
        if (action == GLFW_PRESS) {
            stack.replaceState(State.ID.MENU);
        }
        return false;
    }

    // Pure arithmetic — no allocation; JIT inlines this single-call method
    private float computeAlpha() {
        if (elapsed < FADE_IN) return elapsed / FADE_IN;
        float fadeOutStart = FADE_IN + HOLD;
        if (elapsed > fadeOutStart) return Math.max(0f, 1f - (elapsed - fadeOutStart) / FADE_OUT);
        return 1f;
    }
}
