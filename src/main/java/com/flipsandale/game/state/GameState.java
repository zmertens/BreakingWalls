package com.flipsandale.game.state;

/**
 * Base class for all game states (e.g., Menu, Playing, Paused). Defines the core lifecycle methods
 * that each state must implement.
 */
public abstract class GameState {

  protected final StateContext stateContext;

  // Each state has a unique ID for identification
  protected final GameStateId stateId;

  protected GameState(StateContext stateContext, GameStateId stateId) {
    this.stateContext = stateContext;
    this.stateId = stateId;
  }

  /** Called when the state is first entered. */
  public abstract void onEnter();

  /**
   * Called every frame while the state is active.
   *
   * @param tpf Time per frame in seconds
   */
  public abstract void onUpdate(float tpf);

  /** Called when the state is exited. */
  public abstract void onExit();

  /**
   * Called when an input action is triggered while this state is active. States should process
   * state-specific input and ignore actions they don't handle.
   *
   * @param actionName The name of the action (from InputManager.ACTION_*)
   */
  public abstract void onInputAction(String actionName);

  /**
   * Returns the shared context for the game, providing access to services, managers, and callbacks.
   *
   * @return The StateContext
   */
  public StateContext getStateContext() {
    return stateContext;
  }

  /**
   * Returns the unique identifier for this state.
   *
   * @return The GameState
   */
  public GameStateId getId() {
    return stateId;
  }
}
