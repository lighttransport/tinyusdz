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
// Frame-constant UBO (set 5): disp sliders + camera. The vertex stage derives
// mvp = viewProj*model and the normal matrix from pc.model, so neither needs a
// push-constant lane (keeps the push block <= 128 B, the Vulkan minimum).
layout(set = 5, binding = 0) uniform Frame {
  vec4 disp;          // .x = displacement scale, .y = maxTessLevel
  mat4 viewProj;      // P * V
  vec4 camPos;        // .xyz camera, .w depth normalizer
  vec4 sceneMin;      // .xyz
  vec4 sceneExtent;   // .xyz
  ivec4 mode;         // .x = renderMode
} fr;
// Per-material displacement texture scale/bias (height = texel*scale + bias).
layout(set = 6, binding = 0, std430) readonly buffer DispMat { vec2 sb[]; } dm;

// 128-byte per-draw push constant block (matches struct PushC in vk_renderer.cc).
layout(push_constant) uniform PushConstants {
  mat4 model;
  vec4 baseColor;   // rgb + .w opacity
  vec4 matAux;      // .x metallic, .y roughness (AOVs)
  vec4 emissive;    // .xyz emissive (AOV)
  ivec4 ids;        // .x matId, .y flags, .z meshId
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
  vec2 dsb = pc.ids.x >= 0 ? dm.sb[pc.ids.x] : vec2(1.0, 0.0);
  float disp = textureLod(uDisplacementTex, aUV, 0.0).r * dsb.x + dsb.y;
  pos += normalize(nrm) * (disp * fr.disp.x);
  // Normal matrix = inverse-transpose of the model's upper-left 3x3 (derived
  // here so it costs no push-constant space).
  mat3 nmat = transpose(inverse(mat3(pc.model)));
  vNormalW = nmat * nrm;
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
  gl_Position = fr.viewProj * pc.model * vec4(pos, 1.0);
}
