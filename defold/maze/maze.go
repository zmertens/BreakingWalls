-- maze.go – game object that owns the maze script and sprite factories
components {
  id: "script"
  component: "/maze/maze.script"
}
embedded_components {
  id: "floor_factory"
  type: "factory"
  data: "prototype: \"/maze/tile_floor.goc\"\n"
       "load_dynamically: 0\n"
}
embedded_components {
  id: "wall_factory"
  type: "factory"
  data: "prototype: \"/maze/tile_wall.goc\"\n"
       "load_dynamically: 0\n"
}
embedded_components {
  id: "exit_factory"
  type: "factory"
  data: "prototype: \"/maze/tile_exit.goc\"\n"
       "load_dynamically: 0\n"
}
embedded_components {
  id: "pickup_factory"
  type: "factory"
  data: "prototype: \"/maze/tile_pickup.goc\"\n"
       "load_dynamically: 0\n"
}
