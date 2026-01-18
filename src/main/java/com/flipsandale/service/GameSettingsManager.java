package com.flipsandale.service;

import com.flipsandale.dto.GameSettings;
import java.io.*;
import java.util.ArrayList;
import java.util.List;
import org.springframework.stereotype.Service;

/**
 * Manages game settings persistence and retrieval. Handles loading and saving settings from a
 * configuration file, and provides access to current settings.
 */
@Service
public class GameSettingsManager {

  private static final String SETTINGS_FILE = "breaking_walls_settings.cfg";

  private GameSettings currentSettings;
  private List<SettingsChangeListener> listeners = new ArrayList<>();

  public interface SettingsChangeListener {
    void onSettingsChanged(GameSettings newSettings);
  }

  public GameSettingsManager() {
    this.currentSettings = new GameSettings();
    loadSettings();
  }

  /**
   * Gets the current game settings.
   *
   * @return the current GameSettings instance
   */
  public GameSettings getSettings() {
    return currentSettings;
  }

  /**
   * Updates a setting and notifies listeners.
   *
   * @param settings the new settings to apply
   */
  public void updateSettings(GameSettings settings) {
    this.currentSettings = settings;
    notifyListeners();
    saveSettings();
  }

  /** Resets all settings to defaults and saves. */
  public void resetToDefaults() {
    currentSettings.resetToDefaults();
    notifyListeners();
    saveSettings();
    System.out.println("GameSettings: Reset to defaults");
  }

  /**
   * Registers a listener for settings changes.
   *
   * @param listener the listener to register
   */
  public void addSettingsChangeListener(SettingsChangeListener listener) {
    listeners.add(listener);
  }

  /**
   * Removes a listener from the settings change listeners.
   *
   * @param listener the listener to remove
   */
  public void removeSettingsChangeListener(SettingsChangeListener listener) {
    listeners.remove(listener);
  }

  /** Notifies all registered listeners of a settings change. */
  private void notifyListeners() {
    for (SettingsChangeListener listener : listeners) {
      listener.onSettingsChanged(currentSettings);
    }
  }

  /** Loads settings from the configuration file. If the file doesn't exist, uses defaults. */
  private void loadSettings() {
    File settingsFile = new File(SETTINGS_FILE);

    if (!settingsFile.exists()) {
      System.out.println("GameSettings: Settings file not found, using defaults");
      currentSettings = new GameSettings();
      return;
    }

    try (ObjectInputStream ois = new ObjectInputStream(new FileInputStream(settingsFile))) {
      currentSettings = (GameSettings) ois.readObject();
      System.out.println("GameSettings: Loaded settings from " + SETTINGS_FILE);
    } catch (IOException | ClassNotFoundException e) {
      System.err.println("GameSettings: Error loading settings, using defaults: " + e.getMessage());
      currentSettings = new GameSettings();
    }
  }

  /** Saves the current settings to the configuration file. */
  public void saveSettings() {
    try (ObjectOutputStream oos = new ObjectOutputStream(new FileOutputStream(SETTINGS_FILE))) {
      oos.writeObject(currentSettings);
      System.out.println("GameSettings: Saved settings to " + SETTINGS_FILE);
    } catch (IOException e) {
      System.err.println("GameSettings: Error saving settings: " + e.getMessage());
    }
  }
}
