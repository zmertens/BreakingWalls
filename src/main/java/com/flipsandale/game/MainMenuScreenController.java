package com.flipsandale.game;

/** Screen controller for the main menu. Handles navigation to new game, options, help, and exit. */
public class MainMenuScreenController extends BaseScreenController {

  public MainMenuScreenController(GameUIManager gameUIManager, GameStateService gameStateService) {
    super(gameUIManager, gameStateService);
  }

  @Override
  protected void onBind() {}

  @Override
  protected void onScreenStart() {
    // Reset game state when returning to menu
    gameStateService.setGameState(GameState.MENU);
  }

  @Override
  protected void onScreenEnd() {}

  // Main Menu Actions

  public void startGame() {
    gameUIManager.showModeSelection();
  }

  public void showOptions() {
    gameUIManager.showOptionsMenu();
  }

  public void showHelp() {
    gameUIManager.showHelpScreen();
  }

  public void quitGame() {
    System.exit(0);
  }
}
