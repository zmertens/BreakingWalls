package com.flipsandale.gui;

import com.flipsandale.game.GameStateService;
import de.lessvoid.nifty.Nifty;
import de.lessvoid.nifty.builder.LayerBuilder;
import de.lessvoid.nifty.builder.PanelBuilder;
import de.lessvoid.nifty.builder.ScreenBuilder;
import de.lessvoid.nifty.builder.TextBuilder;
import de.lessvoid.nifty.controls.button.builder.ButtonBuilder;
import de.lessvoid.nifty.controls.label.builder.LabelBuilder;
import de.lessvoid.nifty.screen.ScreenController;

public class NiftyGuiBuilder {

  private final Nifty nifty;
  private final ScreenController screenController;

  public NiftyGuiBuilder(Nifty nifty, ScreenController screenController) {
    this.nifty = nifty;
    this.screenController = screenController;
  }

  public void buildMainMenuScreen() {
    nifty.addScreen(
        "mainMenu",
        new ScreenBuilder("mainMenu") {
          {
            controller(screenController);

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

  public void buildSettingsScreen() {
    nifty.addScreen(
        "settings",
        new ScreenBuilder("settings") {
          {
            controller(screenController);

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

  public void buildPauseMenuScreen(
      ScreenController controller, GameUIManager gameUIManager, GameStateService gameStateService) {
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

  public void buildOptionsMenuScreen(
      ScreenController controller, GameUIManager gameUIManager, GameStateService gameStateService) {
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
                                            marginBottom("5px");
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

  public void buildHelpScreen(
      ScreenController controller, GameUIManager gameUIManager, GameStateService gameStateService) {
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

  public void buildEmptyScreen() {
    nifty.addScreen(
        "empty",
        new ScreenBuilder("empty") {
          {
            controller(screenController);
            layer(
                new LayerBuilder("empty") {
                  {
                    childLayoutCenter();
                  }
                });
          }
        }.build(nifty));
  }
}
