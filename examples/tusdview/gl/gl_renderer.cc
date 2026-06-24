// SPDX-License-Identifier: Apache-2.0
#include "gl/gl_renderer.hh"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "gl/gl_util.hh"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "light3d/material.h"
#include "light3d/math.h"

namespace tusdview {

namespace {

GLint GLWrap(int w) {
  switch (w) {
    case 1: return GL_REPEAT;
    case 2: return GL_MIRRORED_REPEAT;
    case 3: return GL_CLAMP_TO_BORDER;
    default: return GL_CLAMP_TO_EDGE;
  }
}

light3d::Mat4 ToMat4(const float* m) {
  light3d::Mat4 r;
  std::memcpy(r.m, m, sizeof(r.m));
  return r;
}

// Normal matrix (column-major 3x3) = transpose(inverse(upper-left 3x3 of world)).
void NormalMatrix3(const float m[16], float out[9]) {
  const float a00 = m[0], a01 = m[4], a02 = m[8];
  const float a10 = m[1], a11 = m[5], a12 = m[9];
  const float a20 = m[2], a21 = m[6], a22 = m[10];
  const float det = a00 * (a11 * a22 - a12 * a21) -
                    a01 * (a10 * a22 - a12 * a20) +
                    a02 * (a10 * a21 - a11 * a20);
  if (std::fabs(det) < 1e-12f) {
    out[0] = 1; out[1] = 0; out[2] = 0;
    out[3] = 0; out[4] = 1; out[5] = 0;
    out[6] = 0; out[7] = 0; out[8] = 1;
    return;
  }
  const float inv = 1.0f / det;
  // inv[row][col]
  out[0] = (a11 * a22 - a12 * a21) * inv;   // i00
  out[1] = -(a01 * a22 - a02 * a21) * inv;  // i01
  out[2] = (a01 * a12 - a02 * a11) * inv;   // i02
  out[3] = -(a10 * a22 - a12 * a20) * inv;  // i10
  out[4] = (a00 * a22 - a02 * a20) * inv;   // i11
  out[5] = -(a00 * a12 - a02 * a10) * inv;  // i12
  out[6] = (a10 * a21 - a11 * a20) * inv;   // i20
  out[7] = -(a00 * a21 - a01 * a20) * inv;  // i21
  out[8] = (a00 * a11 - a01 * a10) * inv;   // i22
}

}  // namespace

GLRenderer::~GLRenderer() { shutdown(); }

bool GLRenderer::init(GLFWwindow* window, std::string* err) {
  window_ = window;
  caps_.backend_name = "OpenGL";
  caps_.usesZeroToOneDepth = false;
  caps_.flipViewportV = true;
  caps_.supportsGpuSkinning = true;
  caps_.supportsExtendedGpuSkinning = true;
  const GLubyte* renderer = glGetString(GL_RENDERER);
  const GLubyte* vendor = glGetString(GL_VENDOR);
  const GLubyte* version = glGetString(GL_VERSION);
  caps_.gpu_name = renderer ? reinterpret_cast<const char*>(renderer) : "unknown";
  if (vendor && caps_.gpu_name.find(reinterpret_cast<const char*>(vendor)) ==
                    std::string::npos) {
    caps_.gpu_name = std::string(reinterpret_cast<const char*>(vendor)) + " " +
                     caps_.gpu_name;
  }
  caps_.api_info = version ? reinterpret_cast<const char*>(version) : "OpenGL";
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize_);

  program_ = glutil::CompileProgram(light3d::getMaterialVertexShaderGL330(),
                                    light3d::getMaterialFragmentShaderGL330(), err);
  if (!program_) {
    if (err && err->empty()) *err = "Failed to build GL material program";
    return false;
  }
  glUseProgram(program_);
  uMVP_ = glGetUniformLocation(program_, "uModelViewProj");
  uModel_ = glGetUniformLocation(program_, "uModel");
  uNormalMat_ = glGetUniformLocation(program_, "uNormalMatrix");
  uCameraPos_ = glGetUniformLocation(program_, "uCameraPos");
  uGeometricNormal_ = glGetUniformLocation(program_, "uGeometricNormal");
  uRenderMode_ = glGetUniformLocation(program_, "uRenderMode");
  uMatId_ = glGetUniformLocation(program_, "uMatId");
  uDepthScale_ = glGetUniformLocation(program_, "uDepthScale");
  uSceneMin_ = glGetUniformLocation(program_, "uSceneMin");
  uSceneExtent_ = glGetUniformLocation(program_, "uSceneExtent");
  uMeshId_ = glGetUniformLocation(program_, "uMeshId");
  uDoubleSided_ = glGetUniformLocation(program_, "uDoubleSided");
  uPurpose_ = glGetUniformLocation(program_, "uPurpose");
  uKind_ = glGetUniformLocation(program_, "uKind");
  uFaceIdTex_ = glGetUniformLocation(program_, "uFaceIdTex");
  uFaceBase_ = glGetUniformLocation(program_, "uFaceBase");
  uHasFaceId_ = glGetUniformLocation(program_, "uHasFaceId");
  uBaseColor_ = glGetUniformLocation(program_, "uBaseColor");
  uMetallic_ = glGetUniformLocation(program_, "uMetallic");
  uRoughness_ = glGetUniformLocation(program_, "uRoughness");
  uEmissive_ = glGetUniformLocation(program_, "uEmissive");
  uAlpha_ = glGetUniformLocation(program_, "uAlpha");
  uHasBaseColorTex_ = glGetUniformLocation(program_, "uHasBaseColorTex");
  uHasMetalRoughTex_ = glGetUniformLocation(program_, "uHasMetalRoughTex");
  uHasNormalTex_ = glGetUniformLocation(program_, "uHasNormalTex");
  uHasEmissiveTex_ = glGetUniformLocation(program_, "uHasEmissiveTex");
  uHasDisplacement_ = glGetUniformLocation(program_, "uHasDisplacement");
  uHasDisplacementTex_ = glGetUniformLocation(program_, "uHasDisplacementTex");
  uDisplacementConst_ = glGetUniformLocation(program_, "uDisplacementConst");
  uDisplacementScale_ = glGetUniformLocation(program_, "uDisplacementScale");
  uSkinningEnabled_ = glGetUniformLocation(program_, "uSkinningEnabled");
  uExtendedSkinningEnabled_ = glGetUniformLocation(program_, "uExtendedSkinningEnabled");
  uBoneTexWidth_ = glGetUniformLocation(program_, "uBoneTexWidth");
  uBoneMatrixCount_ = glGetUniformLocation(program_, "uBoneMatrixCount");
  uInfluenceTexWidth_ = glGetUniformLocation(program_, "uInfluenceTexWidth");
  // Fixed sampler -> texture-unit bindings.
  glUniform1i(glGetUniformLocation(program_, "uBaseColorTex"), 0);
  glUniform1i(glGetUniformLocation(program_, "uMetalRoughTex"), 1);
  glUniform1i(glGetUniformLocation(program_, "uNormalTex"), 2);
  glUniform1i(glGetUniformLocation(program_, "uEmissiveTex"), 3);
  glUniform1i(glGetUniformLocation(program_, "uBoneTex"), 4);
  glUniform1i(glGetUniformLocation(program_, "uInfluenceTex"), 5);
  glUniform1i(uFaceIdTex_, 6);  // source-face-id texture buffer
  glUniform1i(glGetUniformLocation(program_, "uDisplacementTex"), 7);
  glUseProgram(0);

  // GPU tessellation displacement program (GL >= 4.0 only). Failure to build is
  // non-fatal: the renderer simply keeps using the coarse per-vertex path.
  if (GLAD_GL_VERSION_4_0) {
    buildTessProgram();
  }

  // Instanced flat-shaded program: per-instance 3x4 object-to-world (attribs 6-8,
  // divisor 1) + per-instance/prototype displayColor (attrib 9), drawn with
  // glDrawElementsInstanced. Self-contained flat shader (Lambert + ambient +
  // Blinn spec, same hardcoded headlight as the material shader) so it needs no
  // material uniforms -- the base color comes from the per-instance vColor.
  static const char* kInstancedVS =
      "#version 330 core\n"
      "layout(location=0) in vec3 aPosition;\n"
      "layout(location=1) in vec3 aNormal;\n"
      // Per-instance 3x4 object-to-world (row-major o2w; rows = output x/y/z):
      // worldP.c = dot(vec4(pos,1), aRow[c]).  48 B/instance vs a full mat4's 64.
      "layout(location=6) in vec4 aRow0;\n"
      "layout(location=7) in vec4 aRow1;\n"
      "layout(location=8) in vec4 aRow2;\n"
      "layout(location=9) in vec3 aColor;\n"     // per-instance color or constant
      "layout(location=10) in vec3 aVtxColor;\n"  // per-vertex prototype color (or 1)
      "uniform mat4 uViewProj;\n"
      "out vec3 vWorldPos;\n"
      "out vec3 vNormal;\n"
      "out vec3 vColor;\n"
      "flat out int vInstanceId;\n"
      "void main(){\n"
      "  vInstanceId = gl_InstanceID;\n"
      "  vec4 p = vec4(aPosition, 1.0);\n"
      "  vec3 wp = vec3(dot(p, aRow0), dot(p, aRow1), dot(p, aRow2));\n"
      "  vec3 n = vec3(dot(aNormal, aRow0.xyz), dot(aNormal, aRow1.xyz),\n"
      "                dot(aNormal, aRow2.xyz));\n"
      "  vWorldPos = wp;\n"
      "  vNormal = normalize(n);\n"
      // Prototype per-vertex displayColor x per-instance color (both default 1).
      "  vColor = aColor * aVtxColor;\n"
      "  gl_Position = uViewProj * vec4(wp, 1.0);\n"
      "}\n";
  static const char* kInstancedFS =
      "#version 330 core\n"
      "in vec3 vWorldPos;\n"
      "in vec3 vNormal;\n"
      "in vec3 vColor;\n"
      "flat in int vInstanceId;\n"
      "uniform vec3 uCameraPos;\n"
      "uniform vec3 uEmissive;\n"  // selection-highlight override (else 0)
      // Debug-AOV uniforms (mirror the non-instanced material shader). Instanced
      // prototypes carry no UVs or material scalars, so UV / roughness / metallic /
      // emissive / opacity modes fall through to a neutral gray here.
      "uniform int uRenderMode;\n"
      "uniform float uDepthScale;\n"
      "uniform vec3 uSceneMin;\n"
      "uniform vec3 uSceneExtent;\n"
      "uniform int uMeshId;\n"
      "uniform bool uGeometricNormal;\n"
      "uniform bool uDoubleSided;\n"
      "uniform int uPurpose;\n"
      "uniform int uKind;\n"
      "out vec4 FragColor;\n"
      "vec3 idColor(int id){\n"
      "  if (id < 0) return vec3(0.45);\n"
      "  uint h = (uint(id) + 1u) * 2654435761u;\n"
      "  return vec3(float(h & 255u), float((h >> 8u) & 255u), float((h >> 16u) & 255u)) * (1.0/255.0);\n"
      "}\n"
      "vec3 purposeColor(int p){\n"
      "  if (p==1) return vec3(0.2,0.8,0.3);\n"
      "  if (p==2) return vec3(0.2,0.45,0.95);\n"
      "  if (p==3) return vec3(0.95,0.75,0.1);\n"
      "  return vec3(0.5);\n"
      "}\n"
      "vec3 kindColor(int k){\n"
      "  if (k==1) return vec3(0.2,0.8,0.8);\n"
      "  if (k==2) return vec3(0.85,0.3,0.85);\n"
      "  if (k==3) return vec3(0.95,0.6,0.15);\n"
      "  if (k==4) return vec3(0.5,0.85,0.4);\n"
      "  return vec3(0.35);\n"
      "}\n"
      "void main(){\n"
      // Geometric (screen-derivative) normal: instanced prototypes usually ship
      // without authored normals, and faceted shading reads cleanly for them.
      "  vec3 Ngeo = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));\n"
      "  vec3 N = Ngeo;\n"
      "  if (!gl_FrontFacing) N = -N;\n"  // thin instanced geom is often 1-sided
      "  if (uRenderMode != 0) {\n"
      "    vec3 Nshade = uGeometricNormal ? Ngeo : normalize(vNormal);\n"
      "    if (uRenderMode == 2) { FragColor = vec4(Nshade*0.5+0.5, 1.0); return; }\n"
      "    if (uRenderMode == 4) { FragColor = vec4(Ngeo*0.5+0.5, 1.0); return; }\n"
      "    if (uRenderMode == 6) {\n"
      "      float d = clamp(length(uCameraPos - vWorldPos) / max(uDepthScale,1e-3), 0.0, 1.0);\n"
      "      FragColor = vec4(vec3(1.0-d), 1.0); return; }\n"
      "    if (uRenderMode == 7) { FragColor = vec4(vColor, 1.0); return; }\n"  // albedo
      "    if (uRenderMode == 8) { FragColor = gl_FrontFacing ? vec4(0.1,0.7,0.1,1.0) : vec4(0.7,0.1,0.1,1.0); return; }\n"
      "    if (uRenderMode == 13) { FragColor = vec4(clamp((vWorldPos-uSceneMin)/uSceneExtent,0.0,1.0), 1.0); return; }\n"
      "    if (uRenderMode == 15) { FragColor = vec4(idColor(gl_PrimitiveID), 1.0); return; }\n"  // prim id
      "    if (uRenderMode == 16) { FragColor = vec4(idColor(uMeshId), 1.0); return; }\n"          // mesh id
      "    if (uRenderMode == 19) { FragColor = uGeometricNormal ? vec4(0.95,0.1,0.85,1.0) : vec4(0.2,0.2,0.2,1.0); return; }\n"
      "    if (uRenderMode == 20) { FragColor = uDoubleSided ? vec4(0.95,0.55,0.1,1.0) : vec4(0.2,0.2,0.2,1.0); return; }\n"
      "    if (uRenderMode == 18) { FragColor = vec4(purposeColor(uPurpose), 1.0); return; }\n"
      "    if (uRenderMode == 29) { FragColor = vec4(kindColor(uKind), 1.0); return; }\n"  // kind
      "    if (uRenderMode == 26) { FragColor = vec4(idColor(vInstanceId), 1.0); return; }\n"  // instance id
      "    if (uRenderMode == 25) {\n"  // curvature (screen-space geometric normal variation)
      "      vec3 n = Ngeo; float c = clamp((length(dFdx(n))+length(dFdy(n)))*8.0, 0.0, 1.0);\n"
      "      FragColor = vec4(c, 1.0-abs(c-0.5)*2.0, 1.0-c, 1.0); return; }\n"
      // Modes instanced geometry cannot supply (UV/material scalars): neutral gray
      // so it is visually obvious the channel has no data here, vs masquerading as
      // a lit render.
      "    FragColor = vec4(0.18,0.18,0.18,1.0); return;\n"
      "  }\n"
      "  vec3 V = normalize(uCameraPos - vWorldPos);\n"
      "  vec3 L = normalize(vec3(1.0, 1.0, 1.0));\n"
      "  float NdotL = max(dot(N, L), 0.0);\n"
      "  vec3 H = normalize(L + V);\n"
      "  float NdotH = max(dot(N, H), 0.0);\n"
      "  vec3 col = vColor * (0.05 + NdotL) + vec3(0.15) * pow(NdotH, 32.0);\n"
      "  FragColor = vec4(col + uEmissive, 1.0);\n"
      "}\n";
  instProgram_ = glutil::CompileProgram(kInstancedVS, kInstancedFS, err);
  if (!instProgram_) {
    if (err && err->empty()) *err = "Failed to build GL instanced program";
    return false;
  }
  glUseProgram(instProgram_);
  iUViewProj_ = glGetUniformLocation(instProgram_, "uViewProj");
  iCameraPos_ = glGetUniformLocation(instProgram_, "uCameraPos");
  iEmissive_ = glGetUniformLocation(instProgram_, "uEmissive");
  iRenderMode_ = glGetUniformLocation(instProgram_, "uRenderMode");
  iDepthScale_ = glGetUniformLocation(instProgram_, "uDepthScale");
  iSceneMin_ = glGetUniformLocation(instProgram_, "uSceneMin");
  iSceneExtent_ = glGetUniformLocation(instProgram_, "uSceneExtent");
  iMeshId_ = glGetUniformLocation(instProgram_, "uMeshId");
  iGeometricNormal_ = glGetUniformLocation(instProgram_, "uGeometricNormal");
  iDoubleSided_ = glGetUniformLocation(instProgram_, "uDoubleSided");
  iPurpose_ = glGetUniformLocation(instProgram_, "uPurpose");
  iKind_ = glGetUniformLocation(instProgram_, "uKind");
  glUseProgram(0);

  // 1x1 white default texture (bound to unused sampler units).
  glGenTextures(1, &whiteTex_);
  glBindTexture(GL_TEXTURE_2D, whiteTex_);
  const uint8_t white[4] = {255, 255, 255, 255};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, 0);

  glGenTextures(1, &boneTex_);
  glBindTexture(GL_TEXTURE_2D, boneTex_);
  const float ident[16] = {
      1, 0, 0, 0,
      0, 1, 0, 0,
      0, 0, 1, 0,
      0, 0, 0, 1};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, 1, 0, GL_RGBA, GL_FLOAT, ident);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  boneTexWidth_ = 4;
  boneTexHeight_ = 1;
  boneMatrixCount_ = 1;

  // Unlit, vertex-colored line program for debug helpers (grid/axes/bbox).
  {
    static const char* kLineVS =
        "#version 330 core\n"
        "layout(location=0) in vec3 aPos;\n"
        "layout(location=1) in vec3 aCol;\n"
        "uniform mat4 uVP;\n"
        "out vec3 vCol;\n"
        "void main(){ vCol=aCol; gl_Position=uVP*vec4(aPos,1.0); }\n";
    static const char* kLineFS =
        "#version 330 core\n"
        "in vec3 vCol; out vec4 fragColor;\n"
        "void main(){ fragColor=vec4(vCol,1.0); }\n";
    std::string lerr;
    lineProgram_ = glutil::CompileProgram(kLineVS, kLineFS, &lerr);
    if (lineProgram_) uLineVP_ = glGetUniformLocation(lineProgram_, "uVP");
    glGenVertexArrays(1, &lineVao_);
    glGenBuffers(1, &lineVbo_);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(HelperVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(HelperVertex),
                          (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
  }

  // UsdVol volume raymarch program + unit-cube proxy geometry.
  {
    static const char* kVolumeVS =
        "#version 330 core\n"
        "layout(location=0) in vec3 aPos;\n"  // unit cube [0,1]^3
        "uniform mat4 uVP;\n"
        "uniform mat4 uModel;\n"  // object -> world
        "uniform vec3 uBmin;\n"
        "uniform vec3 uBmax;\n"
        "out vec3 vWorld;\n"
        "void main(){\n"
        "  vec3 objp = uBmin + aPos*(uBmax-uBmin);\n"
        "  vec4 wp = uModel*vec4(objp,1.0);\n"
        "  vWorld = wp.xyz;\n"
        "  gl_Position = uVP*wp;\n"
        "}\n";
    static const char* kVolumeFS =
        "#version 330 core\n"
        "in vec3 vWorld; out vec4 fragColor;\n"
        "uniform vec3 uCameraPos;\n"
        "uniform mat4 uInvModel;\n"  // world -> object
        "uniform vec3 uBmin; uniform vec3 uBmax;\n"
        "uniform sampler3D uDensity;\n"
        "uniform float uDensityScale;\n"
        "uniform vec3 uAlbedo; uniform vec3 uEmission;\n"
        "uniform float uBackground;\n"
        "bool rayAABB(vec3 o, vec3 d, vec3 lo, vec3 hi, out float t0, out float t1){\n"
        "  vec3 inv = 1.0/d;\n"
        "  vec3 ta=(lo-o)*inv, tb=(hi-o)*inv;\n"
        "  vec3 tmin=min(ta,tb), tmax=max(ta,tb);\n"
        "  t0=max(max(tmin.x,tmin.y),tmin.z);\n"
        "  t1=min(min(tmax.x,tmax.y),tmax.z);\n"
        "  return t1>max(t0,0.0);\n"
        "}\n"
        "void main(){\n"
        "  vec3 oo=(uInvModel*vec4(uCameraPos,1.0)).xyz;\n"
        "  vec3 od=normalize((uInvModel*vec4(vWorld,1.0)).xyz - oo);\n"
        "  float t0,t1;\n"
        "  if(!rayAABB(oo,od,uBmin,uBmax,t0,t1)) discard;\n"
        "  t0=max(t0,0.0);\n"
        "  vec3 ext=uBmax-uBmin;\n"
        "  float step=min(ext.x,min(ext.y,ext.z))/128.0;\n"
        "  if(step<=0.0) step=(t1-t0)/256.0;\n"
        "  float T=1.0; vec3 L=vec3(0.0);\n"
        "  for(int i=0;i<256;i++){\n"
        "    float t=t0+(float(i)+0.5)*step;\n"
        "    if(t>=t1) break;\n"
        "    vec3 p=oo+od*t;\n"
        "    vec3 uvw=(p-uBmin)/ext;\n"
        "    float dens=(texture(uDensity,uvw).r - uBackground)*uDensityScale;\n"
        "    if(dens>0.0){\n"
        "      float a=1.0-exp(-dens*step);\n"
        "      vec3 src=uAlbedo*a + uEmission*(dens*step);\n"
        "      L+=T*src; T*=(1.0-a);\n"
        "      if(T<0.003) break;\n"
        "    }\n"
        "  }\n"
        "  float alpha=1.0-T;\n"
        "  if(alpha<=0.001) discard;\n"
        "  fragColor=vec4(L,alpha);\n"  // premultiplied
        "}\n";
    std::string verr;
    volumeProgram_ = glutil::CompileProgram(kVolumeVS, kVolumeFS, &verr);
    if (volumeProgram_) {
      uVolVP_ = glGetUniformLocation(volumeProgram_, "uVP");
      uVolModel_ = glGetUniformLocation(volumeProgram_, "uModel");
      uVolInvModel_ = glGetUniformLocation(volumeProgram_, "uInvModel");
      uVolCameraPos_ = glGetUniformLocation(volumeProgram_, "uCameraPos");
      uVolBmin_ = glGetUniformLocation(volumeProgram_, "uBmin");
      uVolBmax_ = glGetUniformLocation(volumeProgram_, "uBmax");
      uVolDensity_ = glGetUniformLocation(volumeProgram_, "uDensity");
      uVolDensityScale_ = glGetUniformLocation(volumeProgram_, "uDensityScale");
      uVolAlbedo_ = glGetUniformLocation(volumeProgram_, "uAlbedo");
      uVolEmission_ = glGetUniformLocation(volumeProgram_, "uEmission");
      uVolBackground_ = glGetUniformLocation(volumeProgram_, "uBackground");
    }
    static const float kCube[24] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0,
                                    0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1};
    static const unsigned int kCubeIdx[36] = {
        0, 1, 2, 0, 2, 3,  // -z
        4, 6, 5, 4, 7, 6,  // +z
        0, 4, 5, 0, 5, 1,  // -y
        3, 2, 6, 3, 6, 7,  // +y
        0, 3, 7, 0, 7, 4,  // -x
        1, 5, 6, 1, 6, 2}; // +x
    glGenVertexArrays(1, &volumeCubeVao_);
    glGenBuffers(1, &volumeCubeVbo_);
    glGenBuffers(1, &volumeCubeEbo_);
    glBindVertexArray(volumeCubeVao_);
    glBindBuffer(GL_ARRAY_BUFFER, volumeCubeVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCube), kCube, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, volumeCubeEbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kCubeIdx), kCubeIdx,
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                          (void*)0);
    glBindVertexArray(0);
  }
  return true;
}

#if defined(TUSDVIEW_ENABLE_GL_THREAD)
bool GLRenderer::initImGuiPlatform(GLFWwindow* window, std::string* err) {
  // GLFW callbacks + input: main thread (GLFW is main-thread-affine). init() runs
  // later on the render thread, so capture the window here on the main thread.
  window_ = window;
  if (!ImGui_ImplGlfw_InitForOpenGL(window_, true)) {
    if (err) *err = "ImGui_ImplGlfw_InitForOpenGL failed";
    return false;
  }
  return true;
}
bool GLRenderer::initImGuiBackend(std::string* err) {
  // GL objects (shaders, font texture): the context-owning thread.
  if (!ImGui_ImplOpenGL3_Init("#version 330")) {
    if (err) *err = "ImGui_ImplOpenGL3_Init failed";
    return false;
  }
  imguiInited_ = true;
  return true;
}
bool GLRenderer::initImGui(std::string* err) {
  return initImGuiPlatform(window_, err) && initImGuiBackend(err);
}
#else
bool GLRenderer::initImGui(std::string* err) {
  if (!ImGui_ImplGlfw_InitForOpenGL(window_, true)) {
    if (err) *err = "ImGui_ImplGlfw_InitForOpenGL failed";
    return false;
  }
  if (!ImGui_ImplOpenGL3_Init("#version 330")) {
    if (err) *err = "ImGui_ImplOpenGL3_Init failed";
    return false;
  }
  imguiInited_ = true;
  return true;
}
#endif

void GLRenderer::buildTessProgram() {
  // Object-space passthrough VS -> TCS (adaptive level) -> TES (interpolate +
  // displace along the interpolated normal) -> FS (geometric-normal shading).
  // Self-contained for the Shaded view; AOV modes keep using the coarse program.
  static const char* kVS =
      "#version 410 core\n"
      "layout(location=0) in vec3 aPosition;\n"
      "layout(location=1) in vec3 aNormal;\n"
      "layout(location=2) in vec3 aUV;\n"
      "out vec3 vcPos; out vec3 vcNrm; out vec2 vcUV;\n"
      "void main(){ vcPos=aPosition; vcNrm=aNormal; vcUV=aUV.xy; }\n";
  static const char* kTCS =
      "#version 410 core\n"
      "layout(vertices=3) out;\n"
      "in vec3 vcPos[]; in vec3 vcNrm[]; in vec2 vcUV[];\n"
      "out vec3 tcPos[]; out vec3 tcNrm[]; out vec2 tcUV[];\n"
      "uniform mat4 uModel; uniform vec3 uCameraPos; uniform float uMaxTessLevel;\n"
      // Per-edge level from the world-space edge length relative to its distance
      // to the camera: nearer / longer edges subdivide more. Clamped to the slider.
      "float edgeLevel(vec3 a, vec3 b){\n"
      "  vec3 wa=(uModel*vec4(a,1.0)).xyz, wb=(uModel*vec4(b,1.0)).xyz;\n"
      "  vec3 mid=0.5*(wa+wb); float len=length(wa-wb);\n"
      "  float dist=max(length(uCameraPos-mid),1e-3);\n"
      "  return clamp(len/dist*120.0, 1.0, uMaxTessLevel);\n"
      "}\n"
      "void main(){\n"
      "  tcPos[gl_InvocationID]=vcPos[gl_InvocationID];\n"
      "  tcNrm[gl_InvocationID]=vcNrm[gl_InvocationID];\n"
      "  tcUV[gl_InvocationID]=vcUV[gl_InvocationID];\n"
      "  if(gl_InvocationID==0){\n"
      "    float l0=edgeLevel(vcPos[1],vcPos[2]);\n"
      "    float l1=edgeLevel(vcPos[2],vcPos[0]);\n"
      "    float l2=edgeLevel(vcPos[0],vcPos[1]);\n"
      "    gl_TessLevelOuter[0]=l0; gl_TessLevelOuter[1]=l1; gl_TessLevelOuter[2]=l2;\n"
      "    gl_TessLevelInner[0]=max(max(l0,l1),l2);\n"
      "  }\n"
      "}\n";
  static const char* kTES =
      "#version 410 core\n"
      "layout(triangles, equal_spacing, ccw) in;\n"
      "in vec3 tcPos[]; in vec3 tcNrm[]; in vec2 tcUV[];\n"
      "out vec3 vWorldPos; out vec3 vNormal; out vec2 vUV;\n"
      "uniform mat4 uModelViewProj; uniform mat4 uModel; uniform mat3 uNormalMatrix;\n"
      "uniform sampler2D uDisplacementTex; uniform bool uHasDisplacementTex;\n"
      "uniform float uDisplacementConst; uniform float uDisplacementScale;\n"
      "void main(){\n"
      "  vec3 bc=gl_TessCoord;\n"
      "  vec3 pos=bc.x*tcPos[0]+bc.y*tcPos[1]+bc.z*tcPos[2];\n"
      "  vec3 nrm=normalize(bc.x*tcNrm[0]+bc.y*tcNrm[1]+bc.z*tcNrm[2]);\n"
      "  vec2 uv=bc.x*tcUV[0]+bc.y*tcUV[1]+bc.z*tcUV[2];\n"
      "  float d=uHasDisplacementTex? textureLod(uDisplacementTex,uv,0.0).r : uDisplacementConst;\n"
      "  pos += nrm*(d*uDisplacementScale);\n"
      "  vWorldPos=(uModel*vec4(pos,1.0)).xyz;\n"
      "  vNormal=normalize(uNormalMatrix*nrm);\n"
      "  vUV=uv;\n"
      "  gl_Position=uModelViewProj*vec4(pos,1.0);\n"
      "}\n";
  static const char* kFS =
      "#version 410 core\n"
      "in vec3 vWorldPos; in vec3 vNormal; in vec2 vUV;\n"
      "uniform vec3 uCameraPos; uniform vec3 uBaseColor;\n"
      "uniform sampler2D uBaseColorTex; uniform bool uHasBaseColorTex;\n"
      "out vec4 FragColor;\n"
      "void main(){\n"
      "  vec3 base=uBaseColor;\n"
      "  if(uHasBaseColorTex) base*=texture(uBaseColorTex,vUV).rgb;\n"
      // Geometric normal of the displaced surface (screen derivatives) so the new
      // height detail actually shades, matching the coarse path's displaced look.
      "  vec3 N=normalize(cross(dFdx(vWorldPos),dFdy(vWorldPos)));\n"
      "  if(!gl_FrontFacing) N=-N;\n"
      "  vec3 V=normalize(uCameraPos-vWorldPos);\n"
      "  vec3 L=normalize(vec3(1.0,1.0,1.0));\n"
      "  float NdotL=max(dot(N,L),0.0);\n"
      "  vec3 H=normalize(L+V); float NdotH=max(dot(N,H),0.0);\n"
      "  vec3 col=base*(0.05+NdotL)+vec3(0.15)*pow(NdotH,32.0);\n"
      "  FragColor=vec4(col,1.0);\n"
      "}\n";
  std::string terr;
  tessProgram_ = glutil::CompileProgramTess(kVS, kTCS, kTES, kFS, &terr);
  if (!tessProgram_) {
    // Best-effort: keep coarse displacement. (Logged, not fatal.)
    fprintf(stderr, "[tusdview] GL tessellation program unavailable: %s\n",
            terr.c_str());
    return;
  }
  glUseProgram(tessProgram_);
  tMVP_ = glGetUniformLocation(tessProgram_, "uModelViewProj");
  tModel_ = glGetUniformLocation(tessProgram_, "uModel");
  tNormalMat_ = glGetUniformLocation(tessProgram_, "uNormalMatrix");
  tCameraPos_ = glGetUniformLocation(tessProgram_, "uCameraPos");
  tBaseColor_ = glGetUniformLocation(tessProgram_, "uBaseColor");
  tHasBaseColorTex_ = glGetUniformLocation(tessProgram_, "uHasBaseColorTex");
  tHasDisplacementTex_ = glGetUniformLocation(tessProgram_, "uHasDisplacementTex");
  tDisplacementConst_ = glGetUniformLocation(tessProgram_, "uDisplacementConst");
  tDisplacementScale_ = glGetUniformLocation(tessProgram_, "uDisplacementScale");
  tMaxTessLevel_ = glGetUniformLocation(tessProgram_, "uMaxTessLevel");
  glUniform1i(glGetUniformLocation(tessProgram_, "uBaseColorTex"), 0);
  glUniform1i(glGetUniformLocation(tessProgram_, "uDisplacementTex"), 7);
  glUseProgram(0);
  tessAvailable_ = true;
}

void GLRenderer::destroyScene() {
  for (auto& m : meshes_) {
    if (m.ebo) glDeleteBuffers(1, &m.ebo);
    if (m.influenceTex) glDeleteTextures(1, &m.influenceTex);
    if (m.influenceVbo) glDeleteBuffers(1, &m.influenceVbo);
    if (m.weightVbo) glDeleteBuffers(1, &m.weightVbo);
    if (m.jointVbo) glDeleteBuffers(1, &m.jointVbo);
    if (m.instanceVbo) glDeleteBuffers(1, &m.instanceVbo);
    if (m.instanceColorVbo) glDeleteBuffers(1, &m.instanceColorVbo);
    if (m.vertexColorVbo) glDeleteBuffers(1, &m.vertexColorVbo);
    if (m.uv1Vbo) glDeleteBuffers(1, &m.uv1Vbo);
    if (m.morphInflVbo) glDeleteBuffers(1, &m.morphInflVbo);
    if (m.faceIdTex) glDeleteTextures(1, &m.faceIdTex);
    if (m.faceIdBuf) glDeleteBuffers(1, &m.faceIdBuf);
    if (m.vbo) glDeleteBuffers(1, &m.vbo);
    if (m.vao) glDeleteVertexArrays(1, &m.vao);
  }
  meshes_.clear();
  for (GLVolume& gv : volumes_) {
    if (gv.tex3d) glDeleteTextures(1, &gv.tex3d);
  }
  volumes_.clear();
  if (!textures_.empty()) {
    glDeleteTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
  }
  textures_.clear();
  materials_.clear();
}

void GLRenderer::beginScene(const std::vector<DrawMaterialCPU>& materials,
                            int textureCount) {
  destroyScene();
  // Reserve texture slots (0 = not yet uploaded -> resolved to white at draw).
  textures_.assign(textureCount > 0 ? static_cast<size_t>(textureCount) : 0, 0);
  materials_.reserve(materials.size());
  for (const auto& m : materials) {
    GLMaterial gm;
    gm.baseColor[0] = m.baseColor[0];
    gm.baseColor[1] = m.baseColor[1];
    gm.baseColor[2] = m.baseColor[2];
    gm.metallic = m.metallic;
    gm.roughness = m.roughness;
    gm.emissive[0] = m.emissive[0];
    gm.emissive[1] = m.emissive[1];
    gm.emissive[2] = m.emissive[2];
    gm.alpha = m.alpha;
    gm.baseColorTex = m.baseColorTex;  // slot indices (resolved at draw)
    gm.metalRoughTex = m.metalRoughTex;
    gm.normalTex = m.normalTex;
    gm.emissiveTex = m.emissiveTex;
    gm.displacementTex = m.displacementTex;
    gm.displacementConst = m.displacementConst;
    materials_.push_back(gm);
  }
}

void GLRenderer::uploadTexture(int slot, const DrawTextureCPU& t) {
  if (slot < 0 || static_cast<size_t>(slot) >= textures_.size()) return;
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  // Upload as plain RGBA8 (texels used as-is; see note: the simple shader and
  // linear RGBA8 target don't re-encode gamma).
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.image.width, t.image.height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, t.image.data.empty() ? nullptr : t.image.data.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLWrap(t.wrapS));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLWrap(t.wrapT));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  if (textures_[static_cast<size_t>(slot)]) {
    glDeleteTextures(1, &textures_[static_cast<size_t>(slot)]);
  }
  textures_[static_cast<size_t>(slot)] = tex;
}

void GLRenderer::uploadSkinningFrame(const SkinningFrameCPU& skin) {
  if (!boneTex_) return;
  const bool valid = skin.enabled && skin.matrixCount > 0 &&
                     skin.rgba32f.size() >= static_cast<size_t>(skin.matrixCount) * 16;
  skinningFrameEnabled_ = valid;
  const float ident[16] = {
      1, 0, 0, 0,
      0, 1, 0, 0,
      0, 0, 1, 0,
      0, 0, 0, 1};
  const int matrixCount = valid ? skin.matrixCount : 1;
  const size_t texels = static_cast<size_t>(matrixCount) * 4;
  int w = static_cast<int>(std::min<size_t>(texels, 1024));
  w = std::max(4, std::min(w, maxTextureSize_));
  int h = static_cast<int>((texels + static_cast<size_t>(w) - 1) /
                           static_cast<size_t>(w));
  if (h > maxTextureSize_) {
    w = maxTextureSize_;
    h = static_cast<int>((texels + static_cast<size_t>(w) - 1) /
                         static_cast<size_t>(w));
  }
  const size_t packedFloats = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
  if (boneUploadScratch_.size() != packedFloats) {
    boneUploadScratch_.assign(packedFloats, 0.0f);
  } else if (packedFloats > texels * 4) {
    std::fill(boneUploadScratch_.data() + texels * 4,
              boneUploadScratch_.data() + packedFloats, 0.0f);
  }
  const float* src = valid ? skin.rgba32f.data() : ident;
  std::copy(src, src + texels * 4, boneUploadScratch_.begin());
  glBindTexture(GL_TEXTURE_2D, boneTex_);
  if (w != boneTexWidth_ || h != boneTexHeight_) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT,
                 boneUploadScratch_.data());
    boneTexWidth_ = w;
    boneTexHeight_ = h;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_FLOAT,
                    boneUploadScratch_.data());
  }
  boneMatrixCount_ = matrixCount;
  glBindTexture(GL_TEXTURE_2D, 0);
}

void GLRenderer::updateMeshVertices(int meshIndex,
                                    const std::vector<DrawVertex>& verts) {
  if (meshIndex < 0 || meshIndex >= static_cast<int>(meshes_.size())) return;
  GLMesh& gm = meshes_[static_cast<size_t>(meshIndex)];
  if (!gm.vbo || verts.size() != gm.vertexCount) return;  // count must match
  glBindBuffer(GL_ARRAY_BUFFER, gm.vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0,
                  static_cast<GLsizeiptr>(verts.size() * sizeof(DrawVertex)),
                  verts.data());
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GLRenderer::updateMeshWorld(int meshIndex, const float world[16]) {
  if (meshIndex < 0 || meshIndex >= static_cast<int>(meshes_.size())) return;
  // GL recomputes the normal matrix from `world` each draw, so storing the new
  // world is enough.
  std::memcpy(meshes_[static_cast<size_t>(meshIndex)].world, world,
              sizeof(float) * 16);
}

void GLRenderer::replaceMesh(int meshIndex, const DrawMeshCPU& sm) {
  if (meshIndex < 0 || meshIndex >= static_cast<int>(meshes_.size())) return;
  GLMesh& gm = meshes_[static_cast<size_t>(meshIndex)];

  // Delete old GPU resources.
  if (gm.vao) glDeleteVertexArrays(1, &gm.vao);
  if (gm.vbo) glDeleteBuffers(1, &gm.vbo);
  if (gm.ebo) glDeleteBuffers(1, &gm.ebo);
  if (gm.jointVbo) glDeleteBuffers(1, &gm.jointVbo);
  if (gm.weightVbo) glDeleteBuffers(1, &gm.weightVbo);
  if (gm.influenceVbo) glDeleteBuffers(1, &gm.influenceVbo);
  if (gm.influenceTex) glDeleteTextures(1, &gm.influenceTex);

  // Re-create with new data (same logic as appendMesh, but in-place).
  gm = GLMesh{};
  gm.submeshes = sm.submeshes;
  std::memcpy(gm.world, sm.world, sizeof(gm.world));
  gm.doubleSided = sm.doubleSided;
  gm.purposeId = PurposeId(sm.purpose);
  gm.kindId = sm.kindId;
  gm.skinned = sm.jointIdx.size() == sm.vertices.size() * 4 &&
               sm.jointWt.size() == sm.vertices.size() * 4;
  gm.extendedSkinned =
      gm.skinned && sm.influenceOffsetCount.size() == sm.vertices.size() * 2 &&
      !sm.influenceTexels.empty() && sm.influenceTexWidth > 0 &&
      sm.influenceTexHeight > 0 && sm.maxInfluencesPerVertex > 4;
  gm.influenceTexWidth = sm.influenceTexWidth;
  gm.vertexCount = sm.vertices.size();

  glGenVertexArrays(1, &gm.vao);
  glBindVertexArray(gm.vao);
  glGenBuffers(1, &gm.vbo);
  glBindBuffer(GL_ARRAY_BUFFER, gm.vbo);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(sm.vertices.size() * sizeof(DrawVertex)),
               sm.vertices.data(), GL_STATIC_DRAW);
  glGenBuffers(1, &gm.ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gm.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(sm.indices.size() * sizeof(uint32_t)),
               sm.indices.data(), GL_STATIC_DRAW);
  const GLsizei stride = sizeof(DrawVertex);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
  if (gm.skinned) {
    glGenBuffers(1, &gm.jointVbo);
    glBindBuffer(GL_ARRAY_BUFFER, gm.jointVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sm.jointIdx.size() * sizeof(uint32_t)),
                 sm.jointIdx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_UNSIGNED_INT, 4 * sizeof(uint32_t), (void*)0);
    glGenBuffers(1, &gm.weightVbo);
    glBindBuffer(GL_ARRAY_BUFFER, gm.weightVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sm.jointWt.size() * sizeof(float)),
                 sm.jointWt.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    if (gm.extendedSkinned) {
      glGenBuffers(1, &gm.influenceVbo);
      glBindBuffer(GL_ARRAY_BUFFER, gm.influenceVbo);
      glBufferData(GL_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(sm.influenceOffsetCount.size() *
                                           sizeof(uint32_t)),
                   sm.influenceOffsetCount.data(), GL_STATIC_DRAW);
      glEnableVertexAttribArray(5);
      glVertexAttribIPointer(5, 2, GL_UNSIGNED_INT, 2 * sizeof(uint32_t), (void*)0);
      glGenTextures(1, &gm.influenceTex);
      glBindTexture(GL_TEXTURE_2D, gm.influenceTex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, sm.influenceTexWidth,
                   sm.influenceTexHeight, 0, GL_RGBA, GL_FLOAT,
                   sm.influenceTexels.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
      glDisableVertexAttribArray(5);
      glVertexAttribI2ui(5, 0, 0);
    }
  } else {
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);
    glDisableVertexAttribArray(5);
    glVertexAttribI4ui(3, 0, 0, 0, 0);
    glVertexAttrib4f(4, 0.0f, 0.0f, 0.0f, 0.0f);
    glVertexAttribI2ui(5, 0, 0);
  }
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void GLRenderer::appendMesh(const DrawMeshCPU& sm) {
  GLMesh gm;
  gm.submeshes = sm.submeshes;
  std::memcpy(gm.world, sm.world, sizeof(gm.world));
  gm.doubleSided = sm.doubleSided;
  gm.purposeId = PurposeId(sm.purpose);
  gm.kindId = sm.kindId;
  gm.skinned = sm.jointIdx.size() == sm.vertices.size() * 4 &&
               sm.jointWt.size() == sm.vertices.size() * 4;
  gm.extendedSkinned =
      gm.skinned && sm.influenceOffsetCount.size() == sm.vertices.size() * 2 &&
      !sm.influenceTexels.empty() && sm.influenceTexWidth > 0 &&
      sm.influenceTexHeight > 0 && sm.maxInfluencesPerVertex > 4;
  gm.influenceTexWidth = sm.influenceTexWidth;
  gm.vertexCount = sm.vertices.size();

  glGenVertexArrays(1, &gm.vao);
  glBindVertexArray(gm.vao);
  glGenBuffers(1, &gm.vbo);
  glBindBuffer(GL_ARRAY_BUFFER, gm.vbo);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(sm.vertices.size() * sizeof(DrawVertex)),
               sm.vertices.data(), GL_STATIC_DRAW);
  glGenBuffers(1, &gm.ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gm.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(sm.indices.size() * sizeof(uint32_t)),
               sm.indices.data(), GL_STATIC_DRAW);
  const GLsizei stride = sizeof(DrawVertex);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
  if (gm.skinned) {
    glGenBuffers(1, &gm.jointVbo);
    glBindBuffer(GL_ARRAY_BUFFER, gm.jointVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sm.jointIdx.size() * sizeof(uint32_t)),
                 sm.jointIdx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_UNSIGNED_INT, 4 * sizeof(uint32_t), (void*)0);
    glGenBuffers(1, &gm.weightVbo);
    glBindBuffer(GL_ARRAY_BUFFER, gm.weightVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sm.jointWt.size() * sizeof(float)),
                 sm.jointWt.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    if (gm.extendedSkinned) {
      glGenBuffers(1, &gm.influenceVbo);
      glBindBuffer(GL_ARRAY_BUFFER, gm.influenceVbo);
      glBufferData(GL_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(sm.influenceOffsetCount.size() *
                                           sizeof(uint32_t)),
                   sm.influenceOffsetCount.data(), GL_STATIC_DRAW);
      glEnableVertexAttribArray(5);
      glVertexAttribIPointer(5, 2, GL_UNSIGNED_INT, 2 * sizeof(uint32_t), (void*)0);

      glGenTextures(1, &gm.influenceTex);
      glBindTexture(GL_TEXTURE_2D, gm.influenceTex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, sm.influenceTexWidth,
                   sm.influenceTexHeight, 0, GL_RGBA, GL_FLOAT,
                   sm.influenceTexels.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
      glDisableVertexAttribArray(5);
      glVertexAttribI2ui(5, 0, 0);
    }
  } else {
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);
    glDisableVertexAttribArray(5);
    glVertexAttribI4ui(3, 0, 0, 0, 0);
    glVertexAttrib4f(4, 0.0f, 0.0f, 0.0f, 0.0f);
    glVertexAttribI2ui(5, 0, 0);
  }

  gm.geometricNormal = sm.geometricNormal;
  gm.purposeId = PurposeId(sm.purpose);
  gm.kindId = sm.kindId;
  // Per-vertex displayColor (divisor 0). Non-instanced meshes bind it at attrib 9
  // (the shared material shader's aColor); instanced meshes bind it at attrib 10
  // (the instanced shader multiplies per-vertex x per-instance color, since attrib
  // 9 there carries the per-instance color set below). Default white (set per draw)
  // when absent so the base color is unmodulated.
  const bool gmInstanced = !sm.instanceXforms.empty();
  const GLuint vtxColorAttrib = gmInstanced ? 10u : 9u;
  if (sm.vertexColors.size() == sm.vertices.size() * 3 && !sm.vertexColors.empty()) {
    glGenBuffers(1, &gm.vertexColorVbo);
    glBindBuffer(GL_ARRAY_BUFFER, gm.vertexColorVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sm.vertexColors.size() * sizeof(float)),
                 sm.vertexColors.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(vtxColorAttrib);
    glVertexAttribPointer(vtxColorAttrib, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                          (void*)0);
    glVertexAttribDivisor(vtxColorAttrib, 0);
  } else {
    glDisableVertexAttribArray(vtxColorAttrib);  // constant white set per draw
  }

  // Multi-UV (attrib 6) + blendshape influence (attrib 7), non-instanced only --
  // instanced meshes reuse attribs 6-8 for the per-instance rows (separate
  // program). Default 0 when the mesh lacks the data so modes read as zero.
  if (!gmInstanced) {
    if (sm.uv1.size() == sm.vertices.size() * 2 && !sm.uv1.empty()) {
      glGenBuffers(1, &gm.uv1Vbo);
      glBindBuffer(GL_ARRAY_BUFFER, gm.uv1Vbo);
      glBufferData(GL_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(sm.uv1.size() * sizeof(float)),
                   sm.uv1.data(), GL_STATIC_DRAW);
      glEnableVertexAttribArray(6);
      glVertexAttribPointer(6, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
      glVertexAttribDivisor(6, 0);
    } else {
      glDisableVertexAttribArray(6);
      glVertexAttrib2f(6, 0.0f, 0.0f);
    }
    if (sm.morphInfluence.size() == sm.vertices.size() && !sm.morphInfluence.empty()) {
      glGenBuffers(1, &gm.morphInflVbo);
      glBindBuffer(GL_ARRAY_BUFFER, gm.morphInflVbo);
      glBufferData(GL_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(sm.morphInfluence.size() * sizeof(float)),
                   sm.morphInfluence.data(), GL_STATIC_DRAW);
      glEnableVertexAttribArray(7);
      glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
      glVertexAttribDivisor(7, 0);
    } else {
      glDisableVertexAttribArray(7);
      glVertexAttrib1f(7, 0.0f);
    }
    // Per-triangle source face id as a texture buffer (source-face-id AOV),
    // fetched in the FS by gl_PrimitiveID + the submesh's first-triangle offset.
    if (sm.sourceFaceId.size() == sm.indices.size() / 3 && !sm.sourceFaceId.empty()) {
      glGenBuffers(1, &gm.faceIdBuf);
      glBindBuffer(GL_TEXTURE_BUFFER, gm.faceIdBuf);
      glBufferData(GL_TEXTURE_BUFFER,
                   static_cast<GLsizeiptr>(sm.sourceFaceId.size() * sizeof(uint32_t)),
                   sm.sourceFaceId.data(), GL_STATIC_DRAW);
      glGenTextures(1, &gm.faceIdTex);
      glBindTexture(GL_TEXTURE_BUFFER, gm.faceIdTex);
      glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, gm.faceIdBuf);
      glBindBuffer(GL_TEXTURE_BUFFER, 0);
      glBindTexture(GL_TEXTURE_BUFFER, 0);
    }
  }

  // GPU instancing: upload per-instance 3x4 object-to-world matrices (3 vec4
  // rows = 48 B/instance) into a second VBO; bind as instanced attribs 6-8
  // (divisor 1).
  if (!sm.instanceXforms.empty()) {
    gm.instanceCount = static_cast<int>(sm.instanceXforms.size() / 12);
    gm.drawInstanceCount = gm.instanceCount;
    glGenBuffers(1, &gm.instanceVbo);
    glBindBuffer(GL_ARRAY_BUFFER, gm.instanceVbo);
    // GL_DYNAMIC_DRAW: per-instance culling re-uploads the visible subset.
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sm.instanceXforms.size() * sizeof(float)),
                 sm.instanceXforms.data(), GL_DYNAMIC_DRAW);
    const GLsizei mstride = 12 * sizeof(float);
    for (int r = 0; r < 3; ++r) {
      const GLuint loc = 6 + r;
      glEnableVertexAttribArray(loc);
      glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, mstride,
                            (void*)(static_cast<uintptr_t>(r * 4 * sizeof(float))));
      glVertexAttribDivisor(loc, 1);
    }
    // Per-instance displayColor (attrib 9, divisor 1) when present; otherwise a
    // per-draw constant set at draw time (see flatColor in the instanced pass).
    std::memcpy(gm.flatColor, sm.flatColor, sizeof(gm.flatColor));
    if (sm.instanceColors.size() == sm.instanceXforms.size() / 12 * 3 &&
        !sm.instanceColors.empty()) {
      gm.hasInstanceColors = true;
      glGenBuffers(1, &gm.instanceColorVbo);
      glBindBuffer(GL_ARRAY_BUFFER, gm.instanceColorVbo);
      glBufferData(GL_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(sm.instanceColors.size() * sizeof(float)),
                   sm.instanceColors.data(), GL_DYNAMIC_DRAW);
      glEnableVertexAttribArray(9);
      glVertexAttribPointer(9, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
      glVertexAttribDivisor(9, 1);
    } else {
      glDisableVertexAttribArray(9);  // constant color set per draw
    }
  }

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  meshes_.push_back(gm);
}

void GLRenderer::updateInstanceVisibility(size_t meshIndex, const float* xforms,
                                          const float* colors, uint32_t count) {
  if (meshIndex >= meshes_.size()) return;
  GLMesh& m = meshes_[meshIndex];
  if (m.instanceCount <= 0) return;
  if (count > static_cast<uint32_t>(m.instanceCount))
    count = static_cast<uint32_t>(m.instanceCount);
  m.drawInstanceCount = static_cast<int>(count);
  if (count == 0) return;
  // Re-upload the compacted visible subset to the front of the dynamic buffers.
  if (m.instanceVbo && xforms) {
    glBindBuffer(GL_ARRAY_BUFFER, m.instanceVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(count) * 12 * sizeof(float), xforms);
  }
  if (m.instanceColorVbo && colors) {
    glBindBuffer(GL_ARRAY_BUFFER, m.instanceColorVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(count) * 3 * sizeof(float), colors);
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GLRenderer::ensureFbo(int w, int h) {
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (fbo_ && w == vpW_ && h == vpH_) return;
  vpW_ = w;
  vpH_ = h;
  if (!fbo_) glGenFramebuffers(1, &fbo_);
  if (!colorTex_) glGenTextures(1, &colorTex_);
  if (!depthRbo_) glGenRenderbuffers(1, &depthRbo_);

  glBindTexture(GL_TEXTURE_2D, colorTex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glBindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);

  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRbo_);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void GLRenderer::resizeViewport(int width, int height) { ensureFbo(width, height); }

void GLRenderer::newFrame() { ImGui_ImplOpenGL3_NewFrame(); }

void GLRenderer::drawMeshes(const RenderFrameParams& params, bool wireframe,
                            const float* overrideEmissive) {
  static const GLMaterial kDefault;
  light3d::Mat4 P = ToMat4(params.proj);
  light3d::Mat4 V = ToMat4(params.view);

  for (size_t mi = 0; mi < meshes_.size(); ++mi) {
    if (params.meshVisible && mi < static_cast<size_t>(params.meshVisibleCount) &&
        !params.meshVisible[mi]) {
      continue;  // hidden by the viewer's per-mesh visibility mask
    }
    const GLMesh& mesh = meshes_[mi];
    if (mesh.instanceCount > 0) continue;  // drawn in the instanced pass below
    light3d::Mat4 W = ToMat4(mesh.world);
    light3d::Mat4 MVP = P * V * W;
    float nmat[9];
    NormalMatrix3(mesh.world, nmat);
    glUniformMatrix4fv(uMVP_, 1, GL_FALSE, MVP.m);
    glUniformMatrix4fv(uModel_, 1, GL_FALSE, W.m);
    glUniformMatrix3fv(uNormalMat_, 1, GL_FALSE, nmat);
    glUniform1i(uGeometricNormal_, mesh.geometricNormal ? 1 : 0);
    glUniform1i(uRenderMode_, static_cast<int>(params.mode));
    glUniform1f(uDepthScale_, params.depthScale > 1e-4f ? params.depthScale : 1.0f);
    glUniform3fv(uSceneMin_, 1, params.sceneMin);
    glUniform3fv(uSceneExtent_, 1, params.sceneExtent);
    glUniform1i(uMeshId_, static_cast<int>(mi));
    glUniform1i(uDoubleSided_, mesh.doubleSided ? 1 : 0);
    glUniform1i(uPurpose_, mesh.purposeId);
    glUniform1i(uKind_, mesh.kindId);
    glUniform1i(uHasFaceId_, mesh.faceIdTex ? 1 : 0);
    if (mesh.faceIdTex) {
      glActiveTexture(GL_TEXTURE6);
      glBindTexture(GL_TEXTURE_BUFFER, mesh.faceIdTex);
      glActiveTexture(GL_TEXTURE0);
    }
    // Default per-vertex color to white when the mesh has none (so uBaseColor is
    // unmodulated); the VAO supplies the array when vertexColorVbo is set.
    if (!mesh.vertexColorVbo) glVertexAttrib3f(9, 1.0f, 1.0f, 1.0f);
    const bool skinOn = mesh.skinned && skinningFrameEnabled_;
    glUniform1i(uSkinningEnabled_, skinOn ? 1 : 0);
    glUniform1i(uExtendedSkinningEnabled_,
                (mesh.extendedSkinned && skinningFrameEnabled_) ? 1 : 0);
    glUniform1i(uBoneTexWidth_, boneTexWidth_ > 0 ? boneTexWidth_ : 4);
    glUniform1i(uBoneMatrixCount_, skinningFrameEnabled_ ? boneMatrixCount_ : 1);
    glUniform1i(uInfluenceTexWidth_,
                (mesh.extendedSkinned && skinningFrameEnabled_) ? mesh.influenceTexWidth : 0);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, boneTex_ ? boneTex_ : whiteTex_);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, mesh.influenceTex ? mesh.influenceTex : whiteTex_);

    if (mesh.doubleSided || wireframe) {
      glDisable(GL_CULL_FACE);
    } else {
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
    }

    glBindVertexArray(mesh.vao);
    for (const auto& sub : mesh.submeshes) {
      const GLMaterial& mat =
          (sub.materialId >= 0 && static_cast<size_t>(sub.materialId) < materials_.size())
              ? materials_[static_cast<size_t>(sub.materialId)]
              : kDefault;
      glUniform1i(uMatId_, sub.materialId);
      glUniform1i(uFaceBase_, static_cast<int>(sub.indexOffset / 3));
      // Resolve a material texture slot to a GPU texture; white if the slot is out
      // of range or not yet uploaded (lazy texture streaming).
      auto slotTex = [&](int slot) -> GLuint {
        if (slot >= 0 && static_cast<size_t>(slot) < textures_.size() &&
            textures_[static_cast<size_t>(slot)]) {
          return textures_[static_cast<size_t>(slot)];
        }
        return whiteTex_;
      };
      // Coarse displacement: applied identically in the normal and highlight passes
      // (the deformed positions must match) and forces geometric normals so shading
      // follows the displaced surface. Unit 7 always bound (white when disabled) to
      // keep the sampler complete.
      const bool displaced =
          params.displacement &&
          ((sub.materialId >= 0 &&
            static_cast<size_t>(sub.materialId) < materials_.size())
               ? materials_[static_cast<size_t>(sub.materialId)].hasDisplacement()
               : false);
      // GPU tessellation detail path: subdivide displaced triangles on the GPU and
      // displace each generated sample. Only in the Shaded view, for non-skinned
      // displaced meshes, when the slider asks for >1x. The tess program is restored
      // to the coarse program immediately after so following submeshes draw normally.
      if (displaced && tessAvailable_ && !overrideEmissive && !mesh.skinned &&
          params.maxTessLevel > 1 &&
          params.mode == RenderMode::Shaded) {
        const GLMaterial& dmat = materials_[static_cast<size_t>(sub.materialId)];
        glUseProgram(tessProgram_);
        glUniformMatrix4fv(tMVP_, 1, GL_FALSE, MVP.m);
        glUniformMatrix4fv(tModel_, 1, GL_FALSE, W.m);
        glUniformMatrix3fv(tNormalMat_, 1, GL_FALSE, nmat);
        glUniform3fv(tCameraPos_, 1, params.cameraPos);
        glUniform3fv(tBaseColor_, 1, dmat.baseColor);
        glUniform1i(tHasBaseColorTex_, dmat.baseColorTex >= 0 ? 1 : 0);
        glUniform1i(tHasDisplacementTex_, dmat.displacementTex >= 0 ? 1 : 0);
        glUniform1f(tDisplacementConst_, dmat.displacementConst);
        glUniform1f(tDisplacementScale_, params.displacementScale);
        glUniform1f(tMaxTessLevel_, static_cast<float>(params.maxTessLevel));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, slotTex(dmat.baseColorTex));
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, slotTex(dmat.displacementTex));
        glPatchParameteri(GL_PATCH_VERTICES, 3);
        glDrawElements(GL_PATCHES, static_cast<GLsizei>(sub.indexCount),
                       GL_UNSIGNED_INT,
                       (void*)(static_cast<uintptr_t>(sub.indexOffset) * sizeof(uint32_t)));
        glUseProgram(program_);  // restore coarse program for following submeshes
        continue;
      }
      glUniform1i(uHasDisplacement_, displaced ? 1 : 0);
      glActiveTexture(GL_TEXTURE7);
      if (displaced) {
        const GLMaterial& dmat = materials_[static_cast<size_t>(sub.materialId)];
        glUniform1i(uHasDisplacementTex_, dmat.displacementTex >= 0 ? 1 : 0);
        glUniform1f(uDisplacementConst_, dmat.displacementConst);
        glUniform1f(uDisplacementScale_, params.displacementScale);
        glBindTexture(GL_TEXTURE_2D, slotTex(dmat.displacementTex));
        glUniform1i(uGeometricNormal_, 1);
      } else {
        glBindTexture(GL_TEXTURE_2D, whiteTex_);
        glUniform1i(uGeometricNormal_, mesh.geometricNormal ? 1 : 0);
      }
      if (overrideEmissive) {
        glUniform3f(uBaseColor_, 0.f, 0.f, 0.f);
        glUniform1f(uMetallic_, 0.f);
        glUniform1f(uRoughness_, 1.f);
        glUniform3fv(uEmissive_, 1, overrideEmissive);
        glUniform1f(uAlpha_, 1.f);
        glUniform1i(uHasBaseColorTex_, 0);
        glUniform1i(uHasMetalRoughTex_, 0);
        glUniform1i(uHasNormalTex_, 0);
        glUniform1i(uHasEmissiveTex_, 0);
      } else {
        glUniform3fv(uBaseColor_, 1, mat.baseColor);
        glUniform1f(uMetallic_, mat.metallic);
        glUniform1f(uRoughness_, mat.roughness);
        glUniform3fv(uEmissive_, 1, mat.emissive);
        glUniform1f(uAlpha_, mat.alpha);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, slotTex(mat.baseColorTex));
        glUniform1i(uHasBaseColorTex_, mat.baseColorTex >= 0 ? 1 : 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, slotTex(mat.metalRoughTex));
        glUniform1i(uHasMetalRoughTex_, mat.metalRoughTex >= 0 ? 1 : 0);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, slotTex(mat.normalTex));
        glUniform1i(uHasNormalTex_, mat.normalTex >= 0 ? 1 : 0);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, slotTex(mat.emissiveTex));
        glUniform1i(uHasEmissiveTex_, mat.emissiveTex >= 0 ? 1 : 0);
      }
      glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sub.indexCount), GL_UNSIGNED_INT,
                     (void*)(static_cast<uintptr_t>(sub.indexOffset) * sizeof(uint32_t)));
    }
  }

  // Instanced pass: PointInstancer prototypes drawn with glDrawElementsInstanced.
  // Flat-shaded (default gray material, hardcoded headlight in the fragment
  // shader); each instance's model matrix comes from vertex attribs 6-9.
  bool anyInstanced = false;
  for (const GLMesh& m : meshes_) {
    if (m.instanceCount > 0) { anyInstanced = true; break; }
  }
  if (anyInstanced && instProgram_) {
    light3d::Mat4 VP = P * V;
    glUseProgram(instProgram_);
    glUniformMatrix4fv(iUViewProj_, 1, GL_FALSE, VP.m);
    glUniform3fv(iCameraPos_, 1, params.cameraPos);
    const float black[3] = {0.0f, 0.0f, 0.0f};
    glUniform3fv(iEmissive_, 1, overrideEmissive ? overrideEmissive : black);
    glUniform1i(iRenderMode_, static_cast<int>(params.mode));
    glUniform1f(iDepthScale_, params.depthScale > 1e-4f ? params.depthScale : 1.0f);
    glUniform3fv(iSceneMin_, 1, params.sceneMin);
    glUniform3fv(iSceneExtent_, 1, params.sceneExtent);
    for (size_t mi = 0; mi < meshes_.size(); ++mi) {
      if (params.meshVisible && mi < static_cast<size_t>(params.meshVisibleCount) &&
          !params.meshVisible[mi]) {
        continue;
      }
      const GLMesh& mesh = meshes_[mi];
      if (mesh.instanceCount <= 0 || mesh.drawInstanceCount <= 0) continue;
      glUniform1i(iMeshId_, static_cast<int>(mi));
      glUniform1i(iGeometricNormal_, mesh.geometricNormal ? 1 : 0);
      glUniform1i(iDoubleSided_, mesh.doubleSided ? 1 : 0);
      glUniform1i(iPurpose_, mesh.purposeId);
      glUniform1i(iKind_, mesh.kindId);
      if (mesh.doubleSided || wireframe) {
        glDisable(GL_CULL_FACE);
      } else {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
      }
      glBindVertexArray(mesh.vao);
      // Constant per-draw color when there is no per-instance color array (the
      // generic vertex-attribute value feeds aColor for every instance).
      if (!mesh.hasInstanceColors) glVertexAttrib3fv(9, mesh.flatColor);
      // Default per-vertex color to white when the prototype has none (the VAO
      // supplies attrib 10 from vertexColorVbo otherwise).
      if (!mesh.vertexColorVbo) glVertexAttrib3f(10, 1.0f, 1.0f, 1.0f);
      for (const auto& sub : mesh.submeshes) {
        glDrawElementsInstanced(
            GL_TRIANGLES, static_cast<GLsizei>(sub.indexCount), GL_UNSIGNED_INT,
            (void*)(static_cast<uintptr_t>(sub.indexOffset) * sizeof(uint32_t)),
            mesh.drawInstanceCount);
      }
    }
    glUseProgram(program_);  // restore for any caller that assumes program_
  }

  glBindVertexArray(0);
}

void GLRenderer::appendVolume(const DrawVolumeCPU& v) {
  if (v.density.empty() || v.dim[0] <= 0 || v.dim[1] <= 0 || v.dim[2] <= 0)
    return;

  GLVolume gv;
  std::memcpy(gv.world, v.world, sizeof(gv.world));
  light3d::Mat4 W;
  std::memcpy(W.m, v.world, sizeof(W.m));
  light3d::Mat4 inv = W.inverse();
  std::memcpy(gv.invWorld, inv.m, sizeof(gv.invWorld));
  for (int a = 0; a < 3; a++) {
    gv.bmin[a] = v.aabbMin[a];
    gv.bmax[a] = v.aabbMax[a];
    gv.albedo[a] = v.albedo[a];
    gv.emission[a] = v.emission[a];
  }
  gv.densityScale = v.densityScale;
  gv.background = v.background;

  glGenTextures(1, &gv.tex3d);
  glBindTexture(GL_TEXTURE_3D, gv.tex3d);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, v.dim[0], v.dim[1], v.dim[2], 0,
               GL_RED, GL_FLOAT, v.density.data());
  glBindTexture(GL_TEXTURE_3D, 0);

  volumes_.push_back(gv);
}

void GLRenderer::renderFrame(const RenderFrameParams& params) {
  if (!fbo_ || !program_ || !params.view || !params.proj) return;
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glViewport(0, 0, vpW_, vpH_);
  glClearColor(params.clearColor[0], params.clearColor[1], params.clearColor[2],
               params.clearColor[3]);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);

  glUseProgram(program_);
  glUniform3fv(uCameraPos_, 1, params.cameraPos);

  const bool wire = (params.mode == RenderMode::Wireframe);
  glPolygonMode(GL_FRONT_AND_BACK, wire ? GL_LINE : GL_FILL);
  drawMeshes(params, wire, nullptr);

  // Highlight overlay (wireframe, emissive orange) on the selected mesh.
  if (params.highlightMeshIndex >= 0 &&
      static_cast<size_t>(params.highlightMeshIndex) < meshes_.size() && !wire) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glPolygonOffset(-1.0f, -1.0f);
    const float orange[3] = {1.0f, 0.55f, 0.1f};
    const GLMesh& mesh = meshes_[static_cast<size_t>(params.highlightMeshIndex)];
    light3d::Mat4 P = ToMat4(params.proj), V = ToMat4(params.view), W = ToMat4(mesh.world);
    light3d::Mat4 MVP = P * V * W;
    glUniformMatrix4fv(uMVP_, 1, GL_FALSE, MVP.m);
    glUniformMatrix4fv(uModel_, 1, GL_FALSE, W.m);
    glUniform1i(uSkinningEnabled_, (mesh.skinned && skinningFrameEnabled_) ? 1 : 0);
    glUniform1i(uExtendedSkinningEnabled_,
                (mesh.extendedSkinned && skinningFrameEnabled_) ? 1 : 0);
    glUniform1i(uBoneTexWidth_, boneTexWidth_ > 0 ? boneTexWidth_ : 4);
    glUniform1i(uBoneMatrixCount_, skinningFrameEnabled_ ? boneMatrixCount_ : 1);
    glUniform1i(uInfluenceTexWidth_,
                (mesh.extendedSkinned && skinningFrameEnabled_) ? mesh.influenceTexWidth : 0);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, boneTex_ ? boneTex_ : whiteTex_);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, mesh.influenceTex ? mesh.influenceTex : whiteTex_);
    glUniform3f(uBaseColor_, 0, 0, 0);
    glUniform3fv(uEmissive_, 1, orange);
    glUniform1f(uAlpha_, 1.f);
    glUniform1i(uHasBaseColorTex_, 0);
    glUniform1i(uHasMetalRoughTex_, 0);
    glUniform1i(uHasNormalTex_, 0);
    glUniform1i(uHasEmissiveTex_, 0);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(mesh.vao);
    if (params.highlightIndices && params.highlightIndexCount > 0) {
      // Highlight only a selected GeomSubset's triangles: draw a dynamic index
      // buffer over the mesh's vertices. Restore the VAO's element buffer after.
      if (!highlightEbo_) glGenBuffers(1, &highlightEbo_);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, highlightEbo_);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(params.highlightIndexCount * sizeof(uint32_t)),
                   params.highlightIndices, GL_STREAM_DRAW);
      glDrawElements(GL_TRIANGLES, params.highlightIndexCount, GL_UNSIGNED_INT, nullptr);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);  // restore VAO state
    } else {
      for (const auto& sub : mesh.submeshes) {
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sub.indexCount), GL_UNSIGNED_INT,
                       (void*)(static_cast<uintptr_t>(sub.indexOffset) * sizeof(uint32_t)));
      }
    }
    glBindVertexArray(0);
    glDisable(GL_POLYGON_OFFSET_LINE);
  }

  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  // UsdVol volume raymarch pass (proxy-box; emission/absorption). Drawn after
  // opaque geometry with premultiplied-alpha "over" blending. Back faces only
  // (cull front) so each covered pixel marches once and the camera may be inside
  // the box. Depth-tested (no depth write) so opaque geometry in front occludes.
  if (volumeProgram_ && !volumes_.empty()) {
    const light3d::Mat4 VP = ToMat4(params.proj) * ToMat4(params.view);
    glUseProgram(volumeProgram_);
    glUniformMatrix4fv(uVolVP_, 1, GL_FALSE, VP.m);
    glUniform3fv(uVolCameraPos_, 1, params.cameraPos);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(uVolDensity_, 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);  // premultiplied "over"
    glDepthMask(GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    glBindVertexArray(volumeCubeVao_);
    for (const GLVolume& gv : volumes_) {
      glUniformMatrix4fv(uVolModel_, 1, GL_FALSE, gv.world);
      glUniformMatrix4fv(uVolInvModel_, 1, GL_FALSE, gv.invWorld);
      glUniform3fv(uVolBmin_, 1, gv.bmin);
      glUniform3fv(uVolBmax_, 1, gv.bmax);
      glUniform1f(uVolDensityScale_, gv.densityScale);
      glUniform3fv(uVolAlbedo_, 1, gv.albedo);
      glUniform3fv(uVolEmission_, 1, gv.emission);
      glUniform1f(uVolBackground_, gv.background);
      glBindTexture(GL_TEXTURE_3D, gv.tex3d);
      glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_3D, 0);

    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
  }

  // Debug helper lines (grid/axes/bbox), world space, depth-tested so they are
  // occluded by geometry.
  if (params.helperLines && params.helperLineVertexCount > 0 && lineProgram_) {
    glUseProgram(lineProgram_);
    const light3d::Mat4 VP = ToMat4(params.proj) * ToMat4(params.view);
    glUniformMatrix4fv(uLineVP_, 1, GL_FALSE, VP.m);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    const size_t bytes =
        static_cast<size_t>(params.helperLineVertexCount) * sizeof(HelperVertex);
    if (bytes > lineVboCap_) {
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), params.helperLines,
                   GL_DYNAMIC_DRAW);
      lineVboCap_ = bytes;
    } else {
      glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes),
                      params.helperLines);
    }
    glDisable(GL_CULL_FACE);
    glDrawArrays(GL_LINES, 0, params.helperLineVertexCount);
    glBindVertexArray(0);
  }

  // Overlay lines (skeleton bones): drawn on top with depth testing disabled so
  // they remain visible through the mesh (X-ray). Reuses the line program/VBO.
  if (params.overlayLines && params.overlayLineVertexCount > 0 && lineProgram_) {
    glUseProgram(lineProgram_);
    const light3d::Mat4 VP = ToMat4(params.proj) * ToMat4(params.view);
    glUniformMatrix4fv(uLineVP_, 1, GL_FALSE, VP.m);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    const size_t bytes =
        static_cast<size_t>(params.overlayLineVertexCount) * sizeof(HelperVertex);
    if (bytes > lineVboCap_) {
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), params.overlayLines,
                   GL_DYNAMIC_DRAW);
      lineVboCap_ = bytes;
    } else {
      glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes),
                      params.overlayLines);
    }
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_LINES, 0, params.overlayLineVertexCount);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
  }

  glUseProgram(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

ViewportTexHandle GLRenderer::viewportTexture() const {
  return static_cast<ViewportTexHandle>(colorTex_);
}

#if defined(TUSDVIEW_ENABLE_GL_THREAD)
// Shared composite: draw `drawData` to the default framebuffer + swap. Used by
// present() (live, main thread) and presentThreaded() (render thread).
void GLRenderer::presentImpl(ImDrawData* drawData, int fbw, int fbh) {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, fbw, fbh);
  glDisable(GL_DEPTH_TEST);
  glClearColor(0.06f, 0.06f, 0.07f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(drawData);
  if (wantWindowCapture_) {
    winCapW_ = fbw;
    winCapH_ = fbh;
    windowCapture_.resize(static_cast<size_t>(fbw) * static_cast<size_t>(fbh) * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, fbw, fbh, GL_RGBA, GL_UNSIGNED_BYTE, windowCapture_.data());
    wantWindowCapture_ = false;
  }
  glfwSwapBuffers(window_);
}

void GLRenderer::presentThreaded(ImDrawData* drawData, int fbW, int fbH) {
  presentImpl(drawData, fbW, fbH);  // fb size queried on the main thread
}

void GLRenderer::present() {
  int fbw = 0, fbh = 0;
  glfwGetFramebufferSize(window_, &fbw, &fbh);
  presentImpl(ImGui::GetDrawData(), fbw, fbh);
}
#else
void GLRenderer::present() {
  int fbw = 0, fbh = 0;
  glfwGetFramebufferSize(window_, &fbw, &fbh);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, fbw, fbh);
  glDisable(GL_DEPTH_TEST);
  glClearColor(0.06f, 0.06f, 0.07f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  // Grab the composited window from the back buffer before swapping (reliable
  // under headless/Xvfb, unlike reading GL_FRONT after the swap).
  if (wantWindowCapture_) {
    winCapW_ = fbw;
    winCapH_ = fbh;
    windowCapture_.resize(static_cast<size_t>(fbw) * static_cast<size_t>(fbh) * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, fbw, fbh, GL_RGBA, GL_UNSIGNED_BYTE, windowCapture_.data());
    wantWindowCapture_ = false;
  }

  glfwSwapBuffers(window_);
}
#endif

bool GLRenderer::captureViewport(std::vector<uint8_t>* rgba, int* w, int* h) {
  if (!fbo_ || vpW_ < 1 || vpH_ < 1) return false;
  *w = vpW_;
  *h = vpH_;
  const size_t rowBytes = static_cast<size_t>(vpW_) * 4;
  std::vector<uint8_t> tmp(rowBytes * static_cast<size_t>(vpH_));
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, vpW_, vpH_, GL_RGBA, GL_UNSIGNED_BYTE, tmp.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  // GL is bottom-up; return top-down rows (consistent with the Vulkan backend).
  rgba->resize(tmp.size());
  for (int y = 0; y < vpH_; ++y) {
    std::memcpy(&(*rgba)[static_cast<size_t>(y) * rowBytes],
                &tmp[static_cast<size_t>(vpH_ - 1 - y) * rowBytes], rowBytes);
  }
  return true;
}

bool GLRenderer::captureWindow(std::vector<uint8_t>* rgba, int* w, int* h) {
  if (windowCapture_.empty() || winCapW_ < 1 || winCapH_ < 1) return false;
  *w = winCapW_;
  *h = winCapH_;
  const size_t rowBytes = static_cast<size_t>(winCapW_) * 4;
  rgba->resize(windowCapture_.size());
  // Stored bottom-up; emit top-down.
  for (int y = 0; y < winCapH_; ++y) {
    std::memcpy(&(*rgba)[static_cast<size_t>(y) * rowBytes],
                &windowCapture_[static_cast<size_t>(winCapH_ - 1 - y) * rowBytes],
                rowBytes);
  }
  return true;
}

void GLRenderer::shutdown() {
  destroyScene();
  if (whiteTex_) { glDeleteTextures(1, &whiteTex_); whiteTex_ = 0; }
  if (boneTex_) { glDeleteTextures(1, &boneTex_); boneTex_ = 0; }
  if (colorTex_) { glDeleteTextures(1, &colorTex_); colorTex_ = 0; }
  if (depthRbo_) { glDeleteRenderbuffers(1, &depthRbo_); depthRbo_ = 0; }
  if (fbo_) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
  if (program_) { glDeleteProgram(program_); program_ = 0; }
  if (tessProgram_) { glDeleteProgram(tessProgram_); tessProgram_ = 0; }
  if (instProgram_) { glDeleteProgram(instProgram_); instProgram_ = 0; }
  if (lineProgram_) { glDeleteProgram(lineProgram_); lineProgram_ = 0; }
  if (lineVbo_) { glDeleteBuffers(1, &lineVbo_); lineVbo_ = 0; }
  if (lineVao_) { glDeleteVertexArrays(1, &lineVao_); lineVao_ = 0; }
  for (GLVolume& gv : volumes_) {
    if (gv.tex3d) glDeleteTextures(1, &gv.tex3d);
  }
  volumes_.clear();
  if (volumeProgram_) { glDeleteProgram(volumeProgram_); volumeProgram_ = 0; }
  if (volumeCubeVbo_) { glDeleteBuffers(1, &volumeCubeVbo_); volumeCubeVbo_ = 0; }
  if (volumeCubeEbo_) { glDeleteBuffers(1, &volumeCubeEbo_); volumeCubeEbo_ = 0; }
  if (volumeCubeVao_) { glDeleteVertexArrays(1, &volumeCubeVao_); volumeCubeVao_ = 0; }
  if (highlightEbo_) { glDeleteBuffers(1, &highlightEbo_); highlightEbo_ = 0; }
  if (imguiInited_) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    imguiInited_ = false;
  }
}

std::unique_ptr<Renderer> CreateGLRenderer() {
  return std::unique_ptr<Renderer>(new GLRenderer());
}

}  // namespace tusdview
