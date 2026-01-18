package com.flipsandale.game;

import com.jme3.math.Quaternion;
import com.jme3.math.Vector3f;

/**
 * Manages player physics, jumping mechanics, collision detection, and state. The player is
 * represented as a simple capsule with position, velocity, and rotation.
 */
public class PlayerController {
  private Vector3f position; // Player center position
  private Vector3f velocity; // Current velocity
  private float playerRadius = 0.3f; // Collision radius
  private float playerHeight = 1.8f; // Total player height
  private AudioManager audioManager; // Audio manager for sound effects

  // Jump mechanics
  private float jumpForce = 12.0f; // Initial upward velocity when jumping
  private float gravity = -9.81f; // Gravity constant
  private boolean isGrounded = false; // Whether player is on a platform
  private boolean canJump = true; // Whether player can jump
  private float groundDampening = 0.95f; // Friction when on ground

  // Auto-run mechanics
  private float autoRunSpeed = 8.0f; // Speed of auto-run forward movement
  private boolean autoRunEnabled = true; // Whether auto-run is active

  // Camera and rotation
  private float horizontalRotation = 0f; // Rotation around Y axis (radians)
  private Quaternion rotation = new Quaternion(); // Current rotation

  // Landing detection
  private float lastGroundY = 0f; // Y position when last on ground
  private Platform lastPlatform = null; // Last platform player was on

  // Falling detection
  private float voidThreshold = -50f; // Y position below which player is considered fallen
  private com.jme3.scene.Node rootNode; // Scene root node for audio attachment

  public PlayerController(Vector3f startPosition) {
    this.position = startPosition.clone();
    this.velocity = new Vector3f(0, 0, 0);
    this.lastGroundY = startPosition.y;
  }

  /** Set the audio manager for sound effect playback. */
  public void setAudioManager(AudioManager audioManager, com.jme3.scene.Node rootNode) {
    this.audioManager = audioManager;
    this.rootNode = rootNode;
  }

  /** Updates player physics based on time delta. */
  public void update(float tpf, PlatformLayout platformLayout) {
    // Apply auto-run forward movement
    if (autoRunEnabled) {
      Vector3f forward = getForwardDirection();
      velocity.x = forward.x * autoRunSpeed;
      velocity.z = forward.z * autoRunSpeed;
    }

    // Apply gravity
    velocity.y += gravity * tpf;

    // Update position based on velocity
    position.addLocal(velocity.x * tpf, velocity.y * tpf, velocity.z * tpf);

    // Check collision with ground plane and walls
    checkGroundCollision();

    // Check if player has fallen into the void
    checkFallenState();
  }

  /**
   * Updates player with wall collision detection for obstacle-course gameplay.
   *
   * @param tpf Time per frame
   * @param walls List of walls to collide with
   */
  public void updateWithWalls(float tpf, java.util.List<MazeWallService.Wall> walls) {
    // Apply auto-run forward movement
    if (autoRunEnabled) {
      Vector3f forward = getForwardDirection();
      velocity.x = forward.x * autoRunSpeed;
      velocity.z = forward.z * autoRunSpeed;
    }

    // Apply gravity
    velocity.y += gravity * tpf;

    // Update position based on velocity
    position.addLocal(velocity.x * tpf, velocity.y * tpf, velocity.z * tpf);

    // Check collision with ground plane (always at Y=0)
    checkGroundCollision();

    // Check collision with maze walls
    checkWallCollisions(walls);

    // Check if player has fallen into the void
    checkFallenState();
  }

  /** Checks collision with the ground plane at Y=0. */
  private void checkGroundCollision() {
    float groundLevel = 0f;
    float playerFeet = position.y - (playerHeight / 2f);

    // If player's feet are below ground, push them up
    if (playerFeet <= groundLevel && velocity.y <= 0) {
      isGrounded = true;
      canJump = true;
      velocity.y = 0;
      position.y = groundLevel + (playerHeight / 2f); // Position feet on ground
      lastGroundY = position.y;

      // Apply friction
      velocity.x *= groundDampening;
      velocity.z *= groundDampening;
    } else if (playerFeet > groundLevel) {
      // Player is in the air
      isGrounded = false;
    }
  }

  /** Checks collision with maze walls. */
  private void checkWallCollisions(java.util.List<MazeWallService.Wall> walls) {
    for (MazeWallService.Wall wall : walls) {
      // Check if player is colliding with wall
      float halfWidth = wall.width / 2f;
      float halfDepth = wall.depth / 2f;
      float expandRadius = playerRadius + 0.1f; // Add small buffer for smoother collision

      float distX = Math.abs(position.x - wall.position.x);
      float distZ = Math.abs(position.z - wall.position.z);

      // Collision if player is within wall bounds horizontally and below wall top
      if (distX < (halfWidth + expandRadius)
          && distZ < (halfDepth + expandRadius)
          && position.y < wall.height + playerHeight / 2f) {

        // Play collision sound effect
        if (audioManager != null && rootNode != null) {
          audioManager.playCollisionSound(rootNode);
        }

        // Push player out of wall (choose direction based on which side they hit)
        if (distX > distZ) {
          // Hit left/right side of wall
          if (position.x < wall.position.x) {
            position.x = wall.position.x - halfWidth - expandRadius; // Push left
          } else {
            position.x = wall.position.x + halfWidth + expandRadius; // Push right
          }
          velocity.x = 0; // Stop horizontal movement in X
        } else {
          // Hit front/back of wall
          if (position.z < wall.position.z) {
            position.z = wall.position.z - halfDepth - expandRadius; // Push back
          } else {
            position.z = wall.position.z + halfDepth + expandRadius; // Push forward
          }
          velocity.z = 0; // Stop horizontal movement in Z
        }
      }
    }
  }

  /** Checks if the player has fallen below the void threshold. */
  private void checkFallenState() {
    if (position.y < voidThreshold) {
      System.out.println("Player fell into the void!");
      // This will be handled by the game state machine
    }
  }

  /** Checks if the player is in a fallen state (fell off all platforms). */
  public boolean hasFallen() {
    return position.y < voidThreshold;
  }

  /** Initiates a jump action. */
  public void jump() {
    if (canJump && isGrounded) {
      velocity.y = jumpForce;
      isGrounded = false;
      canJump = false;

      // Play jump sound effect
      if (audioManager != null && rootNode != null) {
        audioManager.playJumpSound(rootNode);
      }

      System.out.println("Jump! Velocity: " + jumpForce);
    }
  }

  /** Rotates the player's view horizontally (for mouse/stick look). */
  public void rotateCamera(float deltaRotation) {
    horizontalRotation += deltaRotation;

    // Normalize rotation to 0-2π
    while (horizontalRotation > (float) Math.PI * 2) {
      horizontalRotation -= (float) Math.PI * 2;
    }
    while (horizontalRotation < 0) {
      horizontalRotation += (float) Math.PI * 2;
    }

    // Update rotation quaternion
    rotation = new Quaternion().fromAngleAxis(horizontalRotation, Vector3f.UNIT_Y);
  }

  /** Gets the forward direction based on current rotation. */
  public Vector3f getForwardDirection() {
    Vector3f forward = new Vector3f(0, 0, -1); // Default forward
    rotation.multLocal(forward);
    return forward;
  }

  // Getters and Setters

  public Vector3f getPosition() {
    return position.clone();
  }

  public void setPosition(Vector3f newPosition) {
    this.position = newPosition.clone();
  }

  public Vector3f getVelocity() {
    return velocity.clone();
  }

  public void setVelocity(Vector3f newVelocity) {
    this.velocity = newVelocity.clone();
  }

  public boolean isGrounded() {
    return isGrounded;
  }

  public float getHorizontalRotation() {
    return horizontalRotation;
  }

  public Quaternion getRotation() {
    return rotation.clone();
  }

  public Platform getLastPlatform() {
    return lastPlatform;
  }

  public float getPlayerHeight() {
    return playerHeight;
  }

  public float getPlayerRadius() {
    return playerRadius;
  }

  public void setJumpForce(float jumpForce) {
    this.jumpForce = jumpForce;
  }

  public void setGravity(float gravity) {
    this.gravity = gravity;
  }

  public void setAutoRunSpeed(float speed) {
    this.autoRunSpeed = speed;
  }

  public void setAutoRunEnabled(boolean enabled) {
    this.autoRunEnabled = enabled;
  }

  public boolean isAutoRunEnabled() {
    return autoRunEnabled;
  }

  @Override
  public String toString() {
    return String.format(
        "Player[pos=(%.1f,%.1f,%.1f), vel=(%.1f,%.1f,%.1f), grounded=%b, rotation=%.2f]",
        position.x,
        position.y,
        position.z,
        velocity.x,
        velocity.y,
        velocity.z,
        isGrounded,
        horizontalRotation);
  }
}
