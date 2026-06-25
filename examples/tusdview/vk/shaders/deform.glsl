// Shared GPU vertex-stage deform: blendshape morph (before skinning) + linear-blend
// skinning. Included by mesh.vert (raster) and mesh_tess.vert (tessellation control
// points) so the deform math lives in ONE place. Requires the includer to enable
// GL_GOOGLE_include_directive and to feed the per-vertex attributes as arguments.

layout(set = 1, binding = 0, std430) readonly buffer BoneRows { vec4 boneRows[]; };
layout(set = 2, binding = 0, std430) readonly buffer InfluenceRows { vec4 influenceRows[]; };
// GPU blendshape morph: per-vertex sparse delta list (set 7) + per-frame coefficient
// per channel (set 8). Each entry packs 4 halfs (channelId,dx,dy,dz) into one uvec2
// (unpackHalf2x16). Per-entry channelId lives in its own uint16-packed buffer (set 9)
// so the loop can skip the wide delta fetch when the channel is inactive.
layout(set = 7, binding = 0, std430) readonly buffer MorphDeltas { uvec2 morphDeltas[]; };
layout(set = 8, binding = 0, std430) readonly buffer MorphCoeffs { float morphCoeff[]; };
layout(set = 9, binding = 0, std430) readonly buffer MorphChan { uint morphChanPacked[]; };

uint morphChanId(uint e) {  // entry index -> channelId (low/high 16 of the uint)
  return (morphChanPacked[e >> 1u] >> ((e & 1u) << 4u)) & 0xffffu;
}

mat4 fetchBone(uint idx) {
  int base = int(idx) * 4;
  return mat4(boneRows[base + 0], boneRows[base + 1],
              boneRows[base + 2], boneRows[base + 3]);
}

// Apply morph then skin to one control point. outPos/outNrm are object-space.
void applyMorphSkin(vec3 inPos, vec3 inNrm, uvec2 morphOC,
                    uvec4 joint, vec4 weight, uvec2 influence,
                    out vec3 outPos, out vec3 outNrm) {
  vec3 pos = inPos;
  vec3 nrm = inNrm;
  // GPU blendshape morph (before skinning), with the active-channel skip.
  if (morphOC.y > 0u) {
    int mbase = int(morphOC.x);
    int mcount = min(int(morphOC.y), 256);
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
  // Linear-blend skinning of the morphed position.
  float wsum = weight.x + weight.y + weight.z + weight.w;
  uint maxJoint = max(max(joint.x, joint.y), max(joint.z, joint.w));
  int boneCapacity = boneRows.length() / 4;
  if (influence.y > 0u) {
    mat4 skin = mat4(0.0);
    float fullWeightSum = 0.0;
    int base = int(influence.x);
    int count = min(int(influence.y), 256);
    for (int i = 0; i < 256; ++i) {
      if (i >= count) break;
      vec4 iw = influenceRows[base + i];
      uint j = uint(iw.x + 0.5);
      float w = iw.y;
      if (w > 0.0 && int(j) < boneCapacity) {
        skin += fetchBone(j) * w;
        fullWeightSum += w;
      }
    }
    if (fullWeightSum > 0.0) {
      skin *= 1.0 / fullWeightSum;
      pos = (skin * vec4(pos, 1.0)).xyz;
      nrm = normalize((skin * vec4(inNrm, 0.0)).xyz);
    }
  } else if (wsum > 0.0 && int(maxJoint) < boneCapacity) {
    mat4 skin =
        fetchBone(joint.x) * weight.x + fetchBone(joint.y) * weight.y +
        fetchBone(joint.z) * weight.z + fetchBone(joint.w) * weight.w;
    pos = (skin * vec4(pos, 1.0)).xyz;
    nrm = normalize((skin * vec4(inNrm, 0.0)).xyz);
  }
  outPos = pos;
  outNrm = nrm;
}
