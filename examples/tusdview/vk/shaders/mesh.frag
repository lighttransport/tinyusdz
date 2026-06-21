#version 450

layout(location = 0) in vec3 vNormalW;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) flat in int vDomJoint;   // dominant skin joint (SkinWeights AOV)
layout(location = 4) in float vDomWeight;

// Base-color texture (white 1x1 when the material is untextured).
layout(set = 0, binding = 0) uniform sampler2D uBaseColorTex;

layout(push_constant) uniform PushConstants {
  mat4 mvp;
  mat4 model;
  vec4 nmat[3];        // normal-matrix cols in .xyz; .w = emissive.rgb (AOV)
  vec4 baseColor;
  vec4 camPos;
  vec4 sceneMin;       // .w = metallic (AOV)
  vec4 sceneExtent;    // .w = roughness (AOV)
  int matId;
  int renderMode;
  int flags;
  int meshId;
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

void main() {
  vec3 N = normalize(vNormalW);
  // Debug AOVs.
  if (pc.renderMode != 0) {
    vec3 Ngeo = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
    if (pc.renderMode == 2) { outColor = vec4(N * 0.5 + 0.5, 1.0); return; }
    if (pc.renderMode == 3) { outColor = vec4(idColor(pc.matId), 1.0); return; }
    if (pc.renderMode == 4) { outColor = vec4(Ngeo * 0.5 + 0.5, 1.0); return; }
    if (pc.renderMode == 6) {
      float d = clamp(length(pc.camPos.xyz - vWorldPos) / max(pc.camPos.w, 1e-3), 0.0, 1.0);
      outColor = vec4(vec3(1.0 - d), 1.0);
      return;
    }
    if (pc.renderMode == 5) { outColor = vec4(fract(vUV), 0.0, 1.0); return; }
    if (pc.renderMode == 7) {  // albedo (unlit)
      outColor = vec4(pc.baseColor.rgb * texture(uBaseColorTex, vUV).rgb, 1.0);
      return;
    }
    if (pc.renderMode == 8) {  // facing
      outColor = gl_FrontFacing ? vec4(0.1, 0.7, 0.1, 1.0) : vec4(0.7, 0.1, 0.1, 1.0);
      return;
    }
    if (pc.renderMode == 9) { outColor = vec4(vec3(pc.sceneExtent.w), 1.0); return; }   // roughness
    if (pc.renderMode == 10) { outColor = vec4(vec3(pc.sceneMin.w), 1.0); return; }      // metallic
    if (pc.renderMode == 11) {                                                            // emissive
      outColor = vec4(pc.nmat[0].w, pc.nmat[1].w, pc.nmat[2].w, 1.0); return;
    }
    if (pc.renderMode == 12) { outColor = vec4(vec3(pc.baseColor.a), 1.0); return; }  // opacity
    if (pc.renderMode == 13) {  // world position
      outColor = vec4(clamp((vWorldPos - pc.sceneMin.xyz) / pc.sceneExtent.xyz, 0.0, 1.0), 1.0);
      return;
    }
    if (pc.renderMode == 23) {  // uv checker
      vec2 c = floor(fract(vUV) * 16.0);
      float k = mod(c.x + c.y, 2.0);
      outColor = vec4(vec3(mix(0.25, 0.85, k)), 1.0);
      return;
    }
    if (pc.renderMode == 15) { outColor = vec4(idColor(gl_PrimitiveID), 1.0); return; }  // prim id
    if (pc.renderMode == 16) { outColor = vec4(idColor(pc.meshId), 1.0); return; }        // mesh id
    if (pc.renderMode == 19) {  // missing normals
      outColor = ((pc.flags & 1) != 0) ? vec4(0.95, 0.1, 0.85, 1.0) : vec4(0.2, 0.2, 0.2, 1.0);
      return;
    }
    if (pc.renderMode == 20) {  // double-sided
      outColor = ((pc.flags & 2) != 0) ? vec4(0.95, 0.55, 0.1, 1.0) : vec4(0.2, 0.2, 0.2, 1.0);
      return;
    }
    if (pc.renderMode == 18) {  // purpose (bits 2-3 of flags)
      outColor = vec4(purposeColor((pc.flags >> 2) & 3), 1.0);
      return;
    }
    if (pc.renderMode == 21) {  // skin weights: dominant joint tinted by weight
      outColor = vec4(idColor(vDomJoint) * (0.3 + 0.7 * clamp(vDomWeight, 0.0, 1.0)), 1.0);
      return;
    }
  }
  vec3 base = pc.baseColor.rgb * texture(uBaseColorTex, vUV).rgb;
  // Headlight-ish fixed directional light + ambient (matches the GL look roughly).
  vec3 L = normalize(vec3(0.5, 0.8, 0.6));
  float diff = max(dot(N, L), 0.0);
  vec3 c = base * (0.25 + 0.85 * diff);
  outColor = vec4(c, pc.baseColor.a);
}
