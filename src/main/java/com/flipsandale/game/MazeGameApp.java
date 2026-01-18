package com.flipsandale.game;

import com.flipsandale.dto.MazeRequest;
import com.flipsandale.dto.MazeResponse;
import com.flipsandale.service.MazeService;
import com.jme3.app.SimpleApplication;
import com.jme3.input.KeyInput;
import com.jme3.input.controls.ActionListener;
import com.jme3.input.controls.KeyTrigger;
import com.jme3.material.Material;
import com.jme3.math.ColorRGBA;
import com.jme3.math.Vector3f;
import com.jme3.niftygui.NiftyJmeDisplay;
import com.jme3.scene.Geometry;
import com.jme3.scene.Node;
import com.jme3.scene.shape.Box;
import com.jme3.system.AppSettings;
import de.lessvoid.nifty.Nifty;
import de.lessvoid.nifty.builder.LayerBuilder;
import de.lessvoid.nifty.builder.PanelBuilder;
import de.lessvoid.nifty.builder.ScreenBuilder;
import de.lessvoid.nifty.builder.TextBuilder;
import de.lessvoid.nifty.controls.button.builder.ButtonBuilder;
import de.lessvoid.nifty.controls.label.builder.LabelBuilder;
import de.lessvoid.nifty.screen.Screen;
import de.lessvoid.nifty.screen.ScreenController;
import java.util.Base64;
import java.util.logging.Level;
import java.util.logging.Logger;
import org.apache.logging.log4j.util.Strings;

public class MazeGameApp extends SimpleApplication implements ScreenController {

  private final MazeService mazeService;
  private final HapticFeedbackService hapticFeedbackService;
  private final PlatformGeneratorService platformGeneratorService;
  private final GameStateService gameStateService;
  private final LevelProgressionService levelProgressionService;
  private final Runnable onCloseCallback;
  private String asciiMaze;
  private Node mazeNode;
  private MaterialManager materialManager; // Initialized in simpleInitApp
  private AudioManager audioManager; // Initialized in simpleInitApp

  // Game state
  private GameState currentGameState = GameState.MENU;
  private GameMode currentGameMode = GameMode.ZEN;

  // Platform and player state
  private PlatformLayout currentPlatformLayout;
  private PlayerController playerController;
  private Node platformsNode; // Visual representation of platforms
  private FallingStateManager fallingStateManager;
  private MazeWallService mazeWallService;
  private java.util.List<MazeWallService.Wall> currentWalls;

  // Level progression
  private LevelProgressionService.LevelData nextLevelData; // Preloaded next level

  // Input management
  private InputManager gameInputManager;
  private boolean jumpPressed = false;
  private float cameraRotation = 0f; // Horizontal rotation in radians
  private static final float CAMERA_ROTATION_SPEED = 2f; // Radians per second

  // Nifty GUI
  private Nifty nifty;
  private NiftyJmeDisplay niftyDisplay;
  private NiftyInputProtector niftyInputProtector;
  private volatile boolean isTransitioningScreen = false;
  private boolean menuVisible = true;

  // UI System
  private HUDManager hudManager;
  private GameUIManager gameUIManager;
  private GameplayHUDScreenController gameplayHUDScreenController;
  private HUDLayer hudLayer;
  private GameCountdownManager countdownManager;

  // Game flow
  private volatile boolean gameStarted = false;
  private boolean showRestartOption = false;

  // Maze settings
  private String selectedAlgorithm = "sidewinder";
  private int mazeRows = 20;
  private int mazeCols = 20;
  private int mazeSeed = 42;
  private boolean showDistances = true;

  private static final float CELL_SIZE = 1.0f;
  private static final float WALL_HEIGHT = 2.0f;

  public MazeGameApp(
      MazeService mazeService,
      HapticFeedbackService hapticFeedbackService,
      PlatformGeneratorService platformGeneratorService,
      GameStateService gameStateService,
      LevelProgressionService levelProgressionService,
      Runnable onCloseCallback) {
    this.mazeService = mazeService;
    this.hapticFeedbackService = hapticFeedbackService;
    this.platformGeneratorService = platformGeneratorService;
    this.gameStateService = gameStateService;
    this.levelProgressionService = levelProgressionService;
    this.onCloseCallback = onCloseCallback;
  }

  public static AppSettings createSettings() {
    AppSettings settings = new AppSettings(true);
    settings.setTitle("Maze Game");
    settings.setWidth(1280);
    settings.setHeight(720);
    settings.setVSync(true);
    return settings;
  }

  @Override
  public void simpleInitApp() {
    // Suppress Nifty logging
    Logger.getLogger("de.lessvoid.nifty").setLevel(Level.SEVERE);
    Logger.getLogger("NiftyInputEventHandlingLog").setLevel(Level.SEVERE);

    // Create platforms node for game
    platformsNode = new Node("PlatformsNode");
    rootNode.attachChild(platformsNode);

    // Initialize input management
    gameInputManager = new InputManager(inputManager);
    gameInputManager.setupInputMappings();
    gameInputManager.setListener(new GameInputListener());

    // Initialize material manager now that assetManager is available
    materialManager = new MaterialManager(assetManager);

    // Initialize audio manager now that assetManager is available
    audioManager = new AudioManager(assetManager);
    audioManager.initialize();
    audioManager.playLoadingMusicLoop(rootNode);

    // Initialize maze wall service for obstacle-course gameplay
    mazeWallService = new MazeWallService();
    currentWalls = new java.util.ArrayList<>();

    // Set up lighting and shadows
    materialManager.setupLighting(rootNode);
    materialManager.enableShadows(viewPort, rootNode, 2048);

    // Set up camera
    setupCamera();

    // Add a floor
    addFloor();

    // Initialize Nifty GUI
    initNiftyGui();

    // Set initial game state to MENU
    setGameState(GameState.MENU);
    flyCam.setDragToRotate(true);
    inputManager.setCursorVisible(true);
  }

  private void initNiftyGui() {
    // Use SafeNiftyJmeDisplay instead of regular NiftyJmeDisplay
    // This catches NullPointerException during screen transitions
    niftyDisplay =
        SafeNiftyJmeDisplay.newNiftyJmeDisplay(
            assetManager, inputManager, audioRenderer, guiViewPort);
    nifty = niftyDisplay.getNifty();

    // Create and add a protective input listener
    niftyInputProtector = new NiftyInputProtector(nifty);
    inputManager.addRawInputListener(niftyInputProtector);

    // Load default styles and controls
    nifty.loadStyleFile("nifty-default-styles.xml");
    nifty.loadControlFile("nifty-default-controls.xml");

    // Build empty screen (for gameplay)
    buildEmptyScreen();

    // Build main menu screen
    buildMainMenuScreen();

    // Build settings screen
    buildSettingsScreen();

    // Initialize HUD system
    initializeHUDSystem();

    // Patch Nifty to handle null screen gracefully
    // This wraps Nifty.update() to catch NullPointerException during input handling
    patchNiftyForNullScreen();

    // Add processor after all initialization is complete
    guiViewPort.addProcessor(niftyDisplay);

    // Show main menu safely - now processor is added and initialization is complete
    safeGotoScreen("mainMenu");
  }

  /**
   * Patches Nifty to handle NullPointerException that occurs when getCurrentScreen() is null during
   * screen transitions. This is done by wrapping the update method.
   */
  private void patchNiftyForNullScreen() {
    // We need to override how Nifty handles input updates
    // Since we can't directly patch Nifty, we'll prevent the issue by monitoring screen state
    // The key insight: the error occurs in InputSystemJme.endInput() when it calls Nifty.update()
    // We can prevent this by disabling input processing during transitions

    // Unfortunately, without direct access to InputSystemJme, we can only suppress input
    // The NiftyInputProtector will consume mouse events as a preventative measure
  }

  private void initializeHUDSystem() {
    // Create HUDManager to track game metrics
    hudManager = new HUDManager();

    // Create GameUIManager to coordinate UI transitions
    gameUIManager = new GameUIManager(nifty, hudManager, gameStateService);

    // Create GameplayHUDScreenController for in-game HUD
    gameplayHUDScreenController = new GameplayHUDScreenController(hudManager, gameStateService);

    // Create HUDLayer for JME3-based HUD rendering
    hudLayer = new HUDLayer(guiNode, guiFont, hudManager);

    // Create countdown manager for level start
    countdownManager = new GameCountdownManager(guiNode, guiFont);

    System.out.println("HUD System initialized");
  }

  /** Safely transitions to a screen, checking if it exists first. */
  private void safeGotoScreen(String screenName) {
    try {
      if (nifty == null) {
        System.err.println("Nifty not initialized, cannot transition to screen: " + screenName);
        return;
      }

      de.lessvoid.nifty.screen.Screen screen = nifty.getScreen(screenName);
      if (screen == null) {
        System.err.println("Screen '" + screenName + "' not found in Nifty");
        // Always ensure we have at least the empty screen as fallback
        if (!"empty".equals(screenName)) {
          System.err.println("Falling back to 'empty' screen");
          de.lessvoid.nifty.screen.Screen emptyScreen = nifty.getScreen("empty");
          if (emptyScreen != null) {
            // Remove processor before transition to prevent input processing on null screen
            try {
              guiViewPort.removeProcessor(niftyDisplay);
            } catch (Exception ignored) {
            }
            nifty.gotoScreen("empty");
            // Re-add processor after transition
            try {
              guiViewPort.addProcessor(niftyDisplay);
            } catch (Exception ignored) {
            }
            return;
          }
        }
        // Last resort: don't transition if screen doesn't exist
        System.err.println(
            "WARNING: Requested screen '"
                + screenName
                + "' does not exist and no fallback available");
        return;
      }

      // Remove processor before transition to prevent input processing on null screen
      try {
        guiViewPort.removeProcessor(niftyDisplay);
      } catch (Exception ignored) {
      }

      nifty.gotoScreen(screenName);

      // Re-add processor after transition
      try {
        guiViewPort.addProcessor(niftyDisplay);
      } catch (Exception ignored) {
      }
    } catch (Exception e) {
      System.err.println("Error transitioning to screen '" + screenName + "': " + e.getMessage());
      e.printStackTrace();
    }
  }

  private void buildEmptyScreen() {
    nifty.addScreen(
        "empty",
        new ScreenBuilder("empty") {
          {
            controller(MazeGameApp.this);
            layer(
                new LayerBuilder("empty") {
                  {
                    childLayoutCenter();
                  }
                });
          }
        }.build(nifty));
  }

  private void buildMainMenuScreen() {
    nifty.addScreen(
        "mainMenu",
        new ScreenBuilder("mainMenu") {
          {
            controller(MazeGameApp.this);

            layer(
                new LayerBuilder("background") {
                  {
                    childLayoutCenter();
                    backgroundColor("#000a");
                  }
                });

            layer(
                new LayerBuilder("foreground") {
                  {
                    childLayoutVertical();

                    // Title panel
                    panel(
                        new PanelBuilder("titlePanel") {
                          {
                            childLayoutCenter();
                            alignCenter();
                            height("20%");
                            width("100%");
                            paddingTop("50px");

                            text(
                                new TextBuilder() {
                                  {
                                    text("MAZE GAME");
                                    font("Interface/Fonts/Default.fnt");
                                    color("#0ff");
                                    height("100%");
                                    width("100%");
                                  }
                                });
                          }
                        });

                    // Spacer
                    panel(
                        new PanelBuilder("spacer1") {
                          {
                            height("15%");
                            width("100%");
                          }
                        });

                    // Menu buttons panel
                    panel(
                        new PanelBuilder("menuPanel") {
                          {
                            childLayoutVertical();
                            alignCenter();
                            height("50%");
                            width("30%");

                            // Generate New Maze button
                            control(
                                new ButtonBuilder("settingsBtn", "Generate New Maze") {
                                  {
                                    alignCenter();
                                    height("40px");
                                    width("100%");
                                    interactOnClick("showSettings()");
                                  }
                                });

                            // Quick Generate button
                            control(
                                new ButtonBuilder("quickGenBtn", "Quick Generate") {
                                  {
                                    alignCenter();
                                    height("40px");
                                    width("100%");
                                    interactOnClick("quickGenerate()");
                                  }
                                });

                            // Play button
                            control(
                                new ButtonBuilder("playBtn", "Continue Playing") {
                                  {
                                    alignCenter();
                                    height("40px");
                                    width("100%");
                                    interactOnClick("play()");
                                  }
                                });

                            // Exit button
                            control(
                                new ButtonBuilder("exitBtn", "Exit Game") {
                                  {
                                    alignCenter();
                                    height("40px");
                                    width("100%");
                                    interactOnClick("exitGame()");
                                  }
                                });
                          }
                        });

                    // Info panel
                    panel(
                        new PanelBuilder("infoPanel") {
                          {
                            childLayoutCenter();
                            alignCenter();
                            height("15%");
                            width("100%");

                            text(
                                new TextBuilder() {
                                  {
                                    text("Press ESC to toggle menu | WASD to move | Mouse to look");
                                    font("Interface/Fonts/Default.fnt");
                                    color("#888");
                                  }
                                });
                          }
                        });
                  }
                });
          }
        }.build(nifty));
  }

  private void buildSettingsScreen() {
    nifty.addScreen(
        "settings",
        new ScreenBuilder("settings") {
          {
            controller(MazeGameApp.this);

            layer(
                new LayerBuilder("background") {
                  {
                    childLayoutCenter();
                    backgroundColor("#000a");
                  }
                });

            layer(
                new LayerBuilder("foreground") {
                  {
                    childLayoutVertical();

                    // Title
                    panel(
                        new PanelBuilder("titlePanel") {
                          {
                            childLayoutCenter();
                            alignCenter();
                            height("15%");
                            width("100%");
                            paddingTop("30px");

                            text(
                                new TextBuilder() {
                                  {
                                    text("MAZE SETTINGS");
                                    font("Interface/Fonts/Default.fnt");
                                    color("#ff0");
                                  }
                                });
                          }
                        });

                    // Settings panel
                    panel(
                        new PanelBuilder("settingsPanel") {
                          {
                            childLayoutVertical();
                            alignCenter();
                            height("60%");
                            width("60%");

                            // Algorithm row
                            panel(
                                new PanelBuilder("algoRow") {
                                  {
                                    childLayoutHorizontal();
                                    height("50px");
                                    width("100%");

                                    control(
                                        new LabelBuilder("algoLabel") {
                                          {
                                            text("Algorithm: ");
                                            color("#fff");
                                            width("25%");
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("algoSidewinder", "Sidewinder") {
                                          {
                                            width("25%");
                                            interactOnClick("setAlgorithm(sidewinder)");
                                          }
                                        });
                                    control(
                                        new ButtonBuilder("algoBinarytree", "BinaryTree") {
                                          {
                                            width("25%");
                                            interactOnClick("setAlgorithm(binarytree)");
                                          }
                                        });
                                    control(
                                        new ButtonBuilder("algoAldous", "Aldous-Broder") {
                                          {
                                            width("25%");
                                            interactOnClick("setAlgorithm(aldousbroder)");
                                          }
                                        });
                                  }
                                });

                            // Rows row
                            panel(
                                new PanelBuilder("rowsRow") {
                                  {
                                    childLayoutHorizontal();
                                    height("50px");
                                    width("100%");

                                    control(
                                        new LabelBuilder("rowsTextLabel") {
                                          {
                                            text("Rows: ");
                                            color("#fff");
                                            width("25%");
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("rowsDown", " - ") {
                                          {
                                            width("15%");
                                            interactOnClick("adjustRows(-5)");
                                          }
                                        });

                                    control(
                                        new LabelBuilder("rowsLabel") {
                                          {
                                            text("20");
                                            color("#0ff");
                                            width("20%");
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("rowsUp", " + ") {
                                          {
                                            width("15%");
                                            interactOnClick("adjustRows(5)");
                                          }
                                        });
                                  }
                                });

                            // Columns row
                            panel(
                                new PanelBuilder("colsRow") {
                                  {
                                    childLayoutHorizontal();
                                    height("50px");
                                    width("100%");

                                    control(
                                        new LabelBuilder("colsTextLabel") {
                                          {
                                            text("Columns: ");
                                            color("#fff");
                                            width("25%");
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("colsDown", " - ") {
                                          {
                                            width("15%");
                                            interactOnClick("adjustCols(-5)");
                                          }
                                        });

                                    control(
                                        new LabelBuilder("colsLabel") {
                                          {
                                            text("20");
                                            color("#0ff");
                                            width("20%");
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("colsUp", " + ") {
                                          {
                                            width("15%");
                                            interactOnClick("adjustCols(5)");
                                          }
                                        });
                                  }
                                });

                            // Seed row
                            panel(
                                new PanelBuilder("seedRow") {
                                  {
                                    childLayoutHorizontal();
                                    height("50px");
                                    width("100%");

                                    control(
                                        new LabelBuilder("seedTextLabel") {
                                          {
                                            text("Seed: ");
                                            color("#fff");
                                            width("25%");
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("seedDown", " - ") {
                                          {
                                            width("10%");
                                            interactOnClick("adjustSeed(-1)");
                                          }
                                        });

                                    control(
                                        new LabelBuilder("seedLabel") {
                                          {
                                            text("42");
                                            color("#0ff");
                                            width("15%");
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("seedUp", " + ") {
                                          {
                                            width("10%");
                                            interactOnClick("adjustSeed(1)");
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("seedRandom", "Random") {
                                          {
                                            width("20%");
                                            interactOnClick("randomSeed()");
                                          }
                                        });
                                  }
                                });

                            // Distances toggle
                            panel(
                                new PanelBuilder("distRow") {
                                  {
                                    childLayoutHorizontal();
                                    height("50px");
                                    width("100%");

                                    control(
                                        new LabelBuilder("distTextLabel") {
                                          {
                                            text("Show Distances: ");
                                            color("#fff");
                                            width("25%");
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("distToggle", "ON") {
                                          {
                                            width("20%");
                                            interactOnClick("toggleDistances()");
                                          }
                                        });
                                  }
                                });
                          }
                        });

                    // Action buttons
                    panel(
                        new PanelBuilder("actionPanel") {
                          {
                            childLayoutHorizontal();
                            alignCenter();
                            height("15%");
                            width("50%");

                            control(
                                new ButtonBuilder("generateBtn", "Generate & Play") {
                                  {
                                    width("45%");
                                    height("50px");
                                    interactOnClick("generateAndPlay()");
                                  }
                                });

                            control(
                                new ButtonBuilder("backBtn", "Back") {
                                  {
                                    width("45%");
                                    height("50px");
                                    interactOnClick("backToMenu()");
                                  }
                                });
                          }
                        });
                  }
                });
          }
        }.build(nifty));
  }

  private void setupInputs() {
    // Legacy input mapping - kept for compatibility with menu navigation
    inputManager.addMapping("ToggleMenu", new KeyTrigger(KeyInput.KEY_ESCAPE));
    inputManager.addListener(
        (ActionListener)
            (name, isPressed, tpf) -> {
              if (name.equals("ToggleMenu") && !isPressed) {
                toggleMenu();
              }
            },
        "ToggleMenu");
  }

  // ============== Game State Management ==============

  /** Sets the current game state and triggers any state-specific initialization. */
  private void setGameState(GameState newState) {
    GameState previousState = currentGameState;
    currentGameState = newState;

    System.out.println("Game State: " + previousState + " -> " + newState);

    switch (newState) {
      case MENU:
        onEnterMenuState();
        break;
      case PLAYING:
        onEnterPlayingState();
        break;
      case FALLING:
        onEnterFallingState();
        break;
      case PAUSED:
        onEnterPausedState();
        break;
      case GAME_OVER:
        onEnterGameOverState();
        break;
    }
  }

  private void onEnterMenuState() {
    menuVisible = true;

    // Re-enable Nifty GUI processing when returning to menu
    if (niftyDisplay != null) {
      try {
        guiViewPort.addProcessor(niftyDisplay);
      } catch (Exception e) {
        System.err.println("Note: Could not re-add Nifty processor to viewport");
      }
      gameUIManager.showMainMenu();
    } else {
      safeGotoScreen("mainMenu");
    }
    flyCam.setDragToRotate(true);
    inputManager.setCursorVisible(true);
  }

  private void onEnterPlayingState() {
    menuVisible = false;

    // IMPORTANT: Disable Nifty input processing BEFORE transitioning screens
    // This prevents null pointer exceptions from mouse input during transitions
    // We do this by removing the NiftyJmeDisplay processor before any screen changes
    if (niftyDisplay != null) {
      try {
        guiViewPort.removeProcessor(niftyDisplay);
      } catch (Exception e) {
        System.err.println("Note: Could not remove Nifty processor from viewport");
      }
    }

    // Now safe to transition screens (no Nifty input processing happening)
    if (gameUIManager != null) {
      gameUIManager.showGameplayHUD();
    } else {
      safeGotoScreen("empty");
    }

    flyCam.setDragToRotate(false);
    inputManager.setCursorVisible(false);
    jumpPressed = false;

    // Initialize level with current maze
    initializeLevel();
  }

  private void onEnterFallingState() {
    // Initialize falling state manager
    Vector3f levelStartPos =
        currentPlatformLayout != null
            ? currentPlatformLayout.getStartPosition()
            : (playerController != null ? playerController.getPosition() : new Vector3f(0, 0, 0));

    fallingStateManager = new FallingStateManager(currentGameMode);
    fallingStateManager.onFallStart(playerController, levelStartPos);

    System.out.println("Entered FALLING state. Mode: " + currentGameMode);

    // Reset countdown - will show restart option/timer
    if (countdownManager != null) {
      countdownManager.reset();
    }
  }

  private void onEnterPausedState() {
    // Show pause menu
    menuVisible = true;

    // Re-enable Nifty input processing for pause menu
    if (niftyDisplay != null) {
      try {
        guiViewPort.addProcessor(niftyDisplay);
      } catch (Exception e) {
        System.err.println("Note: Could not re-add Nifty processor to viewport");
      }
    }

    if (gameUIManager != null) {
      gameUIManager.showPauseMenu();
    }
    inputManager.setCursorVisible(true);
  }

  private void onEnterGameOverState() {
    // Show game over screen with stats
    menuVisible = true;

    // Re-enable Nifty input processing for game over screen
    if (niftyDisplay != null) {
      try {
        guiViewPort.addProcessor(niftyDisplay);
      } catch (Exception e) {
        System.err.println("Note: Could not re-add Nifty processor to viewport");
      }
    }
    inputManager.setCursorVisible(true);
  }

  // ============== Level Initialization ==============

  /** Initializes a new level by generating the maze and creating platforms. */
  private void initializeLevel() {
    try {
      // Generate new maze from REST API
      MazeRequest request = new MazeRequest("sidewinder", mazeRows, mazeCols, mazeSeed, true);
      MazeResponse mazeResponse = mazeService.createMaze(request);

      if (mazeResponse == null || mazeResponse.getData() == null) {
        System.err.println("Failed to generate maze");
        return;
      }

      // Generate platform layout from maze
      currentPlatformLayout =
          platformGeneratorService.generateLayout(mazeResponse, gameStateService.getCurrentLevel());

      System.out.println("Generated layout: " + currentPlatformLayout);

      // Clear old platforms visualization
      platformsNode.detachAllChildren();

      // Create visual platforms
      renderPlatforms();

      // Initialize player at start position - on top of the first platform
      if (currentPlatformLayout.getStartPosition() != null
          && !currentPlatformLayout.getPlatforms().isEmpty()) {
        Vector3f startPos = currentPlatformLayout.getStartPosition().clone();

        // For obstacle-course gameplay, start player on ground plane at Y=0
        // Player center is at 0.9f (playerHeight 1.8 / 2) so feet are at Y=0
        startPos.y = 0.9f;

        playerController = new PlayerController(startPos);
        playerController.setAudioManager(audioManager, rootNode);
        System.out.println(
            "Player initialized at ground level: " + startPos + " (infinite plane at Y=0)");

        // Start the countdown
        gameStarted = false;
        if (countdownManager != null) {
          countdownManager.startCountdown(
              () -> {
                gameStarted = true;
                System.out.println("Level started!");
              });
        } else {
          gameStarted = true;
        }
      }

    } catch (Exception e) {
      System.err.println("Error initializing level: " + e.getMessage());
      e.printStackTrace();
    }
  }

  /**
   * Loads the next level by using preloaded data or generating it on demand. Seamlessly transitions
   * the player to the new level without state interruption.
   */
  private void loadNextLevel() {
    try {
      int nextLevelNumber = gameStateService.getCurrentLevel() + 1;
      LevelProgressionService.LevelData levelData;

      // Check if we have preloaded data
      if (nextLevelData != null && nextLevelData.getLevelNumber() == nextLevelNumber) {
        levelData = nextLevelData;
        nextLevelData = null;
        System.out.println("Using preloaded level data");
      } else {
        // Fallback: generate on demand
        levelData = levelProgressionService.generateLevel(nextLevelNumber);
      }

      if (levelData == null || levelData.getMazeResponse() == null) {
        System.err.println("Failed to load next level");
        return;
      }

      // Generate platform layout from maze
      currentPlatformLayout =
          platformGeneratorService.generateLayout(levelData.getMazeResponse(), nextLevelNumber);

      System.out.println("Loaded next level: " + currentPlatformLayout);

      // Clear old platforms visualization
      platformsNode.detachAllChildren();

      // Create visual platforms for new level
      renderPlatforms();

      // Reset player to start of new level
      if (currentPlatformLayout.getStartPosition() != null
          && !currentPlatformLayout.getPlatforms().isEmpty()) {
        Vector3f startPos = currentPlatformLayout.getStartPosition().clone();

        // Get the first platform to determine proper spawn height
        Platform firstPlatform = currentPlatformLayout.getPlatforms().get(0);
        float platformTopY = firstPlatform.getTopSurfaceY();

        // Place player above the platform's top surface
        startPos.y = platformTopY + 3.0f;

        playerController.setPosition(startPos);
        playerController.setVelocity(new Vector3f(0, 0, 0));
        System.out.println(
            "Player positioned at new level start: "
                + startPos
                + " (platform top was at "
                + platformTopY
                + ")");

        // Start the countdown for the new level
        gameStarted = false;
        if (countdownManager != null) {
          countdownManager.startCountdown(
              () -> {
                gameStarted = true;
                System.out.println("Level started!");
              });
        } else {
          gameStarted = true;
        }
      }

      // Update game state with new level
      gameStateService.startNewLevel(nextLevelNumber, levelData.getSeed());

      // Preload the level after next
      levelProgressionService.preloadNextLevel(nextLevelNumber);

    } catch (Exception e) {
      System.err.println("Error loading next level: " + e.getMessage());
      e.printStackTrace();
    }
  }

  /** Renders all maze walls as visual geometry in the scene. */
  private void renderPlatforms() {
    if (currentPlatformLayout == null) {
      return;
    }

    // Generate walls from platforms for obstacle-course gameplay
    if (mazeWallService != null) {
      currentWalls = mazeWallService.generateWallsFromMaze(currentPlatformLayout);
      System.out.println("✓ Generated " + currentWalls.size() + " walls for collision detection");

      // Render the walls as visual geometry
      System.out.println("🎮 Rendering " + currentWalls.size() + " maze walls with lit materials");
      int count = 0;
      for (MazeWallService.Wall wall : currentWalls) {
        renderWall(wall);
        count++;
      }
      System.out.println("✓ All " + count + " walls rendered with lighting effects");
    }
  }

  /** Renders a single wall as a 3D box geometry with visual styling and realistic shading. */
  private void renderWall(MazeWallService.Wall wall) {
    Box wallShape = new Box(wall.width / 2, wall.height / 2, wall.depth / 2);
    Geometry wallGeom = new Geometry("Wall_" + Math.abs(wall.position.hashCode()), wallShape);

    // Generate tangent/normal data for proper lighting
    com.jme3.util.TangentBinormalGenerator.generate(wallGeom.getMesh());

    // Create a lit material using MaterialManager for realistic shading
    // Use absolute value of position hash for consistent but varied coloring per wall
    Material mat = materialManager.getPaletteMaterial(Math.abs((long) wall.position.hashCode()));

    // Debug: Log the material definition being used (first wall only to avoid spam)
    if (platformsNode.getChildren().isEmpty()) {
      System.out.println("✓ Wall 0 - Material: " + mat.getMaterialDef().getName());
      System.out.println("✓ Diffuse color: " + mat.getParam("Diffuse"));
      System.out.println("✓ Scene lights: " + rootNode.getLocalLightList().size());
    }

    wallGeom.setMaterial(mat);

    // Enable shadow casting and receiving for realistic lighting
    wallGeom.setShadowMode(com.jme3.renderer.queue.RenderQueue.ShadowMode.CastAndReceive);

    // Set position - wall.position is already at the center
    wallGeom.setLocalTranslation(wall.position);

    // Attach to scene
    platformsNode.attachChild(wallGeom);
  }

  public GameState getCurrentGameState() {
    return currentGameState;
  }

  public void setGameMode(GameMode mode) {
    currentGameMode = mode;
    System.out.println("Game Mode set to: " + mode);
  }

  public GameMode getCurrentGameMode() {
    return currentGameMode;
  }

  // ============== Input Event Handler ==============

  private class GameInputListener implements InputManager.InputListener {
    private float rotationAccumulator = 0f;

    @Override
    public void onActionPressed(String action) {
      switch (action) {
        case InputManager.ACTION_JUMP:
          if (currentGameState == GameState.PLAYING && !jumpPressed) {
            jumpPressed = true;
            hapticFeedbackService.onJump();
            System.out.println("Jump!");
          }
          break;
        case InputManager.ACTION_PAUSE:
          handlePauseAction();
          break;
        case InputManager.ACTION_MENU_TOGGLE:
          handlePauseAction();
          break;
        case InputManager.ACTION_PERSPECTIVE_TOGGLE:
          if (currentGameState == GameState.PLAYING) {
            // TODO: Toggle between first-person and orthogonal perspectives
            System.out.println("Perspective toggled");
          }
          break;
      }
    }

    private void handlePauseAction() {
      switch (currentGameState) {
        case PLAYING:
          // Pause the game
          setGameState(GameState.PAUSED);
          break;
        case PAUSED:
          // Resume the game
          gameStateService.resumeGame();
          setGameState(GameState.PLAYING);
          break;
        case MENU:
        case GAME_OVER:
          // Return to menu from pause or game over
          setGameState(GameState.MENU);
          break;
      }
    }

    @Override
    public void onActionReleased(String action) {
      switch (action) {
        case InputManager.ACTION_JUMP:
          jumpPressed = false;
          break;
      }
    }

    @Override
    public void onAnalogUpdate(String action, float value) {
      if (currentGameState != GameState.PLAYING || playerController == null) {
        return;
      }

      switch (action) {
        case InputManager.ACTION_CAMERA_ROTATE_LEFT:
          playerController.rotateCamera(-value * CAMERA_ROTATION_SPEED);
          break;
        case InputManager.ACTION_CAMERA_ROTATE_RIGHT:
          playerController.rotateCamera(value * CAMERA_ROTATION_SPEED);
          break;
        case InputManager.ACTION_MOUSE_X:
          // Mouse X movement: positive value = move right, negative = move left
          // Invert for intuitive camera control (moving mouse right rotates view right)
          playerController.rotateCamera(-value * 0.5f);
          break;
      }
    }

    private void updateCameraRotation() {
      cameraRotation = rotationAccumulator;
      // Camera rotation is now applied directly to player controller
    }
  }

  private void toggleMenu() {
    menuVisible = !menuVisible;
    if (menuVisible) {
      safeGotoScreen("mainMenu");
      flyCam.setDragToRotate(true);
      inputManager.setCursorVisible(true);
    } else {
      safeGotoScreen("empty");
      flyCam.setDragToRotate(false);
      inputManager.setCursorVisible(false);
    }
  }

  // ============== Nifty GUI Callbacks ==============

  public void showSettings() {
    safeGotoScreen("settings");
    updateSettingsLabels();
  }

  public void quickGenerate() {
    generateMaze();
    menuVisible = false;
    nifty.gotoScreen("empty");
    flyCam.setDragToRotate(false);
    inputManager.setCursorVisible(false);
  }

  public void play() {
    menuVisible = false;
    safeGotoScreen("empty");
    flyCam.setDragToRotate(false);
    inputManager.setCursorVisible(false);
  }

  public void exitGame() {
    // Cleanup audio resources
    if (audioManager != null) {
      audioManager.cleanup();
    }
    stop();
  }

  public void setAlgorithm(String algo) {
    this.selectedAlgorithm = algo;
    System.out.println("Algorithm set to: " + algo);
  }

  public void adjustRows(String delta) {
    mazeRows = Math.max(5, Math.min(50, mazeRows + Integer.parseInt(delta)));
    updateSettingsLabels();
  }

  public void adjustCols(String delta) {
    mazeCols = Math.max(5, Math.min(50, mazeCols + Integer.parseInt(delta)));
    updateSettingsLabels();
  }

  public void adjustSeed(String delta) {
    mazeSeed += Integer.parseInt(delta);
    updateSettingsLabels();
  }

  public void randomSeed() {
    mazeSeed = (int) (Math.random() * 10000);
    updateSettingsLabels();
  }

  public void toggleDistances() {
    showDistances = !showDistances;
    updateSettingsLabels();
  }

  public void generateAndPlay() {
    try {
      System.out.println("🎮 generateAndPlay() called");

      // Generate initial maze
      System.out.println("→ Calling generateMaze()...");
      generateMaze();
      System.out.println("✓ Maze generated");

      // Initialize game state service with current mode
      System.out.println("→ Starting new game with mode: " + currentGameMode);
      gameStateService.startNewGame(currentGameMode);
      System.out.println("✓ Game state service initialized");

      // Transition to playing state
      System.out.println("→ Transitioning to PLAYING state");
      setGameState(GameState.PLAYING);
      System.out.println("✓ Successfully transitioned to PLAYING state");
    } catch (Exception e) {
      System.err.println("ERROR in generateAndPlay(): " + e.getMessage());
      e.printStackTrace();
    }
  }

  public void backToMenu() {
    nifty.gotoScreen("mainMenu");
  }

  private void updateSettingsLabels() {
    Screen screen = nifty.getScreen("settings");
    if (screen != null) {
      de.lessvoid.nifty.elements.Element rowsLabel = screen.findElementById("rowsLabel");
      if (rowsLabel != null) {
        de.lessvoid.nifty.elements.render.TextRenderer tr =
            rowsLabel.getRenderer(de.lessvoid.nifty.elements.render.TextRenderer.class);
        if (tr != null) {
          tr.setText(String.valueOf(mazeRows));
        }
      }

      de.lessvoid.nifty.elements.Element colsLabel = screen.findElementById("colsLabel");
      if (colsLabel != null) {
        de.lessvoid.nifty.elements.render.TextRenderer tr =
            colsLabel.getRenderer(de.lessvoid.nifty.elements.render.TextRenderer.class);
        if (tr != null) {
          tr.setText(String.valueOf(mazeCols));
        }
      }

      de.lessvoid.nifty.elements.Element seedLabel = screen.findElementById("seedLabel");
      if (seedLabel != null) {
        de.lessvoid.nifty.elements.render.TextRenderer tr =
            seedLabel.getRenderer(de.lessvoid.nifty.elements.render.TextRenderer.class);
        if (tr != null) {
          tr.setText(String.valueOf(mazeSeed));
        }
      }

      de.lessvoid.nifty.elements.Element distToggle = screen.findElementById("distToggle");
      if (distToggle != null) {
        de.lessvoid.nifty.elements.render.TextRenderer tr =
            distToggle.getRenderer(de.lessvoid.nifty.elements.render.TextRenderer.class);
        if (tr != null) {
          tr.setText(showDistances ? "ON" : "OFF");
        }
      }
    }
  }

  // ============== ScreenController interface ==============

  @Override
  public void bind(Nifty nifty, Screen screen) {
    // Called when screen is bound
  }

  @Override
  public void onStartScreen() {
    // Called when screen starts
  }

  @Override
  public void onEndScreen() {
    // Called when screen ends
  }

  // ============== Maze Generation ==============

  private void generateMaze() {
    try {
      System.out.println("→ generateMaze() - clearing old maze");
      // Clear existing maze
      mazeNode.detachAllChildren();

      // Load maze from API with current settings
      System.out.println("→ generateMaze() - loading maze from API");
      loadMazeFromApi();
      System.out.println("✓ generateMaze() - maze loaded");

      // Reset camera position
      System.out.println("→ generateMaze() - setting up camera");
      setupCamera();
      System.out.println("✓ generateMaze() - complete");
    } catch (Exception e) {
      System.err.println("ERROR in generateMaze(): " + e.getMessage());
      e.printStackTrace();
    }
  }

  private void loadMazeFromApi() {
    try {
      MazeRequest request =
          new MazeRequest(selectedAlgorithm, mazeRows, mazeCols, mazeSeed, showDistances);
      MazeResponse response = mazeService.createMaze(request);
      if (response != null && response.getData() != null) {
        byte[] decodedBytes = Base64.getDecoder().decode(response.getData());
        this.asciiMaze = new String(decodedBytes);
        System.out.println("Maze loaded successfully!");
        System.out.println("Algorithm: " + selectedAlgorithm);
        System.out.println("Size: " + mazeRows + "x" + mazeCols);
      }
    } catch (Exception e) {
      System.err.println("Failed to load maze: " + e.getMessage());
      this.asciiMaze = Strings.EMPTY;
    }
  }

  private void addFloor() {
    Box floorBox = new Box(100, 0.05f, 100);
    Geometry floor = new Geometry("Floor", floorBox);

    // Use lit material so floor responds to lighting
    Material mat = new Material(assetManager, "Common/MatDefs/Light/Lighting.j3md");
    mat.setBoolean("UseMaterialColors", true);
    mat.setColor("Diffuse", new ColorRGBA(0.2f, 0.2f, 0.2f, 1f));
    mat.setColor("Ambient", new ColorRGBA(0.1f, 0.1f, 0.1f, 1f));
    mat.setColor("Specular", ColorRGBA.Black);
    mat.setFloat("Shininess", 1f); // Matte finish

    floor.setMaterial(mat);
    floor.setShadowMode(com.jme3.renderer.queue.RenderQueue.ShadowMode.Receive);

    floor.setLocalTranslation(50, 0, -50);
    rootNode.attachChild(floor);
  }

  private void setupCamera() {
    float centerX = mazeCols * CELL_SIZE / 2f;
    float centerZ = -mazeRows * CELL_SIZE / 2f;
    float camHeight = Math.max(mazeRows, mazeCols) * 1.5f;

    cam.setLocation(new Vector3f(centerX, camHeight, centerZ + camHeight * 0.5f));
    cam.lookAt(new Vector3f(centerX, 0, centerZ), Vector3f.UNIT_Y);

    flyCam.setMoveSpeed(30);
  }

  /**
   * Override update to catch NullPointerException from Nifty during screen transitions. This
   * prevents the game from crashing when mouse events are processed while Nifty has no current
   * screen.
   */
  @Override
  public void update() {
    try {
      super.update();
    } catch (NullPointerException e) {
      // Check if this is the Nifty null screen exception
      if (e.getMessage() != null && e.getMessage().contains("getCurrentScreen()")) {
        // Silently suppress - the UI will recover on the next frame
      } else {
        // Re-throw if it's not the Nifty screen exception
        throw e;
      }
    }
  }

  @Override
  public void simpleUpdate(float tpf) {
    // Update countdown timer
    if (countdownManager != null) {
      countdownManager.update(tpf);
    }

    // Update HUD display
    if (hudLayer != null && currentGameState == GameState.PLAYING) {
      hudLayer.update();
    }

    // Update game logic based on current state
    switch (currentGameState) {
      case PLAYING:
        updatePlayingState(tpf);
        break;
      case FALLING:
        updateFallingState(tpf);
        break;
      case MENU:
      case PAUSED:
      case GAME_OVER:
        // No game logic updates needed in these states
        break;
    }
  }

  private void updatePlayingState(float tpf) {
    if (playerController == null || currentPlatformLayout == null) {
      return;
    }

    // Update game state service
    gameStateService.update(tpf);

    // Only update player physics if the game has actually started (countdown finished)
    if (gameStarted) {
      // Handle jump if requested
      if (jumpPressed) {
        playerController.jump();
        jumpPressed = false; // Reset after attempting jump
      }

      // Update player physics with wall collision
      playerController.updateWithWalls(tpf, currentWalls);

      // Check if player fell
      if (playerController.hasFallen()) {
        gameStateService.playerFell();
        setGameState(GameState.FALLING);
        return;
      }

      // Check if player reached end of level
      if (currentPlatformLayout.getEndPosition() != null) {
        float distToEnd =
            playerController.getPosition().distance(currentPlatformLayout.getEndPosition());
        if (distToEnd < 1.5f && playerController.isGrounded()) {
          hapticFeedbackService.onLevelComplete();
          gameStateService.levelComplete();
          loadNextLevel();
        }
      }
    }

    // Update camera to follow player
    updateGameCamera();
  }

  private void updateFallingState(float tpf) {
    if (fallingStateManager == null) {
      return;
    }

    // Update falling timeout
    fallingStateManager.update(tpf);

    // Check if timeout expired
    if (fallingStateManager.hasTimedOut()) {
      GameState nextState = fallingStateManager.getNextGameState();

      if (nextState == GameState.PLAYING) {
        // Zen mode: restart current level
        fallingStateManager.resetPlayerPosition();
        gameStateService.playerFell();

        // Restart the countdown for the restarted level
        gameStarted = false;
        if (countdownManager != null) {
          countdownManager.startCountdown(
              () -> {
                gameStarted = true;
                System.out.println("Level restarted!");
              });
        } else {
          gameStarted = true;
        }

        setGameState(GameState.PLAYING);
      } else {
        // Time-trial mode: game over
        gameStateService.playerFell();
        gameStateService.gameOver();
        setGameState(GameState.GAME_OVER);
      }
    }
  }

  /**
   * Updates the game camera to follow the player. For zen mode (first-person), camera is above
   * player head. For time-trial mode (orthogonal), camera is top-down angled view.
   */
  private void updateGameCamera() {
    if (playerController == null) {
      return;
    }

    Vector3f playerPos = playerController.getPosition();

    if (currentGameMode == GameMode.ZEN) {
      // First-person camera: follow player's eyes
      Vector3f cameraPos = playerPos.clone();
      cameraPos.y += playerController.getPlayerHeight() * 0.8f; // Eyes position

      cam.setLocation(cameraPos);

      // Look in the direction of rotation
      Vector3f lookDir = playerController.getForwardDirection();
      Vector3f targetLook = cameraPos.add(lookDir.mult(10));
      cam.lookAt(targetLook, Vector3f.UNIT_Y);

    } else if (currentGameMode == GameMode.TIME_TRIAL) {
      // Orthogonal camera: top-down angled view
      Vector3f cameraPos = playerPos.clone();
      cameraPos.y += 15; // Height above player
      cameraPos.z += 10; // Offset back
      cameraPos.x += 5; // Slight offset to side

      cam.setLocation(cameraPos);
      cam.lookAt(playerPos, Vector3f.UNIT_Y);
    }
  }

  @Override
  public void destroy() {
    super.destroy();
    // Call the shutdown callback when the game window closes
    if (onCloseCallback != null) {
      onCloseCallback.run();
    }
  }
}
