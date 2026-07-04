# About Breaking Walls

An endless puzzle-game with a synthwave theme and futuristic vibes.

  - Players interact with and discover paths and new levels
  - Simple event handling (mouse, touch, keyboard) and haptic feedback
  - Spatialized sound effects
  - Break down the 4th wall and advance with bigger levels and paths and obstacles to overcome

## Configuration

`Gradle` is used for project configuration and builds.

These exteneral dependencies are downloaded automatically with Gradle:

- [Gson](https://github.com/google/gson)
- [imgui-java](https://github.com/SpaiR/imgui-java)
- [JOML](https://github.com/JOML-CI/JOML)
- [de.articdive:jnoise](https://github.com/Articdive/JNoise)
- [lwjgl-openal](https://github.com/LWJGL/lwjgl3)
- [lwjgl-opengl](https://github.com/LWJGL/lwjgl3)
- [lwjgl-glfw](https://github.com/LWJGL/lwjgl3)
- [lwjgl-stb](https://github.com/LWJGL/lwjgl3)

### Config file (`config.json`)

The config lives in the project root (next to `build.gradle`).  Every key is documented inline with a `_<key>_doc` comment field.

#### Configuration Properties

| Key | Default | Effect |
|---|---|---|
| `window_title` | `"JBreaking Walls"` | OS window title bar text |
| `window_width` | `1280` | Initial width in pixels |
| `window_height` | `720` | Initial height in pixels |
| `target_fps` | `60` | Fixed-step physics rate (Hz) |
| `show_debug_overlay` | `false` | Top-right FPS / frame-time panel |
|---|---|---|
| `runner_speed` | `5.0` | Starting scroll speed (world-units/sec) |
| `runner_acceleration` | `0.05` | Speed ramp per second; `0` = constant speed |
| `runner_max_speed` | `25.0` | Hard cap — prevents unplayable speed |
| `runner_jump_velocity` | `14.0` | Upward velocity on jump; higher = floatier arc |
| `runner_gravity` | `-30.0` | Downward acceleration; more negative = snappier landing |
|---|---|---|
| `runner_starting_points` | `100` | Initial health pool |
| `score_per_meter` | `1.0` | Points per world-unit scrolled |
| `score_multiplier_increment` | `0.25` | Multiplier bonus at each milestone |
| `score_milestone_distance` | `100.0` | Distance (world-units) between milestones |
| `score_milestone_bonus` | `50` | Flat point bonus at each milestone |
|---|---|---|
| `runner_pickup_min_value` | `-25` | Most negative pickup value (health drain, purple gem) |
| `runner_pickup_max_value` | `40` | Most positive pickup value (health gain, gold gem) |
| `runner_pickup_spacing` | `18.0` | Min gap between pickup spawn slots |
|---|---|---|
| `runner_obstacle_penalty` | `25` | Health deducted on each wall collision |
| `runner_collision_cooldown` | `0.4` | Invincibility seconds after a hit |
|---|---|---|
| `randomness_seed` | `0` | `0` = system clock (unique run); other = deterministic replay |
| `randomness_obstacle_density` | `0.35` | Probability a spawn slot is a wall (0 = easy, 1 = impossible) |
| `randomness_pickup_density` | `0.50` | Probability a non-wall slot has a pickup |
|---|---|
| `SPACE` / `↑` / `W` | Jump |
| `ESC` | Pause / unpause |
| `ENTER` on menu | Start game |

---

### Example commands

Clone the repository:
`git clone https://github.com/zmertens/BreakingWalls.git`

Build the project on Windows:
`gradlew.bat clean build`

Run it: `gradlew.bat run --args="path\to\my-config.json"`

Building a standalone fat JAR:
```powershell
.\gradlew jar;
java -jar build\libs\jbreaking-walls-<Major>.<Minor>.<Patch>.jar
```
