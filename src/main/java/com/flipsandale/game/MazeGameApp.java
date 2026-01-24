package com.flipsandale.game;

import static com.flipsandale.game.state.GameStateId.*;

import com.flipsandale.dto.MazeRequest;
import com.flipsandale.dto.MazeResponse;
import com.flipsandale.game.state.GameStateFactory;
import com.flipsandale.game.state.GameStateId;
import com.flipsandale.game.state.GameStateStack;
import com.flipsandale.game.state.StateContext;
import com.flipsandale.gui.NiftyGuiBuilder;
import com.flipsandale.service.CornersService;
import com.jme3.app.SimpleApplication;
import com.jme3.effect.ParticleEmitter;
import com.jme3.effect.ParticleMesh;
import com.jme3.font.BitmapFont;
import com.jme3.material.Material;
import com.jme3.math.ColorRGBA;
import com.jme3.math.Vector2f;
import com.jme3.math.Vector3f;
import com.jme3.niftygui.NiftyJmeDisplay;
import com.jme3.scene.Geometry;
import com.jme3.scene.Node;
import com.jme3.scene.shape.Box;
import com.jme3.system.AppSettings;
import de.lessvoid.nifty.Nifty;
import de.lessvoid.nifty.screen.Screen;
import de.lessvoid.nifty.screen.ScreenController;
import java.awt.image.BufferedImage;
import java.io.InputStream;
import java.util.Base64;
import java.util.logging.Level;
import java.util.logging.Logger;
import javax.imageio.ImageIO;

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
  private GameStateId currentGameState = GameStateId.MENU;
  private GameMode currentGameMode = GameMode.ZEN;

  // Platform and player state
  private PlatformLayout currentPlatformLayout;
  private PlayerController playerController;
  private Node platformsNode;

  private MazeWallService mazeWallService;
  private java.util.List<MazeWallService.Wall> currentWalls;

  // Third-person perspective
  private Geometry playerVisual;
  private Node playerEffectNode;
  private ParticleEmitter playerExplosionEmitter;
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

    // Load and set window icon
    try (InputStream is = MazeGameApp.class.getResourceAsStream("/static/icon.bmp")) {
      if (is != null) {
        BufferedImage icon = ImageIO.read(is);
        settings.setIcons(new BufferedImage[] {icon});
      } else {
        System.err.println("Could not find icon.bmp in classpath");
      }
    } catch (Exception e) {
      System.err.println("Error loading window icon: " + e.getMessage());
    }

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
    setGameState(MENU);
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

    NiftyGuiBuilder niftyGuiBuilder = new NiftyGuiBuilder(nifty, this);

    // Build empty screen (for gameplay)
    niftyGuiBuilder.buildEmptyScreen();

    // Build main menu screen
    niftyGuiBuilder.buildMainMenuScreen();

    // Build settings screen
    niftyGuiBuilder.buildSettingsScreen();

    // Initialize HUD system
    initializeHUDSystem();

    // Initialize command queue for player actions
    gameCommandQueue = new CommandQueue();

    // Initialize state management system
    initializeStateManagement();

    // Add processor after all initialization is complete
    guiViewPort.addProcessor(niftyDisplay);

    // Show main menu safely - now processor is added and initialization is complete
    safeGotoScreen("mainMenu");
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
              System.out.println("Level completed!");
              loadNextLevel();
            },
            () -> {
              System.out.println("Game over!");
              setGameState(GameStateId.GAME_OVER);
            });

    // Create GameStateFactory and stack
    gameStateFactory = new GameStateFactory(stateContext);
    gameStateStack = new GameStateStack(stateContext, gameStateFactory);
    gameStateStack.requestPush(MENU);

    System.out.println("State Management initialized");
  }

  /** Safely transitions to a screen, checking if it exists first. */
  private void safeGotoScreen(String screenName) {
    if (nifty == null) {
      System.err.println("Nifty not initialized; cannot go to screen: " + screenName);
      return;
    }
    try {
      if (nifty.getScreen(screenName) == null) {
        System.err.println("Screen '" + screenName + "' not found");
        return;
      }
      nifty.gotoScreen(screenName);
    } catch (Exception e) {
      System.err.println("Failed to switch to screen '" + screenName + "': " + e.getMessage());
    }
  }

  // ============== Game State Management ==============

  /** Sets the current game state and triggers any state-specific initialization. */
  private void setGameState(GameStateId newState) {
    currentGameState = newState;

    if (gameStateStack != null) {
      if (newState == PLAYING) {
        initializeLevel();
      }

      switch (newState) {
        case MENU:
          gameStateStack.requestPush(MENU);
          break;
        case PLAYING:
          gameStateStack.requestPush(PLAYING);
          break;
        case PAUSED:
          gameStateStack.requestPush(GameStateId.PAUSED);
          break;
        case GAME_OVER:
          gameStateStack.requestPush(GAME_OVER);
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
    // Scale texture coordinates so larger walls tile the brick texture instead of stretching
    wallShape.scaleTextureCoordinates(
        new Vector2f(Math.max(1f, wall.width), Math.max(1f, wall.height)));
    Geometry wallGeom = new Geometry("Wall_" + Math.abs(wall.position.hashCode()), wallShape);

    com.jme3.util.TangentBinormalGenerator.generate(wallGeom.getMesh());

    // Textured material using static resources
    Material mat =
        materialManager.getTexturedWallMaterial(Math.abs((long) wall.position.hashCode()));

    wallGeom.setMaterial(mat);
    wallGeom.setShadowMode(com.jme3.renderer.queue.RenderQueue.ShadowMode.CastAndReceive);
    wallGeom.setLocalTranslation(wall.position);
    platformsNode.attachChild(wallGeom);
  }

  public GameStateId getCurrentGameState() {
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
          if (currentGameState == PLAYING && !jumpPressed) {
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
          currentGameState = GameStateId.PAUSED;
          gameStateStack.requestPush(GameStateId.PAUSED);
          break;
        case PAUSED:
          currentGameState = PLAYING;
          gameStateStack.requestPop();
          gameStateService.resumeGame();
          break;
        case MENU:
        case GAME_OVER:
          currentGameState = MENU;
          gameStateStack.requestPush(MENU);
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

      if (currentGameState != GameStateId.PLAYING || playerController == null) {
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
    setGameState(GameStateId.PLAYING);
  }

  public void play() {
    menuVisible = false;
    flyCam.setDragToRotate(false);
    inputManager.setCursorVisible(false);
    setGameState(GameStateId.PLAYING);
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
      setGameState(GameStateId.PLAYING);
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
    if (playerEffectNode != null) {
      rootNode.detachChild(playerEffectNode);
      playerEffectNode = null;
      playerExplosionEmitter = null;
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

    // Attach to root node so it's part of the scene
    rootNode.attachChild(playerVisual);

    // Create and attach a looping particle emitter that follows the player
    createPlayerEffectNode();

    // Set initial position
    if (playerController != null) {
      Vector3f pos = playerController.getPosition();
      playerVisual.setLocalTranslation(pos);
      if (playerEffectNode != null) {
        playerEffectNode.setLocalTranslation(pos.add(0, playerHeight * 0.5f, 0));
      }
    }
  }

  private void createPlayerEffectNode() {
    if (playerEffectNode != null) {
      rootNode.detachChild(playerEffectNode);
    }
    playerEffectNode = new Node("PlayerEffectNode");

    // Configure particle emitter using Explosion.png sprite sheet
    playerExplosionEmitter =
        new ParticleEmitter("PlayerExplosion", ParticleMesh.Type.Triangle, 128);
    playerExplosionEmitter.setImagesX(4); // Explosion.png grid (columns)
    playerExplosionEmitter.setImagesY(4); // Explosion.png grid (rows)
    playerExplosionEmitter.setSelectRandomImage(true);
    playerExplosionEmitter.setRotateSpeed(6f);
    playerExplosionEmitter.setGravity(0, 0, 0);
    playerExplosionEmitter.setLowLife(0.6f);
    playerExplosionEmitter.setHighLife(1.2f);
    playerExplosionEmitter.setStartSize(0.35f);
    playerExplosionEmitter.setEndSize(0.15f);
    playerExplosionEmitter.setParticlesPerSec(45f);
    playerExplosionEmitter.setRandomAngle(true);
    playerExplosionEmitter.setFacingVelocity(true);
    playerExplosionEmitter.setVelocityVariation(0.6f);
    playerExplosionEmitter.getParticleInfluencer().setInitialVelocity(new Vector3f(0, 2.2f, 0));

    Material explosionMat = new Material(assetManager, "Common/MatDefs/Misc/Particle.j3md");
    explosionMat.setTexture("Texture", assetManager.loadTexture("static/Explosion.png"));
    explosionMat.setBoolean("PointSprite", false);
    playerExplosionEmitter.setMaterial(explosionMat);

    // Colors fade from bright orange to transparent
    playerExplosionEmitter.setStartColor(new ColorRGBA(1f, 0.7f, 0.2f, 0.8f));
    playerExplosionEmitter.setEndColor(new ColorRGBA(1f, 0.2f, 0.1f, 0f));

    playerEffectNode.attachChild(playerExplosionEmitter);
    rootNode.attachChild(playerEffectNode);
  }

  private void updatePlayerEffect(Vector3f playerPos) {
    if (playerEffectNode == null || playerExplosionEmitter == null || playerController == null) {
      return;
    }

    // Enable emission during active gameplay
    boolean active = currentGameState == GameStateId.PLAYING;
    playerExplosionEmitter.setParticlesPerSec(active ? 45f : 0f);
    if (!active) {
      return;
    }

    // Compute trailing offset behind movement direction
    Vector3f dir = playerController.getVelocity();
    if (dir == null || dir.lengthSquared() < 0.0001f) {
      dir = playerController.getRotation().mult(Vector3f.UNIT_Z); // fallback to facing
    }
    if (dir.lengthSquared() > 0.0001f) {
      dir = dir.normalize();
    }

    float offsetBack = 0.7f;
    float offsetUp = 0.6f;
    Vector3f effectPos = playerPos.add(dir.mult(-offsetBack)).addLocal(0, offsetUp, 0);
    playerEffectNode.setLocalTranslation(effectPos);
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

    // Keep player visual/effects in sync during gameplay
    if (currentGameState == GameStateId.PLAYING && playerController != null) {
      updatePlayerVisual();
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

    updatePlayerEffect(playerPos);
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
