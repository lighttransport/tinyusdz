// SPDX-License-Identifier: Apache-2.0
#include "mesh_build.hh"

#include "displacement_bake.hh"
#include "lightrt_mtlx_bridge.hh"
#include "texture_tools.hh"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <unordered_map>

#include "light3d/math.h"
#include "skinning.hh"  // InbetweenSamples, CollectBlendShapeInbetweens
#include "stage.hh"
#include "core/prim.hh"
#include "usdGeom.hh"
#include "tydra/render-data-converter.hh"
#include "tydra/render-data-shader.hh"
#include "tydra/texture-util.hh"

namespace tusdview {

namespace tydra = tinyusdz::tydra;
using tinyusdz::value::matrix4d;

namespace {

constexpr int kInfluenceTexWidth = 1024;

const char* PurposeName(tinyusdz::Purpose purpose) {
  switch (purpose) {
    case tinyusdz::Purpose::Render: return "render";
    case tinyusdz::Purpose::Proxy: return "proxy";
    case tinyusdz::Purpose::Guide: return "guide";
    case tinyusdz::Purpose::Default:
    default: return "default";
  }
}

bool AuthoredPurpose(const tinyusdz::Prim& prim, std::string* out) {
  if (const auto* mesh = prim.as<tinyusdz::GeomMesh>()) {
    if (mesh->purpose.authored()) {
      *out = PurposeName(mesh->purpose.get_value());
      return true;
    }
  }
  if (const auto* xform = prim.as<tinyusdz::Xform>()) {
    if (xform->purpose.authored()) {
      *out = PurposeName(xform->purpose.get_value());
      return true;
    }
  }
  return false;
}

std::string ResolveInheritedPurpose(const tinyusdz::Stage& stage,
                                    const std::string& absPath) {
  std::string path = absPath;
  while (!path.empty()) {
    const tinyusdz::Prim* prim = nullptr;
    std::string err;
    if (stage.find_prim_at_path(tinyusdz::Path(path, ""), prim, &err) && prim) {
      std::string purpose;
      if (AuthoredPurpose(*prim, &purpose)) return purpose;
    }
    if (path == "/") break;
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) {
      path = "/";
    } else {
      path.resize(slash);
    }
  }
  return "default";
}

// USD `kind` is authored on the model prim (component/group/assembly), usually an
// ancestor of the mesh, so walk up to the nearest prim carrying a kind.
int ResolveInheritedKind(const tinyusdz::Stage& stage, const std::string& absPath) {
  std::string path = absPath;
  while (!path.empty()) {
    const tinyusdz::Prim* prim = nullptr;
    std::string err;
    if (stage.find_prim_at_path(tinyusdz::Path(path, ""), prim, &err) && prim) {
      if (prim->metas().has_kind()) return KindId(prim->metas().get_kind_str());
    }
    if (path == "/") break;
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) {
      path = "/";
    } else {
      path.resize(slash);
    }
  }
  return 0;
}

// USD value::matrix4d is row-major (row-vector, pre-multiply: p' = p*M).
// light3d::Mat4 is column-major (column-vector: p' = M*p). For the same
// geometric transform M_gl = transpose(M_usd); combined with the storage-order
// difference this reduces to an element-wise copy: out[i*4+j] = M.m[i][j].
void MatToColMajor(const matrix4d& M, float out[16]) {
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      out[i * 4 + j] = static_cast<float>(M.m[i][j]);
    }
  }
}

float HalfToFloat(uint16_t h);

size_t VertexAttributeComponentCount(tydra::VertexAttributeFormat format) {
  switch (format) {
    case tydra::VertexAttributeFormat::Bool:
    case tydra::VertexAttributeFormat::Char:
    case tydra::VertexAttributeFormat::Byte:
    case tydra::VertexAttributeFormat::Short:
    case tydra::VertexAttributeFormat::Ushort:
    case tydra::VertexAttributeFormat::Half:
    case tydra::VertexAttributeFormat::Float:
    case tydra::VertexAttributeFormat::Int:
    case tydra::VertexAttributeFormat::Uint:
    case tydra::VertexAttributeFormat::Double:
      return 1;
    case tydra::VertexAttributeFormat::Char2:
    case tydra::VertexAttributeFormat::Byte2:
    case tydra::VertexAttributeFormat::Short2:
    case tydra::VertexAttributeFormat::Ushort2:
    case tydra::VertexAttributeFormat::Half2:
    case tydra::VertexAttributeFormat::Vec2:
    case tydra::VertexAttributeFormat::Ivec2:
    case tydra::VertexAttributeFormat::Uvec2:
    case tydra::VertexAttributeFormat::Dvec2:
      return 2;
    case tydra::VertexAttributeFormat::Char3:
    case tydra::VertexAttributeFormat::Byte3:
    case tydra::VertexAttributeFormat::Short3:
    case tydra::VertexAttributeFormat::Ushort3:
    case tydra::VertexAttributeFormat::Half3:
    case tydra::VertexAttributeFormat::Vec3:
    case tydra::VertexAttributeFormat::Ivec3:
    case tydra::VertexAttributeFormat::Uvec3:
    case tydra::VertexAttributeFormat::Dvec3:
      return 3;
    case tydra::VertexAttributeFormat::Char4:
    case tydra::VertexAttributeFormat::Byte4:
    case tydra::VertexAttributeFormat::Short4:
    case tydra::VertexAttributeFormat::Ushort4:
    case tydra::VertexAttributeFormat::Half4:
    case tydra::VertexAttributeFormat::Vec4:
    case tydra::VertexAttributeFormat::Ivec4:
    case tydra::VertexAttributeFormat::Uvec4:
    case tydra::VertexAttributeFormat::Dvec4:
    case tydra::VertexAttributeFormat::Mat2:
    case tydra::VertexAttributeFormat::Dmat2:
      return 4;
    case tydra::VertexAttributeFormat::Mat3:
    case tydra::VertexAttributeFormat::Dmat3:
      return 9;
    case tydra::VertexAttributeFormat::Mat4:
    case tydra::VertexAttributeFormat::Dmat4:
      return 16;
    default:
      return 0;
  }
}

// Read up to `n` numeric components of vertex item `item` as floats.
// Missing/out-of-range components are zero.
void ReadFloats(const tydra::VertexAttribute& a, size_t item, int n, float* out) {
  for (int k = 0; k < n; ++k) out[k] = 0.0f;
  if (a.empty()) return;
  const size_t comps = VertexAttributeComponentCount(a.format);
  if (comps == 0) return;
  if (item >= a.vertex_count()) return;
  const uint8_t* raw = reinterpret_cast<const uint8_t*>(a.buffer());
  if (!raw) return;
  const size_t base = item * comps;
  const int m = std::min<int>(n, static_cast<int>(comps));
  for (int k = 0; k < m; ++k) {
    const size_t idx = base + static_cast<size_t>(k);
    switch (a.format) {
      case tydra::VertexAttributeFormat::Bool:
        out[k] = raw[idx] ? 1.0f : 0.0f;
        break;
      case tydra::VertexAttributeFormat::Char:
      case tydra::VertexAttributeFormat::Char2:
      case tydra::VertexAttributeFormat::Char3:
      case tydra::VertexAttributeFormat::Char4:
        out[k] = static_cast<float>(reinterpret_cast<const int8_t*>(raw)[idx]);
        break;
      case tydra::VertexAttributeFormat::Byte:
      case tydra::VertexAttributeFormat::Byte2:
      case tydra::VertexAttributeFormat::Byte3:
      case tydra::VertexAttributeFormat::Byte4:
        out[k] = static_cast<float>(reinterpret_cast<const uint8_t*>(raw)[idx]);
        break;
      case tydra::VertexAttributeFormat::Short:
      case tydra::VertexAttributeFormat::Short2:
      case tydra::VertexAttributeFormat::Short3:
      case tydra::VertexAttributeFormat::Short4:
        out[k] = static_cast<float>(reinterpret_cast<const int16_t*>(raw)[idx]);
        break;
      case tydra::VertexAttributeFormat::Ushort:
      case tydra::VertexAttributeFormat::Ushort2:
      case tydra::VertexAttributeFormat::Ushort3:
      case tydra::VertexAttributeFormat::Ushort4:
        out[k] = static_cast<float>(reinterpret_cast<const uint16_t*>(raw)[idx]);
        break;
      case tydra::VertexAttributeFormat::Half:
      case tydra::VertexAttributeFormat::Half2:
      case tydra::VertexAttributeFormat::Half3:
      case tydra::VertexAttributeFormat::Half4:
        out[k] = HalfToFloat(reinterpret_cast<const uint16_t*>(raw)[idx]);
        break;
      case tydra::VertexAttributeFormat::Float:
      case tydra::VertexAttributeFormat::Vec2:
      case tydra::VertexAttributeFormat::Vec3:
      case tydra::VertexAttributeFormat::Vec4:
      case tydra::VertexAttributeFormat::Mat2:
      case tydra::VertexAttributeFormat::Mat3:
      case tydra::VertexAttributeFormat::Mat4:
        out[k] = reinterpret_cast<const float*>(raw)[idx];
        break;
      case tydra::VertexAttributeFormat::Int:
      case tydra::VertexAttributeFormat::Ivec2:
      case tydra::VertexAttributeFormat::Ivec3:
      case tydra::VertexAttributeFormat::Ivec4:
        out[k] = static_cast<float>(reinterpret_cast<const int32_t*>(raw)[idx]);
        break;
      case tydra::VertexAttributeFormat::Uint:
      case tydra::VertexAttributeFormat::Uvec2:
      case tydra::VertexAttributeFormat::Uvec3:
      case tydra::VertexAttributeFormat::Uvec4:
        out[k] = static_cast<float>(reinterpret_cast<const uint32_t*>(raw)[idx]);
        break;
      case tydra::VertexAttributeFormat::Double:
      case tydra::VertexAttributeFormat::Dvec2:
      case tydra::VertexAttributeFormat::Dvec3:
      case tydra::VertexAttributeFormat::Dvec4:
      case tydra::VertexAttributeFormat::Dmat2:
      case tydra::VertexAttributeFormat::Dmat3:
      case tydra::VertexAttributeFormat::Dmat4:
        out[k] = static_cast<float>(reinterpret_cast<const double*>(raw)[idx]);
        break;
      default:
        return;
    }
  }
}

bool AttrUsableAsVertex(const tydra::VertexAttribute& a, size_t vertexCount) {
  return !a.empty() && a.is_vertex() && a.vertex_count() == vertexCount &&
         VertexAttributeComponentCount(a.format) > 0;
}

void WriteFloat3Attr(const tydra::VertexAttribute& a, size_t srcItem,
                     size_t dstItem, std::vector<float>* out) {
  if (!out || out->empty()) return;
  const size_t dst = dstItem * 3;
  if (dst + 2 >= out->size()) return;
  float v[3] = {0.0f, 0.0f, 0.0f};
  ReadFloats(a, srcItem, 3, v);
  (*out)[dst + 0] = v[0];
  (*out)[dst + 1] = v[1];
  (*out)[dst + 2] = v[2];
}

// Collect mesh_id -> world matrix from the node hierarchy (first occurrence).
void CollectMeshTransforms(const tydra::Node& node,
                           std::unordered_map<int, matrix4d>* out) {
  if (node.nodeType == tydra::NodeType::Mesh && node.id >= 0) {
    out->emplace(node.id, node.global_matrix);
  }
  for (const auto& c : node.children) {
    CollectMeshTransforms(c, out);
  }
}

int MapWrap(tydra::UVTexture::WrapMode w) {
  switch (w) {
    case tydra::UVTexture::WrapMode::REPEAT:
      return static_cast<int>(WrapMode::Repeat);
    case tydra::UVTexture::WrapMode::MIRROR:
      return static_cast<int>(WrapMode::Mirror);
    case tydra::UVTexture::WrapMode::CLAMP_TO_BORDER:
      return static_cast<int>(WrapMode::ClampToBorder);
    case tydra::UVTexture::WrapMode::CLAMP_TO_EDGE:
    default:
      return static_cast<int>(WrapMode::ClampToEdge);
  }
}

int TextureChannelIndex(tydra::UVTexture::Channel ch, int fallback) {
  switch (ch) {
    case tydra::UVTexture::Channel::R: return 0;
    case tydra::UVTexture::Channel::G: return 1;
    case tydra::UVTexture::Channel::B: return 2;
    case tydra::UVTexture::Channel::A: return 3;
    case tydra::UVTexture::Channel::RGB:
    case tydra::UVTexture::Channel::RGBA:
    default: return fallback;
  }
}

DrawUvXformCPU MapUvXform(const tydra::UVTexture& uv) {
  DrawUvXformCPU out;
  if (!uv.has_transform2d) return out;
  // Tydra stores UsdTransform2d as a 3x3 transform. Keep the affine portion and
  // use it consistently in the preview shaders and CPU RT sampler.
  out.m00 = uv.transform.m[0][0];
  out.m01 = uv.transform.m[0][1];
  out.m10 = uv.transform.m[1][0];
  out.m11 = uv.transform.m[1][1];
  out.tx = uv.transform.m[2][0];
  out.ty = uv.transform.m[2][1];
  return out;
}

void CopyTexSample(const tydra::UVTexture& uv, DrawTexSampleCPU* out) {
  if (!out) return;
  out->uv = MapUvXform(uv);
  for (int i = 0; i < 4; ++i) {
    out->scale[i] = uv.scale[i];
    out->bias[i] = uv.bias[i];
  }
}

void CopyTexSample(const tydra::RenderScene& rs, int texId,
                   DrawTexSampleCPU* out) {
  if (texId < 0 || static_cast<size_t>(texId) >= rs.textures.size()) return;
  CopyTexSample(rs.textures[static_cast<size_t>(texId)], out);
}

DrawUvXformCPU MapUvXform(const tydra::RenderScene& rs, int texId) {
  if (texId < 0 || static_cast<size_t>(texId) >= rs.textures.size()) {
    return DrawUvXformCPU{};
  }
  return MapUvXform(rs.textures[static_cast<size_t>(texId)]);
}

bool IsSrgb(tydra::ColorSpace cs) {
  return cs == tydra::ColorSpace::sRGB || cs == tydra::ColorSpace::sRGB_Texture ||
         cs == tydra::ColorSpace::sRGB_DisplayP3;
}

bool MeshHasSkinData(const tydra::RenderMesh& mesh, size_t pointCount) {
  const auto& jw = mesh.joint_and_weights;
  if (mesh.skel_id < 0 || jw.elementSize < 1) return false;
  const size_t infl = static_cast<size_t>(jw.elementSize);
  return jw.jointIndices.size() == pointCount * infl &&
         jw.jointWeights.size() == pointCount * infl;
}

void SetIdentity4(float out[16]) {
  matrix4d ident = matrix4d::identity();
  MatToColMajor(ident, out);
}

void WriteSkinVertex(const tydra::RenderMesh& mesh, size_t srcPoint,
                     size_t dstVertex, DrawMeshCPU* dm) {
  if (!dm || dm->jointIdx.empty() || dm->jointWt.empty()) return;
  const auto& jw = mesh.joint_and_weights;
  const size_t infl = static_cast<size_t>(jw.elementSize);
  if (srcPoint >= mesh.points.size()) return;
  const size_t src = srcPoint * infl;
  if (src + infl > jw.jointIndices.size() || src + infl > jw.jointWeights.size()) {
    return;
  }
  std::array<std::pair<float, uint32_t>, 4> top{};
  for (auto& v : top) v = {0.0f, 0u};
  for (size_t k = 0; k < infl; ++k) {
    const float w = jw.jointWeights[src + k];
    const int ji = jw.jointIndices[src + k];
    if (w <= 0.0f || ji < 0) continue;
    const uint32_t j = static_cast<uint32_t>(ji);
    for (size_t slot = 0; slot < top.size(); ++slot) {
      if (w > top[slot].first) {
        for (size_t m = top.size() - 1; m > slot; --m) top[m] = top[m - 1];
        top[slot] = {w, j};
        break;
      }
    }
  }
  float sum = 0.0f;
  for (const auto& v : top) sum += v.first;
  const size_t dst = dstVertex * 4;
  for (size_t k = 0; k < 4; ++k) {
    dm->jointIdx[dst + k] = top[k].second;
    dm->jointWt[dst + k] = (sum > 0.0f) ? (top[k].first / sum) : 0.0f;
  }
}

void WriteSkinInfluenceVertex(const tydra::RenderMesh& mesh, size_t srcPoint,
                              size_t dstVertex, DrawMeshCPU* dm) {
  if (!dm || dm->influenceOffsetCount.empty()) return;
  const auto& jw = mesh.joint_and_weights;
  const size_t infl = static_cast<size_t>(jw.elementSize);
  if (srcPoint >= mesh.points.size()) return;
  const size_t src = srcPoint * infl;
  if (src + infl > jw.jointIndices.size() || src + infl > jw.jointWeights.size()) {
    return;
  }

  const uint32_t offset = static_cast<uint32_t>(dm->influenceTexels.size() / 4);
  double sum = 0.0;
  for (size_t k = 0; k < infl; ++k) {
    const float w = jw.jointWeights[src + k];
    const int ji = jw.jointIndices[src + k];
    if (w <= 0.0f || ji < 0 || !std::isfinite(w)) continue;
    dm->influenceTexels.push_back(static_cast<float>(ji));
    dm->influenceTexels.push_back(w);
    dm->influenceTexels.push_back(0.0f);
    dm->influenceTexels.push_back(0.0f);
    sum += static_cast<double>(w);
  }

  uint32_t count = static_cast<uint32_t>(dm->influenceTexels.size() / 4) - offset;
  if (sum > 0.0) {
    const float inv = static_cast<float>(1.0 / sum);
    for (uint32_t i = 0; i < count; ++i) {
      dm->influenceTexels[(static_cast<size_t>(offset + i) * 4) + 1] *= inv;
    }
  } else {
    count = 0;
  }

  const size_t dst = dstVertex * 2;
  dm->influenceOffsetCount[dst + 0] = offset;
  dm->influenceOffsetCount[dst + 1] = count;
  dm->maxInfluencesPerVertex = std::max(dm->maxInfluencesPerVertex,
                                        static_cast<int>(count));
}

void FinalizeInfluenceTexture(DrawMeshCPU* dm) {
  if (!dm || dm->influenceTexels.empty()) return;
  const size_t texels = dm->influenceTexels.size() / 4;
  dm->influenceTexWidth = kInfluenceTexWidth;
  dm->influenceTexHeight =
      static_cast<int>((texels + static_cast<size_t>(kInfluenceTexWidth) - 1) /
                       static_cast<size_t>(kInfluenceTexWidth));
  const size_t padded =
      static_cast<size_t>(dm->influenceTexWidth) *
      static_cast<size_t>(dm->influenceTexHeight) * 4;
  dm->influenceTexels.resize(padded, 0.0f);
}

// IEEE 754 binary16 -> float32 (EXR half texel buffers).
float HalfToFloat(uint16_t h) {
  const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
  uint32_t exp = (h >> 10) & 0x1Fu;
  uint32_t man = h & 0x3FFu;
  uint32_t bits;
  if (exp == 0) {
    if (man == 0) {
      bits = sign;  // +-0
    } else {
      exp = 127 - 15 + 1;
      while (!(man & 0x400u)) {
        man <<= 1;
        --exp;
      }
      man &= 0x3FFu;
      bits = sign | (exp << 23) | (man << 13);
    }
  } else if (exp == 31) {
    bits = sign | 0x7F800000u | (man << 13);  // inf/nan
  } else {
    bits = sign | ((exp - 15 + 127) << 23) | (man << 13);
  }
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}

// Decode a TextureImage's buffer into an RGBA8 light3d::Image. Returns false if
// the image cannot be decoded (caller skips it).
bool DecodeToRGBA8(const tydra::RenderScene& rs, const tydra::TextureImage& img,
                   light3d::Image* out) {
  if (img.buffer_id < 0 ||
      static_cast<size_t>(img.buffer_id) >= rs.buffers.size()) {
    return false;
  }
  if (!img.decoded || img.width <= 0 || img.height <= 0 || img.channels <= 0) {
    return false;
  }
  const tydra::BufferData& buf = rs.buffers[static_cast<size_t>(img.buffer_id)];
  const size_t w = static_cast<size_t>(img.width);
  const size_t h = static_cast<size_t>(img.height);
  const size_t ch = static_cast<size_t>(img.channels);
  const size_t npix = w * h;

  out->width = img.width;
  out->height = img.height;
  out->channels = 4;
  out->data.assign(npix * 4, 255);

  auto store = [&](size_t pix, float r, float g, float b, float a) {
    auto clamp8 = [](float v) -> uint8_t {
      if (v < 0.0f) v = 0.0f;
      if (v > 255.0f) v = 255.0f;
      return static_cast<uint8_t>(v + 0.5f);
    };
    out->data[pix * 4 + 0] = clamp8(r);
    out->data[pix * 4 + 1] = clamp8(g);
    out->data[pix * 4 + 2] = clamp8(b);
    out->data[pix * 4 + 3] = clamp8(a);
  };

  if (img.texelComponentType == tydra::ComponentType::UInt8) {
    if (buf.data.size() < npix * ch) return false;
    const uint8_t* p = buf.data.data();
    for (size_t i = 0; i < npix; ++i) {
      float c0 = p[i * ch + 0];
      float c1 = ch > 1 ? p[i * ch + 1] : c0;
      float c2 = ch > 2 ? p[i * ch + 2] : c0;
      float c3 = ch > 3 ? p[i * ch + 3] : 255.0f;
      if (ch == 1) { c1 = c0; c2 = c0; c3 = 255.0f; }
      else if (ch == 2) { c2 = c0; c1 = c0; c3 = p[i * ch + 1]; }
      store(i, c0, c1, c2, c3);
    }
    return true;
  }
  if (img.texelComponentType == tydra::ComponentType::UInt16) {
    const size_t need = npix * ch * sizeof(uint16_t);
    if (buf.data.size() < need) return false;
    const uint16_t* p = reinterpret_cast<const uint16_t*>(buf.data.data());
    constexpr float kScale = 255.0f / 65535.0f;
    for (size_t i = 0; i < npix; ++i) {
      float c0 = float(p[i * ch + 0]) * kScale;
      float c1 = ch > 1 ? float(p[i * ch + 1]) * kScale : c0;
      float c2 = ch > 2 ? float(p[i * ch + 2]) * kScale : c0;
      float c3 = ch > 3 ? float(p[i * ch + 3]) * kScale : 255.0f;
      if (ch == 1) { c1 = c0; c2 = c0; c3 = 255.0f; }
      else if (ch == 2) { c2 = c0; c1 = c0; c3 = float(p[i * ch + 1]) * kScale; }
      store(i, c0, c1, c2, c3);
    }
    return true;
  }
  if (img.texelComponentType == tydra::ComponentType::Float) {
    const size_t need = npix * ch * sizeof(float);
    if (buf.data.size() < need) return false;
    const float* p = reinterpret_cast<const float*>(buf.data.data());
    for (size_t i = 0; i < npix; ++i) {
      float c0 = p[i * ch + 0] * 255.0f;
      float c1 = ch > 1 ? p[i * ch + 1] * 255.0f : c0;
      float c2 = ch > 2 ? p[i * ch + 2] * 255.0f : c0;
      float c3 = ch > 3 ? p[i * ch + 3] * 255.0f : 255.0f;
      if (ch == 1) { c1 = c0; c2 = c0; c3 = 255.0f; }
      else if (ch == 2) { c2 = c0; c1 = c0; c3 = p[i * ch + 1] * 255.0f; }
      store(i, c0, c1, c2, c3);
    }
    return true;
  }
  if (img.texelComponentType == tydra::ComponentType::Half) {
    const size_t need = npix * ch * sizeof(uint16_t);
    if (buf.data.size() < need) return false;
    const uint16_t* p = reinterpret_cast<const uint16_t*>(buf.data.data());
    for (size_t i = 0; i < npix; ++i) {
      float c0 = HalfToFloat(p[i * ch + 0]) * 255.0f;
      float c1 = ch > 1 ? HalfToFloat(p[i * ch + 1]) * 255.0f : c0;
      float c2 = ch > 2 ? HalfToFloat(p[i * ch + 2]) * 255.0f : c0;
      float c3 = ch > 3 ? HalfToFloat(p[i * ch + 3]) * 255.0f : 255.0f;
      if (ch == 1) { c1 = c0; c2 = c0; c3 = 255.0f; }
      else if (ch == 2) { c2 = c0; c1 = c0; c3 = HalfToFloat(p[i * ch + 1]) * 255.0f; }
      store(i, c0, c1, c2, c3);
    }
    return true;
  }
  return false;  // unsupported texel type
}

// Decode a TextureImage to float RGB (interleaved, top-down) WITHOUT the 8-bit
// clamp of DecodeToRGBA8 — HDR texels pass through. u8 sources are sRGB-decoded
// when tagged sRGB. Used for the DomeLight IBL bake.
bool DecodeToFloatRGB(const tydra::RenderScene& rs,
                      const tydra::TextureImage& img, int* outW, int* outH,
                      std::vector<float>* out) {
  if (!out || img.buffer_id < 0 ||
      static_cast<size_t>(img.buffer_id) >= rs.buffers.size()) {
    return false;
  }
  if (!img.decoded || img.width <= 0 || img.height <= 0 || img.channels <= 0) {
    return false;
  }
  const tydra::BufferData& buf = rs.buffers[static_cast<size_t>(img.buffer_id)];
  const size_t w = static_cast<size_t>(img.width);
  const size_t h = static_cast<size_t>(img.height);
  const size_t ch = static_cast<size_t>(img.channels);
  const size_t npix = w * h;
  out->resize(npix * 3);

  if (img.texelComponentType == tydra::ComponentType::Float) {
    if (buf.data.size() < npix * ch * sizeof(float)) return false;
    const float* p = reinterpret_cast<const float*>(buf.data.data());
    for (size_t i = 0; i < npix; ++i) {
      const float c0 = p[i * ch + 0];
      (*out)[i * 3 + 0] = c0;
      (*out)[i * 3 + 1] = ch > 1 ? p[i * ch + 1] : c0;
      (*out)[i * 3 + 2] = ch > 2 ? p[i * ch + 2] : c0;
      if (ch == 2) { (*out)[i * 3 + 1] = c0; (*out)[i * 3 + 2] = c0; }
    }
  } else if (img.texelComponentType == tydra::ComponentType::Half) {
    if (buf.data.size() < npix * ch * sizeof(uint16_t)) return false;
    const uint16_t* p = reinterpret_cast<const uint16_t*>(buf.data.data());
    for (size_t i = 0; i < npix; ++i) {
      const float c0 = HalfToFloat(p[i * ch + 0]);
      (*out)[i * 3 + 0] = c0;
      (*out)[i * 3 + 1] = ch > 1 ? HalfToFloat(p[i * ch + 1]) : c0;
      (*out)[i * 3 + 2] = ch > 2 ? HalfToFloat(p[i * ch + 2]) : c0;
      if (ch == 2) { (*out)[i * 3 + 1] = c0; (*out)[i * 3 + 2] = c0; }
    }
  } else if (img.texelComponentType == tydra::ComponentType::UInt8) {
    if (buf.data.size() < npix * ch) return false;
    const bool srgb = IsSrgb(img.colorSpace);
    float lut[256];
    for (int v = 0; v < 256; ++v) {
      const float f = static_cast<float>(v) / 255.0f;
      lut[v] = srgb ? (f <= 0.04045f ? f / 12.92f
                                     : std::pow((f + 0.055f) / 1.055f, 2.4f))
                    : f;
    }
    const uint8_t* p = buf.data.data();
    for (size_t i = 0; i < npix; ++i) {
      const float c0 = lut[p[i * ch + 0]];
      (*out)[i * 3 + 0] = c0;
      (*out)[i * 3 + 1] = ch > 1 ? lut[p[i * ch + 1]] : c0;
      (*out)[i * 3 + 2] = ch > 2 ? lut[p[i * ch + 2]] : c0;
      if (ch == 2) { (*out)[i * 3 + 1] = c0; (*out)[i * 3 + 2] = c0; }
    }
  } else {
    return false;
  }
  if (outW) *outW = img.width;
  if (outH) *outH = img.height;
  return true;
}

bool AddImageTexture(const tydra::RenderScene& rs, int imageId, int wrapS,
                     int wrapT, DrawScene* out, int* drawTexId) {
  if (drawTexId) *drawTexId = -1;
  if (!out || imageId < 0 || static_cast<size_t>(imageId) >= rs.images.size()) {
    return false;
  }
  const tydra::TextureImage& img = rs.images[static_cast<size_t>(imageId)];
  DrawTextureCPU tex;
  if (!DecodeToRGBA8(rs, img, &tex.image)) {
    return false;
  }
  tex.assetIdentifier = img.asset_identifier;
  tex.renderImageId = imageId;
  tex.srgb = IsSrgb(img.colorSpace);
  tex.wrapS = wrapS;
  tex.wrapT = wrapT;
  const int id = static_cast<int>(out->textures.size());
  out->textures.push_back(std::move(tex));
  if (drawTexId) *drawTexId = id;
  return true;
}

size_t TextureBytes(const light3d::Image& img) {
  return static_cast<size_t>(std::max(img.width, 0)) *
         static_cast<size_t>(std::max(img.height, 0)) *
         static_cast<size_t>(std::max(img.channels, 0));
}

size_t TextureBytes(const DrawTextureCPU& tex) {
  if (tex.isUdim && !tex.udimTiles.empty()) {
    size_t total = 0;
    for (const DrawUdimTileCPU& tile : tex.udimTiles) {
      total += TextureBytes(tile.image);
    }
    return total;
  }
  return TextureBytes(tex.image);
}

bool ResizeDrawImage(light3d::Image* img, int dstW, int dstH, bool srgb,
                     std::string* err) {
  if (!img || dstW <= 0 || dstH <= 0 || img->width <= 0 || img->height <= 0) {
    return false;
  }
  if (img->width == dstW && img->height == dstH) return true;
  // Prefer the vendored tir resizer (sRGB-aware, premultiplied-alpha,
  // higher-quality filters); fall back to the Tydra stb-based resize when
  // built without textools or on failure.
  if (TexToolsAvailable() && TexToolsResizeRGBA8(img, dstW, dstH, srgb, err)) {
    return true;
  }
  tinyusdz::Image src;
  src.width = img->width;
  src.height = img->height;
  src.channels = img->channels;
  src.bpp = 8;
  src.format = tinyusdz::Image::PixelFormat::UInt;
  src.data = img->data;
  tinyusdz::Image dst;
  const auto filter =
      srgb ? tydra::ResizeFilter::SRGB : tydra::ResizeFilter::Linear;
  if (!tydra::ResizeImage(src, dstW, dstH, &dst, filter, err)) {
    return false;
  }
  img->width = dst.width;
  img->height = dst.height;
  img->channels = dst.channels;
  img->data = std::move(dst.data);
  return true;
}

void InitUdimLookup(DrawTextureCPU* tex) {
  tex->udimLayer.fill(-1);
  for (size_t i = 0; i < tex->udimTiles.size(); ++i) {
    const uint32_t udim = tex->udimTiles[i].udim;
    if (udim >= 1001 && udim <= 1100) {
      tex->udimLayer[udim - 1001] = static_cast<int>(i);
    }
  }
}

void NormalizeUdimTiles(DrawTextureCPU* tex, bool srgb, DrawScene* out) {
  if (!tex || !tex->isUdim || tex->udimTiles.empty()) return;
  int w = 0;
  int h = 0;
  for (const DrawUdimTileCPU& tile : tex->udimTiles) {
    w = std::max(w, tile.image.width);
    h = std::max(h, tile.image.height);
  }
  if (w <= 0 || h <= 0) return;
  for (DrawUdimTileCPU& tile : tex->udimTiles) {
    if (tile.image.width == w && tile.image.height == h) continue;
    std::string err;
    if (!ResizeDrawImage(&tile.image, w, h, srgb, &err) && out) {
      out->skipped.push_back("UDIM tile resize failed: " + err);
    }
  }
  tex->udimTileWidth = w;
  tex->udimTileHeight = h;
  tex->image = tex->udimTiles.front().image;  // representative fallback.
  InitUdimLookup(tex);
}

#if !defined(TUSDVIEW_WITH_TEXTOOLS)
// Minimal fallback BC1/BC3 encoders, used only when tusdview is built without
// the vendored texcomp library.
uint16_t PackRGB565(uint8_t r, uint8_t g, uint8_t b) {
  return uint16_t(((uint16_t(r) >> 3) << 11) | ((uint16_t(g) >> 2) << 5) |
                  (uint16_t(b) >> 3));
}

void UnpackRGB565(uint16_t c, uint8_t* r, uint8_t* g, uint8_t* b) {
  const uint8_t r5 = uint8_t((c >> 11) & 31);
  const uint8_t g6 = uint8_t((c >> 5) & 63);
  const uint8_t b5 = uint8_t(c & 31);
  *r = uint8_t((r5 << 3) | (r5 >> 2));
  *g = uint8_t((g6 << 2) | (g6 >> 4));
  *b = uint8_t((b5 << 3) | (b5 >> 2));
}

void EncodeBC1Block(const uint8_t rgba[16][4], uint8_t* out) {
  uint8_t minv[3] = {255, 255, 255};
  uint8_t maxv[3] = {0, 0, 0};
  for (int i = 0; i < 16; ++i) {
    for (int c = 0; c < 3; ++c) {
      minv[c] = std::min(minv[c], rgba[i][c]);
      maxv[c] = std::max(maxv[c], rgba[i][c]);
    }
  }
  uint16_t c0 = PackRGB565(maxv[0], maxv[1], maxv[2]);
  uint16_t c1 = PackRGB565(minv[0], minv[1], minv[2]);
  if (c0 < c1) std::swap(c0, c1);
  uint8_t pal[4][3]{};
  UnpackRGB565(c0, &pal[0][0], &pal[0][1], &pal[0][2]);
  UnpackRGB565(c1, &pal[1][0], &pal[1][1], &pal[1][2]);
  for (int c = 0; c < 3; ++c) {
    pal[2][c] = uint8_t((2 * int(pal[0][c]) + int(pal[1][c])) / 3);
    pal[3][c] = uint8_t((int(pal[0][c]) + 2 * int(pal[1][c])) / 3);
  }
  uint32_t bits = 0;
  for (int i = 0; i < 16; ++i) {
    int best = 0;
    int bestErr = std::numeric_limits<int>::max();
    for (int p = 0; p < 4; ++p) {
      int err = 0;
      for (int c = 0; c < 3; ++c) {
        const int d = int(rgba[i][c]) - int(pal[p][c]);
        err += d * d;
      }
      if (err < bestErr) {
        bestErr = err;
        best = p;
      }
    }
    bits |= uint32_t(best) << (2 * i);
  }
  out[0] = uint8_t(c0 & 255);
  out[1] = uint8_t(c0 >> 8);
  out[2] = uint8_t(c1 & 255);
  out[3] = uint8_t(c1 >> 8);
  std::memcpy(out + 4, &bits, 4);
}

void EncodeBC3AlphaBlock(const uint8_t rgba[16][4], uint8_t* out) {
  uint8_t a0 = 0;
  uint8_t a1 = 255;
  for (int i = 0; i < 16; ++i) {
    a0 = std::max(a0, rgba[i][3]);
    a1 = std::min(a1, rgba[i][3]);
  }
  out[0] = a0;
  out[1] = a1;
  uint8_t pal[8]{};
  pal[0] = a0;
  pal[1] = a1;
  if (a0 > a1) {
    for (int i = 1; i <= 6; ++i) {
      pal[i + 1] = uint8_t(((7 - i) * int(a0) + i * int(a1)) / 7);
    }
  } else {
    for (int i = 1; i <= 4; ++i) {
      pal[i + 1] = uint8_t(((5 - i) * int(a0) + i * int(a1)) / 5);
    }
    pal[6] = 0;
    pal[7] = 255;
  }
  uint64_t bits = 0;
  for (int i = 0; i < 16; ++i) {
    int best = 0;
    int bestErr = std::numeric_limits<int>::max();
    for (int p = 0; p < 8; ++p) {
      const int d = int(rgba[i][3]) - int(pal[p]);
      const int err = d * d;
      if (err < bestErr) {
        bestErr = err;
        best = p;
      }
    }
    bits |= uint64_t(best) << (3 * i);
  }
  for (int i = 0; i < 6; ++i) out[2 + i] = uint8_t((bits >> (8 * i)) & 255);
}
#endif  // !TUSDVIEW_WITH_TEXTOOLS

// Resolve a requested compression mode to a concrete block format the device
// can actually sample, given its capabilities. `opaque` only affects the BCn
// (BC1 vs BC3) auto choice; ASTC/ETC2/BC7 carry alpha regardless. Falls back
// gracefully (e.g. Astc on a BC-only desktop GPU -> BC7) and returns None when
// nothing is available so the caller keeps the texture uncompressed.
DrawCompressedFormat ChooseCompressedFormat(TextureCompressionMode mode,
                                            const TextureCompressCaps& caps,
                                            bool opaque) {
  const DrawCompressedFormat bcn =
      opaque ? DrawCompressedFormat::BC1 : DrawCompressedFormat::BC3;
  switch (mode) {
    case TextureCompressionMode::Off:
      return DrawCompressedFormat::None;
    case TextureCompressionMode::BCn:
      if (caps.bc) return bcn;
      if (caps.astc) return DrawCompressedFormat::ASTC_4x4;
      if (caps.etc2) return DrawCompressedFormat::ETC2_RGBA;
      return DrawCompressedFormat::None;
    case TextureCompressionMode::BC7:
      if (caps.bc) return DrawCompressedFormat::BC7;
      if (caps.astc) return DrawCompressedFormat::ASTC_4x4;
      if (caps.etc2) return DrawCompressedFormat::ETC2_RGBA;
      return DrawCompressedFormat::None;
    case TextureCompressionMode::Astc:
      if (caps.astc) return DrawCompressedFormat::ASTC_4x4;
      if (caps.bc) return DrawCompressedFormat::BC7;
      if (caps.etc2) return DrawCompressedFormat::ETC2_RGBA;
      return DrawCompressedFormat::None;
    case TextureCompressionMode::Etc2:
      if (caps.etc2) return DrawCompressedFormat::ETC2_RGBA;
      if (caps.bc) return DrawCompressedFormat::BC7;
      if (caps.astc) return DrawCompressedFormat::ASTC_4x4;
      return DrawCompressedFormat::None;
    case TextureCompressionMode::Auto:
      // Prefer the highest-quality format available on the platform.
      if (caps.bc) return DrawCompressedFormat::BC7;
      if (caps.astc) return DrawCompressedFormat::ASTC_4x4;
      if (caps.etc2) return DrawCompressedFormat::ETC2_RGBA;
      return DrawCompressedFormat::None;
  }
  return DrawCompressedFormat::None;
}

bool EncodeBCn(const light3d::Image& img, bool srgb,
               TextureCompressionMode mode, const TextureCompressCaps& caps,
               DrawCompressedImageCPU* out) {
  if (!out || img.width <= 0 || img.height <= 0 || img.channels != 4 ||
      img.data.empty()) {
    return false;
  }
  bool opaque = true;
  for (size_t i = 3; i < img.data.size(); i += 4) {
    if (img.data[i] < 250) {
      opaque = false;
      break;
    }
  }
#if defined(TUSDVIEW_WITH_TEXTOOLS)
  const DrawCompressedFormat format = ChooseCompressedFormat(mode, caps, opaque);
  if (format == DrawCompressedFormat::None) return false;
  return TexToolsCompress(img, srgb, format, out);
#else
  (void)srgb;
  (void)mode;
  (void)caps;  // ASTC/ETC2/BC7 need the vendored texcomp encoder.
  out->format = opaque ? DrawCompressedFormat::BC1 : DrawCompressedFormat::BC3;
  out->width = img.width;
  out->height = img.height;
  const int bw = (img.width + 3) / 4;
  const int bh = (img.height + 3) / 4;
  const int blockBytes = opaque ? 8 : 16;
  out->data.assign(static_cast<size_t>(bw) * static_cast<size_t>(bh) *
                       static_cast<size_t>(blockBytes),
                   0);
  for (int by = 0; by < bh; ++by) {
    for (int bx = 0; bx < bw; ++bx) {
      uint8_t block[16][4]{};
      for (int y = 0; y < 4; ++y) {
        const int sy = std::min(by * 4 + y, img.height - 1);
        for (int x = 0; x < 4; ++x) {
          const int sx = std::min(bx * 4 + x, img.width - 1);
          const uint8_t* src =
              img.data.data() + (static_cast<size_t>(sy) * img.width + sx) * 4;
          std::memcpy(block[y * 4 + x], src, 4);
        }
      }
      uint8_t* dst = out->data.data() +
                     (static_cast<size_t>(by) * bw + bx) * blockBytes;
      if (opaque) {
        EncodeBC1Block(block, dst);
      } else {
        EncodeBC3AlphaBlock(block, dst);
        EncodeBC1Block(block, dst + 8);
      }
    }
  }
  return true;
#endif  // TUSDVIEW_WITH_TEXTOOLS
}

void CompressTexture(DrawTextureCPU* tex, TextureCompressionMode mode,
                     const TextureCompressCaps& caps) {
  if (!tex) return;
  if (tex->isUdim) {
    for (DrawUdimTileCPU& tile : tex->udimTiles) {
      EncodeBCn(tile.image, tex->srgb, mode, caps, &tile.compressed);
    }
  }
  EncodeBCn(tex->image, tex->srgb, mode, caps, &tex->compressed);
}

void ApplyTextureRuntimeOptions(const TextureRuntimeOptions& opt, DrawScene* out) {
  if (!out) return;
  if (opt.maxTextureSize > 0) {
    for (DrawTextureCPU& tex : out->textures) {
      auto resizeOne = [&](light3d::Image* img) {
        const int longest = std::max(img->width, img->height);
        if (longest <= opt.maxTextureSize || longest <= 0) return;
        const double scale = static_cast<double>(opt.maxTextureSize) /
                             static_cast<double>(longest);
        const int nw =
            std::max(1, static_cast<int>(std::floor(img->width * scale)));
        const int nh =
            std::max(1, static_cast<int>(std::floor(img->height * scale)));
        std::string err;
        if (!ResizeDrawImage(img, nw, nh, tex.srgb, &err)) {
          out->skipped.push_back("texture resize failed: " + err);
        }
      };
      if (tex.isUdim) {
        for (DrawUdimTileCPU& tile : tex.udimTiles) resizeOne(&tile.image);
        NormalizeUdimTiles(&tex, tex.srgb, out);
      } else {
        resizeOne(&tex.image);
      }
    }
  }

  if (opt.textureBudgetMB > 0) {
    size_t total = 0;
    for (const DrawTextureCPU& tex : out->textures) total += TextureBytes(tex);
    const size_t budget =
        static_cast<size_t>(opt.textureBudgetMB) * 1024ull * 1024ull;
    if (budget > 0 && total > budget) {
      const double scale =
          std::sqrt(static_cast<double>(budget) / static_cast<double>(total));
      for (DrawTextureCPU& tex : out->textures) {
        auto resizeOne = [&](light3d::Image* img) {
          const int nw =
              std::max(1, static_cast<int>(std::floor(img->width * scale)));
          const int nh =
              std::max(1, static_cast<int>(std::floor(img->height * scale)));
          std::string err;
          if (!ResizeDrawImage(img, nw, nh, tex.srgb, &err)) {
            out->skipped.push_back("texture budget resize failed: " + err);
          }
        };
        if (tex.isUdim) {
          for (DrawUdimTileCPU& tile : tex.udimTiles) resizeOne(&tile.image);
          NormalizeUdimTiles(&tex, tex.srgb, out);
        } else {
          resizeOne(&tex.image);
        }
      }
    }
  }

  if (opt.compression != TextureCompressionMode::Off) {
    for (DrawTextureCPU& tex : out->textures) {
      if (tex.compressedFinal) continue;  // kept-compressed KTX2 — already final
      tex.requestedCompressed = true;
      CompressTexture(&tex, opt.compression, opt.caps);
    }
  }
}

// Classify texture usage from the built materials, then (with --texture-mips)
// build content-aware CPU mip chains and per-level compressed payloads. Must
// run after BuildDrawMaterials on every load path (one-shot and streaming) so
// both produce identical DrawScenes; texture usage (normal map / ORM packing /
// alpha-tested) is only known once materials exist.
void FinalizeDrawTextures(const TextureRuntimeOptions& opt, DrawScene* out) {
  if (!out) return;
  auto texAt = [&](int idx) -> DrawTextureCPU* {
    if (idx < 0 || static_cast<size_t>(idx) >= out->textures.size()) {
      return nullptr;
    }
    return &out->textures[static_cast<size_t>(idx)];
  };
  for (const DrawMaterialCPU& m : out->materials) {
    if (DrawTextureCPU* t = texAt(m.normalTex)) t->isNormalMap = true;
    if (DrawTextureCPU* t = texAt(m.coatNormalTex)) t->isNormalMap = true;
    if (DrawTextureCPU* t = texAt(m.metalRoughTex)) {
      // Packed ORM map: the roughness channel minifies variance-aware (reduces
      // specular aliasing); other channels keep the filtered average.
      if (m.roughnessChannel >= 0 && m.roughnessChannel < 4) {
        t->channelOp[m.roughnessChannel] = 2;  // TP_CH_ROUGHNESS
      }
    }
    if (m.alphaMode == static_cast<int>(AlphaMode::Mask)) {
      if (DrawTextureCPU* t = texAt(m.baseColorTex)) {
        t->isAlphaTested = true;
        t->alphaCutoff = m.alphaCutoff;
      }
    }
  }
  if (!opt.generateMips || !TexToolsAvailable()) return;
  // Build the content-aware chain for one RGBA8 base image + its compressed
  // per-level payloads (same block format as the base level).
  auto buildOne = [&](const light3d::Image& base, const TexUsage& usage,
                      bool srgb, std::vector<light3d::Image>* mips,
                      DrawCompressedImageCPU* compressed,
                      bool requestedCompressed) -> bool {
    mips->clear();
    if (!TexToolsBuildMips(base, usage, mips)) return false;
    if (requestedCompressed && compressed &&
        compressed->format != DrawCompressedFormat::None) {
      compressed->mips.clear();
      compressed->mips.reserve(mips->size());
      bool ok = true;
      for (const light3d::Image& mip : *mips) {
        DrawCompressedImageCPU c;
        if (!TexToolsCompress(mip, srgb, compressed->format, &c)) {
          ok = false;
          break;
        }
        DrawCompressedMipCPU lvl;
        lvl.width = c.width;
        lvl.height = c.height;
        lvl.data = std::move(c.data);
        compressed->mips.push_back(std::move(lvl));
      }
      if (!ok) compressed->mips.clear();  // base-only upload fallback
    }
    return true;
  };
  for (DrawTextureCPU& tex : out->textures) {
    // Kept-compressed KTX2 passthrough: the compressed payload is final and
    // `image` is empty, so there is nothing to build a mip chain from (the KTX2
    // level 0 is uploaded directly; multi-level KTX2 mips are a follow-up).
    if (tex.compressedFinal) continue;
    TexUsage usage;
    usage.srgb = tex.srgb;
    usage.normalMap = tex.isNormalMap;
    usage.alphaTested = tex.isAlphaTested;
    usage.alphaCutoff = tex.alphaCutoff;
    usage.wrapS = tex.wrapS;
    usage.wrapT = tex.wrapT;
    for (int c = 0; c < 4; ++c) usage.channelOp[c] = tex.channelOp[c];
    if (tex.isUdim) {
      // Per-tile chains (tile dims were equalized by NormalizeUdimTiles, so
      // every tile carries the same level count for the GL/VK array upload).
      bool ok = true;
      for (DrawUdimTileCPU& tile : tex.udimTiles) {
        if (!buildOne(tile.image, usage, tex.srgb, &tile.mipImages,
                      &tile.compressed, tex.requestedCompressed)) {
          ok = false;
          break;
        }
      }
      if (!ok) {
        for (DrawUdimTileCPU& tile : tex.udimTiles) {
          tile.mipImages.clear();
          tile.compressed.mips.clear();
        }
        out->skipped.push_back("UDIM mip build failed: " +
                               tex.assetIdentifier);
      }
    }
    if (!buildOne(tex.image, usage, tex.srgb, &tex.mipImages, &tex.compressed,
                  tex.requestedCompressed)) {
      out->skipped.push_back("texture mip build failed: " +
                             tex.assetIdentifier);
      continue;
    }
  }
}

// --- Shared per-element builders (used by both BuildDrawScene and the
// streaming path so the two produce identical output) ---------------------

// Build the renderable textures (dedup by texture_image_id) and a mapping
// drawTexMap[uvTextureIndex] -> DrawScene texture index (-1 if skipped).
#if defined(TUSDVIEW_WITH_TEXTOOLS)
// Map a tydra block format to the tusdview draw format (UNI is handled via the
// srcIsUni flag, not this map). Returns None for formats with no draw mapping.
static DrawCompressedFormat MapTydraBlockFormat(tydra::TextureBlockFormat f) {
  switch (f) {
    case tydra::TextureBlockFormat::BC1: return DrawCompressedFormat::BC1;
    case tydra::TextureBlockFormat::BC3: return DrawCompressedFormat::BC3;
    case tydra::TextureBlockFormat::BC5: return DrawCompressedFormat::BC5;
    case tydra::TextureBlockFormat::BC6H: return DrawCompressedFormat::BC6H;
    case tydra::TextureBlockFormat::BC7: return DrawCompressedFormat::BC7;
    case tydra::TextureBlockFormat::ETC2_RGB: return DrawCompressedFormat::ETC2_RGB;
    case tydra::TextureBlockFormat::ETC2_RGBA: return DrawCompressedFormat::ETC2_RGBA;
    case tydra::TextureBlockFormat::ASTC_4x4: return DrawCompressedFormat::ASTC_4x4;
    case tydra::TextureBlockFormat::UNI: return DrawCompressedFormat::ASTC_4x4;  // uni ~ astc4x4
    default: return DrawCompressedFormat::None;  // EAC_R11/RG11, None
  }
}

// Bytes per 4x4 (or codec-native) block, for splitting a packed mip chain.
static int BlockFormatBytes(tydra::TextureBlockFormat f) {
  switch (f) {
    case tydra::TextureBlockFormat::BC1:
    case tydra::TextureBlockFormat::ETC2_RGB:
    case tydra::TextureBlockFormat::EAC_R11: return 8;
    default: return 16;  // BC3/5/6H/7, ETC2_RGBA, EAC_RG11, ASTC_4x4, UNI
  }
}

// Kept-compressed KTX2 passthrough: a tydra image whose blockFormat != None
// carries GPU block bytes. Adapt them to the device (upload as-is / transcode
// uni / decode fallback) instead of decoding-then-re-encoding. Only engages when
// no size cap / budget resize is requested (those need decoded texels). Returns
// true if the texture was populated (compressed or RGBA fallback).
static bool TryKeepCompressedTexture(const tydra::RenderScene& rs,
                                     const tydra::TextureImage& img,
                                     const TextureRuntimeOptions& opt,
                                     DrawTextureCPU* tex) {
  if (img.blockFormat == tydra::TextureBlockFormat::None) return false;
  if (opt.maxTextureSize > 0 || opt.textureBudgetMB > 0) return false;
  if (img.buffer_id < 0 ||
      static_cast<size_t>(img.buffer_id) >= rs.buffers.size())
    return false;
  if (img.width <= 0 || img.height <= 0) return false;
  const std::vector<uint8_t>& buf =
      rs.buffers[static_cast<size_t>(img.buffer_id)].data;
  if (buf.empty()) return false;

  const bool isUni = img.blockFormat == tydra::TextureBlockFormat::UNI;
  const DrawCompressedFormat srcFmt = MapTydraBlockFormat(img.blockFormat);
  const int blockBytes = BlockFormatBytes(img.blockFormat);
  const int bw = img.blockWidth > 0 ? img.blockWidth : 4;
  const int bh = img.blockHeight > 0 ? img.blockHeight : 4;
  auto levelBytes = [&](int lw, int lh) -> size_t {
    return static_cast<size_t>((lw + bw - 1) / bw) *
           static_cast<size_t>((lh + bh - 1) / bh) *
           static_cast<size_t>(blockBytes);
  };

  // Split the tightly-packed buffer into mip levels (largest-first), deriving
  // each level's size from its dimensions + block geometry.
  struct Lvl { size_t off; size_t size; int w; int h; };
  std::vector<Lvl> levels;
  size_t off = 0;
  int lw = img.width, lh = img.height;
  for (int l = 0; l < 24 && off < buf.size(); ++l) {
    const size_t sz = levelBytes(lw, lh);
    if (sz == 0 || off + sz > buf.size()) break;
    levels.push_back({off, sz, lw, lh});
    off += sz;
    if (lw == 1 && lh == 1) break;
    lw = std::max(1, lw >> 1);
    lh = std::max(1, lh >> 1);
  }
  if (levels.empty()) return false;

  // Level 0 decides the device target (compressed format or RGBA fallback).
  DrawCompressedImageCPU comp;
  light3d::Image rgba;
  if (!TexToolsAdaptCompressed(buf.data() + levels[0].off, levels[0].size, isUni,
                               srcFmt, static_cast<uint32_t>(levels[0].w),
                               static_cast<uint32_t>(levels[0].h), opt.caps,
                               &comp, &rgba)) {
    return false;
  }

  if (comp.data.empty()) {
    tex->image = std::move(rgba);  // uncompressed fallback (level 0 only)
    return true;
  }

  // Compressed: carry the precomputed mip chain (levels 1..N) to the same
  // target format by copy/transcode. On any failure, fall back to base-only.
  for (size_t l = 1; l < levels.size(); ++l) {
    std::vector<uint8_t> mipBytes;
    if (!TexToolsAdaptCompressedLevel(buf.data() + levels[l].off, levels[l].size,
                                      isUni, srcFmt, comp.format,
                                      static_cast<uint32_t>(levels[l].w),
                                      static_cast<uint32_t>(levels[l].h),
                                      &mipBytes)) {
      comp.mips.clear();
      break;
    }
    DrawCompressedMipCPU m;
    m.width = levels[l].w;
    m.height = levels[l].h;
    m.data = std::move(mipBytes);
    comp.mips.push_back(std::move(m));
  }

  std::fprintf(stderr,
               "[tusdview] kept-compressed KTX2: %s (block fmt %d) -> draw fmt "
               "%d, %dx%d, %zu levels, %zu base bytes (no re-encode)\n",
               isUni ? "uni" : "block", static_cast<int>(img.blockFormat),
               static_cast<int>(comp.format), comp.width, comp.height,
               1 + comp.mips.size(), comp.data.size());
  tex->compressed = std::move(comp);
  tex->requestedCompressed = true;
  tex->compressedFinal = true;
  // Metadata only; the compressed payload (+ mips) is uploaded directly.
  tex->image.width = img.width;
  tex->image.height = img.height;
  tex->image.channels = 4;
  tex->image.data.clear();
  return true;
}
#endif  // TUSDVIEW_WITH_TEXTOOLS

void BuildDrawTextures(const tydra::RenderScene& rs, DrawScene* out,
                       std::vector<int>* drawTexMap,
                       const TextureRuntimeOptions& textureOptions) {
  drawTexMap->assign(rs.textures.size(), -1);
  std::unordered_map<std::string, int> imgToDrawTex;
  std::unordered_map<std::string, int> udimToDrawTex;
  for (size_t uvIdx = 0; uvIdx < rs.textures.size(); ++uvIdx) {
    const tydra::UVTexture& uv = rs.textures[uvIdx];
    const int wrapS = MapWrap(uv.wrapS);
    const int wrapT = MapWrap(uv.wrapT);
    if (uv.is_udim && uv.udim_texture_id >= 0 &&
        static_cast<size_t>(uv.udim_texture_id) < rs.udim_textures.size()) {
      const std::string key = std::to_string(uv.udim_texture_id) + "|" +
                              std::to_string(wrapS) + "," +
                              std::to_string(wrapT);
      auto found = udimToDrawTex.find(key);
      if (found != udimToDrawTex.end()) {
        (*drawTexMap)[uvIdx] = found->second;
        continue;
      }
      const tydra::UDIMTexture& udim =
          rs.udim_textures[static_cast<size_t>(uv.udim_texture_id)];
      std::vector<std::pair<uint32_t, int32_t>> tileIds;
      tileIds.reserve(udim.imageTileIds.size());
      for (const auto& kv : udim.imageTileIds) tileIds.push_back(kv);
      std::sort(tileIds.begin(), tileIds.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });

      DrawTextureCPU tex;
      tex.isUdim = true;
      tex.assetIdentifier = udim.asset_identifier;
      tex.renderUdimId = static_cast<int>(uv.udim_texture_id);
      tex.srgb = true;
      tex.wrapS = wrapS;
      tex.wrapT = wrapT;
      for (const auto& kv : tileIds) {
        const uint32_t udimId = kv.first;
        const int32_t imgIndex = kv.second;
        if (imgIndex < 0 || static_cast<size_t>(imgIndex) >= rs.images.size()) {
          continue;
        }
        const tydra::TextureImage& img =
            rs.images[static_cast<size_t>(imgIndex)];
        DrawUdimTileCPU tile;
        tile.udim = udimId;
        tile.u = (udimId - 1001u) % 10u;
        tile.v = (udimId - 1001u) / 10u;
        tile.assetIdentifier = img.asset_identifier;
        tile.renderImageId = static_cast<int>(imgIndex);
        if (!DecodeToRGBA8(rs, img, &tile.image)) {
          out->skipped.push_back("UDIM tile " + std::to_string(udimId) +
                                 " for texture '" + uv.prim_name +
                                 "': undecoded/unsupported image");
          continue;
        }
        if (tex.assetIdentifier.empty()) {
          tex.assetIdentifier = img.asset_identifier;
        }
        tex.srgb = IsSrgb(img.colorSpace);
        tex.udimTiles.push_back(std::move(tile));
      }
      if (tex.udimTiles.empty()) {
        out->skipped.push_back("UDIM texture '" + uv.prim_name +
                               "': no decoded tiles");
        continue;
      }
      NormalizeUdimTiles(&tex, tex.srgb, out);
      int drawIdx = static_cast<int>(out->textures.size());
      out->textures.push_back(std::move(tex));
      udimToDrawTex[key] = drawIdx;
      (*drawTexMap)[uvIdx] = drawIdx;
      continue;
    }

    const int64_t imgId = uv.texture_image_id;
    if (imgId < 0 || static_cast<size_t>(imgId) >= rs.images.size()) {
      out->skipped.push_back("texture '" + uv.prim_name +
                             "': no image (possibly UDIM/unresolved)");
      continue;
    }
    const std::string key = std::to_string(imgId) + "|" + std::to_string(wrapS) +
                            "," + std::to_string(wrapT);
    auto found = imgToDrawTex.find(key);
    if (found != imgToDrawTex.end()) {
      (*drawTexMap)[uvIdx] = found->second;
      continue;
    }
    const tydra::TextureImage& img = rs.images[static_cast<size_t>(imgId)];
    DrawTextureCPU tex;
    bool built = false;
#if defined(TUSDVIEW_WITH_TEXTOOLS)
    // Kept-compressed KTX2 passthrough (blockFormat != None): upload/transcode
    // the GPU blocks directly instead of decoding + re-encoding.
    built = TryKeepCompressedTexture(rs, img, textureOptions, &tex);
#endif
    if (!built && !DecodeToRGBA8(rs, img, &tex.image)) {
      out->skipped.push_back("texture '" + uv.prim_name +
                             "': undecoded/unsupported image");
      continue;
    }
    tex.assetIdentifier = img.asset_identifier;
    tex.renderImageId = static_cast<int>(imgId);
    tex.srgb = IsSrgb(img.colorSpace);
    tex.wrapS = wrapS;
    tex.wrapT = wrapT;
    int drawIdx = static_cast<int>(out->textures.size());
    out->textures.push_back(std::move(tex));
    imgToDrawTex[key] = drawIdx;
    (*drawTexMap)[uvIdx] = drawIdx;
  }
  ApplyTextureRuntimeOptions(textureOptions, out);
}

// Build the renderable materials from the RenderScene, mapping UVTexture
// indices to DrawScene texture slots via `drawTexMap`.
void BuildDrawMaterials(const tydra::RenderScene& rs, DrawScene* out,
                        const std::vector<int>& drawTexMap) {
  auto mapTex = [&](int texId) -> int {
    if (texId < 0 || static_cast<size_t>(texId) >= drawTexMap.size()) return -1;
    return drawTexMap[static_cast<size_t>(texId)];
  };
  auto channelFor = [&](int texId, int fallback) -> int {
    if (texId < 0 || static_cast<size_t>(texId) >= rs.textures.size()) return -1;
    return TextureChannelIndex(rs.textures[static_cast<size_t>(texId)].connectedOutputChannel,
                               fallback);
  };
  auto addFloatParam = [&](DrawMaterialCPU* dm, const char* shader,
                           const char* name, const tydra::ShaderParam<float>& p,
                           int fallbackChannel = 0) {
    if (!dm) return;
    DrawMaterialParamCPU param;
    param.shader = shader;
    param.name = name;
    param.type = DrawMaterialParamType::Float;
    param.value[0] = p.value;
    param.texture = mapTex(p.texture_id);
    param.renderTexture = p.texture_id;
    param.channel = channelFor(p.texture_id, fallbackChannel);
    CopyTexSample(rs, p.texture_id, &param.sample);
    dm->params.push_back(std::move(param));
  };
  auto addVec3Param = [&](DrawMaterialCPU* dm, const char* shader,
                          const char* name, const tydra::ShaderParam<tydra::vec3>& p) {
    if (!dm) return;
    DrawMaterialParamCPU param;
    param.shader = shader;
    param.name = name;
    param.type = DrawMaterialParamType::Vec3;
    param.value[0] = p.value[0];
    param.value[1] = p.value[1];
    param.value[2] = p.value[2];
    param.value[3] = 1.0f;
    param.texture = mapTex(p.texture_id);
    param.renderTexture = p.texture_id;
    param.channel = channelFor(p.texture_id, -1);
    CopyTexSample(rs, p.texture_id, &param.sample);
    dm->params.push_back(std::move(param));
  };

  out->materials.reserve(rs.materials.size());
  for (const auto& mat : rs.materials) {
    DrawMaterialCPU dm;
    dm.name = mat.name;
    dm.absPath = mat.abs_path;
    dm.displayName = mat.display_name;
    dm.hasUsdPreviewSurface = mat.surfaceShader.has_value();
    dm.hasOpenPBRSurface = mat.openPBRShader.has_value();
    dm.hasDisplacementOutput = mat.has_displacement;
    dm.hasVolumeOutput = mat.has_volume;
    dm.displacementShaderPath = mat.displacement_shader_path;
    dm.volumeShaderPath = mat.volume_shader_path;
    if (mat.surfaceShader.has_value()) {
      const tydra::PreviewSurfaceShader& s = *mat.surfaceShader;
      addVec3Param(&dm, "UsdPreviewSurface", "diffuseColor", s.diffuseColor);
      addVec3Param(&dm, "UsdPreviewSurface", "emissiveColor", s.emissiveColor);
      addVec3Param(&dm, "UsdPreviewSurface", "specularColor", s.specularColor);
      addFloatParam(&dm, "UsdPreviewSurface", "metallic", s.metallic, 2);
      addFloatParam(&dm, "UsdPreviewSurface", "roughness", s.roughness, 1);
      addFloatParam(&dm, "UsdPreviewSurface", "clearcoat", s.clearcoat, 0);
      addFloatParam(&dm, "UsdPreviewSurface", "clearcoatRoughness",
                    s.clearcoatRoughness, 0);
      addFloatParam(&dm, "UsdPreviewSurface", "opacity", s.opacity, 3);
      addFloatParam(&dm, "UsdPreviewSurface", "opacityThreshold",
                    s.opacityThreshold, 0);
      addFloatParam(&dm, "UsdPreviewSurface", "ior", s.ior, 0);
      addVec3Param(&dm, "UsdPreviewSurface", "normal", s.normal);
      addFloatParam(&dm, "UsdPreviewSurface", "displacement", s.displacement, 0);
      addFloatParam(&dm, "UsdPreviewSurface", "occlusion", s.occlusion, 0);
      dm.baseColorTex = mapTex(s.diffuseColor.texture_id);
      dm.emissiveTex = mapTex(s.emissiveColor.texture_id);
      dm.normalTex = mapTex(s.normal.texture_id);
      int mrTex = mapTex(s.metallic.texture_id);
      if (mrTex < 0) mrTex = mapTex(s.roughness.texture_id);
      dm.metalRoughTex = mrTex;
      dm.displacementTex = mapTex(s.displacement.texture_id);
      CopyTexSample(rs, s.diffuseColor.texture_id, &dm.baseColorSample);
      CopyTexSample(rs, s.emissiveColor.texture_id, &dm.emissiveSample);
      CopyTexSample(rs, s.normal.texture_id, &dm.normalSample);
      if (s.metallic.texture_id >= 0) {
        CopyTexSample(rs, s.metallic.texture_id, &dm.metalRoughSample);
      } else {
        CopyTexSample(rs, s.roughness.texture_id, &dm.metalRoughSample);
      }
      if (s.normal.texture_id >= 0) {
        for (int i = 0; i < 3; ++i) {
          // UsdPreviewSurface normal maps default to unpacking [0,1] -> [-1,1].
          // If authored, Tydra's scale/bias above overwrites these values.
          if (dm.normalSample.scale[i] == 1.0f && dm.normalSample.bias[i] == 0.0f) {
            dm.normalSample.scale[i] = 2.0f;
            dm.normalSample.bias[i] = -1.0f;
          }
        }
      }
      if (s.metallic.texture_id >= 0 &&
          static_cast<size_t>(s.metallic.texture_id) < rs.textures.size()) {
        const tydra::UVTexture& mt =
            rs.textures[static_cast<size_t>(s.metallic.texture_id)];
        dm.metallicChannel = TextureChannelIndex(mt.connectedOutputChannel, 2);
        dm.metallicTexScale = mt.scale[dm.metallicChannel];
        dm.metallicTexBias = mt.bias[dm.metallicChannel];
      }
      if (s.roughness.texture_id >= 0 &&
          static_cast<size_t>(s.roughness.texture_id) < rs.textures.size()) {
        const tydra::UVTexture& rt =
            rs.textures[static_cast<size_t>(s.roughness.texture_id)];
        dm.roughnessChannel = TextureChannelIndex(rt.connectedOutputChannel, 1);
        dm.roughnessTexScale = rt.scale[dm.roughnessChannel];
        dm.roughnessTexBias = rt.bias[dm.roughnessChannel];
        if (s.metallic.texture_id < 0) {
          CopyTexSample(rt, &dm.metalRoughSample);
        }
      }
      dm.displacementUv = MapUvXform(rs, s.displacement.texture_id);
      dm.displacementConst = s.displacement.value;
      // Displacement maps connect outputs:r (channel 0); honor that channel's
      // UVTexture scale/bias so the viewer centers the height like tusdrender.
      if (s.displacement.texture_id >= 0 &&
          static_cast<size_t>(s.displacement.texture_id) < rs.textures.size()) {
        const tydra::UVTexture& dt =
            rs.textures[static_cast<size_t>(s.displacement.texture_id)];
        dm.displacementTexScale = dt.scale[0];
        dm.displacementTexBias = dt.bias[0];
      }
      // When a parameter is driven by a texture, the shader multiplies the
      // texel by the factor below, so use a neutral factor (1) instead of the
      // constant fallback (which would darken/override the texture).
      dm.baseColor[0] = dm.baseColorTex >= 0 ? 1.0f : s.diffuseColor.value[0];
      dm.baseColor[1] = dm.baseColorTex >= 0 ? 1.0f : s.diffuseColor.value[1];
      dm.baseColor[2] = dm.baseColorTex >= 0 ? 1.0f : s.diffuseColor.value[2];
      dm.metallic = (s.metallic.texture_id >= 0) ? 1.0f : s.metallic.value;
      dm.roughness = (s.roughness.texture_id >= 0) ? 1.0f : s.roughness.value;
      dm.emissive[0] = dm.emissiveTex >= 0 ? 1.0f : s.emissiveColor.value[0];
      dm.emissive[1] = dm.emissiveTex >= 0 ? 1.0f : s.emissiveColor.value[1];
      dm.emissive[2] = dm.emissiveTex >= 0 ? 1.0f : s.emissiveColor.value[2];
      dm.alpha = s.opacity.value;
      switch (mat.materialTag) {
        case tydra::MaterialTag::Masked:
          dm.alphaMode = static_cast<int>(AlphaMode::Mask);
          dm.alphaCutoff = s.opacityThreshold.value;
          break;
        case tydra::MaterialTag::Translucent:
          dm.alphaMode = static_cast<int>(AlphaMode::Blend);
          break;
        case tydra::MaterialTag::Opaque:
        default:
          dm.alphaMode = static_cast<int>(AlphaMode::Opaque);
          break;
      }
    } else if (mat.openPBRShader.has_value()) {
      const tydra::OpenPBRSurfaceShader& s = *mat.openPBRShader;
      dm.materialXNodeGraphJson = s.nodeGraphJson;
      addFloatParam(&dm, "OpenPBRSurface", "base_weight", s.base_weight, 0);
      addVec3Param(&dm, "OpenPBRSurface", "base_color", s.base_color);
      addFloatParam(&dm, "OpenPBRSurface", "base_roughness", s.base_roughness, 1);
      addFloatParam(&dm, "OpenPBRSurface", "base_metalness", s.base_metalness, 2);
      addFloatParam(&dm, "OpenPBRSurface", "base_diffuse_roughness",
                    s.base_diffuse_roughness, 0);
      addFloatParam(&dm, "OpenPBRSurface", "specular_weight", s.specular_weight, 0);
      addVec3Param(&dm, "OpenPBRSurface", "specular_color", s.specular_color);
      addFloatParam(&dm, "OpenPBRSurface", "specular_roughness",
                    s.specular_roughness, 1);
      addFloatParam(&dm, "OpenPBRSurface", "specular_ior", s.specular_ior, 0);
      addFloatParam(&dm, "OpenPBRSurface", "specular_ior_level",
                    s.specular_ior_level, 0);
      addFloatParam(&dm, "OpenPBRSurface", "specular_anisotropy",
                    s.specular_anisotropy, 0);
      addFloatParam(&dm, "OpenPBRSurface", "specular_rotation",
                    s.specular_rotation, 0);
      addFloatParam(&dm, "OpenPBRSurface", "specular_roughness_anisotropy",
                    s.specular_roughness_anisotropy, 0);
      addFloatParam(&dm, "OpenPBRSurface", "transmission_weight",
                    s.transmission_weight, 0);
      addVec3Param(&dm, "OpenPBRSurface", "transmission_color",
                   s.transmission_color);
      addFloatParam(&dm, "OpenPBRSurface", "transmission_depth",
                    s.transmission_depth, 0);
      addVec3Param(&dm, "OpenPBRSurface", "transmission_scatter",
                   s.transmission_scatter);
      addFloatParam(&dm, "OpenPBRSurface", "transmission_scatter_anisotropy",
                    s.transmission_scatter_anisotropy, 0);
      addFloatParam(&dm, "OpenPBRSurface", "transmission_dispersion",
                    s.transmission_dispersion, 0);
      addFloatParam(&dm, "OpenPBRSurface", "transmission_dispersion_abbe_number",
                    s.transmission_dispersion_abbe_number, 0);
      addFloatParam(&dm, "OpenPBRSurface", "transmission_dispersion_scale",
                    s.transmission_dispersion_scale, 0);
      addFloatParam(&dm, "OpenPBRSurface", "subsurface_weight",
                    s.subsurface_weight, 0);
      addVec3Param(&dm, "OpenPBRSurface", "subsurface_color",
                   s.subsurface_color);
      addFloatParam(&dm, "OpenPBRSurface", "subsurface_radius",
                    s.subsurface_radius, 0);
      addVec3Param(&dm, "OpenPBRSurface", "subsurface_radius_scale",
                   s.subsurface_radius_scale);
      addFloatParam(&dm, "OpenPBRSurface", "subsurface_scale",
                    s.subsurface_scale, 0);
      addFloatParam(&dm, "OpenPBRSurface", "subsurface_anisotropy",
                    s.subsurface_anisotropy, 0);
      addFloatParam(&dm, "OpenPBRSurface", "subsurface_scatter_anisotropy",
                    s.subsurface_scatter_anisotropy, 0);
      addFloatParam(&dm, "OpenPBRSurface", "sheen_weight", s.sheen_weight, 0);
      addVec3Param(&dm, "OpenPBRSurface", "sheen_color", s.sheen_color);
      addFloatParam(&dm, "OpenPBRSurface", "sheen_roughness",
                    s.sheen_roughness, 1);
      addFloatParam(&dm, "OpenPBRSurface", "fuzz_weight", s.fuzz_weight, 0);
      addVec3Param(&dm, "OpenPBRSurface", "fuzz_color", s.fuzz_color);
      addFloatParam(&dm, "OpenPBRSurface", "fuzz_roughness", s.fuzz_roughness, 1);
      addFloatParam(&dm, "OpenPBRSurface", "thin_film_weight",
                    s.thin_film_weight, 0);
      addFloatParam(&dm, "OpenPBRSurface", "thin_film_thickness",
                    s.thin_film_thickness, 0);
      addFloatParam(&dm, "OpenPBRSurface", "thin_film_ior", s.thin_film_ior, 0);
      addFloatParam(&dm, "OpenPBRSurface", "coat_weight", s.coat_weight, 0);
      addVec3Param(&dm, "OpenPBRSurface", "coat_color", s.coat_color);
      addFloatParam(&dm, "OpenPBRSurface", "coat_roughness", s.coat_roughness, 1);
      addFloatParam(&dm, "OpenPBRSurface", "coat_anisotropy",
                    s.coat_anisotropy, 0);
      addFloatParam(&dm, "OpenPBRSurface", "coat_rotation", s.coat_rotation, 0);
      addFloatParam(&dm, "OpenPBRSurface", "coat_ior", s.coat_ior, 0);
      addFloatParam(&dm, "OpenPBRSurface", "coat_affect_color",
                    s.coat_affect_color, 0);
      addFloatParam(&dm, "OpenPBRSurface", "coat_affect_roughness",
                    s.coat_affect_roughness, 0);
      addFloatParam(&dm, "OpenPBRSurface", "coat_roughness_anisotropy",
                    s.coat_roughness_anisotropy, 0);
      addFloatParam(&dm, "OpenPBRSurface", "coat_darkening", s.coat_darkening, 0);
      addFloatParam(&dm, "OpenPBRSurface", "emission_luminance",
                    s.emission_luminance, 0);
      addVec3Param(&dm, "OpenPBRSurface", "emission_color", s.emission_color);
      addFloatParam(&dm, "OpenPBRSurface", "opacity", s.opacity, 3);
      addVec3Param(&dm, "OpenPBRSurface", "normal", s.normal);
      addVec3Param(&dm, "OpenPBRSurface", "tangent", s.tangent);
      addVec3Param(&dm, "OpenPBRSurface", "coat_normal", s.coat_normal);
      addVec3Param(&dm, "OpenPBRSurface", "coat_tangent", s.coat_tangent);
      dm.baseColorTex = mapTex(s.base_color.texture_id);
      dm.emissiveTex = mapTex(s.emission_color.texture_id);
      dm.normalTex = mapTex(s.normal.texture_id);
      dm.coatNormalTex = mapTex(s.coat_normal.texture_id);
      int mrTex = mapTex(s.base_metalness.texture_id);
      if (mrTex < 0) mrTex = mapTex(s.base_roughness.texture_id);
      dm.metalRoughTex = mrTex;
      CopyTexSample(rs, s.base_color.texture_id, &dm.baseColorSample);
      CopyTexSample(rs, s.emission_color.texture_id, &dm.emissiveSample);
      CopyTexSample(rs, s.normal.texture_id, &dm.normalSample);
      CopyTexSample(rs, s.coat_normal.texture_id, &dm.coatNormalSample);
      if (s.base_metalness.texture_id >= 0) {
        CopyTexSample(rs, s.base_metalness.texture_id, &dm.metalRoughSample);
      } else {
        CopyTexSample(rs, s.base_roughness.texture_id, &dm.metalRoughSample);
      }
      if (s.normal.texture_id >= 0) {
        for (int i = 0; i < 3; ++i) {
          if (dm.normalSample.scale[i] == 1.0f && dm.normalSample.bias[i] == 0.0f) {
            dm.normalSample.scale[i] = 2.0f;
            dm.normalSample.bias[i] = -1.0f;
          }
        }
      }
      if (s.coat_normal.texture_id >= 0) {
        for (int i = 0; i < 3; ++i) {
          if (dm.coatNormalSample.scale[i] == 1.0f &&
              dm.coatNormalSample.bias[i] == 0.0f) {
            dm.coatNormalSample.scale[i] = 2.0f;
            dm.coatNormalSample.bias[i] = -1.0f;
          }
        }
      }
      if (s.base_metalness.texture_id >= 0 &&
          static_cast<size_t>(s.base_metalness.texture_id) < rs.textures.size()) {
        const tydra::UVTexture& mt =
            rs.textures[static_cast<size_t>(s.base_metalness.texture_id)];
        dm.metallicChannel = TextureChannelIndex(mt.connectedOutputChannel, 2);
        dm.metallicTexScale = mt.scale[dm.metallicChannel];
        dm.metallicTexBias = mt.bias[dm.metallicChannel];
      }
      if (s.base_roughness.texture_id >= 0 &&
          static_cast<size_t>(s.base_roughness.texture_id) < rs.textures.size()) {
        const tydra::UVTexture& rt =
            rs.textures[static_cast<size_t>(s.base_roughness.texture_id)];
        dm.roughnessChannel = TextureChannelIndex(rt.connectedOutputChannel, 1);
        dm.roughnessTexScale = rt.scale[dm.roughnessChannel];
        dm.roughnessTexBias = rt.bias[dm.roughnessChannel];
        if (s.base_metalness.texture_id < 0) {
          CopyTexSample(rt, &dm.metalRoughSample);
        }
      }
      // Raster and current RT preview shaders multiply sampled textures by these
      // factors. Use neutral factors when texture-driven, and keep OpenPBR's
      // emission_luminance as the emissive intensity multiplier.
      dm.baseColor[0] = dm.baseColorTex >= 0 ? 1.0f : s.base_color.value[0];
      dm.baseColor[1] = dm.baseColorTex >= 0 ? 1.0f : s.base_color.value[1];
      dm.baseColor[2] = dm.baseColorTex >= 0 ? 1.0f : s.base_color.value[2];
      dm.metallic = (s.base_metalness.texture_id >= 0) ? 1.0f : s.base_metalness.value;
      dm.roughness = (s.base_roughness.texture_id >= 0) ? 1.0f : s.base_roughness.value;
      const float emissionScale = s.emission_luminance.value;
      dm.emissive[0] =
          (dm.emissiveTex >= 0 ? 1.0f : s.emission_color.value[0]) * emissionScale;
      dm.emissive[1] =
          (dm.emissiveTex >= 0 ? 1.0f : s.emission_color.value[1]) * emissionScale;
      dm.emissive[2] =
          (dm.emissiveTex >= 0 ? 1.0f : s.emission_color.value[2]) * emissionScale;
      dm.alpha = s.opacity.value;
      dm.alphaMode = (mat.materialTag == tydra::MaterialTag::Translucent)
                         ? static_cast<int>(AlphaMode::Blend)
                         : static_cast<int>(AlphaMode::Opaque);
    }
    // else: leave default gray.
    BakeLightRtOpenPBR(&dm);
    out->materials.push_back(std::move(dm));
  }
}

DrawLightCPU::Type MapLightType(tydra::RenderLight::Type t) {
  switch (t) {
    case tydra::RenderLight::Type::Sphere: return DrawLightCPU::Type::Sphere;
    case tydra::RenderLight::Type::Disk: return DrawLightCPU::Type::Disk;
    case tydra::RenderLight::Type::Rect: return DrawLightCPU::Type::Rect;
    case tydra::RenderLight::Type::Cylinder: return DrawLightCPU::Type::Cylinder;
    case tydra::RenderLight::Type::Distant: return DrawLightCPU::Type::Distant;
    case tydra::RenderLight::Type::Dome: return DrawLightCPU::Type::Dome;
    case tydra::RenderLight::Type::Geometry: return DrawLightCPU::Type::Geometry;
    case tydra::RenderLight::Type::Portal: return DrawLightCPU::Type::Portal;
    case tydra::RenderLight::Type::Point:
    default: return DrawLightCPU::Type::Point;
  }
}

DrawLightCPU::DomeTextureFormat MapDomeTextureFormat(
    tydra::RenderLight::DomeTextureFormat f) {
  switch (f) {
    case tydra::RenderLight::DomeTextureFormat::Latlong:
      return DrawLightCPU::DomeTextureFormat::Latlong;
    case tydra::RenderLight::DomeTextureFormat::MirroredBall:
      return DrawLightCPU::DomeTextureFormat::MirroredBall;
    case tydra::RenderLight::DomeTextureFormat::Angular:
      return DrawLightCPU::DomeTextureFormat::Angular;
    case tydra::RenderLight::DomeTextureFormat::Automatic:
    default:
      return DrawLightCPU::DomeTextureFormat::Automatic;
  }
}

void Mat4fToColMajor(const tydra::mat4& M, float out[16]) {
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      out[i * 4 + j] = M.m[i][j];
    }
  }
}

float ClampLightTemperature(float kelvin) {
  return std::max(1000.0f, std::min(40000.0f, kelvin));
}

void TemperatureRgb(float kelvin, float out[3]) {
  const float temp = ClampLightTemperature(kelvin) / 100.0f;
  if (temp <= 66.0f) {
    out[0] = 1.0f;
  } else {
    const float r = 329.698727446f * std::pow(temp - 60.0f, -0.1332047592f);
    out[0] = std::max(0.0f, std::min(1.0f, r / 255.0f));
  }
  if (temp <= 66.0f) {
    const float g = 99.4708025861f * std::log(temp) - 161.1195681661f;
    out[1] = std::max(0.0f, std::min(1.0f, g / 255.0f));
  } else {
    const float g = 288.1221695283f * std::pow(temp - 60.0f, -0.0755148492f);
    out[1] = std::max(0.0f, std::min(1.0f, g / 255.0f));
  }
  if (temp >= 66.0f) {
    out[2] = 1.0f;
  } else if (temp <= 19.0f) {
    out[2] = 0.0f;
  } else {
    const float b = 138.5177312231f * std::log(temp - 10.0f) - 305.0447927307f;
    out[2] = std::max(0.0f, std::min(1.0f, b / 255.0f));
  }
}

float LightShapeArea(const DrawLightCPU& light) {
  constexpr float kPi = 3.14159265358979323846f;
  switch (light.type) {
    case DrawLightCPU::Type::Sphere:
    case DrawLightCPU::Type::Point:
      return 4.0f * kPi * light.radius * light.radius;
    case DrawLightCPU::Type::Disk:
      return kPi * light.radius * light.radius;
    case DrawLightCPU::Type::Rect:
      return light.width * light.height;
    case DrawLightCPU::Type::Cylinder:
      return 2.0f * kPi * light.radius * light.length;
    default:
      return 0.0f;
  }
}

void BakeLightDerivedParams(DrawLightCPU* light) {
  if (!light) return;
  float c[3]{light->color[0], light->color[1], light->color[2]};
  if (light->enableColorTemperature) {
    float tc[3];
    TemperatureRgb(light->colorTemperature, tc);
    c[0] *= tc[0];
    c[1] *= tc[1];
    c[2] *= tc[2];
  }
  light->effectiveIntensity = light->intensity * std::pow(2.0f, light->exposure);
  for (int i = 0; i < 3; ++i) {
    light->effectiveColor[i] = c[i] * light->effectiveIntensity;
    light->normalizedColor[i] = light->effectiveColor[i];
  }
  light->area = std::max(0.0f, LightShapeArea(*light));
  light->invArea = (light->area > 0.0f) ? 1.0f / light->area : 0.0f;
  if (light->normalize && light->invArea > 0.0f) {
    for (int i = 0; i < 3; ++i) {
      light->normalizedColor[i] = light->effectiveColor[i] * light->invArea;
    }
  }
  light->hasShaping = light->shapingConeAngle < 90.0f ||
                      !light->shapingIesFile.empty() ||
                      light->shapingFocus != 0.0f ||
                      light->shapingFocusTint[0] != 0.0f ||
                      light->shapingFocusTint[1] != 0.0f ||
                      light->shapingFocusTint[2] != 0.0f;
}

void BuildDrawLights(const tydra::RenderScene& rs, DrawScene* out,
                     const TextureRuntimeOptions& textureOptions = {}) {
  if (!out) return;
  out->lights.reserve(rs.lights.size());
  for (const tydra::RenderLight& src : rs.lights) {
    DrawLightCPU dst;
    dst.name = src.name;
    dst.absPath = src.abs_path;
    dst.displayName = src.display_name;
    dst.type = MapLightType(src.type);
    dst.color[0] = src.color[0];
    dst.color[1] = src.color[1];
    dst.color[2] = src.color[2];
    dst.intensity = src.intensity;
    dst.exposure = src.exposure;
    dst.diffuse = src.diffuse;
    dst.specular = src.specular;
    dst.normalize = src.normalize;
    dst.enableColorTemperature = src.enableColorTemperature;
    dst.colorTemperature = src.colorTemperature;
    Mat4fToColMajor(src.transform, dst.transform);
    dst.position[0] = src.position[0];
    dst.position[1] = src.position[1];
    dst.position[2] = src.position[2];
    dst.direction[0] = src.direction[0];
    dst.direction[1] = src.direction[1];
    dst.direction[2] = src.direction[2];
    dst.radius = src.radius;
    dst.width = src.width;
    dst.height = src.height;
    dst.length = src.length;
    dst.angle = src.angle;
    dst.textureFile = src.textureFile;
    dst.renderEnvmapImage = src.envmap_texture_id;
    if (src.type == tydra::RenderLight::Type::Dome && src.envmap_texture_id >= 0) {
      int texId = -1;
      if (AddImageTexture(rs, src.envmap_texture_id,
                          static_cast<int>(WrapMode::Repeat),
                          static_cast<int>(WrapMode::ClampToEdge), out,
                          &texId)) {
        dst.envmapTexture = texId;
      } else {
        out->skipped.push_back("dome light '" + src.name +
                               "': undecoded/unsupported envmap image");
      }
    }
    dst.domeTextureFormat = MapDomeTextureFormat(src.domeTextureFormat);
    // Split-sum IBL bake (HDR-correct: decodes the source image to float,
    // bypassing the 8-bit envmapTexture clamp). MirroredBall/Angular probes
    // are resampled to an equirect first.
    if (src.type == tydra::RenderLight::Type::Dome &&
        src.envmap_texture_id >= 0 && textureOptions.domeIbl > 0 &&
        TexToolsAvailable() &&
        static_cast<size_t>(src.envmap_texture_id) < rs.images.size()) {
      std::vector<float> rgb;
      int ew = 0, eh = 0;
      const auto t0 = std::chrono::steady_clock::now();
      bool decoded = DecodeToFloatRGB(
          rs, rs.images[static_cast<size_t>(src.envmap_texture_id)], &ew, &eh,
          &rgb);
      if (decoded &&
          (dst.domeTextureFormat ==
               DrawLightCPU::DomeTextureFormat::MirroredBall ||
           dst.domeTextureFormat == DrawLightCPU::DomeTextureFormat::Angular)) {
        std::vector<float> eq;
        int eqH = 0;
        const int eqW = std::min(2048, std::max(256, 2 * ew));
        if (TexToolsProbeToEquirect(
                rgb.data(), ew, eh, static_cast<int>(dst.domeTextureFormat),
                eqW, &eq, &eqH)) {
          rgb = std::move(eq);
          ew = eqW;
          eh = eqH;
        } else {
          decoded = false;
        }
      }
      if (decoded &&
          TexToolsBuildDomeIbl(rgb.data(), ew, eh,
                               textureOptions.domeIbl >= 2, &dst.ibl)) {
        const double ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0)
                .count();
        fprintf(stderr, "[tusdview] dome IBL bake '%s': %dx%d -> spec %d/irr %d in %.0f ms\n",
                src.name.c_str(), ew, eh, dst.ibl.specFaceSize,
                dst.ibl.irrFaceSize, ms);
      } else {
        out->skipped.push_back("dome light '" + src.name +
                               "': IBL bake failed (constant ambient)");
      }
    }
    dst.guideRadius = src.guideRadius;
    dst.shapingConeAngle = src.shapingConeAngle;
    dst.shapingConeSoftness = src.shapingConeSoftness;
    dst.shapingFocus = src.shapingFocus;
    dst.shapingFocusTint[0] = src.shapingFocusTint[0];
    dst.shapingFocusTint[1] = src.shapingFocusTint[1];
    dst.shapingFocusTint[2] = src.shapingFocusTint[2];
    dst.shapingIesFile = src.shapingIesFile;
    dst.shapingIesAngleScale = src.shapingIesAngleScale;
    dst.shapingIesNormalize = src.shapingIesNormalize;
    dst.shadowEnable = src.shadowEnable;
    dst.shadowColor[0] = src.shadowColor[0];
    dst.shadowColor[1] = src.shadowColor[1];
    dst.shadowColor[2] = src.shadowColor[2];
    dst.shadowDistance = src.shadowDistance;
    dst.shadowFalloff = src.shadowFalloff;
    dst.shadowFalloffGamma = src.shadowFalloffGamma;
    dst.geometryMesh = src.geometry_mesh_id;
    dst.materialSyncMode = src.material_sync_mode;
    dst.lightLinksAll = src.light_links_all;
    dst.lightLinkMeshIndices = src.light_link_mesh_indices;
    dst.shadowLinksAll = src.shadow_links_all;
    dst.shadowLinkMeshIndices = src.shadow_link_mesh_indices;
    dst.hasSpectralEmission = src.hasSpectralEmission();
    BakeLightDerivedParams(&dst);
    out->lights.push_back(std::move(dst));
  }
}

// Build the geometry (interleaved vertices, indices, submeshes, normals) of one
// RenderMesh into `dm`. World transform is left as identity; call PlaceDrawMesh
// afterwards. Returns false if the mesh has no usable geometry.
bool MakeDrawMesh(const tydra::RenderMesh& mesh, DrawMeshCPU* dmOut) {
  const size_t nPoints = mesh.points.size();
  const std::vector<uint32_t>& srcIndices = mesh.faceVertexIndices();
  const std::vector<uint32_t>& faceCounts = mesh.faceVertexCounts();
  if (nPoints == 0 || srcIndices.empty()) {
    return false;
  }

  DrawMeshCPU dm;
  dm.name = mesh.prim_name;
  dm.absPath = mesh.abs_path;
  dm.doubleSided = mesh.doubleSided;
  dm.normalSign = mesh.is_rightHanded ? 1.0f : -1.0f;
  SetIdentity4(dm.skinGeomBind);
  dm.skelId = mesh.skel_id;
  if (MeshHasSkinData(mesh, nPoints)) {
    dm.jointIdx.assign(nPoints * 4, 0u);
    dm.jointWt.assign(nPoints * 4, 0.0f);
    dm.influenceOffsetCount.assign(nPoints * 2, 0u);
    MatToColMajor(mesh.joint_and_weights.geomBindTransform, dm.skinGeomBind);
  }

  // Primary texcoord slot (0 if present, else the first available).
  const tydra::VertexAttribute* uvAttr = nullptr;
  {
    auto it = mesh.texcoords.find(0);
    if (it != mesh.texcoords.end()) {
      uvAttr = &it->second;
    } else if (!mesh.texcoords.empty()) {
      uvAttr = &mesh.texcoords.begin()->second;
    }
  }

  // Optional 2nd texcoord set (multi-UV AOV). The converter already extracts all
  // material-referenced UV slots into mesh.texcoords; slot 1 is the 2nd set.
  const tydra::VertexAttribute* uv1Attr = nullptr;
  {
    auto it = mesh.texcoords.find(1);
    if (it != mesh.texcoords.end()) uv1Attr = &it->second;
  }

  const bool normalsPerVertex = AttrUsableAsVertex(mesh.normals, nPoints);
  const bool uvPerVertex = uvAttr && AttrUsableAsVertex(*uvAttr, nPoints);
  const bool uv1PerVertex = uv1Attr && AttrUsableAsVertex(*uv1Attr, nPoints);
  const bool uv1FV = uv1Attr && !uv1Attr->empty() && uv1Attr->is_facevarying();
  const bool colorPerVertex = AttrUsableAsVertex(mesh.vertex_colors, nPoints);
  const bool colorFV =
      !mesh.vertex_colors.empty() && mesh.vertex_colors.is_facevarying();
  const bool colorUniform = !mesh.vertex_colors.empty() &&
                            mesh.vertex_colors.is_uniform() &&
                            mesh.vertex_colors.vertex_count() ==
                                faceCounts.size();
  const bool tangentPerVertex = AttrUsableAsVertex(mesh.tangents, nPoints);
  const bool binormalPerVertex = AttrUsableAsVertex(mesh.binormals, nPoints);
  const bool tangentFV = !mesh.tangents.empty() && mesh.tangents.is_facevarying();
  const bool binormalFV = !mesh.binormals.empty() && mesh.binormals.is_facevarying();
  bool gotNormals = false;

  if (mesh.is_single_indexable && !colorUniform) {
    // Indexed path: one DrawVertex per point; indices reference points.
    dm.vertices.resize(nPoints);
    if (colorPerVertex) dm.vertexColors.resize(nPoints * 3);
    if (tangentPerVertex) dm.tangents.resize(nPoints * 3);
    if (binormalPerVertex) dm.binormals.resize(nPoints * 3);
    for (size_t i = 0; i < nPoints; ++i) {
      DrawVertex& v = dm.vertices[i];
      v.px = mesh.points[i][0];
      v.py = mesh.points[i][1];
      v.pz = mesh.points[i][2];
      float nrm[3] = {0, 0, 0};
      if (normalsPerVertex) ReadFloats(mesh.normals, i, 3, nrm);
      v.nx = nrm[0]; v.ny = nrm[1]; v.nz = nrm[2];
      float uv[2] = {0, 0};
      if (uvPerVertex) ReadFloats(*uvAttr, i, 2, uv);
      // Flip V: USD `st` has v=0 at the image bottom, but decoded images are
      // top-row-first and uploaded so v=0 samples the top, so invert here.
      v.u = uv[0]; v.v = 1.0f - uv[1];
      if (uv1Attr) {
        float t[2] = {0, 0};
        if (uv1PerVertex) ReadFloats(*uv1Attr, i, 2, t);
        dm.uv1.push_back(t[0]);
        dm.uv1.push_back(1.0f - t[1]);
      }
      if (colorPerVertex) {
        float c[3] = {1, 1, 1};
        ReadFloats(mesh.vertex_colors, i, 3, c);
        dm.vertexColors[i * 3 + 0] = c[0];
        dm.vertexColors[i * 3 + 1] = c[1];
        dm.vertexColors[i * 3 + 2] = c[2];
      }
      if (tangentPerVertex) WriteFloat3Attr(mesh.tangents, i, i, &dm.tangents);
      if (binormalPerVertex) WriteFloat3Attr(mesh.binormals, i, i, &dm.binormals);
      WriteSkinVertex(mesh, i, i, &dm);
      WriteSkinInfluenceVertex(mesh, i, i, &dm);
    }
    dm.indices.assign(srcIndices.begin(), srcIndices.end());
    gotNormals = normalsPerVertex;
  } else {
    // Facevarying fallback: expand one vertex per face-vertex.
    dm.vertices.resize(srcIndices.size());
    if (MeshHasSkinData(mesh, nPoints)) {
      dm.jointIdx.assign(srcIndices.size() * 4, 0u);
      dm.jointWt.assign(srcIndices.size() * 4, 0.0f);
      dm.influenceOffsetCount.assign(srcIndices.size() * 2, 0u);
    }
    dm.indices.resize(srcIndices.size());
    if (colorFV || colorPerVertex || colorUniform) {
      dm.vertexColors.resize(srcIndices.size() * 3);
    }
    if (tangentFV || tangentPerVertex) dm.tangents.resize(srcIndices.size() * 3);
    if (binormalFV || binormalPerVertex) {
      dm.binormals.resize(srcIndices.size() * 3);
    }
    const bool normalsFV = !mesh.normals.empty() && mesh.normals.is_facevarying();
    const bool uvFV = uvAttr && !uvAttr->empty() && uvAttr->is_facevarying();
    gotNormals = normalsFV || normalsPerVertex;
    size_t faceIdx = 0;
    size_t nextFaceStart = faceCounts.empty() ? srcIndices.size()
                                              : size_t(faceCounts[0]);
    for (size_t k = 0; k < srcIndices.size(); ++k) {
      while (k >= nextFaceStart && faceIdx + 1 < faceCounts.size()) {
        ++faceIdx;
        nextFaceStart += size_t(faceCounts[faceIdx]);
      }
      const uint32_t pidx = srcIndices[k];
      DrawVertex& v = dm.vertices[k];
      if (pidx < nPoints) {
        v.px = mesh.points[pidx][0];
        v.py = mesh.points[pidx][1];
        v.pz = mesh.points[pidx][2];
      } else {
        v.px = v.py = v.pz = 0.0f;
      }
      float nrm[3] = {0, 0, 0};
      if (normalsFV) ReadFloats(mesh.normals, k, 3, nrm);
      else if (normalsPerVertex && pidx < nPoints)
        ReadFloats(mesh.normals, pidx, 3, nrm);
      v.nx = nrm[0]; v.ny = nrm[1]; v.nz = nrm[2];
      float uv[2] = {0, 0};
      if (uvFV) ReadFloats(*uvAttr, k, 2, uv);
      else if (uvPerVertex && pidx < nPoints)
        ReadFloats(*uvAttr, pidx, 2, uv);
      // Flip V: USD `st` has v=0 at the image bottom, but decoded images are
      // top-row-first and uploaded so v=0 samples the top, so invert here.
      v.u = uv[0]; v.v = 1.0f - uv[1];
      if (uv1Attr) {
        float t[2] = {0, 0};
        if (uv1FV) ReadFloats(*uv1Attr, k, 2, t);
        else if (uv1PerVertex && pidx < nPoints)
          ReadFloats(*uv1Attr, pidx, 2, t);
        dm.uv1.push_back(t[0]);
        dm.uv1.push_back(1.0f - t[1]);
      }
      if (!dm.vertexColors.empty()) {
        float c[3] = {1, 1, 1};
        if (colorFV) ReadFloats(mesh.vertex_colors, k, 3, c);
        else if (colorPerVertex && pidx < nPoints)
          ReadFloats(mesh.vertex_colors, pidx, 3, c);
        else if (colorUniform)
          ReadFloats(mesh.vertex_colors, faceIdx, 3, c);
        dm.vertexColors[k * 3 + 0] = c[0];
        dm.vertexColors[k * 3 + 1] = c[1];
        dm.vertexColors[k * 3 + 2] = c[2];
      }
      if (!dm.tangents.empty()) {
        if (tangentFV) WriteFloat3Attr(mesh.tangents, k, k, &dm.tangents);
        else if (tangentPerVertex && pidx < nPoints)
          WriteFloat3Attr(mesh.tangents, pidx, k, &dm.tangents);
      }
      if (!dm.binormals.empty()) {
        if (binormalFV) WriteFloat3Attr(mesh.binormals, k, k, &dm.binormals);
        else if (binormalPerVertex && pidx < nPoints)
          WriteFloat3Attr(mesh.binormals, pidx, k, &dm.binormals);
      }
      if (pidx < nPoints) WriteSkinVertex(mesh, pidx, k, &dm);
      if (pidx < nPoints) WriteSkinInfluenceVertex(mesh, pidx, k, &dm);
      dm.indices[k] = static_cast<uint32_t>(k);
    }
  }
  FinalizeInfluenceTexture(&dm);

  // Generate smooth normals if none were provided/usable.
  if (!gotNormals) {
    const float sign = mesh.is_rightHanded ? 1.0f : -1.0f;
    for (auto& v : dm.vertices) { v.nx = v.ny = v.nz = 0.0f; }
    for (size_t t = 0; t + 2 < dm.indices.size(); t += 3) {
      uint32_t i0 = dm.indices[t], i1 = dm.indices[t + 1], i2 = dm.indices[t + 2];
      if (i0 >= dm.vertices.size() || i1 >= dm.vertices.size() ||
          i2 >= dm.vertices.size())
        continue;
      DrawVertex& a = dm.vertices[i0];
      DrawVertex& b = dm.vertices[i1];
      DrawVertex& c = dm.vertices[i2];
      float e1[3] = {b.px - a.px, b.py - a.py, b.pz - a.pz};
      float e2[3] = {c.px - a.px, c.py - a.py, c.pz - a.pz};
      float nx = (e1[1] * e2[2] - e1[2] * e2[1]) * sign;
      float ny = (e1[2] * e2[0] - e1[0] * e2[2]) * sign;
      float nz = (e1[0] * e2[1] - e1[1] * e2[0]) * sign;
      a.nx += nx; a.ny += ny; a.nz += nz;
      b.nx += nx; b.ny += ny; b.nz += nz;
      c.nx += nx; c.ny += ny; c.nz += nz;
    }
    for (auto& v : dm.vertices) {
      float len = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
      if (len > 1e-8f) { v.nx /= len; v.ny /= len; v.nz /= len; }
    }
  }

  // --- Blendshape targets, remapped to DrawVertex order (for GPU morph) ---
  if (!mesh.targets.empty()) {
    // point index -> the DrawVertices that came from it (single-index: 1:1;
    // facevarying: one per face-vertex referencing the point).
    std::vector<std::vector<uint32_t>> p2v(nPoints);
    if (mesh.is_single_indexable) {
      for (uint32_t i = 0; i < nPoints && i < dm.vertices.size(); ++i) {
        p2v[i].push_back(i);
      }
    } else {
      for (uint32_t k = 0; k < srcIndices.size(); ++k) {
        const uint32_t pidx = srcIndices[k];
        if (pidx < nPoints) p2v[pidx].push_back(k);
      }
    }
    for (const auto& kv : mesh.targets) {
      const tydra::ShapeTarget& tgt = kv.second;
      const size_t no = tgt.pointOffsets.size();
      MorphTargetCPU mt;
      mt.name = kv.first;

      // In-between shapes. tydra carries them in `tgt.inbetweens`, already
      // reordered parallel to `pointIndices`/`pointOffsets` (so the same p2v
      // expansion below applies, single-indexable and facevarying alike).
      // Sort ascending by weight so usdWeights stays ascending; ibSrc stays
      // index-parallel to ibOut so the expansion below is unambiguous.
      std::vector<MorphInbetweenCPU> ibOut;
      std::vector<const tydra::InbetweenShapeTarget*> ibSrc;
      {
        std::vector<const tydra::InbetweenShapeTarget*> sorted;
        sorted.reserve(tgt.inbetweens.size());
        for (const auto& ib : tgt.inbetweens) sorted.push_back(&ib.second);
        std::sort(sorted.begin(), sorted.end(),
                  [](const tydra::InbetweenShapeTarget* a,
                     const tydra::InbetweenShapeTarget* b) {
                    return a->weight < b->weight;
                  });
        for (const tydra::InbetweenShapeTarget* ib : sorted) {
          if (ib->pointOffsets.size() != tgt.pointIndices.size()) continue;
          MorphInbetweenCPU mi;
          mi.weight = ib->weight;
          ibOut.push_back(std::move(mi));
          ibSrc.push_back(ib);
        }
      }

      for (size_t e = 0; e < tgt.pointIndices.size() && e < no; ++e) {
        const uint32_t pidx = tgt.pointIndices[e];
        if (pidx >= nPoints) continue;
        for (uint32_t dv : p2v[pidx]) {
          mt.vtx.push_back(dv);
          mt.dpos.push_back(tgt.pointOffsets[e][0]);
          mt.dpos.push_back(tgt.pointOffsets[e][1]);
          mt.dpos.push_back(tgt.pointOffsets[e][2]);
          // Same expansion for each usable in-between sample.
          for (size_t s = 0; s < ibOut.size(); ++s) {
            const auto& src = ibSrc[s]->pointOffsets[e];
            ibOut[s].dpos.push_back(src[0]);
            ibOut[s].dpos.push_back(src[1]);
            ibOut[s].dpos.push_back(src[2]);
          }
        }
      }
      if (!mt.vtx.empty()) {
        mt.inbetweens = std::move(ibOut);
        dm.morphs.push_back(std::move(mt));
      }
    }
  }

  // Per-vertex blendshape influence: the largest single-target displacement
  // magnitude (world units) at each vertex. Drives the BlendInfluence AOV.
  if (!dm.morphs.empty()) {
    dm.morphInfluence.assign(dm.vertices.size(), 0.0f);
    for (const MorphTargetCPU& mt : dm.morphs) {
      for (size_t e = 0; e < mt.vtx.size(); ++e) {
        const uint32_t v = mt.vtx[e];
        if (v >= dm.morphInfluence.size()) continue;
        const float dx = mt.dpos[e * 3 + 0], dy = mt.dpos[e * 3 + 1],
                    dz = mt.dpos[e * 3 + 2];
        const float mag = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (mag > dm.morphInfluence[v]) dm.morphInfluence[v] = mag;
      }
    }
  }

  // --- GPU-morph channels (raster path) ---
  // Flatten each target's primary + in-between samples into mesh-local "channels",
  // then invert the sparse target storage into a per-vertex CSR list of
  // (channelId, dx, dy, dz). The vertex shader sums coeff[channelId] * delta; the
  // CPU computes the tiny per-channel coefficients each frame. Channel order per
  // target: in-betweens (ascending) then the primary, so usdWeights stays ascending.
  //
  // Built two-pass (count -> prefix-sum -> scatter) straight into the flat
  // half-precision buffer, with NO per-vertex vector-of-vectors intermediate --
  // at facial scale that temporary was the build-time peak (~16 B x targets x
  // points plus a heap allocation per vertex).
  if (!dm.morphs.empty()) {
    // A channel = one sparse delta stream (a target's primary or an in-between).
    struct Chan {
      int id;
      const std::vector<float>* dpos;
      const std::vector<uint32_t>* vtx;
    };
    std::vector<Chan> chans;
    int nextChannel = 0;
    dm.morphTargetChannels.clear();
    dm.morphTargetChannels.reserve(dm.morphs.size());
    for (const MorphTargetCPU& mt : dm.morphs) {
      MorphTargetChannelsCPU tc;
      tc.name = mt.name;
      for (const MorphInbetweenCPU& ib : mt.inbetweens) {
        const int ch = nextChannel++;
        tc.usdWeights.push_back(ib.weight);
        tc.channelIds.push_back(ch);
        chans.push_back({ch, &ib.dpos, &mt.vtx});
      }
      const int chPrimary = nextChannel++;
      tc.usdWeights.push_back(1.0f);
      tc.channelIds.push_back(chPrimary);
      chans.push_back({chPrimary, &mt.dpos, &mt.vtx});
      dm.morphTargetChannels.push_back(std::move(tc));
    }
    dm.morphChannelCount = nextChannel;

    const size_t nv = dm.vertices.size();
    // Same guard in both passes so the count matches the scatter exactly.
    auto usable = [](const std::vector<float>& dp, size_t e, uint32_t v,
                     size_t nverts) {
      return v < nverts && e * 3 + 2 < dp.size();
    };
    // Pass 1: count entries per vertex.
    std::vector<uint32_t> count(nv, 0u);
    for (const Chan& c : chans) {
      for (size_t e = 0; e < c.vtx->size(); ++e) {
        const uint32_t v = (*c.vtx)[e];
        if (usable(*c.dpos, e, v, nv)) count[v]++;
      }
    }
    // Prefix-sum into morphOffsetCount (offset,count per vertex).
    dm.morphOffsetCount.assign(nv * 2, 0u);
    uint64_t total = 0;
    for (size_t v = 0; v < nv; ++v) {
      dm.morphOffsetCount[v * 2 + 0] = static_cast<uint32_t>(total);
      dm.morphOffsetCount[v * 2 + 1] = count[v];
      total += count[v];
    }
    // Pass 2: scatter [channelId, dx, dy, dz] halfs at a running per-vertex
    // cursor, plus the parallel uint16 channelId side buffer (GL skip pre-check).
    auto h = [](float f) { return tinyusdz::value::float_to_half_full(f).value; };
    dm.morphDeltaHalf.assign(total * 4, 0);
    dm.morphChannelId.assign(total, 0);
    std::vector<uint32_t> cursor(nv, 0u);
    for (const Chan& c : chans) {
      const std::vector<float>& dp = *c.dpos;
      const std::vector<uint32_t>& vtx = *c.vtx;
      const uint16_t chHalf = h(static_cast<float>(c.id));
      const uint16_t chId = static_cast<uint16_t>(c.id);
      for (size_t e = 0; e < vtx.size(); ++e) {
        const uint32_t v = vtx[e];
        if (!usable(dp, e, v, nv)) continue;
        const uint64_t slot = dm.morphOffsetCount[v * 2 + 0] + cursor[v]++;
        uint16_t* o = &dm.morphDeltaHalf[slot * 4];
        o[0] = chHalf;  // legacy fallback; the skip reads channelId from morphChannelId
        o[1] = h(dp[e * 3 + 0]);
        o[2] = h(dp[e * 3 + 1]);
        o[3] = h(dp[e * 3 + 2]);
        dm.morphChannelId[slot] = chId;
      }
    }

    // dm.morphs (the CPU sparse per-vertex deltas) is now redundant: the GPU
    // raster path morphs from morphDeltaHalf, RT backends morph via the tydra
    // targets (DeformSkinnedMeshes), the influence AOV is baked into
    // morphInfluence above, and the blend editor reads names/weights from
    // morphTargetChannels. Free it -- at facial scale (64+ targets, 64k+ pts)
    // this is the dominant CPU copy (~16 B x targets x points). A manual-blend
    // reconvert rebuilds the DrawScene from scratch if a CPU path ever needs it.
    std::vector<MorphTargetCPU>().swap(dm.morphs);
  }

  // --- Submeshes (group triangles by material) ---
  const size_t triCount = dm.indices.size() / 3;

  // Per-triangle source USD face id (pre-grouping order): expand the converter's
  // per-original-face triangle counts. Triangulation keeps face order, so the
  // t-th triangle belongs to face triFacePre[t].
  std::vector<uint32_t> triFacePre;
  if (mesh.is_triangulated() && !mesh.triangulatedFaceCounts.empty()) {
    triFacePre.reserve(triCount);
    for (uint32_t f = 0; f < mesh.triangulatedFaceCounts.size(); ++f)
      for (uint32_t k = 0; k < mesh.triangulatedFaceCounts[f]; ++k)
        triFacePre.push_back(f);
  }
  if (triFacePre.size() != triCount) triFacePre.clear();  // mismatch -> disable

  if (mesh.material_subsetMap.empty()) {
    DrawSubmesh sub;
    sub.indexOffset = 0;
    sub.indexCount = static_cast<uint32_t>(dm.indices.size());
    sub.materialId = mesh.material_id;
    dm.submeshes.push_back(sub);
    dm.sourceFaceId = std::move(triFacePre);  // no reorder
  } else {
    std::vector<int> triMat(triCount, mesh.material_id);
    for (const auto& kv : mesh.material_subsetMap) {
      const tydra::MaterialSubset& ss = kv.second;
      for (int triIdx : ss.indices()) {
        if (triIdx >= 0 && static_cast<size_t>(triIdx) < triCount) {
          triMat[static_cast<size_t>(triIdx)] = ss.material_id;
        }
      }
    }
    // Bucket triangles by material id, preserving order within a material. Bucket
    // the source-face id in lockstep so it stays parallel to the grouped tris.
    std::map<int, std::vector<uint32_t>> buckets;
    std::map<int, std::vector<uint32_t>> faceBuckets;
    for (size_t t = 0; t < triCount; ++t) {
      auto& bucket = buckets[triMat[t]];
      bucket.push_back(dm.indices[t * 3 + 0]);
      bucket.push_back(dm.indices[t * 3 + 1]);
      bucket.push_back(dm.indices[t * 3 + 2]);
      if (!triFacePre.empty()) faceBuckets[triMat[t]].push_back(triFacePre[t]);
    }
    std::vector<uint32_t> grouped;
    std::vector<uint32_t> groupedFace;
    grouped.reserve(dm.indices.size());
    for (auto& kv : buckets) {
      DrawSubmesh sub;
      sub.indexOffset = static_cast<uint32_t>(grouped.size());
      sub.indexCount = static_cast<uint32_t>(kv.second.size());
      sub.materialId = kv.first;
      grouped.insert(grouped.end(), kv.second.begin(), kv.second.end());
      if (!triFacePre.empty()) {
        const auto& fb = faceBuckets[kv.first];
        groupedFace.insert(groupedFace.end(), fb.begin(), fb.end());
      }
      dm.submeshes.push_back(sub);
    }
    dm.indices.swap(grouped);
    dm.sourceFaceId = std::move(groupedFace);
  }

  // Authored constant primvars:displayColor (single element) is captured by
  // Tydra in RenderMesh::displayColor, not vertex_colors. Replicate it into the
  // per-vertex color stream (which the shaders multiply into the base color, as
  // they already do for per-vertex displayColor) so simple colored prims and
  // analytic primitives show their authored color instead of the default gray.
  if (mesh.has_authored_displayColor && dm.vertexColors.empty() &&
      !dm.vertices.empty()) {
    const float dc[3] = {mesh.displayColor.r, mesh.displayColor.g,
                         mesh.displayColor.b};
    dm.vertexColors.resize(dm.vertices.size() * 3);
    for (size_t i = 0; i < dm.vertices.size(); ++i) {
      dm.vertexColors[i * 3 + 0] = dc[0];
      dm.vertexColors[i * 3 + 1] = dc[1];
      dm.vertexColors[i * 3 + 2] = dc[2];
    }
  }

  // World left as identity; PlaceDrawMesh applies the node transform.
  light3d::Mat4 ident = light3d::Mat4::identity();
  std::memcpy(dm.world, ident.m, sizeof(ident.m));

  *dmOut = std::move(dm);
  return true;
}

// Apply a world transform to a built DrawMeshCPU and compute its world-space
// AABB from the (local) vertex positions.
void PlaceDrawMesh(DrawMeshCPU* dm, const matrix4d& worldMat) {
  light3d::Mat4 world = light3d::Mat4::identity();
  MatToColMajor(worldMat, world.m);
  std::memcpy(dm->world, world.m, sizeof(world.m));

  // World bounds from the vertices THEMSELVES, not from the 8 corners of the local
  // AABB transformed into world -- that box is a strict superset (rotate a box and
  // its axis-aligned hull grows), so it disagreed with every other bounds
  // computation in the app. UpdateMeshBoundsFromVertices, which re-derives the box
  // once GPU skinning poses a mesh, walks the vertices; so the same scene came out
  // with a LOOSE box under CPU skinning and a TIGHT one under GPU, which moved the
  // ground grid and the depth normalization (both scale with scene bounds) between
  // two paths that render identical geometry. One extra transform per vertex, in a
  // loop that already visits every vertex.
  float wmin[3] = {1e30f, 1e30f, 1e30f};
  float wmax[3] = {-1e30f, -1e30f, -1e30f};
  for (const auto& v : dm->vertices) {
    const light3d::Vec3 wp =
        light3d::transformPoint(world, light3d::Vec3{v.px, v.py, v.pz});
    const float wparr[3] = {wp.x, wp.y, wp.z};
    for (int c = 0; c < 3; ++c) {
      wmin[c] = std::min(wmin[c], wparr[c]);
      wmax[c] = std::max(wmax[c], wparr[c]);
    }
  }
  for (int c = 0; c < 3; ++c) { dm->aabbMin[c] = wmin[c]; dm->aabbMax[c] = wmax[c]; }
}

// Union the world AABB of every built mesh into the scene bounds.
void ComputeSceneBounds(DrawScene* out) {
  bool first = true;
  auto extend = [&](const float p[3]) {
    if (first) {
      for (int c = 0; c < 3; ++c) { out->aabbMin[c] = p[c]; out->aabbMax[c] = p[c]; }
      first = false;
    } else {
      for (int c = 0; c < 3; ++c) {
        out->aabbMin[c] = std::min(out->aabbMin[c], p[c]);
        out->aabbMax[c] = std::max(out->aabbMax[c], p[c]);
      }
    }
  };
  for (const auto& dm : out->meshes) {
    // Instanced prototypes carry world-space bounds in aabbMin/aabbMax (set by
    // BuildDrawInstances), so the plain min/max below is already correct.
    extend(dm.aabbMin);
    extend(dm.aabbMax);
  }
  // Include UsdVol volumes: transform their object-space AABB corners to world
  // (DrawVolumeCPU::world is column-major: p' = M*p).
  for (const auto& dv : out->volumes) {
    for (int corner = 0; corner < 8; ++corner) {
      float o[3] = {(corner & 1) ? dv.aabbMax[0] : dv.aabbMin[0],
                    (corner & 2) ? dv.aabbMax[1] : dv.aabbMin[1],
                    (corner & 4) ? dv.aabbMax[2] : dv.aabbMin[2]};
      const float* M = dv.world;
      float w[3];
      for (int i = 0; i < 3; ++i) {
        w[i] = M[0 * 4 + i] * o[0] + M[1 * 4 + i] * o[1] +
               M[2 * 4 + i] * o[2] + M[3 * 4 + i];
      }
      extend(w);
    }
  }
  out->hasBounds = !first;
}

bool Normalize3(float v[3]) {
  const float len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
  if (len2 <= 1.0e-12f) return false;
  const float inv = 1.0f / std::sqrt(len2);
  v[0] *= inv;
  v[1] *= inv;
  v[2] *= inv;
  return true;
}

// (definition moved below the anonymous namespace so the `next` loader can
// derive the preview key light from its own lights too; see UpdatePreviewLight)

void FinalizeSkinningLayout(const tydra::RenderScene& rs, DrawScene* out) {
  if (!out) return;
  int nextMatrix = 0;
  for (DrawMeshCPU& dm : out->meshes) {
    const bool hasAttribs =
        !dm.jointIdx.empty() && !dm.jointWt.empty() &&
        dm.jointIdx.size() == dm.vertices.size() * 4 &&
        dm.jointWt.size() == dm.vertices.size() * 4;
    if (!hasAttribs || dm.skelId < 0 ||
        static_cast<size_t>(dm.skelId) >= rs.skeletons.size()) {
      dm.jointIdx.clear();
      dm.jointWt.clear();
      dm.influenceOffsetCount.clear();
      dm.influenceTexels.clear();
      dm.skelId = -1;
      dm.skinMatrixBase = -1;
      continue;
    }
    const size_t nj = rs.skeletons[static_cast<size_t>(dm.skelId)].num_joints();
    if (nj == 0 || nj > static_cast<size_t>(std::numeric_limits<int>::max() - nextMatrix)) {
      dm.jointIdx.clear();
      dm.jointWt.clear();
      dm.influenceOffsetCount.clear();
      dm.influenceTexels.clear();
      dm.skelId = -1;
      dm.skinMatrixBase = -1;
      continue;
    }
    dm.skinMatrixBase = nextMatrix;
    for (uint32_t& j : dm.jointIdx) {
      if (j < nj) {
        j += static_cast<uint32_t>(dm.skinMatrixBase);
      } else {
        j = static_cast<uint32_t>(dm.skinMatrixBase);
      }
    }
    const bool hasFullInfluences =
        dm.influenceOffsetCount.size() == dm.vertices.size() * 2 &&
        !dm.influenceTexels.empty() && dm.influenceTexels.size() % 4 == 0;
    if (hasFullInfluences) {
      const size_t texels = dm.influenceTexels.size() / 4;
      for (size_t t = 0; t < texels; ++t) {
        const size_t base = t * 4;
        if (!(dm.influenceTexels[base + 1] > 0.0f)) continue;
        const uint32_t localJoint =
            static_cast<uint32_t>(std::max(0.0f, dm.influenceTexels[base] + 0.5f));
        dm.influenceTexels[base] =
            static_cast<float>(dm.skinMatrixBase +
                               (localJoint < nj ? localJoint : uint32_t{0}));
      }
    } else {
      dm.influenceOffsetCount.clear();
      dm.influenceTexels.clear();
      dm.influenceTexWidth = 0;
      dm.influenceTexHeight = 0;
      dm.maxInfluencesPerVertex = 0;
    }
    nextMatrix += static_cast<int>(nj);
  }
  out->boneMatrixCount = nextMatrix;
}

// Returns true if adding `dm` would exceed the triangle / vertex-byte budget.
bool OverBudget(const DrawScene& out, size_t cumulativeVertexBytes,
                const DrawMeshCPU& dm, const LoadControl& ctrl) {
  const size_t thisTris = dm.indices.size() / 3;
  const size_t estBytes =
      dm.vertices.size() * sizeof(DrawVertex) +
      dm.jointIdx.size() * sizeof(uint32_t) +
      dm.jointWt.size() * sizeof(float) +
      dm.influenceOffsetCount.size() * sizeof(uint32_t) +
      dm.influenceTexels.size() * sizeof(float) +
      dm.indices.size() * sizeof(uint32_t);
  return out.triangleCount + thisTris > ctrl.maxTriangles ||
         cumulativeVertexBytes + estBytes > ctrl.maxVertexBytes;
}

}  // namespace

// Derive the raster preview key light from the scene lights: the first Distant
// light's (reversed) direction, else the first finite light's direction from
// the scene center, else a fixed fallback. Public so the `next` loader (which
// builds its own DrawScene) can apply the same derivation.
LoadDiagnostics CategorizeLoadWarnings(
    const std::string& warn_blob, const std::vector<std::string>& skipped) {
  LoadDiagnostics d;
  auto contains = [](const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
  };
  size_t start = 0;
  while (start <= warn_blob.size()) {
    size_t nl = warn_blob.find('\n', start);
    const std::string line = warn_blob.substr(
        start, nl == std::string::npos ? std::string::npos : nl - start);
    start = (nl == std::string::npos) ? warn_blob.size() + 1 : nl + 1;
    // Trim trailing whitespace/CR; skip blank lines.
    size_t end = line.find_last_not_of(" \t\r");
    if (end == std::string::npos) continue;
    const std::string l = line.substr(0, end + 1);

    // Order matters: a full fallback ("using default material") is the most
    // severe and is checked first; unsupported-mtlx before the generic
    // missing-texture bucket since its message also mentions resolution.
    int* bucket = nullptr;
    if (contains(l, "using default material") ||
        contains(l, "Material conversion failed")) {
      bucket = &d.degraded_material;
    } else if (contains(l, "Unsupported node type")) {
      bucket = &d.unsupported_mtlx;
    } else if (contains(l, "failed to load texture") ||
               contains(l, "Failed to load texture") ||
               contains(l, "Failed to load image") ||
               contains(l, "could not be resolved to a UsdUVTexture") ||
               contains(l, "could not be resolved to a texture") ||
               contains(l, "Failed to find MaterialX texture") ||
               contains(l, "Failed to convert MaterialX texture")) {
      bucket = &d.missing_texture;
    } else {
      bucket = &d.other;
    }
    (*bucket)++;
    if (d.examples.size() < 6 && bucket != &d.other) d.examples.push_back(l);
  }

  d.skipped = static_cast<int>(skipped.size());
  for (const std::string& s : skipped) {
    if (d.examples.size() >= 6) break;
    d.examples.push_back("skipped: " + s);
  }
  return d;
}

void UpdatePreviewLight(DrawScene* draw) {
  if (!draw) return;
  float fallback[3]{0.5f, 0.8f, 0.6f};
  Normalize3(fallback);
  for (int i = 0; i < 3; ++i) {
    draw->previewLightDir[i] = fallback[i];
    draw->previewLightColor[i] = 1.0f;
  }
  draw->hasPreviewLight = false;

  const DrawLightCPU* chosen = nullptr;
  for (const DrawLightCPU& light : draw->lights) {
    if (light.type == DrawLightCPU::Type::Distant) {
      chosen = &light;
      break;
    }
    if (!chosen && light.type != DrawLightCPU::Type::Dome &&
        light.type != DrawLightCPU::Type::Portal) {
      chosen = &light;
    }
  }
  if (!chosen) return;

  float dir[3]{fallback[0], fallback[1], fallback[2]};
  if (chosen->type == DrawLightCPU::Type::Distant) {
    // DrawLightCPU::direction is the light emission direction; preview shaders
    // use L as the vector from the shaded point toward the light.
    dir[0] = -chosen->direction[0];
    dir[1] = -chosen->direction[1];
    dir[2] = -chosen->direction[2];
  } else {
    float center[3]{0.0f, 0.0f, 0.0f};
    if (draw->hasBounds) {
      for (int i = 0; i < 3; ++i) {
        center[i] = 0.5f * (draw->aabbMin[i] + draw->aabbMax[i]);
      }
    }
    for (int i = 0; i < 3; ++i) {
      dir[i] = chosen->position[i] - center[i];
    }
  }
  if (!Normalize3(dir)) {
    dir[0] = fallback[0];
    dir[1] = fallback[1];
    dir[2] = fallback[2];
  }

  for (int i = 0; i < 3; ++i) {
    draw->previewLightDir[i] = dir[i];
    draw->previewLightColor[i] = chosen->normalizedColor[i];
  }
  draw->hasPreviewLight = true;
}

void ApplyMeshPurposes(const tinyusdz::Stage& stage, DrawScene* draw) {
  if (!draw) return;
  for (DrawMeshCPU& mesh : draw->meshes) {
    mesh.purpose = mesh.absPath.empty() ? "default"
                                        : ResolveInheritedPurpose(stage, mesh.absPath);
    mesh.kindId =
        mesh.absPath.empty() ? 0 : ResolveInheritedKind(stage, mesh.absPath);
  }
}

void BuildDrawVolumes(const tydra::RenderScene& rs, DrawScene* out) {
  for (const auto& v : rs.volumes) {
    int fi = v.density_field_index();
    if (fi < 0) continue;
    const auto& f = v.fields[static_cast<size_t>(fi)];
    if (f.buffer_id < 0 || static_cast<size_t>(f.buffer_id) >= rs.buffers.size())
      continue;
    const auto& buf = rs.buffers[static_cast<size_t>(f.buffer_id)];
    const size_t n =
        size_t(f.dim[0]) * size_t(f.dim[1]) * size_t(f.dim[2]);
    if (n == 0 || buf.data.size() < n * sizeof(float)) continue;

    DrawVolumeCPU dv;
    dv.name = v.prim_name;
    dv.dim[0] = f.dim[0];
    dv.dim[1] = f.dim[1];
    dv.dim[2] = f.dim[2];
    dv.density.resize(n);
    std::memcpy(dv.density.data(), buf.data.data(), n * sizeof(float));
    MatToColMajor(v.world_matrix, dv.world);
    for (int a = 0; a < 3; a++) {
      dv.aabbMin[a] = f.bounds_min[a];
      dv.aabbMax[a] = f.bounds_max[a];
      dv.albedo[a] = v.albedo[a];
      dv.emission[a] = v.emission_color[a] * v.emission_scale;
    }
    dv.densityScale = v.density_scale;
    dv.background = f.background;
    out->volumes.push_back(std::move(dv));
  }
}

struct F3 {
  float x{0}, y{0}, z{0};
};

F3 Add(F3 a, F3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
F3 Sub(F3 a, F3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
F3 Mul(F3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(F3 a, F3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
F3 Cross(F3 a, F3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}
float Length(F3 a) { return std::sqrt(Dot(a, a)); }
F3 Normalize(F3 a, F3 fallback = {0, 1, 0}) {
  const float l = Length(a);
  return l > 1.0e-12f ? Mul(a, 1.0f / l) : fallback;
}

F3 ToF3(const tinyusdz::value::point3f& p) { return {p[0], p[1], p[2]}; }
void AppendGeneratedMaterial(DrawScene* out, const float color[3],
                             const std::string& name, int* materialId) {
  if (!out || !materialId) return;
  DrawMaterialCPU mat;
  mat.name = name;
  mat.baseColor[0] = color[0];
  mat.baseColor[1] = color[1];
  mat.baseColor[2] = color[2];
  mat.roughness = 0.75f;
  *materialId = static_cast<int>(out->materials.size());
  out->materials.push_back(std::move(mat));
}

void FinalizeGeneratedMesh(DrawMeshCPU* dm) {
  if (!dm || dm->vertices.empty()) return;
  dm->sourceFaceId.resize(dm->indices.size() / 3);
  for (size_t i = 0; i < dm->sourceFaceId.size(); ++i) {
    dm->sourceFaceId[i] = static_cast<uint32_t>(i);
  }
  dm->wireframeIndices.clear();
  for (size_t i = 0; i + 2 < dm->indices.size(); i += 3) {
    const uint32_t a = dm->indices[i + 0];
    const uint32_t b = dm->indices[i + 1];
    const uint32_t c = dm->indices[i + 2];
    dm->wireframeIndices.push_back(a);
    dm->wireframeIndices.push_back(b);
    dm->wireframeIndices.push_back(b);
    dm->wireframeIndices.push_back(c);
    dm->wireframeIndices.push_back(c);
    dm->wireframeIndices.push_back(a);
  }
}

void AddQuad(DrawMeshCPU* dm, const DrawVertex& a, const DrawVertex& b,
             const DrawVertex& c, const DrawVertex& d) {
  const uint32_t base = static_cast<uint32_t>(dm->vertices.size());
  dm->vertices.push_back(a);
  dm->vertices.push_back(b);
  dm->vertices.push_back(c);
  dm->vertices.push_back(d);
  dm->indices.insert(dm->indices.end(),
                     {base + 0, base + 1, base + 2, base + 0, base + 2,
                      base + 3});
}

float CurveWidthAt(const std::vector<float>& widths, size_t pointIndex,
                   size_t curveIndex, size_t pointCount) {
  if (widths.empty()) return 0.04f;
  if (widths.size() == 1) return widths[0];
  if (pointIndex < widths.size()) return widths[pointIndex];
  if (curveIndex < widths.size()) return widths[curveIndex];
  if (pointCount > 0 && widths.size() >= pointCount) {
    return widths[std::min(pointIndex, widths.size() - 1)];
  }
  return widths.back();
}

bool BuildBasisCurveRibbonMesh(const tinyusdz::GeomBasisCurves& curves,
                               const std::string& absPath,
                               const tinyusdz::value::matrix4d& world,
                               int materialId, DrawMeshCPU* dm) {
  if (!dm) return false;
  const std::vector<tinyusdz::value::point3f> pts = curves.get_points();
  const std::vector<int> counts = curves.get_curveVertexCounts();
  const std::vector<float> widths = curves.get_widths();
  if (pts.empty() || counts.empty()) return false;

  const size_t curveSlash = absPath.find_last_of('/');
  dm->name = curveSlash == std::string::npos ? absPath : absPath.substr(curveSlash + 1);
  dm->absPath = absPath;
  MatToColMajor(world, dm->world);
  dm->doubleSided = true;
  dm->geometricNormal = true;
  dm->purpose = PurposeName(curves.purpose.get_value());
  dm->submeshes.push_back({0, 0, materialId});

  size_t cursor = 0;
  for (size_t ci = 0; ci < counts.size(); ++ci) {
    const int count = counts[ci];
    if (count < 2 || cursor + static_cast<size_t>(count) > pts.size()) {
      cursor += count > 0 ? static_cast<size_t>(count) : 0;
      continue;
    }
    for (int j = 0; j + 1 < count; ++j) {
      const size_t i0 = cursor + static_cast<size_t>(j);
      const size_t i1 = cursor + static_cast<size_t>(j + 1);
      const F3 p0 = ToF3(pts[i0]);
      const F3 p1 = ToF3(pts[i1]);
      const F3 dir = Normalize(Sub(p1, p0), {1, 0, 0});
      F3 side = Cross(dir, {0, 0, 1});
      if (Length(side) < 1.0e-6f) side = Cross(dir, {0, 1, 0});
      side = Normalize(side);
      const float w0 = std::max(0.001f, CurveWidthAt(widths, i0, ci, pts.size()));
      const float w1 = std::max(0.001f, CurveWidthAt(widths, i1, ci, pts.size()));
      const F3 n = Normalize(Cross(side, dir), {0, 0, 1});
      DrawVertex v0{p0.x - side.x * w0 * 0.5f, p0.y - side.y * w0 * 0.5f,
                    p0.z - side.z * w0 * 0.5f, n.x, n.y, n.z, 0.0f, 0.0f};
      DrawVertex v1{p1.x - side.x * w1 * 0.5f, p1.y - side.y * w1 * 0.5f,
                    p1.z - side.z * w1 * 0.5f, n.x, n.y, n.z, 1.0f, 0.0f};
      DrawVertex v2{p1.x + side.x * w1 * 0.5f, p1.y + side.y * w1 * 0.5f,
                    p1.z + side.z * w1 * 0.5f, n.x, n.y, n.z, 1.0f, 1.0f};
      DrawVertex v3{p0.x + side.x * w0 * 0.5f, p0.y + side.y * w0 * 0.5f,
                    p0.z + side.z * w0 * 0.5f, n.x, n.y, n.z, 0.0f, 1.0f};
      AddQuad(dm, v0, v1, v2, v3);
    }
    cursor += static_cast<size_t>(count);
  }
  dm->submeshes[0].indexCount = static_cast<uint32_t>(dm->indices.size());
  FinalizeGeneratedMesh(dm);
  return !dm->indices.empty();
}

double BsplineBasis(int i, int degree, double u, const std::vector<double>& knots) {
  if (degree == 0) {
    const bool last = !knots.empty() && (i + 1 < static_cast<int>(knots.size())) &&
                      std::abs(u - knots.back()) < 1.0e-12 &&
                      std::abs(knots[i + 1] - knots.back()) < 1.0e-12 &&
                      knots[i] < knots[i + 1];
    return (knots[i] <= u && u < knots[i + 1]) || last ? 1.0 : 0.0;
  }
  double v = 0.0;
  const double d0 = knots[i + degree] - knots[i];
  if (std::abs(d0) > 1.0e-12) {
    v += (u - knots[i]) / d0 * BsplineBasis(i, degree - 1, u, knots);
  }
  const double d1 = knots[i + degree + 1] - knots[i + 1];
  if (std::abs(d1) > 1.0e-12) {
    v += (knots[i + degree + 1] - u) / d1 *
         BsplineBasis(i + 1, degree - 1, u, knots);
  }
  return v;
}

F3 EvalNurbsPatch(const std::vector<tinyusdz::value::point3f>& pts,
                  const std::vector<double>& weights, int uCount, int vCount,
                  int uOrder, int vOrder, const std::vector<double>& uKnots,
                  const std::vector<double>& vKnots, double u, double v) {
  F3 sum{0, 0, 0};
  double wsum = 0.0;
  const int uDeg = std::max(0, uOrder - 1);
  const int vDeg = std::max(0, vOrder - 1);
  for (int j = 0; j < vCount; ++j) {
    const double bv = BsplineBasis(j, vDeg, v, vKnots);
    if (bv == 0.0) continue;
    for (int i = 0; i < uCount; ++i) {
      const size_t idx = static_cast<size_t>(j * uCount + i);
      if (idx >= pts.size()) continue;
      const double bu = BsplineBasis(i, uDeg, u, uKnots);
      const double wt = idx < weights.size() ? weights[idx] : 1.0;
      const double b = bu * bv * wt;
      const F3 p = ToF3(pts[idx]);
      sum = Add(sum, Mul(p, static_cast<float>(b)));
      wsum += b;
    }
  }
  if (std::abs(wsum) > 1.0e-12) sum = Mul(sum, static_cast<float>(1.0 / wsum));
  return sum;
}

bool BuildNurbsPatchMesh(const tinyusdz::GeomNurbsPatch& patch,
                         const std::string& absPath,
                         const tinyusdz::value::matrix4d& world,
                         int materialId, DrawMeshCPU* dm) {
  if (!dm) return false;
  auto ptsOpt = patch.points.get_value();
  auto uCountOpt = patch.uVertexCount.get_value();
  auto vCountOpt = patch.vVertexCount.get_value();
  auto uOrderOpt = patch.uOrder.get_value();
  auto vOrderOpt = patch.vOrder.get_value();
  auto uKnotsOpt = patch.uKnots.get_value();
  auto vKnotsOpt = patch.vKnots.get_value();
  if (!ptsOpt || !uCountOpt || !vCountOpt || !uOrderOpt || !vOrderOpt ||
      !uKnotsOpt || !vKnotsOpt) {
    return false;
  }
  const auto& ptsAnim = ptsOpt.value();
  const auto& uCountAnim = uCountOpt.value();
  const auto& vCountAnim = vCountOpt.value();
  const auto& uOrderAnim = uOrderOpt.value();
  const auto& vOrderAnim = vOrderOpt.value();
  const auto& uKnotsAnim = uKnotsOpt.value();
  const auto& vKnotsAnim = vKnotsOpt.value();
  std::vector<tinyusdz::value::point3f> pts;
  std::vector<double> uKnots, vKnots, weights;
  int uCount = 0, vCount = 0, uOrder = 0, vOrder = 0;
  if (!ptsAnim.get_scalar(&pts) || !uCountAnim.get_scalar(&uCount) ||
      !vCountAnim.get_scalar(&vCount) || !uOrderAnim.get_scalar(&uOrder) ||
      !vOrderAnim.get_scalar(&vOrder) || !uKnotsAnim.get_scalar(&uKnots) ||
      !vKnotsAnim.get_scalar(&vKnots)) {
    return false;
  }
  if (uCount < 2 || vCount < 2 || uOrder < 1 || vOrder < 1 ||
      pts.size() < static_cast<size_t>(uCount * vCount) ||
      uKnots.size() < static_cast<size_t>(uCount + uOrder) ||
      vKnots.size() < static_cast<size_t>(vCount + vOrder)) {
    return false;
  }
  if (auto weightsOpt = patch.pointWeights.get_value()) {
    weightsOpt.value().get_scalar(&weights);
  }
  double u0 = uKnots[static_cast<size_t>(uOrder - 1)];
  double u1 = uKnots[static_cast<size_t>(uCount)];
  double v0 = vKnots[static_cast<size_t>(vOrder - 1)];
  double v1 = vKnots[static_cast<size_t>(vCount)];
  if (auto r = patch.uRange.get_value()) {
    tinyusdz::value::double2 rr;
    if (r.value().get_scalar(&rr)) { u0 = rr[0]; u1 = rr[1]; }
  }
  if (auto r = patch.vRange.get_value()) {
    tinyusdz::value::double2 rr;
    if (r.value().get_scalar(&rr)) { v0 = rr[0]; v1 = rr[1]; }
  }

  const size_t patchSlash = absPath.find_last_of('/');
  dm->name = patchSlash == std::string::npos ? absPath : absPath.substr(patchSlash + 1);
  dm->absPath = absPath;
  MatToColMajor(world, dm->world);
  dm->doubleSided = true;
  dm->purpose = PurposeName(patch.purpose.get_value());
  dm->submeshes.push_back({0, 0, materialId});
  constexpr int kUSeg = 16;
  constexpr int kVSeg = 16;
  std::vector<F3> grid(static_cast<size_t>(kUSeg + 1) *
                       static_cast<size_t>(kVSeg + 1));
  for (int y = 0; y <= kVSeg; ++y) {
    const double tv = static_cast<double>(y) / static_cast<double>(kVSeg);
    const double vv = v0 + (v1 - v0) * tv;
    for (int x = 0; x <= kUSeg; ++x) {
      const double tu = static_cast<double>(x) / static_cast<double>(kUSeg);
      const double uu = u0 + (u1 - u0) * tu;
      grid[static_cast<size_t>(y * (kUSeg + 1) + x)] =
          EvalNurbsPatch(pts, weights, uCount, vCount, uOrder, vOrder, uKnots,
                         vKnots, uu, vv);
    }
  }
  for (int y = 0; y <= kVSeg; ++y) {
    for (int x = 0; x <= kUSeg; ++x) {
      const F3 p = grid[static_cast<size_t>(y * (kUSeg + 1) + x)];
      const F3 px0 = grid[static_cast<size_t>(y * (kUSeg + 1) + std::max(0, x - 1))];
      const F3 px1 = grid[static_cast<size_t>(y * (kUSeg + 1) + std::min(kUSeg, x + 1))];
      const F3 py0 = grid[static_cast<size_t>(std::max(0, y - 1) * (kUSeg + 1) + x)];
      const F3 py1 = grid[static_cast<size_t>(std::min(kVSeg, y + 1) * (kUSeg + 1) + x)];
      const F3 n = Normalize(Cross(Sub(px1, px0), Sub(py1, py0)), {0, 0, 1});
      dm->vertices.push_back({p.x, p.y, p.z, n.x, n.y, n.z,
                              static_cast<float>(x) / kUSeg,
                              static_cast<float>(y) / kVSeg});
    }
  }
  for (int y = 0; y < kVSeg; ++y) {
    for (int x = 0; x < kUSeg; ++x) {
      const uint32_t a = static_cast<uint32_t>(y * (kUSeg + 1) + x);
      const uint32_t b = a + 1;
      const uint32_t d = static_cast<uint32_t>((y + 1) * (kUSeg + 1) + x);
      const uint32_t c = d + 1;
      dm->indices.insert(dm->indices.end(), {a, b, c, a, c, d});
    }
  }
  dm->submeshes[0].indexCount = static_cast<uint32_t>(dm->indices.size());
  FinalizeGeneratedMesh(dm);
  return !dm->indices.empty();
}

std::string ChildPath(const std::string& parent, const tinyusdz::Prim& prim) {
  const std::string name = prim.element_name();
  if (parent.empty() || parent == "/") return "/" + name;
  return parent + "/" + name;
}

void AppendCurveAndPatchMeshesRec(const tinyusdz::Prim& prim,
                                  const std::string& absPath,
                                  const tinyusdz::value::matrix4d& parentWorld,
                                  int* curveMat, int* patchMat, DrawScene* out) {
  if (!out || !curveMat || !patchMat) return;
  bool reset = false;
  tinyusdz::value::matrix4d local =
      tinyusdz::GetLocalTransform(prim, &reset, tinyusdz::value::TimeCode::Default(),
                                  tinyusdz::value::TimeSampleInterpolationType::Linear);
  tinyusdz::value::matrix4d world =
      reset ? local : (local * parentWorld);  // row-major local-first convention
  if (const auto* curves = prim.as<tinyusdz::GeomBasisCurves>()) {
    if (*curveMat < 0) {
      const float curveColor[3] = {0.95f, 0.55f, 0.12f};
      AppendGeneratedMaterial(out, curveColor, "__tusdview_basis_curve_ribbon",
                              curveMat);
    }
    DrawMeshCPU dm;
    if (BuildBasisCurveRibbonMesh(*curves, absPath, world, *curveMat, &dm)) {
      out->triangleCount += dm.indices.size() / 3;
      out->meshes.push_back(std::move(dm));
    } else {
      out->skipped.push_back("BasisCurves '" + absPath + "': unsupported/empty");
    }
  } else if (const auto* patch = prim.as<tinyusdz::GeomNurbsPatch>()) {
    if (*patchMat < 0) {
      const float patchColor[3] = {0.18f, 0.52f, 0.95f};
      AppendGeneratedMaterial(out, patchColor, "__tusdview_nurbs_patch",
                              patchMat);
    }
    DrawMeshCPU dm;
    if (BuildNurbsPatchMesh(*patch, absPath, world, *patchMat, &dm)) {
      out->triangleCount += dm.indices.size() / 3;
      out->meshes.push_back(std::move(dm));
    } else {
      out->skipped.push_back("NurbsPatch '" + absPath + "': unsupported/empty");
    }
  }
  for (const tinyusdz::Prim& child : prim.children()) {
    AppendCurveAndPatchMeshesRec(child, ChildPath(absPath, child), world,
                                 curveMat, patchMat, out);
  }
}

void AppendCurveAndPatchMeshes(const tinyusdz::Stage& stage, DrawScene* out) {
  if (!out) return;
  int curveMat = -1, patchMat = -1;
  const tinyusdz::value::matrix4d ident = tinyusdz::value::matrix4d::identity();
  for (const tinyusdz::Prim& root : stage.root_prims()) {
    AppendCurveAndPatchMeshesRec(root, ChildPath("", root), ident, &curveMat,
                                 &patchMat, out);
  }
}

// Attach USD instances (PointInstancer expansion + scenegraph instancing;
// tydra RenderScene::instances) to their prototype DrawMeshCPU. Each prototype
// mesh referenced by >=1 instance is rendered via the GPU-instanced path
// (DrawMeshCPU::instanceXforms), which ignores its `world` and draws one copy
// per instance transform. Prototypes with no instances render normally.
void BuildDrawInstances(const tydra::RenderScene& rs,
                        const std::vector<int>& rsMeshToDraw, DrawScene* out) {
  if (rs.instances.empty() || !out) return;
  // Local (proto-space) AABB of each draw mesh, captured before we start
  // attaching instances so we can re-derive world bounds afterward.
  std::vector<std::array<float, 6>> localBox(out->meshes.size());
  std::vector<size_t> sourceDraw(out->meshes.size());
  std::vector<uint8_t> touched(out->meshes.size(), 0);
  std::vector<uint8_t> suppressStatic(out->meshes.size(), 0);
  std::unordered_map<uint64_t, size_t> overrideDraw;
  auto appendInstanceColor = [](DrawMeshCPU& dm, const tydra::RenderInstance& inst) {
    const size_t instance_count = dm.instanceXforms.size() / 12;
    const size_t previous_count = instance_count ? instance_count - 1 : 0;
    if (inst.has_display_color) {
      while (dm.instanceColors.size() / 3 < previous_count) {
        dm.instanceColors.push_back(dm.flatColor[0]);
        dm.instanceColors.push_back(dm.flatColor[1]);
        dm.instanceColors.push_back(dm.flatColor[2]);
      }
      dm.instanceColors.push_back(inst.display_color[0]);
      dm.instanceColors.push_back(inst.display_color[1]);
      dm.instanceColors.push_back(inst.display_color[2]);
    } else if (!dm.instanceColors.empty()) {
      dm.instanceColors.push_back(dm.flatColor[0]);
      dm.instanceColors.push_back(dm.flatColor[1]);
      dm.instanceColors.push_back(dm.flatColor[2]);
    }
  };
  auto appendInstanceOpacity = [](DrawMeshCPU& dm, const tydra::RenderInstance& inst) {
    const size_t instance_count = dm.instanceXforms.size() / 12;
    const size_t previous_count = instance_count ? instance_count - 1 : 0;
    if (inst.has_display_opacity) {
      while (dm.instanceOpacities.size() < previous_count) {
        dm.instanceOpacities.push_back(dm.flatOpacity);
      }
      dm.instanceOpacities.push_back(
          std::max(0.0f, std::min(1.0f, inst.display_opacity)));
    } else if (!dm.instanceOpacities.empty()) {
      dm.instanceOpacities.push_back(dm.flatOpacity);
    }
  };
  for (size_t i = 0; i < rs.instances.size(); ++i) {
    const tydra::RenderInstance& inst = rs.instances[i];
    if (!inst.visible) continue;
    if (inst.mesh_id < 0 ||
        static_cast<size_t>(inst.mesh_id) >= rsMeshToDraw.size()) {
      continue;
    }
    const int di = rsMeshToDraw[static_cast<size_t>(inst.mesh_id)];
    if (di < 0 || static_cast<size_t>(di) >= out->meshes.size()) continue;
    const size_t baseDi = static_cast<size_t>(di);
    if (!touched[baseDi]) {
      const DrawMeshCPU& base = out->meshes[baseDi];
      localBox[baseDi] = {base.aabbMin[0], base.aabbMin[1],
                          base.aabbMin[2], base.aabbMax[0],
                          base.aabbMax[1], base.aabbMax[2]};
      sourceDraw[baseDi] = baseDi;
      touched[baseDi] = 1;
    }

    size_t targetDi = baseDi;
    if (inst.material_id >= 0) {
      suppressStatic[baseDi] = 1;
      const uint64_t key = (uint64_t(baseDi) << 32) |
                           uint32_t(inst.material_id);
      auto it = overrideDraw.find(key);
      if (it == overrideDraw.end()) {
        DrawMeshCPU clone = out->meshes[baseDi];
        clone.name += "_instMaterial_" + std::to_string(inst.material_id);
        clone.instanceXforms.clear();
        clone.instanceColors.clear();
        clone.instanceOpacities.clear();
        for (DrawSubmesh& sub : clone.submeshes) {
          sub.materialId = inst.material_id;
        }
        targetDi = out->meshes.size();
        out->meshes.push_back(std::move(clone));
        sourceDraw.push_back(baseDi);
        localBox.push_back(localBox[baseDi]);
        touched.push_back(1);
        suppressStatic.push_back(0);
        overrideDraw.emplace(key, targetDi);
      } else {
        targetDi = it->second;
      }
    }

    DrawMeshCPU& dm = out->meshes[targetDi];
    if (!touched[targetDi]) {
      localBox[targetDi] = localBox[baseDi];
      sourceDraw[targetDi] = baseDi;
      touched[targetDi] = 1;
    }
    // Pack the instance world matrix (row-vector matrix4d) into the 12-float
    // o2w the instanced shader expects: 3 rows, each a column of the matrix
    // (matches next_scene_loader.cc Mat4dToO2W).
    const matrix4d& mm = inst.global_matrix;
    float o2w[12];
    for (int k = 0; k < 3; ++k) {
      o2w[k * 4 + 0] = static_cast<float>(mm.m[0][k]);
      o2w[k * 4 + 1] = static_cast<float>(mm.m[1][k]);
      o2w[k * 4 + 2] = static_cast<float>(mm.m[2][k]);
      o2w[k * 4 + 3] = static_cast<float>(mm.m[3][k]);
    }
    dm.instanceXforms.insert(dm.instanceXforms.end(), o2w, o2w + 12);
    appendInstanceColor(dm, inst);
    appendInstanceOpacity(dm, inst);
  }
  for (size_t di = 0; di < suppressStatic.size(); ++di) {
    if (suppressStatic[di] && out->meshes[di].instanceXforms.empty()) {
      out->meshes[di].indices.clear();
      out->meshes[di].submeshes.clear();
    }
  }
  // For every prototype that received instances, replace its local AABB with the
  // world extent over all instance placements. This matches the next loader's
  // convention (next_scene_loader.cc), so scene-bounds and camera-fit consume
  // instanced meshes' bounds directly without any per-instance expansion.
  for (size_t di = 0; di < out->meshes.size(); ++di) {
    if (!touched[di]) continue;
    DrawMeshCPU& dm = out->meshes[di];
    const std::array<float, 6>& lb = localBox[sourceDraw[di]];
    bool first = true;
    const size_t ni = dm.instanceCount();
    for (size_t ii = 0; ii < ni; ++ii) {
      const float* o2w = &dm.instanceXforms[ii * 12];
      for (int corner = 0; corner < 8; ++corner) {
        const float o[3] = {(corner & 1) ? lb[3] : lb[0],
                            (corner & 2) ? lb[4] : lb[1],
                            (corner & 4) ? lb[5] : lb[2]};
        float w[3];
        for (int c = 0; c < 3; ++c) {
          w[c] = o2w[c * 4 + 0] * o[0] + o2w[c * 4 + 1] * o[1] +
                 o2w[c * 4 + 2] * o[2] + o2w[c * 4 + 3];
        }
        if (first) {
          for (int c = 0; c < 3; ++c) { dm.aabbMin[c] = w[c]; dm.aabbMax[c] = w[c]; }
          first = false;
        } else {
          for (int c = 0; c < 3; ++c) {
            dm.aabbMin[c] = std::min(dm.aabbMin[c], w[c]);
            dm.aabbMax[c] = std::max(dm.aabbMax[c], w[c]);
          }
        }
      }
    }
  }
}

void BuildDrawScene(const tydra::RenderScene& rs, DrawScene* out,
                    LoadControl* ctrl, const tinyusdz::Stage* stage,
                    const TextureRuntimeOptions& textureOptions) {
  *out = DrawScene{};

  // Mesh world transforms from the node hierarchy.
  std::unordered_map<int, matrix4d> meshXform;
  for (const auto& n : rs.nodes) {
    CollectMeshTransforms(n, &meshXform);
  }

  std::vector<int> drawTexMap;
  BuildDrawTextures(rs, out, &drawTexMap, textureOptions);
  BuildDrawMaterials(rs, out, drawTexMap);
  FinalizeDrawTextures(textureOptions, out);
  BuildDrawLights(rs, out, textureOptions);

  size_t cumulativeVertexBytes = 0;
  // rs.meshes index -> out->meshes index (meshes may be skipped when empty).
  // Used to attach PointInstancer / scenegraph instances to their prototype.
  std::vector<int> rsMeshToDraw(rs.meshes.size(), -1);
  for (size_t m = 0; m < rs.meshes.size(); ++m) {
    const tydra::RenderMesh& mesh = rs.meshes[m];
    DrawMeshCPU dm;
    if (!MakeDrawMesh(mesh, &dm)) {
      out->skipped.push_back("mesh '" + mesh.prim_name + "': empty geometry");
      continue;
    }

    // Cancellation + render budget (stops before adding an over-budget mesh so
    // huge scenes neither freeze the per-frame loop nor thrash VRAM).
    if (ctrl) {
      if (ctrl->cancel.load()) {
        out->truncated = true;
        out->skipped.push_back("build cancelled at mesh " + std::to_string(m) + "/" +
                               std::to_string(rs.meshes.size()));
        break;
      }
      if (OverBudget(*out, cumulativeVertexBytes, dm, *ctrl)) {
        out->truncated = true;
        out->skipped.push_back(
            "render budget reached: stopped at mesh " + std::to_string(m) + "/" +
            std::to_string(rs.meshes.size()) + " (" +
            std::to_string(out->triangleCount / 1000) + "K tris)");
        break;
      }
    }

    matrix4d world = matrix4d::identity();
    auto xit = meshXform.find(static_cast<int>(m));
    if (xit != meshXform.end()) world = xit->second;
    PlaceDrawMesh(&dm, world);

    cumulativeVertexBytes +=
        dm.vertices.size() * sizeof(DrawVertex) +
        dm.jointIdx.size() * sizeof(uint32_t) +
        dm.jointWt.size() * sizeof(float) +
        dm.influenceOffsetCount.size() * sizeof(uint32_t) +
        dm.influenceTexels.size() * sizeof(float) +
        dm.indices.size() * sizeof(uint32_t);
    out->triangleCount += dm.indices.size() / 3;
    rsMeshToDraw[m] = static_cast<int>(out->meshes.size());
    out->meshes.push_back(std::move(dm));
  }
  BuildDrawInstances(rs, rsMeshToDraw, out);

  if (stage) {
    AppendCurveAndPatchMeshes(*stage, out);
  }

  BuildDrawVolumes(rs, out);

  FinalizeSkinningLayout(rs, out);
  ComputeSceneBounds(out);
  UpdatePreviewLight(out);
  BakeRTDisplacement(out);  // displaced geometry for the ray-tracing backends
}

bool BuildDrawSceneStreaming(tydra::RenderSceneConverter& converter,
                             const tydra::RenderSceneConverterEnv& env,
                             tydra::RenderScene* render, DrawScene* out,
                             LoadControl* ctrl,
                             const TextureRuntimeOptions& textureOptions) {
  *out = DrawScene{};

  // Streaming state (lives for the duration of the synchronous conversion).
  std::vector<int> rsMeshToDraw;   // rs mesh index -> out->meshes index (-1 skipped)
  std::vector<bool> placed;        // per out->meshes: world applied yet?
  size_t cumulativeVertexBytes = 0;

  tydra::RenderSceneSink sink;

  // Build each mesh's geometry as it is converted (LOCAL space; placed later).
  sink.on_mesh = [&](const tydra::RenderMesh& mesh, size_t index,
                     const std::string&, void*) -> bool {
    if (index >= rsMeshToDraw.size()) rsMeshToDraw.resize(index + 1, -1);
    DrawMeshCPU dm;
    if (!MakeDrawMesh(mesh, &dm)) {
      out->skipped.push_back("mesh '" + mesh.prim_name + "': empty geometry");
      return true;
    }
    // Render budget (draw-side cap): stop adding draw meshes but let the
    // conversion finish so out->render stays complete for the GUI.
    if (ctrl && !out->truncated && OverBudget(*out, cumulativeVertexBytes, dm, *ctrl)) {
      out->truncated = true;
      out->skipped.push_back("render budget reached at mesh " +
                             std::to_string(index) + " (" +
                             std::to_string(out->triangleCount / 1000) + "K tris)");
    }
    if (out->truncated) return true;  // skip building further draw meshes

    cumulativeVertexBytes +=
        dm.vertices.size() * sizeof(DrawVertex) +
        dm.jointIdx.size() * sizeof(uint32_t) +
        dm.jointWt.size() * sizeof(float) +
        dm.influenceOffsetCount.size() * sizeof(uint32_t) +
        dm.influenceTexels.size() * sizeof(float) +
        dm.indices.size() * sizeof(uint32_t);
    out->triangleCount += dm.indices.size() / 3;
    rsMeshToDraw[index] = static_cast<int>(out->meshes.size());
    out->meshes.push_back(std::move(dm));
    placed.push_back(false);
    return true;
  };

  // Place meshes once the node hierarchy (world matrices) is known.
  sink.on_root_node = [&](const tydra::Node& root, size_t, void*) -> bool {
    std::unordered_map<int, matrix4d> meshXform;
    CollectMeshTransforms(root, &meshXform);
    for (const auto& kv : meshXform) {
      const int rsIdx = kv.first;
      if (rsIdx < 0 || static_cast<size_t>(rsIdx) >= rsMeshToDraw.size()) continue;
      const int drawIdx = rsMeshToDraw[static_cast<size_t>(rsIdx)];
      if (drawIdx < 0 || static_cast<size_t>(drawIdx) >= out->meshes.size()) continue;
      PlaceDrawMesh(&out->meshes[static_cast<size_t>(drawIdx)], kv.second);
      placed[static_cast<size_t>(drawIdx)] = true;
    }
    return true;
  };

  // Build textures + materials from the finished scene, then finalize bounds.
  sink.on_complete = [&](const tydra::RenderScene& scene, void*) -> bool {
    std::vector<int> drawTexMap;
    BuildDrawTextures(scene, out, &drawTexMap, textureOptions);
    BuildDrawMaterials(scene, out, drawTexMap);
    FinalizeDrawTextures(textureOptions, out);
    BuildDrawLights(scene, out, textureOptions);
    // Any mesh not referenced by a node keeps identity placement (matches
    // BuildDrawScene, which uses identity when no transform is found).
    const matrix4d ident = matrix4d::identity();
    for (size_t i = 0; i < out->meshes.size(); ++i) {
      if (!placed[i]) PlaceDrawMesh(&out->meshes[i], ident);
    }
    AppendCurveAndPatchMeshes(env.stage, out);
    BuildDrawVolumes(scene, out);
    FinalizeSkinningLayout(scene, out);
    BuildDrawInstances(scene, rsMeshToDraw, out);
    ComputeSceneBounds(out);
    UpdatePreviewLight(out);
    BakeRTDisplacement(out);  // displaced geometry for the ray-tracing backends
    return true;
  };

  return converter.ConvertToRenderSceneStreaming(env, sink, render);
}

}  // namespace tusdview
