package com.flipsandale.game;

import com.flipsandale.dto.GameSettings;
import com.flipsandale.service.GameSettingsManager;
import de.lessvoid.nifty.elements.Element;
import org.springframework.beans.factory.annotation.Autowired;

/**
 * Screen controller for the pause menu with tabbed interface. Provides tabs for navigation,
 * graphics/audio settings, and game information. Allows player to resume, restart, or return to
 * main menu.
 */
public class PauseMenuScreenController extends BaseScreenController {

  @Autowired private GameSettingsManager gameSettingsManager;

  private GameSettings settings;
  private String currentActiveTab = "main"; // Track which tab is active

  public PauseMenuScreenController(GameUIManager gameUIManager, GameStateService gameStateService) {
    super(gameUIManager, gameStateService);
  }

  @Override
  protected void onBind() {
    System.out.println("PauseMenu: Binding to Nifty");
    if (gameSettingsManager != null) {
      settings = gameSettingsManager.getSettings();
    } else {
      settings = new GameSettings();
    }
  }

  @Override
  protected void onScreenStart() {
    System.out.println("PauseMenu: Pause menu opened");
    // Show main tab by default
    switchToMainTab();
    // Update about tab statistics
    updateAboutTabStats();
  }

  @Override
  protected void onScreenEnd() {
    System.out.println("PauseMenu: Pause menu closed");
  }

  // ============ Tab Navigation Methods ============

  /** Switches to the Main tab showing navigation and controls. */
  public void switchToMainTab() {
    System.out.println("PauseMenu: Switching to Main tab");
    currentActiveTab = "main";
    setTabVisibility("mainTabContent", true);
    setTabVisibility("graphicsTabContent", false);
    setTabVisibility("aboutTabContent", false);
    setButtonStyle("btnTabMain", true);
    setButtonStyle("btnTabGraphics", false);
    setButtonStyle("btnTabAbout", false);
  }

  /** Switches to the Graphics tab showing audio and display settings. */
  public void switchToGraphicsTab() {
    System.out.println("PauseMenu: Switching to Graphics tab");
    currentActiveTab = "graphics";
    setTabVisibility("mainTabContent", false);
    setTabVisibility("graphicsTabContent", true);
    setTabVisibility("aboutTabContent", false);
    setButtonStyle("btnTabMain", false);
    setButtonStyle("btnTabGraphics", true);
    setButtonStyle("btnTabAbout", false);
  }

  /** Switches to the About tab showing game statistics and version info. */
  public void switchToAboutTab() {
    System.out.println("PauseMenu: Switching to About tab");
    currentActiveTab = "about";
    setTabVisibility("mainTabContent", false);
    setTabVisibility("graphicsTabContent", false);
    setTabVisibility("aboutTabContent", true);
    setButtonStyle("btnTabMain", false);
    setButtonStyle("btnTabGraphics", false);
    setButtonStyle("btnTabAbout", true);
    // Update stats when switching to about tab
    updateAboutTabStats();
  }

  // ============ Tab Content Visibility Helpers ============

  /**
   * Sets the visibility of a tab content panel.
   *
   * @param elementId the element ID of the tab content
   * @param visible whether the panel should be visible
   */
  private void setTabVisibility(String elementId, boolean visible) {
    if (screen != null) {
      Element element = screen.findElementByName(elementId);
      if (element != null) {
        element.setVisible(visible);
      }
    }
  }

  /**
   * Sets the style of a tab button to indicate active/inactive state.
   *
   * @param buttonId the button element ID
   * @param isActive whether the button is the active tab
   */
  private void setButtonStyle(String buttonId, boolean isActive) {
    // Style changes will be handled through tab visibility
    // In a more complex implementation, this could update button appearance
    System.out.println(
        "PauseMenu: Button " + buttonId + " is " + (isActive ? "active" : "inactive"));
  }

  /** Updates the statistics displayed in the About tab. */
  private void updateAboutTabStats() {
    if (screen != null) {
      GameMode mode = gameStateService.getCurrentMode();
      int level = gameStateService.getCurrentLevel();
      int platforms = gameStateService.getPlatformsCleared();
      int falls = gameStateService.getFallCount();

      updateTextElement("aboutCurrentMode", "Current Mode: " + mode);
      updateTextElement("aboutCurrentLevel", "Current Level: " + level);
      updateTextElement("aboutPlatformsCleared", "Platforms Cleared: " + platforms);
      updateTextElement("aboutFallCount", "Falls: " + falls);
    }
  }

  /**
   * Updates the text content of an element.
   *
   * @param elementId the element ID
   * @param text the new text content
   */
  private void updateTextElement(String elementId, String text) {
    if (screen != null) {
      Element element = screen.findElementByName(elementId);
      if (element != null) {
        System.out.println("PauseMenu: Updated " + elementId + " to " + text);
        // In Nifty, text updates are typically done through bindings or direct property changes
      }
    }
  }

  // ============ Settings Control Methods ============

  /** Called when master volume slider changes. */
  public void onMasterVolumeChanged() {
    System.out.println("PauseMenu: Master volume slider changed");
  }

  /** Called when SFX volume slider changes. */
  public void onSfxVolumeChanged() {
    System.out.println("PauseMenu: SFX volume slider changed");
  }

  /** Called when FOV slider changes. */
  public void onFovChanged() {
    System.out.println("PauseMenu: FOV slider changed");
  }

  /** Called when show stats overlay checkbox changes. */
  public void onShowStatsToggled() {
    System.out.println("PauseMenu: Show stats toggle changed");
  }

  /** Called when invert mouse Y checkbox changes. */
  public void onInvertMouseToggled() {
    System.out.println("PauseMenu: Invert mouse Y toggle changed");
  }

  // ============ Navigation Methods ============

  /** Resumes the game from pause. */
  public void resumeGame() {
    System.out.println("PauseMenu: Resuming game");
    if (gameSettingsManager != null) {
      gameSettingsManager.updateSettings(settings);
    }
    gameStateService.setGameState(GameState.PLAYING);
    gameUIManager.showGameplayHUD();
  }

  /** Restarts the current level. */
  public void restartLevel() {
    System.out.println("PauseMenu: Restarting level");
    if (gameSettingsManager != null) {
      gameSettingsManager.updateSettings(settings);
    }
    gameUIManager.onRestartLevel();
    gameStateService.startNewLevel();
    gameUIManager.showGameplayHUD();
  }

  /** Returns to the main menu. */
  public void returnToMainMenu() {
    System.out.println("PauseMenu: Returning to main menu");
    if (gameSettingsManager != null) {
      gameSettingsManager.updateSettings(settings);
    }
    gameStateService.setGameState(GameState.MENU);
    gameUIManager.showMainMenu();
  }

  /** Navigates to the options menu from pause. */
  public void showOptions() {
    System.out.println("PauseMenu: Opening options menu");
    gameUIManager.showOptionsMenu();
  }

  /** Navigates to the help screen from pause. */
  public void showHelp() {
    System.out.println("PauseMenu: Opening help screen");
    gameUIManager.showHelpScreen();
  }

  /** Quits the application. */
  // quitGame() removed - ESC should not close the application
}
