package com.flipsandale.gui;

import com.flipsandale.game.GameStateService;
import com.flipsandale.game.HUDManager;
import de.lessvoid.nifty.Nifty;

public class GameUIManager {

  private final Nifty nifty;
  private final HUDManager hudManager;
  private final GameStateService gameStateService;

  public GameUIManager(Nifty nifty, HUDManager hudManager, GameStateService gameStateService) {
    this.nifty = nifty;
    this.hudManager = hudManager;
    this.gameStateService = gameStateService;
  }

  public void resumeGame() {
    nifty.gotoScreen("empty");
    gameStateService.resumeGame();
  }

  public void restartLevel() {
    nifty.gotoScreen("empty");
  }

  public void showOptions() {
    nifty.gotoScreen("optionsMenu");
  }

  public void showHelp() {
    nifty.gotoScreen("helpScreen");
  }

  public void returnToMainMenu() {
    nifty.gotoScreen("mainMenu");
  }
}
