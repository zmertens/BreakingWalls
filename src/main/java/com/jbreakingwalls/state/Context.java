package com.jbreakingwalls.state;

import com.jbreakingwalls.config.GameConfig;
import com.jbreakingwalls.render.Renderer;
import imgui.glfw.ImGuiImplGlfw;
import imgui.gl3.ImGuiImplGl3;

/**
 * Shared read-only context bag injected into every {@link State}.
 *
 * Mirrors the State::Context builder pattern from the C++ source:
 * each state has access to the config, renderer, window handle, and ImGui
 * backends without depending on the full JBreakingWalls singleton.
 */
public final class Context {

    /** Loaded and validated game configuration. */
    public final GameConfig config;

    /** GPU-side renderer (shader, VAO, projection matrix). */
    public final Renderer renderer;

    /** Raw GLFW window handle — pass to GLFW functions when needed. */
    public final long window;

    /** ImGui GLFW platform backend. */
    public final ImGuiImplGlfw imguiGlfw;

    /** ImGui OpenGL3 renderer backend. */
    public final ImGuiImplGl3 imguiGl3;

    public Context(GameConfig config,
                   Renderer renderer,
                   long window,
                   ImGuiImplGlfw imguiGlfw,
                   ImGuiImplGl3 imguiGl3) {
        this.config     = config;
        this.renderer   = renderer;
        this.window     = window;
        this.imguiGlfw  = imguiGlfw;
        this.imguiGl3   = imguiGl3;
    }
}
