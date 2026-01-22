package com.flipsandale.game;

import java.util.LinkedList;
import java.util.Queue;

/**
 * A simple queue class for managing game commands. Commands are stored in FIFO (First-In-First-Out)
 * order and can be executed in sequence during game updates.
 */
public class CommandQueue {
  private final Queue<Command> queue;

  /** Creates a new CommandQueue with an empty command list. */
  public CommandQueue() {
    this.queue = new LinkedList<>();
  }

  /**
   * Adds a command to the end of the queue.
   *
   * @param command the command to add
   */
  public void push(Command command) {
    queue.add(command);
  }

  /**
   * Removes and returns the command at the front of the queue.
   *
   * @return the next command in the queue, or null if the queue is empty
   */
  public Command pop() {
    return queue.poll();
  }

  /**
   * Checks if the queue is empty.
   *
   * @return true if the queue contains no commands, false otherwise
   */
  public boolean isEmpty() {
    return queue.isEmpty();
  }
}
