package com.flipsandale.gui;

import com.flipsandale.game.GameStateService;
import de.lessvoid.nifty.Nifty;
import de.lessvoid.nifty.screen.Screen;
import de.lessvoid.nifty.screen.ScreenController;

public class PauseMenuScreenController implements ScreenController {

  private final GameUIManager gameUIManager;
  private final GameStateService gameStateService;

  public PauseMenuScreenController(GameUIManager gameUIManager, GameStateService gameStateService) {
    this.gameUIManager = gameUIManager;
    this.gameStateService = gameStateService;
  }

  @Override
  public void bind(Nifty nifty, Screen screen) {}

  @Override
  public void onStartScreen() {}

  @Override
  public void onEndScreen() {}

  public void resumeGame() {
    gameUIManager.resumeGame();
  }

  public void restartLevel() {
    gameUIManager.restartLevel();
  }

  public void showOptions() {
    gameUIManager.showOptions();
  }

  public void showHelp() {
    gameUIManager.showHelp();
  }

  public void returnToMainMenu() {
    gameUIManager.returnToMainMenu();
  }

  public void switchToMainTab() {
    // Logic to switch to the main tab
  }

  public void switchToGraphicsTab() {
    // Logic to switch to the graphics tab
  }

  public void switchToAboutTab() {
    // Logic to switch to the about tab
  }

  public void onVSyncToggled() {
    // Logic to toggle VSync
  }

  public void onFullscreenToggled() {
    // Logic to toggle fullscreen
  }

  public void onMasterVolumeChanged() {
    // Logic to change master volume
  }

  public void onSfxVolumeChanged() {
    // Logic to change SFX volume
  }
}
