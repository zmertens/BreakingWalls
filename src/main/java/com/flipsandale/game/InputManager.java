package com.flipsandale.game;

import com.jme3.input.KeyInput;
import com.jme3.input.controls.ActionListener;
import com.jme3.input.controls.AnalogListener;
import com.jme3.input.controls.JoyAxisTrigger;
import com.jme3.input.controls.JoyButtonTrigger;
import com.jme3.input.controls.KeyTrigger;
import com.jme3.input.controls.MouseButtonTrigger;

/**
 * Abstracts input handling across keyboard, mouse, joystick, and touch devices. Maps
 * platform-specific inputs (spacebar, mouse click, joystick A button) to logical game actions.
 */
public class InputManager {
  private final com.jme3.input.InputManager jmeInputManager;
  private InputListener listener;

  // Input action identifiers
  public static final String ACTION_JUMP = "Jump";
  public static final String ACTION_PAUSE = "Pause";
  public static final String ACTION_CAMERA_ROTATE_LEFT = "CameraRotateLeft";
  public static final String ACTION_CAMERA_ROTATE_RIGHT = "CameraRotateRight";
  public static final String ACTION_PERSPECTIVE_TOGGLE = "PerspectiveToggle";
  public static final String ACTION_MENU_TOGGLE = "MenuToggle";

  public interface InputListener {
    void onActionPressed(String action);

    void onActionReleased(String action);

    void onAnalogUpdate(String action, float value);
  }

  public InputManager(com.jme3.input.InputManager jmeInputManager) {
    this.jmeInputManager = jmeInputManager;
  }

  public void setListener(InputListener listener) {
    this.listener = listener;
  }

  public void setupInputMappings() {
    // Jump action: Spacebar, Mouse Left Click, Joystick A button
    jmeInputManager.addMapping(
        ACTION_JUMP,
        new KeyTrigger(KeyInput.KEY_SPACE),
        new MouseButtonTrigger(com.jme3.input.MouseInput.BUTTON_LEFT),
        new JoyButtonTrigger(0, 0)); // Joystick 0, Button 0 (A button)

    // Pause action: ESC key, Joystick Start button
    jmeInputManager.addMapping(
        ACTION_PAUSE,
        new KeyTrigger(KeyInput.KEY_ESCAPE),
        new JoyButtonTrigger(0, 7)); // Joystick Start button

    // Camera rotation: Mouse horizontal movement / Right stick
    jmeInputManager.addMapping(
        ACTION_CAMERA_ROTATE_LEFT,
        new KeyTrigger(KeyInput.KEY_A),
        new JoyAxisTrigger(0, 2, true)); // Right stick left

    jmeInputManager.addMapping(
        ACTION_CAMERA_ROTATE_RIGHT,
        new KeyTrigger(KeyInput.KEY_D),
        new JoyAxisTrigger(0, 2, false)); // Right stick right

    // Perspective toggle: P key, Joystick Y button
    jmeInputManager.addMapping(
        ACTION_PERSPECTIVE_TOGGLE,
        new KeyTrigger(KeyInput.KEY_P),
        new JoyButtonTrigger(0, 3)); // Joystick Y button

    // Menu toggle: M key, Joystick Menu button
    jmeInputManager.addMapping(
        ACTION_MENU_TOGGLE,
        new KeyTrigger(KeyInput.KEY_M),
        new JoyButtonTrigger(0, 6)); // Joystick Menu button

    // Register action and analog listeners
    ActionListener actionListener =
        new ActionListener() {
          @Override
          public void onAction(String name, boolean isPressed, float tpf) {
            if (listener != null) {
              if (isPressed) {
                listener.onActionPressed(name);
              } else {
                listener.onActionReleased(name);
              }
            }
          }
        };

    AnalogListener analogListener =
        new AnalogListener() {
          @Override
          public void onAnalog(String name, float value, float tpf) {
            if (listener != null) {
              listener.onAnalogUpdate(name, value);
            }
          }
        };

    jmeInputManager.addListener(
        actionListener, ACTION_JUMP, ACTION_PAUSE, ACTION_PERSPECTIVE_TOGGLE, ACTION_MENU_TOGGLE);

    jmeInputManager.addListener(
        analogListener, ACTION_CAMERA_ROTATE_LEFT, ACTION_CAMERA_ROTATE_RIGHT);
  }

  public void removeInputMappings() {
    jmeInputManager.deleteMapping(ACTION_JUMP);
    jmeInputManager.deleteMapping(ACTION_PAUSE);
    jmeInputManager.deleteMapping(ACTION_CAMERA_ROTATE_LEFT);
    jmeInputManager.deleteMapping(ACTION_CAMERA_ROTATE_RIGHT);
    jmeInputManager.deleteMapping(ACTION_PERSPECTIVE_TOGGLE);
    jmeInputManager.deleteMapping(ACTION_MENU_TOGGLE);
  }

  public void setCursorVisible(boolean visible) {
    jmeInputManager.setCursorVisible(visible);
  }
}
