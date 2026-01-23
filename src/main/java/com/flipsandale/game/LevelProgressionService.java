package com.flipsandale.game;

import com.flipsandale.service.CornersService;
import org.springframework.stereotype.Service;

/**
 * Service for managing level progression and infinite level generation. Handles fetching new mazes
 * from the REST API and preloading next levels.
 */
@Service
public class LevelProgressionService {

  private final CornersService CornersService;

  // Level generation parameters
  private static final String ALGORITHM = "sidewinder";
  private static final int MAZE_WIDTH = 20;
  private static final int MAZE_HEIGHT = 20;

  // Cache for next level to enable seamless transitions
  private LevelData currentLevelCache;
  private LevelData nextLevelCache;

  public LevelProgressionService(CornersService CornersService) {
    this.CornersService = CornersService;
  }

  /** Data class holding maze and platform layout for a level. */
  public static class LevelData {
    private int levelNumber;
    private int seed;
    private com.flipsandale.dto.MazeResponse mazeResponse;
    private PlatformLayout platformLayout;

    public LevelData(int levelNumber, int seed, com.flipsandale.dto.MazeResponse mazeResponse) {
      this.levelNumber = levelNumber;
      this.seed = seed;
      this.mazeResponse = mazeResponse;
    }

    public int getLevelNumber() {
      return levelNumber;
    }

    public int getSeed() {
      return seed;
    }

    public com.flipsandale.dto.MazeResponse getMazeResponse() {
      return mazeResponse;
    }

    public PlatformLayout getPlatformLayout() {
      return platformLayout;
    }

    public void setPlatformLayout(PlatformLayout platformLayout) {
      this.platformLayout = platformLayout;
    }

    @Override
    public String toString() {
      return String.format("LevelData[level=%d, seed=%d]", levelNumber, seed);
    }
  }

  /**
   * Generates a new level with the specified seed.
   *
   * @param levelNumber The level number (used for seed calculation)
   * @return A LevelData object containing the maze and platform layout
   */
  public LevelData generateLevel(int levelNumber) {
    try {
      int seed = calculateSeed(levelNumber);
      com.flipsandale.dto.MazeRequest request =
          new com.flipsandale.dto.MazeRequest(ALGORITHM, MAZE_HEIGHT, MAZE_WIDTH, seed, true);

      com.flipsandale.dto.MazeResponse mazeResponse = CornersService.createMaze(request);

      if (mazeResponse == null || mazeResponse.getData() == null) {
        System.err.println("Failed to generate maze for level " + levelNumber);
        return null;
      }

      LevelData levelData = new LevelData(levelNumber, seed, mazeResponse);
      System.out.println("Generated level: " + levelData);
      return levelData;

    } catch (Exception e) {
      System.err.println("Error generating level " + levelNumber + ": " + e.getMessage());
      e.printStackTrace();
      return null;
    }
  }

  /** Calculates the seed for a given level number to ensure deterministic maze generation. */
  private int calculateSeed(int levelNumber) {
    // Simple formula: each level gets a unique seed
    return 42 + (levelNumber - 1) * 1000;
  }

  /** Loads the next level asynchronously and caches it. */
  public void preloadNextLevel(int currentLevelNumber) {
    Thread preloadThread =
        new Thread(
            () -> {
              try {
                nextLevelCache = generateLevel(currentLevelNumber + 1);
              } catch (Exception e) {
                System.err.println("Error preloading level: " + e.getMessage());
              }
            });
    preloadThread.setDaemon(true);
    preloadThread.setName("LevelPreloader");
    preloadThread.start();
  }

  /** Gets the cached next level and immediately preloads the level after that. */
  public LevelData getAndCacheNextLevel(int nextLevelNumber) {
    if (nextLevelCache != null && nextLevelCache.getLevelNumber() == nextLevelNumber) {
      LevelData result = nextLevelCache;
      nextLevelCache = null; // Clear cache after use
      preloadNextLevel(nextLevelNumber); // Preload the level after next
      return result;
    }
    // Fallback: generate if not cached
    LevelData result = generateLevel(nextLevelNumber);
    preloadNextLevel(nextLevelNumber); // Preload next level
    return result;
  }
}
