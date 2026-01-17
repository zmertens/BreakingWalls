package com.flipsandale.game;

/** Screen controller for the main menu. */
public class MainMenuScreenController extends BaseScreenController {

  public MainMenuScreenController(GameUIManager gameUIManager, GameStateService gameStateService) {
    super(gameUIManager, gameStateService);
  }

  @Override
  protected void onBind() {
    System.out.println("MainMenu: Binding to Nifty");
  }

  @Override
  protected void onScreenStart() {
    System.out.println("MainMenu: Main menu displayed");
    // Reset game state when returning to menu
    gameStateService.setGameState(GameState.MENU);
  }

  @Override
  protected void onScreenEnd() {
    System.out.println("MainMenu: Main menu closed");
  }

  // UI Callback Methods (called from Nifty XML)

  public void startGame() {
    System.out.println("MainMenu: Starting new game");
    gameUIManager.showModeSelection();
  }

  public void showSettings() {
    System.out.println("MainMenu: Settings selected (not yet implemented)");
    // TODO: Implement settings screen
  }

  public void quitGame() {
    System.out.println("MainMenu: Quitting game");
    System.exit(0);
  }
}
