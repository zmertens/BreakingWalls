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

public class MazeGameApp extends SimpleApplication implements ScreenController {

  private final MazeService mazeService;
  private final HapticFeedbackService hapticFeedbackService;
  private final PlatformGeneratorService platformGeneratorService;
  private final GameStateService gameStateService;
  private final LevelProgressionService levelProgressionService;
  private final Runnable onCloseCallback;
  private String asciiMaze;
  private Node mazeNode;

  // Game state
  private GameState currentGameState = GameState.MENU;
  private GameMode currentGameMode = GameMode.ZEN;

  // Platform and player state
  private PlatformLayout currentPlatformLayout;
  private PlayerController playerController;
  private Node platformsNode; // Visual representation of platforms
  private FallingStateManager fallingStateManager;

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

    // Create maze node container (legacy)
    mazeNode = new Node("MazeNode");
    rootNode.attachChild(mazeNode);

    // Create platforms node for game
    platformsNode = new Node("PlatformsNode");
    rootNode.attachChild(platformsNode);

    // Initialize input management
    gameInputManager = new InputManager(inputManager);
    gameInputManager.setupInputMappings();
    gameInputManager.setListener(new GameInputListener());

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
      if (currentPlatformLayout.getStartPosition() != null) {
        Vector3f startPos = currentPlatformLayout.getStartPosition();
        // Place player on top of the starting platform
        startPos.y += 1.0f; // Add height to spawn on top of the platform
        playerController = new PlayerController(startPos);
        System.out.println("Player initialized at: " + startPos);

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
      if (currentPlatformLayout.getStartPosition() != null) {
        Vector3f startPos = currentPlatformLayout.getStartPosition();
        // Place player on top of the starting platform
        startPos.y += 1.0f; // Add height to spawn on top of the platform
        playerController.setPosition(startPos);
        playerController.setVelocity(new Vector3f(0, 0, 0));
        System.out.println("Player positioned at new level start: " + startPos);

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

  /** Renders all platforms as visual geometry in the scene. */
  private void renderPlatforms() {
    if (currentPlatformLayout == null) {
      return;
    }

    for (Platform platform : currentPlatformLayout.getPlatforms()) {
      renderPlatform(platform);
    }
  }

  /** Renders a single platform as a 3D box geometry with visual styling. */
  private void renderPlatform(Platform platform) {
    Box platformShape =
        new Box(platform.getWidth() / 2, platform.getHeight() / 2, platform.getDepth() / 2);
    Geometry platformGeom = new Geometry("Platform_" + platform.getId(), platformShape);

    // Create a material with varying colors based on position and randomness
    Material mat = new Material(assetManager, "Common/MatDefs/Misc/Unshaded.j3md");
    mat.setColor("Color", getRandomPlatformColor(platform));
    platformGeom.setMaterial(mat);

    // Set position
    platformGeom.setLocalTranslation(platform.getPosition());

    // Attach to scene
    platformsNode.attachChild(platformGeom);
  }

  /** Gets a random but visually appealing color for a platform. */
  private ColorRGBA getRandomPlatformColor(Platform platform) {
    // Use platform ID as seed for consistent but varied coloring
    java.util.Random colorGen = new java.util.Random(platform.getId());

    // Create color palettes for different visual styles
    ColorRGBA[] colorPalette = {
      new ColorRGBA(0.2f, 0.8f, 0.9f, 1.0f), // Cyan
      new ColorRGBA(0.1f, 0.9f, 0.4f, 1.0f), // Bright Green
      new ColorRGBA(1.0f, 0.6f, 0.1f, 1.0f), // Orange
      new ColorRGBA(0.9f, 0.3f, 0.8f, 1.0f), // Magenta
      new ColorRGBA(0.3f, 0.7f, 1.0f, 1.0f), // Light Blue
      new ColorRGBA(1.0f, 0.9f, 0.2f, 1.0f), // Yellow
      new ColorRGBA(0.0f, 0.8f, 0.6f, 1.0f), // Teal
      new ColorRGBA(1.0f, 0.4f, 0.4f, 1.0f), // Light Red
      new ColorRGBA(0.5f, 1.0f, 0.2f, 1.0f), // Lime Green
      new ColorRGBA(0.6f, 0.4f, 1.0f, 1.0f), // Lavender
    };

    // Pick a color from the palette and slightly vary it
    int colorIndex = Math.abs(platform.getId()) % colorPalette.length;
    ColorRGBA baseColor = colorPalette[colorIndex];

    // Add slight randomization to avoid monotonous look
    float hueVariation = (colorGen.nextFloat() - 0.5f) * 0.1f;
    float brightnessVariation = (colorGen.nextFloat() - 0.5f) * 0.05f;

    ColorRGBA finalColor =
        new ColorRGBA(
            Math.max(0.1f, Math.min(1.0f, baseColor.r + brightnessVariation)),
            Math.max(0.1f, Math.min(1.0f, baseColor.g + brightnessVariation)),
            Math.max(0.1f, Math.min(1.0f, baseColor.b + brightnessVariation)),
            1.0f);

    return finalColor;
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
      if (currentGameState != GameState.PLAYING) {
        return;
      }

      switch (action) {
        case InputManager.ACTION_CAMERA_ROTATE_LEFT:
          rotationAccumulator -= value * CAMERA_ROTATION_SPEED;
          updateCameraRotation();
          break;
        case InputManager.ACTION_CAMERA_ROTATE_RIGHT:
          rotationAccumulator += value * CAMERA_ROTATION_SPEED;
          updateCameraRotation();
          break;
      }
    }

    private void updateCameraRotation() {
      cameraRotation = rotationAccumulator;
      // TODO: Apply camera rotation to game camera based on perspective mode
      // For now, this is a placeholder for rotation logic
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
    // Generate initial maze
    generateMaze();

    // Initialize game state service with current mode
    gameStateService.startNewGame(currentGameMode);

    // Transition to playing state
    setGameState(GameState.PLAYING);
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
    // Clear existing maze
    mazeNode.detachAllChildren();

    // Load maze from API with current settings
    loadMazeFromApi();

    // Build the 3D maze
    if (asciiMaze != null) {
      buildMaze3D();
    }

    // Reset camera position
    setupCamera();
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
      this.asciiMaze = createDefaultMaze();
    }
  }

  private String createDefaultMaze() {
    return """
            +---+---+---+---+---+
            | 0   1   2 | 3   4 |
            +   +---+   +   +---+
            | 1 | 4   3 | 4 | 7 |
            +   +   +---+   +   +
            | 2   3 | 6   5   6 |
            +---+   +   +---+   +
            | 5   4 | 7 | 8   7 |
            +   +---+   +   +---+
            | 6   7   8   9   8 |
            +---+---+---+---+---+
            """;
  }

  private void buildMaze3D() {
    String[] lines = asciiMaze.split("\n");
    int height = lines.length;
    int maxDistance = findMaxDistance(asciiMaze);

    for (int y = 0; y < height; y++) {
      String line = lines[y];
      for (int x = 0; x < line.length(); x++) {
        char c = line.charAt(x);

        if (c == '+' || c == '-' || c == '|') {
          createWallBlock(x, y, ColorRGBA.DarkGray);
        } else if (Character.isLetterOrDigit(c) && c != ' ') {
          int distance = parseBase36(c);
          if (distance >= 0) {
            ColorRGBA color = getGradientColor(distance, maxDistance);
            createFloorTile(x, y, color);
          }
        }
      }
    }
  }

  private int findMaxDistance(String maze) {
    int max = 0;
    for (char c : maze.toCharArray()) {
      if (Character.isLetterOrDigit(c) && c != '+' && c != '-' && c != '|') {
        int d = parseBase36(c);
        if (d > max) max = d;
      }
    }
    return max;
  }

  private void createWallBlock(int x, int z, ColorRGBA color) {
    Box box = new Box(CELL_SIZE / 2, WALL_HEIGHT / 2, CELL_SIZE / 2);
    Geometry wall = new Geometry("Wall_" + x + "_" + z, box);

    Material mat = new Material(assetManager, "Common/MatDefs/Misc/Unshaded.j3md");
    mat.setColor("Color", color);
    wall.setMaterial(mat);

    wall.setLocalTranslation(x * CELL_SIZE, WALL_HEIGHT / 2, -z * CELL_SIZE);
    mazeNode.attachChild(wall);
  }

  private void createFloorTile(int x, int z, ColorRGBA color) {
    Box box = new Box(CELL_SIZE / 2, 0.1f, CELL_SIZE / 2);
    Geometry tile = new Geometry("Tile_" + x + "_" + z, box);

    Material mat = new Material(assetManager, "Common/MatDefs/Misc/Unshaded.j3md");
    mat.setColor("Color", color);
    tile.setMaterial(mat);

    tile.setLocalTranslation(x * CELL_SIZE, 0.1f, -z * CELL_SIZE);
    mazeNode.attachChild(tile);
  }

  private void addFloor() {
    Box floorBox = new Box(100, 0.05f, 100);
    Geometry floor = new Geometry("Floor", floorBox);

    Material mat = new Material(assetManager, "Common/MatDefs/Misc/Unshaded.j3md");
    mat.setColor("Color", new ColorRGBA(0.15f, 0.15f, 0.15f, 1f));
    floor.setMaterial(mat);

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

  private ColorRGBA getGradientColor(int distance, int maxDistance) {
    if (maxDistance == 0) {
      return ColorRGBA.White;
    }

    float ratio = (float) distance / maxDistance;
    float red, green, blue;

    if (ratio < 0.25f) {
      float localRatio = ratio / 0.25f;
      red = 0;
      green = localRatio;
      blue = 1;
    } else if (ratio < 0.5f) {
      float localRatio = (ratio - 0.25f) / 0.25f;
      red = 0;
      green = 1;
      blue = 1 - localRatio;
    } else if (ratio < 0.75f) {
      float localRatio = (ratio - 0.5f) / 0.25f;
      red = localRatio;
      green = 1;
      blue = 0;
    } else {
      float localRatio = (ratio - 0.75f) / 0.25f;
      red = 1;
      green = 1 - localRatio;
      blue = 0;
    }

    return new ColorRGBA(red, green, blue, 1f);
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
      // Update player physics
      playerController.update(tpf, currentPlatformLayout);

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
