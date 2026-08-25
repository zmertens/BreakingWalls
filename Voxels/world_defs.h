#ifndef WORLD_DEFS_H
#define WORLD_DEFS_H

// Internal constants shared across world.cpp, chunk_manager.cpp, and world_renderer.cpp.
// Not part of the public API — do not include from headers.

#define CREATE_CHUNK_RADIUS  10
#define RENDER_CHUNK_RADIUS  20
#define BUILD_CHUNK_SIZE     32
#define RENDER_SIGN_RADIUS   4
#define DELETE_CHUNK_RADIUS  14
#define CHUNK_WORKERS_TOTAL  4

#endif // WORLD_DEFS_H
