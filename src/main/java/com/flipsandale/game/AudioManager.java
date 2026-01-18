package com.flipsandale.game;

import com.jme3.asset.AssetManager;
import com.jme3.audio.AudioData;
import com.jme3.audio.AudioNode;

/**
 * Manages audio playback for the game using JMonkeyEngine's audio engine. Handles loading and
 * playing sounds and music.
 *
 * <p>This class is NOT a Spring bean. It must be instantiated manually in MazeGameApp after the
 * AssetManager becomes available.
 */
public class AudioManager {
  private final AssetManager assetManager;
  private AudioNode loadingMusic;
  private AudioNode sfxSelect;
  private AudioNode sfxThrow;
  private boolean isInitialized = false;

  public AudioManager(AssetManager assetManager) {
    this.assetManager = assetManager;
  }

  /**
   * Initialize audio manager with JME3 asset manager. This must be called after assetManager is
   * available.
   */
  public void initialize() {
    if (isInitialized) {
      return;
    }
    loadingMusic = new AudioNode(assetManager, "static/loading.wav", AudioData.DataType.Buffer);
    sfxSelect = new AudioNode(assetManager, "static/sfx_select.wav", AudioData.DataType.Buffer);
    sfxThrow = new AudioNode(assetManager, "static/sfx_throw.wav", AudioData.DataType.Buffer);
    isInitialized = true;
    System.out.println("✓ AudioManager initialized with loading music");
  }

  /**
   * Play the loading music in a loop.
   *
   * @param rootNode The scene root node to attach the audio node to
   */
  public void playLoadingMusicLoop(com.jme3.scene.Node rootNode) {
    if (!isInitialized || loadingMusic == null) {
      System.err.println("AudioManager not initialized. Call initialize() first.");
      return;
    }

    // Check if audio node is not already attached to rootNode
    if (loadingMusic.getParent() == null || !loadingMusic.getParent().equals(rootNode)) {
      rootNode.attachChild(loadingMusic);
    }

    loadingMusic.setPositional(false); // Allow stereo audio (non-positional background music)
    loadingMusic.setLooping(true);
    loadingMusic.setVolume(1.0f);
    loadingMusic.play();
    System.out.println("🔊 Playing loading music in loop");
  }

  /** Stop the loading music. */
  public void stopLoadingMusic() {
    if (loadingMusic != null && loadingMusic.getStatus().name().equals("Playing")) {
      loadingMusic.stop();
      System.out.println("🔇 Stopped loading music");
    }
  }

  /**
   * Set the volume of the loading music (0.0 to 1.0).
   *
   * @param volume Volume level from 0.0 (silent) to 1.0 (max)
   */
  public void setLoadingMusicVolume(float volume) {
    if (loadingMusic != null) {
      loadingMusic.setVolume(Math.max(0.0f, Math.min(1.0f, volume)));
    }
  }

  /**
   * Check if loading music is currently playing.
   *
   * @return true if the music is playing, false otherwise
   */
  public boolean isLoadingMusicPlaying() {
    return loadingMusic != null && loadingMusic.getStatus().name().equals("Playing");
  }

  /**
   * Play the jump sound effect.
   *
   * @param rootNode The scene root node to attach the audio node to
   */
  public void playJumpSound(com.jme3.scene.Node rootNode) {
    if (!isInitialized || sfxSelect == null) {
      return;
    }
    if (sfxSelect.getParent() == null) {
      rootNode.attachChild(sfxSelect);
    }
    sfxSelect.setPositional(false);
    sfxSelect.setLooping(false);
    sfxSelect.setVolume(0.7f);
    sfxSelect.playInstance();
  }

  /**
   * Play the collision/fall sound effect.
   *
   * @param rootNode The scene root node to attach the audio node to
   */
  public void playCollisionSound(com.jme3.scene.Node rootNode) {
    if (!isInitialized || sfxThrow == null) {
      return;
    }
    if (sfxThrow.getParent() == null) {
      rootNode.attachChild(sfxThrow);
    }
    sfxThrow.setPositional(false);
    sfxThrow.setLooping(false);
    sfxThrow.setVolume(0.7f);
    sfxThrow.playInstance();
  }

  /** Cleanup audio resources. */
  public void cleanup() {
    if (loadingMusic != null) {
      stopLoadingMusic();
      loadingMusic.removeFromParent();
      loadingMusic = null;
    }
    if (sfxSelect != null) {
      sfxSelect.removeFromParent();
      sfxSelect = null;
    }
    if (sfxThrow != null) {
      sfxThrow.removeFromParent();
      sfxThrow = null;
    }
    isInitialized = false;
  }
}
