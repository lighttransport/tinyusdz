#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in uvec4 aJoint;
layout(location = 4) in vec4 aWeight;
layout(location = 5) in uvec2 aInfluence;

layout(set = 1, binding = 0, std430) readonly buffer BoneRows {
  vec4 boneRows[];
};
layout(set = 2, binding = 0, std430) readonly buffer InfluenceRows {
  vec4 influenceRows[];
};

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
  vNormalW = mat3(pc.nmat[0].xyz, pc.nmat[1].xyz, pc.nmat[2].xyz) * nrm;
  vUV = aUV;
  vWorldPos = (pc.model * vec4(pos, 1.0)).xyz;
  gl_Position = pc.mvp * vec4(pos, 1.0);
}
