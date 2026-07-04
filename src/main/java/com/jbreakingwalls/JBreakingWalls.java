package com.jbreakingwalls;

import com.jbreakingwalls.config.GameConfig;
import com.jbreakingwalls.render.Renderer;
import com.jbreakingwalls.state.*;
import imgui.ImGui;
import imgui.flag.ImGuiConfigFlags;
import imgui.gl3.ImGuiImplGl3;
import imgui.glfw.ImGuiImplGlfw;
import org.lwjgl.glfw.Callbacks;
import org.lwjgl.glfw.GLFWErrorCallback;
import org.lwjgl.glfw.GLFWVidMode;
import org.lwjgl.opengl.GL;
import org.lwjgl.system.MemoryUtil;

import static org.lwjgl.glfw.GLFW.*;
import static org.lwjgl.opengl.GL33C.*;

/**
 * Game singleton — mirrors PhysicsGame from the C++ source.
 *
 * Owns the GLFW window, OpenGL context, ImGui backends, the Renderer,
 * and the StateStack. The main game loop follows the fixed-timestep
 * accumulator pattern used in the C++ implementation.
 */
public final class JBreakingWalls {

    // ── Singleton ─────────────────────────────────────────────────────────────

    private static JBreakingWalls instance;

    public static JBreakingWalls create(String title, int w, int h, GameConfig config) {
        if (instance == null) {
            instance = new JBreakingWalls(title, w, h, config);
            instance.init();
        }
        return instance;
    }

    // ── Fields ────────────────────────────────────────────────────────────────

    private final String     title;
    private final int        initW, initH;
    private final GameConfig config;

    private long window;

    private final Renderer      renderer  = new Renderer();
    private final ImGuiImplGlfw imguiGlfw = new ImGuiImplGlfw();
    private final ImGuiImplGl3  imguiGl3  = new ImGuiImplGl3();

    private StateStack stateStack;

    private double fpsTimer     = 0.0;
    private int    smoothedFps  = 0;
    private static final double FPS_UPDATE_INTERVAL_MS = 250.0;

    private JBreakingWalls(String title, int w, int h, GameConfig config) {
        this.title  = title;
        this.initW  = w;
        this.initH  = h;
        this.config = config;
    }

    // ── Initialisation ────────────────────────────────────────────────────────

    private void init() {
        initGlfw();
        initImGui();
        initRenderer();
        initStates();
    }

    private void initGlfw() {
        GLFWErrorCallback.createPrint(System.err).set();

        if (!glfwInit()) {
            throw new RuntimeException("Failed to initialise GLFW");
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        window = glfwCreateWindow(initW, initH, title, MemoryUtil.NULL, MemoryUtil.NULL);
        if (window == MemoryUtil.NULL) {
            glfwTerminate();
            throw new RuntimeException("Failed to create GLFW window");
        }

        GLFWVidMode mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
        if (mode != null) {
            glfwSetWindowPos(window,
                    (mode.width()  - initW) / 2,
                    (mode.height() - initH) / 2);
        }

        glfwMakeContextCurrent(window);
        glfwSwapInterval(1); // VSync
        GL.createCapabilities();

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    private void initImGui() {
        ImGui.createContext();
        ImGui.getIO().addConfigFlags(ImGuiConfigFlags.NavEnableKeyboard);
        ImGui.styleColorsDark();

        // installCallbacks=false so our own GLFW key callback drives game input
        imguiGlfw.init(window, false);
        imguiGl3.init("#version 330");
    }

    private void initRenderer() {
        renderer.init(config);
    }

    private void initStates() {
        Context ctx = new Context(config, renderer, window, imguiGlfw, imguiGl3);
        stateStack  = new StateStack(ctx);

        // Lambdas captured at registration time — no allocation per push, only on first create
        // EnumMap inside StateStack stores these in O(1) by ordinal
        stateStack.registerState(State.ID.SPLASH,  () -> new SplashState (stateStack, ctx));
        // LoadingState: pass a Runnable for async work — swap in real asset loads as needed
        stateStack.registerState(State.ID.LOADING, () -> new LoadingState(stateStack, ctx, State.ID.GAME, () -> {}));
        stateStack.registerState(State.ID.MENU,    () -> new MenuState   (stateStack, ctx));
        stateStack.registerState(State.ID.GAME,    () -> new GameState   (stateStack, ctx));
        stateStack.registerState(State.ID.PAUSE,   () -> new PauseState  (stateStack, ctx));

        // Start with the splash screen; it transitions to MENU automatically
        stateStack.pushState(State.ID.SPLASH);
        // Flush the deferred push immediately so the first draw has a state
        stateStack.update(0f);
    }

    // ── Main game loop ────────────────────────────────────────────────────────

    public void run() {
        glfwSetKeyCallback(window, (win, key, scancode, action, mods) -> {
            if (!stateStack.isEmpty()) {
                stateStack.handleKey(key, action);
            }
        });

        long   previousNanos = System.nanoTime();
        double accumulator   = 0.0;
        final double fixedDt = 1.0 / config.targetFps;

        while (!glfwWindowShouldClose(window) && !stateStack.isEmpty()) {
            glfwPollEvents();

            imguiGlfw.newFrame();
            ImGui.newFrame();

            long   currentNanos = System.nanoTime();
            double elapsed      = (currentNanos - previousNanos) / 1_000_000_000.0;
            if (elapsed > 0.25) elapsed = 0.25;
            previousNanos = currentNanos;
            accumulator += elapsed;

            int steps = 0;
            while (accumulator >= fixedDt && steps < 4) {
                stateStack.update((float) fixedDt);
                accumulator -= fixedDt;
                steps++;
            }

            int[] fbW = {0}, fbH = {0};
            glfwGetFramebufferSize(window, fbW, fbH);
            glViewport(0, 0, fbW[0], fbH[0]);

            glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            stateStack.draw();

            if (config.showDebugOverlay) {
                drawDebugOverlay(elapsed * 1000.0, fbW[0], fbH[0]);
            }

            ImGui.render();
            imguiGl3.renderDrawData(ImGui.getDrawData());
            glfwSwapBuffers(window);
        }
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────

    public void dispose() {
        stateStack.clearAll();
        stateStack.update(0f);

        renderer.dispose();

        imguiGl3.dispose();
        imguiGlfw.dispose();
        ImGui.destroyContext();

        Callbacks.glfwFreeCallbacks(window);
        glfwDestroyWindow(window);
        glfwTerminate();
        GLFWErrorCallback cb = glfwSetErrorCallback(null);
        if (cb != null) cb.free();
    }

    // ── Debug overlay ─────────────────────────────────────────────────────────

    private void drawDebugOverlay(double elapsedMs, int fbW, int fbH) {
        fpsTimer += elapsedMs;
        if (fpsTimer >= FPS_UPDATE_INTERVAL_MS) {
            smoothedFps = (elapsedMs > 0) ? (int) (1000.0 / elapsedMs) : 0;
            fpsTimer    = 0.0;
        }

        ImGui.setNextWindowPos(fbW - 120f, 10f);
        ImGui.setNextWindowBgAlpha(0.5f);
        int flags = imgui.flag.ImGuiWindowFlags.NoDecoration
                  | imgui.flag.ImGuiWindowFlags.NoMove
                  | imgui.flag.ImGuiWindowFlags.AlwaysAutoResize
                  | imgui.flag.ImGuiWindowFlags.NoSavedSettings;

        if (ImGui.begin("##fps", flags)) {
            ImGui.text("%d fps".formatted(smoothedFps));
            ImGui.text("%.2f ms".formatted(elapsedMs));
        }
        ImGui.end();
    }
}
