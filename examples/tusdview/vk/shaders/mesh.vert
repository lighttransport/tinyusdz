#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in uvec4 aJoint;
layout(location = 4) in vec4 aWeight;
layout(location = 5) in uvec2 aInfluence;
layout(location = 6) in vec2 aUV1;        // 2nd texcoord set (multi-UV AOV)
layout(location = 7) in float aMorphInfl; // blendshape influence (world units)

layout(set = 1, binding = 0, std430) readonly buffer BoneRows {
  vec4 boneRows[];
};
layout(set = 2, binding = 0, std430) readonly buffer InfluenceRows {
  vec4 influenceRows[];
};
// Coarse displacement height map (red channel), sampled in the vertex stage. The
// renderer binds black (red=0) when the submesh has no displacement, so this is an
// unconditional no-op there -- no push-constant lane is spent on an enable flag.
layout(set = 4, binding = 0) uniform sampler2D uDisplacementTex;

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

mat4 fetchBone(uint idx) {
  int base = int(idx) * 4;
  return mat4(
      boneRows[base + 0],
      boneRows[base + 1],
      boneRows[base + 2],
      boneRows[base + 3]);
}

void main() {
  vec3 pos = aPos;
  vec3 nrm = aNormal;
  float wsum = aWeight.x + aWeight.y + aWeight.z + aWeight.w;
  uint maxJoint = max(max(aJoint.x, aJoint.y), max(aJoint.z, aJoint.w));
  int boneCapacity = boneRows.length() / 4;
  if (aInfluence.y > 0u) {
    mat4 skin = mat4(0.0);
    float fullWeightSum = 0.0;
    int base = int(aInfluence.x);
    int count = min(int(aInfluence.y), 256);
    for (int i = 0; i < 256; ++i) {
      if (i >= count) break;
      int linear = base + i;
      vec4 iw = influenceRows[linear];
      uint joint = uint(iw.x + 0.5);
      float weight = iw.y;
      if (weight > 0.0 && int(joint) < boneCapacity) {
        skin += fetchBone(joint) * weight;
        fullWeightSum += weight;
      }
    }
    if (fullWeightSum > 0.0) {
      skin *= 1.0 / fullWeightSum;
      pos = (skin * vec4(aPos, 1.0)).xyz;
      nrm = normalize((skin * vec4(aNormal, 0.0)).xyz);
    }
  } else if (wsum > 0.0 && int(maxJoint) < boneCapacity) {
    mat4 skin =
        fetchBone(aJoint.x) * aWeight.x +
        fetchBone(aJoint.y) * aWeight.y +
        fetchBone(aJoint.z) * aWeight.z +
        fetchBone(aJoint.w) * aWeight.w;
    pos = (skin * vec4(aPos, 1.0)).xyz;
    nrm = normalize((skin * vec4(aNormal, 0.0)).xyz);
  }
  // Coarse displacement: offset along the (object-space) normal by the height map's
  // red channel (no derivatives in the vertex stage -> textureLod 0). Black map =>
  // no offset. Geometric normals (flags bit0, set by the renderer) keep shading
  // consistent with the deformed surface.
  pos += normalize(nrm) * textureLod(uDisplacementTex, aUV, 0.0).r;
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
