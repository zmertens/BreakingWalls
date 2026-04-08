-- tile_pickup.go
embedded_components {
  id: "sprite"
  type: "sprite"
  data: "default_animation: \"spin\"\n"
       "material: \"/materials/tile.materialc\"\n"
       "blend_mode: BLEND_MODE_ALPHA\n"
       "image: \"/assets/tiles.atlas\"\n"
       "animations {\n"
       "  id: \"spin\"\n"
       "  images { image: \"/assets/images/tile_pickup.png\" }\n"
       "  playback: PLAYBACK_LOOP_FORWARD\n"
       "  fps: 12\n"
       "}\n"
       "size_mode: SIZE_MODE_AUTO\n"
}
components {
  id: "script"
  component: "/maze/tile_pickup.script"
}
