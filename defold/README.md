# Breaking Walls – Defold 2D Engine Proof of Concept

A proof-of-concept isometric maze runner built with the [Defold](https://defold.com) game engine,
inspired by the Breaking Walls endless-runner concept.

## Features

| Feature | Implementation |
|---|---|
| **Isometric maze** | Standard 2:1 isometric projection in Lua, DFS-generated maze |
| **Endless generation** | Chunk-based streaming: new chunks generated ahead of player, old ones culled |
| **Sprite animation** | `player.atlas` with idle/walk animation groups (flipbook) |
| **GUI** | Main menu, in-game HUD (score/level/lives), pause screen, game-over screen |
| **Audio** | Background music loop + SFX (footstep, pickup, level-up) via Defold sound components |
| **Shaders** | Custom vertex/fragment programs with scanlines, vignette, chromatic aberration, synthwave colour grade |
| **Render pipeline** | Custom render script with orthographic camera, z-sorted draw pass, GUI overlay |

## Project Structure

```
defold/
├── game.project                    # Project settings
├── main/
│   ├── main.collection             # Bootstrap scene
│   ├── controller.go/.script       # Game state machine (menu → playing → paused → game_over)
│   ├── camera.go/.script           # Smooth-follow orthographic camera
│   ├── audio.go/.script            # Centralised audio manager (BGM + SFX)
│   ├── game.input_binding          # Keyboard / mouse input mappings
│   ├── player_factory.go           # Factory wrapper for spawning the player
│   └── maze_factory.go             # Factory wrapper for spawning the maze manager
├── player/
│   ├── player.go                   # Player game object (sprite + script)
│   └── player.script               # Input handling, grid movement, animation, footsteps
├── maze/
│   ├── maze_gen.lua                # Reusable maze generation library (DFS backtracking)
│   ├── maze.go/.script             # Chunk manager: spawn/cull tiles, detect exit/pickup
│   ├── tile_floor.go               # Floor tile game object
│   ├── tile_wall.go                # Wall tile game object
│   ├── tile_exit.go/.script        # Exit tile with pulse animation
│   └── tile_pickup.go/.script      # Pickup orb with bobbing animation
├── gui/
│   ├── hud.go / hud.gui / hud.gui_script        # In-game HUD
│   └── menu.go / menu.gui / menu.gui_script      # Menus (main / pause / game-over)
├── render/
│   ├── iso_render.render           # Render pipeline descriptor
│   └── iso_render.render_script    # Custom render script
├── shaders/
│   ├── sprite.vp / sprite.fp       # Sprite shader (scanlines + vignette)
│   ├── tile.vp / tile.fp           # Tile shader (depth fog + tint)
│   └── screen.vp / screen.fp       # Post-process shader (chromatic aberration + colour grade)
├── materials/
│   ├── sprite.material             # Uses sprite.vp/fp
│   ├── tile.material               # Uses tile.vp/fp
│   └── screen.material             # Uses screen.vp/fp
└── assets/
    ├── tiles.atlas                 # Floor, wall, exit, pickup tiles
    ├── player.atlas                # Player idle + walk animations
    ├── ui.atlas                    # UI panel and button textures
    ├── images/                     # Source PNG sprites (auto-generated placeholders)
    └── audio/                      # OGG/MP3 sound files
```

## How to Open

1. Download and install [Defold editor](https://defold.com/download/).
2. Open Defold and choose **Open Project → From Disk**.
3. Navigate to `defold/game.project` and open it.
4. Press **Project → Build** (or `Ctrl+B`) to build and run.

## Gameplay

| Key | Action |
|---|---|
| Arrow keys / WASD | Move player through the maze |
| `Escape` | Pause / resume |
| `Enter` / `Space` | Confirm in menus |
| Left click | Interact with GUI buttons |

Navigate to the **cyan exit tile** to advance to the next level.  
Collect **yellow orbs** for bonus score.

## Maze Generation Algorithm

The `maze/maze_gen.lua` module implements *Recursive Backtracking* (iterative DFS):

1. All cells start with all four walls intact.
2. Starting from (1,1), randomly choose an unvisited neighbour.
3. Carve the wall between current cell and chosen neighbour.
4. Push the neighbour onto the stack and repeat.
5. When no unvisited neighbours remain, backtrack (pop stack).
6. BFS from start cell finds the furthest cell for exit placement.
7. Pickups are scattered randomly at configurable density.

New chunks are generated ahead of the player and old chunks are deleted to keep memory usage constant regardless of play time.

## Shader System

Shaders are written in GLSL ES 1.0 (compatible with Defold's OpenGL ES 2.0 backend).

### `sprite.fp`
- **Synthwave tint** – multiplicative RGB tint from material constant.
- **Scanlines** – horizontal sine-wave modulation, density and strength adjustable.
- **Vignette** – radial edge darkening, radius and softness adjustable.

### `tile.fp`
- Inherits tint.
- **Depth fog** – UV-Y based depth approximation blends in a dark purple fog.

### `screen.fp` (post-process)
- **Chromatic aberration** – red/blue channels slightly offset from centre.
- **Scanlines** – applied to final composite.
- **Vignette** – full-screen edge darkening.
- **Colour grade** – per-channel multipliers for the synthwave look.

All shader parameters are exposed as material constants and can be changed at runtime via `go.set()` from Lua scripts.

## Extending the PoC

- **Enemies**: Add an enemy game object with a `maze_gen.can_move` aware script.
- **Multiple levels**: Adjust `pickup_chance` and maze size per level in `maze.script`.
- **Networking**: The controller state machine has clear seams for adding server messages.
- **Proper art**: Replace images in `assets/images/` with real isometric tile art (64×32 floor, 64×64 walls).
