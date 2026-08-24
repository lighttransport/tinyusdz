#version 450
layout(location = 0) in vec3 vCol;
layout(location = 1) in vec3 vWorld;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0, r32f) uniform readonly image2D primaryDepth;
layout(push_constant) uniform PushConstants { mat4 vp; vec4 cameraPos; } pc;
void main() {
  ivec2 size = imageSize(primaryDepth);
  ivec2 pixel = ivec2(gl_FragCoord.xy);
  pixel = clamp(pixel, ivec2(0), size - ivec2(1));
  float surfaceDistance = imageLoad(primaryDepth, pixel).x;
  float lineDistance = length(vWorld - pc.cameraPos.xyz);
  float tolerance = max(1.0e-4, surfaceDistance * 2.0e-4);
  if (lineDistance > surfaceDistance + tolerance) discard;
  outColor = vec4(vCol, 1.0);
}
