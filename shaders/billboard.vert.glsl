#version 430 core

layout(location = 0) in vec3 aPos;

uniform mat4 ModelViewMatrix;

void main()
{
    // Output view-space position; the geometry shader expands this point to a
    // quad and applies the projection matrix itself.
    gl_Position = ModelViewMatrix * vec4(aPos, 1.0);
}
