package com.flipsandale.game.state;

/**
 * Abstract base class for all game states. Each state manages its own lifecycle (enter, update,
 * exit) and input handling. States are managed by GameStateStack and use StateContext to access
 * shared services.
 */
public abstract class GameState {

  protected final StateContext stateContext;
  protected final GameStateId stateId;

  protected GameState(StateContext stateContext, GameStateId stateId) {
    this.stateContext = stateContext;
    this.stateId = stateId;
  }

  /**
   * Called when this state is pushed onto the state stack. Use for initialization like setting up
   * UI, starting timers, etc.
   */
  public abstract void onEnter();

  /**
   * Called each frame while this state is active. Use for game logic updates, physics, rendering
   * preparation, etc.
   *
   * @param tpf Time per frame in seconds
   */
  public abstract void onUpdate(float tpf);

  /**
   * Called when this state is popped from the stack. Use for cleanup like detaching nodes, stopping
   * sounds, etc.
   */
  public abstract void onExit();

  /**
   * Called when an input action is triggered while this state is active. States should process
   * state-specific input and ignore actions they don't handle.
   *
   * @param actionName The name of the action (from InputManager.ACTION_*)
   */
  public abstract void onInputAction(String actionName);

  /**
   * Gets the shared game context containing all services, managers, and callbacks.
   *
   * @return The StateContext
   */
  public StateContext getStateContext() {
    return stateContext;
  }

  /**
   * Gets this state's identifier.
   *
   * @return The GameStateId
   */
  public GameStateId getId() {
    return stateId;
  }
}
