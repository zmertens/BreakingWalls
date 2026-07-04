package com.jbreakingwalls.config;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.annotations.SerializedName;

import java.io.IOException;
import java.io.Reader;
import java.nio.file.Files;
import java.nio.file.Path;

/**
 * All runtime parameters for jbreaking-walls, loaded from {@code config.json}.
 * Gson ignores unknown JSON keys so old runner-era config.json files load cleanly.
 */
public final class GameConfig {

    // Window
    @SerializedName("window_title")
    public String windowTitle = "JBreaking Walls";

    @SerializedName("window_width")
    public int windowWidth = 1280;

    @SerializedName("window_height")
    public int windowHeight = 720;

    @SerializedName("target_fps")
    public int targetFps = 60;

    @SerializedName("show_debug_overlay")
    public boolean showDebugOverlay = false;

    // Maze generation (iteration 1)

    /** Starting maze dimension (rows = cols). Minimum useful value: 4. */
    @SerializedName("maze_start_size")
    public int mazeStartSize = 5;

    /** Row and col growth per completed level. 0 = fixed-size challenge mode. */
    @SerializedName("maze_growth_per_level")
    public int mazeGrowthPerLevel = 1;

    /** Pre-generated queue depth; Java 21 virtual threads fill this in the background. */
    @SerializedName("maze_pool_size")
    public int mazePoolSize = 4;

    /** RNG seed: 0 = random per run; any other value = reproducible maze sequence. */
    @SerializedName("randomness_seed")
    public long randomnessSeed = 0L;

    // Difficulty: wall obstacles (iteration 2)

    /**
     * Percentage of path cells that become walled each level.
     * Level 1 = 0% (pure drag-reveal); level 2 = wallDensityGrowth%; etc.
     */
    @SerializedName("wall_density_growth")
    public int wallDensityGrowth = 5;

    // Rendering

    @SerializedName("shader_quad_vert")
    public String shaderQuadVert = "shaders/quad.vert.glsl";

    @SerializedName("shader_quad_frag")
    public String shaderQuadFrag = "shaders/quad.frag.glsl";

    // Factory

    /**
     * Load config from a JSON file on the filesystem.
     * Falls back to built-in defaults when the file is absent or unreadable.
     */
    public static GameConfig fromFile(String path) {
        Gson gson = new GsonBuilder().create();
        Path p = Path.of(path);
        if (Files.exists(p)) {
            try (Reader r = Files.newBufferedReader(p)) {
                GameConfig cfg = gson.fromJson(r, GameConfig.class);
                if (cfg != null) {
                    System.out.printf("[Config] Loaded '%s'%n", path);
                    return cfg;
                }
            } catch (IOException e) {
                System.err.printf("[Config] Failed to read '%s': %s%n", path, e.getMessage());
            }
        } else {
            System.out.printf("[Config] '%s' not found - using built-in defaults.%n", path);
        }
        return new GameConfig();
    }
}
