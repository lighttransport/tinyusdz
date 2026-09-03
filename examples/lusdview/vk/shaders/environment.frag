#version 450
layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 49) uniform samplerCube uEnvironmentMap;
struct RasterLight { vec4 positionType; vec4 directionAngle;
                     vec4 colorDiffuse; vec4 specularShape; vec4 areaParams;
                     vec4 iesAxisX; vec4 iesAxisY; vec4 iesProfile[6]; };
layout(set = 2, binding = 0) uniform Frame {
  vec4 disp; mat4 viewProj; vec4 camPos; vec4 sceneMin; vec4 sceneExtent;
  vec4 lightDir; vec4 lightColor; RasterLight rasterLights[16];
  uvec4 rasterLightInfo; ivec4 mode; mat4 envRot; vec4 iblColor;
  vec4 iblParams; mat4 shadowViewProj; vec4 pointShadowLight;
  mat4 pointShadowViewProj[6];
} fr;
layout(push_constant) uniform EnvironmentPush { mat4 invViewProj; } pc;
vec3 linearToSrgb(vec3 c) {
  c = clamp(c, 0.0, 1.0);
  return mix(1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, c * 12.92,
             lessThanEqual(c, vec3(0.0031308)));
}
vec3 acesFitted(vec3 x) {
  return clamp((x * (2.51 * x + 0.03)) /
                   (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
void main() {
  vec4 world = pc.invViewProj * vec4(vNdc, 1.0, 1.0);
  vec3 direction = normalize(world.xyz / max(abs(world.w), 1.0e-6) -
                             fr.camPos.xyz);
  direction = normalize(mat3(fr.envRot) * direction);
  vec3 color = textureLod(uEnvironmentMap, direction, 0.0).rgb *
               fr.iblColor.rgb * exp2(fr.iblParams.y);
  if (fr.mode.y != 0) color = acesFitted(color);
  outColor = vec4(linearToSrgb(color), 1.0);
}
