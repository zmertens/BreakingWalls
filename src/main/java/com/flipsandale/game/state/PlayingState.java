package com.flipsandale.game.state;

import com.flipsandale.game.GameCountdownManager;
import com.flipsandale.game.InputManager;

/**
 * Playing state - main gameplay state. Responsible for: - Player physics updates - Collision
 * detection - Level completion checks - Camera updates - Input handling (jump, perspective toggle)
 */
public class PlayingState extends GameState {

  private GameCountdownManager countdownManager;
  private boolean gameStarted = false;
  private boolean jumpPressed = false;

  public PlayingState(StateContext stateContext) {
    super(stateContext, GameStateId.PLAYING);
  }

  @Override
  public void onEnter() {
    System.out.println("→ PlayingState.onEnter()");

    // Show gameplay HUD
    if (stateContext.nifty != null) {
      stateContext.nifty.gotoScreen("gameplayHUD");
    }

    // Level should be pre-initialized before entering this state
    // If not, log warning
    if (stateContext.playerController == null || stateContext.currentPlatformLayout == null) {
      System.err.println("WARNING: PlayingState entered but level not initialized!");
    }

    // Create countdown manager
    countdownManager = new GameCountdownManager(stateContext.guiNode, stateContext.guiFont);
    gameStarted = false;

    // Start countdown
    countdownManager.startCountdown(
        () -> {
          gameStarted = true;
          System.out.println("Level started!");
        });
  }

  @Override
  public void onUpdate(float tpf) {
    if (stateContext.playerController == null || stateContext.currentPlatformLayout == null) {
      return;
    }

    // Update countdown timer
    if (countdownManager != null) {
      countdownManager.update(tpf);
    }

    // Update HUD display
    if (stateContext.hudLayer != null) {
      stateContext.hudLayer.update();
    }

    // Only update player physics if countdown finished
    if (gameStarted) {
      // Handle jump if requested
      if (jumpPressed) {
        stateContext.playerController.jump();
        jumpPressed = false;
      }

      // Update player physics with wall collision
      stateContext.playerController.updateWithWalls(tpf, stateContext.currentWalls);

      // Check if player fell
      if (stateContext.playerController.hasFallen()) {
        stateContext.gameStateService.playerFell();
        stateContext.onPlayerFall.run();
        return;
      }

      // Check if player reached end of level
      if (stateContext.currentPlatformLayout.getEndPosition() != null) {
        float distToEnd =
            stateContext
                .playerController
                .getPosition()
                .distance(stateContext.currentPlatformLayout.getEndPosition());
        if (distToEnd < 1.5f && stateContext.playerController.isGrounded()) {
          stateContext.gameStateService.levelComplete();
          stateContext.onLevelComplete.run();
          return;
        }
      }
    }

    // Update camera to follow player
    CameraController.updateCamera(
        stateContext.camera,
        stateContext.playerController,
        stateContext.currentGameMode,
        stateContext.useThirdPersonView);

    // Update player visual and camera every frame
    if (stateContext.playerController != null) {
      updatePlayerVisual();
    }
  }

  @Override
  public void onExit() {
    System.out.println("→ PlayingState.onExit()");
    // Clean up countdown timer
    if (countdownManager != null) {
      // GameCountdownManager doesn't have a stop method, just let it clean up
    }
  }

  @Override
  public void onInputAction(String actionName) {
    if (InputManager.ACTION_JUMP.equals(actionName)) {
      jumpPressed = true;
    } else if (InputManager.ACTION_PAUSE.equals(actionName)) {
      // Pause request - external state transition logic will handle
    } else if (InputManager.ACTION_PERSPECTIVE_TOGGLE.equals(actionName)) {
      stateContext.useThirdPersonView = !stateContext.useThirdPersonView;
      System.out.println(
          "Perspective toggled: "
              + (stateContext.useThirdPersonView ? "Third-Person" : "First-Person"));
    }
  }

  /** Updates the player visual geometry to match the player's current position and rotation. */
  private void updatePlayerVisual() {
    // This will be extracted to a separate service
    // For now, placeholder implementation
  }
}
