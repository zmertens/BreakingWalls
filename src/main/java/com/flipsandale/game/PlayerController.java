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

  // Jump mechanics
  private float jumpForce = 12.0f; // Initial upward velocity when jumping
  private float gravity = -9.81f; // Gravity constant
  private boolean isGrounded = false; // Whether player is on a platform
  private boolean canJump = true; // Whether player can jump
  private float groundDampening = 0.95f; // Friction when on ground

  // Camera and rotation
  private float horizontalRotation = 0f; // Rotation around Y axis (radians)
  private Quaternion rotation = new Quaternion(); // Current rotation

  // Landing detection
  private float lastGroundY = 0f; // Y position when last on ground
  private Platform lastPlatform = null; // Last platform player was on

  // Falling detection
  private float voidThreshold = -50f; // Y position below which player is considered fallen

  public PlayerController(Vector3f startPosition) {
    this.position = startPosition.clone();
    this.velocity = new Vector3f(0, 0, 0);
    this.lastGroundY = startPosition.y;
  }

  /** Updates player physics based on time delta. */
  public void update(float tpf, PlatformLayout platformLayout) {
    // Apply gravity
    velocity.y += gravity * tpf;

    // Update position based on velocity
    position.addLocal(velocity.x * tpf, velocity.y * tpf, velocity.z * tpf);

    // Check collision with platforms
    checkPlatformCollisions(platformLayout);

    // Check if player has fallen into the void
    checkFallenState();
  }

  /** Initiates a jump action. */
  public void jump() {
    if (canJump && isGrounded) {
      velocity.y = jumpForce;
      isGrounded = false;
      canJump = false;

      System.out.println("Jump! Velocity: " + jumpForce);
    }
  }

  /** Checks collision with all platforms in the layout. */
  private void checkPlatformCollisions(PlatformLayout platformLayout) {
    Platform collidingPlatform = null;
    float highestPlatformY = Float.NEGATIVE_INFINITY;

    if (platformLayout.getPlatforms().isEmpty()) {
      System.out.println("WARNING: No platforms to check collision with!");
      return;
    }

    // Find the highest platform below or at the player that the player is colliding with
    for (Platform platform : platformLayout.getPlatforms()) {
      boolean isAbove = platform.isPointAbovePlatform(position);

      if (isAbove) {
        float platformTop = platform.getTopSurfaceY();
        float playerFeet = position.y - playerRadius / 2;

        // Check if player is falling onto the platform or already on it
        boolean isAboveAndFalling = playerFeet >= platformTop && velocity.y <= 0;
        boolean isInsidePlatform =
            playerFeet < platformTop && playerFeet >= (platformTop - playerRadius);

        if ((isAboveAndFalling || isInsidePlatform) && platformTop > highestPlatformY) {
          // This is the highest platform we're colliding with
          highestPlatformY = platformTop;
          collidingPlatform = platform;
        }
      }
    }

    // Land on platform if collision detected
    if (collidingPlatform != null) {
      // Always land on the platform to keep the player from falling through
      if (!isGrounded) {
        System.out.println("Landing on platform at Y=" + collidingPlatform.getTopSurfaceY());
      }
      landOnPlatform(collidingPlatform);
    } else if (!isGrounded) {
      // If not grounded and not on a platform, we're falling
      if (position.y > -40f) { // Only log while still in playable area
        System.out.println(
            "Falling: Pos.y="
                + position.y
                + ", Vel.y="
                + velocity.y
                + ", Platforms count="
                + platformLayout.getPlatforms().size());
      }
      canJump = false;
    }
  }

  /** Handles landing on a platform. */
  private void landOnPlatform(Platform platform) {
    isGrounded = true;
    canJump = true;
    lastPlatform = platform;
    lastGroundY = position.y;

    // Stop downward velocity
    velocity.y = 0;

    // Position player on top of platform
    position.y = platform.getTopSurfaceY() + playerRadius / 2;

    // Apply friction
    velocity.x *= groundDampening;
    velocity.z *= groundDampening;
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
