#version 450

// GPU-tessellation path, pre-tessellation vertex stage. Applies the blendshape
// morph to each control point (object space), then forwards it to the tess
// control shader. The tessellator subdivides the MORPHED surface; displacement is
// applied per tess-vertex in mesh_tess.tese. Skinning is NOT applied here -- skinned
// displaced meshes stay on the coarse path (gated out at draw), matching GL.
// Object-space output only -- projection happens in the eval shader.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 8) in uvec2 aMorphOffsetCount;  // GPU morph (offset,count); 0 = none

layout(set = 7, binding = 0, std430) readonly buffer MorphDeltas { uvec2 morphDeltas[]; };
layout(set = 8, binding = 0, std430) readonly buffer MorphCoeffs { float morphCoeff[]; };
layout(set = 9, binding = 0, std430) readonly buffer MorphChan { uint morphChanPacked[]; };
uint morphChanId(uint e) {  // entry index -> channelId (low/high 16 of the uint)
  return (morphChanPacked[e >> 1u] >> ((e & 1u) << 4u)) & 0xffffu;
}

layout(location = 0) out vec3 vcPos;
layout(location = 1) out vec3 vcNrm;
layout(location = 2) out vec2 vcUV;

void main() {
  vec3 pos = aPos;
  // GPU blendshape morph with the active-channel skip (mirrors mesh.vert).
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
  vcPos = pos;
  vcNrm = aNormal;
  vcUV = aUV;
}
