package com.flipsandale.game.state;

/**
 * Defines unique identifiers for each game state. This enum is used by the state factory and state
 * stack to manage and transition between states.
 */
public enum GameStateId {
  MENU,
  PLAYING,
  PAUSED,
  GAME_OVER
}
