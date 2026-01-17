package com.flipsandale.game;

import com.jme3.input.RawInputListener;
import com.jme3.input.event.*;
import de.lessvoid.nifty.Nifty;
import java.util.concurrent.atomic.AtomicLong;

/**
 * A raw input listener that protects against NullPointerException by consuming mouse input when
 * Nifty has no current screen.
 *
 * <p>This listener must be added BEFORE NiftyJmeDisplay's processor is added to the viewport, so it
 * gets priority in processing input events. It tracks when screens are missing and aggressively
 * suppresses mouse input during vulnerable periods.
 */
public class NiftyInputProtector implements RawInputListener {
  private final Nifty nifty;
  private volatile boolean blockInput = false;
  private final AtomicLong lastNullScreenTime = new AtomicLong(System.currentTimeMillis());
  // Extended grace period - 200ms to ensure screen transitions complete
  private static final long GRACE_PERIOD_MS = 200;

  public NiftyInputProtector(Nifty nifty) {
    this.nifty = nifty;
  }

  public void setBlockInput(boolean block) {
    this.blockInput = block;
  }

  private boolean isNullScreenRecent() {
    long timeSinceNull = System.currentTimeMillis() - lastNullScreenTime.get();
    return timeSinceNull < GRACE_PERIOD_MS;
  }

  @Override
  public void beginInput() {
    // At the start of input processing, if screen is null, block input for this frame
    if (nifty.getCurrentScreen() == null) {
      blockInput = true;
      lastNullScreenTime.set(System.currentTimeMillis());
    }
  }

  @Override
  public void endInput() {
    // During endInput, if we don't have a screen, block any mouse events
    // that might be processed during Nifty.update()
    if (nifty.getCurrentScreen() == null) {
      blockInput = true;
      lastNullScreenTime.set(System.currentTimeMillis());
    }
  }

  @Override
  public void onMouseMotionEvent(MouseMotionEvent evt) {
    // Consume mouse motion if:
    // 1. We're blocking input from detecting a null screen
    // 2. Screen is currently null
    // 3. We recently detected a null screen (grace period)
    if (blockInput || nifty.getCurrentScreen() == null || isNullScreenRecent()) {
      evt.setConsumed();
      blockInput = false;
    }
  }

  @Override
  public void onMouseButtonEvent(MouseButtonEvent evt) {
    // Consume mouse buttons if:
    // 1. We're blocking input from detecting a null screen
    // 2. Screen is currently null
    // 3. We recently detected a null screen (grace period)
    if (blockInput || nifty.getCurrentScreen() == null || isNullScreenRecent()) {
      evt.setConsumed();
      blockInput = false;
    }
  }

  @Override
  public void onKeyEvent(KeyInputEvent evt) {}

  @Override
  public void onJoyAxisEvent(JoyAxisEvent evt) {}

  @Override
  public void onJoyButtonEvent(JoyButtonEvent evt) {}

  @Override
  public void onTouchEvent(TouchEvent evt) {}
}
