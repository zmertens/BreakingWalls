package com.flipsandale.game;

import com.jme3.math.Vector3f;

/**
 * Represents a single platform in the game world. Platforms are the surfaces that players jump
 * between in the maze runner.
 */
public class Platform {
  private int id;
  private Vector3f position; // Center position of the platform
  private float width; // Extends from -width/2 to +width/2 in X
  private float depth; // Extends from -depth/2 to +depth/2 in Z
  private float height; // Platform thickness
  private int mazeX; // Original maze coordinates for reference
  private int mazeZ;

  public Platform(
      int id, Vector3f position, float width, float depth, float height, int mazeX, int mazeZ) {
    this.id = id;
    this.position = position;
    this.width = width;
    this.depth = depth;
    this.height = height;
    this.mazeX = mazeX;
    this.mazeZ = mazeZ;
  }

  // Getters
  public int getId() {
    return id;
  }

  public Vector3f getPosition() {
    return position;
  }

  public void setPosition(Vector3f newPosition) {
    this.position = newPosition;
  }

  public float getWidth() {
    return width;
  }

  public float getDepth() {
    return depth;
  }

  public float getHeight() {
    return height;
  }

  public int getMazeX() {
    return mazeX;
  }

  public int getMazeZ() {
    return mazeZ;
  }

  /** Checks if a point in 3D space is on top of this platform (for landing detection). */
  public boolean isPointAbovePlatform(Vector3f point) {
    float distX = Math.abs(point.x - position.x);
    float distZ = Math.abs(point.z - position.z);
    return distX <= width / 2 && distZ <= depth / 2;
  }

  /** Gets the top surface Y coordinate of this platform. */
  public float getTopSurfaceY() {
    return position.y + height / 2;
  }

  @Override
  public String toString() {
    return String.format(
        "Platform[id=%d, pos=(%.1f,%.1f,%.1f), size=%.1fx%.1f, mazePos=(%d,%d)]",
        id, position.x, position.y, position.z, width, depth, mazeX, mazeZ);
  }
}
