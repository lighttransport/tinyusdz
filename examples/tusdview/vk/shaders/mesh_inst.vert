#version 450

// Instanced flat-shaded prototype (large-scene --next path). Per-instance 3x4
// object-to-world (binding 6, instance-rate, rows = output x/y/z) + per-instance
// color (binding 7) + per-vertex prototype color (binding 8). Mirrors the GL
// kInstancedVS so both rasterizers match.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 6) in vec4 aRow0;       // o2w row 0 (instance-rate)
layout(location = 7) in vec4 aRow1;
layout(location = 8) in vec4 aRow2;
layout(location = 9) in vec3 aInstColor;  // per-instance color (instance-rate)
layout(location = 10) in vec3 aVtxColor;  // per-vertex prototype color

layout(push_constant) uniform InstPushC {
  mat4 viewProj;     // P * V
  vec4 camPos;       // xyz = camera world pos, w = depthScale
  vec4 sceneMin;     // xyz = position-AOV bbox min
  vec4 sceneExtent;  // xyz = position-AOV bbox size
  vec4 emissive;     // xyz = selection-highlight override (else 0)
  int renderMode;
  int meshId;
  int flags;         // bit0=geometricNormal, bit1=doubleSided, bits2-3=purpose, bits4-6=kind
  int pad;
} pc;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vColor;
layout(location = 3) flat out int vInstanceId;

void main() {
  vInstanceId = gl_InstanceIndex;
  vec4 p = vec4(aPos, 1.0);
  vec3 wp = vec3(dot(p, aRow0), dot(p, aRow1), dot(p, aRow2));
  vec3 n = vec3(dot(aNormal, aRow0.xyz), dot(aNormal, aRow1.xyz),
                dot(aNormal, aRow2.xyz));
  vWorldPos = wp;
  vNormal = normalize(n);
  vColor = aInstColor * aVtxColor;  // per-instance x per-vertex (both default 1)
  gl_Position = pc.viewProj * vec4(wp, 1.0);
}
