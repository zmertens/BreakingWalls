package com.flipsandale.game.state;

/**
 * Factory for creating game state instances. Uses constructor injection of StateContext to provide
 * dependency injection to all created states. This enables states to be instantiated fresh on each
 * push, ensuring clean state.
 */
public class GameStateFactory {

  private final StateContext stateContext;

  public GameStateFactory(StateContext stateContext) {
    this.stateContext = stateContext;
  }

  /**
   * Creates a new GameState instance based on the provided state ID. This factory is responsible
   * for instantiating the correct state class and injecting the shared StateContext.
   *
   * @param stateId The ID of the state to create
   * @return A new GameState instance
   */
  public GameState createState(GameStateId stateId) {
    switch (stateId) {
      case MENU:
        return new MenuState(stateContext);
      case PLAYING:
        return new PlayingState(stateContext);
      case PAUSED:
        return new PausedState(stateContext);
      case GAME_OVER:
        return new GameOverState(stateContext);
      default:
        throw new IllegalArgumentException("Unknown game state ID: " + stateId);
    }
  }
}
