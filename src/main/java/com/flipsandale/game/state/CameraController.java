package com.flipsandale.game.state;

import com.flipsandale.game.GameMode;
import com.flipsandale.game.PlayerController;
import com.jme3.math.Vector3f;
import com.jme3.renderer.Camera;

/**
 * Camera controller service - centralizes all camera update logic. Separated from states to enable
 * reuse and testability. Handles both first-person and third-person perspectives.
 */
public class CameraController {

  private static final float THIRD_PERSON_DISTANCE = 4.0f;
  private static final float THIRD_PERSON_HEIGHT = 2.0f;

  /**
   * Updates the game camera to follow the player based on game mode and perspective settings.
   *
   * @param camera The JME3 camera to update
   * @param playerController The player controller
   * @param gameMode The current game mode
   * @param useThirdPersonView Whether to use third-person perspective
   */
  public static void updateCamera(
      Camera camera,
      PlayerController playerController,
      GameMode gameMode,
      boolean useThirdPersonView) {
    if (playerController == null || camera == null) {
      return;
    }

    Vector3f playerPos = playerController.getPosition();

    if (gameMode == GameMode.ZEN) {
      if (useThirdPersonView) {
        // Third-person camera: follow player from behind
        Vector3f cameraPos = playerPos.clone();

        // Get the forward direction (direction player is facing)
        Vector3f forward = playerController.getForwardDirection();

        // Position camera behind and above the player
        cameraPos.addLocal(forward.mult(-THIRD_PERSON_DISTANCE));
        cameraPos.y += THIRD_PERSON_HEIGHT;

        camera.setLocation(cameraPos);

        // Look slightly down at the player from behind
        Vector3f lookTarget = playerPos.clone();
        lookTarget.y += playerController.getPlayerHeight() * 0.5f; // Look at player's torso
        camera.lookAt(lookTarget, Vector3f.UNIT_Y);
      } else {
        // First-person camera: follow player's eyes
        Vector3f cameraPos = playerPos.clone();
        cameraPos.y += playerController.getPlayerHeight() * 0.8f; // Eyes position

        camera.setLocation(cameraPos);

        // Look in the direction of rotation
        Vector3f lookDir = playerController.getForwardDirection();
        Vector3f targetLook = cameraPos.add(lookDir.mult(10));
        camera.lookAt(targetLook, Vector3f.UNIT_Y);
      }
    } else if (gameMode == GameMode.TIME_TRIAL) {
      // Orthogonal camera: top-down angled view
      Vector3f cameraPos = playerPos.clone();
      cameraPos.y += 15; // Height above player
      cameraPos.z += 10; // Offset back
      cameraPos.x += 5; // Slight offset to side

      camera.setLocation(cameraPos);
      camera.lookAt(playerPos, Vector3f.UNIT_Y);
    }
  }
}
