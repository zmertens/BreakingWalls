package com.flipsandale.game.state;

import java.util.Optional;
import java.util.Stack;

/**
 * Manages the stack of game states (e.g., Menu, Playing, Paused). Handles transitions between
 * states and manages the lifecycle of each state.
 *
 * <p>States are stored in a stack; the top of the stack is the active state. Pending transitions
 * (push, pop, clear) are queued and applied at the start of the next update, ensuring consistent
 * state during the frame.
 */
public class GameStateStack {

  private enum Action {
    PUSH,
    POP
  }

  private static final class PendingChange {
    final Action action;
    final GameStateId stateId; // Only used for PUSH

    PendingChange(Action action, GameStateId stateId) {
      this.action = action;
      this.stateId = stateId;
    }
  }

  private final Stack<GameState> stack = new Stack<>();
  private final Stack<PendingChange> pendingChanges = new Stack<>();
  private final StateContext stateContext;
  private final GameStateFactory gameStateFactory;

  public GameStateStack(StateContext stateContext, GameStateFactory gameStateFactory) {
    this.stateContext = stateContext;
    this.gameStateFactory = gameStateFactory;
  }

  /**
   * Requests a new state to be pushed onto the stack. The change is queued and processed at the
   * start of the next update cycle.
   *
   * @param stateId The ID of the state to push
   */
  public void requestPush(GameStateId stateId) {
    pendingChanges.push(new PendingChange(Action.PUSH, stateId));
  }

  /**
   * Requests the current state to be popped from the stack. The change is queued and processed at
   * the start of the next update cycle.
   */
  public void requestPop() {
    pendingChanges.push(new PendingChange(Action.POP, null));
  }

  /**
   * Applies all pending state changes. Called at the start of each update before state logic runs.
   */
  public void applyPendingChanges() {
    while (!pendingChanges.isEmpty()) {
      PendingChange pendingChange = pendingChanges.pop();

      switch (pendingChange.action) {
        case PUSH:
          GameState newState = gameStateFactory.createState(pendingChange.stateId);
          stack.push(newState);
          newState.onEnter();
          break;
        case POP:
          popState();
          break;
      }
    }
  }

  /**
   * Updates the active state (top of stack). Applies pending changes first, then delegates update
   * to active state.
   *
   * @param tpf Time per frame in seconds
   */
  public void update(float tpf) {
    applyPendingChanges();

    if (!stack.isEmpty()) {
      stack.peek().onUpdate(tpf);
    }
  }

  /**
   * Routes input action to the active state.
   *
   * @param actionName The name of the action
   */
  public void handleInputAction(String actionName) {
    if (!stack.isEmpty()) {
      stack.peek().onInputAction(actionName);
    }
  }

  /**
   * Gets the active state (top of stack).
   *
   * @return Optional containing the active state, or empty if stack is empty
   */
  public Optional<GameState> getActiveState() {
    return stack.isEmpty() ? Optional.empty() : Optional.of(stack.peek());
  }

  /**
   * Checks if the state stack is empty.
   *
   * @return true if the stack is empty, false otherwise
   */
  public boolean isEmpty() {
    return stack.isEmpty();
  }

  // Private implementation methods

  private void popState() {
    if (!stack.isEmpty()) {
      GameState poppedState = stack.pop();
      poppedState.onExit();
    }
  }
}
