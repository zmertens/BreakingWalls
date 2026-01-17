package com.flipsandale.game;

/**
 * Screen controller for the mode selection screen. Allows player to choose between ZEN and
 * TIME_TRIAL modes.
 */
public class ModeSelectScreenController extends BaseScreenController {

  public ModeSelectScreenController(
      GameUIManager gameUIManager, GameStateService gameStateService) {
    super(gameUIManager, gameStateService);
  }

  @Override
  protected void onBind() {
    System.out.println("ModeSelectScreen: Binding to Nifty");
  }

  @Override
  protected void onScreenStart() {
    System.out.println("ModeSelectScreen: Screen started");
  }

  @Override
  protected void onScreenEnd() {
    System.out.println("ModeSelectScreen: Screen ended");
  }

  // UI Callback Methods (called from Nifty XML)

  public void selectZenMode() {
    System.out.println("ModeSelectScreen: ZEN mode selected");
    gameUIManager.onZenModeSelected();
    startGameWithSelectedMode();
  }

  public void selectTimeTrialMode() {
    System.out.println("ModeSelectScreen: TIME_TRIAL mode selected");
    gameUIManager.onTimeTrialModeSelected();
    startGameWithSelectedMode();
  }

  public void startGameWithSelectedMode() {
    System.out.println("ModeSelectScreen: Starting game with selected mode");
    GameMode selectedMode = gameUIManager.getSelectedGameMode();
    gameStateService.startNewGame(selectedMode);
    gameUIManager.showGameplayHUD();
  }

  public void backToMainMenu() {
    System.out.println("ModeSelectScreen: Returning to main menu");
    gameUIManager.showMainMenu();
  }
}
