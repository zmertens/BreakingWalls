package com.flipsandale.game;

/** Screen controller for the help/information screen. Displays game instructions and controls. */
public class HelpScreenController extends BaseScreenController {

  public HelpScreenController(GameUIManager gameUIManager, GameStateService gameStateService) {
    super(gameUIManager, gameStateService);
  }

  @Override
  protected void onBind() {}

  @Override
  protected void onScreenStart() {}

  @Override
  protected void onScreenEnd() {}

  /** Navigates to options menu from help screen. */
  public void showOptions() {
    gameUIManager.showOptionsMenu();
  }

  // Navigation

  public void backToMainMenu() {
    gameUIManager.showMainMenu();
  }

  public void backToPause() {
    gameUIManager.showPauseMenu();
  }
}
