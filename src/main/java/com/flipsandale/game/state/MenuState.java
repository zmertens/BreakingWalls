package com.flipsandale.game.state;

import com.flipsandale.game.InputManager;

/**
 * Menu state - the initial state of the game. Displays the main menu and handles navigation to
 * other screens like settings and gameplay.
 */
public class MenuState extends GameState {

  public MenuState(StateContext stateContext) {
    super(stateContext, GameStateId.MENU);
  }

  @Override
  public void onEnter() {
    System.out.println("→ MenuState.onEnter()");

    // Enable Nifty GUI input processing
    if (stateContext.nifty != null) {
      stateContext.nifty.gotoScreen("mainMenu");
    }

    // Show cursor for menu navigation
    stateContext.inputManager.setCursorVisible(true);
  }

  @Override
  public void onUpdate(float tpf) {
    // Menu doesn't need frame-by-frame updates
  }

  @Override
  public void onExit() {
    System.out.println("→ MenuState.onExit()");
    // Clean up if needed
  }

  @Override
  public void onInputAction(String actionName) {
    if (InputManager.ACTION_PAUSE.equals(actionName)) {
      // ESC to go back/exit from menu doesn't apply here
    }
  }
}
