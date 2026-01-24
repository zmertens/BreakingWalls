package com.flipsandale.game;

import com.flipsandale.game.state.GameStateId;
import java.util.ArrayList;
import java.util.List;
import org.springframework.stereotype.Service;

/**
 * Manages the overall game state, scoring, timing, and level progression. Coordinates between
 * player controller, platform layouts, and game modes.
 */
@Service
public class GameStateService {

  // Game flow state
  private GameMode currentMode = GameMode.ZEN;
  private GameStateId currentGameState = GameStateId.MENU;
  private int currentLevel = 1;
  private int currentLevelSeed = 42;
  private int platformsCleared = 0;

  // Time tracking
  private float elapsedTime = 0f; // For time-trial mode
  private float bestTime = Float.MAX_VALUE; // Best time for time-trial
  private float trialStartTime = 0f; // Time when current trial started

  // Lives and scoring
  private int livesRemaining = 3;
  private int totalScore = 0;
  private int fallCount = 0;
  private int levelStartScore = 0;

  // Event listeners
  private List<GameStateListener> listeners = new ArrayList<>();

  public interface GameStateListener {
    void onGameModeChanged(GameMode newMode);

    void onLevelChanged(int levelNumber);

    void onScoreChanged(int newScore);

    void onTimeChanged(float elapsedTime);

    void onLivesChanged(int livesRemaining);

    void onPlatformCleared(int platformCount);

    void onFall();
  }

  /** Initializes a new game session with the specified mode. */
  public void startNewGame(GameMode mode) {
    currentMode = mode;
    currentLevel = 1;
    currentLevelSeed = 42;
    platformsCleared = 0;
    elapsedTime = 0f;
    fallCount = 0;
    livesRemaining = 3;
    totalScore = 0;
    trialStartTime = 0f;

    System.out.println("Game started in " + mode + " mode");
    fireGameModeChanged(mode);
    fireLevelChanged(currentLevel);
  }

  /** Initializes a new level. */
  public void startNewLevel(int levelNumber, int levelSeed) {
    currentLevel = levelNumber;
    currentLevelSeed = levelSeed;
    platformsCleared = 0;
    levelStartScore = totalScore;
    trialStartTime = elapsedTime;
    fallCount = 0;

    System.out.println("Level " + levelNumber + " started with seed " + levelSeed);
    fireLevelChanged(levelNumber);
  }

  /** Initializes a new level using current game mode. */
  public void startNewLevel() {
    startNewLevel(currentLevel + 1, currentLevelSeed + 1000);
  }

  /** Resumes a paused game. */
  public void resumeGame() {
    System.out.println("Game resumed from pause");
  }

  /** Sets the current game state. */
  public void setGameState(GameStateId newState) {
    currentGameState = newState;
    System.out.println("Game state changed to: " + newState);
  }

  /** Updates the game state (called each frame). */
  public void update(float tpf) {
    if (currentMode == GameMode.TIME_TRIAL) {
      elapsedTime += tpf;
      fireTimeChanged(elapsedTime - trialStartTime);
    }
  }

  /** Records that the player cleared a platform. */
  public void platformCleared() {
    platformsCleared++;
    int scoreGain = 10;

    if (currentMode == GameMode.TIME_TRIAL) {
      scoreGain = Math.max(5, 20 - (int) (elapsedTime - trialStartTime)); // Bonus for speed
    }

    totalScore += scoreGain;
    System.out.println("Platform cleared! Score: " + totalScore);
    fireScoreChanged(totalScore);
    firePlatformCleared(platformsCleared);
  }

  /** Records that the player fell off a platform. */
  public void playerFell() {
    fallCount++;
    System.out.println("Player fell! Fall count: " + fallCount);
    firePlayerFell();

    if (currentMode == GameMode.TIME_TRIAL) {
      // Reset the timer for the trial
      elapsedTime = trialStartTime;
      fireTimeChanged(0f);
    } else {
      // ZEN mode: just continue
    }
  }

  /** Completes the current level and prepares for the next one. */
  public void levelComplete() {
    int levelScore = totalScore - levelStartScore;
    System.out.println(
        "Level "
            + currentLevel
            + " completed! Level score: "
            + levelScore
            + ", Total score: "
            + totalScore);

    // Prepare for next level
    startNewLevel(currentLevel + 1, currentLevelSeed + currentLevel);
  }

  /** Ends the current game session. */
  public void gameOver() {
    System.out.println(
        "Game Over! Final Score: "
            + totalScore
            + ", Levels Completed: "
            + (currentLevel - 1)
            + ", Falls: "
            + fallCount);
  }

  /** Gets the current time for the ongoing trial (time-trial mode). */
  public float getCurrentTrialTime() {
    if (currentMode == GameMode.TIME_TRIAL) {
      return elapsedTime - trialStartTime;
    }
    return 0f;
  }

  // Event listener management

  public void addListener(GameStateListener listener) {
    listeners.add(listener);
  }

  public void removeListener(GameStateListener listener) {
    listeners.remove(listener);
  }

  private void fireGameModeChanged(GameMode newMode) {
    for (GameStateListener listener : listeners) {
      listener.onGameModeChanged(newMode);
    }
  }

  private void fireLevelChanged(int levelNumber) {
    for (GameStateListener listener : listeners) {
      listener.onLevelChanged(levelNumber);
    }
  }

  private void fireScoreChanged(int newScore) {
    for (GameStateListener listener : listeners) {
      listener.onScoreChanged(newScore);
    }
  }

  private void fireTimeChanged(float elapsedTime) {
    for (GameStateListener listener : listeners) {
      listener.onTimeChanged(elapsedTime);
    }
  }

  private void fireLivesChanged(int livesRemaining) {
    for (GameStateListener listener : listeners) {
      listener.onLivesChanged(livesRemaining);
    }
  }

  private void firePlatformCleared(int platformCount) {
    for (GameStateListener listener : listeners) {
      listener.onPlatformCleared(platformCount);
    }
  }

  private void firePlayerFell() {
    for (GameStateListener listener : listeners) {
      listener.onFall();
    }
  }

  // Getters

  public GameMode getCurrentMode() {
    return currentMode;
  }

  public GameStateId getCurrentGameState() {
    return currentGameState;
  }

  public int getCurrentLevel() {
    return currentLevel;
  }

  public int getCurrentLevelSeed() {
    return currentLevelSeed;
  }

  public int getPlatformsCleared() {
    return platformsCleared;
  }

  public float getElapsedTime() {
    return elapsedTime;
  }

  public float getBestTime() {
    return bestTime;
  }

  public int getCurrentScore() {
    return totalScore;
  }

  public int getTotalScore() {
    return totalScore;
  }

  public int getFallCount() {
    return fallCount;
  }

  public int getTotalFalls() {
    return fallCount;
  }

  public int getLivesRemaining() {
    return livesRemaining;
  }

  @Override
  public String toString() {
    return String.format(
        "GameState[mode=%s, level=%d, score=%d, time=%.2f, falls=%d]",
        currentMode, currentLevel, totalScore, elapsedTime, fallCount);
  }
}
