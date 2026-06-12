#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in uvec4 aJoint;
layout(location = 4) in vec4 aWeight;

layout(set = 1, binding = 0) uniform sampler2D uBoneTex;

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

mat4 fetchBone(uint idx) {
  int y = int(idx);
  return mat4(
      texelFetch(uBoneTex, ivec2(0, y), 0),
      texelFetch(uBoneTex, ivec2(1, y), 0),
      texelFetch(uBoneTex, ivec2(2, y), 0),
      texelFetch(uBoneTex, ivec2(3, y), 0));
}

void main() {
  vec3 pos = aPos;
  vec3 nrm = aNormal;
  float wsum = aWeight.x + aWeight.y + aWeight.z + aWeight.w;
  uint maxJoint = max(max(aJoint.x, aJoint.y), max(aJoint.z, aJoint.w));
  if (wsum > 0.0 && int(maxJoint) < textureSize(uBoneTex, 0).y) {
    mat4 skin =
        fetchBone(aJoint.x) * aWeight.x +
        fetchBone(aJoint.y) * aWeight.y +
        fetchBone(aJoint.z) * aWeight.z +
        fetchBone(aJoint.w) * aWeight.w;
    pos = (skin * vec4(aPos, 1.0)).xyz;
    nrm = normalize((skin * vec4(aNormal, 0.0)).xyz);
  }
  vNormalW = pc.nmat * nrm;
  vUV = aUV;
  gl_Position = pc.mvp * vec4(pos, 1.0);
}
