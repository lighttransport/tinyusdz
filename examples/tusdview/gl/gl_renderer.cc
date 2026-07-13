// SPDX-License-Identifier: Apache-2.0
#include "gl/gl_renderer.hh"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "gl/gl_util.hh"
#include "lod_math.hh"  // UnitCubeCorner, kBoxIndices (LOD box proxies)
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

void SetUvUniform(GLint loc0, GLint loc1, const DrawUvXformCPU& uv) {
  glUniform3f(loc0, uv.m00, uv.m01, uv.tx);
  glUniform3f(loc1, uv.m10, uv.m11, uv.ty);
}

// Compressed-format enum values that may be absent from older glad headers.
#ifndef GL_COMPRESSED_RG_RGTC2
#define GL_COMPRESSED_RG_RGTC2 0x8DBD
#endif
#ifndef GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT
#define GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT 0x8E8F
#endif
#ifndef GL_COMPRESSED_RGB8_ETC2
#define GL_COMPRESSED_RGB8_ETC2 0x9274
#endif
#ifndef GL_COMPRESSED_SRGB8_ETC2
#define GL_COMPRESSED_SRGB8_ETC2 0x9275
#endif
#ifndef GL_COMPRESSED_RGBA8_ETC2_EAC
#define GL_COMPRESSED_RGBA8_ETC2_EAC 0x9278
#endif
#ifndef GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC
#define GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC 0x9279
#endif
#ifndef GL_COMPRESSED_RGBA_ASTC_4x4_KHR
#define GL_COMPRESSED_RGBA_ASTC_4x4_KHR 0x93B0
#endif
#ifndef GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR
#define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR 0x93D0
#endif

GLenum GLCompressedFormat(DrawCompressedFormat format, bool srgb) {
  switch (format) {
    case DrawCompressedFormat::BC1:
      return srgb ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT
                  : GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    case DrawCompressedFormat::BC3:
      return srgb ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
                  : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    case DrawCompressedFormat::BC5:
      // GL_ARB_texture_compression_rgtc (core since GL 3.0). No sRGB variant.
      return GL_COMPRESSED_RG_RGTC2;
    case DrawCompressedFormat::BC6H:
      // GL_ARB_texture_compression_bptc float (HDR). No sRGB variant.
      return GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT;
    case DrawCompressedFormat::BC7:
      // GL_ARB_texture_compression_bptc (core since GL 4.2).
      return srgb ? GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM
                  : GL_COMPRESSED_RGBA_BPTC_UNORM;
    case DrawCompressedFormat::ETC2_RGB:
      return srgb ? GL_COMPRESSED_SRGB8_ETC2 : GL_COMPRESSED_RGB8_ETC2;
    case DrawCompressedFormat::ETC2_RGBA:
      return srgb ? GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC
                  : GL_COMPRESSED_RGBA8_ETC2_EAC;
    case DrawCompressedFormat::ASTC_4x4:
      return srgb ? GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR
                  : GL_COMPRESSED_RGBA_ASTC_4x4_KHR;
    case DrawCompressedFormat::None:
    default:
      return 0;
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

  // Compressed-texture format support (extension strings + core versions).
  {
    std::string exts;
    GLint next = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &next);
    exts.reserve(static_cast<size_t>(next) * 24);
    for (GLint i = 0; i < next; ++i) {
      const GLubyte* e = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i));
      if (e) { exts += reinterpret_cast<const char*>(e); exts += ' '; }
    }
    auto has = [&](const char* s) { return exts.find(s) != std::string::npos; };
    const bool bptc = has("texture_compression_bptc") || GLAD_GL_VERSION_4_2;
    caps_.supportsBC = has("GL_EXT_texture_compression_s3tc") || bptc;  // BC1/3/7
    caps_.supportsBC5 = has("texture_compression_rgtc") || GLAD_GL_VERSION_3_0;
    caps_.supportsBC6H = bptc;
    caps_.supportsASTC = has("GL_KHR_texture_compression_astc_ldr");
    caps_.supportsETC2 = has("GL_ARB_ES3_compatibility") || GLAD_GL_VERSION_4_3;
  }

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
  uLightDir_ = glGetUniformLocation(program_, "uLightDir");
  uLightColor_ = glGetUniformLocation(program_, "uLightColor");
  uHasIbl_ = glGetUniformLocation(program_, "uHasIbl");
  uIblColor_ = glGetUniformLocation(program_, "uIblColor");
  uEnvRotation_ = glGetUniformLocation(program_, "uEnvRotation");
  uPrefilteredLods_ = glGetUniformLocation(program_, "uPrefilteredLods");
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
  uAlphaMode_ = glGetUniformLocation(program_, "uAlphaMode");
  uAlphaCutoff_ = glGetUniformLocation(program_, "uAlphaCutoff");
  uHasBaseColorTex_ = glGetUniformLocation(program_, "uHasBaseColorTex");
  uHasMetalRoughTex_ = glGetUniformLocation(program_, "uHasMetalRoughTex");
  uHasNormalTex_ = glGetUniformLocation(program_, "uHasNormalTex");
  uHasEmissiveTex_ = glGetUniformLocation(program_, "uHasEmissiveTex");
  uBaseColorTexIsUdim_ = glGetUniformLocation(program_, "uBaseColorTexIsUdim");
  uMetalRoughTexIsUdim_ = glGetUniformLocation(program_, "uMetalRoughTexIsUdim");
  uNormalTexIsUdim_ = glGetUniformLocation(program_, "uNormalTexIsUdim");
  uEmissiveTexIsUdim_ = glGetUniformLocation(program_, "uEmissiveTexIsUdim");
  uBaseColorUv0_ = glGetUniformLocation(program_, "uBaseColorUv0");
  uBaseColorUv1_ = glGetUniformLocation(program_, "uBaseColorUv1");
  uMetalRoughUv0_ = glGetUniformLocation(program_, "uMetalRoughUv0");
  uMetalRoughUv1_ = glGetUniformLocation(program_, "uMetalRoughUv1");
  uNormalUv0_ = glGetUniformLocation(program_, "uNormalUv0");
  uNormalUv1_ = glGetUniformLocation(program_, "uNormalUv1");
  uEmissiveUv0_ = glGetUniformLocation(program_, "uEmissiveUv0");
  uEmissiveUv1_ = glGetUniformLocation(program_, "uEmissiveUv1");
  uUvSet_ = glGetUniformLocation(program_, "uUvSet");
  uBaseColorTexScale_ = glGetUniformLocation(program_, "uBaseColorTexScale");
  uBaseColorTexBias_ = glGetUniformLocation(program_, "uBaseColorTexBias");
  uNormalTexScale_ = glGetUniformLocation(program_, "uNormalTexScale");
  uNormalTexBias_ = glGetUniformLocation(program_, "uNormalTexBias");
  uEmissiveTexScale_ = glGetUniformLocation(program_, "uEmissiveTexScale");
  uEmissiveTexBias_ = glGetUniformLocation(program_, "uEmissiveTexBias");
  uMetallicChannel_ = glGetUniformLocation(program_, "uMetallicChannel");
  uRoughnessChannel_ = glGetUniformLocation(program_, "uRoughnessChannel");
  uMetallicTexScale_ = glGetUniformLocation(program_, "uMetallicTexScale");
  uMetallicTexBias_ = glGetUniformLocation(program_, "uMetallicTexBias");
  uRoughnessTexScale_ = glGetUniformLocation(program_, "uRoughnessTexScale");
  uRoughnessTexBias_ = glGetUniformLocation(program_, "uRoughnessTexBias");
  uHasDisplacement_ = glGetUniformLocation(program_, "uHasDisplacement");
  uHasDisplacementTex_ = glGetUniformLocation(program_, "uHasDisplacementTex");
  uDisplacementConst_ = glGetUniformLocation(program_, "uDisplacementConst");
  uDisplacementScale_ = glGetUniformLocation(program_, "uDisplacementScale");
  uDisplacementTexScale_ = glGetUniformLocation(program_, "uDisplacementTexScale");
  uDisplacementTexBias_ = glGetUniformLocation(program_, "uDisplacementTexBias");
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
  glUniform1i(glGetUniformLocation(program_, "uBaseColorUdimTex"), 11);
  glUniform1i(glGetUniformLocation(program_, "uBaseColorUdimLut"), 12);
  glUniform1i(glGetUniformLocation(program_, "uMetalRoughUdimTex"), 13);
  glUniform1i(glGetUniformLocation(program_, "uMetalRoughUdimLut"), 14);
  glUniform1i(glGetUniformLocation(program_, "uNormalUdimTex"), 15);
  glUniform1i(glGetUniformLocation(program_, "uNormalUdimLut"), 16);
  glUniform1i(glGetUniformLocation(program_, "uEmissiveUdimTex"), 17);
  glUniform1i(glGetUniformLocation(program_, "uEmissiveUdimLut"), 18);
  glUniform1i(glGetUniformLocation(program_, "uBoneTex"), 4);
  glUniform1i(glGetUniformLocation(program_, "uInfluenceTex"), 5);
  glUniform1i(uFaceIdTex_, 6);  // source-face-id texture buffer
  glUniform1i(glGetUniformLocation(program_, "uDisplacementTex"), 7);
  glUniform1i(glGetUniformLocation(program_, "uMorphDeltaTex"), 8);  // GPU morph
  glUniform1i(glGetUniformLocation(program_, "uMorphCoeffTex"), 9);
  glUniform1i(glGetUniformLocation(program_, "uMorphChanTex"), 10);  // skip pre-check
  glUniform1i(glGetUniformLocation(program_, "uIrradianceMap"), 19);  // dome IBL
  glUniform1i(glGetUniformLocation(program_, "uPrefilteredMap"), 20);
  glUniform1i(glGetUniformLocation(program_, "uBrdfLut"), 21);
  // Filter across cube-face borders (core since GL 3.2); matters for the
  // low-res prefiltered/irradiance cubes.
  glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
  uHasMorph_ = glGetUniformLocation(program_, "uHasMorph");
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
      // The non-instanced program's skin slots (3/4) are taken by these rows, so
      // instanced skinning carries its joints/weights at 6/7 instead (attrib 8 is
      // the morph CSR). All instances of a prototype share ONE bone block: USD
      // instancing requires identical composed contents, hence one skeleton+pose.
      "layout(location=3) in vec4 aRow0;\n"
      "layout(location=4) in vec4 aRow1;\n"
      "layout(location=5) in vec4 aRow2;\n"
      "layout(location=6) in uvec4 aJoint;\n"
      "layout(location=7) in vec4 aWeight;\n"
      "layout(location=9) in vec3 aColor;\n"     // per-instance color or constant
      "layout(location=10) in vec3 aVtxColor;\n"  // per-vertex prototype color (or 1)
      "layout(location=11) in float aOpacity;\n"  // per-instance opacity or constant
      // GPU blendshape morph (shared by instanced prototypes): per-vertex CSR
      // (offset,count) + delta/coeff/channelId texture-buffers, summed into the
      // local position before the per-instance transform. Same scheme + units
      // (8/9/10) as the non-instanced material shader.
      "layout(location=8) in uvec2 aMorphOffsetCount;\n"
      "uniform mat4 uViewProj;\n"
      "uniform bool uHasMorph;\n"
      "uniform samplerBuffer uMorphDeltaTex;\n"
      "uniform samplerBuffer uMorphCoeffTex;\n"
      "uniform usamplerBuffer uMorphChanTex;\n"
      // Skeletal skinning, shared with the non-instanced program: the same 4xN
      // RGBA32F bone texture (unit 4), absolute joint rows. Skinning happens in
      // PROTOTYPE-LOCAL space, before the per-instance transform.
      "uniform bool uSkinningEnabled;\n"
      "uniform sampler2D uBoneTex;\n"
      "uniform int uBoneTexWidth;\n"
      "uniform int uBoneMatrixCount;\n"
      "mat4 fetchBone(uint idx){\n"
      "  int base=int(idx)*4;\n"
      "  return mat4(\n"
      "    texelFetch(uBoneTex,ivec2((base+0)%uBoneTexWidth,(base+0)/uBoneTexWidth),0),\n"
      "    texelFetch(uBoneTex,ivec2((base+1)%uBoneTexWidth,(base+1)/uBoneTexWidth),0),\n"
      "    texelFetch(uBoneTex,ivec2((base+2)%uBoneTexWidth,(base+2)/uBoneTexWidth),0),\n"
      "    texelFetch(uBoneTex,ivec2((base+3)%uBoneTexWidth,(base+3)/uBoneTexWidth),0));\n"
      "}\n"
      "out vec3 vWorldPos;\n"
      "out vec3 vNormal;\n"
      "out vec3 vColor;\n"
      "out float vOpacity;\n"
      "flat out int vInstanceId;\n"
      "void main(){\n"
      "  vInstanceId = gl_InstanceID;\n"
      "  vec3 pos = aPosition;\n"
      "  if (uHasMorph && aMorphOffsetCount.y > 0u) {\n"
      "    int mbase = int(aMorphOffsetCount.x);\n"
      "    int mcount = min(int(aMorphOffsetCount.y), 256);\n"
      "    for (int i = 0; i < 256; ++i) {\n"
      "      if (i >= mcount) break;\n"
      "      int ch = int(texelFetch(uMorphChanTex, mbase + i).r);\n"
      "      float c = texelFetch(uMorphCoeffTex, ch).r;\n"
      "      if (abs(c) < 1e-6) continue;\n"
      "      pos += c * texelFetch(uMorphDeltaTex, mbase + i).yzw;\n"
      "    }\n"
      "  }\n"
      // Linear-blend skinning (prototype-local). Vertices of an unskinned mesh --
      // or of an unskinned vertex in a skinned one -- carry zero weights and pass
      // through untouched, exactly as in the non-instanced program.
      "  vec3 nrm = aNormal;\n"
      "  float wsum = aWeight.x + aWeight.y + aWeight.z + aWeight.w;\n"
      "  uint maxJoint = max(max(aJoint.x, aJoint.y), max(aJoint.z, aJoint.w));\n"
      "  if (uSkinningEnabled && wsum > 0.0 && int(maxJoint) < uBoneMatrixCount) {\n"
      "    mat4 skin = fetchBone(aJoint.x) * aWeight.x + fetchBone(aJoint.y) * aWeight.y\n"
      "              + fetchBone(aJoint.z) * aWeight.z + fetchBone(aJoint.w) * aWeight.w;\n"
      "    pos = (skin * vec4(pos, 1.0)).xyz;\n"
      "    nrm = normalize((skin * vec4(aNormal, 0.0)).xyz);\n"
      "  }\n"
      "  vec4 p = vec4(pos, 1.0);\n"
      "  vec3 wp = vec3(dot(p, aRow0), dot(p, aRow1), dot(p, aRow2));\n"
      "  vec3 n = vec3(dot(nrm, aRow0.xyz), dot(nrm, aRow1.xyz),\n"
      "                dot(nrm, aRow2.xyz));\n"
      "  vWorldPos = wp;\n"
      "  vNormal = normalize(n);\n"
      // Prototype per-vertex displayColor x per-instance color (both default 1).
      "  vColor = aColor * aVtxColor;\n"
      "  vOpacity = clamp(aOpacity, 0.0, 1.0);\n"
      "  gl_Position = uViewProj * vec4(wp, 1.0);\n"
      "}\n";
  static const char* kInstancedFS =
      "#version 330 core\n"
      "in vec3 vWorldPos;\n"
      "in vec3 vNormal;\n"
      "in vec3 vColor;\n"
      "in float vOpacity;\n"
      "flat in int vInstanceId;\n"
      "uniform vec3 uCameraPos;\n"
      "uniform vec3 uLightDir;\n"
      "uniform vec3 uLightColor;\n"
      // DomeLight IBL (diffuse-only here: prototypes carry no material scalars).
      "uniform bool uHasIbl;\n"
      "uniform vec3 uIblColor;\n"
      "uniform mat3 uEnvRotation;\n"
      "uniform samplerCube uIrradianceMap;\n"
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
      // Linear -> sRGB OETF for the final shaded output (see material.cpp):
      // scene lit in linear, FBO is RGBA8. Lit path only.
      "vec3 linearToSrgb(vec3 c){\n"
      "  c = clamp(c, 0.0, 1.0);\n"
      "  vec3 lo = c * 12.92;\n"
      "  vec3 hi = 1.055 * pow(c, vec3(1.0/2.4)) - 0.055;\n"
      "  return mix(lo, hi, vec3(greaterThan(c, vec3(0.0031308))));\n"
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
      // Soft camera-headlight: face the normal to the camera, combine N.V with a
      // gentle half-Lambert key + ambient floor so no facet renders pure black.
      "  vec3 V = normalize(uCameraPos - vWorldPos);\n"
      "  vec3 Nf = (dot(N, V) < 0.0) ? -N : N;\n"
      "  float facing = max(dot(Nf, V), 0.0);\n"
      "  vec3 L = (dot(uLightDir,uLightDir)>1e-8) ? normalize(uLightDir) : normalize(vec3(0.3,0.5,0.8));\n"
      "  vec3 lightColor = (dot(uLightColor,uLightColor)>1e-8) ? uLightColor : vec3(1.0);\n"
      "  float key = dot(Nf, L) * 0.5 + 0.5;\n"
      "  float shade = 0.25 + 0.75 * (0.6 * facing + 0.4 * key);\n"
      "  vec3 H = normalize(L + V);\n"
      "  float NdotH = max(dot(Nf, H), 0.0);\n"
      "  vec3 amb = uHasIbl ? texture(uIrradianceMap, normalize(uEnvRotation * Nf)).rgb * uIblColor : vec3(0.25);\n"
      "  vec3 col = vColor * (amb + lightColor * (shade - 0.25)) + lightColor * 0.12 * pow(NdotH, 32.0) * facing;\n"
      "  FragColor = vec4(linearToSrgb(col + uEmissive), vOpacity);\n"
      "}\n";
  instProgram_ = glutil::CompileProgram(kInstancedVS, kInstancedFS, err);
  if (!instProgram_) {
    if (err && err->empty()) *err = "Failed to build GL instanced program";
    return false;
  }
  glUseProgram(instProgram_);
  iUViewProj_ = glGetUniformLocation(instProgram_, "uViewProj");
  iCameraPos_ = glGetUniformLocation(instProgram_, "uCameraPos");
  iLightDir_ = glGetUniformLocation(instProgram_, "uLightDir");
  iHasIbl_ = glGetUniformLocation(instProgram_, "uHasIbl");
  iIblColor_ = glGetUniformLocation(instProgram_, "uIblColor");
  iEnvRotation_ = glGetUniformLocation(instProgram_, "uEnvRotation");
  iLightColor_ = glGetUniformLocation(instProgram_, "uLightColor");
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
  iHasMorph_ = glGetUniformLocation(instProgram_, "uHasMorph");
  iSkinningEnabled_ = glGetUniformLocation(instProgram_, "uSkinningEnabled");
  iBoneTexWidth_ = glGetUniformLocation(instProgram_, "uBoneTexWidth");
  iBoneMatrixCount_ = glGetUniformLocation(instProgram_, "uBoneMatrixCount");
  glUniform1i(glGetUniformLocation(instProgram_, "uBoneTex"), 4);  // same unit as mesh
  glUniform1i(glGetUniformLocation(instProgram_, "uMorphDeltaTex"), 8);
  glUniform1i(glGetUniformLocation(instProgram_, "uMorphCoeffTex"), 9);
  glUniform1i(glGetUniformLocation(instProgram_, "uMorphChanTex"), 10);
  glUniform1i(glGetUniformLocation(instProgram_, "uIrradianceMap"), 19);
  glUseProgram(0);

  buildWireProgram();  // flat polygon-edge wireframe (best-effort; non-fatal)

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
  initBoxProxy();
  return true;
}

// Shared unit-cube proxy mesh drawn with the instanced program (raster LOD). Static
// geometry (8 verts / 36 indices) + dynamic per-instance box-fit o2w (attribs 3-5)
// and tint (attrib 9), filled each frame by updateProxyInstances.
void GLRenderer::initBoxProxy() {
  float verts[8 * 3];
  for (int c = 0; c < 8; ++c) UnitCubeCorner(c, &verts[c * 3]);
  glGenVertexArrays(1, &boxProxyVao_);
  glBindVertexArray(boxProxyVao_);
  glGenBuffers(1, &boxProxyVbo_);
  glBindBuffer(GL_ARRAY_BUFFER, boxProxyVbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
  glVertexAttribDivisor(0, 0);
  glGenBuffers(1, &boxProxyEbo_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, boxProxyEbo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kBoxIndices), kBoxIndices,
               GL_STATIC_DRAW);
  // Per-instance box-fit o2w rows (3-5, divisor 1) -- dynamic.
  glGenBuffers(1, &boxProxyInstVbo_);
  glBindBuffer(GL_ARRAY_BUFFER, boxProxyInstVbo_);
  const GLsizei mstride = 12 * sizeof(float);
  for (int r = 0; r < 3; ++r) {
    const GLuint loc = 3 + r;
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, mstride,
                          (void*)(static_cast<uintptr_t>(r * 4 * sizeof(float))));
    glVertexAttribDivisor(loc, 1);
  }
  // Per-instance tint (9, divisor 1) -- dynamic.
  glGenBuffers(1, &boxProxyColorVbo_);
  glBindBuffer(GL_ARRAY_BUFFER, boxProxyColorVbo_);
  glEnableVertexAttribArray(9);
  glVertexAttribPointer(9, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
  glVertexAttribDivisor(9, 1);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void GLRenderer::updateProxyInstances(const float* xforms, const float* tints,
                                      uint32_t count) {
  boxProxyCount_ = count;
  if (count == 0) return;
  const size_t xb = static_cast<size_t>(count) * 12 * sizeof(float);
  const size_t cb = static_cast<size_t>(count) * 3 * sizeof(float);
  const bool grow = static_cast<size_t>(count) > boxProxyInstCap_;
  glBindBuffer(GL_ARRAY_BUFFER, boxProxyInstVbo_);
  if (grow) glBufferData(GL_ARRAY_BUFFER, xb, xforms, GL_DYNAMIC_DRAW);
  else glBufferSubData(GL_ARRAY_BUFFER, 0, xb, xforms);
  glBindBuffer(GL_ARRAY_BUFFER, boxProxyColorVbo_);
  if (grow) glBufferData(GL_ARRAY_BUFFER, cb, tints, GL_DYNAMIC_DRAW);
  else glBufferSubData(GL_ARRAY_BUFFER, 0, cb, tints);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  if (grow) boxProxyInstCap_ = count;
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
      "layout(location=3) in uvec4 aJoint;\n"
      "layout(location=4) in vec4 aWeight;\n"
      "layout(location=5) in uvec2 aInfluence;\n"
      "layout(location=8) in uvec2 aMorphOffsetCount;\n"
      // GPU blendshape morph (active-channel skip) + linear-blend skinning, so the
      // tessellator subdivides the DEFORMED control mesh. Mirrors the coarse GL330
      // vertex stage (morph before skin).
      "uniform bool uHasMorph;\n"
      "uniform samplerBuffer uMorphDeltaTex;\n"
      "uniform samplerBuffer uMorphCoeffTex;\n"
      "uniform usamplerBuffer uMorphChanTex;\n"
      "uniform sampler2D uBoneTex;\n"
      "uniform sampler2D uInfluenceTex;\n"
      "uniform bool uSkinningEnabled;\n"
      "uniform bool uExtendedSkinningEnabled;\n"
      "uniform int uBoneTexWidth;\n"
      "uniform int uBoneMatrixCount;\n"
      "uniform int uInfluenceTexWidth;\n"
      "out vec3 vcPos; out vec3 vcNrm; out vec2 vcUV;\n"
      "mat4 fetchBone(uint idx){\n"
      "  int base=int(idx)*4;\n"
      "  return mat4(\n"
      "    texelFetch(uBoneTex,ivec2((base+0)%uBoneTexWidth,(base+0)/uBoneTexWidth),0),\n"
      "    texelFetch(uBoneTex,ivec2((base+1)%uBoneTexWidth,(base+1)/uBoneTexWidth),0),\n"
      "    texelFetch(uBoneTex,ivec2((base+2)%uBoneTexWidth,(base+2)/uBoneTexWidth),0),\n"
      "    texelFetch(uBoneTex,ivec2((base+3)%uBoneTexWidth,(base+3)/uBoneTexWidth),0));\n"
      "}\n"
      "void main(){\n"
      "  vec3 pos=aPosition; vec3 nrm=aNormal;\n"
      "  if(uHasMorph && aMorphOffsetCount.y>0u){\n"
      "    int mbase=int(aMorphOffsetCount.x);\n"
      "    int mcount=min(int(aMorphOffsetCount.y),256);\n"
      "    for(int i=0;i<256;++i){\n"
      "      if(i>=mcount) break;\n"
      "      int ch=int(texelFetch(uMorphChanTex,mbase+i).r);\n"
      "      float c=texelFetch(uMorphCoeffTex,ch).r;\n"
      "      if(abs(c)<1e-6) continue;\n"
      "      pos += c*texelFetch(uMorphDeltaTex,mbase+i).yzw;\n"
      "    }\n"
      "  }\n"
      "  float wsum=aWeight.x+aWeight.y+aWeight.z+aWeight.w;\n"
      "  uint maxJoint=max(max(aJoint.x,aJoint.y),max(aJoint.z,aJoint.w));\n"
      "  if(uSkinningEnabled && uExtendedSkinningEnabled && aInfluence.y>0u && uInfluenceTexWidth>0){\n"
      "    mat4 skin=mat4(0.0); float fws=0.0;\n"
      "    int base=int(aInfluence.x); int count=min(int(aInfluence.y),256);\n"
      "    for(int i=0;i<256;++i){\n"
      "      if(i>=count) break;\n"
      "      int linear=base+i;\n"
      "      vec4 iw=texelFetch(uInfluenceTex,ivec2(linear%uInfluenceTexWidth,linear/uInfluenceTexWidth),0);\n"
      "      uint joint=uint(iw.x+0.5); float w=iw.y;\n"
      "      if(w>0.0 && int(joint)<uBoneMatrixCount){ skin+=fetchBone(joint)*w; fws+=w; }\n"
      "    }\n"
      "    if(fws>0.0){ skin*=1.0/fws; pos=(skin*vec4(pos,1.0)).xyz; nrm=normalize((skin*vec4(aNormal,0.0)).xyz); }\n"
      "  } else if(uSkinningEnabled && wsum>0.0 && int(maxJoint)<uBoneMatrixCount){\n"
      "    mat4 skin=fetchBone(aJoint.x)*aWeight.x+fetchBone(aJoint.y)*aWeight.y+fetchBone(aJoint.z)*aWeight.z+fetchBone(aJoint.w)*aWeight.w;\n"
      "    pos=(skin*vec4(pos,1.0)).xyz; nrm=normalize((skin*vec4(aNormal,0.0)).xyz);\n"
      "  }\n"
      "  vcPos=pos; vcNrm=nrm; vcUV=aUV.xy;\n"
      "}\n";
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
      "uniform float uDisplacementTexScale; uniform float uDisplacementTexBias;\n"
      "void main(){\n"
      "  vec3 bc=gl_TessCoord;\n"
      "  vec3 pos=bc.x*tcPos[0]+bc.y*tcPos[1]+bc.z*tcPos[2];\n"
      "  vec3 nrm=normalize(bc.x*tcNrm[0]+bc.y*tcNrm[1]+bc.z*tcNrm[2]);\n"
      "  vec2 uv=bc.x*tcUV[0]+bc.y*tcUV[1]+bc.z*tcUV[2];\n"
      "  float d=uHasDisplacementTex? (textureLod(uDisplacementTex,uv,0.0).r*uDisplacementTexScale+uDisplacementTexBias) : uDisplacementConst;\n"
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
      "uniform vec3 uLightDir; uniform vec3 uLightColor;\n"
      "uniform bool uHasIbl; uniform vec3 uIblColor; uniform mat3 uEnvRotation;\n"
      "uniform samplerCube uIrradianceMap;\n"
      "uniform sampler2D uBaseColorTex; uniform bool uHasBaseColorTex;\n"
      "out vec4 FragColor;\n"
      "void main(){\n"
      "  vec3 base=uBaseColor;\n"
      "  if(uHasBaseColorTex) base*=texture(uBaseColorTex,vUV).rgb;\n"
      // Geometric normal of the displaced surface (screen derivatives) so the new
      // height detail actually shades, matching the coarse path's displaced look.
      "  vec3 N=normalize(cross(dFdx(vWorldPos),dFdy(vWorldPos)));\n"
      "  if(!gl_FrontFacing) N=-N;\n"
      // Soft camera-headlight (matches the coarse/material path): N.V + half-Lambert
      // key + ambient floor so no facet renders pure black.
      "  vec3 V=normalize(uCameraPos-vWorldPos);\n"
      "  vec3 Nf=(dot(N,V)<0.0)?-N:N;\n"
      "  float facing=max(dot(Nf,V),0.0);\n"
      "  vec3 L=(dot(uLightDir,uLightDir)>1e-8)?normalize(uLightDir):normalize(vec3(0.3,0.5,0.8));\n"
      "  vec3 lightColor=(dot(uLightColor,uLightColor)>1e-8)?uLightColor:vec3(1.0);\n"
      "  float key=dot(Nf,L)*0.5+0.5;\n"
      "  float shade=0.25+0.75*(0.6*facing+0.4*key);\n"
      "  vec3 H=normalize(L+V); float NdotH=max(dot(Nf,H),0.0);\n"
      "  vec3 amb=uHasIbl?texture(uIrradianceMap,normalize(uEnvRotation*Nf)).rgb*uIblColor:vec3(0.25);\n"
      "  vec3 col=base*(amb+lightColor*(shade-0.25))+lightColor*0.12*pow(NdotH,32.0)*facing;\n"
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
  tLightDir_ = glGetUniformLocation(tessProgram_, "uLightDir");
  tLightColor_ = glGetUniformLocation(tessProgram_, "uLightColor");
  tHasIbl_ = glGetUniformLocation(tessProgram_, "uHasIbl");
  tIblColor_ = glGetUniformLocation(tessProgram_, "uIblColor");
  tEnvRotation_ = glGetUniformLocation(tessProgram_, "uEnvRotation");
  tBaseColor_ = glGetUniformLocation(tessProgram_, "uBaseColor");
  tHasBaseColorTex_ = glGetUniformLocation(tessProgram_, "uHasBaseColorTex");
  tHasDisplacementTex_ = glGetUniformLocation(tessProgram_, "uHasDisplacementTex");
  tDisplacementConst_ = glGetUniformLocation(tessProgram_, "uDisplacementConst");
  tDisplacementScale_ = glGetUniformLocation(tessProgram_, "uDisplacementScale");
  tDisplacementTexScale_ = glGetUniformLocation(tessProgram_, "uDisplacementTexScale");
  tDisplacementTexBias_ = glGetUniformLocation(tessProgram_, "uDisplacementTexBias");
  tMaxTessLevel_ = glGetUniformLocation(tessProgram_, "uMaxTessLevel");
  tHasMorph_ = glGetUniformLocation(tessProgram_, "uHasMorph");
  tSkinningEnabled_ = glGetUniformLocation(tessProgram_, "uSkinningEnabled");
  tExtendedSkinningEnabled_ =
      glGetUniformLocation(tessProgram_, "uExtendedSkinningEnabled");
  tBoneTexWidth_ = glGetUniformLocation(tessProgram_, "uBoneTexWidth");
  tBoneMatrixCount_ = glGetUniformLocation(tessProgram_, "uBoneMatrixCount");
  tInfluenceTexWidth_ = glGetUniformLocation(tessProgram_, "uInfluenceTexWidth");
  glUniform1i(glGetUniformLocation(tessProgram_, "uBaseColorTex"), 0);
  glUniform1i(glGetUniformLocation(tessProgram_, "uDisplacementTex"), 7);
  glUniform1i(glGetUniformLocation(tessProgram_, "uMorphDeltaTex"), 8);
  glUniform1i(glGetUniformLocation(tessProgram_, "uMorphCoeffTex"), 9);
  glUniform1i(glGetUniformLocation(tessProgram_, "uMorphChanTex"), 10);
  glUniform1i(glGetUniformLocation(tessProgram_, "uBoneTex"), 4);
  glUniform1i(glGetUniformLocation(tessProgram_, "uInfluenceTex"), 5);
  glUniform1i(glGetUniformLocation(tessProgram_, "uIrradianceMap"), 19);
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
    if (m.instanceOpacityVbo) glDeleteBuffers(1, &m.instanceOpacityVbo);
    if (m.vertexColorVbo) glDeleteBuffers(1, &m.vertexColorVbo);
    if (m.uv1Vbo) glDeleteBuffers(1, &m.uv1Vbo);
    if (m.morphInflVbo) glDeleteBuffers(1, &m.morphInflVbo);
    if (m.morphOffsetVbo) glDeleteBuffers(1, &m.morphOffsetVbo);
    if (m.morphDeltaTex) glDeleteTextures(1, &m.morphDeltaTex);
    if (m.morphDeltaBuf) glDeleteBuffers(1, &m.morphDeltaBuf);
    if (m.morphCoeffTex) glDeleteTextures(1, &m.morphCoeffTex);
    if (m.morphCoeffBuf) glDeleteBuffers(1, &m.morphCoeffBuf);
    if (m.morphChanTex) glDeleteTextures(1, &m.morphChanTex);
    if (m.morphChanBuf) glDeleteBuffers(1, &m.morphChanBuf);
    if (m.faceIdTex) glDeleteTextures(1, &m.faceIdTex);
    if (m.faceIdBuf) glDeleteBuffers(1, &m.faceIdBuf);
    if (m.wireEbo) glDeleteBuffers(1, &m.wireEbo);
    if (m.vbo) glDeleteBuffers(1, &m.vbo);
    if (m.vao) glDeleteVertexArrays(1, &m.vao);
  }
  meshes_.clear();
  for (GLVolume& gv : volumes_) {
    if (gv.tex3d) glDeleteTextures(1, &gv.tex3d);
  }
  volumes_.clear();
  for (GLTexture& tex : textures_) {
    if (tex.tex2d) glDeleteTextures(1, &tex.tex2d);
    if (tex.arrayTex) glDeleteTextures(1, &tex.arrayTex);
    if (tex.lutTex) glDeleteTextures(1, &tex.lutTex);
  }
  textures_.clear();
  materials_.clear();
  destroyIblTextures();
}

void GLRenderer::beginScene(const std::vector<DrawMaterialCPU>& materials,
                            int textureCount) {
  destroyScene();
  // Reserve texture slots (0 = not yet uploaded -> resolved to white at draw).
  textures_.assign(textureCount > 0 ? static_cast<size_t>(textureCount) : 0,
                   GLTexture{});
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
    gm.alphaMode = m.alphaMode;
    gm.alphaCutoff = m.alphaCutoff;
    gm.baseColorTex = m.baseColorTex;  // slot indices (resolved at draw)
    gm.metalRoughTex = m.metalRoughTex;
    gm.normalTex = m.normalTex;
    gm.emissiveTex = m.emissiveTex;
    gm.baseColorSample = m.baseColorSample;
    gm.metalRoughSample = m.metalRoughSample;
    gm.normalSample = m.normalSample;
    gm.emissiveSample = m.emissiveSample;
    gm.metallicChannel = m.metallicChannel;
    gm.roughnessChannel = m.roughnessChannel;
    gm.metallicTexScale = m.metallicTexScale;
    gm.metallicTexBias = m.metallicTexBias;
    gm.roughnessTexScale = m.roughnessTexScale;
    gm.roughnessTexBias = m.roughnessTexBias;
    gm.displacementTex = m.displacementTex;
    gm.displacementUv = m.displacementUv;
    gm.displacementConst = m.displacementConst;
    gm.displacementTexScale = m.displacementTexScale;
    gm.displacementTexBias = m.displacementTexBias;
    materials_.push_back(gm);
  }
}

void GLRenderer::uploadTexture(int slot, const DrawTextureCPU& t) {
  if (slot < 0 || static_cast<size_t>(slot) >= textures_.size()) return;
  GLTexture gpu;
  // sRGB color textures (base color / emissive) upload as GL_SRGB8_ALPHA8 so the
  // sampler linearizes them for the linear-space lighting (T11); normal /
  // metal-rough stay GL_RGBA8. Same rule the compressed path already applies.
  const GLenum uncompFmt = t.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
  if (t.isUdim && !t.udimTiles.empty() && t.udimTileWidth > 0 &&
      t.udimTileHeight > 0) {
    glGenTextures(1, &gpu.arrayTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, gpu.arrayTex);
    bool compressedArray = t.requestedCompressed;
    DrawCompressedFormat arrayFormat = DrawCompressedFormat::None;
    size_t layerBytes = 0;
    for (const DrawUdimTileCPU& tile : t.udimTiles) {
      if (tile.compressed.format == DrawCompressedFormat::None ||
          tile.compressed.data.empty()) {
        compressedArray = false;
        break;
      }
      if (arrayFormat == DrawCompressedFormat::None) {
        arrayFormat = tile.compressed.format;
        layerBytes = tile.compressed.data.size();
      } else if (arrayFormat != tile.compressed.format ||
                 layerBytes != tile.compressed.data.size()) {
        compressedArray = false;
        break;
      }
    }
    const GLenum compressedFmt = compressedArray
                                     ? GLCompressedFormat(arrayFormat, t.srgb)
                                     : 0;
    // Precomputed per-tile mip chains (FinalizeDrawTextures): usable for the
    // array only when every tile carries the same level count (+ equal
    // per-level payload sizes on the compressed path).
    size_t arrayMips = t.udimTiles.empty() ? 0 : t.udimTiles[0].mipImages.size();
    for (const DrawUdimTileCPU& tile : t.udimTiles) {
      if (tile.mipImages.size() != arrayMips) { arrayMips = 0; break; }
      if (compressedFmt != 0 &&
          tile.compressed.mips.size() != arrayMips) { arrayMips = 0; break; }
    }
    if (compressedFmt != 0 && arrayMips > 0) {
      for (size_t l = 1; l <= arrayMips && arrayMips > 0; ++l) {
        const DrawCompressedMipCPU& ref = t.udimTiles[0].compressed.mips[l - 1];
        for (const DrawUdimTileCPU& tile : t.udimTiles) {
          if (tile.compressed.mips[l - 1].data.size() != ref.data.size()) {
            arrayMips = 0;
            break;
          }
        }
      }
    }
    if (compressedFmt != 0) {
      std::vector<uint8_t> layers;
      layers.reserve(layerBytes * t.udimTiles.size());
      for (const DrawUdimTileCPU& tile : t.udimTiles) {
        layers.insert(layers.end(), tile.compressed.data.begin(),
                      tile.compressed.data.end());
      }
      glCompressedTexImage3D(GL_TEXTURE_2D_ARRAY, 0, compressedFmt,
                             t.udimTileWidth, t.udimTileHeight,
                             static_cast<GLsizei>(t.udimTiles.size()), 0,
                             static_cast<GLsizei>(layers.size()), layers.data());
      for (size_t l = 1; l <= arrayMips; ++l) {
        const DrawCompressedMipCPU& ref = t.udimTiles[0].compressed.mips[l - 1];
        std::vector<uint8_t> lvl;
        lvl.reserve(ref.data.size() * t.udimTiles.size());
        for (const DrawUdimTileCPU& tile : t.udimTiles) {
          const DrawCompressedMipCPU& m = tile.compressed.mips[l - 1];
          lvl.insert(lvl.end(), m.data.begin(), m.data.end());
        }
        glCompressedTexImage3D(GL_TEXTURE_2D_ARRAY, static_cast<GLint>(l),
                               compressedFmt, ref.width, ref.height,
                               static_cast<GLsizei>(t.udimTiles.size()), 0,
                               static_cast<GLsizei>(lvl.size()), lvl.data());
      }
    } else {
      glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, uncompFmt, t.udimTileWidth,
                   t.udimTileHeight, static_cast<GLsizei>(t.udimTiles.size()), 0,
                   GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      for (size_t layer = 0; layer < t.udimTiles.size(); ++layer) {
        const light3d::Image& img = t.udimTiles[layer].image;
        if (img.width != t.udimTileWidth || img.height != t.udimTileHeight ||
            img.data.empty()) {
          continue;
        }
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0,
                        static_cast<GLint>(layer), t.udimTileWidth,
                        t.udimTileHeight, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                        img.data.data());
      }
      for (size_t l = 1; l <= arrayMips; ++l) {
        const light3d::Image& ref = t.udimTiles[0].mipImages[l - 1];
        glTexImage3D(GL_TEXTURE_2D_ARRAY, static_cast<GLint>(l), uncompFmt,
                     ref.width, ref.height,
                     static_cast<GLsizei>(t.udimTiles.size()), 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        for (size_t layer = 0; layer < t.udimTiles.size(); ++layer) {
          const light3d::Image& img = t.udimTiles[layer].mipImages[l - 1];
          if (img.width != ref.width || img.height != ref.height ||
              img.data.empty()) {
            continue;
          }
          glTexSubImage3D(GL_TEXTURE_2D_ARRAY, static_cast<GLint>(l), 0, 0,
                          static_cast<GLint>(layer), img.width, img.height, 1,
                          GL_RGBA, GL_UNSIGNED_BYTE, img.data.data());
        }
      }
    }
    if (arrayMips > 0) {
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL,
                      static_cast<GLint>(arrayMips));
    } else {
      glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    }
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GLWrap(t.wrapS));
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GLWrap(t.wrapT));
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    std::array<int16_t, 100> lut{};
    lut.fill(-1);
    for (size_t i = 0; i < t.udimLayer.size(); ++i) {
      lut[i] = static_cast<int16_t>(t.udimLayer[i]);
    }
    glGenTextures(1, &gpu.lutTex);
    glBindTexture(GL_TEXTURE_1D, gpu.lutTex);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_R16I, 100, 0, GL_RED_INTEGER, GL_SHORT,
                 lut.data());
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_1D, 0);
    gpu.isUdim = true;

    glGenTextures(1, &gpu.tex2d);
    glBindTexture(GL_TEXTURE_2D, gpu.tex2d);
    const GLenum fmt = GLCompressedFormat(t.compressed.format, t.srgb);
    if (t.requestedCompressed && fmt != 0 && !t.compressed.data.empty()) {
      glCompressedTexImage2D(GL_TEXTURE_2D, 0, fmt, t.compressed.width,
                             t.compressed.height, 0,
                             static_cast<GLsizei>(t.compressed.data.size()),
                             t.compressed.data.data());
    } else {
      glTexImage2D(GL_TEXTURE_2D, 0, uncompFmt, t.image.width, t.image.height, 0,
                   GL_RGBA, GL_UNSIGNED_BYTE,
                   t.image.data.empty() ? nullptr : t.image.data.data());
    }
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLWrap(t.wrapS));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLWrap(t.wrapT));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
  } else {
    glGenTextures(1, &gpu.tex2d);
    glBindTexture(GL_TEXTURE_2D, gpu.tex2d);
    // Upload as plain RGBA8 (texels used as-is; see note: the simple shader and
    // linear RGBA8 target don't re-encode gamma).
    const GLenum fmt = GLCompressedFormat(t.compressed.format, t.srgb);
    const bool useCompressed =
        t.requestedCompressed && fmt != 0 && !t.compressed.data.empty();
    // Precomputed content-aware mips (sRGB/alpha-coverage/normal-aware,
    // FinalizeDrawTextures) replace glGenerateMipmap when present. This also
    // gives compressed textures real mips (glGenerateMipmap is typically a
    // no-op on BC-compressed textures).
    const bool precomputedMips =
        useCompressed ? !t.compressed.mips.empty() : !t.mipImages.empty();
    if (useCompressed) {
      glCompressedTexImage2D(GL_TEXTURE_2D, 0, fmt, t.compressed.width,
                             t.compressed.height, 0,
                             static_cast<GLsizei>(t.compressed.data.size()),
                             t.compressed.data.data());
      for (size_t l = 0; l < t.compressed.mips.size(); ++l) {
        const DrawCompressedMipCPU& mip = t.compressed.mips[l];
        glCompressedTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(l + 1), fmt,
                               mip.width, mip.height, 0,
                               static_cast<GLsizei>(mip.data.size()),
                               mip.data.data());
      }
    } else {
      glTexImage2D(GL_TEXTURE_2D, 0, uncompFmt, t.image.width, t.image.height, 0,
                   GL_RGBA, GL_UNSIGNED_BYTE,
                   t.image.data.empty() ? nullptr : t.image.data.data());
      for (size_t l = 0; l < t.mipImages.size(); ++l) {
        const light3d::Image& mip = t.mipImages[l];
        glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(l + 1), uncompFmt,
                     mip.width, mip.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     mip.data.empty() ? nullptr : mip.data.data());
      }
    }
    if (precomputedMips) {
      const size_t levels =
          useCompressed ? t.compressed.mips.size() : t.mipImages.size();
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
                      static_cast<GLint>(levels));
    } else {
      glGenerateMipmap(GL_TEXTURE_2D);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLWrap(t.wrapS));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLWrap(t.wrapT));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  GLTexture& old = textures_[static_cast<size_t>(slot)];
  if (old.tex2d) glDeleteTextures(1, &old.tex2d);
  if (old.arrayTex) glDeleteTextures(1, &old.arrayTex);
  if (old.lutTex) glDeleteTextures(1, &old.lutTex);
  old = gpu;
}

void GLRenderer::destroyIblTextures() {
  if (iblIrrTex_) glDeleteTextures(1, &iblIrrTex_);
  if (iblSpecTex_) glDeleteTextures(1, &iblSpecTex_);
  if (iblLutTex_) glDeleteTextures(1, &iblLutTex_);
  iblIrrTex_ = iblSpecTex_ = iblLutTex_ = 0;
  iblSpecLods_ = 0;
  iblActive_ = false;
}

void GLRenderer::setLights(const std::vector<DrawLightCPU>& lights) {
  destroyIblTextures();
  const DrawLightCPU* dome = nullptr;
  for (const DrawLightCPU& l : lights) {
    if (l.type == DrawLightCPU::Type::Dome && l.ibl.valid) {
      dome = &l;
      break;
    }
  }
  if (!dome) return;
  const DomeIblCPU& ibl = dome->ibl;

  // GGX-prefiltered specular chain: one cube level per roughness step.
  glGenTextures(1, &iblSpecTex_);
  glBindTexture(GL_TEXTURE_CUBE_MAP, iblSpecTex_);
  for (size_t l = 0; l < ibl.specLevels.size(); ++l) {
    const int fs = std::max(1, ibl.specFaceSize >> l);
    const float* data = ibl.specLevels[l].data();
    for (int f = 0; f < 6; ++f) {
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(f),
                   static_cast<GLint>(l), GL_RGB16F, fs, fs, 0, GL_RGB,
                   GL_FLOAT, data + static_cast<size_t>(f) * fs * fs * 3);
    }
  }
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL,
                  static_cast<GLint>(ibl.specLevels.size()) - 1);

  // Diffuse irradiance cube (single level).
  glGenTextures(1, &iblIrrTex_);
  glBindTexture(GL_TEXTURE_CUBE_MAP, iblIrrTex_);
  for (int f = 0; f < 6; ++f) {
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(f), 0,
                 GL_RGB16F, ibl.irrFaceSize, ibl.irrFaceSize, 0, GL_RGB,
                 GL_FLOAT,
                 ibl.irradiance.data() +
                     static_cast<size_t>(f) * ibl.irrFaceSize * ibl.irrFaceSize * 3);
  }
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

  // Split-sum BRDF LUT (scale, bias).
  glGenTextures(1, &iblLutTex_);
  glBindTexture(GL_TEXTURE_2D, iblLutTex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, ibl.lutSize, ibl.lutSize, 0, GL_RG,
               GL_FLOAT, ibl.brdfLut.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);

  iblSpecLods_ = static_cast<int>(ibl.specLevels.size());
  iblColor_[0] = dome->effectiveColor[0];
  iblColor_[1] = dome->effectiveColor[1];
  iblColor_[2] = dome->effectiveColor[2];
  // World -> environment: transpose of the dome transform's normalized
  // rotation block (column-major 4x4 -> column-major 3x3).
  for (int c = 0; c < 3; ++c) {
    float col[3] = {dome->transform[c * 4 + 0], dome->transform[c * 4 + 1],
                    dome->transform[c * 4 + 2]};
    const float len =
        std::sqrt(col[0] * col[0] + col[1] * col[1] + col[2] * col[2]);
    const float inv = (len > 1e-12f) ? 1.0f / len : 1.0f;
    // Row c of the inverse rotation = normalized column c of the transform.
    iblRotation_[0 * 3 + c] = col[0] * inv;
    iblRotation_[1 * 3 + c] = col[1] * inv;
    iblRotation_[2 * 3 + c] = col[2] * inv;
  }
  iblActive_ = true;
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

void GLRenderer::updateMorphWeights(int meshIndex,
                                    const std::vector<float>& coeffs) {
  if (meshIndex < 0 || meshIndex >= static_cast<int>(meshes_.size())) return;
  GLMesh& gm = meshes_[static_cast<size_t>(meshIndex)];
  if (!gm.hasMorph || !gm.morphCoeffBuf || coeffs.empty()) return;
  const size_t n = std::min(coeffs.size(),
                            static_cast<size_t>(gm.morphChannelCount));
  glBindBuffer(GL_TEXTURE_BUFFER, gm.morphCoeffBuf);
  glBufferSubData(GL_TEXTURE_BUFFER, 0,
                  static_cast<GLsizeiptr>(n * sizeof(float)), coeffs.data());
  glBindBuffer(GL_TEXTURE_BUFFER, 0);
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
  if (gm.wireEbo) glDeleteBuffers(1, &gm.wireEbo);
  if (gm.instanceVbo) glDeleteBuffers(1, &gm.instanceVbo);
  if (gm.instanceColorVbo) glDeleteBuffers(1, &gm.instanceColorVbo);
  if (gm.instanceOpacityVbo) glDeleteBuffers(1, &gm.instanceOpacityVbo);
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
  // Mesh-space bbox center for the translucency back-to-front sort.
  if (!sm.vertices.empty()) {
    float lo[3] = {sm.vertices[0].px, sm.vertices[0].py, sm.vertices[0].pz};
    float hi[3] = {lo[0], lo[1], lo[2]};
    for (const DrawVertex& v : sm.vertices) {
      lo[0] = std::min(lo[0], v.px); hi[0] = std::max(hi[0], v.px);
      lo[1] = std::min(lo[1], v.py); hi[1] = std::max(hi[1], v.py);
      lo[2] = std::min(lo[2], v.pz); hi[2] = std::max(hi[2], v.pz);
    }
    gm.localCentroid[0] = 0.5f * (lo[0] + hi[0]);
    gm.localCentroid[1] = 0.5f * (lo[1] + hi[1]);
    gm.localCentroid[2] = 0.5f * (lo[2] + hi[2]);
  }

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
  // Skin attribute locations differ by program: the mesh program takes joints at 3
  // and weights at 4, but in the INSTANCED program those slots (and 5) carry the
  // per-instance o2w rows, so a skinned prototype binds 6/7 instead. The extended
  // (>4 influence) stream needs attrib 5 and therefore has no instanced form --
  // instanced prototypes use the 4-influence path only.
  const bool skinInstanced = !sm.instanceXforms.empty();
  const GLuint jointLoc = skinInstanced ? 6u : 3u;
  const GLuint weightLoc = skinInstanced ? 7u : 4u;
  if (skinInstanced) gm.extendedSkinned = false;
  if (gm.skinned) {
    glGenBuffers(1, &gm.jointVbo);
    glBindBuffer(GL_ARRAY_BUFFER, gm.jointVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sm.jointIdx.size() * sizeof(uint32_t)),
                 sm.jointIdx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(jointLoc);
    glVertexAttribIPointer(jointLoc, 4, GL_UNSIGNED_INT, 4 * sizeof(uint32_t),
                           (void*)0);
    glVertexAttribDivisor(jointLoc, 0);
    glGenBuffers(1, &gm.weightVbo);
    glBindBuffer(GL_ARRAY_BUFFER, gm.weightVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sm.jointWt.size() * sizeof(float)),
                 sm.jointWt.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(weightLoc);
    glVertexAttribPointer(weightLoc, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)0);
    glVertexAttribDivisor(weightLoc, 0);
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
  } else if (skinInstanced) {
    // Unskinned prototype: constant zero weights, so the instanced shader passes
    // every vertex through. Slots 3/4/5 belong to the instance rows here and are
    // set up below -- do NOT touch them.
    glDisableVertexAttribArray(6);
    glDisableVertexAttribArray(7);
    glVertexAttribI4ui(6, 0, 0, 0, 0);
    glVertexAttrib4f(7, 0.0f, 0.0f, 0.0f, 0.0f);
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
  // Mesh-space bbox center for the translucency back-to-front sort.
  if (!sm.vertices.empty()) {
    float lo[3] = {sm.vertices[0].px, sm.vertices[0].py, sm.vertices[0].pz};
    float hi[3] = {lo[0], lo[1], lo[2]};
    for (const DrawVertex& v : sm.vertices) {
      lo[0] = std::min(lo[0], v.px); hi[0] = std::max(hi[0], v.px);
      lo[1] = std::min(lo[1], v.py); hi[1] = std::max(hi[1], v.py);
      lo[2] = std::min(lo[2], v.pz); hi[2] = std::max(hi[2], v.pz);
    }
    gm.localCentroid[0] = 0.5f * (lo[0] + hi[0]);
    gm.localCentroid[1] = 0.5f * (lo[1] + hi[1]);
    gm.localCentroid[2] = 0.5f * (lo[2] + hi[2]);
  }

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
  // Skin attribute locations differ by program: the mesh program takes joints at 3
  // and weights at 4, but in the INSTANCED program those slots (and 5) carry the
  // per-instance o2w rows, so a skinned prototype binds 6/7 instead. The extended
  // (>4 influence) stream needs attrib 5 and therefore has no instanced form --
  // instanced prototypes use the 4-influence path only.
  const bool skinInstanced = !sm.instanceXforms.empty();
  const GLuint jointLoc = skinInstanced ? 6u : 3u;
  const GLuint weightLoc = skinInstanced ? 7u : 4u;
  if (skinInstanced) gm.extendedSkinned = false;
  if (gm.skinned) {
    glGenBuffers(1, &gm.jointVbo);
    glBindBuffer(GL_ARRAY_BUFFER, gm.jointVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sm.jointIdx.size() * sizeof(uint32_t)),
                 sm.jointIdx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(jointLoc);
    glVertexAttribIPointer(jointLoc, 4, GL_UNSIGNED_INT, 4 * sizeof(uint32_t),
                           (void*)0);
    glVertexAttribDivisor(jointLoc, 0);
    glGenBuffers(1, &gm.weightVbo);
    glBindBuffer(GL_ARRAY_BUFFER, gm.weightVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sm.jointWt.size() * sizeof(float)),
                 sm.jointWt.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(weightLoc);
    glVertexAttribPointer(weightLoc, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)0);
    glVertexAttribDivisor(weightLoc, 0);
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
  } else if (skinInstanced) {
    // Unskinned prototype: constant zero weights, so the instanced shader passes
    // every vertex through. Slots 3/4/5 belong to the instance rows here and are
    // set up below -- do NOT touch them.
    glDisableVertexAttribArray(6);
    glDisableVertexAttribArray(7);
    glVertexAttribI4ui(6, 0, 0, 0, 0);
    glVertexAttrib4f(7, 0.0f, 0.0f, 0.0f, 0.0f);
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
  }  // end !gmInstanced (uv1 / morphInfl / faceId)

  // GPU blendshape morph: per-vertex (offset,count) attr 8 + a static delta
  // texture-buffer (RGBA16F: channelId,dx,dy,dz half-precision) + a per-frame
  // coefficient texture-buffer (R32F). The vertex shader sums coeff*delta before
  // the per-instance transform. Shared by non-instanced AND instanced meshes:
  // instance rows live at locations 3/4/5, leaving attr 8 free for the morph CSR.
    if (sm.morphChannelCount > 0 &&
        sm.morphOffsetCount.size() == sm.vertices.size() * 2 &&
        !sm.morphDeltaHalf.empty()) {
      glGenBuffers(1, &gm.morphOffsetVbo);
      glBindBuffer(GL_ARRAY_BUFFER, gm.morphOffsetVbo);
      glBufferData(GL_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(sm.morphOffsetCount.size() * sizeof(uint32_t)),
                   sm.morphOffsetCount.data(), GL_STATIC_DRAW);
      glEnableVertexAttribArray(8);
      glVertexAttribIPointer(8, 2, GL_UNSIGNED_INT, 2 * sizeof(uint32_t), (void*)0);
      glVertexAttribDivisor(8, 0);
      glGenBuffers(1, &gm.morphDeltaBuf);
      glBindBuffer(GL_TEXTURE_BUFFER, gm.morphDeltaBuf);
      glBufferData(GL_TEXTURE_BUFFER,
                   static_cast<GLsizeiptr>(sm.morphDeltaHalf.size() * sizeof(uint16_t)),
                   sm.morphDeltaHalf.data(), GL_STATIC_DRAW);
      glGenTextures(1, &gm.morphDeltaTex);
      glBindTexture(GL_TEXTURE_BUFFER, gm.morphDeltaTex);
      glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA16F, gm.morphDeltaBuf);
      // Per-entry channelId (R16UI) for the in-shader active-channel skip.
      if (sm.morphChannelId.size() == sm.morphDeltaHalf.size() / 4) {
        glGenBuffers(1, &gm.morphChanBuf);
        glBindBuffer(GL_TEXTURE_BUFFER, gm.morphChanBuf);
        glBufferData(GL_TEXTURE_BUFFER,
                     static_cast<GLsizeiptr>(sm.morphChannelId.size() * sizeof(uint16_t)),
                     sm.morphChannelId.data(), GL_STATIC_DRAW);
        glGenTextures(1, &gm.morphChanTex);
        glBindTexture(GL_TEXTURE_BUFFER, gm.morphChanTex);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_R16UI, gm.morphChanBuf);
      }
      const std::vector<float> zeroCoeff(static_cast<size_t>(sm.morphChannelCount), 0.0f);
      glGenBuffers(1, &gm.morphCoeffBuf);
      glBindBuffer(GL_TEXTURE_BUFFER, gm.morphCoeffBuf);
      glBufferData(GL_TEXTURE_BUFFER,
                   static_cast<GLsizeiptr>(zeroCoeff.size() * sizeof(float)),
                   zeroCoeff.data(), GL_DYNAMIC_DRAW);
      glGenTextures(1, &gm.morphCoeffTex);
      glBindTexture(GL_TEXTURE_BUFFER, gm.morphCoeffTex);
      glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, gm.morphCoeffBuf);
      glBindBuffer(GL_TEXTURE_BUFFER, 0);
      glBindTexture(GL_TEXTURE_BUFFER, 0);
      gm.morphChannelCount = sm.morphChannelCount;
      // The skip shader reads channelId from morphChanTex; without it (the
      // size-match guard above failing) binding texture 0 would feed channelId 0
      // for every entry -> wrong morph. Gate on it, mirroring the VK path.
      gm.hasMorph = (gm.morphChanTex != 0);
    } else {
      glDisableVertexAttribArray(8);
      glVertexAttribI2ui(8, 0u, 0u);
    }

  // GPU instancing: upload per-instance 3x4 object-to-world matrices (3 vec4
  // rows = 48 B/instance) into a second VBO; bind as instanced attribs 3-5
  // (divisor 1) -- off the morph/uv slots so an instanced prototype can morph.
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
      const GLuint loc = 3 + r;
      glEnableVertexAttribArray(loc);
      glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, mstride,
                            (void*)(static_cast<uintptr_t>(r * 4 * sizeof(float))));
      glVertexAttribDivisor(loc, 1);
    }
    // Per-instance displayColor/displayOpacity (attribs 9/11, divisor 1) when
    // present; otherwise per-draw constants are set at draw time.
    std::memcpy(gm.flatColor, sm.flatColor, sizeof(gm.flatColor));
    gm.flatOpacity = sm.flatOpacity;
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
    if (sm.instanceOpacities.size() == sm.instanceXforms.size() / 12 &&
        !sm.instanceOpacities.empty()) {
      gm.hasInstanceOpacities = true;
      for (float opacity : sm.instanceOpacities) {
        if (opacity < 1.0f - 1.0e-6f) {
          gm.hasTranslucentInstances = true;
          break;
        }
      }
      glGenBuffers(1, &gm.instanceOpacityVbo);
      glBindBuffer(GL_ARRAY_BUFFER, gm.instanceOpacityVbo);
      glBufferData(GL_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(sm.instanceOpacities.size() * sizeof(float)),
                   sm.instanceOpacities.data(), GL_DYNAMIC_DRAW);
      glEnableVertexAttribArray(11);
      glVertexAttribPointer(11, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
      glVertexAttribDivisor(11, 1);
    } else {
      gm.hasTranslucentInstances = gm.flatOpacity < 1.0f - 1.0e-6f;
      glDisableVertexAttribArray(11);  // constant opacity set per draw
    }
  }

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

  // Wireframe edges. Prefer the loader-provided original-polygon perimeter edges
  // (sm.wireframeIndices: quads/ngons, correct for double-sided meshes). Fall
  // back to deriving edges from triangles + source face ids (drop triangulation
  // diagonals), and finally to every triangle edge. Always from the base (coarse)
  // indices, so wireframe shows the pre-tessellation mesh.
  if (!sm.wireframeIndices.empty()) {
    glGenBuffers(1, &gm.wireEbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gm.wireEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sm.wireframeIndices.size() * sizeof(uint32_t)),
                 sm.wireframeIndices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    gm.wireCount = static_cast<GLsizei>(sm.wireframeIndices.size());
  } else {
    const size_t triCount = sm.indices.size() / 3;
    const bool haveFaceIds =
        sm.sourceFaceId.size() == triCount && !sm.sourceFaceId.empty();
    // edge key -> (first face id seen, keep?)
    std::unordered_map<uint64_t, std::pair<uint32_t, bool>> edges;
    edges.reserve(triCount * 3);
    auto key = [](uint32_t a, uint32_t b) -> uint64_t {
      if (a > b) { uint32_t t = a; a = b; b = t; }
      return (static_cast<uint64_t>(a) << 32) | b;
    };
    for (size_t t = 0; t < triCount; ++t) {
      const uint32_t v[3] = {sm.indices[t * 3], sm.indices[t * 3 + 1],
                             sm.indices[t * 3 + 2]};
      const uint32_t f = haveFaceIds ? sm.sourceFaceId[t] : static_cast<uint32_t>(t);
      const uint32_t e[3][2] = {{v[0], v[1]}, {v[1], v[2]}, {v[2], v[0]}};
      for (int k = 0; k < 3; ++k) {
        const uint64_t ek = key(e[k][0], e[k][1]);
        auto it = edges.find(ek);
        if (it == edges.end()) {
          edges.emplace(ek, std::make_pair(f, true));  // boundary until proven interior
        } else {
          it->second.second = (it->second.first != f);  // same face -> diagonal -> drop
        }
      }
    }
    std::vector<uint32_t> wire;
    wire.reserve(edges.size() * 2);
    for (const auto& kv : edges) {
      if (!kv.second.second) continue;
      wire.push_back(static_cast<uint32_t>(kv.first >> 32));
      wire.push_back(static_cast<uint32_t>(kv.first & 0xffffffffu));
    }
    if (!wire.empty()) {
      glGenBuffers(1, &gm.wireEbo);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gm.wireEbo);  // no VAO bound: not VAO state
      glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(wire.size() * sizeof(uint32_t)),
                   wire.data(), GL_STATIC_DRAW);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
      gm.wireCount = static_cast<GLsizei>(wire.size());
    }
  }

  meshes_.push_back(gm);
}

void GLRenderer::updateInstanceVisibility(size_t meshIndex, const float* xforms,
                                          const float* colors,
                                          const float* opacities,
                                          uint32_t count) {
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
  if (m.instanceOpacityVbo && opacities) {
    glBindBuffer(GL_ARRAY_BUFFER, m.instanceOpacityVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(count) * sizeof(float), opacities);
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

void GLRenderer::buildWireProgram() {
  // Anti-aliased thin wireframe: the VS transforms an edge endpoint to clip space
  // (with a small NDC depth bias to lift it off the surface); the GS expands each
  // edge into a screen-space quad uHalfWidth pixels to each side; the FS applies an
  // analytic distance-to-center falloff for a crisp ~1 px AA line (usdview look,
  // resolution-independent -- the same edge-distance idea the RT path can use).
  static const char* kWireFS =
      "#version 330 core\n"
      "in float vDist;\n"            // signed pixel distance from the line center
      "out vec4 FragColor;\n"
      "uniform vec3 uWireColor;\n"
      "uniform float uHalfWidth;\n"  // pixels: half the expanded quad width
      "void main(){\n"
      "  float d = abs(vDist);\n"
      // 1 px-wide analytic AA: opaque core, feather to 0 over the last pixel.
      "  float a = 1.0 - smoothstep(uHalfWidth - 1.0, uHalfWidth, d);\n"
      "  if (a <= 0.0) discard;\n"
      "  FragColor = vec4(uWireColor, a);\n"
      "}\n";
  // Geometry shader (shared): line -> screen-space quad. Endpoints come in clip
  // space; convert to pixels, offset along the screen-space normal by uHalfWidth,
  // convert back to clip (scaling by w for perspective). vDist feathers the edge.
  static const char* kWireGS =
      "#version 330 core\n"
      "layout(lines) in;\n"
      "layout(triangle_strip, max_vertices=4) out;\n"
      "uniform vec2 uViewport;\n"     // pixels
      "uniform float uHalfWidth;\n"   // pixels
      "out float vDist;\n"
      "void main(){\n"
      "  vec4 c0 = gl_in[0].gl_Position;\n"
      "  vec4 c1 = gl_in[1].gl_Position;\n"
      "  if (c0.w <= 0.0 || c1.w <= 0.0) return;\n"  // skip edges behind the eye
      "  vec2 s0 = (c0.xy / c0.w) * 0.5 * uViewport;\n"
      "  vec2 s1 = (c1.xy / c1.w) * 0.5 * uViewport;\n"
      "  vec2 dir = s1 - s0;\n"
      "  float len = length(dir);\n"
      "  dir = len > 1e-5 ? dir / len : vec2(1.0, 0.0);\n"
      "  vec2 nrm = vec2(-dir.y, dir.x);\n"
      "  vec2 off = nrm / (0.5 * uViewport) * uHalfWidth;\n"  // pixels -> NDC
      "  vec4 e0 = c0; vec4 e1 = c1;\n"
      "  gl_Position = vec4(e0.xy + off * e0.w, e0.zw); vDist =  uHalfWidth; EmitVertex();\n"
      "  gl_Position = vec4(e0.xy - off * e0.w, e0.zw); vDist = -uHalfWidth; EmitVertex();\n"
      "  gl_Position = vec4(e1.xy + off * e1.w, e1.zw); vDist =  uHalfWidth; EmitVertex();\n"
      "  gl_Position = vec4(e1.xy - off * e1.w, e1.zw); vDist = -uHalfWidth; EmitVertex();\n"
      "  EndPrimitive();\n"
      "}\n";
  static const char* kWireVS =
      "#version 330 core\n"
      "layout(location=0) in vec3 aPosition;\n"
      "uniform mat4 uModelViewProj;\n"
      "uniform float uDepthBias;\n"
      "void main(){\n"
      "  vec4 p = uModelViewProj * vec4(aPosition, 1.0);\n"
      "  p.z -= uDepthBias * p.w;\n"
      "  gl_Position = p;\n"
      "}\n";
  static const char* kWireInstVS =
      "#version 330 core\n"
      "layout(location=0) in vec3 aPosition;\n"
      "layout(location=3) in vec4 aRow0;\n"
      "layout(location=4) in vec4 aRow1;\n"
      "layout(location=5) in vec4 aRow2;\n"
      "uniform mat4 uViewProj;\n"
      "uniform float uDepthBias;\n"
      "void main(){\n"
      "  vec4 lp = vec4(aPosition, 1.0);\n"
      "  vec3 wp = vec3(dot(lp, aRow0), dot(lp, aRow1), dot(lp, aRow2));\n"
      "  vec4 p = uViewProj * vec4(wp, 1.0);\n"
      "  p.z -= uDepthBias * p.w;\n"
      "  gl_Position = p;\n"
      "}\n";
  std::string werr;
  wireProgram_ = glutil::CompileProgramGeom(kWireVS, kWireGS, kWireFS, &werr);
  if (wireProgram_) {
    wMVP_ = glGetUniformLocation(wireProgram_, "uModelViewProj");
    wWireColor_ = glGetUniformLocation(wireProgram_, "uWireColor");
    wDepthBias_ = glGetUniformLocation(wireProgram_, "uDepthBias");
    wViewport_ = glGetUniformLocation(wireProgram_, "uViewport");
    wHalfWidth_ = glGetUniformLocation(wireProgram_, "uHalfWidth");
  } else if (!werr.empty()) {
    fprintf(stderr, "[tusdview] wire program: %s\n", werr.c_str());
  }
  werr.clear();
  wireInstProgram_ = glutil::CompileProgramGeom(kWireInstVS, kWireGS, kWireFS, &werr);
  if (wireInstProgram_) {
    wiViewProj_ = glGetUniformLocation(wireInstProgram_, "uViewProj");
    wiWireColor_ = glGetUniformLocation(wireInstProgram_, "uWireColor");
    wiDepthBias_ = glGetUniformLocation(wireInstProgram_, "uDepthBias");
    wiViewport_ = glGetUniformLocation(wireInstProgram_, "uViewport");
    wiHalfWidth_ = glGetUniformLocation(wireInstProgram_, "uHalfWidth");
  } else if (!werr.empty()) {
    fprintf(stderr, "[tusdview] wire inst program: %s\n", werr.c_str());
  }
}

void GLRenderer::drawWireframe(const RenderFrameParams& params, const float wireColor[3]) {
  if (!wireProgram_ && !wireInstProgram_) return;
  const float kBias = 0.0008f;  // NDC z bias: lines just in front of the surface
  light3d::Mat4 P = ToMat4(params.proj);
  light3d::Mat4 V = ToMat4(params.view);
  glDisable(GL_CULL_FACE);
  // The GS expands each edge into a thin screen-space quad and the FS feathers it
  // analytically, so we get sub-pixel-thin AA lines (no GL_LINE_SMOOTH). Alpha
  // blend for the feathered edge; depth test on (VS bias keeps lines in front),
  // depth writes off so overlapping edges don't fight.
  glDisable(GL_LINE_SMOOTH);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  const float vpx = static_cast<float>(vpW_), vpy = static_cast<float>(vpH_);
  const float kHalfWidth = 1.3f;  // pixels: ~1 px visible line with a 1 px AA edge

  // Non-instanced meshes: per-mesh MVP.
  if (wireProgram_) {
    glUseProgram(wireProgram_);
    glUniform3fv(wWireColor_, 1, wireColor);
    glUniform1f(wDepthBias_, kBias);
    glUniform2f(wViewport_, vpx, vpy);
    glUniform1f(wHalfWidth_, kHalfWidth);
    for (size_t mi = 0; mi < meshes_.size(); ++mi) {
      const GLMesh& mesh = meshes_[mi];
      if (mesh.instanceCount > 0 || mesh.wireEbo == 0 || mesh.wireCount == 0) continue;
      if (params.meshVisible && mi < static_cast<size_t>(params.meshVisibleCount) &&
          !params.meshVisible[mi]) {
        continue;
      }
      light3d::Mat4 MVP = P * V * ToMat4(mesh.world);
      glUniformMatrix4fv(wMVP_, 1, GL_FALSE, MVP.m);
      glBindVertexArray(mesh.vao);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.wireEbo);  // override the VAO's tri EBO
      glDrawElements(GL_LINES, mesh.wireCount, GL_UNSIGNED_INT, nullptr);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);      // restore the VAO state
    }
  }

  // Instanced prototypes: per-instance 3x4 rows (attribs 3/4/5) + view-proj.
  if (wireInstProgram_) {
    light3d::Mat4 VP = P * V;
    glUseProgram(wireInstProgram_);
    glUniformMatrix4fv(wiViewProj_, 1, GL_FALSE, VP.m);
    glUniform3fv(wiWireColor_, 1, wireColor);
    glUniform1f(wiDepthBias_, kBias);
    glUniform2f(wiViewport_, vpx, vpy);
    glUniform1f(wiHalfWidth_, kHalfWidth);
    for (size_t mi = 0; mi < meshes_.size(); ++mi) {
      const GLMesh& mesh = meshes_[mi];
      if (mesh.instanceCount <= 0 || mesh.drawInstanceCount <= 0) continue;
      if (mesh.wireEbo == 0 || mesh.wireCount == 0) continue;
      if (params.meshVisible && mi < static_cast<size_t>(params.meshVisibleCount) &&
          !params.meshVisible[mi]) {
        continue;
      }
      glBindVertexArray(mesh.vao);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.wireEbo);
      glDrawElementsInstanced(GL_LINES, mesh.wireCount, GL_UNSIGNED_INT, nullptr,
                              mesh.drawInstanceCount);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    }
  }
  glBindVertexArray(0);
  // Restore default state for the rest of the frame (ImGui / next passes).
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glDisable(GL_LINE_SMOOTH);
  glUseProgram(program_);  // restore for callers that assume program_ is bound
}

void GLRenderer::drawMeshes(const RenderFrameParams& params, bool wireframe,
                            const float* overrideEmissive, AlphaPass alphaPass) {
  static const GLMaterial kDefault;
  light3d::Mat4 P = ToMat4(params.proj);
  light3d::Mat4 V = ToMat4(params.view);

  auto matTranslucent = [&](int materialId) -> bool {
    if (materialId < 0 || static_cast<size_t>(materialId) >= materials_.size())
      return false;
    return materials_[static_cast<size_t>(materialId)].alphaMode == 2;  // Blend
  };

  // Draw order. For the translucent pass, sort meshes back-to-front by world
  // centroid distance to the camera so alpha "over" composites correctly.
  std::vector<size_t> order(meshes_.size());
  for (size_t i = 0; i < meshes_.size(); ++i) order[i] = i;
  if (alphaPass == AlphaPass::Translucent) {
    std::vector<float> dist(meshes_.size(), 0.0f);
    for (size_t mi = 0; mi < meshes_.size(); ++mi) {
      const GLMesh& m = meshes_[mi];
      const float* c = m.localCentroid;
      // world * centroid (column-major world[16]).
      const float wx = m.world[0]*c[0] + m.world[4]*c[1] + m.world[8]*c[2] + m.world[12];
      const float wy = m.world[1]*c[0] + m.world[5]*c[1] + m.world[9]*c[2] + m.world[13];
      const float wz = m.world[2]*c[0] + m.world[6]*c[1] + m.world[10]*c[2] + m.world[14];
      const float dx = wx - params.cameraPos[0];
      const float dy = wy - params.cameraPos[1];
      const float dz = wz - params.cameraPos[2];
      dist[mi] = dx*dx + dy*dy + dz*dz;
    }
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b) { return dist[a] > dist[b]; });
  }

  int cullState = -1;  // -1 unknown; dedup glEnable/glDisable(GL_CULL_FACE)
  for (size_t oi = 0; oi < order.size(); ++oi) {
    const size_t mi = order[oi];
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
    // Morphed meshes shade with geometric normals (the rest normals no longer
    // match the GPU-morphed surface), like the displacement path.
    glUniform1i(uGeometricNormal_, (mesh.geometricNormal || mesh.hasMorph) ? 1 : 0);
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
    // GPU blendshape morph: bind the per-vertex delta + per-frame coefficient
    // texture buffers (units 8/9). The VAO already supplies attr 8 (offset,count).
    glUniform1i(uHasMorph_, mesh.hasMorph ? 1 : 0);
    if (mesh.hasMorph) {
      glActiveTexture(GL_TEXTURE8);
      glBindTexture(GL_TEXTURE_BUFFER, mesh.morphDeltaTex);
      glActiveTexture(GL_TEXTURE9);
      glBindTexture(GL_TEXTURE_BUFFER, mesh.morphCoeffTex);
      glActiveTexture(GL_TEXTURE10);
      glBindTexture(GL_TEXTURE_BUFFER, mesh.morphChanTex);
      glActiveTexture(GL_TEXTURE0);
    }

    const int wantCull = (mesh.doubleSided || wireframe) ? 0 : 1;
    if (wantCull != cullState) {
      if (wantCull) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }
      else glDisable(GL_CULL_FACE);
      cullState = wantCull;
    }

    glBindVertexArray(mesh.vao);
    for (const auto& sub : mesh.submeshes) {
      // Alpha-class filtering: the opaque pass skips Blend submeshes, the
      // translucent pass skips the rest. `All` (AOV/wireframe) draws everything.
      if (alphaPass == AlphaPass::Opaque && matTranslucent(sub.materialId)) continue;
      if (alphaPass == AlphaPass::Translucent && !matTranslucent(sub.materialId)) continue;
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
            textures_[static_cast<size_t>(slot)].tex2d) {
          return textures_[static_cast<size_t>(slot)].tex2d;
        }
        return whiteTex_;
      };
      auto slotGpuTex = [&](int slot) -> const GLTexture* {
        if (slot >= 0 && static_cast<size_t>(slot) < textures_.size()) {
          const GLTexture& tex = textures_[static_cast<size_t>(slot)];
          if (tex.tex2d || (tex.isUdim && tex.arrayTex && tex.lutTex)) return &tex;
        }
        return nullptr;
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
      if (displaced && tessAvailable_ && !overrideEmissive &&
          params.maxTessLevel > 1 &&
          params.mode == RenderMode::Shaded) {
        const GLMaterial& dmat = materials_[static_cast<size_t>(sub.materialId)];
        glUseProgram(tessProgram_);
        glUniformMatrix4fv(tMVP_, 1, GL_FALSE, MVP.m);
        glUniformMatrix4fv(tModel_, 1, GL_FALSE, W.m);
        glUniformMatrix3fv(tNormalMat_, 1, GL_FALSE, nmat);
        glUniform3fv(tCameraPos_, 1, params.cameraPos);
        glUniform3fv(tLightDir_, 1, params.lightDir);
        glUniform1i(tHasIbl_, iblActive_ ? 1 : 0);
        if (iblActive_) {
          glUniform3fv(tIblColor_, 1, iblColor_);
          glUniformMatrix3fv(tEnvRotation_, 1, GL_FALSE, iblRotation_);
        }
        glUniform3fv(tLightColor_, 1, params.lightColor);
        glUniform3fv(tBaseColor_, 1, dmat.baseColor);
        glUniform1i(tHasBaseColorTex_, dmat.baseColorTex >= 0 ? 1 : 0);
        glUniform1i(tHasDisplacementTex_, dmat.displacementTex >= 0 ? 1 : 0);
        glUniform1f(tDisplacementConst_, dmat.displacementConst);
        glUniform1f(tDisplacementScale_, params.displacementScale);
        glUniform1f(tDisplacementTexScale_, dmat.displacementTexScale);
        glUniform1f(tDisplacementTexBias_, dmat.displacementTexBias);
        glUniform1f(tMaxTessLevel_, static_cast<float>(params.maxTessLevel));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, slotTex(dmat.baseColorTex));
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, slotTex(dmat.displacementTex));
        // GPU blendshape morph in the tess vertex stage (the VAO already supplies
        // attr 8); bind the morph delta/coeff/channelId texture buffers (units
        // 8/9/10) so the tessellated surface is the morphed one.
        glUniform1i(tHasMorph_, mesh.hasMorph ? 1 : 0);
        if (mesh.hasMorph) {
          glActiveTexture(GL_TEXTURE8);
          glBindTexture(GL_TEXTURE_BUFFER, mesh.morphDeltaTex);
          glActiveTexture(GL_TEXTURE9);
          glBindTexture(GL_TEXTURE_BUFFER, mesh.morphCoeffTex);
          glActiveTexture(GL_TEXTURE10);
          glBindTexture(GL_TEXTURE_BUFFER, mesh.morphChanTex);
          glActiveTexture(GL_TEXTURE0);
        }
        // Skinning in the tess vertex stage (mirrors the coarse program); bone
        // matrices (unit 4) + extended-influence (unit 5) texture buffers.
        const bool tSkinOn = mesh.skinned && skinningFrameEnabled_;
        glUniform1i(tSkinningEnabled_, tSkinOn ? 1 : 0);
        glUniform1i(tExtendedSkinningEnabled_,
                    (mesh.extendedSkinned && skinningFrameEnabled_) ? 1 : 0);
        glUniform1i(tBoneTexWidth_, boneTexWidth_ > 0 ? boneTexWidth_ : 4);
        glUniform1i(tBoneMatrixCount_, skinningFrameEnabled_ ? boneMatrixCount_ : 1);
        glUniform1i(tInfluenceTexWidth_,
                    (mesh.extendedSkinned && skinningFrameEnabled_) ? mesh.influenceTexWidth : 0);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, boneTex_ ? boneTex_ : whiteTex_);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, mesh.influenceTex ? mesh.influenceTex : whiteTex_);
        glActiveTexture(GL_TEXTURE0);
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
        glUniform1f(uDisplacementTexScale_, dmat.displacementTexScale);
        glUniform1f(uDisplacementTexBias_, dmat.displacementTexBias);
        glBindTexture(GL_TEXTURE_2D, slotTex(dmat.displacementTex));
        glUniform1i(uGeometricNormal_, 1);
      } else {
        glBindTexture(GL_TEXTURE_2D, whiteTex_);
        glUniform1i(uGeometricNormal_, (mesh.geometricNormal || mesh.hasMorph) ? 1 : 0);
      }
      if (overrideEmissive) {
        glUniform3f(uBaseColor_, 0.f, 0.f, 0.f);
        glUniform1f(uMetallic_, 0.f);
        glUniform1f(uRoughness_, 1.f);
        glUniform3fv(uEmissive_, 1, overrideEmissive);
        glUniform1f(uAlpha_, 1.f);
        glUniform1i(uAlphaMode_, 0);
        glUniform1f(uAlphaCutoff_, 0.5f);
        glUniform1i(uHasBaseColorTex_, 0);
        glUniform1i(uHasMetalRoughTex_, 0);
        glUniform1i(uHasNormalTex_, 0);
        glUniform1i(uHasEmissiveTex_, 0);
        glUniform1i(uBaseColorTexIsUdim_, 0);
        glUniform1i(uMetalRoughTexIsUdim_, 0);
        glUniform1i(uNormalTexIsUdim_, 0);
        glUniform1i(uEmissiveTexIsUdim_, 0);
      } else {
        glUniform3fv(uBaseColor_, 1, mat.baseColor);
        glUniform1f(uMetallic_, mat.metallic);
        glUniform1f(uRoughness_, mat.roughness);
        glUniform3fv(uEmissive_, 1, mat.emissive);
        glUniform1f(uAlpha_, mat.alpha);
        glUniform1i(uAlphaMode_, mat.alphaMode);
        glUniform1f(uAlphaCutoff_, mat.alphaCutoff);
        SetUvUniform(uBaseColorUv0_, uBaseColorUv1_, mat.baseColorSample.uv);
        SetUvUniform(uMetalRoughUv0_, uMetalRoughUv1_, mat.metalRoughSample.uv);
        SetUvUniform(uNormalUv0_, uNormalUv1_, mat.normalSample.uv);
        SetUvUniform(uEmissiveUv0_, uEmissiveUv1_, mat.emissiveSample.uv);
        {
          const GLint uvSets[4] = {mat.baseColorSample.uvSet,
                                   mat.metalRoughSample.uvSet,
                                   mat.normalSample.uvSet,
                                   mat.emissiveSample.uvSet};
          glUniform4iv(uUvSet_, 1, uvSets);
        }
        glUniform4fv(uBaseColorTexScale_, 1, mat.baseColorSample.scale);
        glUniform4fv(uBaseColorTexBias_, 1, mat.baseColorSample.bias);
        glUniform4fv(uNormalTexScale_, 1, mat.normalSample.scale);
        glUniform4fv(uNormalTexBias_, 1, mat.normalSample.bias);
        glUniform4fv(uEmissiveTexScale_, 1, mat.emissiveSample.scale);
        glUniform4fv(uEmissiveTexBias_, 1, mat.emissiveSample.bias);
        glUniform1i(uMetallicChannel_, mat.metallicChannel);
        glUniform1i(uRoughnessChannel_, mat.roughnessChannel);
        glUniform1f(uMetallicTexScale_, mat.metallicTexScale);
        glUniform1f(uMetallicTexBias_, mat.metallicTexBias);
        glUniform1f(uRoughnessTexScale_, mat.roughnessTexScale);
        glUniform1f(uRoughnessTexBias_, mat.roughnessTexBias);
        auto bindMaterialTexture = [&](int slot, GLenum texUnit2D,
                                       GLenum texUnitArray, GLenum texUnitLut,
                                       GLint hasLoc, GLint isUdimLoc) {
          const GLTexture* tex = slotGpuTex(slot);
          const bool hasTex = tex != nullptr;
          const bool isUdim =
              hasTex && tex->isUdim && tex->arrayTex && tex->lutTex;
          glUniform1i(hasLoc, hasTex ? 1 : 0);
          glUniform1i(isUdimLoc, isUdim ? 1 : 0);
          glActiveTexture(texUnit2D);
          glBindTexture(GL_TEXTURE_2D,
                        (hasTex && tex->tex2d) ? tex->tex2d : whiteTex_);
          if (isUdim) {
            glActiveTexture(texUnitArray);
            glBindTexture(GL_TEXTURE_2D_ARRAY, tex->arrayTex);
            glActiveTexture(texUnitLut);
            glBindTexture(GL_TEXTURE_1D, tex->lutTex);
          }
        };
        bindMaterialTexture(mat.baseColorTex, GL_TEXTURE0, GL_TEXTURE11,
                            GL_TEXTURE12, uHasBaseColorTex_,
                            uBaseColorTexIsUdim_);
        bindMaterialTexture(mat.metalRoughTex, GL_TEXTURE1, GL_TEXTURE13,
                            GL_TEXTURE14, uHasMetalRoughTex_,
                            uMetalRoughTexIsUdim_);
        bindMaterialTexture(mat.normalTex, GL_TEXTURE2, GL_TEXTURE15,
                            GL_TEXTURE16, uHasNormalTex_,
                            uNormalTexIsUdim_);
        bindMaterialTexture(mat.emissiveTex, GL_TEXTURE3, GL_TEXTURE17,
                            GL_TEXTURE18, uHasEmissiveTex_,
                            uEmissiveTexIsUdim_);
        glActiveTexture(GL_TEXTURE0);
      }
      glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sub.indexCount), GL_UNSIGNED_INT,
                     (void*)(static_cast<uintptr_t>(sub.indexOffset) * sizeof(uint32_t)));
    }
  }

  // Instanced pass: PointInstancer prototypes drawn with glDrawElementsInstanced.
  // Flat-shaded (default gray material, hardcoded headlight in the fragment
  // shader); each instance's model matrix comes from vertex attribs 3-5, color
  // from attrib 9, opacity from attrib 11. Batches with any instance opacity < 1
  // draw in the translucent pass; the rest draw in opaque/All.
  bool anyInstanced = false;
  for (const GLMesh& m : meshes_) {
    if (m.instanceCount > 0) { anyInstanced = true; break; }
  }
  if (anyInstanced && instProgram_) {
    light3d::Mat4 VP = P * V;
    glUseProgram(instProgram_);
    glUniformMatrix4fv(iUViewProj_, 1, GL_FALSE, VP.m);
    glUniform3fv(iCameraPos_, 1, params.cameraPos);
    glUniform3fv(iLightDir_, 1, params.lightDir);
    glUniform1i(iHasIbl_, iblActive_ ? 1 : 0);
    if (iblActive_) {
      glUniform3fv(iIblColor_, 1, iblColor_);
      glUniformMatrix3fv(iEnvRotation_, 1, GL_FALSE, iblRotation_);
    }
    glUniform3fv(iLightColor_, 1, params.lightColor);
    const float black[3] = {0.0f, 0.0f, 0.0f};
    glUniform3fv(iEmissive_, 1, overrideEmissive ? overrideEmissive : black);
    glUniform1i(iRenderMode_, static_cast<int>(params.mode));
    glUniform1f(iDepthScale_, params.depthScale > 1e-4f ? params.depthScale : 1.0f);
    glUniform3fv(iSceneMin_, 1, params.sceneMin);
    glUniform3fv(iSceneExtent_, 1, params.sceneExtent);
    // Scene-wide bone texture (unit 4), shared with the mesh program.
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, boneTex_ ? boneTex_ : whiteTex_);
    glActiveTexture(GL_TEXTURE0);
    cullState = -1;  // reset across the program switch; dedup within this pass
    for (size_t mi = 0; mi < meshes_.size(); ++mi) {
      if (params.meshVisible && mi < static_cast<size_t>(params.meshVisibleCount) &&
          !params.meshVisible[mi]) {
        continue;
      }
      const GLMesh& mesh = meshes_[mi];
      if (mesh.instanceCount <= 0 || mesh.drawInstanceCount <= 0) continue;
      if (alphaPass == AlphaPass::Opaque && mesh.hasTranslucentInstances) continue;
      if (alphaPass == AlphaPass::Translucent && !mesh.hasTranslucentInstances) continue;
      glUniform1i(iMeshId_, static_cast<int>(mi));
      glUniform1i(iGeometricNormal_, mesh.geometricNormal ? 1 : 0);
      glUniform1i(iDoubleSided_, mesh.doubleSided ? 1 : 0);
      glUniform1i(iPurpose_, mesh.purposeId);
      glUniform1i(iKind_, mesh.kindId);
      // Skeletal skinning of the PROTOTYPE (all its instances share one pose).
      // The bone texture is scene-wide and stays bound for the whole pass; only
      // the enable flag is per-draw. Unskinned prototypes carry zero weights, so
      // the shader would pass them through even without this, but skip the fetch.
      const bool instSkinOn = mesh.skinned && skinningFrameEnabled_;
      glUniform1i(iSkinningEnabled_, instSkinOn ? 1 : 0);
      glUniform1i(iBoneTexWidth_, boneTexWidth_ > 0 ? boneTexWidth_ : 4);
      glUniform1i(iBoneMatrixCount_, boneMatrixCount_);
      if (!mesh.jointVbo) {  // no skin attrs: constant zero weights
        glVertexAttribI4ui(6, 0, 0, 0, 0);
        glVertexAttrib4f(7, 0.0f, 0.0f, 0.0f, 0.0f);
      }
      // Only touch cull state on a transition: across tens of thousands of
      // prototypes the back-face cull flag almost never changes, so the per-draw
      // glEnable/glDisable/glCullFace was pure redundant driver traffic.
      const int wantCull = (mesh.doubleSided || wireframe) ? 0 : 1;
      if (wantCull != cullState) {
        if (wantCull) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }
        else glDisable(GL_CULL_FACE);
        cullState = wantCull;
      }
      glBindVertexArray(mesh.vao);
      // Constant per-draw color when there is no per-instance color array (the
      // generic vertex-attribute value feeds aColor for every instance).
      if (!mesh.hasInstanceColors) glVertexAttrib3fv(9, mesh.flatColor);
      if (!mesh.hasInstanceOpacities) glVertexAttrib1f(11, mesh.flatOpacity);
      // Default per-vertex color to white when the prototype has none (the VAO
      // supplies attrib 10 from vertexColorVbo otherwise).
      if (!mesh.vertexColorVbo) glVertexAttrib3f(10, 1.0f, 1.0f, 1.0f);
      // GPU blendshape morph for a morphed prototype: bind its delta/coeff/chan
      // texture-buffers (units 8/9/10, matching the instanced program's samplers)
      // and enable the in-shader morph. Same as the non-instanced material pass.
      glUniform1i(iHasMorph_, mesh.hasMorph ? 1 : 0);
      if (mesh.hasMorph) {
        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_BUFFER, mesh.morphDeltaTex);
        glActiveTexture(GL_TEXTURE9);
        glBindTexture(GL_TEXTURE_BUFFER, mesh.morphCoeffTex);
        glActiveTexture(GL_TEXTURE10);
        glBindTexture(GL_TEXTURE_BUFFER, mesh.morphChanTex);
        glActiveTexture(GL_TEXTURE0);
      }
      for (const auto& sub : mesh.submeshes) {
        glDrawElementsInstanced(
            GL_TRIANGLES, static_cast<GLsizei>(sub.indexCount), GL_UNSIGNED_INT,
            (void*)(static_cast<uintptr_t>(sub.indexOffset) * sizeof(uint32_t)),
            mesh.drawInstanceCount);
      }
    }
    // LOD box proxies (optimization B): one instanced draw of the shared unit cube
    // for every distant instance the cull collapsed. Flat geometric-normal shading;
    // tint from the per-instance color (attrib 9), white per-vertex color.
    if (boxProxyCount_ > 0 && alphaPass != AlphaPass::Translucent) {
      glUniform1i(iMeshId_, -1);
      glUniform1i(iGeometricNormal_, 1);
      glUniform1i(iDoubleSided_, 0);
      glUniform1i(iPurpose_, 0);
      glUniform1i(iKind_, 0);
      glUniform1i(iHasMorph_, 0);
      if (cullState != 1) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        cullState = 1;
      }
      glBindVertexArray(boxProxyVao_);
      glVertexAttrib3f(1, 0.0f, 1.0f, 0.0f);    // constant normal (FS uses geometric)
      glVertexAttribI2ui(8, 0u, 0u);            // no morph
      glVertexAttrib3f(10, 1.0f, 1.0f, 1.0f);   // white per-vertex color (tint=attr 9)
      glVertexAttrib1f(11, 1.0f);               // box proxies are opaque
      glDrawElementsInstanced(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr,
                              static_cast<GLsizei>(boxProxyCount_));
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
  glUniform3fv(uLightDir_, 1, params.lightDir);
  glUniform3fv(uLightColor_, 1, params.lightColor);
  glUniform1i(uHasIbl_, iblActive_ ? 1 : 0);
  if (iblActive_) {
    glUniform3fv(uIblColor_, 1, iblColor_);
    glUniformMatrix3fv(uEnvRotation_, 1, GL_FALSE, iblRotation_);
    glUniform1i(uPrefilteredLods_, iblSpecLods_);
    glActiveTexture(GL_TEXTURE19);
    glBindTexture(GL_TEXTURE_CUBE_MAP, iblIrrTex_);
    glActiveTexture(GL_TEXTURE20);
    glBindTexture(GL_TEXTURE_CUBE_MAP, iblSpecTex_);
    glActiveTexture(GL_TEXTURE21);
    glBindTexture(GL_TEXTURE_2D, iblLutTex_);
    glActiveTexture(GL_TEXTURE0);
  }

  // Resolve the wireframe state: explicit params.wireMode wins; the legacy
  // RenderMode::Wireframe maps to "wireframe only".
  int wireMode = params.wireMode;
  if (wireMode == 0 && params.mode == RenderMode::Wireframe) wireMode = 1;
  const bool haveWire = (wireProgram_ != 0 || wireInstProgram_ != 0);
  const bool wire = (wireMode != 0);  // disables back-face cull in fill passes
  const float wireCol[3] = {0.75f, 0.85f, 0.95f};  // cool light gray

  if (wireMode == 1 && haveWire) {
    // Wireframe only (hidden-line removed): render the fill into DEPTH ONLY so
    // occluded edges are hidden, then draw the polygon edges over the background.
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    drawMeshes(params, /*wireframe=*/false, nullptr);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    drawWireframe(params, wireCol);
  } else {
    // Shaded fill. In the lit mode (RenderMode 0), draw opaque geometry first,
    // then translucent (Blend) materials in a separate back-to-front, blended,
    // depth-write-off pass so transparency composites over what's behind it.
    // AOV / debug modes draw everything in one unblended pass (blending would
    // corrupt the encoded AOV values).
    const bool shaded = (static_cast<int>(params.mode) == 0);
    if (shaded) {
      drawMeshes(params, /*wireframe=*/false, nullptr, AlphaPass::Opaque);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glDepthMask(GL_FALSE);
      drawMeshes(params, /*wireframe=*/false, nullptr, AlphaPass::Translucent);
      glDepthMask(GL_TRUE);
      glDisable(GL_BLEND);
    } else {
      drawMeshes(params, /*wireframe=*/false, nullptr, AlphaPass::All);
    }
    if (wireMode == 2 && haveWire) drawWireframe(params, wireCol);
  }

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
    glUniform1i(uAlphaMode_, 0);
    glUniform1f(uAlphaCutoff_, 0.5f);
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

bool GLRenderer::gpuMemoryMB(size_t* usedMB, size_t* totalMB) const {
  // GL_NVX_gpu_memory_info (NVIDIA, and exposed by Mesa for AMD/Intel): total
  // dedicated VRAM + currently-available; used = total - available.
  constexpr GLenum kDedicated = 0x9047;  // GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX
  constexpr GLenum kAvailable = 0x9049;  // GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
  GLint totalKB = 0, availKB = 0;
  while (glGetError() != GL_NO_ERROR) {}  // clear prior errors
  glGetIntegerv(kDedicated, &totalKB);
  glGetIntegerv(kAvailable, &availKB);
  if (glGetError() != GL_NO_ERROR || totalKB <= 0) return false;
  if (totalMB) *totalMB = static_cast<size_t>(totalKB) / 1024u;
  if (usedMB) {
    const GLint usedKB = totalKB > availKB ? (totalKB - availKB) : 0;
    *usedMB = static_cast<size_t>(usedKB) / 1024u;
  }
  return true;
}

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
  if (wireProgram_) { glDeleteProgram(wireProgram_); wireProgram_ = 0; }
  if (wireInstProgram_) { glDeleteProgram(wireInstProgram_); wireInstProgram_ = 0; }
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
