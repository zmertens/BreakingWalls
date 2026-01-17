package com.flipsandale.game;

import de.lessvoid.nifty.Nifty;
import de.lessvoid.nifty.screen.Screen;

/**
 * Base class for all Nifty UI screens. Provides common functionality and access to game services.
 */
public abstract class ScreenController implements de.lessvoid.nifty.screen.ScreenController {

  protected Nifty nifty;
  protected Screen screen;
  protected GameUIManager gameUIManager;
  protected GameStateService gameStateService;

  public ScreenController(GameUIManager gameUIManager, GameStateService gameStateService) {
    this.gameUIManager = gameUIManager;
    this.gameStateService = gameStateService;
  }

  @Override
  public void bind(Nifty nifty, Screen screen) {
    this.nifty = nifty;
    this.screen = screen;
    onBind();
  }

  @Override
  public void onStartScreen() {
    onScreenStart();
  }

  @Override
  public void onEndScreen() {
    onScreenEnd();
  }

  /** Called when screen is bound to Nifty. */
  protected abstract void onBind();

  /** Called when screen starts displaying. */
  protected abstract void onScreenStart();

  /** Called when screen stops displaying. */
  protected abstract void onScreenEnd();
}
