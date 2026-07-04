package com.jbreakingwalls.state;

import static org.lwjgl.glfw.GLFW.*;

/**
 * Abstract game state — mirrors State.hpp from the C++ source.
 *
 * Each concrete state overrides the three lifecycle hooks.
 * The {@link StateStack} drives the stack by calling these methods from
 * the main game loop.
 *
 * sealed + permits: the compiler verifies all subclasses are listed here,
 * enabling exhaustive pattern-matching switch expressions throughout the
 * codebase with no default branch required.
 */
// sealed: closed type hierarchy; compiler-verified exhaustive matching
public abstract sealed class State
        permits GameState, MenuState, PauseState, SplashState, LoadingState {

    /** Enumeration of all registered states — ordered by typical presentation flow. */
    // enum over int constants: type-safe, serialisable, and zero-cost in EnumMap/EnumSet
    public enum ID { SPLASH, LOADING, MENU, GAME, PAUSE }

    protected final StateStack stack;
    protected final Context    ctx;

    protected State(StateStack stack, Context ctx) {
        this.stack = stack;
        this.ctx   = ctx;
    }

    /**
     * Issue OpenGL draw calls and ImGui widget calls for this state.
     * Called once per rendered frame.
     */
    public abstract void draw();

    /**
     * Advance simulation by one fixed timestep.
     *
     * @param dt fixed delta-time in seconds
     * @return {@code true} to keep processing states below on the stack
     */
    public abstract boolean update(float dt);

    /**
     * Handle a GLFW key event.
     *
     * @param key    GLFW key constant
     * @param action GLFW_PRESS, GLFW_RELEASE, or GLFW_REPEAT
     * @return {@code true} to keep passing the event down the stack
     */
    public abstract boolean handleKey(int key, int action);

    /** Called once immediately after the state is pushed onto the stack. */
    public void onEnter() {}

    /** Called once immediately before the state is popped or replaced. */
    public void onExit()  {}

    protected boolean isPressed(int key, int action) {
        return action == GLFW_PRESS;
    }
}
