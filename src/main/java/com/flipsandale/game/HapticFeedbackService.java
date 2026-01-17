package com.flipsandale.game;

import org.springframework.stereotype.Service;

/**
 * Service for triggering haptic feedback on game events. Provides haptic responses for jumps,
 * landings, and falls to enhance player immersion.
 */
@Service
public class HapticFeedbackService {

  // Haptic intensity levels (0.0 - 1.0)
  private static final float JUMP_INTENSITY = 0.3f;
  private static final float LANDING_INTENSITY = 0.5f;
  private static final float FALL_INTENSITY = 0.8f;
  private static final float LEVEL_COMPLETE_INTENSITY = 0.6f;

  // Haptic duration in milliseconds
  private static final int JUMP_DURATION = 50;
  private static final int LANDING_DURATION = 100;
  private static final int FALL_DURATION = 200;
  private static final int LEVEL_COMPLETE_DURATION = 150;

  /** Triggers haptic feedback for a jump action. */
  public void onJump() {
    triggerHaptic(JUMP_INTENSITY, JUMP_DURATION);
  }

  /** Triggers haptic feedback for landing on a platform. */
  public void onLanding() {
    triggerHaptic(LANDING_INTENSITY, LANDING_DURATION);
  }

  /** Triggers haptic feedback for falling off a platform. */
  public void onFall() {
    triggerHaptic(FALL_INTENSITY, FALL_DURATION);
  }

  /** Triggers haptic feedback for completing a level. */
  public void onLevelComplete() {
    triggerHaptic(LEVEL_COMPLETE_INTENSITY, LEVEL_COMPLETE_DURATION);
  }

  /**
   * Triggers a haptic pulse with the specified intensity and duration. This is a placeholder that
   * will be integrated with the actual device haptic API.
   *
   * @param intensity The haptic intensity (0.0 - 1.0)
   * @param durationMs The duration in milliseconds
   */
  private void triggerHaptic(float intensity, int durationMs) {
    // TODO: Integrate with Windows haptic API or cross-platform haptic library
    // For now, this is a stub that can be extended with actual haptic implementation
    if (isHapticSupported()) {
      // Placeholder for actual haptic call
      // Example: WindowsTouchInput.vibrate(intensity, durationMs);
    }
  }

  /** Checks if the device supports haptic feedback. */
  private boolean isHapticSupported() {
    // TODO: Detect platform and check for haptic support
    return false; // Disabled for now, enable when haptic library is integrated
  }
}
