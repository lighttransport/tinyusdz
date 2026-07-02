#version 450
#extension GL_ARB_shader_draw_parameters : require

// Instanced flat-shaded prototype (large-scene --next path). Per-instance 3x4
// object-to-world (instance-rate, rows = output x/y/z) at locations 3/4/5 -- the
// unused skin slots, leaving location 8 free for the per-vertex morph CSR --
// plus per-instance color + per-vertex prototype color. Mirrors the GL
// kInstancedVS so both rasterizers match.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 3) in vec4 aRow0;       // o2w row 0 (instance-rate)
layout(location = 4) in vec4 aRow1;
layout(location = 5) in vec4 aRow2;
layout(location = 9) in vec3 aInstColor;  // per-instance color (instance-rate)
layout(location = 10) in vec3 aVtxColor;  // per-vertex prototype color
layout(location = 8) in uvec2 aMorphOffsetCount;  // GPU morph (offset,count); 0=none

// GPU blendshape morph (sets 7/8/9, same layout as deform.glsl): a morphed
// prototype is summed into its local position before the per-instance transform.
// Non-morphed meshes bind the shared dummy sets and carry count 0, so the loop
// is skipped. The morph is per-prototype -- shared by all instances of it.
layout(set = 7, binding = 0, std430) readonly buffer MorphDeltas { uvec2 morphDeltas[]; };
layout(set = 8, binding = 0, std430) readonly buffer MorphCoeffs { float morphCoeff[]; };
layout(set = 9, binding = 0, std430) readonly buffer MorphChan { uint morphChanPacked[]; };
uint morphChanId(uint e) {  // entry index -> channelId (low/high 16 of the uint)
  return (morphChanPacked[e >> 1u] >> ((e & 1u) << 4u)) & 0xffffu;
}

// Frame UBO (set 5): viewProj / camPos / scene bbox / renderMode, frame-constant
// for the whole instanced pass (shared with the mesh pipeline).
layout(set = 5, binding = 0) uniform Frame {
  vec4 disp;
  mat4 viewProj;     // P * V
  vec4 camPos;       // xyz camera, w depthScale
  vec4 sceneMin;
  vec4 sceneExtent;
  ivec4 mode;        // .x renderMode
} fr;
// Per-draw push constant: the base index of this draw in the DrawMeta SSBO. In
// the per-mesh loop each draw is separate (gl_DrawIDARB == 0) so baseDraw selects
// the mesh; in a multi-draw-indirect batch baseDraw is the batch's first slot and
// gl_DrawIDARB walks its commands. The resolved slot is passed flat to the
// fragment stage (gl_DrawIDARB is not available there).
layout(push_constant) uniform InstPushC { ivec4 draw; } pc;  // .x = baseDraw

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vColor;
layout(location = 3) flat out int vInstanceId;
layout(location = 4) flat out int vDrawSlot;

void main() {
  vInstanceId = gl_InstanceIndex;
  vDrawSlot = pc.draw.x + gl_DrawIDARB;
  // GPU blendshape morph (active-channel skip), before the per-instance transform.
  vec3 pos = aPos;
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
  vec4 p = vec4(pos, 1.0);
  vec3 wp = vec3(dot(p, aRow0), dot(p, aRow1), dot(p, aRow2));
  vec3 n = vec3(dot(aNormal, aRow0.xyz), dot(aNormal, aRow1.xyz),
                dot(aNormal, aRow2.xyz));
  vWorldPos = wp;
  vNormal = normalize(n);
  vColor = aInstColor * aVtxColor;  // per-instance x per-vertex (both default 1)
  gl_Position = fr.viewProj * vec4(wp, 1.0);
}
