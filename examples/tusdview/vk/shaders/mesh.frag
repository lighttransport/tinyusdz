#version 450

layout(location = 0) in vec3 vNormalW;
layout(location = 1) in vec2 vUV;

// Base-color texture (white 1x1 when the material is untextured).
layout(set = 0, binding = 0) uniform sampler2D uBaseColorTex;

layout(push_constant) uniform PushConstants {
  mat4 mvp;
  mat3 nmat;
  vec4 baseColor;
  int matId;
  int renderMode;
} pc;

layout(location = 0) out vec4 outColor;

vec3 idColor(int id) {
  if (id < 0) return vec3(0.45);
  uint h = (uint(id) + 1u) * 2654435761u;
  return vec3(float(h & 255u), float((h >> 8) & 255u), float((h >> 16) & 255u)) * (1.0 / 255.0);
}

void main() {
  if (pc.renderMode == 3) { outColor = vec4(idColor(pc.matId), 1.0); return; }
  vec3 base = pc.baseColor.rgb * texture(uBaseColorTex, vUV).rgb;
  vec3 N = normalize(vNormalW);
  // Headlight-ish fixed directional light + ambient (matches the GL look roughly).
  vec3 L = normalize(vec3(0.5, 0.8, 0.6));
  float diff = max(dot(N, L), 0.0);
  vec3 c = base * (0.25 + 0.85 * diff);
  outColor = vec4(c, pc.baseColor.a);
}
