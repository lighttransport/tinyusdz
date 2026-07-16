#version 450

// Tessellation evaluation: barycentric-interpolate the patch, sample the
// displacement height (set 4, red channel) and offset along the interpolated
// normal, then emit the same varyings mesh.frag consumes. mesh.frag shades with
// the geometric normal (flags bit0, set for displaced draws), so the new detail
// shades correctly. Displacement scale comes from the set-5 params UBO.

layout(triangles, equal_spacing, ccw) in;

layout(location = 0) in vec3 tcPos[];
layout(location = 1) in vec3 tcNrm[];
layout(location = 2) in vec2 tcUV[];

layout(set = 4, binding = 0) uniform sampler2D uDisplacementTex;
// Frame UBO (set 5): .disp.x = displacement scale, viewProj for clip position.
layout(set = 5, binding = 0) uniform Frame {
  vec4 disp;
  mat4 viewProj;
  vec4 camPos;
  vec4 sceneMin;
  vec4 sceneExtent;
  vec4 lightDir;
  vec4 lightColor;
  ivec4 mode;
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
  vec4 scalar0;
  vec4 scalar1;
  vec4 uvSets;  // per-slot UV set (see mesh.frag); unused here, but every stage's
                // copy of this struct must stay byte-identical.
  vec4 specParams;  // specular F0 (see mesh.frag); unused here, kept for
                    // the byte-identical SSBO stride.
  vec4 opacityUv0; vec4 opacityUv1;
  vec4 opacityParams;
  vec4 udimSlots0;
  vec4 udimSlots1;
};
layout(set = 6, binding = 0, std430) readonly buffer MatTex { MaterialTexParam p[]; } mtp;

layout(push_constant) uniform PushConstants {
  mat4 model;
  vec4 baseColor;
  vec4 matAux;
  vec4 emissive;
  ivec4 ids;
} pc;

layout(location = 0) out vec3 vNormalW;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vWorldPos;
layout(location = 3) flat out int vDomJoint;
layout(location = 4) out float vDomWeight;
layout(location = 5) out vec2 vUV1;
layout(location = 6) out float vMorphInfl;
// The frag shader consumes vColor (location 7). The tess path serves displaced
// meshes and carries no per-vertex color; white keeps shading unmodulated.
layout(location = 7) out vec4 vColor;

void main() {
  vec3 bc = gl_TessCoord;
  vec3 pos = bc.x * tcPos[0] + bc.y * tcPos[1] + bc.z * tcPos[2];
  vec3 nsum = bc.x * tcNrm[0] + bc.y * tcNrm[1] + bc.z * tcNrm[2];
  // normalize(vec3(0)) is NaN; guard so zero normals can't corrupt the position.
  vec3 nrm = dot(nsum, nsum) > 1e-12 ? normalize(nsum) : vec3(0.0);
  vec2 uv = bc.x * tcUV[0] + bc.y * tcUV[1] + bc.z * tcUV[2];
  int mid = max(pc.ids.x, 0);
  vec2 duv = vec2(dot(vec3(uv, 1.0), mtp.p[mid].dispUv0.xyz),
                  dot(vec3(uv, 1.0), mtp.p[mid].dispUv1.xyz));
  vec2 dsb = pc.ids.x >= 0 ? mtp.p[mid].scalar1.zw : vec2(1.0, 0.0);
  float disp = textureLod(uDisplacementTex, duv, 0.0).r * dsb.x + dsb.y;
  pos += nrm * (disp * fr.disp.x);
  mat3 nmat = transpose(inverse(mat3(pc.model)));
  vNormalW = nmat * nrm;
  vUV = uv;
  vWorldPos = (pc.model * vec4(pos, 1.0)).xyz;
  vDomJoint = -1;
  vDomWeight = 0.0;
  vUV1 = vec2(0.0);
  vMorphInfl = 0.0;
  vColor = vec4(1.0);
  gl_Position = fr.viewProj * pc.model * vec4(pos, 1.0);
}
