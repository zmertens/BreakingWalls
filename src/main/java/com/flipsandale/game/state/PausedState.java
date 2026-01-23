package com.flipsandale.game.state;

import com.flipsandale.game.InputManager;

/**
 * Paused state - pause menu overlay. Responsible for: - Showing pause menu - Handling resume,
 * settings, and exit to menu actions
 */
public class PausedState extends GameState {

  public PausedState(StateContext stateContext) {
    super(stateContext, GameStateId.PAUSED);
  }

  @Override
  public void onEnter() {
    System.out.println("→ PausedState.onEnter()");

    // Show pause menu
    if (stateContext.nifty != null) {
      System.out.println("  - Nifty is available, switching to pauseMenu");
      stateContext.nifty.gotoScreen("pauseMenu");
      System.out.println("  - gotoScreen call completed");
    } else {
      System.err.println("  - ERROR: Nifty is null!");
    }

    // Show cursor for menu interaction
    stateContext.inputManager.setCursorVisible(true);
  }

  @Override
  public void onUpdate(float tpf) {
    // Paused state doesn't update game logic
  }

  @Override
  public void onExit() {
    System.out.println("→ PausedState.onExit()");
    // Hide cursor for gameplay
  }

  @Override
  public void onInputAction(String actionName) {
    if (InputManager.ACTION_PAUSE.equals(actionName)) {
      // ESC to resume game
      stateContext.gameStateService.resumeGame();
      // State transition handled by external logic
    } else if (InputManager.ACTION_MENU_TOGGLE.equals(actionName)) {
      // M to return to menu - transitions handled by external logic
    }
  }
}
