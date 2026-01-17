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
  private final Runnable onCloseCallback;
  private String asciiMaze;
  private Node mazeNode;

  // Nifty GUI
  private Nifty nifty;
  private NiftyJmeDisplay niftyDisplay;
  private boolean menuVisible = true;

  // Maze settings
  private String selectedAlgorithm = "sidewinder";
  private int mazeRows = 20;
  private int mazeCols = 20;
  private int mazeSeed = 42;
  private boolean showDistances = true;

  private static final float CELL_SIZE = 1.0f;
  private static final float WALL_HEIGHT = 2.0f;

  public MazeGameApp(MazeService mazeService, Runnable onCloseCallback) {
    this.mazeService = mazeService;
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

    // Create maze node container
    mazeNode = new Node("MazeNode");
    rootNode.attachChild(mazeNode);

    // Set up input mappings
    setupInputs();

    // Set up camera
    setupCamera();

    // Add a floor
    addFloor();

    // Initialize Nifty GUI
    initNiftyGui();

    // Disable fly cam initially (menu mode)
    flyCam.setDragToRotate(true);
    inputManager.setCursorVisible(true);
  }

  private void initNiftyGui() {
    niftyDisplay =
        NiftyJmeDisplay.newNiftyJmeDisplay(assetManager, inputManager, audioRenderer, guiViewPort);
    nifty = niftyDisplay.getNifty();
    guiViewPort.addProcessor(niftyDisplay);

    // Load default styles and controls
    nifty.loadStyleFile("nifty-default-styles.xml");
    nifty.loadControlFile("nifty-default-controls.xml");

    // Build empty screen (for gameplay)
    buildEmptyScreen();

    // Build main menu screen
    buildMainMenuScreen();

    // Build settings screen
    buildSettingsScreen();

    // Show main menu
    nifty.gotoScreen("mainMenu");
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

  private void toggleMenu() {
    menuVisible = !menuVisible;
    if (menuVisible) {
      nifty.gotoScreen("mainMenu");
      flyCam.setDragToRotate(true);
      inputManager.setCursorVisible(true);
    } else {
      nifty.gotoScreen("empty");
      flyCam.setDragToRotate(false);
      inputManager.setCursorVisible(false);
    }
  }

  // ============== Nifty GUI Callbacks ==============

  public void showSettings() {
    nifty.gotoScreen("settings");
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
    nifty.gotoScreen("empty");
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
    generateMaze();
    menuVisible = false;
    nifty.gotoScreen("empty");
    flyCam.setDragToRotate(false);
    inputManager.setCursorVisible(false);
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

  @Override
  public void simpleUpdate(float tpf) {
    // Game loop updates
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
