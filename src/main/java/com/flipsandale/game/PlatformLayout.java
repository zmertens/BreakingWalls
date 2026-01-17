package com.flipsandale.game;

import com.jme3.math.Vector3f;
import java.util.ArrayList;
import java.util.List;

/** Holds all platform data for a single level, along with metadata about the level. */
public class PlatformLayout {
  private List<Platform> platforms;
  private Vector3f startPosition;
  private Vector3f endPosition;
  private int levelSeed;
  private int levelNumber;
  private int mazeWidth;
  private int mazeHeight;
  private String algorithmType;

  public PlatformLayout() {
    this.platforms = new ArrayList<>();
  }

  public PlatformLayout(int levelNumber, int levelSeed, String algorithmType) {
    this();
    this.levelNumber = levelNumber;
    this.levelSeed = levelSeed;
    this.algorithmType = algorithmType;
  }

  // Getters and Setters
  public List<Platform> getPlatforms() {
    return platforms;
  }

  public void addPlatform(Platform platform) {
    platforms.add(platform);
  }

  public Vector3f getStartPosition() {
    return startPosition;
  }

  public void setStartPosition(Vector3f startPosition) {
    this.startPosition = startPosition;
  }

  public Vector3f getEndPosition() {
    return endPosition;
  }

  public void setEndPosition(Vector3f endPosition) {
    this.endPosition = endPosition;
  }

  public int getLevelSeed() {
    return levelSeed;
  }

  public void setLevelSeed(int levelSeed) {
    this.levelSeed = levelSeed;
  }

  public int getLevelNumber() {
    return levelNumber;
  }

  public void setLevelNumber(int levelNumber) {
    this.levelNumber = levelNumber;
  }

  public int getMazeWidth() {
    return mazeWidth;
  }

  public void setMazeWidth(int mazeWidth) {
    this.mazeWidth = mazeWidth;
  }

  public int getMazeHeight() {
    return mazeHeight;
  }

  public void setMazeHeight(int mazeHeight) {
    this.mazeHeight = mazeHeight;
  }

  public String getAlgorithmType() {
    return algorithmType;
  }

  public void setAlgorithmType(String algorithmType) {
    this.algorithmType = algorithmType;
  }

  /** Gets a platform by its index in the list. */
  public Platform getPlatformAt(int index) {
    if (index >= 0 && index < platforms.size()) {
      return platforms.get(index);
    }
    return null;
  }

  /** Gets the total number of platforms in this layout. */
  public int getPlatformCount() {
    return platforms.size();
  }

  @Override
  public String toString() {
    return String.format(
        "PlatformLayout[level=%d, seed=%d, platforms=%d, algo=%s, maze=(%dx%d)]",
        levelNumber, levelSeed, platforms.size(), algorithmType, mazeWidth, mazeHeight);
  }
}
