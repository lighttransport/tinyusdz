#version 450

// Tessellation evaluation: barycentric-interpolate the patch, sample the
// displacement height (set 4, red channel) and offset along the interpolated
// normal, then emit the same varyings mesh.frag consumes. mesh.frag shades with
// the geometric normal (flags bit0, set for displaced draws), so the new detail
// shades correctly. Displacement scale is fixed at 1.0 (no free push lane).

layout(triangles, equal_spacing, ccw) in;

layout(location = 0) in vec3 tcPos[];
layout(location = 1) in vec3 tcNrm[];
layout(location = 2) in vec2 tcUV[];

layout(set = 4, binding = 0) uniform sampler2D uDisplacementTex;

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

layout(location = 0) out vec3 vNormalW;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vWorldPos;
layout(location = 3) flat out int vDomJoint;
layout(location = 4) out float vDomWeight;
layout(location = 5) out vec2 vUV1;
layout(location = 6) out float vMorphInfl;

void main() {
  vec3 bc = gl_TessCoord;
  vec3 pos = bc.x * tcPos[0] + bc.y * tcPos[1] + bc.z * tcPos[2];
  vec3 nrm = normalize(bc.x * tcNrm[0] + bc.y * tcNrm[1] + bc.z * tcNrm[2]);
  vec2 uv = bc.x * tcUV[0] + bc.y * tcUV[1] + bc.z * tcUV[2];
  pos += nrm * textureLod(uDisplacementTex, uv, 0.0).r;
  vNormalW = mat3(pc.nmat[0].xyz, pc.nmat[1].xyz, pc.nmat[2].xyz) * nrm;
  vUV = uv;
  vWorldPos = (pc.model * vec4(pos, 1.0)).xyz;
  vDomJoint = -1;
  vDomWeight = 0.0;
  vUV1 = vec2(0.0);
  vMorphInfl = 0.0;
  gl_Position = pc.mvp * vec4(pos, 1.0);
}
