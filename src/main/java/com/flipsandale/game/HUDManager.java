package com.flipsandale.game;

/**
 * Manages real-time HUD display showing game metrics. Updates score, time, level, and platform
 * count during gameplay.
 */
public class HUDManager implements GameStateService.GameStateListener {

  // HUD state
  private int currentLevel = 1;
  private int currentScore = 0;
  private float currentTime = 0f;
  private int platformsCleared = 0;
  private int fallCount = 0;
  private GameMode currentMode = GameMode.ZEN;

  // Formatting
  private static final String SCORE_FORMAT = "Score: %d";
  private static final String TIME_FORMAT = "Time: %.2fs";
  private static final String LEVEL_FORMAT = "Level: %d";
  private static final String PLATFORMS_FORMAT = "Platforms: %d";
  private static final String FALLS_FORMAT = "Falls: %d";
  private static final String BEST_TIME_FORMAT = "Best: %.2fs";

  /** Gets formatted score string for display. */
  public String getScoreText() {
    return String.format(SCORE_FORMAT, currentScore);
  }

  /** Gets formatted time string for display. */
  public String getTimeText() {
    return String.format(TIME_FORMAT, currentTime);
  }

  /** Gets formatted level string for display. */
  public String getLevelText() {
    return String.format(LEVEL_FORMAT, currentLevel);
  }

  /** Gets formatted platforms cleared string for display. */
  public String getPlatformsText() {
    return String.format(PLATFORMS_FORMAT, platformsCleared);
  }

  /** Gets formatted fall count string for display. */
  public String getFallsText() {
    return String.format(FALLS_FORMAT, fallCount);
  }

  /**
   * Gets the appropriate HUD content based on game mode. Zen mode shows: Level, Platforms, Falls
   * Time-trial mode shows: Time, Score, Level
   */
  public String getHUDContent() {
    StringBuilder sb = new StringBuilder();

    if (currentMode == GameMode.ZEN) {
      sb.append(getLevelText()).append("\n");
      sb.append(getPlatformsText()).append("\n");
      sb.append(getFallsText());
    } else {
      sb.append(getTimeText()).append("\n");
      sb.append(getScoreText()).append("\n");
      sb.append(getLevelText());
    }

    return sb.toString();
  }

  // GameStateListener Implementation

  @Override
  public void onGameModeChanged(GameMode newMode) {
    currentMode = newMode;
    System.out.println("HUD: Game mode changed to " + newMode);
  }

  @Override
  public void onLevelChanged(int levelNumber) {
    currentLevel = levelNumber;
    System.out.println("HUD: Level changed to " + levelNumber);
  }

  @Override
  public void onScoreChanged(int newScore) {
    currentScore = newScore;
  }

  @Override
  public void onTimeChanged(float elapsedTime) {
    currentTime = elapsedTime;
  }

  @Override
  public void onLivesChanged(int livesRemaining) {
    // Not used in current game design
  }

  @Override
  public void onPlatformCleared(int platformCount) {
    platformsCleared = platformCount;
  }

  @Override
  public void onFall() {
    fallCount++;
  }

  // Getters for individual values

  public int getCurrentLevel() {
    return currentLevel;
  }

  public int getCurrentScore() {
    return currentScore;
  }

  public float getCurrentTime() {
    return currentTime;
  }

  public int getPlatformsCleared() {
    return platformsCleared;
  }

  public int getFallCount() {
    return fallCount;
  }

  public GameMode getCurrentMode() {
    return currentMode;
  }
}
