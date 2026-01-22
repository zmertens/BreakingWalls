package com.flipsandale.game.state;

import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.Vector;

/**
 * State stack manager for game states. Handles state lifecycle, transitions, and pending changes
 * queue. Similar to craft.cpp's state_stack pattern, this enables clean state management with
 * deferred transitions (pending changes applied at frame start).
 *
 * <p>States are stored in a stack; the top of the stack is the active state. Pending transitions
 * (push, pop, clear) are queued and applied at the start of the next update, ensuring consistent
 * state during the frame.
 */
public class GameStateStack {

  private static class PendingChange {
    enum Action {
      PUSH,
      POP,
      CLEAR
    }

    final Action action;
    final GameStateId stateId; // Only used for PUSH

    PendingChange(Action action, GameStateId stateId) {
      this.action = action;
      this.stateId = stateId;
    }

    PendingChange(Action action) {
      this(action, null);
    }
  }

  private final Vector<GameState> stateStack = new Vector<>();
  private final List<PendingChange> pendingChanges = new ArrayList<>();
  private final StateContext stateContext;
  private final GameStateFactory stateFactory;

  public GameStateStack(StateContext stateContext, GameStateFactory stateFactory) {
    this.stateContext = stateContext;
    this.stateFactory = stateFactory;
  }

  /**
   * Requests a state to be pushed onto the stack. The push is deferred and applied at the start of
   * the next update.
   *
   * @param stateId The ID of the state to push
   */
  public void requestPush(GameStateId stateId) {
    pendingChanges.add(new PendingChange(PendingChange.Action.PUSH, stateId));
  }

  /** Requests the active state to be popped. The pop is deferred and applied at the next update. */
  public void requestPop() {
    pendingChanges.add(new PendingChange(PendingChange.Action.POP));
  }

  /** Requests all states to be cleared. The clear is deferred and applied at the next update. */
  public void requestClear() {
    pendingChanges.add(new PendingChange(PendingChange.Action.CLEAR));
  }

  /**
   * Applies all pending state changes. Called at the start of each update before state logic runs.
   */
  public void applyPendingChanges() {
    for (PendingChange change : pendingChanges) {
      switch (change.action) {
        case PUSH:
          pushState(change.stateId);
          break;
        case POP:
          popState();
          break;
        case CLEAR:
          clearStates();
          break;
      }
    }
    pendingChanges.clear();
  }

  /**
   * Updates the active state (top of stack). Applies pending changes first, then delegates update
   * to active state.
   *
   * @param tpf Time per frame in seconds
   */
  public void update(float tpf) {
    applyPendingChanges();

    if (!stateStack.isEmpty()) {
      stateStack.lastElement().onUpdate(tpf);
    }
  }

  /**
   * Routes input action to the active state.
   *
   * @param actionName The name of the action
   */
  public void handleInputAction(String actionName) {
    if (!stateStack.isEmpty()) {
      stateStack.lastElement().onInputAction(actionName);
    }
  }

  /**
   * Gets the active state (top of stack).
   *
   * @return Optional containing the active state, or empty if stack is empty
   */
  public Optional<GameState> getActiveState() {
    return stateStack.isEmpty() ? Optional.empty() : Optional.of(stateStack.lastElement());
  }

  /**
   * Checks if the state stack is empty.
   *
   * @return true if the stack is empty, false otherwise
   */
  public boolean isEmpty() {
    return stateStack.isEmpty();
  }

  // Private implementation methods

  private void pushState(GameStateId stateId) {
    GameState newState = stateFactory.createState(stateId);
    newState.onEnter();
    stateStack.add(newState);
    System.out.println("→ Pushed state: " + stateId);
  }

  private void popState() {
    if (!stateStack.isEmpty()) {
      GameState poppedState = stateStack.remove(stateStack.size() - 1);
      poppedState.onExit();
      System.out.println("→ Popped state: " + poppedState.getId());
    }
  }

  private void clearStates() {
    while (!stateStack.isEmpty()) {
      popState();
    }
  }
}
