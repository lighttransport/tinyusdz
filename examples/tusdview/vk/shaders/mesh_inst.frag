#version 450

// Instanced flat-shaded prototype fragment shader. Mirrors the GL kInstancedFS
// AOV ladder + headlight so instanced geometry looks identical across backends.
// Prototypes carry no UV / material scalars, so those AOV modes fall through to
// a neutral gray (visually obvious there is no data, vs masquerading as a render).
layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vColor;
layout(location = 3) in float vOpacity;
layout(location = 4) flat in int vInstanceId;
layout(location = 5) flat in int vDrawSlot;

// Per-draw metadata (set 6), indexed by the vertex-resolved draw slot. Replaces
// the old per-draw push constant so a whole multi-draw-indirect batch shares one
// binding: each draw's meshId + flag bits come from meta[vDrawSlot]. Instanced
// prototypes are never selection-highlighted, so there is no emissive term.
struct DrawMeta { ivec4 ids; };  // .x meshId, .y flags (as before)
layout(set = 6, binding = 0, std430) readonly buffer DrawMetaB { DrawMeta meta[]; };

// Frame UBO (set 5): camera / scene bbox / renderMode (frame-constant).
// DomeLight IBL irradiance (diffuse-only: prototypes carry no material
// scalars). Set 0 is otherwise unused by the instanced pipeline; a 1x1 black
// cube is bound when no dome IBL is baked.
layout(set = 0, binding = 0) uniform samplerCube uIrradianceMap;

layout(set = 5, binding = 0) uniform Frame {
  vec4 disp;
  mat4 viewProj;
  vec4 camPos;       // xyz camera, w depthScale
  vec4 sceneMin;
  vec4 sceneExtent;
  vec4 lightDir;
  vec4 lightColor;
  ivec4 mode;        // .x renderMode
  mat4 envRot;        // world -> environment rotation (dome IBL)
  vec4 iblColor;      // .rgb dome effectiveColor, .w = hasIbl (0/1)
  vec4 iblParams;     // .x = prefiltered mip count
} fr;
layout(push_constant) uniform InstPushC { ivec4 draw; } pc;  // .x = baseDraw (unused here)

layout(location = 0) out vec4 outColor;

vec3 idColor(int id) {
  if (id < 0) return vec3(0.45);
  uint h = (uint(id) + 1u) * 2654435761u;
  return vec3(float(h & 255u), float((h >> 8) & 255u), float((h >> 16) & 255u)) * (1.0 / 255.0);
}
vec3 purposeColor(int p) {
  if (p == 1) return vec3(0.2, 0.8, 0.3);
  if (p == 2) return vec3(0.2, 0.45, 0.95);
  if (p == 3) return vec3(0.95, 0.75, 0.1);
  return vec3(0.5);
}
vec3 kindColor(int k) {
  if (k == 1) return vec3(0.2, 0.8, 0.8);
  if (k == 2) return vec3(0.85, 0.3, 0.85);
  if (k == 3) return vec3(0.95, 0.6, 0.15);
  if (k == 4) return vec3(0.5, 0.85, 0.4);
  return vec3(0.35);
}

void main() {
  vec3 Ngeo = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
  // Face the geometric normal toward the camera (winding-independent). Using the
  // view vector instead of gl_FrontFacing avoids the VK Y-flipped-viewport
  // winding inversion that would otherwise leave every face unlit.
  vec3 Vdir = normalize(fr.camPos.xyz - vWorldPos);
  vec3 N = Ngeo;
  if (dot(N, Vdir) < 0.0) N = -N;
  const ivec4 ids = meta[vDrawSlot].ids;
  const bool geoNrm = (ids.y & 1) != 0;
  const bool dsided = (ids.y & 2) != 0;
  const int purpose = (ids.y >> 2) & 3;
  const int kind = (ids.y >> 4) & 7;
  if (fr.mode.x != 0) {
    vec3 Nshade = geoNrm ? Ngeo : normalize(vNormal);
    if (fr.mode.x == 2) { outColor = vec4(Nshade * 0.5 + 0.5, 1.0); return; }
    if (fr.mode.x == 4) { outColor = vec4(Ngeo * 0.5 + 0.5, 1.0); return; }
    if (fr.mode.x == 6) {
      float d = clamp(length(fr.camPos.xyz - vWorldPos) / max(fr.camPos.w, 1e-3), 0.0, 1.0);
      outColor = vec4(vec3(1.0 - d), 1.0); return;
    }
    if (fr.mode.x == 7) { outColor = vec4(vColor, 1.0); return; }  // albedo
    if (fr.mode.x == 8) {
      outColor = gl_FrontFacing ? vec4(0.1, 0.7, 0.1, 1.0) : vec4(0.7, 0.1, 0.1, 1.0); return;
    }
    if (fr.mode.x == 13) {  // world position
      outColor = vec4(clamp((vWorldPos - fr.sceneMin.xyz) / fr.sceneExtent.xyz, 0.0, 1.0), 1.0); return;
    }
    if (fr.mode.x == 15) { outColor = vec4(idColor(gl_PrimitiveID), 1.0); return; }  // prim id
    if (fr.mode.x == 16) { outColor = vec4(idColor(ids.x), 1.0); return; }           // mesh id
    if (fr.mode.x == 19) {  // missing normals
      outColor = geoNrm ? vec4(0.95, 0.1, 0.85, 1.0) : vec4(0.2, 0.2, 0.2, 1.0); return;
    }
    if (fr.mode.x == 20) {  // double-sided
      outColor = dsided ? vec4(0.95, 0.55, 0.1, 1.0) : vec4(0.2, 0.2, 0.2, 1.0); return;
    }
    if (fr.mode.x == 18) { outColor = vec4(purposeColor(purpose), 1.0); return; }
    if (fr.mode.x == 29) { outColor = vec4(kindColor(kind), 1.0); return; }
    if (fr.mode.x == 26) { outColor = vec4(idColor(vInstanceId), 1.0); return; }  // instance id
    if (fr.mode.x == 25) {  // curvature (screen-space geometric normal variation)
      vec3 n = Ngeo;
      float c = clamp((length(dFdx(n)) + length(dFdy(n))) * 8.0, 0.0, 1.0);
      outColor = vec4(c, 1.0 - abs(c - 0.5) * 2.0, 1.0 - c, 1.0); return;
    }
    // Modes instanced prototypes cannot supply (UV/material scalars): neutral gray.
    outColor = vec4(0.18, 0.18, 0.18, 1.0); return;
  }
  vec3 V = normalize(fr.camPos.xyz - vWorldPos);
  vec3 L = (dot(fr.lightDir.xyz, fr.lightDir.xyz) > 1e-8)
               ? normalize(fr.lightDir.xyz)
               : normalize(vec3(1.0, 1.0, 1.0));
  vec3 lightColor = (dot(fr.lightColor.rgb, fr.lightColor.rgb) > 1e-8)
                        ? fr.lightColor.rgb
                        : vec3(1.0);
  float NdotL = max(dot(N, L), 0.0);
  vec3 H = normalize(L + V);
  float NdotH = max(dot(N, H), 0.0);
  vec3 amb = (fr.iblColor.w > 0.5)
                 ? texture(uIrradianceMap, normalize(mat3(fr.envRot) * N)).rgb *
                       fr.iblColor.rgb
                 : vec3(0.05);
  vec3 col = vColor * (amb + lightColor * NdotL) +
             lightColor * vec3(0.15) * pow(NdotH, 32.0);
  outColor = vec4(col, vOpacity);  // instanced prototypes carry no selection emissive
}
