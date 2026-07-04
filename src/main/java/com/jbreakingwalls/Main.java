package com.jbreakingwalls;

import com.jbreakingwalls.config.GameConfig;

/**
 * Entry point — mirrors Main.cpp from the C++ source.
 *
 * Accepts an optional first argument that overrides the default config file
 * path ("config.json"). The singleton {@link JBreakingWalls} takes ownership
 * of the window and the full game loop from here on.
 */
public final class Main {

    private Main() {}

    public static void main(String[] args) {
        String configPath = (args.length > 0) ? args[0] : "config.json";

        GameConfig config = GameConfig.fromFile(configPath);

        JBreakingWalls game = JBreakingWalls.create(
                config.windowTitle,
                config.windowWidth,
                config.windowHeight,
                config
        );

        try {
            game.run();
        } finally {
            game.dispose();
        }
    }
}
