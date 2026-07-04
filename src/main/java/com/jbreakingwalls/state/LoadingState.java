package com.jbreakingwalls.state;

import imgui.ImGui;
import imgui.flag.ImGuiWindowFlags;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Async loading screen — mirrors LoadingState from the C++ source.
 *
 * CompletableFuture provides structured, exception-propagating async work:
 *   - No manual thread lifecycle management.
 *   - Exceptions are captured and surfaced in statusMessage.
 *   - The future becomes GC-eligible once the transition fires.
 *
 * Once loadFuture completes the state replaces itself with targetState.
 */
public final class LoadingState extends State {

    private static final float PANEL_W = 340f;
    private static final float PANEL_H = 90f;

    private final State.ID targetState;

    // AtomicReference: safe cross-thread write from ForkJoinPool worker, read on GL thread
    private final AtomicReference<String> statusMessage = new AtomicReference<>("Loading");

    // CompletableFuture runs the Runnable on ForkJoinPool.commonPool()
    private final CompletableFuture<Void> loadFuture;

    // Animation counter — primitive float field avoids boxing
    private float dotTimer = 0f;

    /**
     * @param stack    owning state stack
     * @param ctx      shared context bag
     * @param target   state to transition to on completion
     * @param loadTask background work to perform (runs on ForkJoinPool)
     */
    public LoadingState(StateStack stack, Context ctx, State.ID target, Runnable loadTask) {
        super(stack, ctx);
        this.targetState = target;
        this.loadFuture = CompletableFuture
                .runAsync(loadTask)
                .exceptionally(ex -> {
                    statusMessage.set("Error: " + ex.getMessage());
                    return null;
                });
    }

    @Override
    public void onEnter() {
        dotTimer = 0f;
    }

    @Override
    public boolean update(float dt) {
        dotTimer += dt;
        // isDone() is a non-blocking check — avoids blocking the GL thread
        if (loadFuture.isDone()) {
            stack.replaceState(targetState);
        }
        return false;
    }

    @Override
    public void draw() {
        float w = ImGui.getIO().getDisplaySizeX();
        float h = ImGui.getIO().getDisplaySizeY();

        ImGui.setNextWindowPos((w - PANEL_W) * 0.5f, (h - PANEL_H) * 0.5f);
        ImGui.setNextWindowSize(PANEL_W, PANEL_H);
        ImGui.setNextWindowBgAlpha(0.88f);

        int flags = ImGuiWindowFlags.NoDecoration
                  | ImGuiWindowFlags.NoMove
                  | ImGuiWindowFlags.NoSavedSettings;

        if (ImGui.begin("##loading", flags)) {
            // Animated ellipsis — String.repeat is JVM-intrinsified, avoids StringBuilder
            int dotCount = (int)(dotTimer * 2.5f) % 4;
            String line  = statusMessage.get() + ".".repeat(dotCount);
            float textW  = ImGui.calcTextSize(line).x;
            ImGui.setCursorPosX((PANEL_W - textW) * 0.5f);
            ImGui.text(line);

            // Indeterminate spinner
            float t    = (float)(System.nanoTime() % 2_000_000_000L) / 2_000_000_000f;
            float prog = (float) Math.abs(Math.sin(t * Math.PI));
            ImGui.setCursorPosX(16f);
            ImGui.progressBar(prog, PANEL_W - 32f, 6f, "");
        }
        ImGui.end();
    }

    @Override
    public boolean handleKey(int key, int action) {
        return false; // swallow all input during loading
    }
}
