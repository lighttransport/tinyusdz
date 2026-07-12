#version 450

layout(location = 0) in vec3 vNormalW;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) flat in int vDomJoint;   // dominant skin joint (SkinWeights AOV)
layout(location = 4) in float vDomWeight;
layout(location = 5) in vec2 vUV1;            // 2nd texcoord set (multi-UV AOV)
layout(location = 6) in float vMorphInfl;     // blendshape influence (world units)
layout(location = 7) in vec3 vColor;          // per-vertex displayColor (white = none)

// Base-color texture (white 1x1 when the material is untextured).
layout(set = 0, binding = 0) uniform sampler2D uBaseColorTex;
layout(set = 10, binding = 0) uniform sampler2D uMetalRoughTex;
layout(set = 11, binding = 0) uniform sampler2D uEmissiveTex;
layout(set = 12, binding = 0) uniform sampler2D uNormalTex;
layout(set = 13, binding = 0) uniform sampler2DArray uBaseColorUdimTex;
layout(set = 14, binding = 0) uniform sampler1D uBaseColorUdimLut;
layout(set = 15, binding = 0) uniform sampler2DArray uMetalRoughUdimTex;
layout(set = 16, binding = 0) uniform sampler1D uMetalRoughUdimLut;
layout(set = 17, binding = 0) uniform sampler2DArray uNormalUdimTex;
layout(set = 18, binding = 0) uniform sampler1D uNormalUdimLut;
layout(set = 19, binding = 0) uniform sampler2DArray uEmissiveUdimTex;
layout(set = 20, binding = 0) uniform sampler1D uEmissiveUdimLut;
// DomeLight split-sum IBL (1x1 black fallbacks when no dome is baked).
layout(set = 21, binding = 0) uniform samplerCube uIrradianceMap;
layout(set = 22, binding = 0) uniform samplerCube uPrefilteredMap;
layout(set = 23, binding = 0) uniform sampler2D uBrdfLut;
// Per-triangle source USD face id (source-face-id AOV). Indexed by the submesh's
// first triangle (flags bits 8-31) + gl_PrimitiveID (submesh-local).
layout(set = 3, binding = 0, std430) readonly buffer Faces { uint faceId[]; };

// Frame-constant UBO (set 5): camera + scene bbox + renderMode (shared with the
// vertex/tess stages). Per-draw material/ids come from the push block below.
layout(set = 5, binding = 0) uniform Frame {
  vec4 disp;
  mat4 viewProj;
  vec4 camPos;        // .xyz camera, .w depth normalizer
  vec4 sceneMin;      // .xyz
  vec4 sceneExtent;   // .xyz
  vec4 lightDir;      // .xyz preview key light direction toward the light
  vec4 lightColor;    // .rgb preview key light color/intensity
  ivec4 mode;         // .x = renderMode
  mat4 envRot;        // world -> environment rotation (dome IBL)
  vec4 iblColor;      // .rgb dome effectiveColor, .w = hasIbl (0/1)
  vec4 iblParams;     // .x = prefiltered mip count
} fr;

struct MaterialTexParam {
  vec4 baseUv0; vec4 baseUv1;
  vec4 mrUv0; vec4 mrUv1;
  vec4 normalUv0; vec4 normalUv1;
  vec4 emissiveUv0; vec4 emissiveUv1;
  vec4 dispUv0; vec4 dispUv1;
  vec4 baseScale; vec4 baseBias;
  vec4 normalScale; vec4 normalBias;
  vec4 emissiveScale; vec4 emissiveBias;
  vec4 scalar0;  // metallicChannel, roughnessChannel, metallicScale, metallicBias
  vec4 scalar1;  // roughnessScale, roughnessBias, displacementScale, displacementBias
  // Per-slot UV set: 0 = vUV (texcoords_0), 1 = vUV1 (texcoords_1).
  // x = base color, y = metal/rough, z = normal, w = emissive.
  // Displacement is absent on purpose: it is sampled in the vertex/tessellation
  // stages, which do not carry the second set.
  vec4 uvSets;
};
layout(set = 6, binding = 0, std430) readonly buffer MatTex { MaterialTexParam p[]; } mtp;

layout(push_constant) uniform PushConstants {
  mat4 model;
  vec4 baseColor;   // rgb + .w opacity
  vec4 matAux;      // .x metallic, .y roughness, .z alphaMode, .w alphaCutoff
  vec4 emissive;    // .xyz emissive (AOV)
  ivec4 ids;        // .x matId, .y flags, .z meshId
} pc;

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

vec4 sampleUdim(sampler2DArray tex, sampler1D lut, vec2 uv) {
  ivec2 tile = ivec2(floor(uv));
  int idx = tile.x + tile.y * 10;
  if (idx < 0 || idx >= 100) return vec4(1.0, 0.0, 1.0, 1.0);
  int layer = int(texelFetch(lut, idx, 0).r * 255.0 + 0.5) - 1;
  if (layer < 0) return vec4(1.0, 0.0, 1.0, 1.0);
  return texture(tex, vec3(fract(uv), float(layer)));
}

MaterialTexParam matTexParam() {
  return mtp.p[max(pc.ids.x, 0)];
}

vec2 xformUv(vec2 uv, vec4 row0, vec4 row1) {
  return vec2(dot(vec3(uv, 1.0), row0.xyz), dot(vec3(uv, 1.0), row1.xyz));
}

float channelOf(vec4 c, float chf) {
  int ch = int(chf + 0.5);
  if (ch == 1) return c.g;
  if (ch == 2) return c.b;
  if (ch == 3) return c.a;
  return c.r;
}

vec4 sampleBaseColor(vec2 uv) {
  MaterialTexParam m = matTexParam();
  vec2 suv = (m.uvSets.x > 0.5) ? vUV1 : uv;
  vec2 tuv = xformUv(suv, m.baseUv0, m.baseUv1);
  vec4 c = ((pc.ids.w & 1) != 0)
      ? sampleUdim(uBaseColorUdimTex, uBaseColorUdimLut, tuv)
      : texture(uBaseColorTex, tuv);
  return c * m.baseScale + m.baseBias;
}

vec4 sampleMetalRough(vec2 uv) {
  MaterialTexParam m = matTexParam();
  vec2 suv = (m.uvSets.y > 0.5) ? vUV1 : uv;
  vec2 tuv = xformUv(suv, m.mrUv0, m.mrUv1);
  return ((pc.ids.w & 2) != 0)
      ? sampleUdim(uMetalRoughUdimTex, uMetalRoughUdimLut, tuv)
      : texture(uMetalRoughTex, tuv);
}

vec4 sampleNormal(vec2 uv) {
  MaterialTexParam m = matTexParam();
  vec2 suv = (m.uvSets.z > 0.5) ? vUV1 : uv;
  vec2 tuv = xformUv(suv, m.normalUv0, m.normalUv1);
  vec4 c = ((pc.ids.w & 4) != 0)
      ? sampleUdim(uNormalUdimTex, uNormalUdimLut, tuv)
      : texture(uNormalTex, tuv);
  return c * m.normalScale + m.normalBias;
}

vec4 sampleEmissive(vec2 uv) {
  MaterialTexParam m = matTexParam();
  vec2 suv = (m.uvSets.w > 0.5) ? vUV1 : uv;
  vec2 tuv = xformUv(suv, m.emissiveUv0, m.emissiveUv1);
  vec4 c = ((pc.ids.w & 8) != 0)
      ? sampleUdim(uEmissiveUdimTex, uEmissiveUdimLut, tuv)
      : texture(uEmissiveTex, tuv);
  return c * m.emissiveScale + m.emissiveBias;
}

vec3 applyNormalMap(vec3 n) {
  if ((pc.ids.w & 16) == 0) {
    return n;
  }

  vec3 dp1 = dFdx(vWorldPos);
  vec3 dp2 = dFdy(vWorldPos);
  vec2 du1 = dFdx(vUV);
  vec2 du2 = dFdy(vUV);
  float r = du1.x * du2.y - du2.x * du1.y;
  vec3 t = dp1 * du2.y - dp2 * du1.y;
  t = (abs(r) > 1e-8) ? t / r : dp1;
  t = normalize(t - n * dot(n, t));
  vec3 b = normalize(cross(n, t)) * (r < 0.0 ? -1.0 : 1.0);
  vec3 nm = sampleNormal(vUV).xyz;
  return normalize(mat3(t, b, n) * nm);
}

void main() {
  vec3 N = normalize(vNormalW);
  // Debug AOVs.
  if (fr.mode.x != 0) {
    vec3 Ngeo = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
    if (fr.mode.x == 2) { outColor = vec4(N * 0.5 + 0.5, 1.0); return; }
    if (fr.mode.x == 3) { outColor = vec4(idColor(pc.ids.x), 1.0); return; }
    if (fr.mode.x == 4) { outColor = vec4(Ngeo * 0.5 + 0.5, 1.0); return; }
    if (fr.mode.x == 6) {
      float d = clamp(length(fr.camPos.xyz - vWorldPos) / max(fr.camPos.w, 1e-3), 0.0, 1.0);
      outColor = vec4(vec3(1.0 - d), 1.0);
      return;
    }
    if (fr.mode.x == 5) { outColor = vec4(fract(vUV), 0.0, 1.0); return; }
    if (fr.mode.x == 7) {  // albedo (unlit)
      outColor = vec4(pc.baseColor.rgb * vColor * sampleBaseColor(vUV).rgb, 1.0);
      return;
    }
    if (fr.mode.x == 8) {  // facing
      outColor = gl_FrontFacing ? vec4(0.1, 0.7, 0.1, 1.0) : vec4(0.7, 0.1, 0.1, 1.0);
      return;
    }
    if (fr.mode.x == 9) {
      MaterialTexParam m = matTexParam();
      vec4 mr = sampleMetalRough(vUV);
      outColor = vec4(vec3(pc.matAux.y * (channelOf(mr, m.scalar0.y) * m.scalar1.x + m.scalar1.y)), 1.0);
      return;
    }
    if (fr.mode.x == 10) {
      MaterialTexParam m = matTexParam();
      vec4 mr = sampleMetalRough(vUV);
      outColor = vec4(vec3(pc.matAux.x * (channelOf(mr, m.scalar0.x) * m.scalar0.z + m.scalar0.w)), 1.0);
      return;
    }
    if (fr.mode.x == 11) {                                                            // emissive
      outColor = vec4(pc.emissive.xyz * sampleEmissive(vUV).rgb, 1.0); return;
    }
    if (fr.mode.x == 12) {
      float opacity = clamp(pc.baseColor.a * sampleBaseColor(vUV).a, 0.0, 1.0);
      if (pc.matAux.z > 0.5 && pc.matAux.z < 1.5) {
        opacity = (opacity >= pc.matAux.w) ? 1.0 : 0.0;
      }
      outColor = vec4(vec3(opacity), 1.0);
      return;
    }
    if (fr.mode.x == 13) {  // world position
      outColor = vec4(clamp((vWorldPos - fr.sceneMin.xyz) / fr.sceneExtent.xyz, 0.0, 1.0), 1.0);
      return;
    }
    if (fr.mode.x == 23) {  // uv checker
      vec2 c = floor(fract(vUV) * 16.0);
      float k = mod(c.x + c.y, 2.0);
      outColor = vec4(vec3(mix(0.25, 0.85, k)), 1.0);
      return;
    }
    if (fr.mode.x == 15) { outColor = vec4(idColor(gl_PrimitiveID), 1.0); return; }  // prim id
    if (fr.mode.x == 16) { outColor = vec4(idColor(pc.ids.z), 1.0); return; }        // mesh id
    if (fr.mode.x == 19) {  // missing normals
      outColor = ((pc.ids.y & 1) != 0) ? vec4(0.95, 0.1, 0.85, 1.0) : vec4(0.2, 0.2, 0.2, 1.0);
      return;
    }
    if (fr.mode.x == 20) {  // double-sided
      outColor = ((pc.ids.y & 2) != 0) ? vec4(0.95, 0.55, 0.1, 1.0) : vec4(0.2, 0.2, 0.2, 1.0);
      return;
    }
    if (fr.mode.x == 18) {  // purpose (bits 2-3 of flags)
      outColor = vec4(purposeColor((pc.ids.y >> 2) & 3), 1.0);
      return;
    }
    if (fr.mode.x == 29) {  // kind (bits 4-6 of flags)
      outColor = vec4(kindColor((pc.ids.y >> 4) & 7), 1.0);
      return;
    }
    if (fr.mode.x == 30) {  // udim tile from UV set 0
      int tile = int(floor(vUV.x)) + 10 * int(floor(vUV.y));
      outColor = vec4(idColor(tile), 1.0);
      return;
    }
    if (fr.mode.x == 34) {  // source USD face id
      if ((pc.ids.y & 0x80) != 0) {
        int base = (pc.ids.y >> 8) & 0xFFFFFF;
        outColor = vec4(idColor(int(faceId[base + gl_PrimitiveID])), 1.0);
      } else {
        outColor = vec4(0.45, 0.45, 0.45, 1.0);
      }
      return;
    }
    if (fr.mode.x == 33) {  // texel density (UV/world area ratio, view-independent)
      vec2 du = dFdx(vUV), dv = dFdy(vUV);
      float uvArea = abs(du.x * dv.y - dv.x * du.y);
      float worldArea = length(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
      float td = sqrt(uvArea / max(worldArea, 1e-12));
      float c = clamp(td * fr.camPos.w * 0.5, 0.0, 1.0);
      outColor = vec4(c, 1.0 - abs(c - 0.5) * 2.0, 1.0 - c, 1.0);
      return;
    }
    if (fr.mode.x == 31) { outColor = vec4(fract(vUV1), 0.0, 1.0); return; }  // uv set 1
    if (fr.mode.x == 32) {  // blendshape influence (normalize by ~10% scene extent)
      float c = clamp(vMorphInfl / max(fr.camPos.w * 0.1, 1e-4), 0.0, 1.0);
      outColor = vec4(c, 1.0 - abs(c - 0.5) * 2.0, 1.0 - c, 1.0);
      return;
    }
    if (fr.mode.x == 21) {  // skin weights: dominant joint tinted by weight
      outColor = vec4(idColor(vDomJoint) * (0.3 + 0.7 * clamp(vDomWeight, 0.0, 1.0)), 1.0);
      return;
    }
    if (fr.mode.x == 22) {  // tangent (from UV gradient)
      vec3 dp1 = dFdx(vWorldPos), dp2 = dFdy(vWorldPos);
      vec2 du1 = dFdx(vUV), du2 = dFdy(vUV);
      float r = du1.x * du2.y - du2.x * du1.y;
      vec3 T = dp1 * du2.y - dp2 * du1.y;
      T = (abs(r) > 1e-8) ? T / r : dp1;
      outColor = vec4(normalize(T) * 0.5 + 0.5, 1.0);
      return;
    }
    if (fr.mode.x == 25) {  // curvature (screen-space normal variation)
      vec3 n = normalize(vNormalW);
      float c = clamp((length(dFdx(n)) + length(dFdy(n))) * 8.0, 0.0, 1.0);
      outColor = vec4(c, 1.0 - abs(c - 0.5) * 2.0, 1.0 - c, 1.0);
      return;
    }
    if (fr.mode.x == 26) { outColor = vec4(idColor(-1), 1.0); return; }  // instance id: raster non-instanced -> gray
  }
  vec4 baseSample = sampleBaseColor(vUV);
  float opacity = clamp(pc.baseColor.a * baseSample.a, 0.0, 1.0);
  if (pc.matAux.z > 0.5 && pc.matAux.z < 1.5) {
    if (opacity < pc.matAux.w) discard;
    opacity = 1.0;
  }
  // Per-vertex displayColor multiplies the base color (GL parity: attrib 9's
  // vColor does the same in material.cpp). White when the mesh has none.
  vec3 base = pc.baseColor.rgb * vColor * baseSample.rgb;
  MaterialTexParam m = matTexParam();
  vec4 mr = sampleMetalRough(vUV);
  float metallic = pc.matAux.x * (channelOf(mr, m.scalar0.x) * m.scalar0.z + m.scalar0.w);
  float roughness = pc.matAux.y * (channelOf(mr, m.scalar0.y) * m.scalar1.x + m.scalar1.y);
  vec3 emissive = pc.emissive.xyz * sampleEmissive(vUV).rgb;
  N = applyNormalMap(N);
  vec3 L = (dot(fr.lightDir.xyz, fr.lightDir.xyz) > 1e-8)
               ? normalize(fr.lightDir.xyz)
               : normalize(vec3(0.5, 0.8, 0.6));
  vec3 lightColor = (dot(fr.lightColor.rgb, fr.lightColor.rgb) > 1e-8)
                        ? fr.lightColor.rgb
                        : vec3(1.0);
  float diff = max(dot(N, L), 0.0);
  vec3 V = normalize(fr.camPos.xyz - vWorldPos);
  vec3 H = normalize(L + V);
  float spec = pow(max(dot(N, H), 0.0), mix(96.0, 8.0, clamp(roughness, 0.0, 1.0)));
  // Ambient: DomeLight split-sum IBL when baked, else the constant floor
  // (matches the GL raster path in light3d/material.cpp).
  vec3 ambient;
  if (fr.iblColor.w > 0.5) {
    float rgh = clamp(roughness, 0.0, 1.0);
    vec3 Ne = normalize(mat3(fr.envRot) * N);
    vec3 Re = normalize(mat3(fr.envRot) * reflect(-V, N));
    vec3 irr = texture(uIrradianceMap, Ne).rgb;
    vec3 pref = textureLod(uPrefilteredMap, Re, rgh * (fr.iblParams.x - 1.0)).rgb;
    vec2 dfg = texture(uBrdfLut, vec2(max(dot(N, V), 0.0), rgh)).rg;
    vec3 F0 = mix(vec3(0.04), base, clamp(metallic, 0.0, 1.0));
    ambient = (base * (1.0 - clamp(metallic, 0.0, 1.0)) * irr +
               pref * (F0 * dfg.x + dfg.y)) * fr.iblColor.rgb;
  } else {
    ambient = base * 0.25;
  }
  vec3 c = ambient + base * lightColor * (0.85 * diff) +
           lightColor * vec3(spec) * mix(0.04, 0.35, clamp(metallic, 0.0, 1.0)) +
           emissive;
  if (pc.matAux.z > 1.5 && opacity < 1.0) {
    c *= opacity;  // pipeline uses premultiplied alpha blending
  }
  outColor = vec4(c, opacity);
}
