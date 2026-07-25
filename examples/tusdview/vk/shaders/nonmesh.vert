#version 450

// Non-mesh vertex shader for Points (camera-facing billboards) and Curves
// (camera-facing ribbons).  Instanced: 4 vertices per instance (GL_TRIANGLE_STRIP).
// gl_VertexIndex % 4 selects the corner: (-1,-1), (1,-1), (-1,1), (1,1).

layout(location = 0) in vec3 aP0;       // point center or curve segment start
layout(location = 1) in vec3 aP1;       // point center (same as aP0) or curve seg end
layout(location = 2) in float aWidth;   // billboard/ribbon width
layout(location = 3) in vec4 aColor;    // rgba

// Frame UBO (set 2, binding 0) — same layout as mesh.vert
struct RasterLight { vec4 positionType; vec4 directionAngle;
                     vec4 colorDiffuse; vec4 specularShape; };
layout(set = 2, binding = 0) uniform Frame {
  vec4 disp;
  mat4 viewProj;
  vec4 camPos;
  vec4 sceneMin;
  vec4 sceneExtent;
  vec4 lightDir;
  vec4 lightColor;
  RasterLight rasterLights[16];
  uvec4 rasterLightInfo;
  ivec4 mode;
  mat4 envRot;
  vec4 iblColor;
  vec4 iblParams;
  mat4 shadowViewProj;
  vec4 pointShadowLight;
  mat4 pointShadowViewProj[6];
} fr;

layout(push_constant) uniform Push {
  int uKind;       // 0 = point billboard, 1 = curve ribbon
  int uMaterialId;
  int uCarrierId;
  int uPurpose;
  int uRenderMode;
  vec3 uCameraRight;   // first row of the view matrix (world-space RIGHT)
  vec3 uCameraUp;      // second row of the view matrix (world-space UP)
} pc;

layout(location = 0) out vec2 vLocal;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec3 vWorldPos;
layout(location = 3) out vec3 vView;
layout(location = 4) flat out int vInstanceId;

void main() {
  vec3 camRight = pc.uCameraRight;
  vec3 camUp = pc.uCameraUp;

  int cornerId = gl_VertexIndex & 3;
  vec2 corner = cornerId == 0 ? vec2(-1,-1) :
                cornerId == 1 ? vec2( 1,-1) :
                cornerId == 2 ? vec2(-1, 1) : vec2(1,1);

  vec3 p;
  if (pc.uKind == 0) {
    // Point billboard: camera-facing quad
    p = aP0 + (camRight * corner.x + camUp * corner.y) * (0.5 * aWidth);
    vLocal = corner;
  } else {
    // Curve ribbon: segment between aP0 and aP1, camera-facing
    float along = corner.y * 0.5 + 0.5;
    vec3 center = mix(aP0, aP1, along);
    vec3 tangent = normalize(aP1 - aP0);
    vec3 view = normalize(fr.camPos.xyz - center);
    vec3 side = normalize(cross(tangent, view));
    if (dot(side, side) < 1e-10) side = camRight;
    side = normalize(side);
    p = center + side * (corner.x * 0.5 * aWidth);
    vLocal = vec2(corner.x, along);
  }

  vColor = aColor;
  vWorldPos = p;
  vView = normalize(fr.camPos.xyz - p);
  vInstanceId = gl_InstanceIndex;
  gl_Position = fr.viewProj * vec4(p, 1.0);
}
