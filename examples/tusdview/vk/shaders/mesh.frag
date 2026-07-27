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
                     vec4 colorDiffuse; vec4 specularShape; };

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
// Per-triangle source USD face id (source-face-id AOV). Indexed by the submesh's
// first triangle (flags bits 8-31) + gl_PrimitiveID (submesh-local).
layout(set = 1, binding = 6, std430) readonly buffer Faces { uint faceId[]; };

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
};
layout(set = 3, binding = 0, std430) readonly buffer MatTex { MaterialTexParam p[]; } mtp;

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
  int base = int(info.x + 0.5) + int(face) * 8;
  uint value[4];
  for (int component = 0; component < 4; ++component) {
    int lo = base + component * 2;
    float a = texelFetch(tex, ivec2(lo % size.x, lo / size.x), 0).a;
    float b = texelFetch(tex, ivec2((lo + 1) % size.x,
                                    (lo + 1) / size.x), 0).a;
    value[component] = uint(a * 255.0 + 0.5) |
                       (uint(b * 255.0 + 0.5) << 8u);
  }
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
    int base = int(m.ptexBaseInfo.x + 0.5) + int(ptexFace) * 8;
    ivec2 size = textureSize(uBaseColorTex, 0);
    for (int component = 0; component < 4; ++component) {
      int lo = base + component * 2;
      float a = texelFetch(uBaseColorTex,
                           ivec2(lo % size.x, lo / size.x), 0).a;
      float b = texelFetch(uBaseColorTex,
                           ivec2((lo + 1) % size.x, (lo + 1) / size.x), 0).a;
      values[component] = uint(a * 255.0 + 0.5) |
                          (uint(b * 255.0 + 0.5) << 8u);
    }
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
                           float uvSet, float channel, vec4 scale, vec4 bias) {
  bool udim = !ordinary && row >= 0.0;
  if (!ordinary && !udim) return 1.0;
  vec2 suv = uvSet > 0.5 ? vUV1 : vUV;
  vec2 uv = xformUv(suv, uv0, uv1);
  vec4 c = udim ? sampleUdim(udimTex, int(row + 0.5), uv, vec4(1.0))
                : texture(tex, uv);
  return clamp(channelOf(c * scale + bias, channel < 0.0 ? 0.0 : channel),
               0.0, 1.0);
}

vec3 sampleCoatColorUdim(bool ordinary, float row, vec4 uv0, vec4 uv1,
                         float uvSet, vec4 scale, vec4 bias) {
  bool udim = !ordinary && row >= 0.0;
  if (!ordinary && !udim) return vec3(1.0);
  vec2 suv = uvSet > 0.5 ? vUV1 : vUV;
  vec2 uv = xformUv(suv, uv0, uv1);
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
    return p.z - bias <= texture(uPointShadowMap, normalize(d)).r ? 1.0 : 0.0;
  }
  if (fr.iblParams.w < 0.5) return 1.0;
  vec4 clip = fr.shadowViewProj * vec4(worldPos, 1.0);
  vec3 p = clip.xyz / clip.w;
  p.xy = p.xy * 0.5 + 0.5;
  p.y = 1.0 - p.y;
  if (p.z <= 0.0 || p.z >= 1.0 || any(lessThan(p.xy, vec2(0.0))) ||
      any(greaterThan(p.xy, vec2(1.0)))) return 1.0;
  float bias = max(0.00035, 0.0015 * (1.0 - max(dot(normal, lightDir), 0.0)));
  vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
  float visible = 0.0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
      visible += p.z - bias <= texture(uShadowMap, p.xy + vec2(x, y) * texel).r
                     ? 1.0 : 0.0;
  return visible / 9.0;
}

vec3 applyNormalMap(vec3 n) {
  if ((pc.ids.w & 16) == 0) {
    return n;
  }

  // Build the tangent frame from the exact coordinates used for the normal
  // sample. Using raw vUV here makes UV1-routed or transformed normal maps
  // fetch the right texel but interpret its tangent-space vector in the wrong
  // basis (GL and the software RT path already use the transformed UVs).
  MaterialTexParam m = matTexParam();
  vec2 sourceUv = (m.uvSets.z > 0.5) ? vUV1 : vUV;
  vec2 normalUv = xformUv(sourceUv, m.normalUv0, m.normalUv1);
  vec3 dp1 = dFdx(vWorldPos);
  vec3 dp2 = dFdy(vWorldPos);
  vec2 du1 = dFdx(normalUv);
  vec2 du2 = dFdy(normalUv);
  float r = du1.x * du2.y - du2.x * du1.y;
  vec3 t = dp1 * du2.y - dp2 * du1.y;
  t = (abs(r) > 1e-8) ? t / r : dp1;
  t = normalize(t - n * dot(n, t));
  vec3 b = normalize(cross(n, t)) * (r < 0.0 ? -1.0 : 1.0);
  vec3 nm = sampleNormal(vUV).xyz;
  return normalize(mat3(t, b, n) * nm);
}

void main() {
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
  vec3 coatN = applyCoatNormalMap(N);
  // Debug AOVs.
  if (fr.mode.x != 0 && fr.mode.x != 36 && fr.mode.x != 37 &&
      fr.mode.x != 38 && fr.mode.x != 39 && fr.mode.x != 40) {
    vec3 Ngeo = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
    if (fr.mode.x == 2) { outColor = vec4(N * 0.5 + 0.5, 1.0); return; }
    if (fr.mode.x == 35) { outColor = vec4(coatN * 0.5 + 0.5, 1.0); return; }
    if (fr.mode.x == 3) { outColor = vec4(idColor(pc.ids.x), 1.0); return; }
    if (fr.mode.x == 4) { outColor = vec4(Ngeo * 0.5 + 0.5, 1.0); return; }
    if (fr.mode.x == 6) {
      float d = clamp(length(fr.camPos.xyz - vWorldPos) / max(fr.camPos.w, 1e-3), 0.0, 1.0);
      outColor = vec4(vec3(1.0 - d), 1.0);
      return;
    }
    if (fr.mode.x == 5) { outColor = vec4(fract(vUV), 0.0, 1.0); return; }
    if (fr.mode.x == 7) {  // albedo (unlit)
      outColor = vec4(pc.baseColor.rgb * vColor.rgb * sampleBaseColor(vUV).rgb, 1.0);
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
  float opacity = clamp(pc.baseColor.a * baseSample.a * sampleOpacity(vUV) *
                        vColor.a, 0.0, 1.0);
  if (pc.matAux.z > 0.5 && pc.matAux.z < 1.5) {
    if (opacity < pc.matAux.w) discard;
    opacity = 1.0;
  }
  // Per-vertex displayColor multiplies the base color (GL parity: attrib 9's
  // vColor does the same in material.cpp). White when the mesh has none.
  vec3 base = pc.baseColor.rgb * vColor.rgb * baseSample.rgb;
  MaterialTexParam m = matTexParam();
  vec4 mt = sampleMetallic(vUV);
  vec4 rt = sampleRoughness(vUV);
  float metallic = pc.matAux.x * (channelOf(mt, m.scalar0.x) * m.scalar0.z + m.scalar0.w);
  float roughness = pc.matAux.y * (channelOf(rt, m.scalar0.y) * m.scalar1.x + m.scalar1.y);
  vec3 emissive = pc.emissive.xyz * sampleEmissive(vUV).rgb;
  vec3 V = normalize(fr.camPos.xyz - vWorldPos);

  // Real-time Cook-Torrance preview, matching light3d/material.cpp.
  vec3 Nf = (dot(N, V) < 0.0) ? -N : N;
  vec3 coatNf = (dot(coatN, V) < 0.0) ? -coatN : coatN;
  float NoV = max(dot(Nf, V), 1e-4);
  float rgh = clamp(roughness, 0.02, 1.0);
  float met = clamp(metallic, 0.0, 1.0);
  vec3 F0 = computeF0(base, met);
  MaterialTexParam pbr = matTexParam();
  // inputs:specularColor texture modulates F0, but only in the specular
  // workflow (where F0 *is* specularColor). Vulkan has the sampler budget for
  // this slot; the GL path deliberately omits it.
  if (pbr.specParams.w < 0.0 || pbr.specParams.w > 100.0) {
    vec2 specSrc = pbr.extraUvSets.x > 0.5 ? vUV1 : vUV;
    vec2 specUv = xformUv(specSrc, pbr.specColorUv0, pbr.specColorUv1);
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
                                                pbr.coatWeightBias),
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
                                                   pbr.coatRoughBias),
                              0.02, 1.0);
  vec3 coatTint = pbr.coatColor.rgb *
                  sampleCoatColorUdim((pc.ids.w & 16384) != 0,
                                  pbr.semanticUdimSlots.z,
                                  pbr.coatColorUv0, pbr.coatColorUv1,
                                  pbr.extraUvSets.y, pbr.coatColorScale,
                                  pbr.coatColorBias);
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
    vec3 L;
    float attenuation = 1.0;
    if (lightType == 5) {
      L = normalize(da.xyz);
    } else {
      vec3 toLight = pt.xyz - vWorldPos;
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
    direct += (baseBrdf * NoL + coatBrdf * coatNoL) * lc.rgb *
              (attenuation * shape * visibility);
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
    direct = (diffuse + specular) * lightColor * NoL;
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
    ambient = base * 0.12;
  }
  ambient *= clamp(pbr.coatParams.w, 0.0, 1.0) * sampleOcclusion(vUV);
  vec3 c = linearToSrgb((ambient + direct + emissive) * exp2(fr.iblParams.y));
  if (pc.matAux.z > 1.5 && opacity < 1.0) {
    c *= opacity;  // pipeline uses premultiplied alpha blending
  }
  outColor = vec4(c, opacity);
}
