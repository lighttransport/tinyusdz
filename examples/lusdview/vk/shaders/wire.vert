#version 450

layout(location = 0) in vec3 aPosition;

layout(set = 2, binding = 0) uniform Frame {
  vec4 disp;
  mat4 viewProj;
  vec4 camPos;
  vec4 sceneMin;
  vec4 sceneExtent;
  vec4 lightDir;
  vec4 lightColor;
  ivec4 mode;
  mat4 shadowViewProj;
  vec4 shadowParams;
  mat4 pointShadowViewProj[6];
  vec4 pointShadowLight;
} fr;

layout(push_constant) uniform PushConstants {
  mat4 model;
  vec4 baseColor;
  vec4 matAux;
  vec4 emissive;
  ivec4 ids;
} pc;

void main() { gl_Position = fr.viewProj * pc.model * vec4(aPosition, 1.0); }
