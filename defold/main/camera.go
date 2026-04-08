-- camera.go
-- Simple follow camera for the isometric view
embedded_components {
  id: "camera"
  type: "camera"
  data: "aspect_ratio: 1.7777778\n"
       "fov: 45.0\n"
       "near_z: 0.1\n"
       "far_z: 1000.0\n"
       "auto_aspect_ratio: 1\n"
       "orthographic_projection: 1\n"
       "orthographic_zoom: 1.0\n"
}
components {
  id: "script"
  component: "/main/camera.script"
}
