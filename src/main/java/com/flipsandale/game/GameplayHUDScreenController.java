package com.flipsandale.game;

import de.lessvoid.nifty.Nifty;
import de.lessvoid.nifty.elements.Element;
import de.lessvoid.nifty.screen.Screen;

/** Screen controller for the in-game HUD overlay. Updates real-time metrics during gameplay. */
public class GameplayHUDScreenController implements de.lessvoid.nifty.screen.ScreenController {

  private Nifty nifty;
  private Screen screen;
  private HUDManager hudManager;
  private GameStateService gameStateService;

  // UI elements for dynamic updates
  private Element levelText;
  private Element scoreText;
  private Element timeText;
  private Element platformsText;
  private Element fallsText;

  public GameplayHUDScreenController(HUDManager hudManager, GameStateService gameStateService) {
    this.hudManager = hudManager;
    this.gameStateService = gameStateService;
  }

  @Override
  public void bind(Nifty nifty, Screen screen) {
    this.nifty = nifty;
    this.screen = screen;

    // Get references to text elements for dynamic updates
    levelText = screen.findElementByName("levelText");
    scoreText = screen.findElementByName("scoreText");
    timeText = screen.findElementByName("timeText");
    platformsText = screen.findElementByName("platformsText");
    fallsText = screen.findElementByName("fallsText");

    System.out.println("GameplayHUD: Binding to Nifty");
  }

  @Override
  public void onStartScreen() {
    System.out.println("GameplayHUD: HUD display started");
  }

  @Override
  public void onEndScreen() {
    System.out.println("GameplayHUD: HUD display ended");
  }

  /** Updates HUD elements with current game state (called from game loop). */
  public void updateHUD() {
    // Dynamic text updates are handled through GameStateListener pattern
    // See HUDManager and GameUIManager for event-driven updates
  }

  /** Gets HUD content as formatted string for display. */
  public String getHUDContent() {
    return hudManager.getHUDContent();
  }
}
