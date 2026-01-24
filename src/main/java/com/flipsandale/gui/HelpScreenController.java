package com.flipsandale.gui;

import com.flipsandale.game.GameStateService;
import de.lessvoid.nifty.Nifty;
import de.lessvoid.nifty.screen.Screen;
import de.lessvoid.nifty.screen.ScreenController;

public class HelpScreenController implements ScreenController {

  private final GameUIManager gameUIManager;
  private final GameStateService gameStateService;

  public HelpScreenController(GameUIManager gameUIManager, GameStateService gameStateService) {
    this.gameUIManager = gameUIManager;
    this.gameStateService = gameStateService;
  }

  @Override
  public void bind(Nifty nifty, Screen screen) {}

  @Override
  public void onStartScreen() {}

  @Override
  public void onEndScreen() {}

  public void backToMainMenu() {
    gameUIManager.returnToMainMenu();
  }
}
