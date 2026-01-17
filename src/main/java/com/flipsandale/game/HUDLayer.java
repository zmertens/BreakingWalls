package com.flipsandale.game;

import com.jme3.font.BitmapFont;
import com.jme3.font.BitmapText;
import com.jme3.math.ColorRGBA;
import com.jme3.scene.Node;

/**
 * Renders the heads-up display (HUD) during gameplay. Displays score, time, level, and platform
 * count based on game mode.
 */
public class HUDLayer {

  private BitmapText hudText;
  private HUDManager hudManager;
  private Node guiNode;
  private BitmapFont guiFont;

  // Text update tracking to avoid excessive string rebuilds
  private String lastDisplayedText = "";

  public HUDLayer(Node guiNode, BitmapFont guiFont, HUDManager hudManager) {
    this.guiNode = guiNode;
    this.guiFont = guiFont;
    this.hudManager = hudManager;

    // Create the HUD text element
    hudText = new BitmapText(guiFont, false);
    hudText.setText("BREAKING WALLS");
    hudText.setColor(ColorRGBA.White);
    // Position at top-left corner (viewport assumed 1280x720)
    hudText.setLocalTranslation(10, 710, 0);
    guiNode.attachChild(hudText);
  }

  /** Updates the HUD display each frame. */
  public void update() {
    String currentContent = hudManager.getHUDContent();

    // Only update text if content has changed (avoid excessive allocations)
    if (!currentContent.equals(lastDisplayedText)) {
      hudText.setText(currentContent);
      lastDisplayedText = currentContent;
    }
  }

  /** Positions the HUD at specified screen coordinates. */
  public void setPosition(float x, float y) {
    if (hudText != null) {
      hudText.setLocalTranslation(x, y, 0);
    }
  }

  /** Changes the HUD text color. */
  public void setColor(ColorRGBA color) {
    if (hudText != null) {
      hudText.setColor(color);
    }
  }

  /** Shows or hides the HUD. */
  public void setVisible(boolean visible) {
    if (hudText != null) {
      hudText.setCullHint(
          visible ? com.jme3.scene.Spatial.CullHint.Never : com.jme3.scene.Spatial.CullHint.Always);
    }
  }

  /** Gets the raw HUD text element for advanced customization. */
  public BitmapText getHUDText() {
    return hudText;
  }

  /** Detaches the HUD from the scene. */
  public void cleanup() {
    if (hudText != null) {
      guiNode.detachChild(hudText);
    }
  }
}
