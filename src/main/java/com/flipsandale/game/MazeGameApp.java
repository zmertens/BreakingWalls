package com.flipsandale.game;

import com.flipsandale.dto.MazeRequest;
import com.flipsandale.dto.MazeResponse;
import com.flipsandale.game.state.GameStateFactory;
import com.flipsandale.game.state.GameStateId;
import com.flipsandale.game.state.GameStateStack;
import com.flipsandale.game.state.StateContext;
import com.flipsandale.service.CornersService;
import com.jme3.app.SimpleApplication;
import com.jme3.font.BitmapFont;
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

  private final CornersService CornersService;
  private final HapticFeedbackService hapticFeedbackService;
  private final PlatformGeneratorService platformGeneratorService;
  private final GameStateService gameStateService;
  private final LevelProgressionService levelProgressionService;
  private final Runnable onCloseCallback;
  private Node mazeNode;
  private MaterialManager materialManager;
  private AudioManager audioManager;

  // Game state
  private GameState currentGameState = GameState.MENU;
  private GameMode currentGameMode = GameMode.ZEN;

  // Platform and player state
  private PlatformLayout currentPlatformLayout;
  private PlayerController playerController;
  private Node platformsNode;
  private FallingStateManager fallingStateManager;
  private MazeWallService mazeWallService;
  private java.util.List<MazeWallService.Wall> currentWalls;

  // Third-person perspective
  private Geometry playerVisual;
  private boolean useThirdPersonView = true; // Toggle between first and third person
  private static final float PLAYER_VISUAL_SCALE =
      0.25f; // Scale of visual player relative to actual player
  private static final float THIRD_PERSON_DISTANCE = 4.0f; // Distance behind player for camera
  private static final float THIRD_PERSON_HEIGHT = 2.0f; // Height above player for camera

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
  private BitmapFont guiFont;

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

  // State management (new architecture)
  private GameStateStack gameStateStack;
  private StateContext stateContext;
  private GameStateFactory gameStateFactory;

  // Command queue for processing player actions
  private CommandQueue gameCommandQueue;

  public MazeGameApp(
      CornersService CornersService,
      HapticFeedbackService hapticFeedbackService,
      PlatformGeneratorService platformGeneratorService,
      GameStateService gameStateService,
      LevelProgressionService levelProgressionService,
      Runnable onCloseCallback) {
    this.CornersService = CornersService;
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

    // Create maze node for game
    mazeNode = new Node("MazeNode");
    rootNode.attachChild(mazeNode);

    // Disable jMonkeyEngine's default ESC-to-exit behavior so we can use it for pause menu
    if (inputManager.hasMapping("SIMPLEAPP_Exit")) {
      inputManager.deleteMapping("SIMPLEAPP_Exit");
    }

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

    // Initialize command queue for player actions
    gameCommandQueue = new CommandQueue();

    // Initialize state management system
    initializeStateManagement();

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
    // Load GUI font for HUD elements (required before creating HUD systems)
    guiFont = assetManager.loadFont("Interface/Fonts/Default.fnt");
    hudLayer = new HUDLayer(guiNode, guiFont, hudManager);

    // Create countdown manager for level start
    countdownManager = new GameCountdownManager(guiNode, guiFont);

    System.out.println("HUD System initialized");
  }

  private void initializeStateManagement() {
    // Create StateContext with all shared services
    stateContext =
        new StateContext(
            assetManager,
            rootNode,
            guiNode,
            cam,
            guiFont,
            materialManager,
            audioManager,
            gameInputManager,
            gameStateService,
            levelProgressionService,
            mazeWallService,
            nifty,
            hudLayer,
            // Callbacks for state transitions
            () -> {
              // onLevelComplete - load next level
              System.out.println("Level completed!");
              loadNextLevel();
            },
            () -> {
              // onPlayerFall - transition to FALLING state
              System.out.println("Player fell!");
              setGameState(GameState.FALLING);
            },
            () -> {
              // onGameOver - transition to GAME_OVER state
              System.out.println("Game over!");
              setGameState(GameState.GAME_OVER);
            });

    // Create GameStateFactory for dependency injection
    gameStateFactory = new GameStateFactory(stateContext);

    // Create GameStateStack and push initial MENU state
    gameStateStack = new GameStateStack(stateContext, gameStateFactory);
    gameStateStack.requestPush(GameStateId.MENU);

    System.out.println("State Management initialized");
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
                                new ButtonBuilder("generateBtn", "Generate New Maze") {
                                  {
                                    alignCenter();
                                    height("40px");
                                    width("100%");
                                    interactOnClick("quickGenerate()");
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

  private void buildPauseMenuScreen() {
    nifty.addScreen(
        "pauseMenu",
        new ScreenBuilder("pauseMenu") {
          {
            controller(new PauseMenuScreenController(gameUIManager, gameStateService));

            layer(
                new LayerBuilder("background") {
                  {
                    backgroundColor("#00000099");
                    childLayoutCenter();
                    width("100%");
                    height("100%");

                    panel(
                        new PanelBuilder("pauseMenuContainer") {
                          {
                            childLayoutVertical();
                            width("700px");
                            height("600px");
                            backgroundColor("#1a1a2eff");
                            valignCenter();
                            paddingLeft("20px");
                            paddingRight("20px");
                            paddingTop("20px");
                            paddingBottom("20px");

                            // Title Bar
                            panel(
                                new PanelBuilder("titleBar") {
                                  {
                                    childLayoutCenter();
                                    height("60px");
                                    backgroundColor("#0066ccff");

                                    text(
                                        new TextBuilder() {
                                          {
                                            text("PAUSED");
                                            color("#ffffffff");
                                            font("Interface/Fonts/Default.fnt");
                                          }
                                        });
                                  }
                                });

                            // Tab Navigation Buttons
                            panel(
                                new PanelBuilder("tabButtons") {
                                  {
                                    childLayoutHorizontal();
                                    height("50px");
                                    paddingTop("10px");
                                    paddingBottom("10px");
                                    paddingLeft("5px");
                                    paddingRight("5px");

                                    control(
                                        new ButtonBuilder("btnTabMain", "Main") {
                                          {
                                            height("40px");
                                            width("150px");
                                            alignCenter();
                                            interactOnClick("switchToMainTab()");
                                          }
                                        });

                                    panel(
                                        new PanelBuilder("spacer1") {
                                          {
                                            width("10px");
                                            childLayoutVertical();
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("btnTabGraphics", "Graphics") {
                                          {
                                            height("40px");
                                            width("150px");
                                            alignCenter();
                                            interactOnClick("switchToGraphicsTab()");
                                          }
                                        });

                                    panel(
                                        new PanelBuilder("spacer2") {
                                          {
                                            width("10px");
                                            childLayoutVertical();
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("btnTabAbout", "About") {
                                          {
                                            height("40px");
                                            width("150px");
                                            alignCenter();
                                            interactOnClick("switchToAboutTab()");
                                          }
                                        });

                                    panel(
                                        new PanelBuilder("spacerExpand") {
                                          {
                                            width("100%");
                                            childLayoutVertical();
                                          }
                                        });
                                  }
                                });

                            // Separator
                            panel(
                                new PanelBuilder("separator") {
                                  {
                                    height("2px");
                                    backgroundColor("#0066ccff");
                                    childLayoutVertical();
                                    marginBottom("15px");
                                  }
                                });

                            // Main Tab Content
                            panel(
                                new PanelBuilder("mainTabContent") {
                                  {
                                    childLayoutVertical();
                                    width("100%");
                                    height("350px");
                                    paddingLeft("10px");
                                    paddingRight("10px");

                                    // Main menu buttons
                                    control(
                                        new ButtonBuilder("btnResume", "Resume Game") {
                                          {
                                            height("40px");
                                            width("250px");
                                            alignCenter();
                                            marginBottom("10px");
                                            interactOnClick("resumeGame()");
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("btnRestart", "Restart Level") {
                                          {
                                            height("40px");
                                            width("250px");
                                            alignCenter();
                                            marginBottom("10px");
                                            interactOnClick("restartLevel()");
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("btnOptions", "Options") {
                                          {
                                            height("40px");
                                            width("250px");
                                            alignCenter();
                                            marginBottom("10px");
                                            interactOnClick("showOptions()");
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("btnHelp", "Help") {
                                          {
                                            height("40px");
                                            width("250px");
                                            alignCenter();
                                            marginBottom("10px");
                                            interactOnClick("showHelp()");
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("btnMainMenu", "Return to Main Menu") {
                                          {
                                            height("40px");
                                            width("250px");
                                            alignCenter();
                                            interactOnClick("returnToMainMenu()");
                                          }
                                        });
                                  }
                                });

                            // Graphics Tab Content
                            panel(
                                new PanelBuilder("graphicsTabContent") {
                                  {
                                    childLayoutVertical();
                                    width("100%");
                                    height("350px");
                                    paddingLeft("10px");
                                    paddingRight("10px");
                                    visible(false);

                                    text(
                                        new TextBuilder() {
                                          {
                                            text("GRAPHICS SETTINGS");
                                            color("#0088ffff");
                                            font("Interface/Fonts/Default.fnt");
                                            marginBottom("10px");
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("btnVSync", "Toggle VSync") {
                                          {
                                            height("40px");
                                            width("250px");
                                            alignCenter();
                                            marginBottom("10px");
                                            interactOnClick("onVSyncToggled()");
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("btnFullscreen", "Toggle Fullscreen") {
                                          {
                                            height("40px");
                                            width("250px");
                                            alignCenter();
                                            marginBottom("10px");
                                            interactOnClick("onFullscreenToggled()");
                                          }
                                        });

                                    text(
                                        new TextBuilder() {
                                          {
                                            text("AUDIO SETTINGS");
                                            color("#0088ffff");
                                            font("Interface/Fonts/Default.fnt");
                                            marginTop("20px");
                                            marginBottom("10px");
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("btnMasterVolume", "Master Volume") {
                                          {
                                            height("40px");
                                            width("250px");
                                            alignCenter();
                                            marginBottom("10px");
                                            interactOnClick("onMasterVolumeChanged()");
                                          }
                                        });

                                    control(
                                        new ButtonBuilder("btnSFXVolume", "SFX Volume") {
                                          {
                                            height("40px");
                                            width("250px");
                                            alignCenter();
                                            interactOnClick("onSfxVolumeChanged()");
                                          }
                                        });
                                  }
                                });

                            // About Tab Content
                            panel(
                                new PanelBuilder("aboutTabContent") {
                                  {
                                    childLayoutVertical();
                                    width("100%");
                                    height("350px");
                                    paddingLeft("10px");
                                    paddingRight("10px");
                                    visible(false);

                                    text(
                                        new TextBuilder() {
                                          {
                                            text("GAME STATUS");
                                            color("#0088ffff");
                                            font("Interface/Fonts/Default.fnt");
                                            marginBottom("10px");
                                          }
                                        });

                                    text(
                                        new TextBuilder() {
                                          {
                                            text("Current Mode: ");
                                            color("#ccccccff");
                                            font("Interface/Fonts/Default.fnt");
                                            marginBottom("5px");
                                          }
                                        });

                                    text(
                                        new TextBuilder() {
                                          {
                                            text("Current Level: ");
                                            color("#ccccccff");
                                            font("Interface/Fonts/Default.fnt");
                                            marginBottom("5px");
                                          }
                                        });

                                    text(
                                        new TextBuilder() {
                                          {
                                            text("Platforms Cleared: ");
                                            color("#ccccccff");
                                            font("Interface/Fonts/Default.fnt");
                                            marginBottom("5px");
                                          }
                                        });

                                    text(
                                        new TextBuilder() {
                                          {
                                            text("Falls: ");
                                            color("#ccccccff");
                                            font("Interface/Fonts/Default.fnt");
                                            marginBottom("20px");
                                          }
                                        });

                                    text(
                                        new TextBuilder() {
                                          {
                                            text("ABOUT");
                                            color("#0088ffff");
                                            font("Interface/Fonts/Default.fnt");
                                            marginBottom("10px");
                                          }
                                        });

                                    text(
                                        new TextBuilder() {
                                          {
                                            text("Breaking Walls - A 3D Maze Adventure");
                                            color("#ccccccff");
                                            font("Interface/Fonts/Default.fnt");
                                            marginBottom("3px");
                                          }
                                        });

                                    text(
                                        new TextBuilder() {
                                          {
                                            text("Built with JMonkeyEngine 3");
                                            color("#ccccccff");
                                            font("Interface/Fonts/Default.fnt");
                                          }
                                        });
                                  }
                                });
                          }
                        });
                  }
                });
          }
        }.build(nifty));
  }

  private void buildOptionsMenuScreen() {
    nifty.addScreen(
        "optionsMenu",
        new ScreenBuilder("optionsMenu") {
          {
            controller(new OptionsScreenController(gameUIManager, gameStateService));

            layer(
                new LayerBuilder("background") {
                  {
                    backgroundColor("#00000099");
                    childLayoutCenter();
                    width("100%");
                    height("100%");

                    panel(
                        new PanelBuilder("optionsMenuContainer") {
                          {
                            childLayoutVertical();
                            width("700px");
                            height("600px");
                            backgroundColor("#1a1a2eff");
                            valignCenter();
                            paddingLeft("20px");
                            paddingRight("20px");
                            paddingTop("20px");
                            paddingBottom("20px");

                            // Title Bar
                            panel(
                                new PanelBuilder("titleBar") {
                                  {
                                    childLayoutCenter();
                                    height("60px");
                                    backgroundColor("#0066ccff");

                                    text(
                                        new TextBuilder() {
                                          {
                                            text("OPTIONS");
                                            color("#ffffffff");
                                            font("Interface/Fonts/Default.fnt");
                                          }
                                        });
                                  }
                                });

                            // Separator
                            panel(
                                new PanelBuilder("separator1") {
                                  {
                                    height("2px");
                                    backgroundColor("#0066ccff");
                                    childLayoutVertical();
                                    marginBottom("15px");
                                  }
                                });

                            // Audio Settings Section
                            text(
                                new TextBuilder() {
                                  {
                                    text("AUDIO SETTINGS");
                                    color("#0088ffff");
                                    font("Interface/Fonts/Default.fnt");
                                    marginBottom("10px");
                                  }
                                });

                            panel(
                                new PanelBuilder("audioSettings") {
                                  {
                                    childLayoutVertical();
                                    marginBottom("20px");

                                    // Master Volume
                                    control(
                                        new ButtonBuilder("masterVolumeBtn", "Master Volume") {
                                          {
                                            height("30px");
                                            width("200px");
                                            marginBottom("5px");
                                            interactOnClick("setMasterVolume(0.5)");
                                          }
                                        });

                                    // Music Volume
                                    control(
                                        new ButtonBuilder("musicVolumeBtn", "Music Volume") {
                                          {
                                            height("30px");
                                            width("200px");
                                            marginBottom("5px");
                                            interactOnClick("setMusicVolume(0.5)");
                                          }
                                        });

                                    // SFX Volume
                                    control(
                                        new ButtonBuilder("sfxVolumeBtn", "SFX Volume") {
                                          {
                                            height("30px");
                                            width("200px");
                                            interactOnClick("setSfxVolume(0.5)");
                                          }
                                        });
                                  }
                                });

                            // Separator
                            panel(
                                new PanelBuilder("separator2") {
                                  {
                                    height("1px");
                                    backgroundColor("#444444ff");
                                    childLayoutVertical();
                                    marginBottom("15px");
                                  }
                                });

                            // Graphics Settings Section
                            text(
                                new TextBuilder() {
                                  {
                                    text("GRAPHICS SETTINGS");
                                    color("#0088ffff");
                                    font("Interface/Fonts/Default.fnt");
                                    marginBottom("10px");
                                  }
                                });

                            panel(
                                new PanelBuilder("graphicsSettings") {
                                  {
                                    childLayoutVertical();
                                    marginBottom("20px");

                                    // VSync Toggle
                                    control(
                                        new ButtonBuilder("vSyncBtn", "Toggle VSync") {
                                          {
                                            height("30px");
                                            width("200px");
                                            marginBottom("5px");
                                            interactOnClick("toggleVSync()");
                                          }
                                        });

                                    // Fullscreen Toggle
                                    control(
                                        new ButtonBuilder("fullscreenBtn", "Toggle Fullscreen") {
                                          {
                                            height("30px");
                                            width("200px");
                                            interactOnClick("toggleFullscreen()");
                                          }
                                        });
                                  }
                                });

                            // Bottom buttons panel with padding
                            panel(
                                new PanelBuilder("bottomPanel") {
                                  {
                                    height("80px");
                                    width("100%");
                                    childLayoutHorizontal();
                                    paddingTop("20px");

                                    // Back button
                                    control(
                                        new ButtonBuilder("btnBackMain", "Back") {
                                          {
                                            height("40px");
                                            width("200px");
                                            interactOnClick("backToMainMenu()");
                                          }
                                        });

                                    // Spacer
                                    panel(
                                        new PanelBuilder("spacerBottom") {
                                          {
                                            width("100%");
                                            childLayoutVertical();
                                          }
                                        });
                                  }
                                });
                          }
                        });
                  }
                });
          }
        }.build(nifty));
  }

  private void buildHelpScreen() {
    nifty.addScreen(
        "helpScreen",
        new ScreenBuilder("helpScreen") {
          {
            controller(new HelpScreenController(gameUIManager, gameStateService));

            layer(
                new LayerBuilder("background") {
                  {
                    backgroundColor("#00000099");
                    childLayoutCenter();
                    width("100%");
                    height("100%");

                    panel(
                        new PanelBuilder("helpContainer") {
                          {
                            childLayoutVertical();
                            width("800px");
                            height("650px");
                            backgroundColor("#1a1a2eff");
                            valignCenter();
                            paddingLeft("20px");
                            paddingRight("20px");
                            paddingTop("20px");
                            paddingBottom("20px");

                            // Title Bar
                            panel(
                                new PanelBuilder("titleBar") {
                                  {
                                    childLayoutCenter();
                                    height("60px");
                                    backgroundColor("#0066ccff");

                                    text(
                                        new TextBuilder() {
                                          {
                                            text("HELP & CONTROLS");
                                            color("#ffffffff");
                                            font("Interface/Fonts/Default.fnt");
                                          }
                                        });
                                  }
                                });

                            // Separator
                            panel(
                                new PanelBuilder("separator") {
                                  {
                                    height("2px");
                                    backgroundColor("#0066ccff");
                                    childLayoutVertical();
                                    marginBottom("15px");
                                  }
                                });

                            // Content area
                            panel(
                                new PanelBuilder("contentPanel") {
                                  {
                                    childLayoutVertical();
                                    width("100%");
                                    height("450px");
                                    paddingLeft("10px");
                                    paddingRight("10px");

                                    // Objective section
                                    text(
                                        new TextBuilder() {
                                          {
                                            text("OBJECTIVE");
                                            color("#0088ffff");
                                            font("Interface/Fonts/Default.fnt");
                                            marginBottom("5px");
                                          }
                                        });

                                    text(
                                        new TextBuilder() {
                                          {
                                            text(
                                                "Navigate through a procedurally generated maze. Reach the exit "
                                                    + "to complete each level. Be careful not to fall!");
                                            color("#ccccccff");
                                            font("Interface/Fonts/Default.fnt");
                                            marginBottom("15px");
                                            wrap(true);
                                          }
                                        });

                                    // Controls section
                                    text(
                                        new TextBuilder() {
                                          {
                                            text("CONTROLS");
                                            color("#0088ffff");
                                            font("Interface/Fonts/Default.fnt");
                                            marginBottom("5px");
                                          }
                                        });

                                    text(
                                        new TextBuilder() {
                                          {
                                            text(
                                                "W / A / S / D - Move | Mouse - Look | P - Toggle 3D View | ESC - Menu");
                                            color("#ccccccff");
                                            font("Interface/Fonts/Default.fnt");
                                            marginBottom("15px");
                                          }
                                        });

                                    // Game Modes section
                                    text(
                                        new TextBuilder() {
                                          {
                                            text("GAME MODES");
                                            color("#0088ffff");
                                            font("Interface/Fonts/Default.fnt");
                                            marginBottom("5px");
                                          }
                                        });

                                    text(
                                        new TextBuilder() {
                                          {
                                            text(
                                                "ZEN MODE - Explore at your own pace. No time limit.");
                                            color("#ccccccff");
                                            font("Interface/Fonts/Default.fnt");
                                            marginBottom("3px");
                                          }
                                        });

                                    text(
                                        new TextBuilder() {
                                          {
                                            text(
                                                "TIME-TRIAL MODE - Race against the clock to complete each level.");
                                            color("#ccccccff");
                                            font("Interface/Fonts/Default.fnt");
                                            marginBottom("15px");
                                          }
                                        });

                                    // Tips section
                                    text(
                                        new TextBuilder() {
                                          {
                                            text("TIPS");
                                            color("#0088ffff");
                                            font("Interface/Fonts/Default.fnt");
                                            marginBottom("5px");
                                          }
                                        });

                                    text(
                                        new TextBuilder() {
                                          {
                                            text(
                                                "- Use third-person view to better navigate and plan your path");
                                            color("#ccccccff");
                                            font("Interface/Fonts/Default.fnt");
                                            marginBottom("3px");
                                          }
                                        });

                                    text(
                                        new TextBuilder() {
                                          {
                                            text(
                                                "- Take your time in Zen Mode to understand the maze");
                                            color("#ccccccff");
                                            font("Interface/Fonts/Default.fnt");
                                          }
                                        });
                                  }
                                });

                            // Back button
                            control(
                                new ButtonBuilder("btnBackHelp", "Back") {
                                  {
                                    height("40px");
                                    width("200px");
                                    alignCenter();
                                    marginTop("20px");
                                    interactOnClick("backToMainMenu()");
                                  }
                                });
                          }
                        });
                  }
                });
          }
        }.build(nifty));
  }

  // ============== Game State Management ==============

  /** Sets the current game state and triggers any state-specific initialization. */
  /**
   * Transitions to a new game state via the state stack. This method translates the legacy
   * GameState enum to the new GameStateId enum for backward compatibility.
   */
  private void setGameState(GameState newState) {
    currentGameState = newState;

    if (gameStateStack != null) {
      // Initialize level before entering PLAYING state
      if (newState == GameState.PLAYING) {
        initializeLevel();
      }

      switch (newState) {
        case MENU:
          gameStateStack.requestPush(GameStateId.MENU);
          break;
        case PLAYING:
          gameStateStack.requestPush(GameStateId.PLAYING);
          break;
        case FALLING:
          gameStateStack.requestPush(GameStateId.FALLING);
          break;
        case PAUSED:
          gameStateStack.requestPush(GameStateId.PAUSED);
          break;
        case GAME_OVER:
          gameStateStack.requestPush(GameStateId.GAME_OVER);
          break;
      }
    }
  }

  // ============== Level Initialization ==============

  /** Initializes a new level by generating the maze and creating platforms. */
  private void initializeLevel() {
    try {
      // Generate new maze from REST API
      MazeRequest request = new MazeRequest("sidewinder", mazeRows, mazeCols, mazeSeed, true);
      MazeResponse mazeResponse = CornersService.createMaze(request);

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

        // Create visual representation of player for third-person perspective
        createPlayerVisual();

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

    // Synchronize with state context so states can access initialized data
    if (stateContext != null) {
      stateContext.playerController = playerController;
      stateContext.currentPlatformLayout = currentPlatformLayout;
      stateContext.currentGameMode = currentGameMode;
      stateContext.currentWalls = currentWalls;
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
    private boolean pausePressed = false;

    @Override
    public void onActionPressed(String action) {
      // Route to state stack first (new architecture)
      if (gameStateStack != null) {
        gameStateStack.handleInputAction(action);
      }

      // Keep old logic for backward compatibility during transition
      switch (action) {
        case InputManager.ACTION_JUMP:
          if (currentGameState == GameState.PLAYING && !jumpPressed) {
            jumpPressed = true;
            hapticFeedbackService.onJump();
            System.out.println("Jump!");
          }
          break;
        case InputManager.ACTION_PAUSE:
          if (!pausePressed) {
            pausePressed = true;
            handlePauseAction();
          }
          break;
        case InputManager.ACTION_MENU_TOGGLE:
          if (!pausePressed) {
            pausePressed = true;
            handlePauseAction();
          }
          break;
      }
    }

    private void handlePauseAction() {
      System.out.println("handlePauseAction() called. Current state: " + currentGameState);
      switch (currentGameState) {
        case PLAYING:
          // Pause the game - push PAUSED state onto stack
          System.out.println("→ Transitioning from PLAYING to PAUSED");
          currentGameState = GameState.PAUSED;
          gameStateStack.requestPush(GameStateId.PAUSED);
          break;
        case PAUSED:
          // Resume the game - pop PAUSED state from stack
          System.out.println("→ Transitioning from PAUSED to PLAYING");
          currentGameState = GameState.PLAYING;
          gameStateStack.requestPop();
          gameStateService.resumeGame();
          break;
        case FALLING:
          // Pause during falling transitions - just show pause menu
          System.out.println("→ Cannot pause while falling");
          break;
        case MENU:
        case GAME_OVER:
          // Return to menu from pause or game over
          System.out.println("→ Transitioning to MENU");
          currentGameState = GameState.MENU;
          gameStateStack.requestPush(GameStateId.MENU);
          break;
      }
    }

    @Override
    public void onActionReleased(String action) {
      if (gameStateStack != null) {
        // States don't currently handle action release, but route through for future extensibility
        gameStateStack.handleInputAction(action);
      }

      switch (action) {
        case InputManager.ACTION_JUMP:
          jumpPressed = false;
          break;
        case InputManager.ACTION_PAUSE:
        case InputManager.ACTION_MENU_TOGGLE:
          pausePressed = false;
          break;
      }
    }

    @Override
    public void onAnalogUpdate(String action, float value) {
      if (gameStateStack != null) {
        // Route analog input to current state (states typically don't handle analog,
        // but routing for future state-specific analog handling)
      }

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

  // ============== Nifty GUI Callbacks ==============

  public void showSettings() {
    safeGotoScreen("settings");
    updateSettingsLabels();
  }

  public void quickGenerate() {
    generateMaze();
    menuVisible = false;
    flyCam.setDragToRotate(false);
    inputManager.setCursorVisible(false);
    setGameState(GameState.PLAYING);
  }

  public void play() {
    menuVisible = false;
    flyCam.setDragToRotate(false);
    inputManager.setCursorVisible(false);
    setGameState(GameState.PLAYING);
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
      MazeResponse response = CornersService.createMaze(request);
      if (response != null && response.getData() != null) {
        byte[] decodedBytes = Base64.getDecoder().decode(response.getData());
        System.out.println("Maze loaded successfully!");
        System.out.println("Algorithm: " + selectedAlgorithm);
        System.out.println("Size: " + mazeRows + "x" + mazeCols);
      }
    } catch (Exception e) {
      System.err.println("Failed to load maze: " + e.getMessage());
    }
  }

  private void addFloor() {
    Box floorBox = new Box(100, 0.05f, 100);
    Geometry floor = new Geometry("Floor", floorBox);

    // Use lit material with brick texture
    Material mat = new Material(assetManager, "Common/MatDefs/Light/Lighting.j3md");
    mat.setBoolean("UseMaterialColors", true);

    // Load and apply brick texture
    try {
      com.jme3.texture.Texture brickTexture = assetManager.loadTexture("static/brick_brown.png");
      brickTexture.setWrap(com.jme3.texture.Texture.WrapMode.Repeat);
      mat.setTexture("DiffuseMap", brickTexture);
      System.out.println("✓ Loaded brick texture for floor");
    } catch (Exception e) {
      System.err.println("⚠ Could not load brick texture: " + e.getMessage());
      // Fallback to solid color if texture not found
      mat.setColor("Diffuse", new ColorRGBA(0.4f, 0.3f, 0.2f, 1f));
    }

    mat.setColor("Diffuse", new ColorRGBA(1f, 1f, 1f, 1f)); // White so texture shows clearly
    mat.setColor("Ambient", new ColorRGBA(0.3f, 0.3f, 0.3f, 1f));
    mat.setColor("Specular", ColorRGBA.Black);
    mat.setFloat("Shininess", 1f); // Matte finish

    // Set texture coordinates for tiling (repeat 50x50 times across the plane)
    floorBox.scaleTextureCoordinates(new com.jme3.math.Vector2f(50, 50));

    floor.setMaterial(mat);
    floor.setShadowMode(com.jme3.renderer.queue.RenderQueue.ShadowMode.Receive);

    floor.setLocalTranslation(50, 0, -50);
    rootNode.attachChild(floor);
    System.out.println("✓ Floor created with brick texture");
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
   * Creates a visual representation of the player for third-person perspective. Uses a scaled-down
   * block to represent the character in the game world.
   */
  private void createPlayerVisual() {
    // Remove old player visual if it exists
    if (playerVisual != null) {
      rootNode.detachChild(playerVisual);
    }

    // Create a small scaled-down box to represent the player
    float playerWidth = 0.4f * PLAYER_VISUAL_SCALE;
    float playerHeight = 1.2f * PLAYER_VISUAL_SCALE;
    float playerDepth = 0.4f * PLAYER_VISUAL_SCALE;

    Box playerBox = new Box(playerWidth / 2, playerHeight / 2, playerDepth / 2);
    playerVisual = new Geometry("PlayerVisual", playerBox);

    // Apply a distinctive material to the player visual (cyan color)
    Material playerMaterial =
        materialManager.createUnshadedMaterial(new ColorRGBA(0.2f, 0.8f, 1.0f, 1.0f));
    playerVisual.setMaterial(playerMaterial);

    rootNode.attachChild(playerVisual);
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
    // Process any queued commands from player actions
    if (gameCommandQueue != null) {
      while (!gameCommandQueue.isEmpty()) {
        Command command = gameCommandQueue.pop();
        if (command != null) {
          command.execute(tpf);
        }
      }
    }

    // Delegate all game updates to the state stack
    if (gameStateStack != null) {
      gameStateStack.update(tpf);
    }
  }

  /**
   * Updates the game camera to follow the player. Supports both first-person and third-person
   * perspectives. For zen mode with third-person, camera follows from behind. For time-trial mode,
   * uses top-down angled view.
   */

  /**
   * Updates the player visual geometry to match the player's current position and rotation. This is
   * only visible in third-person perspective.
   */
  private void updatePlayerVisual() {
    if (playerVisual == null || playerController == null) {
      return;
    }

    // Update position to match player's position
    Vector3f playerPos = playerController.getPosition();
    playerVisual.setLocalTranslation(playerPos);

    // Update rotation to match player's facing direction
    if (useThirdPersonView) {
      // Get player's rotation quaternion
      com.jme3.math.Quaternion playerRot = playerController.getRotation();
      playerVisual.setLocalRotation(playerRot);
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
