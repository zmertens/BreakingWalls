package com.flipsandale.game;

/**
 * Screen controller for the pause menu. Allows player to resume, restart, or return to main menu.
 */
public class PauseMenuScreenController extends BaseScreenController {

  public PauseMenuScreenController(GameUIManager gameUIManager, GameStateService gameStateService) {
    super(gameUIManager, gameStateService);
  }

  @Override
  protected void onBind() {
    System.out.println("PauseMenu: Binding to Nifty");
  }

  @Override
  protected void onScreenStart() {
    System.out.println("PauseMenu: Pause menu opened");
  }

  @Override
  protected void onScreenEnd() {
    System.out.println("PauseMenu: Pause menu closed");
  }

  // UI Callback Methods (called from Nifty XML)

  public void resumeGame() {
    System.out.println("PauseMenu: Resuming game");
    gameStateService.resumeGame();
    gameUIManager.showGameplayHUD();
  }

  public void restartLevel() {
    System.out.println("PauseMenu: Restarting level");
    gameUIManager.onRestartLevel();
    gameStateService.startNewLevel();
    gameUIManager.showGameplayHUD();
  }

  public void returnToMainMenu() {
    System.out.println("PauseMenu: Returning to main menu");
    gameStateService.setGameState(GameState.MENU);
    gameUIManager.showMainMenu();
  }

  public void quitGame() {
    System.out.println("PauseMenu: Quitting game");
    System.exit(0);
  }
}
