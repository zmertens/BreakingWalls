package com.flipsandale.game;

import com.flipsandale.dto.MazeResponse;
import com.flipsandale.service.MazeService;
import com.jme3.math.Vector3f;
import org.springframework.stereotype.Service;

/**
 * Converts maze ASCII data from MazeResponse into 3D platform layouts. Extracts horizontal
 * corridors from the maze and creates platforms for jumping.
 */
@Service
public class PlatformGeneratorService {

  private final MazeService mazeService;

  // Platform dimensions (in world units)
  private static final float PLATFORM_WIDTH = 1.5f;
  private static final float PLATFORM_DEPTH = 1.5f;
  private static final float PLATFORM_HEIGHT = 0.3f;
  private static final float PLATFORM_SPACING = 0.2f; // Gap between platforms

  // Vertical spacing between levels (for visual interest)
  private static final float VERTICAL_VARIATION = 0.3f;

  public PlatformGeneratorService(MazeService mazeService) {
    this.mazeService = mazeService;
  }

  /**
   * Generates a platform layout from a MazeResponse.
   *
   * @param mazeResponse The maze response containing ASCII maze data
   * @param levelNumber The level number for tracking
   * @return A PlatformLayout containing all platforms and metadata
   */
  public PlatformLayout generateLayout(MazeResponse mazeResponse, int levelNumber) {
    // Decode the maze ASCII data
    String base64Data = mazeResponse.getData();
    String asciiMaze = mazeService.decodeBase64Data(base64Data);

    // Create layout with metadata
    PlatformLayout layout =
        new PlatformLayout(
            levelNumber,
            mazeResponse.getConfig() != null
                ? (Integer) mazeResponse.getConfig().getOrDefault("seed", 42)
                : 42,
            mazeResponse.getConfig() != null
                ? (String) mazeResponse.getConfig().getOrDefault("algorithm", "sidewinder")
                : "sidewinder");

    // Parse the maze
    String[] lines = asciiMaze.split("\n");
    int mazeHeight = lines.length;
    int mazeWidth = mazeHeight > 0 ? lines[0].length() : 0;

    layout.setMazeHeight(mazeHeight);
    layout.setMazeWidth(mazeWidth);

    // Extract platforms from maze
    extractPlatformsFromMaze(lines, layout);

    // Set start and end positions
    if (layout.getPlatformCount() > 0) {
      layout.setStartPosition(layout.getPlatformAt(0).getPosition());
      layout.setEndPosition(layout.getPlatformAt(layout.getPlatformCount() - 1).getPosition());
    }

    return layout;
  }

  /**
   * Extracts platforms from the ASCII maze by identifying open spaces (corridors). Each open space
   * becomes a platform that the player can land on.
   */
  private void extractPlatformsFromMaze(String[] lines, PlatformLayout layout) {
    int platformId = 0;
    float baseY = 0f;

    // Scan the maze for open spaces (spaces and distance digits)
    for (int z = 0; z < lines.length; z++) {
      String line = lines[z];

      // Track whether we're in a corridor (sequence of spaces or digits)
      int corridorStart = -1;
      Integer corridorDistance = null;

      for (int x = 0; x < line.length(); x++) {
        char c = line.charAt(x);

        // Check if this character represents an open space
        boolean isOpenSpace = c == ' ' || isDistanceDigit(c);

        if (isOpenSpace) {
          // If we're in a corridor, extract distance from digits
          if (Character.isLetterOrDigit(c) && !Character.isWhitespace(c)) {
            int distance = parseBase36(c);
            if (distance >= 0) {
              corridorDistance = distance;
            }
          }

          if (corridorStart == -1) {
            corridorStart = x;
          }
        } else {
          // End of corridor - create platform if corridor was long enough
          if (corridorStart != -1) {
            int corridorLength = x - corridorStart;
            if (corridorLength >= 1) {
              Platform platform =
                  createPlatformFromCorridor(
                      platformId++, corridorStart, x - 1, z, baseY, corridorDistance, lines.length);
              layout.addPlatform(platform);
            }
            corridorStart = -1;
            corridorDistance = null;
          }
        }
      }

      // Handle corridor at end of line
      if (corridorStart != -1) {
        int corridorLength = line.length() - corridorStart;
        if (corridorLength >= 1) {
          Platform platform =
              createPlatformFromCorridor(
                  platformId++,
                  corridorStart,
                  line.length() - 1,
                  z,
                  baseY,
                  corridorDistance,
                  lines.length);
          layout.addPlatform(platform);
        }
      }
    }
  }

  /** Creates a platform from a corridor (sequence of open spaces in the maze). */
  private Platform createPlatformFromCorridor(
      int platformId,
      int startX,
      int endX,
      int z,
      float baseY,
      Integer distanceValue,
      int mazeHeight) {
    // Calculate platform position (center of corridor)
    float centerX = (startX + endX) / 2.0f;
    float centerZ = z;

    // Convert maze coordinates to world coordinates
    float worldX = centerX * (PLATFORM_WIDTH + PLATFORM_SPACING);
    float worldZ = -centerZ * (PLATFORM_DEPTH + PLATFORM_SPACING);

    // Vary platform height based on distance for visual interest
    float worldY = baseY;
    if (distanceValue != null && distanceValue > 0) {
      // Create a gentle wave pattern based on distance
      float normalizedDistance = (float) distanceValue / mazeHeight;
      worldY += Math.sin(normalizedDistance * (float) Math.PI) * VERTICAL_VARIATION;
    }

    Vector3f position = new Vector3f(worldX, worldY, worldZ);

    Platform platform =
        new Platform(
            platformId,
            position,
            PLATFORM_WIDTH,
            PLATFORM_DEPTH,
            PLATFORM_HEIGHT,
            (int) centerX,
            z);

    return platform;
  }

  /** Checks if a character is a base36 distance digit. */
  private boolean isDistanceDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
  }

  /** Parses a base36 character to its integer value. */
  private int parseBase36(char c) {
    if (c >= '0' && c <= '9') {
      return c - '0';
    } else if (c >= 'A' && c <= 'Z') {
      return 10 + (c - 'A');
    } else if (c >= 'a' && c <= 'z') {
      return 10 + (c - 'a');
    }
    return -1;
  }

  /** Gets the platform dimensions (useful for collision detection). */
  public float getPlatformWidth() {
    return PLATFORM_WIDTH;
  }

  public float getPlatformDepth() {
    return PLATFORM_DEPTH;
  }

  public float getPlatformHeight() {
    return PLATFORM_HEIGHT;
  }
}
