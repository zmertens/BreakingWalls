package com.flipsandale.game;

import com.flipsandale.service.MazeService;
import com.jme3.system.AppSettings;
import org.springframework.boot.SpringApplication;
import org.springframework.context.ApplicationContext;
import org.springframework.stereotype.Component;

@Component
public class GameLauncher {

  private final MazeService mazeService;
  private final ApplicationContext applicationContext;
  private MazeGameApp gameApp;

  public GameLauncher(MazeService mazeService, ApplicationContext applicationContext) {
    this.mazeService = mazeService;
    this.applicationContext = applicationContext;
  }

  /**
   * Launches the JMonkeyEngine game window. Call this from a controller endpoint or command-line
   * runner.
   */
  public void launchGame() {
    if (gameApp != null && gameApp.getContext() != null) {
      System.out.println("Game is already running!");
      return;
    }

    gameApp = new MazeGameApp(mazeService, this::shutdownSpring);

    AppSettings settings = MazeGameApp.createSettings();
    gameApp.setSettings(settings);
    gameApp.setShowSettings(false); // Skip settings dialog

    // Start the game (this will run on its own thread internally)
    gameApp.start();
  }

  public void stopGame() {
    if (gameApp != null) {
      gameApp.stop();
      gameApp = null;
    }
  }

  private void shutdownSpring() {
    System.out.println("Game closed, shutting down Spring Boot...");
    SpringApplication.exit(applicationContext, () -> 0);
    System.exit(0);
  }
}
