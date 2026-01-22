package com.flipsandale.game.state;

import com.flipsandale.game.AudioManager;
import com.flipsandale.game.GameMode;
import com.flipsandale.game.GameStateService;
import com.flipsandale.game.HUDLayer;
import com.flipsandale.game.InputManager;
import com.flipsandale.game.LevelProgressionService;
import com.flipsandale.game.MaterialManager;
import com.flipsandale.game.MazeWallService;
import com.flipsandale.game.PlatformLayout;
import com.flipsandale.game.PlayerController;
import com.jme3.asset.AssetManager;
import com.jme3.font.BitmapFont;
import com.jme3.renderer.Camera;
import com.jme3.scene.Node;
import de.lessvoid.nifty.Nifty;
import java.util.List;

/**
 * Context object containing all shared "game services" that states need access to. Passed to each
 * state on creation, enabling dependency injection and decoupling states from MazeGameApp. Also
 * contains functional callbacks for state transitions and game events.
 */
public class StateContext {

  // JME3 References
  public final AssetManager assetManager;
  public final Node rootNode;
  public final Node guiNode;
  public final Camera camera;
  public final BitmapFont guiFont;

  // Game Services
  public final MaterialManager materialManager;
  public final AudioManager audioManager;
  public final InputManager inputManager;
  public final GameStateService gameStateService;
  public final LevelProgressionService levelProgressionService;
  public final MazeWallService mazeWallService;

  // UI References
  public final Nifty nifty;
  public final HUDLayer hudLayer;

  // Game State
  public PlayerController playerController;
  public PlatformLayout currentPlatformLayout;
  public List<MazeWallService.Wall> currentWalls;
  public GameMode currentGameMode;

  // Player visual and camera settings
  public boolean useThirdPersonView = true;

  // Callbacks for state transitions and events
  public final Runnable onLevelComplete;
  public final Runnable onPlayerFall;
  public final Runnable onGameOver;

  public StateContext(
      AssetManager assetManager,
      Node rootNode,
      Node guiNode,
      Camera camera,
      BitmapFont guiFont,
      MaterialManager materialManager,
      AudioManager audioManager,
      InputManager inputManager,
      GameStateService gameStateService,
      LevelProgressionService levelProgressionService,
      MazeWallService mazeWallService,
      Nifty nifty,
      HUDLayer hudLayer,
      Runnable onLevelComplete,
      Runnable onPlayerFall,
      Runnable onGameOver) {
    this.assetManager = assetManager;
    this.rootNode = rootNode;
    this.guiNode = guiNode;
    this.camera = camera;
    this.guiFont = guiFont;
    this.materialManager = materialManager;
    this.audioManager = audioManager;
    this.inputManager = inputManager;
    this.gameStateService = gameStateService;
    this.levelProgressionService = levelProgressionService;
    this.mazeWallService = mazeWallService;
    this.nifty = nifty;
    this.hudLayer = hudLayer;
    this.onLevelComplete = onLevelComplete;
    this.onPlayerFall = onPlayerFall;
    this.onGameOver = onGameOver;
    this.currentWalls = new java.util.ArrayList<>();
  }
}
