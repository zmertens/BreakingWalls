-- tile_exit.go
embedded_components {
  id: "sprite"
  type: "sprite"
  data: "default_animation: \"pulse\"\n"
       "material: \"/materials/tile.materialc\"\n"
       "blend_mode: BLEND_MODE_ALPHA\n"
       "image: \"/assets/tiles.atlas\"\n"
       "animations {\n"
       "  id: \"pulse\"\n"
       "  images { image: \"/assets/images/tile_exit.png\" }\n"
       "  playback: PLAYBACK_LOOP_PINGPONG\n"
       "  fps: 8\n"
       "}\n"
       "size_mode: SIZE_MODE_AUTO\n"
}
components {
  id: "script"
  component: "/maze/tile_exit.script"
}
