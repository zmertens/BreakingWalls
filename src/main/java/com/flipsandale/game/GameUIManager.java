package com.flipsandale.game;

import de.lessvoid.nifty.Nifty;
import de.lessvoid.nifty.screen.Screen;

/**
 * Central manager for all UI elements and interactions. Coordinates HUD updates, screen
 * transitions, and UI callbacks.
 */
public class GameUIManager implements GameStateService.GameStateListener {

  private Nifty nifty;
  private HUDManager hudManager;
  private GameStateService gameStateService;

  // Screen references
  private Screen currentScreen;
  private String selectedMode = "zen"; // Default mode

  public GameUIManager(Nifty nifty, HUDManager hudManager, GameStateService gameStateService) {
    this.nifty = nifty;
    this.hudManager = hudManager;
    this.gameStateService = gameStateService;

    // Subscribe to game state changes
    gameStateService.addListener(this);
    gameStateService.addListener(hudManager);
  }

  /** Updates the HUD display with current game metrics. */
  public void updateHUD() {
    // HUD updates happen through Nifty screen controller callbacks
    // and HUDManager listener updates
  }

  /** Gets the currently selected game mode from menu selection. */
  public GameMode getSelectedGameMode() {
    if ("time-trial".equalsIgnoreCase(selectedMode)) {
      return GameMode.TIME_TRIAL;
    }
    return GameMode.ZEN;
  }

  /** Sets the selected game mode (called from UI callbacks). */
  public void setSelectedMode(String mode) {
    selectedMode = mode;
    System.out.println("UI: Game mode selected: " + mode);
  }

  /** Shows the mode selection screen. */
  public void showModeSelection() {
    if (nifty != null) {
      nifty.gotoScreen("modeSelection");
      System.out.println("UI: Showing mode selection screen");
    }
  }

  /** Shows the pause menu. */
  public void showPauseMenu() {
    if (nifty != null) {
      nifty.gotoScreen("pauseMenu");
      System.out.println("UI: Showing pause menu");
    }
  }

  /** Shows the game over screen. */
  public void showGameOverScreen() {
    if (nifty != null) {
      nifty.gotoScreen("gameOverScreen");
      System.out.println("UI: Showing game over screen");
    }
  }

  /** Shows the main menu. */
  public void showMainMenu() {
    if (nifty != null) {
      nifty.gotoScreen("mainMenu");
      System.out.println("UI: Showing main menu");
    }
  }

  /** Shows the gameplay HUD (empty screen for HUD overlay). */
  public void showGameplayHUD() {
    if (nifty != null) {
      nifty.gotoScreen("gameplayHUD");
      System.out.println("UI: Showing gameplay HUD");
    }
  }

  /** Gets current HUD content for display. */
  public String getHUDContent() {
    return hudManager.getHUDContent();
  }

  // GameStateListener Implementation

  @Override
  public void onGameModeChanged(GameMode newMode) {
    System.out.println("UIManager: Game mode changed to " + newMode);
  }

  @Override
  public void onLevelChanged(int levelNumber) {
    System.out.println("UIManager: Level changed to " + levelNumber);
  }

  @Override
  public void onScoreChanged(int newScore) {
    updateHUD();
  }

  @Override
  public void onTimeChanged(float elapsedTime) {
    updateHUD();
  }

  @Override
  public void onLivesChanged(int livesRemaining) {
    // Not used in current game design
  }

  @Override
  public void onPlatformCleared(int platformCount) {
    updateHUD();
  }

  @Override
  public void onFall() {
    updateHUD();
  }

  // UI Callbacks (called from Nifty XML)

  public void onZenModeSelected() {
    setSelectedMode("zen");
  }

  public void onTimeTrialModeSelected() {
    setSelectedMode("time-trial");
  }

  public void onStartGame() {
    System.out.println("UI: Starting game with mode: " + selectedMode);
  }

  public void onResumeGame() {
    System.out.println("UI: Resuming game");
  }

  public void onRestartLevel() {
    System.out.println("UI: Restarting level");
  }

  public void onReturnToMenu() {
    System.out.println("UI: Returning to menu");
  }

  public void onQuitGame() {
    System.out.println("UI: Quitting game");
  }
}
