package com.flipsandale.game;

/**
 * A functional interface representing a single game action that can be executed. This is used to
 * define commands that can be queued and executed during game updates.
 */
@FunctionalInterface
public interface Command {
  /**
   * Executes this command with the given delta time.
   *
   * @param deltaTime the time elapsed since the last frame, in seconds
   */
  void execute(float deltaTime);
}
