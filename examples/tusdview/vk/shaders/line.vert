#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aCol;

layout(push_constant) uniform PushConstants {
  mat4 vp;
} pc;

layout(location = 0) out vec3 vCol;

void main() {
  vCol = aCol;
  gl_Position = pc.vp * vec4(aPos, 1.0);
}
