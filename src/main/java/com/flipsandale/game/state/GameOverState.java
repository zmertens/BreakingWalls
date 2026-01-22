package com.flipsandale.game.state;

/**
 * Game over state - final game state. Responsible for: - Showing game over screen with stats -
 * Handling restart and return to menu actions
 */
public class GameOverState extends GameState {

  public GameOverState(StateContext stateContext) {
    super(stateContext, GameStateId.GAME_OVER);
  }

  @Override
  public void onEnter() {
    System.out.println("→ GameOverState.onEnter()");

    // Show game over screen
    if (stateContext.nifty != null) {
      stateContext.nifty.gotoScreen("gameOverMenu");
    }

    // Show cursor for menu interaction
    stateContext.inputManager.setCursorVisible(true);
  }

  @Override
  public void onUpdate(float tpf) {
    // Game over state doesn't update game logic
  }

  @Override
  public void onExit() {
    System.out.println("→ GameOverState.onExit()");
  }

  @Override
  public void onInputAction(String actionName) {
    // Game over screen handles inputs through Nifty GUI callbacks
  }
}
