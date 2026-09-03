#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
  mat4 model;
  vec4 baseColor;
  vec4 matAux;
  vec4 emissive;
  ivec4 ids;
} pc;

void main() { outColor = pc.baseColor; }
