-- player.go
embedded_components {
  id: "sprite"
  type: "sprite"
  data: "default_animation: \"idle\"\n"
       "material: \"/materials/sprite.materialc\"\n"
       "blend_mode: BLEND_MODE_ALPHA\n"
       "image: \"/assets/player.atlas\"\n"
       "size_mode: SIZE_MODE_AUTO\n"
}
components {
  id: "script"
  component: "/player/player.script"
}
