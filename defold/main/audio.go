-- audio.go  (embedded sound components for BGM + SFX)
embedded_components {
  id: "bgm"
  type: "sound"
  data: "sound: \"/assets/audio/bgm.mp3\"\n"
       "looping: 1\n"
       "group: \"master\"\n"
       "gain: 0.7\n"
       "pan: 0.0\n"
       "speed: 1.0\n"
}
embedded_components {
  id: "step"
  type: "sound"
  data: "sound: \"/assets/audio/step.ogg\"\n"
       "looping: 0\n"
       "group: \"master\"\n"
       "gain: 0.5\n"
       "pan: 0.0\n"
       "speed: 1.0\n"
}
embedded_components {
  id: "pickup"
  type: "sound"
  data: "sound: \"/assets/audio/pickup.ogg\"\n"
       "looping: 0\n"
       "group: \"master\"\n"
       "gain: 0.8\n"
       "pan: 0.0\n"
       "speed: 1.0\n"
}
embedded_components {
  id: "levelup"
  type: "sound"
  data: "sound: \"/assets/audio/levelup.ogg\"\n"
       "looping: 0\n"
       "group: \"master\"\n"
       "gain: 1.0\n"
       "pan: 0.0\n"
       "speed: 1.0\n"
}
components {
  id: "script"
  component: "/main/audio.script"
}
