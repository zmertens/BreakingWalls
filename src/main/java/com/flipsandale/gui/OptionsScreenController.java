package com.flipsandale.gui;

import com.flipsandale.game.GameStateService;
import de.lessvoid.nifty.Nifty;
import de.lessvoid.nifty.screen.Screen;
import de.lessvoid.nifty.screen.ScreenController;

public class OptionsScreenController implements ScreenController {

  private final GameUIManager gameUIManager;
  private final GameStateService gameStateService;

  public OptionsScreenController(GameUIManager gameUIManager, GameStateService gameStateService) {
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

  public void setMasterVolume(float volume) {
    // Logic to set master volume
  }

  public void setMusicVolume(float volume) {
    // Logic to set music volume
  }

  public void setSfxVolume(float volume) {
    // Logic to set SFX volume
  }

  public void toggleVSync() {
    // Logic to toggle VSync
  }

  public void toggleFullscreen() {
    // Logic to toggle fullscreen
  }
}
