# About Breaking Walls

An endless sidescrolling pointer game with a relaxing theme and futuristic vibes.

  - Players highlight nodes and graphs
  - Simple event handling (mouse, touch, keyboard) and haptic feedback
  - Spatialized sound effects
  - Break down the 4th wall and advance the canvas with evolving effects

##  TODO's



## Configuration

`NPM` is used for project configuration and builds.

These exteneral dependencies are downloaded automatically with NPM:

### Config file (`config.json`)

The config lives in the project root (next to `build.gradle`).  Every key is documented inline with a `_<key>_doc` comment field.

#### Configuration Properties

| Key | Default | Effect |
|---|---|---|
| `window_title` | `"JBreaking Walls"` | OS window title bar text |
| `window_width` | `1280` | Initial width in pixels |
| `window_height` | `720` | Initial height in pixels |
| `window_icon` | `textures/icon.bmp` | Initial window icon as a BMP file |
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

Build the project in Production mode:
`npm run build`

Run it in debug mode: `npm run dev`

Run the generated files directly:
```powershell
python -m http.server .\dist\index.html
```
