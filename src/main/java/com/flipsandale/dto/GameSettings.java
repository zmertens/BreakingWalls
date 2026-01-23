package com.flipsandale.dto;

import java.io.Serializable;

/**
 * Data Transfer Object for game settings. Stores all user-configurable game options including
 * graphics, audio, and gameplay preferences.
 */
public class GameSettings implements Serializable {

  private static final long serialVersionUID = 1L;

  // Graphics Settings
  private float masterVolume = 1.0f;
  private float musicVolume = 0.8f;
  private float sfxVolume = 0.9f;
  private float fov = 75.0f;
  private boolean showStatsOverlay = false;
  private boolean vsyncEnabled = true;
  private boolean fullscreenEnabled = false;
  private boolean invertMouseY = false;

  // Gameplay Settings
  private boolean showControlHints = true;
  private float gameSpeed = 1.0f; // 0.5f to 2.0f

  // Display Settings
  private boolean showFallCount = true;
  private boolean showPlatformCount = true;
  private boolean showLevelIndicator = true;

  public GameSettings() {
    // Default constructor with default values
  }

  // ============ Getters and Setters ============

  // Graphics
  public float getMasterVolume() {
    return masterVolume;
  }

  public void setMasterVolume(float volume) {
    this.masterVolume = Math.max(0.0f, Math.min(1.0f, volume));
  }

  public float getMusicVolume() {
    return musicVolume;
  }

  public void setMusicVolume(float volume) {
    this.musicVolume = Math.max(0.0f, Math.min(1.0f, volume));
  }

  public float getSfxVolume() {
    return sfxVolume;
  }

  public void setSfxVolume(float volume) {
    this.sfxVolume = Math.max(0.0f, Math.min(1.0f, volume));
  }

  public float getFov() {
    return fov;
  }

  public void setFov(float fov) {
    this.fov = Math.max(30.0f, Math.min(120.0f, fov));
  }

  public boolean isShowStatsOverlay() {
    return showStatsOverlay;
  }

  public void setShowStatsOverlay(boolean show) {
    this.showStatsOverlay = show;
  }

  public boolean isVsyncEnabled() {
    return vsyncEnabled;
  }

  public void setVsyncEnabled(boolean enabled) {
    this.vsyncEnabled = enabled;
  }

  public boolean isFullscreenEnabled() {
    return fullscreenEnabled;
  }

  public void setFullscreenEnabled(boolean enabled) {
    this.fullscreenEnabled = enabled;
  }

  public boolean isInvertMouseY() {
    return invertMouseY;
  }

  public void setInvertMouseY(boolean invert) {
    this.invertMouseY = invert;
  }

  // Gameplay
  public boolean isShowControlHints() {
    return showControlHints;
  }

  public void setShowControlHints(boolean show) {
    this.showControlHints = show;
  }

  public float getGameSpeed() {
    return gameSpeed;
  }

  public void setGameSpeed(float speed) {
    this.gameSpeed = Math.max(0.5f, Math.min(2.0f, speed));
  }

  // Display
  public boolean isShowFallCount() {
    return showFallCount;
  }

  public void setShowFallCount(boolean show) {
    this.showFallCount = show;
  }

  public boolean isShowPlatformCount() {
    return showPlatformCount;
  }

  public void setShowPlatformCount(boolean show) {
    this.showPlatformCount = show;
  }

  public boolean isShowLevelIndicator() {
    return showLevelIndicator;
  }

  public void setShowLevelIndicator(boolean show) {
    this.showLevelIndicator = show;
  }

  /** Resets all settings to their default values. */
  public void resetToDefaults() {
    masterVolume = 1.0f;
    musicVolume = 0.8f;
    sfxVolume = 0.9f;
    fov = 75.0f;
    showStatsOverlay = false;
    vsyncEnabled = true;
    fullscreenEnabled = false;
    invertMouseY = false;
    showControlHints = true;
    gameSpeed = 1.0f;
    showFallCount = true;
    showPlatformCount = true;
    showLevelIndicator = true;
  }

  @Override
  public String toString() {
    return "GameSettings{"
        + "masterVolume="
        + masterVolume
        + ", musicVolume="
        + musicVolume
        + ", sfxVolume="
        + sfxVolume
        + ", fov="
        + fov
        + ", showStatsOverlay="
        + showStatsOverlay
        + ", vsyncEnabled="
        + vsyncEnabled
        + ", fullscreenEnabled="
        + fullscreenEnabled
        + '}';
  }
}
