package com.flipsandale.game;

import com.flipsandale.dto.MazeRequest;
import com.flipsandale.dto.MazeResponse;
import com.flipsandale.service.MazeService;
import com.jme3.math.Vector3f;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

/**
 * Manages infinite maze generation by loading/unloading platform chunks as the player moves. Uses a
 * streaming approach where chunks are generated ahead of the player and removed behind them.
 */
public class InfiniteMazeManager {
  private final MazeService mazeService;
  private final PlatformGeneratorService platformGeneratorService;

  // Chunk management
  private static final int CHUNK_SIZE = 20; // Size of each maze chunk
  private static final float LOAD_DISTANCE = 40f; // Distance ahead to generate chunks
  private static final float UNLOAD_DISTANCE = 80f; // Distance behind to unload chunks

  private List<MazeChunk> loadedChunks;
  private int currentChunkX = 0; // Which chunk along X axis
  private int currentChunkZ = 0; // Which chunk along Z axis
  private Vector3f lastPlayerPos;

  /** Represents a generated maze chunk. */
  private static class MazeChunk {
    int chunkX;
    int chunkZ;
    PlatformLayout layout;
    Vector3f origin; // World position origin of this chunk

    MazeChunk(int chunkX, int chunkZ, PlatformLayout layout, Vector3f origin) {
      this.chunkX = chunkX;
      this.chunkZ = chunkZ;
      this.layout = layout;
      this.origin = origin;
    }
  }

  public InfiniteMazeManager(
      MazeService mazeService, PlatformGeneratorService platformGeneratorService) {
    this.mazeService = mazeService;
    this.platformGeneratorService = platformGeneratorService;
    this.loadedChunks = new ArrayList<>();
    this.lastPlayerPos = new Vector3f();
  }

  /**
   * Updates the loaded chunks based on player position. Generates new chunks ahead and unloads
   * chunks behind.
   */
  public void update(Vector3f playerPos, int currentLevel, long seed) {
    // Check if player has moved enough to warrant chunk updates
    if (lastPlayerPos.distance(playerPos) < 10f && !loadedChunks.isEmpty()) {
      return; // No need to update chunks yet
    }

    lastPlayerPos.set(playerPos);

    // Determine which chunk the player is in
    int newChunkX = (int) Math.floor(playerPos.x / (CHUNK_SIZE * 1.7f)); // 1.7 is platform spacing
    int newChunkZ = (int) Math.floor(playerPos.z / (CHUNK_SIZE * 1.7f));

    // Generate chunks around the player if needed
    generateChunksAround(newChunkX, newChunkZ, currentLevel, seed);

    // Unload chunks that are too far away
    unloadDistantChunks(playerPos);

    currentChunkX = newChunkX;
    currentChunkZ = newChunkZ;
  }

  /** Generates chunks around the player's current position. */
  private void generateChunksAround(int centerChunkX, int centerChunkZ, int level, long seed) {
    // Generate a 3x3 grid of chunks around the player
    for (int dx = -1; dx <= 1; dx++) {
      for (int dz = -1; dz <= 1; dz++) {
        int chunkX = centerChunkX + dx;
        int chunkZ = centerChunkZ + dz;

        // Check if chunk is already loaded
        if (getChunk(chunkX, chunkZ) == null) {
          generateChunk(chunkX, chunkZ, level, seed);
        }
      }
    }
  }

  /** Generates a single maze chunk. */
  private void generateChunk(int chunkX, int chunkZ, int level, long seed) {
    try {
      // Generate unique seed for this chunk
      long chunkSeed = seed + chunkX * 73856093L ^ chunkZ * 19349663L;

      // Create maze request for this chunk
      MazeRequest request = new MazeRequest();
      request.setAlgo("sidewinder");
      request.setRows(CHUNK_SIZE);
      request.setColumns(CHUNK_SIZE);
      request.setSeed((int) (chunkSeed & 0x7FFFFFFF));

      // Generate maze
      MazeResponse response = mazeService.createMaze(request);

      if (response != null && response.getData() != null) {
        // Generate platforms from maze
        PlatformLayout layout = platformGeneratorService.generateLayout(response, level);

        // Calculate chunk origin in world space
        float worldX = chunkX * CHUNK_SIZE * 1.7f; // 1.7 is platform spacing
        float worldZ = chunkZ * CHUNK_SIZE * 1.7f;
        Vector3f origin = new Vector3f(worldX, 0, worldZ);

        // Offset all platforms in this chunk to their world position
        offsetPlatformsToChunk(layout, origin);

        // Create and add chunk
        MazeChunk chunk = new MazeChunk(chunkX, chunkZ, layout, origin);
        loadedChunks.add(chunk);

        System.out.println(
            "Generated chunk ("
                + chunkX
                + ", "
                + chunkZ
                + ") with "
                + layout.getPlatforms().size()
                + " platforms");
      }
    } catch (Exception e) {
      System.err.println("Failed to generate chunk (" + chunkX + ", " + chunkZ + "): " + e);
    }
  }

  /** Offsets all platforms in a layout to their chunk world position. */
  private void offsetPlatformsToChunk(PlatformLayout layout, Vector3f chunkOrigin) {
    for (Platform platform : layout.getPlatforms()) {
      Vector3f pos = platform.getPosition();
      pos.addLocal(chunkOrigin);
      platform.setPosition(pos);
    }
  }

  /** Unloads chunks that are too far from the player. */
  private void unloadDistantChunks(Vector3f playerPos) {
    Iterator<MazeChunk> iterator = loadedChunks.iterator();
    while (iterator.hasNext()) {
      MazeChunk chunk = iterator.next();
      float distance = chunk.origin.distance(playerPos);

      if (distance > UNLOAD_DISTANCE) {
        System.out.println("Unloaded chunk (" + chunk.chunkX + ", " + chunk.chunkZ + ")");
        iterator.remove();
      }
    }
  }

  /** Gets a chunk at the specified coordinates, or null if not loaded. */
  private MazeChunk getChunk(int chunkX, int chunkZ) {
    for (MazeChunk chunk : loadedChunks) {
      if (chunk.chunkX == chunkX && chunk.chunkZ == chunkZ) {
        return chunk;
      }
    }
    return null;
  }

  /** Gets all platforms from all loaded chunks. */
  public List<Platform> getAllPlatforms() {
    List<Platform> allPlatforms = new ArrayList<>();
    for (MazeChunk chunk : loadedChunks) {
      allPlatforms.addAll(chunk.layout.getPlatforms());
    }
    return allPlatforms;
  }

  /** Gets the combined platform layout from all loaded chunks. */
  public PlatformLayout getCombinedLayout() {
    PlatformLayout combined = new PlatformLayout();
    for (Platform platform : getAllPlatforms()) {
      combined.addPlatform(platform);
    }
    return combined;
  }
}
