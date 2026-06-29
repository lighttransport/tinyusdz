#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in uvec4 aJoint;
layout(location = 4) in vec4 aWeight;
layout(location = 5) in uvec2 aInfluence;
layout(location = 6) in vec2 aUV1;        // 2nd texcoord set (multi-UV AOV)
layout(location = 7) in float aMorphInfl; // blendshape influence (world units)
layout(location = 8) in uvec2 aMorphOffsetCount; // GPU morph (offset,count); 0 = none

// Blendshape morph + linear-blend skinning (sets 1,2,7,8,9 + fetchBone /
// morphChanId / applyMorphSkin); shared with the GPU-tessellation vertex stage.
#include "deform.glsl"

// Coarse displacement height map (red channel), sampled in the vertex stage. The
// renderer binds black (red=0) when the submesh has no displacement, so this is an
// unconditional no-op there -- no push-constant lane is spent on an enable flag.
layout(set = 4, binding = 0) uniform sampler2D uDisplacementTex;
// Global displacement params: .x = scale, .y = maxTessLevel (set by the UI sliders).
layout(set = 5, binding = 0) uniform DispParams { vec4 disp; } dp;
// Per-material displacement texture scale/bias (height = texel*scale + bias).
layout(set = 6, binding = 0, std430) readonly buffer DispMat { vec2 sb[]; } dm;

// 128-byte push constant block (Vulkan-guaranteed minimum):
//   mat4 mvp        : 64 bytes
//   mat3 nmat       : 48 bytes (std layout: 3 x vec4)
//   vec4 baseColor  : 16 bytes
layout(push_constant) uniform PushConstants {
  mat4 mvp;
  mat4 model;
  vec4 nmat[3];   // normal matrix columns in .xyz; .w packs emissive.rgb (AOV)
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
layout(location = 3) flat out int vDomJoint;   // dominant skin joint (SkinWeights AOV)
layout(location = 4) out float vDomWeight;
layout(location = 5) out vec2 vUV1;            // 2nd texcoord set (multi-UV AOV)
layout(location = 6) out float vMorphInfl;     // blendshape influence (world units)

void main() {
  // Blendshape morph (before skin) + linear-blend skin -> object-space pos/nrm.
  vec3 pos, nrm;
  applyMorphSkin(aPos, aNormal, aMorphOffsetCount, aJoint, aWeight, aInfluence,
                 pos, nrm);
  // Coarse displacement: offset along the (object-space) normal by the height map's
  // red channel (no derivatives in the vertex stage -> textureLod 0). Black map =>
  // no offset. Geometric normals (flags bit0, set by the renderer) keep shading
  // consistent with the deformed surface.
  vec2 dsb = pc.matId >= 0 ? dm.sb[pc.matId] : vec2(1.0, 0.0);
  float disp = textureLod(uDisplacementTex, aUV, 0.0).r * dsb.x + dsb.y;
  pos += normalize(nrm) * (disp * dp.disp.x);
  vNormalW = mat3(pc.nmat[0].xyz, pc.nmat[1].xyz, pc.nmat[2].xyz) * nrm;
  vUV = aUV;
  vWorldPos = (pc.model * vec4(pos, 1.0)).xyz;
  // Dominant skin joint (SkinWeights AOV) from the base 4-weight set.
  vDomJoint = -1;
  vDomWeight = 0.0;
  if (aWeight.x > vDomWeight) { vDomWeight = aWeight.x; vDomJoint = int(aJoint.x); }
  if (aWeight.y > vDomWeight) { vDomWeight = aWeight.y; vDomJoint = int(aJoint.y); }
  if (aWeight.z > vDomWeight) { vDomWeight = aWeight.z; vDomJoint = int(aJoint.z); }
  if (aWeight.w > vDomWeight) { vDomWeight = aWeight.w; vDomJoint = int(aJoint.w); }
  vUV1 = aUV1;
  vMorphInfl = aMorphInfl;
  gl_Position = pc.mvp * vec4(pos, 1.0);
}
