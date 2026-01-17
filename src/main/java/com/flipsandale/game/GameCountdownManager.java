package com.flipsandale.game;

import com.jme3.font.BitmapFont;
import com.jme3.font.BitmapText;
import com.jme3.math.ColorRGBA;
import com.jme3.scene.Node;

/**
 * Manages the countdown timer displayed before the level starts. Shows "3, 2, 1, Go!" sequence
 * giving the player time to prepare before the level begins.
 */
public class GameCountdownManager {
  private final BitmapText countdownText;
  private final int totalCountdownSeconds;
  private float elapsedTime = 0f;
  private boolean isActive = false;
  private CountdownListener listener;
  private int lastDisplayedNumber = -1;

  public interface CountdownListener {
    void onCountdownFinished();
  }

  public GameCountdownManager(Node guiNode, BitmapFont font) {
    this.totalCountdownSeconds = 3; // 3 second countdown

    // Create countdown text
    countdownText = new BitmapText(font);
    countdownText.setText("");
    countdownText.setSize(font.getCharSet().getRenderedSize() * 2); // Make it larger
    countdownText.setColor(ColorRGBA.White);
    countdownText.setLocalTranslation(
        guiNode.getLocalTranslation().x + 400, guiNode.getLocalTranslation().y + 100, 0);
    guiNode.attachChild(countdownText);
  }

  public void startCountdown(CountdownListener listener) {
    this.listener = listener;
    this.isActive = true;
    this.elapsedTime = 0f;
    this.lastDisplayedNumber = -1;
    updateDisplay();
  }

  public void update(float tpf) {
    if (!isActive) {
      return;
    }

    elapsedTime += tpf;

    // Check if countdown is finished
    if (elapsedTime >= totalCountdownSeconds) {
      isActive = false;
      countdownText.setText("");
      if (listener != null) {
        listener.onCountdownFinished();
      }
      return;
    }

    updateDisplay();
  }

  private void updateDisplay() {
    int secondsRemaining = totalCountdownSeconds - (int) elapsedTime;

    if (secondsRemaining != lastDisplayedNumber) {
      lastDisplayedNumber = secondsRemaining;

      if (secondsRemaining > 0) {
        countdownText.setText(String.valueOf(secondsRemaining));
        // Fade color from white to green as we approach zero
        float ratio = 1f - (secondsRemaining / (float) totalCountdownSeconds);
        countdownText.setColor(new ColorRGBA(1f, 1f - ratio, 1f - ratio, 1f)); // Fade to yellow-ish
      } else {
        countdownText.setText("GO!");
        countdownText.setColor(ColorRGBA.Green);
      }
    }
  }

  public boolean isActive() {
    return isActive;
  }

  public void reset() {
    isActive = false;
    elapsedTime = 0f;
    lastDisplayedNumber = -1;
    countdownText.setText("");
  }
}
