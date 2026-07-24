#version 450

layout(location = 1) in vec2 vUV;
layout(location = 5) in vec2 vUV1;

layout(set = 0, binding = 0) uniform sampler2D uBaseColorTex;
layout(set = 0, binding = 4) uniform sampler2DArray uBaseColorUdimTex;
layout(set = 0, binding = 5) uniform sampler2D uUdimLutAtlas;
layout(set = 0, binding = 9) uniform sampler2D uOpacityTex;
layout(set = 0, binding = 10) uniform sampler2DArray uOpacityUdimTex;

// Keep this stride byte-identical to mesh.{vert,frag}; the shadow pass only
// consumes base/opacity rows but shares the same material SSBO.
struct MaterialTexParam {
  vec4 baseUv0; vec4 baseUv1;
  vec4 mrUv0; vec4 mrUv1;
  vec4 normalUv0; vec4 normalUv1;
  vec4 emissiveUv0; vec4 emissiveUv1;
  vec4 dispUv0; vec4 dispUv1;
  vec4 baseScale; vec4 baseBias;
  vec4 normalScale; vec4 normalBias;
  vec4 emissiveScale; vec4 emissiveBias;
  vec4 scalar0; vec4 scalar1; vec4 uvSets; vec4 specParams;
  vec4 opacityUv0; vec4 opacityUv1; vec4 opacityParams;
  vec4 udimSlots0; vec4 udimSlots1;
  vec4 roughUv0; vec4 roughUv1;
  vec4 coatParams; vec4 coatColor;
  vec4 occlusionUv0; vec4 occlusionUv1; vec4 occlusionParams;
  vec4 specColorUv0; vec4 specColorUv1;
  vec4 coatWeightUv0; vec4 coatWeightUv1;
  vec4 coatColorUv0; vec4 coatColorUv1;
  vec4 coatRoughUv0; vec4 coatRoughUv1;
  vec4 coatTexParams; vec4 extraUvSets;
  vec4 specColorScale; vec4 specColorBias;
  vec4 coatWeightScale; vec4 coatWeightBias;
  vec4 coatColorScale; vec4 coatColorBias;
  vec4 coatRoughScale; vec4 coatRoughBias;
  vec4 coatNormalUv0; vec4 coatNormalUv1;
  vec4 coatNormalScale; vec4 coatNormalBias;
  vec4 semanticUdimSlots;
  vec4 semanticUdimSlots2;
};
layout(set = 3, binding = 0, std430) readonly buffer MatTex {
  MaterialTexParam p[];
} mtp;

layout(push_constant) uniform PushConstants {
  mat4 model;
  vec4 baseColor;
  vec4 matAux;
  vec4 emissive;
  ivec4 ids;
} pc;

vec2 xformUv(vec2 uv, vec4 r0, vec4 r1) {
  return vec2(r0.x * uv.x + r0.y * uv.y + r0.z,
              r1.x * uv.x + r1.y * uv.y + r1.z);
}

float channelOf(vec4 c, float ch) {
  int i = int(ch + 0.5);
  return i == 1 ? c.g : i == 2 ? c.b : i == 3 ? c.a : c.r;
}

vec4 sampleUdim(sampler2DArray tex, int slot, vec2 uv, vec4 missing) {
  ivec2 tile = ivec2(floor(uv));
  int idx = tile.x + tile.y * 10;
  if (idx < 0 || idx >= 100) return missing;
  float encoded = texelFetch(uUdimLutAtlas, ivec2(idx, slot), 0).r;
  int layer = int(encoded * 255.0 + 0.5) - 1;
  return layer < 0 ? missing : texture(tex, vec3(fract(uv), float(layer)));
}

void main() {
  if (pc.matAux.z < 0.5 || pc.matAux.z > 1.5) return;
  MaterialTexParam m = mtp.p[max(pc.ids.x, 0)];
  vec2 baseUv = m.uvSets.x > 0.5 ? vUV1 : vUV;
  vec2 baseTuv = xformUv(baseUv, m.baseUv0, m.baseUv1);
  vec4 baseSample = (pc.ids.w & 1) != 0
      ? sampleUdim(uBaseColorUdimTex, int(m.udimSlots0.x + 0.5), baseTuv,
                   vec4(1.0))
      : texture(uBaseColorTex, baseTuv);
  float alpha = pc.baseColor.a * (baseSample * m.baseScale + m.baseBias).a;
  // ids.w bit 6 means an opacity texture is bound; bit 7 selects UDIM.
  if ((pc.ids.w & 64) != 0) {
    vec2 opacityUv = m.opacityParams.w > 0.5 ? vUV1 : vUV;
    vec2 tuv = xformUv(opacityUv, m.opacityUv0, m.opacityUv1);
    vec4 c = (pc.ids.w & 128) != 0
        ? sampleUdim(uOpacityUdimTex, int(m.udimSlots1.x + 0.5), tuv,
                     vec4(1.0))
        : texture(uOpacityTex, tuv);
    alpha *= channelOf(c, m.opacityParams.x) * m.opacityParams.y +
             m.opacityParams.z;
  }
  if (alpha < pc.matAux.w) discard;
}
