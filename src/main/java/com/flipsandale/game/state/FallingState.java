package com.flipsandale.game.state;

import com.flipsandale.game.FallingStateManager;
import com.flipsandale.game.GameMode;
import com.jme3.math.Vector3f;

/**
 * Falling state - player fall recovery. Responsible for: - Managing fall timeout (2 seconds) -
 * Transitioning to next state based on game mode (PLAYING for restart, GAME_OVER for time-trial)
 */
public class FallingState extends GameState {

  private FallingStateManager fallingStateManager;

  public FallingState(StateContext stateContext) {
    super(stateContext, GameStateId.FALLING);
  }

  @Override
  public void onEnter() {
    System.out.println("→ FallingState.onEnter()");

    // Initialize falling state manager
    Vector3f levelStartPos =
        stateContext.currentPlatformLayout != null
            ? stateContext.currentPlatformLayout.getStartPosition()
            : (stateContext.playerController != null
                ? stateContext.playerController.getPosition()
                : new Vector3f(0, 0, 0));

    fallingStateManager = new FallingStateManager(stateContext.currentGameMode);
    fallingStateManager.onFallStart(stateContext.playerController, levelStartPos);

    System.out.println("Entered FALLING state. Mode: " + stateContext.currentGameMode);
  }

  @Override
  public void onUpdate(float tpf) {
    if (fallingStateManager == null) {
      return;
    }

    // Update falling timeout
    fallingStateManager.update(tpf);

    // Check if timeout expired
    if (fallingStateManager.hasTimedOut()) {
      stateContext.gameStateService.playerFell();

      if (stateContext.currentGameMode == GameMode.ZEN) {
        // Zen mode: restart current level
        fallingStateManager.resetPlayerPosition();
        System.out.println("Level restarted!");
        // Transition back to PLAYING - handled by external logic
      } else {
        // Time-trial mode: game over
        stateContext.gameStateService.gameOver();
        stateContext.onGameOver.run();
        // Transition to GAME_OVER - handled by external logic
      }
    }
  }

  @Override
  public void onExit() {
    System.out.println("→ FallingState.onExit()");
    // FallingStateManager handles its own cleanup
  }

  @Override
  public void onInputAction(String actionName) {
    // Falling state doesn't handle input
  }
}
