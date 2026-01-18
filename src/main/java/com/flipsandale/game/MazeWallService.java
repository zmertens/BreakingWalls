package com.flipsandale.game;

import com.jme3.math.Vector3f;
import java.util.ArrayList;
import java.util.List;
import org.springframework.stereotype.Service;

/**
 * Converts maze data into wall geometry for an obstacle-course style game.
 *
 * <p>The ground plane is at Y=0, and walls are vertical obstacles placed on top. Walls are
 * generated from the maze structure, creating barriers the player must navigate around or jump
 * over.
 */
@Service
public class MazeWallService {

  // Wall dimensions
  private static final float WALL_HEIGHT = 2.0f; // Height of walls above ground
  private static final float WALL_THICKNESS = 0.2f; // Thickness of wall geometry
  private static final float CELL_SIZE = 1.0f; // Size of each maze cell

  /** Represents a wall segment in the game world */
  public static class Wall {
    public Vector3f position; // Center position of wall
    public float width; // Width of wall (along one axis)
    public float depth; // Depth of wall (along other axis)
    public float height; // Height of wall

    public Wall(Vector3f position, float width, float depth) {
      this.position = position;
      this.width = width;
      this.depth = depth;
      this.height = WALL_HEIGHT;
    }

    /** Check if a point is within the wall's collision bounds */
    public boolean isPointInWall(Vector3f point) {
      float halfWidth = width / 2f;
      float halfDepth = depth / 2f;

      // Check X bounds
      if (Math.abs(point.x - position.x) > halfWidth) {
        return false;
      }

      // Check Z bounds
      if (Math.abs(point.z - position.z) > halfDepth) {
        return false;
      }

      // Check Y bounds (should be above ground, below wall top)
      if (point.y < 0 || point.y > height) {
        return false;
      }

      return true;
    }
  }

  /**
   * Generates wall geometry from a platform layout (converted maze). In the new system, "platforms"
   * become walls that sit on the ground plane.
   *
   * @param platformLayout The platform layout containing maze structure
   * @return List of wall segments that make up the maze
   */
  public List<Wall> generateWallsFromMaze(PlatformLayout platformLayout) {
    List<Wall> walls = new ArrayList<>();

    if (platformLayout == null || platformLayout.getPlatforms().isEmpty()) {
      return walls;
    }

    // Convert each platform into a wall on the ground plane
    for (Platform platform : platformLayout.getPlatforms()) {
      // Position wall on ground plane (Y=0) at the platform's X/Z location
      Vector3f wallPos =
          new Vector3f(platform.getPosition().x, WALL_HEIGHT / 2f, platform.getPosition().z);

      // Create wall with platform's width and depth
      Wall wall = new Wall(wallPos, platform.getWidth(), platform.getDepth());
      walls.add(wall);
    }

    return walls;
  }

  /**
   * Check if a moving object (player) collides with any wall. Returns the wall if collision
   * detected, null otherwise.
   *
   * @param position Current position of player
   * @param radius Collision radius of player
   * @param walls List of walls to check
   * @return Colliding wall or null
   */
  public Wall checkWallCollision(Vector3f position, float radius, List<Wall> walls) {
    for (Wall wall : walls) {
      // Expand wall bounds by player radius for collision detection
      float expandedWidth = wall.width + radius * 2;
      float expandedDepth = wall.depth + radius * 2;

      float halfWidth = expandedWidth / 2f;
      float halfDepth = expandedDepth / 2f;

      // Check if player is within wall bounds
      if (Math.abs(position.x - wall.position.x) < halfWidth
          && Math.abs(position.z - wall.position.z) < halfDepth
          && position.y < wall.height) {
        return wall;
      }
    }

    return null;
  }

  /**
   * Get the height of the ground at a given X/Z position. Returns 0 (ground level) since we have a
   * flat infinite plane.
   *
   * @param x X position
   * @param z Z position
   * @return Ground height (always 0)
   */
  public float getGroundHeight(float x, float z) {
    return 0f; // Flat infinite plane at Y=0
  }
}
