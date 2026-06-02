#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

// 128-byte push constant block (Vulkan-guaranteed minimum):
//   mat4 mvp        : 64 bytes
//   mat3 nmat       : 48 bytes (std layout: 3 x vec4)
//   vec4 baseColor  : 16 bytes
layout(push_constant) uniform PushConstants {
  mat4 mvp;
  mat3 nmat;
  vec4 baseColor;
} pc;

layout(location = 0) out vec3 vNormalW;
layout(location = 1) out vec2 vUV;

void main() {
  vNormalW = pc.nmat * aNormal;
  vUV = aUV;
  gl_Position = pc.mvp * vec4(aPos, 1.0);
}
