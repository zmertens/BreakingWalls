package com.flipsandale.game;

import com.jme3.asset.AssetManager;
import com.jme3.audio.AudioRenderer;
import com.jme3.input.InputManager;
import com.jme3.niftygui.NiftyJmeDisplay;
import com.jme3.renderer.ViewPort;

/**
 * An extension of NiftyJmeDisplay that safely handles NullPointerException when Nifty's
 * getCurrentScreen() is null during input event processing.
 */
public class SafeNiftyJmeDisplay extends NiftyJmeDisplay {
  private SafeNiftyJmeDisplay(
      AssetManager assetManager,
      InputManager inputManager,
      AudioRenderer audioRenderer,
      ViewPort viewport) {
    super(assetManager, inputManager, audioRenderer, viewport);
  }

  /** Factory method to create a safe Nifty display. */
  public static SafeNiftyJmeDisplay newNiftyJmeDisplay(
      AssetManager assetManager,
      InputManager inputManager,
      AudioRenderer audioRenderer,
      ViewPort viewport) {
    return new SafeNiftyJmeDisplay(assetManager, inputManager, audioRenderer, viewport);
  }

  /**
   * Override the processor interface method to catch NPE during input processing. Provides a second
   * layer of protection beyond NiftyInputProtector.
   */
  @Override
  public void reshape(ViewPort vp, int w, int h) {
    try {
      super.reshape(vp, w, h);
    } catch (NullPointerException e) {
      // Suppress NPE when Nifty's screen is null during transitions
      System.err.println("Note: Nifty reshape skipped (null screen)");
    }
  }
}
