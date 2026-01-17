package com.flipsandale.game;

/**
 * Screen controller for the game over screen. Displays final statistics and allows player to
 * restart or return to menu.
 */
public class GameOverScreenController extends BaseScreenController {

  private int finalScore = 0;
  private float finalTime = 0f;
  private int finalLevel = 0;
  private int totalPlatformsCleared = 0;
  private int totalFalls = 0;

  public GameOverScreenController(GameUIManager gameUIManager, GameStateService gameStateService) {
    super(gameUIManager, gameStateService);
  }

  @Override
  protected void onBind() {
    System.out.println("GameOverScreen: Binding to Nifty");
  }

  @Override
  protected void onScreenStart() {
    System.out.println("GameOverScreen: Displaying game over statistics");
    // Capture final statistics
    finalScore = gameStateService.getCurrentScore();
    finalTime = gameStateService.getElapsedTime();
    finalLevel = gameStateService.getCurrentLevel();
    totalPlatformsCleared = gameStateService.getPlatformsCleared();
    totalFalls = gameStateService.getTotalFalls();

    System.out.println(
        "Final Stats - Score: "
            + finalScore
            + ", Time: "
            + finalTime
            + "s, Level: "
            + finalLevel
            + ", Platforms: "
            + totalPlatformsCleared
            + ", Falls: "
            + totalFalls);
  }

  @Override
  protected void onScreenEnd() {
    System.out.println("GameOverScreen: Game over screen closed");
  }

  // Getters for display in Nifty (can be used with dynamic text binding)

  public String getFinalScoreText() {
    return String.format("Score: %d", finalScore);
  }

  public String getFinalTimeText() {
    return String.format("Time: %.1f seconds", finalTime);
  }

  public String getFinalLevelText() {
    return String.format("Level: %d", finalLevel);
  }

  public String getPlatformsClearedText() {
    return String.format("Platforms Cleared: %d", totalPlatformsCleared);
  }

  public String getTotalFallsText() {
    return String.format("Total Falls: %d", totalFalls);
  }

  // UI Callback Methods (called from Nifty XML)

  public void playAgainSameMode() {
    System.out.println("GameOverScreen: Playing again with same mode");
    gameStateService.startNewGame(gameStateService.getCurrentMode());
    gameUIManager.showGameplayHUD();
  }

  public void selectDifferentMode() {
    System.out.println("GameOverScreen: Selecting different mode");
    gameUIManager.showModeSelection();
  }

  public void returnToMainMenu() {
    System.out.println("GameOverScreen: Returning to main menu");
    gameStateService.setGameState(GameState.MENU);
    gameUIManager.showMainMenu();
  }

  public void quitGame() {
    System.out.println("GameOverScreen: Quitting game");
    System.exit(0);
  }
}
