package com.flipsandale.game;

import com.jme3.math.Vector3f;

/** Manages the FALLING state, handling timeouts and transitions based on game mode. */
public class FallingStateManager {
  private float fallingTimeElapsed = 0f;
  private static final float FALLING_TIMEOUT = 2.0f; // Seconds before transitioning out of FALLING
  private static final float RESPAWN_HEIGHT_BUFFER = 1.0f; // How high above start to respawn

  private GameMode gameMode;
  private PlayerController playerController;
  private Vector3f levelStartPosition;

  private boolean hasTimedOut = false;

  public FallingStateManager(GameMode gameMode) {
    this.gameMode = gameMode;
  }

  /** Sets up the falling state with current player and level context. */
  public void onFallStart(PlayerController playerController, Vector3f levelStartPosition) {
    this.playerController = playerController;
    this.levelStartPosition = levelStartPosition;
    this.fallingTimeElapsed = 0f;
    this.hasTimedOut = false;
  }

  /** Updates the falling state timer. */
  public void update(float tpf) {
    fallingTimeElapsed += tpf;
  }

  /** Checks if the falling state timeout has expired. */
  public boolean hasTimedOut() {
    return fallingTimeElapsed >= FALLING_TIMEOUT;
  }

  /** Determines what state to transition to based on the game mode. */
  public GameState getNextGameState() {
    if (gameMode == GameMode.ZEN) {
      // Zen mode: restart at current level
      return GameState.PLAYING;
    } else {
      // Time-trial mode: game over
      return GameState.GAME_OVER;
    }
  }

  /** Resets the player position for zen mode restart. */
  public void resetPlayerPosition() {
    if (playerController != null && levelStartPosition != null) {
      Vector3f spawnPos = levelStartPosition.clone();
      spawnPos.y += RESPAWN_HEIGHT_BUFFER;
      playerController.setPosition(spawnPos);
      playerController.setVelocity(new Vector3f(0, 0, 0));
      System.out.println("Player respawned at: " + spawnPos);
    }
  }

  /** Gets the elapsed time in the falling state. */
  public float getElapsedTime() {
    return fallingTimeElapsed;
  }

  /** Gets the remaining time before timeout. */
  public float getRemainingTime() {
    return Math.max(0, FALLING_TIMEOUT - fallingTimeElapsed);
  }

  /** Gets the timeout duration. */
  public float getTimeoutDuration() {
    return FALLING_TIMEOUT;
  }
}
