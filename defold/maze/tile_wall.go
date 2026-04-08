-- tile_wall.go
embedded_components {
  id: "sprite"
  type: "sprite"
  data: "default_animation: \"idle\"\n"
       "material: \"/materials/tile.materialc\"\n"
       "blend_mode: BLEND_MODE_ALPHA\n"
       "image: \"/assets/tiles.atlas\"\n"
       "animations {\n"
       "  id: \"idle\"\n"
       "  images { image: \"/assets/images/tile_wall.png\" }\n"
       "  playback: PLAYBACK_NONE\n"
       "  fps: 0\n"
       "}\n"
       "size_mode: SIZE_MODE_AUTO\n"
}
