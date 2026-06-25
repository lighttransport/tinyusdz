#version 450

// GPU-tessellation path, pre-tessellation vertex stage. Applies the blendshape
// morph + skinning to each control point (object space), then forwards it to the
// tess control shader. The tessellator subdivides the DEFORMED surface;
// displacement is applied per tess-vertex in mesh_tess.tese. Mirrors mesh.vert's
// morph+skin so morphed and/or skinned displaced meshes tessellate. Object-space
// output only -- projection happens in the eval shader.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in uvec4 aJoint;
layout(location = 4) in vec4 aWeight;
layout(location = 5) in uvec2 aInfluence;
layout(location = 8) in uvec2 aMorphOffsetCount;  // GPU morph (offset,count); 0 = none

layout(set = 1, binding = 0, std430) readonly buffer BoneRows { vec4 boneRows[]; };
layout(set = 2, binding = 0, std430) readonly buffer InfluenceRows { vec4 influenceRows[]; };
layout(set = 7, binding = 0, std430) readonly buffer MorphDeltas { uvec2 morphDeltas[]; };
layout(set = 8, binding = 0, std430) readonly buffer MorphCoeffs { float morphCoeff[]; };
layout(set = 9, binding = 0, std430) readonly buffer MorphChan { uint morphChanPacked[]; };
uint morphChanId(uint e) {  // entry index -> channelId (low/high 16 of the uint)
  return (morphChanPacked[e >> 1u] >> ((e & 1u) << 4u)) & 0xffffu;
}

layout(location = 0) out vec3 vcPos;
layout(location = 1) out vec3 vcNrm;
layout(location = 2) out vec2 vcUV;

mat4 fetchBone(uint idx) {
  int base = int(idx) * 4;
  return mat4(boneRows[base + 0], boneRows[base + 1],
              boneRows[base + 2], boneRows[base + 3]);
}

void main() {
  vec3 pos = aPos;
  vec3 nrm = aNormal;
  // GPU blendshape morph (before skinning), with the active-channel skip.
  if (aMorphOffsetCount.y > 0u) {
    int mbase = int(aMorphOffsetCount.x);
    int mcount = min(int(aMorphOffsetCount.y), 256);
    for (int i = 0; i < 256; ++i) {
      if (i >= mcount) break;
      float c = morphCoeff[int(morphChanId(uint(mbase + i)))];
      if (abs(c) < 1e-6) continue;
      uvec2 raw = morphDeltas[mbase + i];
      vec2 a = unpackHalf2x16(raw.x);  // (channelId, dx)
      vec2 b = unpackHalf2x16(raw.y);  // (dy, dz)
      pos += c * vec3(a.y, b.x, b.y);
    }
  }
  // Linear-blend skinning of the morphed position (mirrors mesh.vert).
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
      vec4 iw = influenceRows[base + i];
      uint joint = uint(iw.x + 0.5);
      float weight = iw.y;
      if (weight > 0.0 && int(joint) < boneCapacity) {
        skin += fetchBone(joint) * weight;
        fullWeightSum += weight;
      }
    }
    if (fullWeightSum > 0.0) {
      skin *= 1.0 / fullWeightSum;
      pos = (skin * vec4(pos, 1.0)).xyz;  // skin the morphed position
      nrm = normalize((skin * vec4(aNormal, 0.0)).xyz);
    }
  } else if (wsum > 0.0 && int(maxJoint) < boneCapacity) {
    mat4 skin =
        fetchBone(aJoint.x) * aWeight.x + fetchBone(aJoint.y) * aWeight.y +
        fetchBone(aJoint.z) * aWeight.z + fetchBone(aJoint.w) * aWeight.w;
    pos = (skin * vec4(pos, 1.0)).xyz;
    nrm = normalize((skin * vec4(aNormal, 0.0)).xyz);
  }
  vcPos = pos;
  vcNrm = nrm;
  vcUV = aUV;
}
