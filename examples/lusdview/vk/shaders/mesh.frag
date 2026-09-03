#version 450

layout(location = 0) in vec3 vNormalW;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) flat in int vDomJoint;   // dominant skin joint (SkinWeights AOV)
layout(location = 4) in float vDomWeight;
layout(location = 5) in vec2 vUV1;            // 2nd texcoord set (multi-UV AOV)
layout(location = 6) in float vMorphInfl;     // blendshape influence (world units)
layout(location = 7) in vec4 vColor;          // displayColor.rgb + displayOpacity
struct RasterLight { vec4 positionType; vec4 directionAngle;
                     vec4 colorDiffuse; vec4 specularShape; vec4 areaParams;
                     vec4 iesAxisX; vec4 iesAxisY; vec4 iesProfile[6]; };

// Base-color texture (white 1x1 when the material is untextured).
layout(set = 0, binding = 0) uniform sampler2D uBaseColorTex;
layout(set = 0, binding = 1) uniform sampler2D uMetallicTex;
layout(set = 0, binding = 2) uniform sampler2D uEmissiveTex;
layout(set = 0, binding = 3) uniform sampler2D uNormalTex;
layout(set = 0, binding = 4) uniform sampler2DArray uBaseColorUdimTex;
layout(set = 0, binding = 5) uniform sampler2D uUdimLutAtlas;
layout(set = 0, binding = 6) uniform sampler2DArray uMetallicUdimTex;
layout(set = 0, binding = 7) uniform sampler2DArray uNormalUdimTex;
layout(set = 0, binding = 8) uniform sampler2DArray uEmissiveUdimTex;
layout(set = 0, binding = 9) uniform sampler2D uOpacityTex;
layout(set = 0, binding = 10) uniform sampler2DArray uOpacityUdimTex;
layout(set = 0, binding = 11) uniform sampler2D uRoughnessTex;
// DomeLight split-sum IBL (1x1 black fallbacks when no dome is baked).
layout(set = 0, binding = 12) uniform samplerCube uIrradianceMap;
layout(set = 0, binding = 13) uniform samplerCube uPrefilteredMap;
layout(set = 0, binding = 14) uniform sampler2D uBrdfLut;
layout(set = 0, binding = 15) uniform sampler2DArray uRoughnessUdimTex;
layout(set = 0, binding = 17) uniform sampler2D uOcclusionTex;
layout(set = 0, binding = 18) uniform sampler2DArray uOcclusionUdimTex;
layout(set = 0, binding = 19) uniform sampler2D uShadowMap;
// Extra material slots (ordinary 2D only; UDIM sources are treated as unbound).
layout(set = 0, binding = 20) uniform sampler2D uSpecularColorTex;
layout(set = 0, binding = 21) uniform sampler2D uCoatWeightTex;
layout(set = 0, binding = 22) uniform sampler2D uCoatColorTex;
layout(set = 0, binding = 23) uniform sampler2D uCoatRoughnessTex;
layout(set = 0, binding = 24) uniform sampler2D uCoatNormalTex;
layout(set = 0, binding = 25) uniform samplerCube uPointShadowMap;
layout(set = 0, binding = 26) uniform sampler2DArray uSpecularColorUdimTex;
layout(set = 0, binding = 27) uniform sampler2DArray uCoatWeightUdimTex;
layout(set = 0, binding = 28) uniform sampler2DArray uCoatColorUdimTex;
layout(set = 0, binding = 29) uniform sampler2DArray uCoatRoughnessUdimTex;
layout(set = 0, binding = 30) uniform sampler2DArray uCoatNormalUdimTex;
// Bounded per-material graph image slots. They avoid descriptor indexing on
// low-limit raster devices while allowing arbitrary graph images to be sampled
// independently of the semantic material slots.
layout(set = 0, binding = 32) uniform sampler2D uGraphTex0;
layout(set = 0, binding = 33) uniform sampler2D uGraphTex1;
layout(set = 0, binding = 34) uniform sampler2D uGraphTex2;
layout(set = 0, binding = 35) uniform sampler2D uGraphTex3;
layout(set = 0, binding = 36) uniform sampler2D uGraphTex4;
layout(set = 0, binding = 37) uniform sampler2D uGraphTex5;
layout(set = 0, binding = 38) uniform sampler2D uGraphTex6;
layout(set = 0, binding = 39) uniform sampler2D uGraphTex7;
layout(set = 0, binding = 40) uniform sampler2DArray uGraphUdim0;
layout(set = 0, binding = 41) uniform sampler2DArray uGraphUdim1;
layout(set = 0, binding = 42) uniform sampler2DArray uGraphUdim2;
layout(set = 0, binding = 43) uniform sampler2DArray uGraphUdim3;
layout(set = 0, binding = 44) uniform sampler2DArray uGraphUdim4;
layout(set = 0, binding = 45) uniform sampler2DArray uGraphUdim5;
layout(set = 0, binding = 46) uniform sampler2DArray uGraphUdim6;
layout(set = 0, binding = 47) uniform sampler2DArray uGraphUdim7;
#ifdef LUSDVIEW_OIT
// Opaque color from the completed first raster pass. Weighted OIT renders into
// separate accumulation attachments, so this is safe to sample for refraction.
layout(set = 0, binding = 48) uniform sampler2D uOpaqueSceneColor;
#endif
// Per-triangle source USD face id (source-face-id AOV). Indexed by the submesh's
// first triangle (flags bits 8-31) + gl_PrimitiveID (submesh-local).
layout(set = 1, binding = 6, std430) readonly buffer Faces { uint faceId[]; };
// Compact Ptex face rectangles. Keeping this out of the sampled image permits
// the atlas and its streamed physical pages to use BC7 block compression.
layout(set = 1, binding = 7, std430) readonly buffer PtexRects {
  uvec4 rect[];
} ptexRects;

// Frame-constant UBO (set 5): camera + scene bbox + renderMode (shared with the
// vertex/tess stages). Per-draw material/ids come from the push block below.
layout(set = 2, binding = 0) uniform Frame {
  vec4 disp;
  mat4 viewProj;
  vec4 camPos;        // .xyz camera, .w depth normalizer
  vec4 sceneMin;      // .xyz
  vec4 sceneExtent;   // .xyz
  vec4 lightDir;      // .xyz preview key light direction toward the light
  vec4 lightColor;    // .rgb preview key light color/intensity
  RasterLight rasterLights[16];
  uvec4 rasterLightInfo;
  ivec4 mode;         // .x = renderMode
  mat4 envRot;        // world -> environment rotation (dome IBL)
  vec4 iblColor;      // .rgb dome effectiveColor, .w = hasIbl (0/1)
  vec4 iblParams;     // .x = prefiltered mip count, .y = exposure stops
  mat4 shadowViewProj;
  vec4 pointShadowLight; // xyz position, w = point cube shadow enabled
  mat4 pointShadowViewProj[6];
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
  // Specular F0 (T12): rgb = inputs:specularColor, w = ior with the specular-
  // workflow flag in its sign (w < 0 -> specular workflow, F0 = specularColor).
  vec4 specParams;
  vec4 opacityUv0; vec4 opacityUv1;
  vec4 opacityParams; // channel, scale, bias, uvSet
  vec4 udimSlots0;    // base, metal/rough, normal, emissive atlas rows
  vec4 udimSlots1;    // x = opacity atlas row
  vec4 roughUv0; vec4 roughUv1; // roughUv0.w = UV-set selector
  vec4 coatParams;    // weight, roughness, ior, occlusion
  vec4 coatColor;
  vec4 occlusionUv0; vec4 occlusionUv1;
  vec4 occlusionParams; // channel, scale, bias, uvSet
  // Extra semantic slots (specular color / coat weight / coat color / coat
  // roughness). The loaders neutralize the matching constant to 1.0 whenever a
  // texture is bound, so constant * texel is always the right combine.
  vec4 specColorUv0; vec4 specColorUv1;
  vec4 coatWeightUv0; vec4 coatWeightUv1;
  vec4 coatColorUv0; vec4 coatColorUv1;
  vec4 coatRoughUv0; vec4 coatRoughUv1;
  // coatWeightChannel, coatRoughnessChannel, coatWeightUvSet, coatRoughUvSet
  vec4 coatTexParams;
  // specColorUvSet, coatColorUvSet, unused, unused
  vec4 extraUvSets;
  vec4 specColorScale; vec4 specColorBias;
  vec4 coatWeightScale; vec4 coatWeightBias;
  vec4 coatColorScale; vec4 coatColorBias;
  vec4 coatRoughScale; vec4 coatRoughBias;
  vec4 coatNormalUv0; vec4 coatNormalUv1;
  vec4 coatNormalScale; vec4 coatNormalBias;
  vec4 semanticUdimSlots;
  vec4 semanticUdimSlots2;
  vec4 ptexBaseInfo; // rect texel offset, face count, enabled, reserved
  vec4 ptexMetalInfo;
  vec4 ptexRoughInfo;
  vec4 ptexNormalInfo;
  vec4 ptexEmissiveInfo;
  vec4 ptexOpacityInfo;
  vec4 ptexOcclusionInfo;
  vec4 ptexSpecularInfo;
  vec4 ptexCoatWeightInfo;
  vec4 ptexCoatColorInfo;
  vec4 ptexCoatRoughInfo;
  // Approximate transmission for raster: weight, depth, dispersion, thin-wall.
  vec4 transmissionParams;
  vec4 transmissionColor;
  vec4 volumeParams;  // density, emission scale
  vec4 volumeAlbedo;
  vec4 volumeEmission;
  vec4 subsurfaceParams; // weight, scale, radius, reserved
  vec4 subsurfaceColor;
};
layout(set = 3, binding = 0, std430) readonly buffer MatTex { MaterialTexParam p[]; } mtp;
layout(set = 3, binding = 1, std430) readonly buffer MatGraph { float graphRows[]; } mgraph;

layout(push_constant) uniform PushConstants {
  mat4 model;
  vec4 baseColor;   // rgb + .w opacity
  vec4 matAux;      // .x metallic, .y roughness, .z alphaMode, .w alphaCutoff
  vec4 emissive;    // .xyz emissive (AOV)
  ivec4 ids;        // .x matId, .y flags, .z meshId
} pc;

#ifdef LUSDVIEW_OIT
layout(location = 0) out vec4 outAccum;
layout(location = 1) out float outReveal;
vec4 outColor;
#else
layout(location = 0) out vec4 outColor;
#endif

vec3 idColor(int id) {
  if (id < 0) return vec3(0.45);
  uint h = (uint(id) + 1u) * 2654435761u;
  return vec3(float(h & 255u), float((h >> 8) & 255u), float((h >> 16) & 255u)) * (1.0 / 255.0);
}

int logicalMaterialId() {
  return int(floatBitsToUint(pc.emissive.w) >> 1u) - 1;
}

// Linear -> sRGB OETF for the final shaded output. The scene is lit in linear
// space (sRGB base-color textures are uploaded as _SRGB, so the sampler
// linearizes them; constants are already linear), and the framebuffer is UNORM,
// so the encode happens here. Applied only to the lit path -- AOVs stay raw.
vec3 linearToSrgb(vec3 c) {
  c = clamp(c, 0.0, 1.0);
  vec3 lo = c * 12.92;
  vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
  return mix(lo, hi, greaterThan(c, vec3(0.0031308)));
}

vec3 srgbToLinear(vec3 c) {
  c = clamp(c, 0.0, 1.0);
  vec3 lo = c / 12.92;
  vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
  return mix(hi, lo, lessThanEqual(c, vec3(0.04045)));
}

// Narkowicz' compact ACES fit. The high-quality raster path keeps lighting in
// scene-linear space until this display transform, preserving HDR highlights
// from DomeLight images instead of clipping every channel at one.
vec3 acesFitted(vec3 x) {
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
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

vec4 sampleUdim(sampler2DArray tex, int slot, vec2 uv, vec4 missing) {
  ivec2 tile = ivec2(floor(uv));
  int idx = tile.x + tile.y * 10;
  if (idx < 0 || idx >= 100) return missing;
  int layer = int(texelFetch(uUdimLutAtlas, ivec2(idx, slot), 0).r * 255.0 + 0.5) - 1;
  if (layer < 0) return missing;
  return texture(tex, vec3(fract(uv), float(layer)));
}

vec4 sampleGraphImage(int slot, float udimRow, vec2 uv, vec4 missing) {
  if (slot < 0 || slot >= 8) return missing;
  bool udim = udimRow >= 0.0;
  if (slot == 0) return udim ? sampleUdim(uGraphUdim0, int(udimRow + 0.5), uv, missing) : texture(uGraphTex0, uv);
  if (slot == 1) return udim ? sampleUdim(uGraphUdim1, int(udimRow + 0.5), uv, missing) : texture(uGraphTex1, uv);
  if (slot == 2) return udim ? sampleUdim(uGraphUdim2, int(udimRow + 0.5), uv, missing) : texture(uGraphTex2, uv);
  if (slot == 3) return udim ? sampleUdim(uGraphUdim3, int(udimRow + 0.5), uv, missing) : texture(uGraphTex3, uv);
  if (slot == 4) return udim ? sampleUdim(uGraphUdim4, int(udimRow + 0.5), uv, missing) : texture(uGraphTex4, uv);
  if (slot == 5) return udim ? sampleUdim(uGraphUdim5, int(udimRow + 0.5), uv, missing) : texture(uGraphTex5, uv);
  if (slot == 6) return udim ? sampleUdim(uGraphUdim6, int(udimRow + 0.5), uv, missing) : texture(uGraphTex6, uv);
  return udim ? sampleUdim(uGraphUdim7, int(udimRow + 0.5), uv, missing) : texture(uGraphTex7, uv);
}

bool hasGraphRoute(int route) {
  int mid = max(pc.ids.x, 0);
  uint base = uint(mid) * 1394u;
  return route >= 0 && route < 44 && base + uint(route) + 1u < uint(mgraph.graphRows.length()) &&
         mgraph.graphRows[base + uint(route) + 1u] >= 0.0;
}

vec4 evalRasterMaterialXGraph(int route, vec2 uv) {
  int mid = max(pc.ids.x, 0);
  uint base = uint(mid) * 1394u;
  if (!hasGraphRoute(route)) return vec4(0.0);
  int count = int(clamp(mgraph.graphRows[base], 0.0, 64.0));
  int wanted = int(mgraph.graphRows[base + uint(route) + 1u] + 0.5);
  if (count <= 0 || wanted < 0 || wanted >= count) return vec4(0.0);
  vec4 v[64];
  // CompileMaterialXGraphRuntime packs dependencies before their consumers.
  for (int i = 0; i < count; ++i) {
      uint p = base + 50u + uint(i) * 21u;
      int op = int(mgraph.graphRows[p] + 0.5);
      int a = int(mgraph.graphRows[p + 1u] + 0.5);
      int b = int(mgraph.graphRows[p + 2u] + 0.5);
      int c = int(mgraph.graphRows[p + 3u] + 0.5);
      vec4 av = (a >= 0 && a < count) ? v[a] : vec4(mgraph.graphRows[p + 4u], mgraph.graphRows[p + 5u], mgraph.graphRows[p + 6u], mgraph.graphRows[p + 7u]);
      vec4 bv = (b >= 0 && b < count) ? v[b] : vec4(mgraph.graphRows[p + 8u], mgraph.graphRows[p + 9u], mgraph.graphRows[p + 10u], mgraph.graphRows[p + 11u]);
      vec4 cv = (c >= 0 && c < count) ? v[c] : vec4(mgraph.graphRows[p + 12u], mgraph.graphRows[p + 13u], mgraph.graphRows[p + 14u], mgraph.graphRows[p + 15u]);
      vec4 value = av;
      int encoded = int(mgraph.graphRows[p + 16u] + (mgraph.graphRows[p + 16u] < 0.0 ? -0.5 : 0.5));
      vec2 graphUv = uv;
      int uvInput = int(floor(mgraph.graphRows[p + 15u] + 0.5));
      if (uvInput == 0) graphUv = av.xy;
      else if (uvInput == 1) graphUv = bv.xy;
      else if (uvInput == 2) graphUv = cv.xy;
      graphUv = graphUv * vec2(mgraph.graphRows[p + 17u], mgraph.graphRows[p + 18u]) +
                vec2(mgraph.graphRows[p + 19u], mgraph.graphRows[p + 20u]);
      if (op == 0) v[i] = value;
      else if (op == 1 || op == 2) {
        int slot = encoded;
        float row = -1.0;
        if (encoded < 0) { slot = -encoded - 1; row = value.w; }
        v[i] = sampleGraphImage(slot, row, graphUv, value);
      } else if (op == 3) v[i] = vec4(normalize(av.xyz * 2.0 - 1.0) * 0.5 + 0.5, av.w);
      else if (op == 4) v[i] = av + bv;
      else if (op == 5) v[i] = av - bv;
      else if (op == 6) v[i] = av * bv;
      else if (op == 7) v[i] = av / max(abs(bv), vec4(1e-6));
      else if (op == 8) v[i] = mix(av, bv, cv);
      else if (op == 9) v[i] = clamp(av, bv, cv);
      else if (op == 10) v[i] = vec4(dot(av.xyz, bv.xyz));
      else if (op == 11) v[i] = vec4(normalize(av.xyz), av.w);
      else if (op == 12) v[i] = pow(max(av, vec4(0.0)), bv);
      else if (op == 13) v[i] = min(av, bv);
      else if (op == 14) v[i] = max(av, bv);
      else if (op == 15) v[i] = abs(av);
      else if (op == 16) v[i] = sqrt(max(av, vec4(0.0)));
      else if (op == 17) v[i] = sin(av);
      else if (op == 18) v[i] = cos(av);
      else if (op == 19) { float l = dot(av.xyz, vec3(0.2126, 0.7152, 0.0722)); v[i] = vec4(l, l, l, av.w); }
      else if (op == 20) v[i] = av.x >= 0.5 ? bv : cv;
      else if (op == 21) v[i] = vec4(mgraph.graphRows[p + 14u] > 0.5 ? vUV1 : uv,
                                      0.0, 1.0);
      else if (op == 22) v[i] = floor(av);
      else if (op == 23) v[i] = ceil(av);
      else if (op == 24) v[i] = fract(av);
      else if (op == 25) v[i] = step(av, bv);
      else if (op == 26) v[i] = smoothstep(bv, cv, av);
      else if (op == 27) v[i] = vec4(cross(av.xyz, bv.xyz), av.w);
      else if (op == 28) v[i] = vec4(length(av.xyz));
      else if (op == 29) v[i] = vec4(fract(sin(dot(av.xy + uv, vec2(127.1, 311.7))) * 43758.5453));
      else if (op == 40) v[i] = vec4(fract(sin(dot(av.xyz + vec3(uv, uv.x + uv.y),
                                                        vec3(127.1, 311.7, 74.7))) *
                                           43758.5453));
      else if (op == 30) v[i] = tan(av);
      else if (op == 31) v[i] = exp(av);
      else if (op == 32) v[i] = log(max(av, vec4(1e-6)));
      else if (op == 33) v[i] = mod(av, max(abs(bv), vec4(1e-6)));
      else if (op == 34) v[i] = vec4(1.0) - av;
      else if (op == 35) v[i] = (av - bv) / max(cv - bv, vec4(1e-6));
      else if (op == 36) v[i] = vec4(0.0, 0.0, 1.0, 1.0);
      else if (op == 37) v[i] = vec4(1.0, 0.0, 0.0, 1.0);
      else if (op == 38) {
        vec3 axis = normalize(bv.xyz);
        float ang = radians(av.x);
        vec3 q = cv.xyz;
        v[i] = vec4(q * cos(ang) + cross(axis, q) * sin(ang) +
                    axis * dot(axis, q) * (1.0 - cos(ang)), cv.w);
      }
      else if (op == 39) {
        v[i] = vec4(av.xy * vec2(mgraph.graphRows[p + 17u], mgraph.graphRows[p + 18u]) +
                    vec2(mgraph.graphRows[p + 19u], mgraph.graphRows[p + 20u]),
                    av.zw);
      }
      else v[i] = value;
  }
  return v[wanted];
}

// Specular F0 (T12): specular workflow -> specularColor directly; else the
// dielectric reflectance from ior lerped toward base by metalness. ior 1.5 (the
// default) gives exactly 0.04, matching the old fixed constant.
vec3 computeF0(vec3 base, float metallic) {
  vec4 sp = mtp.p[max(pc.ids.x, 0)].specParams;
  if (sp.w < 0.0) return sp.rgb;                 // specular workflow
  bool openPbr = sp.w > 100.0;
  float ior = max(1.0, openPbr ? sp.w - 100.0 : sp.w);
  float d = (ior - 1.0) / (ior + 1.0);
  vec3 dielectric = vec3(d * d) * (openPbr ? sp.rgb : vec3(1.0));
  return mix(dielectric, base, clamp(metallic, 0.0, 1.0));
}

const float kPi = 3.14159265358979323846;

float distributionGGX(float NoH, float roughness) {
  float a = max(roughness * roughness, 0.002);
  float a2 = a * a;
  float d = NoH * NoH * (a2 - 1.0) + 1.0;
  return a2 / max(kPi * d * d, 1e-6);
}

float geometrySchlickGGX(float NoX, float roughness) {
  float r = roughness + 1.0;
  float k = (r * r) * 0.125;
  return NoX / max(NoX * (1.0 - k) + k, 1e-6);
}

vec3 fresnelSchlick(float VoH, vec3 f0) {
  float f = pow(1.0 - clamp(VoH, 0.0, 1.0), 5.0);
  return f0 + (vec3(1.0) - f0) * f;
}

// Exact unpolarized Fresnel for a smooth dielectric interface.  Schlick is a
// good BRDF approximation, but its grazing-angle error is visible when it is
// also used to decide how much energy enters clear lantern glass.
float fresnelDielectric(float cosThetaI, float etaI, float etaT) {
  float ci = clamp(abs(cosThetaI), 0.0, 1.0);
  float eta = etaI / max(etaT, 1.0e-6);
  float sinThetaT2 = eta * eta * max(0.0, 1.0 - ci * ci);
  if (sinThetaT2 >= 1.0) return 1.0;
  float ct = sqrt(max(0.0, 1.0 - sinThetaT2));
  float rs = (etaI * ci - etaT * ct) /
             max(etaI * ci + etaT * ct, 1.0e-6);
  float rp = (etaT * ci - etaI * ct) /
             max(etaT * ci + etaI * ct, 1.0e-6);
  return 0.5 * (rs * rs + rp * rp);
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

uint sourceFaceForPtex() {
  if ((pc.ids.y & 0x80) == 0) return 0u;
  uint base = uint((pc.ids.y >> 8) & 0xFFFFFF);
  return faceId[base + uint(gl_PrimitiveID)];
}

vec2 ptexUv(sampler2D tex, vec2 uv, vec4 info) {
  if (info.z <= 0.5 || (pc.ids.y & 0x80) == 0) return uv;
  uint face = sourceFaceForPtex();
  if (face >= uint(info.y)) return uv;
  ivec2 size = textureSize(tex, 0);
  uint value[4];
  uvec4 rect = ptexRects.rect[uint(info.x + 0.5) + face];
  value[0] = rect.x; value[1] = rect.y;
  value[2] = rect.z; value[3] = rect.w;
  if (value[2] == 0u || value[3] == 0u) return uv;
  vec2 px = vec2(float(value[0]), float(value[1])) +
            vec2(clamp(uv.x, 0.0, 1.0), 1.0 - clamp(uv.y, 0.0, 1.0)) *
            vec2(float(value[2] - 1u), float(value[3] - 1u));
  return (px + vec2(0.5)) / vec2(size);
}

vec4 sampleBaseColor(vec2 uv) {
  MaterialTexParam m = matTexParam();
  vec2 suv = (m.uvSets.x > 0.5) ? vUV1 : uv;
  vec2 tuv = xformUv(suv, m.baseUv0, m.baseUv1);
  uint ptexFace = sourceFaceForPtex();
  vec2 ptexUv = tuv;
  bool validPtex = m.ptexBaseInfo.z > 0.5 && (pc.ids.y & 0x80) != 0 &&
                   ptexFace < uint(m.ptexBaseInfo.y);
  if (validPtex) {
    uint values[4];
    ivec2 size = textureSize(uBaseColorTex, 0);
    uvec4 rect = ptexRects.rect[uint(m.ptexBaseInfo.x + 0.5) + ptexFace];
    values[0] = rect.x; values[1] = rect.y;
    values[2] = rect.z; values[3] = rect.w;
    vec2 px = vec2(float(values[0]), float(values[1])) +
              vec2(clamp(tuv.x, 0.0, 1.0),
                   1.0 - clamp(tuv.y, 0.0, 1.0)) *
              vec2(float(max(values[2], 1u) - 1u),
                   float(max(values[3], 1u) - 1u));
    ptexUv = (px + vec2(0.5)) / vec2(size);
  }
  vec4 c = validPtex
      ? texture(uBaseColorTex, ptexUv)
      : ((pc.ids.w & 1) != 0)
      ? sampleUdim(uBaseColorUdimTex, int(m.udimSlots0.x + 0.5), tuv,
                   vec4(1.0, 0.0, 1.0, 1.0))
      : texture(uBaseColorTex, tuv);
  return c * m.baseScale + m.baseBias;
}

vec4 sampleMetallic(vec2 uv) {
  MaterialTexParam m = matTexParam();
  vec2 suv = (m.uvSets.y > 0.5) ? vUV1 : uv;
  vec2 tuv = xformUv(suv, m.mrUv0, m.mrUv1);
  tuv = ptexUv(uMetallicTex, tuv, m.ptexMetalInfo);
  return ((pc.ids.w & 2) != 0)
      ? sampleUdim(uMetallicUdimTex, int(m.udimSlots0.y + 0.5), tuv,
                   vec4(1.0, 0.0, 1.0, 1.0))
      : texture(uMetallicTex, tuv);
}

vec4 sampleRoughness(vec2 uv) {
  MaterialTexParam m = matTexParam();
  vec2 suv = (m.roughUv0.w > 0.5) ? vUV1 : uv;
  vec2 tuv = xformUv(suv, m.roughUv0, m.roughUv1);
  tuv = ptexUv(uRoughnessTex, tuv, m.ptexRoughInfo);
  return ((pc.ids.w & 512) != 0)
      ? sampleUdim(uRoughnessUdimTex, int(m.udimSlots1.y + 0.5), tuv,
                   vec4(1.0))
      : texture(uRoughnessTex, tuv);
}

vec4 sampleNormal(vec2 uv) {
  MaterialTexParam m = matTexParam();
  vec2 suv = (m.uvSets.z > 0.5) ? vUV1 : uv;
  vec2 tuv = xformUv(suv, m.normalUv0, m.normalUv1);
  tuv = ptexUv(uNormalTex, tuv, m.ptexNormalInfo);
  vec4 c = ((pc.ids.w & 4) != 0)
      ? sampleUdim(uNormalUdimTex, int(m.udimSlots0.z + 0.5), tuv,
                   vec4(0.5, 0.5, 1.0, 1.0))
      : texture(uNormalTex, tuv);
  return c * m.normalScale + m.normalBias;
}

vec4 sampleEmissive(vec2 uv) {
  MaterialTexParam m = matTexParam();
  vec2 suv = (m.uvSets.w > 0.5) ? vUV1 : uv;
  vec2 tuv = xformUv(suv, m.emissiveUv0, m.emissiveUv1);
  tuv = ptexUv(uEmissiveTex, tuv, m.ptexEmissiveInfo);
  vec4 c = ((pc.ids.w & 8) != 0)
      ? sampleUdim(uEmissiveUdimTex, int(m.udimSlots0.w + 0.5), tuv,
                   vec4(1.0, 0.0, 1.0, 1.0))
      : texture(uEmissiveTex, tuv);
  return c * m.emissiveScale + m.emissiveBias;
}

vec3 applyCoatNormalMap(vec3 n) {
  MaterialTexParam m = matTexParam();
  if (m.coatNormalScale.w < 0.5) return n;
  vec2 suv = m.coatNormalBias.w > 0.5 ? vUV1 : vUV;
  vec2 uv = xformUv(suv, m.coatNormalUv0, m.coatNormalUv1);
  vec4 sampledNormal = m.semanticUdimSlots2.x >= 0.0
      ? sampleUdim(uCoatNormalUdimTex, int(m.semanticUdimSlots2.x + 0.5), uv,
                   vec4(0.5, 0.5, 1.0, 1.0))
      : texture(uCoatNormalTex, uv);
  vec3 nm = (sampledNormal * m.coatNormalScale +
             m.coatNormalBias).xyz;
  vec3 dp1 = dFdx(vWorldPos), dp2 = dFdy(vWorldPos);
  vec2 du1 = dFdx(uv), du2 = dFdy(uv);
  float r = du1.x * du2.y - du2.x * du1.y;
  vec3 t = dp1 * du2.y - dp2 * du1.y;
  t = (abs(r) > 1e-8) ? t / r : dp1;
  t = normalize(t - n * dot(n, t));
  vec3 b = normalize(cross(n, t)) * (r < 0.0 ? -1.0 : 1.0);
  return normalize(mat3(t, b, n) * nm);
}

float sampleOpacity(vec2 uv) {
  if ((pc.ids.w & 64) == 0) return 1.0;
  MaterialTexParam m = matTexParam();
  vec2 suv = (m.opacityParams.w > 0.5) ? vUV1 : uv;
  vec2 tuv = xformUv(suv, m.opacityUv0, m.opacityUv1);
  tuv = ptexUv(uOpacityTex, tuv, m.ptexOpacityInfo);
  vec4 c = ((pc.ids.w & 128) != 0)
      ? sampleUdim(uOpacityUdimTex, int(m.udimSlots1.x + 0.5), tuv, vec4(1.0))
      : texture(uOpacityTex, tuv);
  return clamp(channelOf(c, m.opacityParams.x) * m.opacityParams.y +
               m.opacityParams.z, 0.0, 1.0);
}

float sampleOcclusion(vec2 uv) {
  if ((pc.ids.w & 1024) == 0) return 1.0;
  MaterialTexParam m = matTexParam();
  vec2 suv = (m.occlusionParams.w > 0.5) ? vUV1 : uv;
  vec2 tuv = xformUv(suv, m.occlusionUv0, m.occlusionUv1);
  vec4 c = ((pc.ids.w & 2048) != 0)
      ? sampleUdim(uOcclusionUdimTex, int(m.udimSlots1.z + 0.5), tuv,
                   vec4(1.0))
      : texture(uOcclusionTex, tuv);
  return clamp(channelOf(c, m.occlusionParams.x) * m.occlusionParams.y +
               m.occlusionParams.z, 0.0, 1.0);
}

// Scalar coat slot: returns 1.0 when unbound so constant * 1.0 is a no-op.
// A negative packed channel selector means "use channel 0 (R)".
float sampleCoatScalar(sampler2D tex, bool has, vec4 uv0, vec4 uv1,
                       float uvSet, float channel, vec4 scale, vec4 bias) {
  if (!has) return 1.0;
  vec2 suv = (uvSet > 0.5) ? vUV1 : vUV;
  float ch = (channel < 0.0) ? 0.0 : channel;
  vec4 c = texture(tex, xformUv(suv, uv0, uv1)) * scale + bias;
  return clamp(channelOf(c, ch), 0.0, 1.0);
}

vec3 sampleColorSlot(sampler2D tex, bool has, vec4 uv0, vec4 uv1, float uvSet,
                     vec4 scale, vec4 bias) {
  if (!has) return vec3(1.0);
  vec2 suv = (uvSet > 0.5) ? vUV1 : vUV;
  return (texture(tex, xformUv(suv, uv0, uv1)) * scale + bias).rgb;
}

float sampleCoatScalarUdim(sampler2D tex, sampler2DArray udimTex,
                           bool ordinary, float row, vec4 uv0, vec4 uv1,
                           float uvSet, float channel, vec4 scale, vec4 bias,
                           vec4 ptexInfo) {
  bool udim = !ordinary && row >= 0.0;
  if (!ordinary && !udim) return 1.0;
  vec2 suv = uvSet > 0.5 ? vUV1 : vUV;
  vec2 uv = xformUv(suv, uv0, uv1);
  uv = ptexUv(tex, uv, ptexInfo);
  vec4 c = udim ? sampleUdim(udimTex, int(row + 0.5), uv, vec4(1.0))
                : texture(tex, uv);
  return clamp(channelOf(c * scale + bias, channel < 0.0 ? 0.0 : channel),
               0.0, 1.0);
}

vec3 sampleCoatColorUdim(bool ordinary, float row, vec4 uv0, vec4 uv1,
                         float uvSet, vec4 scale, vec4 bias, vec4 ptexInfo) {
  bool udim = !ordinary && row >= 0.0;
  if (!ordinary && !udim) return vec3(1.0);
  vec2 suv = uvSet > 0.5 ? vUV1 : vUV;
  vec2 uv = xformUv(suv, uv0, uv1);
  uv = ptexUv(uCoatColorTex, uv, ptexInfo);
  vec4 c = udim ? sampleUdim(uCoatColorUdimTex, int(row + 0.5), uv,
                             vec4(1.0))
                : texture(uCoatColorTex, uv);
  return (c * scale + bias).rgb;
}

float sampleShadow(vec3 worldPos, vec3 normal, vec3 lightDir) {
  if (fr.pointShadowLight.w > 0.5) {
    vec3 d = worldPos - fr.pointShadowLight.xyz;
    vec3 a = abs(d);
    int face = a.x >= a.y && a.x >= a.z ? (d.x >= 0.0 ? 0 : 1)
             : (a.y >= a.z ? (d.y >= 0.0 ? 2 : 3) : (d.z >= 0.0 ? 4 : 5));
    vec4 clip = fr.pointShadowViewProj[face] * vec4(worldPos, 1.0);
    vec3 p = clip.xyz / clip.w;
    if (p.z <= 0.0 || p.z >= 1.0) return 1.0;
    float bias = max(0.00035, 0.0015 * (1.0 - max(dot(normal, lightDir), 0.0)));
    vec3 dir = normalize(d);
    if (fr.mode.y == 0) {
      return p.z - bias <= texture(uPointShadowMap, dir).r ? 1.0 : 0.0;
    }
    // Small tangent-plane disk around the cube lookup. This removes the hard,
    // aliased edge of the old single-tap point/area-light shadow while keeping
    // the kernel bounded for real-time use.
    vec3 axis = abs(dir.y) < 0.95 ? vec3(0.0, 1.0, 0.0)
                                  : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(axis, dir));
    vec3 bitangent = cross(dir, tangent);
    float visible = 0.0;
    const int pointTaps = 12;
    const float goldenAngle = 2.39996323;
    for (int i = 0; i < pointTaps; ++i) {
      float r = sqrt((float(i) + 0.5) / float(pointTaps));
      float a = float(i) * goldenAngle;
      vec2 disk = vec2(cos(a), sin(a)) * r * 0.0035;
      vec3 tapDir = normalize(dir + tangent * disk.x + bitangent * disk.y);
      visible += p.z - bias <= texture(uPointShadowMap, tapDir).r ? 1.0 : 0.0;
    }
    return visible / float(pointTaps);
  }
  if (fr.iblParams.w < 0.5) return 1.0;
  vec4 clip = fr.shadowViewProj * vec4(worldPos, 1.0);
  vec3 p = clip.xyz / clip.w;
  p.xy = p.xy * 0.5 + 0.5;
  p.y = 1.0 - p.y;
  if (p.z <= 0.0 || p.z >= 1.0 || any(lessThan(p.xy, vec2(0.0))) ||
      any(greaterThan(p.xy, vec2(1.0)))) return 1.0;
  float slopeBias = 0.0015 * (1.0 - max(dot(normal, lightDir), 0.0));
  float receiverBias = 1.25 * (abs(dFdx(p.z)) + abs(dFdy(p.z)));
  float bias = max(0.00035, max(slopeBias, receiverBias));
  vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
  if (fr.mode.y != 0) {
    // Bounded PCSS: estimate nearby blocker depth, then expand a tent kernel
    // with receiver/blocker separation. Close contacts stay crisp while more
    // distant receivers get the broad soft penumbra expected from area lights.
    float blockerDepth = 0.0;
    float blockers = 0.0;
    for (int y = -1; y <= 1; ++y)
      for (int x = -1; x <= 1; ++x) {
        float z = texture(uShadowMap, p.xy + vec2(x, y) * texel * 2.0).r;
        if (z < p.z - bias) { blockerDepth += z; blockers += 1.0; }
      }
    if (blockers < 0.5) return 1.0;
    blockerDepth /= blockers;
    float penumbra = clamp((p.z - blockerDepth) /
                           max(blockerDepth, 1.0e-3) * 80.0, 1.25, 4.5);
    float visible = 0.0;
    float weightSum = 0.0;
    for (int y = -3; y <= 3; ++y)
      for (int x = -3; x <= 3; ++x) {
        vec2 q = vec2(x, y) / 3.0;
        float weight = max(0.0, 1.0 - length(q) * 0.72);
        float z = texture(uShadowMap, p.xy + q * texel * penumbra).r;
        visible += (p.z - bias <= z ? 1.0 : 0.0) * weight;
        weightSum += weight;
      }
    return visible / max(weightSum, 1.0e-4);
  }
  float visible = 0.0;
  int shadowRadius = 1;
  for (int y = -shadowRadius; y <= shadowRadius; ++y)
    for (int x = -shadowRadius; x <= shadowRadius; ++x)
      visible += p.z - bias <= texture(uShadowMap, p.xy + vec2(x, y) * texel).r
                     ? 1.0 : 0.0;
  float taps = float((shadowRadius * 2 + 1) * (shadowRadius * 2 + 1));
  return visible / taps;
}

vec3 applyTangentNormal(vec3 n, vec3 tangentNormal, vec2 normalUv) {
  // Build the tangent frame from the exact coordinates used for the normal
  // sample. Using raw vUV here makes UV1-routed or transformed normal maps
  // fetch the right texel but interpret its tangent-space vector in the wrong
  // basis (GL and the software RT path already use the transformed UVs).
  vec3 dp1 = dFdx(vWorldPos);
  vec3 dp2 = dFdy(vWorldPos);
  vec2 du1 = dFdx(normalUv);
  vec2 du2 = dFdy(normalUv);
  float r = du1.x * du2.y - du2.x * du1.y;
  vec3 t = dp1 * du2.y - dp2 * du1.y;
  t = (abs(r) > 1e-8) ? t / r : dp1;
  t = normalize(t - n * dot(n, t));
  vec3 b = normalize(cross(n, t)) * (r < 0.0 ? -1.0 : 1.0);
  return normalize(mat3(t, b, n) * tangentNormal);
}

vec3 applyNormalMap(vec3 n) {
  if ((pc.ids.w & 16) == 0) {
    return n;
  }
  MaterialTexParam m = matTexParam();
  vec2 sourceUv = (m.uvSets.z > 0.5) ? vUV1 : vUV;
  vec2 normalUv = xformUv(sourceUv, m.normalUv0, m.normalUv1);
  return applyTangentNormal(n, sampleNormal(vUV).xyz, normalUv);
}

void shadeFragment() {
  // Shading normal, with the tangent-space normal map applied up front so the
  // Normals AOV (mode 2) shows the same perturbed normal the lit path uses --
  // matching the GL backend, which also maps before its AOV branch.
  // Meshes without authored normals carry zero vertex normals and set flags
  // bit 0. Normalizing that zero vector poisoned the lit path with NaNs, making
  // non-subdivision displayColor meshes render black. Derive their geometric
  // normal from screen-space position derivatives, matching the GL path.
  vec3 Nbase = ((pc.ids.y & 1) != 0)
                   ? normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)))
                   : normalize(vNormalW);
  vec3 N = applyNormalMap(Nbase);
  vec4 graphBase = evalRasterMaterialXGraph(0, vUV);
  vec4 graphMetal = evalRasterMaterialXGraph(1, vUV);
  vec4 graphRough = evalRasterMaterialXGraph(2, vUV);
  vec4 graphOpacity = evalRasterMaterialXGraph(3, vUV);
  vec4 graphEmission = evalRasterMaterialXGraph(4, vUV);
  vec4 graphNormal = evalRasterMaterialXGraph(5, vUV);
  vec4 graphSubsurface = evalRasterMaterialXGraph(6, vUV);
  vec4 graphSubsurfaceColor = evalRasterMaterialXGraph(7, vUV);
  vec4 graphSubsurfaceRadius = evalRasterMaterialXGraph(8, vUV);
  if (hasGraphRoute(5)) {
    MaterialTexParam gm = matTexParam();
    vec2 graphSourceUv = (gm.uvSets.z > 0.5) ? vUV1 : vUV;
    vec2 graphNormalUv = xformUv(graphSourceUv, gm.normalUv0, gm.normalUv1);
    N = applyTangentNormal(N, graphNormal.xyz * 2.0 - 1.0, graphNormalUv);
  }
  vec3 coatN = applyCoatNormalMap(N);
  // Debug AOVs.
  if (fr.mode.x != 0 && fr.mode.x != 36 && fr.mode.x != 37 &&
      fr.mode.x != 38 && fr.mode.x != 39 && fr.mode.x != 40) {
    vec3 Ngeo = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
    if (fr.mode.x == 2) { outColor = vec4(N * 0.5 + 0.5, 1.0); return; }
    if (fr.mode.x == 35) { outColor = vec4(coatN * 0.5 + 0.5, 1.0); return; }
    if (fr.mode.x == 3) {
      outColor = vec4(idColor(logicalMaterialId()), 1.0);
      return;
    }
    if (fr.mode.x == 4) { outColor = vec4(Ngeo * 0.5 + 0.5, 1.0); return; }
    if (fr.mode.x == 6) {
      float d = clamp(length(fr.camPos.xyz - vWorldPos) / max(fr.camPos.w, 1e-3), 0.0, 1.0);
      outColor = vec4(vec3(1.0 - d), 1.0);
      return;
    }
    if (fr.mode.x == 5) { outColor = vec4(fract(vUV), 0.0, 1.0); return; }
    if (fr.mode.x == 7) {  // albedo (unlit)
      vec3 a = hasGraphRoute(0) ? graphBase.rgb : sampleBaseColor(vUV).rgb;
      outColor = vec4(pc.baseColor.rgb * vColor.rgb * a, 1.0);
      return;
    }
    if (fr.mode.x == 8) {  // facing
      outColor = gl_FrontFacing ? vec4(0.1, 0.7, 0.1, 1.0) : vec4(0.7, 0.1, 0.1, 1.0);
      return;
    }
    if (fr.mode.x == 9) {
      MaterialTexParam m = matTexParam();
      vec4 rt = sampleRoughness(vUV);
      outColor = vec4(vec3(pc.matAux.y * (channelOf(rt, m.scalar0.y) * m.scalar1.x + m.scalar1.y)), 1.0);
      return;
    }
    if (fr.mode.x == 10) {
      MaterialTexParam m = matTexParam();
      vec4 mt = sampleMetallic(vUV);
      outColor = vec4(vec3(pc.matAux.x * (channelOf(mt, m.scalar0.x) * m.scalar0.z + m.scalar0.w)), 1.0);
      return;
    }
    if (fr.mode.x == 11) {                                                            // emissive
      outColor = vec4(pc.emissive.xyz * sampleEmissive(vUV).rgb, 1.0); return;
    }
    if (fr.mode.x == 12) {
      float opacity = clamp(pc.baseColor.a * sampleBaseColor(vUV).a *
                            sampleOpacity(vUV) * vColor.a, 0.0, 1.0);
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
    if (fr.mode.x == 16) {  // mesh id / picking
      // The picking pass is depth-writing and includes Blend geometry. Reject
      // texture holes while retaining any visibly translucent surface, so clicks
      // select the same frontmost geometry shown by the shaded pass.
      float pickOpacity = clamp(pc.baseColor.a * sampleBaseColor(vUV).a *
                                sampleOpacity(vUV) * vColor.a, 0.0, 1.0);
      float pickCutoff = (pc.matAux.z > 0.5 && pc.matAux.z < 1.5)
                             ? pc.matAux.w
                             : (1.0 / 255.0);
      if (pickOpacity < pickCutoff) discard;
      outColor = vec4(idColor(pc.ids.z & 0x1fffffff), 1.0);
      return;
    }
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
  float opacity = clamp(pc.baseColor.a * baseSample.a * sampleOpacity(vUV) *
                        vColor.a, 0.0, 1.0);
  if (hasGraphRoute(3)) opacity = clamp(graphOpacity.x, 0.0, 1.0);
  if (pc.matAux.z > 0.5 && pc.matAux.z < 1.5) {
    if (opacity < pc.matAux.w) discard;
    opacity = 1.0;
  }
  // Mixed opaque/glass atlases are submitted to both passes. The high bits of
  // ids.z select the complementary coverage so opaque texels write depth and
  // transparent texels alone enter sorted/weighted blending.
  // This path is specifically for a single atlas mixing opaque and transparent
  // islands. ALab's full-quality mask stores some solid painted-metal islands
  // around 0.6--0.8 rather than one, while its glass is near zero; use the
  // conventional coverage split instead of interpreting the metal as alpha.
  const float opaqueCoverage = 0.5;
  if ((pc.ids.z & (1 << 30)) != 0 && opacity < opaqueCoverage) discard;
  if ((pc.ids.z & (1 << 29)) != 0 && opacity >= opaqueCoverage) discard;
  if ((pc.ids.z & (1 << 30)) != 0) opacity = 1.0;
  // Per-vertex displayColor multiplies the base color (GL parity: attrib 9's
  // vColor does the same in material.cpp). White when the mesh has none.
  vec3 base = pc.baseColor.rgb * vColor.rgb *
              (hasGraphRoute(0) ? graphBase.rgb : baseSample.rgb);
  MaterialTexParam m = matTexParam();
  vec4 mt = sampleMetallic(vUV);
  vec4 rt = sampleRoughness(vUV);
  float metallic = pc.matAux.x * (channelOf(mt, m.scalar0.x) * m.scalar0.z + m.scalar0.w);
  float roughness = pc.matAux.y * (channelOf(rt, m.scalar0.y) * m.scalar1.x + m.scalar1.y);
  vec3 emissive = pc.emissive.xyz * sampleEmissive(vUV).rgb;
  if (hasGraphRoute(1)) metallic = clamp(graphMetal.x, 0.0, 1.0);
  if (hasGraphRoute(2)) roughness = clamp(graphRough.x, 0.02, 1.0);
  if (hasGraphRoute(4)) emissive = max(graphEmission.rgb, vec3(0.0));
  vec3 V = normalize(fr.camPos.xyz - vWorldPos);

  // Real-time Cook-Torrance preview, matching light3d/material.cpp.
  vec3 Nf = (dot(N, V) < 0.0) ? -N : N;
  vec3 coatNf = (dot(coatN, V) < 0.0) ? -coatN : coatN;
  float NoV = max(dot(Nf, V), 1e-4);
  float rgh = clamp(roughness, 0.02, 1.0);
  if (fr.mode.y != 0) {
    // Specular anti-aliasing: widen the microfacet lobe when the shading normal
    // changes rapidly inside a pixel, suppressing normal-map sparkle.
    vec3 dnx = dFdx(Nf);
    vec3 dny = dFdy(Nf);
    float normalVariance = min(0.25, 0.5 * (dot(dnx, dnx) + dot(dny, dny)));
    rgh = clamp(sqrt(rgh * rgh + normalVariance), 0.02, 1.0);
  }
  float met = clamp(metallic, 0.0, 1.0);
  vec3 F0 = computeF0(base, met);
  MaterialTexParam pbr = matTexParam();
  float subsurfaceWeight = clamp(pbr.subsurfaceParams.x, 0.0, 1.0);
  vec3 subsurfaceColor = max(pbr.subsurfaceColor.rgb, vec3(0.0));
  if (hasGraphRoute(6)) subsurfaceWeight = clamp(graphSubsurface.x, 0.0, 1.0);
  if (hasGraphRoute(7)) subsurfaceColor = max(graphSubsurfaceColor.rgb, vec3(0.0));
  float subsurfaceRadius = max(pbr.subsurfaceParams.z, 0.0);
  if (hasGraphRoute(8)) subsurfaceRadius = max(dot(graphSubsurfaceRadius.rgb,
                                                    vec3(0.2126, 0.7152, 0.0722)), 0.0);
  float volumeOpacity = clamp(1.0 - exp(-max(pbr.volumeParams.x, 0.0) * 0.1),
                              0.0, 1.0);
  base = mix(base, base * pbr.volumeAlbedo.rgb, volumeOpacity);
  emissive += pbr.volumeEmission.rgb * max(pbr.volumeParams.y, 0.0) *
              volumeOpacity;
  // inputs:specularColor texture modulates F0, but only in the specular
  // workflow (where F0 *is* specularColor). Vulkan has the sampler budget for
  // this slot; the GL path deliberately omits it.
  if (pbr.specParams.w < 0.0 || pbr.specParams.w > 100.0) {
    vec2 specSrc = pbr.extraUvSets.x > 0.5 ? vUV1 : vUV;
    vec2 specUv = xformUv(specSrc, pbr.specColorUv0, pbr.specColorUv1);
    specUv = ptexUv(uSpecularColorTex, specUv, pbr.ptexSpecularInfo);
    bool ordinarySpec = (pc.ids.w & 4096) != 0;
    bool udimSpec = !ordinarySpec && pbr.udimSlots1.w >= 0.0;
    vec4 specSample = udimSpec
        ? sampleUdim(uSpecularColorUdimTex,
                     int(pbr.udimSlots1.w + 0.5), specUv, vec4(1.0))
        : texture(uSpecularColorTex, specUv);
    bool hasSpec = udimSpec || ordinarySpec;
    if (hasSpec) F0 *= (specSample * pbr.specColorScale + pbr.specColorBias).rgb;
  }
  if (fr.mode.x == 39) { outColor = vec4(F0, 1.0); return; }
  if (fr.mode.x == 40) {
    float encodedIor = abs(pbr.specParams.w);
    float ior = max(encodedIor > 100.0 ? encodedIor - 100.0 : encodedIor, 1.0);
    float d = (ior - 1.0) / (ior + 1.0);
    outColor = vec4(vec3(d * d), 1.0); return;
  }
  float coatWeight = clamp(pbr.coatParams.x *
                               sampleCoatScalarUdim(uCoatWeightTex,
                                                uCoatWeightUdimTex,
                                                (pc.ids.w & 8192) != 0,
                                                pbr.semanticUdimSlots.y,
                                                pbr.coatWeightUv0,
                                                pbr.coatWeightUv1,
                                                pbr.coatTexParams.z,
                                                pbr.coatTexParams.x,
                                                pbr.coatWeightScale,
                                                pbr.coatWeightBias,
                                                pbr.ptexCoatWeightInfo),
                           0.0, 1.0);
  float coatRoughness = clamp(pbr.coatParams.y *
                                  sampleCoatScalarUdim(uCoatRoughnessTex,
                                                   uCoatRoughnessUdimTex,
                                                   (pc.ids.w & 32768) != 0,
                                                   pbr.semanticUdimSlots.w,
                                                   pbr.coatRoughUv0,
                                                   pbr.coatRoughUv1,
                                                   pbr.coatTexParams.w,
                                                   pbr.coatTexParams.y,
                                                   pbr.coatRoughScale,
                                                   pbr.coatRoughBias,
                                                   pbr.ptexCoatRoughInfo),
                              0.02, 1.0);
  vec3 coatTint = pbr.coatColor.rgb *
                  sampleCoatColorUdim((pc.ids.w & 16384) != 0,
                                  pbr.semanticUdimSlots.z,
                                  pbr.coatColorUv0, pbr.coatColorUv1,
                                  pbr.extraUvSets.y, pbr.coatColorScale,
                                  pbr.coatColorBias,
                                  pbr.ptexCoatColorInfo);
  if (fr.mode.x == 36) { outColor = vec4(vec3(coatWeight), 1.0); return; }
  if (fr.mode.x == 37) { outColor = vec4(coatTint, 1.0); return; }
  if (fr.mode.x == 38) { outColor = vec4(vec3(coatRoughness), 1.0); return; }
  float coatIor = max(pbr.coatParams.z, 1.0);
  float coatD = (coatIor - 1.0) / (coatIor + 1.0);
  vec3 direct = vec3(0.0);
  uint lightMask = uint(pc.ids.w) >> 16u;
  for (uint li = 0u; li < min(fr.rasterLightInfo.x, 16u); ++li) {
    if ((lightMask & (1u << li)) == 0u) continue;
    vec4 pt = fr.rasterLights[li].positionType;
    vec4 da = fr.rasterLights[li].directionAngle;
    vec4 lc = fr.rasterLights[li].colorDiffuse;
    vec4 ss = fr.rasterLights[li].specularShape;
    int lightType = int(pt.w + 0.5);
    int sampleCount = (lightType == 2 || lightType == 3 || lightType == 4) ? 8 : 1;
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
    vec3 samplePos = pt.xyz;
    vec3 areaX = normalize(fr.rasterLights[li].iesAxisX.xyz);
    vec3 areaY = normalize(fr.rasterLights[li].iesAxisY.xyz);
    if (lightType == 3) {
      float sx = (float(sampleIndex % 4) + 0.5) * 0.25 - 0.5;
      float sy = (float(sampleIndex / 4) + 0.5) * 0.5 - 0.5;
      samplePos += areaX * (sx * fr.rasterLights[li].areaParams.y) +
                   areaY * (sy * fr.rasterLights[li].areaParams.z);
    } else if (lightType == 2) {
      const float k = 0.5;
      float a = 6.28318530718 * (float(sampleIndex) + 0.5) / 8.0;
      float sx = cos(a) * k * fr.rasterLights[li].areaParams.x;
      float sy = sin(a) * k * fr.rasterLights[li].areaParams.x;
      samplePos += areaX * sx + areaY * sy;
    } else if (lightType == 4) {
      const float k = 0.5;
      float a = 6.28318530718 * (float(sampleIndex) + 0.5) / 8.0;
      float sx = cos(a) * k * fr.rasterLights[li].areaParams.x;
      float sy = sin(a) * k * fr.rasterLights[li].areaParams.x;
      samplePos += areaX * sx + areaY * sy;
    }
    vec3 L;
    float attenuation = 1.0;
    if (lightType == 5) {
      L = normalize(da.xyz);
    } else {
      vec3 toLight = samplePos - vWorldPos;
      float dist2 = max(dot(toLight, toLight), 1e-6);
      L = toLight * inversesqrt(dist2);
      attenuation = 1.0 / dist2;
    }
    float shape = 1.0;
    if (ss.w > 0.5 && lightType != 5) {
      float coneCos = dot(normalize(da.xyz), -L);
      float outer = cos(radians(clamp(da.w, 0.0, 180.0)));
      float inner = cos(radians(clamp(da.w * (1.0 - clamp(ss.y, 0.0, 1.0)),
                                      0.0, 180.0)));
      shape = smoothstep(outer, max(inner, outer + 1e-5), coneCos) *
              pow(max(coneCos, 0.0), max(ss.z, 0.0));
    }
    float ies = 1.0;
    if (lightType != 5 && dot(fr.rasterLights[li].iesProfile[0],
                              fr.rasterLights[li].iesProfile[0]) > 1e-8) {
      vec3 iesDir = normalize(-L);
      float v = degrees(acos(clamp(dot(iesDir, normalize(da.xyz)), -1.0, 1.0)));
      float fy = clamp(v / 60.0, 0.0, 3.0);
      int y0 = int(floor(fy));
      int y1 = min(y0 + 1, 3);
      float az = degrees(atan(dot(iesDir, fr.rasterLights[li].iesAxisY.xyz),
                              dot(iesDir, fr.rasterLights[li].iesAxisX.xyz)));
      if (az < 0.0) az += 360.0;
      float fx = az / 60.0;
      int x0 = min(int(floor(fx)), 5);
      int x1 = (x0 + 1) % 6;
      float tx = fx - float(x0);
      float a0 = mix(fr.rasterLights[li].iesProfile[y0][x0],
                     fr.rasterLights[li].iesProfile[y0][x1], tx);
      float a1 = mix(fr.rasterLights[li].iesProfile[y1][x0],
                     fr.rasterLights[li].iesProfile[y1][x1], tx);
      ies = mix(a0, a1, fy - float(y0));
    }
    float NoL = max(dot(Nf, L), 0.0);
    if (NoL <= 0.0 || shape <= 0.0) continue;
    vec3 H = normalize(L + V);
    float NoH = max(dot(Nf, H), 0.0), VoH = max(dot(V, H), 0.0);
    vec3 F = fresnelSchlick(VoH, F0);
    vec3 specular = distributionGGX(NoH, rgh) *
                    geometrySchlickGGX(NoV, rgh) *
                    geometrySchlickGGX(NoL, rgh) * F /
                    max(4.0 * NoV * NoL, 1e-5);
    vec3 diffuse = (vec3(1.0) - F) * (1.0 - met) * base / kPi;
    float radiusGain = clamp(sqrt(max(pbr.subsurfaceParams.y *
                                      subsurfaceRadius, 0.0)) * 8.0,
                              0.0, 1.0);
    float diffusionShape = mix(0.15 + 0.85 * NoL,
                               0.15 + 0.85 * sqrt(NoL), radiusGain);
    vec3 subsurfaceDiffuse = subsurfaceColor * diffusionShape / kPi;
    diffuse = mix(diffuse, subsurfaceDiffuse, subsurfaceWeight);
    float transmissionWeight = clamp(pbr.transmissionParams.x, 0.0, 1.0);
    vec3 transmissionDiffuse = max(pbr.transmissionColor.rgb, vec3(0.0)) *
                                transmissionWeight * (1.0 - NoL) *
                                (1.0 - met) * (0.5 / kPi);
    float coatNoL = max(dot(coatNf, L), 0.0);
    float coatNoV = max(dot(coatNf, V), 1e-4);
    float coatNoH = max(dot(coatNf, H), 0.0);
    vec3 coatF = fresnelSchlick(VoH, vec3(coatD * coatD));
    vec3 coatSpecular = distributionGGX(coatNoH, coatRoughness) *
                        geometrySchlickGGX(coatNoV, coatRoughness) *
                        geometrySchlickGGX(coatNoL, coatRoughness) * coatF /
                        max(4.0 * coatNoV * coatNoL, 1e-5);
    vec3 baseBrdf = diffuse * lc.w +
                    specular * (vec3(1.0) - coatF * coatWeight) * ss.x;
    vec3 coatBrdf = coatSpecular * coatTint * coatWeight * ss.x;
    float visibility = (int(li) == int(fr.iblParams.z + 0.5))
                           ? sampleShadow(vWorldPos, Nf, L) : 1.0;
    direct += (baseBrdf * NoL + coatBrdf * coatNoL +
               transmissionDiffuse * (0.25 + 0.75 * NoL)) * lc.rgb *
              (attenuation * shape * ies * visibility) / float(sampleCount);
    }
  }
  if (fr.rasterLightInfo.x == 0u) {
    vec3 L = (dot(fr.lightDir.xyz, fr.lightDir.xyz) > 1e-8)
                 ? normalize(fr.lightDir.xyz)
                 : normalize(vec3(0.3, 0.5, 0.8));
    vec3 lightColor = (dot(fr.lightColor.rgb, fr.lightColor.rgb) > 1e-8)
                          ? fr.lightColor.rgb : vec3(1.0);
    float NoL = max(dot(Nf, L), 0.0);
    vec3 H = normalize(L + V);
    float NoH = max(dot(Nf, H), 0.0), VoH = max(dot(V, H), 0.0);
    vec3 F = fresnelSchlick(VoH, F0);
    vec3 specular = distributionGGX(NoH, rgh) *
                    geometrySchlickGGX(NoV, rgh) *
                    geometrySchlickGGX(NoL, rgh) * F /
                    max(4.0 * NoV * NoL, 1e-5);
    vec3 diffuse = (vec3(1.0) - F) * (1.0 - met) * base / kPi;
    float radiusGain = clamp(sqrt(max(pbr.subsurfaceParams.y *
                                      subsurfaceRadius, 0.0)) * 8.0,
                              0.0, 1.0);
    float diffusionShape = mix(0.15 + 0.85 * NoL,
                               0.15 + 0.85 * sqrt(NoL), radiusGain);
    vec3 subsurfaceDiffuse = subsurfaceColor * diffusionShape / kPi;
    diffuse = mix(diffuse, subsurfaceDiffuse, subsurfaceWeight);
    float transmissionWeight = clamp(pbr.transmissionParams.x, 0.0, 1.0);
    vec3 transmissionDiffuse = max(pbr.transmissionColor.rgb, vec3(0.0)) *
                                transmissionWeight * (1.0 - NoL) *
                                (1.0 - met) * (0.5 / kPi);
    direct = (diffuse + specular) * lightColor * NoL +
             transmissionDiffuse * lightColor * 0.25;
  }
  // Ambient: DomeLight split-sum IBL when baked, else the constant floor
  // (matches the GL raster path in light3d/material.cpp).
  vec3 ambient;
  if (fr.iblColor.w > 0.5) {
    vec3 Ne = normalize(mat3(fr.envRot) * Nf);
    vec3 Re = normalize(mat3(fr.envRot) * reflect(-V, Nf));
    vec3 irr = texture(uIrradianceMap, Ne).rgb;
    vec3 pref = textureLod(uPrefilteredMap, Re, rgh * (fr.iblParams.x - 1.0)).rgb;
    vec2 dfg = texture(uBrdfLut, vec2(max(dot(Nf, V), 0.0), rgh)).rg;
    vec3 baseIbl = base * (1.0 - met) * irr +
                   pref * (F0 * dfg.x + dfg.y);
    vec3 coatPref = textureLod(uPrefilteredMap, Re,
        coatRoughness * (fr.iblParams.x - 1.0)).rgb;
    vec2 coatDfg = texture(uBrdfLut,
                           vec2(max(dot(Nf, V), 0.0), coatRoughness)).rg;
    vec3 coatF0 = vec3(coatD * coatD);
    vec3 coatIbl = coatPref * (coatF0 * coatDfg.x + coatDfg.y) *
                   coatTint * coatWeight;
    ambient = (baseIbl * (1.0 - coatWeight * coatF0) + coatIbl) *
              fr.iblColor.rgb;
  } else {
    // Match the full OpenGL material shader's no-IBL preview floor. Keeping
    // this backend-neutral is especially important for light-linked meshes:
    // channels excluded from a light expose the ambient term directly.
    ambient = base * 0.12;
  }
  ambient *= clamp(pbr.coatParams.w, 0.0, 1.0) * sampleOcclusion(vUV);
  vec3 c = (ambient + direct + emissive) * exp2(fr.iblParams.y);
  float screenTransmission = 0.0;
  vec2 screenRefractionUv = vec2(0.0);
  float screenRefractionRoughness = 0.0;
  vec3 screenTransmissionTint = vec3(1.0);
  // Bounded real-time transmission for the raster path. The opaque scene-color
  // refraction pass can refine this later; this environment-space result is a
  // stable physically-directed fallback for off-screen rays and makes authored
  // PreviewSurface opacity+IOR glass respond to the HDR dome immediately.
  if (fr.mode.y != 0 && fr.iblColor.w > 0.5) {
    float encodedIor = abs(pbr.specParams.w);
    float ior = max(encodedIor > 100.0 ? encodedIor - 100.0 : encodedIor,
                    1.0001);
    float authoredTransmission = clamp(pbr.transmissionParams.x, 0.0, 1.0);
    float previewTransmission = (ior > 1.01) ? (1.0 - opacity) : 0.0;
    float transmission = max(authoredTransmission, previewTransmission) *
                         (1.0 - met);
    if (transmission > 1.0e-4) {
      bool thinWalled = pbr.transmissionParams.w > 0.5;
      // `transmission_depth` is an attenuation reference distance, not shell
      // geometry. Until the backface-distance attachment supplies an exact
      // entry-to-exit length, use a scene-relative normal thickness and the
      // expected 1/cos(theta) path elongation. Keeping these quantities
      // separate avoids forcing every object to transmit exactly
      // transmission_color regardless of its scale or viewing angle.
      float sceneScale = max(fr.camPos.w, 1.0e-3);
      float normalThickness = sceneScale * 0.01;
      float travel = thinWalled
          ? normalThickness
          : normalThickness / max(NoV, 0.2);
      travel = clamp(travel, sceneScale * 0.002, sceneScale * 0.1);
      vec3 Tw = thinWalled ? -V : refract(-V, Nf, 1.0 / ior);
      if (dot(Tw, Tw) < 1.0e-6) Tw = reflect(-V, Nf);
      vec3 Te = normalize(mat3(fr.envRot) * Tw);
      vec3 transmitted = textureLod(
          uPrefilteredMap, Te,
          clamp(rgh + pbr.transmissionParams.z, 0.02, 1.0) *
              (fr.iblParams.x - 1.0)).rgb;
      vec3 tint = vec3(1.0);
      if (authoredTransmission > 0.0) {
        vec3 transmissionColor = clamp(pbr.transmissionColor.rgb,
                                       vec3(1.0e-6), vec3(1.0));
        // OpenPBR transmission_color is the transmittance observed after
        // transmission_depth stage units, not an interface tint.  Keep the
        // zero-depth convention as a one-time tint, and use Beer-Lambert for
        // positive depths.  The backface thickness pass can replace `travel`
        // with measured shell distance without changing this material math.
        tint = pbr.transmissionParams.y > 0.0 && !thinWalled
                   ? exp(log(transmissionColor) *
                         (travel / pbr.transmissionParams.y))
                   : transmissionColor;
      }
      // Transmission crosses a dielectric boundary, so use the exact
      // angle/IOR relation rather than the BRDF's Schlick approximation.
      vec3 surfaceF = vec3(fresnelDielectric(NoV, 1.0, ior));
      screenTransmissionTint = tint * (vec3(1.0) - surfaceF);
      transmitted *= screenTransmissionTint * fr.iblColor.rgb;
      c = mix(c, transmitted * exp2(fr.iblParams.y), transmission);
#ifdef LUSDVIEW_OIT
      // Project a short refracted ray into screen space. The opaque pass is a
      // stable fallback for geometry behind glass; off-screen rays retain the
      // environment result above. Depth controls authored travel distance and
      // is clamped to avoid extreme offsets on large-unit scenes.
      vec4 fromClip = fr.viewProj * vec4(vWorldPos, 1.0);
      vec4 toClip = fr.viewProj * vec4(vWorldPos + Tw * travel, 1.0);
      vec2 fromNdc = fromClip.xy / max(abs(fromClip.w), 1.0e-5);
      vec2 toNdc = toClip.xy / max(abs(toClip.w), 1.0e-5);
      screenRefractionUv = gl_FragCoord.xy /
                           vec2(textureSize(uOpaqueSceneColor, 0));
      screenRefractionUv += (toNdc - fromNdc) * vec2(0.5, -0.5);
      screenTransmission = transmission;
      screenRefractionRoughness = rgh;
#endif
    }
  }
#ifndef LUSDVIEW_OIT
  c = linearToSrgb(fr.mode.y != 0 ? acesFitted(c) : c);
  if (pc.matAux.z > 1.5 && opacity < 1.0) {
    c *= opacity;  // pipeline uses premultiplied alpha blending
  }
#else
  if (fr.mode.y != 0) c = acesFitted(c);
#ifdef LUSDVIEW_OIT
  if (fr.mode.y != 0 && screenTransmission > 1.0e-4 &&
      all(greaterThan(screenRefractionUv, vec2(0.002))) &&
      all(lessThan(screenRefractionUv, vec2(0.998)))) {
    vec2 texel = 1.0 / vec2(textureSize(uOpaqueSceneColor, 0));
    vec2 blur = texel * (1.0 + 6.0 * screenRefractionRoughness);
    vec3 refracted = srgbToLinear(texture(uOpaqueSceneColor,
                                          screenRefractionUv).rgb) * 0.4;
    refracted += srgbToLinear(texture(uOpaqueSceneColor,
        screenRefractionUv + vec2( blur.x, 0.0)).rgb) * 0.15;
    refracted += srgbToLinear(texture(uOpaqueSceneColor,
        screenRefractionUv + vec2(-blur.x, 0.0)).rgb) * 0.15;
    refracted += srgbToLinear(texture(uOpaqueSceneColor,
        screenRefractionUv + vec2(0.0,  blur.y)).rgb) * 0.15;
    refracted += srgbToLinear(texture(uOpaqueSceneColor,
        screenRefractionUv + vec2(0.0, -blur.y)).rgb) * 0.15;
    // Screen-space and environment fallback transmission must carry the same
    // Beer/Fresnel tint. Previously the in-frame sample replaced the correctly
    // tinted fallback with raw scene color, creating a bright seam as refracted
    // rays crossed the viewport edge.
    refracted *= screenTransmissionTint * exp2(fr.iblParams.y);
    c = mix(c, refracted, screenTransmission);
  }
#endif
#endif
  // Transmission changes the ray direction but still covers the pixel. If OIT
  // used the opacity texture as coverage, clear glass (opacity near zero) would
  // discard the already-computed refracted color and become an invisible hole.
#ifdef LUSDVIEW_OIT
  float surfaceCoverage = fr.mode.y != 0
                              ? max(opacity, screenTransmission)
                              : opacity;
  outColor = vec4(c, clamp(surfaceCoverage, 0.0, 1.0));
#else
  outColor = vec4(c, opacity);
#endif
}

#ifdef LUSDVIEW_OIT
void main() {
  shadeFragment();
  float alpha = clamp(outColor.a, 0.0, 1.0);
  if (alpha <= 1.0e-4) discard;
  float weight = clamp(
      pow(min(1.0, alpha * 10.0) + 0.01, 3.0) * 1.0e8 *
          pow(1.0 - gl_FragCoord.z * 0.9, 3.0),
      1.0e-2, 3.0e3);
  outAccum = vec4(outColor.rgb * alpha, alpha) * weight;
  outReveal = alpha;
}
#else
void main() { shadeFragment(); }
#endif
