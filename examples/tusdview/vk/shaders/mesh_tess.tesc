#version 450

// Tessellation control: adaptive per-edge level from the world-space edge length
// relative to its distance to the camera (nearer / longer edges subdivide more),
// clamped to a fixed cap (the VK path has no live max-tess slider -- the slider
// only toggles tessellation on/off via pipeline selection on the CPU side).

layout(vertices = 3) out;

layout(location = 0) in vec3 vcPos[];
layout(location = 1) in vec3 vcNrm[];
layout(location = 2) in vec2 vcUV[];

layout(location = 0) out vec3 tcPos[];
layout(location = 1) out vec3 tcNrm[];
layout(location = 2) out vec2 tcUV[];

// Must match PushC / mesh.vert exactly (shared push-constant range).
layout(push_constant) uniform PushConstants {
  mat4 mvp;
  mat4 model;
  vec4 nmat[3];
  vec4 baseColor;
  vec4 camPos;
  vec4 sceneMin;
  vec4 sceneExtent;
  int matId;
  int renderMode;
  int flags;
  int meshId;
} pc;

const float kMaxTessLevel = 16.0;

float edgeLevel(vec3 a, vec3 b) {
  vec3 wa = (pc.model * vec4(a, 1.0)).xyz;
  vec3 wb = (pc.model * vec4(b, 1.0)).xyz;
  vec3 mid = 0.5 * (wa + wb);
  float len = length(wa - wb);
  float dist = max(length(pc.camPos.xyz - mid), 1e-3);
  return clamp(len / dist * 120.0, 1.0, kMaxTessLevel);
}

void main() {
  tcPos[gl_InvocationID] = vcPos[gl_InvocationID];
  tcNrm[gl_InvocationID] = vcNrm[gl_InvocationID];
  tcUV[gl_InvocationID] = vcUV[gl_InvocationID];
  if (gl_InvocationID == 0) {
    float l0 = edgeLevel(vcPos[1], vcPos[2]);
    float l1 = edgeLevel(vcPos[2], vcPos[0]);
    float l2 = edgeLevel(vcPos[0], vcPos[1]);
    gl_TessLevelOuter[0] = l0;
    gl_TessLevelOuter[1] = l1;
    gl_TessLevelOuter[2] = l2;
    gl_TessLevelInner[0] = max(max(l0, l1), l2);
  }
}
