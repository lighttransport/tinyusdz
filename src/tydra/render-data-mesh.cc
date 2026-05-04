// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// TODO:
//   - [ ] Subdivision surface to polygon mesh conversion.
//     - [ ] Correctly handle primvar with 'vertex' interpolation(Use the basis
//     function of subd surface)
//   - [ ] Support Inbetween BlendShape
//   - [ ] Support material binding collection(Collection API)
//   - [ ] Support multiple skel animation
//   https://github.com/PixarAnimationStudios/OpenUSD/issues/2246
//   - [ ] Adjust normal vector computation with handness?
//   - [ ] Node xform animation
//   - [ ] Better build of index buffer
//     - [ ] Preserve the order of 'points' variable(mesh.points, Skin
//     indices/weights, BlendShape points, ...) as much as possible.
//     - Implement spatial hash
//
//
// Mesh conversion routines split from render-data.cc
//
#include <numeric>
#include <set>

#include "common-utils.hh"
#include "common-types.hh"
#include "image-loader.hh"
#include "image-util.hh"
#include "image-types.hh"
#include "linear-algebra.hh"
#include "math-util.inc"
#include "pprint-enum.hh"
#include "core/prim.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "tinyusdz.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "usdLux.hh"
#include "usdMtlx.hh"
#include "value-pprint.hh"
#include "logger.hh"
#include "bone-util.hh"
#include "shape-to-mesh.hh"
#include "materialx-to-json.hh"
#include "mmap-array-ref.hh"
#include "safe-arithmetic.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// For tangent/binormal computation
// NOTE: HalfEdge is not used atm.
#include "external/half-edge.hh"

#if defined(TINYUSDZ_WITH_MESHOPT)
#include "external/meshoptimizer/meshoptimizer.h"
#endif

// MikkTSpace tangent computation
#include "mikktspace-tangent.hh"
// Optimized MikkTSpace reimplementation
#include "fast-mikktspace.hh"

// For triangulation.
// TODO: Use tinyobjloader's triangulation
#include "external/mapbox/earcut/earcut.hpp"

// For kNN point search
// #include "external/nanoflann.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

//
#include "common-macros.inc"
#include "math-util.inc"


//
#include "tydra/attribute-eval.hh"
#include "tydra/render-data.hh"
#include "tydra/render-data-internal.hh"
#include "tydra/tangent-quantize.hh"
#include "tydra/scene-access.hh"
#include "tydra/shader-network.hh"

namespace tinyusdz {

namespace tydra {

namespace {

#define PushError(msg) TYDRA_PUSH_ERROR(err, msg)


//
// Convert vertex attribute with Uniform variability(interpolation) to
// facevarying variability, by replicating uniform value per face over face
// vertices.
//

//
// Convert vertex attribute with Uniform variability(interpolation) to vertex
// variability, by replicating uniform value for vertices of a face. For shared
// vertex, the value will be overwritten.
//

nonstd::expected<std::vector<uint8_t>, std::string> UniformToVertex(
    const std::vector<uint8_t> &inputs, const size_t stride_bytes,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices) {
  // NOTE: Uniform -> Vertex convertsion may give wrong result.
  std::vector<uint8_t> dst;

  if (stride_bytes == 0) {
    return nonstd::make_unexpected(fmt::format("stride_bytes is zero."));
  }

  if (faceVertexIndices.size() < 3) {
    return nonstd::make_unexpected(
        fmt::format("faceVertexIndices.size must be 3 or greater, but got {}.",
                    faceVertexCounts.size()));
  }

  if ((inputs.size() % stride_bytes) != 0) {
    return nonstd::make_unexpected(
        fmt::format("input bytes {} must be dividable by stride_bytes {}.",
                    inputs.size(), stride_bytes));
  }

  size_t num_uniforms = inputs.size() / stride_bytes;

  if (num_uniforms != faceVertexCounts.size()) {
    return nonstd::make_unexpected(fmt::format(
        "The number of input uniform attributes {} must be the same with "
        "faceVertexCounts.size() {}",
        num_uniforms, faceVertexCounts.size()));
  }

  const uint32_t num_vertices =
      *std::max_element(faceVertexIndices.cbegin(), faceVertexIndices.cend()) + 1;

  size_t resize_size;
  if (!safe::mul(num_vertices, stride_bytes, &resize_size)) {
    return nonstd::make_unexpected("Integer overflow: num_vertices * stride_bytes");
  }
  dst.resize(resize_size);

  size_t fvIndexOffset{0};

  for (size_t i = 0; i < faceVertexCounts.size(); i++) {
    size_t cnt = faceVertexCounts[i];

    if ((fvIndexOffset + cnt) > faceVertexIndices.size()) {
      return nonstd::make_unexpected(
          fmt::format("faceVertexCounts[{}] {} gives buffer-overrun to "
                      "faceVertexIndices.size {}.",
                      i, cnt, faceVertexIndices.size()));
    }

    for (size_t k = 0; k < cnt; k++) {
      uint32_t v_idx = faceVertexIndices[fvIndexOffset + k];

      // may overwrite the value when a vertex is referenced from multiple facet.
      memcpy(dst.data() + v_idx * stride_bytes,
             inputs.data() + i * stride_bytes, stride_bytes);
    }

    fvIndexOffset += cnt;
  }

  return dst;
}

// Generic uniform to facevarying conversion
nonstd::expected<std::vector<uint8_t>, std::string> UniformToFaceVarying(
    const std::vector<uint8_t> &src, const size_t stride_bytes,
    const std::vector<uint32_t> &faceVertexCounts) {
  std::vector<uint8_t> dst;

  if (stride_bytes == 0) {
    return nonstd::make_unexpected("stride_bytes is zero.");
  }

  if ((src.size() % stride_bytes) != 0) {
    return nonstd::make_unexpected(
        fmt::format("input bytes {} must be the multiple of stride_bytes {}",
                    src.size(), stride_bytes));
  }

  size_t num_uniforms = src.size() / stride_bytes;

  if (num_uniforms != faceVertexCounts.size()) {
    return nonstd::make_unexpected(fmt::format(
        "The number of input uniform attributes {} must be the same with "
        "faceVertexCounts.size() {}",
        num_uniforms, faceVertexCounts.size()));
  }

  std::vector<uint8_t> buf;
  buf.resize(stride_bytes);

  for (size_t i = 0; i < faceVertexCounts.size(); i++) {
    size_t cnt = faceVertexCounts[i];

    memcpy(buf.data(), src.data() + i * stride_bytes, stride_bytes);

    // repeat cnt times.
    for (size_t k = 0; k < cnt; k++) {
      dst.insert(dst.end(), buf.begin(), buf.end());
    }
  }

  return dst;
}

//
// Convert vertex attribute with Vertex variability(interpolation) to
// facevarying attribute, by expanding(flatten) the value per vertex per face.
//

// Generic vertex to facevarying conversion
nonstd::expected<std::vector<uint8_t>, std::string> VertexToFaceVarying(
    const std::vector<uint8_t> &src, const size_t stride_bytes,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices) {

  if (src.empty()) {
    return nonstd::make_unexpected("src data is empty.");
  }

  if (stride_bytes == 0) {
    return nonstd::make_unexpected("stride_bytes must be non-zero.");
  }

  if ((src.size() % stride_bytes) != 0) {
    return nonstd::make_unexpected(
        fmt::format("src size {} must be the multiple of stride_bytes {}",
                    src.size(), stride_bytes));
  }

  const size_t num_vertices = src.size() / stride_bytes;

  // Pre-allocate output buffer to exact size needed
  const size_t total_face_vertices = faceVertexIndices.size();
  std::vector<uint8_t> dst;
  size_t dst_size;
  if (!safe::mul(total_face_vertices, stride_bytes, &dst_size)) {
    return nonstd::make_unexpected("Integer overflow: total_face_vertices * stride_bytes");
  }
  dst.resize(dst_size);

  const uint8_t* src_data = src.data();
  uint8_t* dst_ptr = dst.data();

  size_t faceVertexIndexOffset{0};

  for (size_t i = 0; i < faceVertexCounts.size(); i++) {
    size_t cnt = faceVertexCounts[i];

    for (size_t k = 0; k < cnt; k++) {
      size_t fv_idx = k + faceVertexIndexOffset;

      if (fv_idx >= faceVertexIndices.size()) {
        return nonstd::make_unexpected(
            fmt::format("faeVertexIndex {} out-of-range at faceVertexCount[{}]",
                        fv_idx, i));
      }

      size_t v_idx = faceVertexIndices[fv_idx];

      if (v_idx >= num_vertices) {
        return nonstd::make_unexpected(fmt::format(
            "faeVertexIndices[{}] {} exceeds the number of vertices {}", fv_idx,
            v_idx, num_vertices));
      }

      // Direct memcpy to pre-allocated destination
      std::memcpy(dst_ptr, src_data + v_idx * stride_bytes, stride_bytes);
      dst_ptr += stride_bytes;
    }

    faceVertexIndexOffset += cnt;
  }

  return dst;
}


static nonstd::expected<std::vector<uint8_t>, std::string> ConstantToVertex(
    const std::vector<uint8_t> &src, const size_t stride_bytes,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices) {
  if (faceVertexCounts.empty()) {
    return nonstd::make_unexpected("faceVertexCounts is empty.");
  }

  if (faceVertexIndices.size() < 3) {
    return nonstd::make_unexpected(
        fmt::format("faceVertexIndices.size must be at least 3, but got {}.",
                    faceVertexIndices.size()));
  }

  const uint32_t num_vertices =
      *std::max_element(faceVertexIndices.cbegin(), faceVertexIndices.cend()) + 1;

  std::vector<uint8_t> dst;

  if (src.empty()) {
    return nonstd::make_unexpected("src data is empty.");
  }

  if (stride_bytes == 0) {
    return nonstd::make_unexpected("stride_bytes must be non-zero.");
  }

  if (src.size() != stride_bytes) {
    return nonstd::make_unexpected(
        fmt::format("src size {} must be equal to stride_bytes {}", src.size(),
                    stride_bytes));
  }

  size_t dst_size;
  if (!safe::mul(stride_bytes, num_vertices, &dst_size)) {
    return nonstd::make_unexpected("Integer overflow: stride_bytes * num_vertices");
  }
  dst.resize(dst_size);

  size_t faceVertexIndexOffset = 0;
  for (size_t i = 0; i < faceVertexCounts.size(); i++) {
    uint32_t cnt = faceVertexCounts[i];
    if (cnt < 3) {
      return nonstd::make_unexpected(fmt::format(
          "faeVertexCounts[{}] must be equal to or greater than 3, but got {}",
          i, cnt));
    }

    for (size_t k = 0; k < cnt; k++) {
      size_t fv_idx = k + faceVertexIndexOffset;

      if (fv_idx >= faceVertexIndices.size()) {
        return nonstd::make_unexpected(
            fmt::format("faeVertexIndex {} out-of-range at faceVertexCount[{}]",
                        fv_idx, i));
      }

      size_t v_idx = faceVertexIndices[fv_idx];

      if (v_idx >= num_vertices) {  // this should not happen. just in case.
        return nonstd::make_unexpected(fmt::format(
            "faeVertexIndices[{}] {} exceeds the number of vertices {}", fv_idx,
            v_idx, num_vertices));
      }

      memcpy(dst.data() + v_idx * stride_bytes, src.data(), stride_bytes);
    }

    faceVertexIndexOffset += cnt;
  }

  return dst;
}


// T = int
template <typename T>
bool TryConvertFacevaryingToVertexInt(
    const std::vector<T> &src, std::vector<T> *dst,
    const std::vector<uint32_t> &faceVertexIndices) {
  if (!dst) {
    return false;
  }

  if (src.size() != faceVertexIndices.size()) {
    return false;
  }

  // size must be at least 1 triangle(3 verts).
  if (faceVertexIndices.size() < 3) {
    return false;
  }

  // vidx, value
  std::unordered_map<uint32_t, T> vdata;
  vdata.reserve(faceVertexIndices.size());

  uint32_t max_vidx = 0;
  for (size_t i = 0; i < faceVertexIndices.size(); i++) {
    uint32_t vidx = faceVertexIndices[i];
    max_vidx = (std::max)(vidx, max_vidx);

    auto it = vdata.find(vidx);
    if (it != vdata.end()) {
      if (!math::is_close(it->second, src[i])) {
        return false;
      }
    } else {
      vdata.emplace(vidx, src[i]);
    }
  }

  dst->resize(max_vidx + 1);
  memset(dst->data(), 0, (max_vidx + 1) * sizeof(T));

  for (const auto &v : vdata) {
    (*dst)[v.first] = v.second;
  }

  return true;
}

// T = float, double, float2, ...
template <typename T, typename EpsTy>
bool TryConvertFacevaryingToVertexFloat(
    const std::vector<T> &src, std::vector<T> *dst,
    const std::vector<uint32_t> &faceVertexIndices, const EpsTy eps) {
  DCOUT("TryConvertFacevaryingToVertexFloat");
  if (!dst) {
    return false;
  }

  if (src.size() != faceVertexIndices.size()) {
    DCOUT("size mismatch.");
    return false;
  }

  // size must be at least 1 triangle(3 verts).
  if (faceVertexIndices.size() < 3) {
    return false;
  }

  // vidx, value
  std::unordered_map<uint32_t, T> vdata;
  vdata.reserve(faceVertexIndices.size());

  uint32_t max_vidx = 0;
  for (size_t i = 0; i < faceVertexIndices.size(); i++) {
    uint32_t vidx = faceVertexIndices[i];
    max_vidx = (std::max)(vidx, max_vidx);

    auto it = vdata.find(vidx);
    if (it != vdata.end()) {
      if (!math::is_close(it->second, src[i], eps)) {
        DCOUT("diff at faceVertexIndices[" << i << "]");
        return false;
      }
    } else {
      vdata.emplace(vidx, src[i]);
    }
  }

  dst->resize(max_vidx + 1);
  memset(dst->data(), 0, (max_vidx + 1) * sizeof(T));

  for (const auto &v : vdata) {
    (*dst)[v.first] = v.second;
  }

  return true;
}

// T = matrix type.
template <typename T>
bool TryConvertFacevaryingToVertexMat(
    const std::vector<T> &src, std::vector<T> *dst,
    const std::vector<uint32_t> &faceVertexIndices) {
  if (!dst) {
    return false;
  }

  if (src.size() != faceVertexIndices.size()) {
    return false;
  }

  // size must be at least 1 triangle(3 verts).
  if (faceVertexIndices.size() < 3) {
    return false;
  }

  // vidx, value
  std::unordered_map<uint32_t, T> vdata;
  vdata.reserve(faceVertexIndices.size());

  uint32_t max_vidx = 0;
  for (size_t i = 0; i < faceVertexIndices.size(); i++) {
    uint32_t vidx = faceVertexIndices[i];
    max_vidx = (std::max)(vidx, max_vidx);

    auto it = vdata.find(vidx);
    if (it != vdata.end()) {
      if (!is_close(it->second, src[i])) {
        return false;
      }
    } else {
      vdata.emplace(vidx, src[i]);
    }
  }

  dst->assign(max_vidx + 1, T::identity());

  for (const auto &v : vdata) {
    (*dst)[v.first] = v.second;
  }

  return true;
}

///
/// Try to convert 'facevarying' vertex attribute to 'vertex' attribute.
/// Inspect each vertex value is the same(with given eps)
///
/// Current limitation:
/// - stride must be 0 or tightly packed.
/// - elementSize must be 1
///
/// @return true when 'facevarying' vertex attribute successfully converted to
/// 'vertex'
///
static bool TryConvertFacevaryingToVertex(
    const VertexAttribute &src, VertexAttribute *dst,
    const std::vector<uint32_t> &faceVertexIndices, std::string *err,
    const float eps) {
  DCOUT("TryConvertFacevaryingToVertex");
  if (!dst) {
    PUSH_ERROR_AND_RETURN("Output `dst` is nullptr.");
  }

  if (!src.is_facevarying()) {
    PUSH_ERROR_AND_RETURN("Input must be 'facevarying' attribute");
  }

  if (src.element_size() != 1) {
    PUSH_ERROR_AND_RETURN("Input's element_size must be 1.");
  }

  if ((src.stride != 0) && (src.stride_bytes() != src.format_size())) {
    PUSH_ERROR_AND_RETURN(
        "Input attribute must be tightly packed. stride_bytes = "
        << src.stride_bytes() << ", format_size = " << src.format_size());
  }

#define CONVERT_FUN_INT(__fmt, __ty)                                      \
  if (src.format == __fmt) {                                              \
    std::vector<__ty> vsrc;                                               \
    vsrc.resize(src.vertex_count());                                      \
    memcpy(vsrc.data(), src.get_data().data(), src.get_data().size());    \
    std::vector<__ty> vdst;                                               \
    bool ret = TryConvertFacevaryingToVertexInt<__ty>(vsrc, &vdst,        \
                                                      faceVertexIndices); \
    if (!ret) {                                                           \
      return false;                                                       \
    }                                                                     \
    dst->name = src.name;                                                 \
    dst->elementSize = 1;                                                 \
    dst->format = src.format;                                             \
    dst->variability = VertexVariability::Vertex;                         \
    dst->data.resize(vdst.size() * src.format_size());                    \
    memcpy(dst->data.data(), vdst.data(), dst->data.size());              \
    return true;                                                          \
  } else

#define CONVERT_FUN_FLOAT(__fmt, __ty, __epsty)                        \
  if (src.format == __fmt) {                                           \
    std::vector<__ty> vsrc;                                            \
    vsrc.resize(src.vertex_count());                                   \
    memcpy(vsrc.data(), src.get_data().data(), src.get_data().size()); \
    std::vector<__ty> vdst;                                            \
    bool ret = TryConvertFacevaryingToVertexFloat<__ty, __epsty>(      \
        vsrc, &vdst, faceVertexIndices, __epsty(eps));                 \
    if (!ret) {                                                        \
      return false;                                                    \
    }                                                                  \
    dst->name = src.name;                                                 \
    dst->elementSize = 1;                                              \
    dst->format = src.format;                                          \
    dst->variability = VertexVariability::Vertex;                      \
    dst->data.resize(vdst.size() * src.format_size());                 \
    memcpy(dst->data.data(), vdst.data(), dst->data.size());           \
    return true;                                                       \
  } else

#define CONVERT_FUN_MAT(__fmt, __ty)                                      \
  if (src.format == __fmt) {                                              \
    std::vector<__ty> vsrc;                                               \
    vsrc.resize(src.vertex_count());                                      \
    memcpy(vsrc.data(), src.get_data().data(), src.get_data().size());    \
    std::vector<__ty> vdst;                                               \
    bool ret = TryConvertFacevaryingToVertexMat<__ty>(vsrc, &vdst,        \
                                                      faceVertexIndices); \
    if (!ret) {                                                           \
      return false;                                                       \
    }                                                                     \
    dst->name = src.name;                                                 \
    dst->elementSize = 1;                                                 \
    dst->format = src.format;                                             \
    dst->variability = VertexVariability::Vertex;                         \
    dst->data.resize(vdst.size() * src.format_size());                    \
    memcpy(dst->data.data(), vdst.data(), dst->data.size());              \
    return true;                                                          \
  } else

  // NOTE: VertexAttributeFormat::Bool is preserved
  CONVERT_FUN_INT(VertexAttributeFormat::Bool, uint8_t)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Float, float, float)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Vec2, value::float2, float)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Vec3, value::float3, float)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Vec4, value::float4, float)
  CONVERT_FUN_INT(VertexAttributeFormat::Char, signed char)
  // CONVERT_FUN(VertexAttributeFormat::Char2, value::char2)
  // CONVERT_FUN(VertexAttributeFormat::Char3, value::char3)
  // CONVERT_FUN(VertexAttributeFormat::Char4,    // int8x4
  CONVERT_FUN_INT(VertexAttributeFormat::Byte, uint8_t)
  // CONVERT_FUN(VertexAttributeFormat::Byte2,    // uint8x2
  // CONVERT_FUN(VertexAttributeFormat::Byte3,    // uint8x3
  // CONVERT_FUN(VertexAttributeFormat::Byte4,    // uint8x4
  CONVERT_FUN_INT(VertexAttributeFormat::Short, int16_t)
  // CONVERT_FUN(VertexAttributeFormat::Short2, value::short2)
  // CONVERT_FUN(VertexAttributeFormat::Short3, value::short3)
  // CONVERT_FUN(VertexAttributeFormat::Short4, value::short4)
  CONVERT_FUN_INT(VertexAttributeFormat::Ushort, uint16_t)
  // CONVERT_FUN(VertexAttributeFormat::Ushort2, uint16_t)
  // CONVERT_FUN(VertexAttributeFormat::Ushort3, uint16_t)
  // CONVERT_FUN(VertexAttributeFormat::Ushort4, uint16_t)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Half, value::half, float)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Half2, value::half2, float)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Half3, value::half3, float)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Half4, value::half4, float)
  CONVERT_FUN_INT(VertexAttributeFormat::Int, int)
  CONVERT_FUN_INT(VertexAttributeFormat::Ivec2, value::int2)
  CONVERT_FUN_INT(VertexAttributeFormat::Ivec3, value::int3)
  CONVERT_FUN_INT(VertexAttributeFormat::Ivec4, value::int4)
  CONVERT_FUN_INT(VertexAttributeFormat::Uint, uint32_t)
  CONVERT_FUN_INT(VertexAttributeFormat::Uvec2, value::uint2)
  CONVERT_FUN_INT(VertexAttributeFormat::Uvec3, value::uint3)
  CONVERT_FUN_INT(VertexAttributeFormat::Uvec4, value::uint4)
  // NOTE: Use float precision eps is upcasted to double precision.
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Double, double, double)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Dvec2, value::double2, double)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Dvec3, value::double3, double)
  CONVERT_FUN_FLOAT(VertexAttributeFormat::Dvec4, value::double4, double)
  CONVERT_FUN_MAT(VertexAttributeFormat::Mat2, value::matrix2f)
  CONVERT_FUN_MAT(VertexAttributeFormat::Mat3, value::matrix3f)
  CONVERT_FUN_MAT(VertexAttributeFormat::Mat4, value::matrix4f)
  CONVERT_FUN_MAT(VertexAttributeFormat::Dmat2, value::matrix2d)
  CONVERT_FUN_MAT(VertexAttributeFormat::Dmat3, value::matrix3d)
  CONVERT_FUN_MAT(VertexAttributeFormat::Dmat4, value::matrix4d) {
    if (err) {
      (*err) +=
          fmt::format("Unsupported/Unimplemented VertexAttributeFormat: {}",
                      to_string(src.format));
    }
  }

#undef CONVERT_FUN_INT
#undef CONVERT_FUN_FLOAT
#undef CONVERT_FUN_MAT

  return false;
}




///
/// Triangulate VeretexAttribute data.
///
static bool TriangulateVertexAttribute(
    VertexAttribute &vattr, const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &triangulatedToOrigFaceVertexIndexMap,
    const std::vector<uint32_t> &triangulatedFaceCounts,
    const std::vector<uint32_t> &triangulatedFaceVertexIndices,
    std::string *err) {
  if (vattr.vertex_count() == 0) {
    return true;
  }

  if (triangulatedFaceCounts.empty()) {
    PUSH_ERROR_AND_RETURN("triangulatedFaceCounts is empty.");
  }

  if (faceVertexCounts.size() != triangulatedFaceCounts.size()) {
    PUSH_ERROR_AND_RETURN(
        "faceVertexCounts.size must be equal to triangulatedFaceCounts.size.");
  }

  if ((triangulatedFaceVertexIndices.size() % 3) != 0) {
    PUSH_ERROR_AND_RETURN("Invalid size for triangulatedFaceVertexIndices.");
  }

  if (vattr.is_facevarying()) {
    if (triangulatedToOrigFaceVertexIndexMap.size() !=
        triangulatedFaceVertexIndices.size()) {
      PUSH_ERROR_AND_RETURN(
          "triangulatedToOrigFaceVertexIndexMap.size must be equal to "
          "triangulatedFaceVertexIndices.");
    }

    size_t num_vs = vattr.vertex_count();
    const size_t stride = vattr.stride_bytes();
    const size_t total_size = triangulatedFaceVertexIndices.size() * stride;

    std::vector<uint8_t> buf;
    buf.resize(total_size);  // Pre-allocate exact size

    const uint8_t* src_data = vattr.get_data().data();
    uint8_t* dst_ptr = buf.data();

    for (uint32_t f = 0; f < triangulatedFaceVertexIndices.size(); f++) {
      // Array index to faceVertexIndices(before triangulation).
      uint32_t src_fvIdx = triangulatedToOrigFaceVertexIndexMap[f];

      if (src_fvIdx >= num_vs) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("triangulatedToOrigFaceVertexIndexMap[{}] {} exceeds num_vs {}.", f, src_fvIdx, num_vs));
      }

      // Use memcpy instead of insert for better performance
      std::memcpy(dst_ptr, src_data + src_fvIdx * stride, stride);
      dst_ptr += stride;
    }

    vattr.data = std::move(buf);
  } else if (vattr.is_vertex()) {
    // # of vertices does not change, so nothing is required.
    return true;
  } else if (vattr.is_indexed()) {
    PUSH_ERROR_AND_RETURN("Indexed VertexAttribute is not supported.");
  } else if (vattr.is_constant()) {
    const size_t stride = vattr.stride_bytes();

    // Pre-calculate total size to avoid reallocations
    size_t total_triangles = 0;
    for (size_t f = 0; f < triangulatedFaceCounts.size(); f++) {
      total_triangles += triangulatedFaceCounts[f];
    }
    // Each triangle has 3 vertices
    const size_t total_size = total_triangles * 3 * stride;

    std::vector<uint8_t> buf;
    buf.resize(total_size);

    const uint8_t* src_data = vattr.get_data().data();
    uint8_t* dst_ptr = buf.data();

    for (size_t f = 0; f < triangulatedFaceCounts.size(); f++) {
      uint32_t nf = triangulatedFaceCounts[f];
      const uint8_t* face_data = src_data + f * stride;

      // copy `nf` triangles (each with 3 vertices)
      for (size_t k = 0; k < nf * 3; k++) {
        std::memcpy(dst_ptr, face_data, stride);
        dst_ptr += stride;
      }
    }

    vattr.data = std::move(buf);
  } else if (vattr.is_uniform()) {
    // nothing is required
    return true;
  }

  return true;
}

template <typename T>
static bool TryReadMMapArray(
    const Stage &stage,
    const std::string &prim_path,
    const std::string &attr_name,
    std::vector<T> *out) {
  if (!stage.has_mmap_zero_copy()) return false;
  const MMapArrayRef *ref = stage.mmap_table()->find(prim_path, attr_name);
  if (!ref) return false;
  const T *ptr = stage.mmap_source()->get_ptr<T>(*ref);
  if (!ptr) return false;
  if (ref->element_count >
      (uint64_t((std::numeric_limits<size_t>::max)()) / sizeof(T))) {
    return false;
  }
  out->resize(static_cast<size_t>(ref->element_count));
  const size_t byte_count = static_cast<size_t>(ref->element_count) * sizeof(T);
  memcpy(out->data(), ptr, byte_count);
  return true;
}

/// Try to read an indexed primvar from mmap: read raw data, then expand with indices.
/// Returns false if mmap read fails or primvar has no indices (caller should use other path).
template <typename T>
static bool TryReadMMapArrayWithIndices(
    const Stage &stage,
    const std::string &prim_path,
    const std::string &attr_name,
    const GeomPrimvar &primvar,
    double timecode,
    std::vector<T> *out) {
  if (!stage.has_mmap_zero_copy()) return false;
  if (!primvar.has_indices()) return false;

  // Read raw (un-expanded) data from mmap
  std::vector<T> raw_data;
  if (!TryReadMMapArray<T>(stage, prim_path, attr_name, &raw_data)) {
    return false;
  }

  // Get indices (int arrays are NOT deferred — they use LZ4 compression)
  std::vector<int32_t> indices = primvar.get_indices(timecode);
  if (indices.empty()) {
    // No indices at this timecode — use raw data directly
    *out = std::move(raw_data);
    return true;
  }

  // Expand: out[i] = raw_data[indices[i]]
  out->resize(indices.size());
  for (size_t i = 0; i < indices.size(); i++) {
    int32_t idx = indices[i];
    if (idx >= 0 && size_t(idx) < raw_data.size()) {
      (*out)[i] = raw_data[size_t(idx)];
    }
    // Out-of-bounds indices get zero-initialized (default T{})
  }
  return true;
}

//
// name does not include "primvars:" prefix.
//
nonstd::expected<VertexAttribute, std::string> GetTextureCoordinate(
    const Stage &stage, const GeomMesh &mesh, const std::string &name,
    const double t, const value::TimeSampleInterpolationType tinterp,
    const std::string &prim_path = std::string()) {
  VertexAttribute vattr;

  std::string err;
  GeomPrimvar primvar;
  if (!GetGeomPrimvar(stage, &mesh, name, &primvar, &err)) {
    return nonstd::make_unexpected(err);
  }

  if (!primvar.has_value()) {
    return nonstd::make_unexpected("No value exist for primvars:" + name +
                                   "\n");
  }

  // TODO: allow float2?
  if (primvar.get_type_id() !=
      value::TypeTraits<std::vector<value::texcoord2f>>::type_id()) {
    return nonstd::make_unexpected(
        "Texture coordinate primvar must be texCoord2f[] type, but got " +
        primvar.get_type_name() + "\n");
  }

  std::vector<value::texcoord2f> uvs;

  // mmap zero-copy path: read texcoords directly from mmap.
  // V2: also handles indexed primvars via TryReadMMapArrayWithIndices.
  bool got_from_mmap = false;
  if (!prim_path.empty()) {
    if (primvar.has_indices()) {
      got_from_mmap = TryReadMMapArrayWithIndices<value::texcoord2f>(
          stage, prim_path, "primvars:" + name, primvar, t, &uvs);
    } else {
      got_from_mmap = TryReadMMapArray<value::texcoord2f>(
          stage, prim_path, "primvars:" + name, &uvs);
    }
  }

  if (!got_from_mmap) {
    if (!primvar.flatten_with_indices(t, &uvs, tinterp)) {
      return nonstd::make_unexpected(
          "Failed to retrieve texture coordinate primvar with concrete type.\n");
    }
  }

  if (primvar.get_interpolation() == Interpolation::Varying) {
    vattr.variability = VertexVariability::Varying;
  } else if (primvar.get_interpolation() == Interpolation::Constant) {
    vattr.variability = VertexVariability::Constant;
  } else if (primvar.get_interpolation() == Interpolation::Uniform) {
    vattr.variability = VertexVariability::Uniform;
  } else if (primvar.get_interpolation() == Interpolation::Vertex) {
    vattr.variability = VertexVariability::Vertex;
  } else if (primvar.get_interpolation() == Interpolation::FaceVarying) {
    vattr.variability = VertexVariability::FaceVarying;
  }


  //TUSDZ_LOG_I("texcoord. " << name << ", " << uvs.size());
  DCOUT("texcoord " << name << " : " << uvs);

  vattr.format = VertexAttributeFormat::Vec2;
  size_t resize_size;
  if (!safe::n_to_size<value::texcoord2f>(uvs.size(), &resize_size)) {
    return nonstd::make_unexpected("Integer overflow: uvs.size() * sizeof(value::texcoord2f)");
  }
  vattr.data.resize(resize_size);
  memcpy(vattr.data.data(), uvs.data(), vattr.data.size());
  vattr.indices.clear();  // just in case.

  vattr.name = name;  // TODO: add "primvars:" namespace?
  //TUSDZ_LOG_I("end");

  return std::move(vattr);
}


namespace {

template <typename UnderlyingTy>
bool ScalarValueToVertexAttribute(const value::Value &value,
                                  const std::string &name,
                                  const VertexAttributeFormat format,
                                  VertexAttribute &dst, std::string *err) {
  if (VertexAttributeFormatSize(format) != sizeof(UnderlyingTy)) {
    PUSH_ERROR_AND_RETURN("format size mismatch.");
    return false;
  }

  if (auto pv = value.as<UnderlyingTy>()) {
    dst.data.resize(sizeof(UnderlyingTy));
    memcpy(dst.data.data(), pv, sizeof(UnderlyingTy));

    dst.elementSize = 1;
    dst.stride = 0;
    dst.format = format;
    dst.variability = VertexVariability::Constant;
    dst.name = name;
    dst.indices.clear();
    return true;
  }

  PUSH_ERROR_AND_RETURN("[Internal error] value is not scalar-typed value.");
}

template <typename UnderlyingTy>
bool ArrayValueToVertexAttribute(
    const value::Value &value, const std::string &name,
    const uint32_t elementSize, const VertexVariability variability,
    const uint32_t num_vertices, const uint32_t num_face_counts,
    const uint32_t num_face_vertex_indices, const VertexAttributeFormat format,
    VertexAttribute &dst, std::string *err) {
  if (!value::TypeTraits<UnderlyingTy>::is_array()) {
    PUSH_ERROR_AND_RETURN(
        "[Internal error] UnderlyingTy template parameter must be array type.");
  }

  size_t baseTySize = value::TypeTraits<UnderlyingTy>::size();

  size_t value_counts = value.array_size();
  if (value_counts == 0) {
    PUSH_ERROR_AND_RETURN("Empty array size");
  }

  if (variability == VertexVariability::Indexed) {
    PUSH_ERROR_AND_RETURN("Indexed variability is not supported.");
  }

  if (VertexAttributeFormatSize(format) != baseTySize) {
    PUSH_ERROR_AND_RETURN("format size mismatch. expected "
                          << VertexAttributeFormatSize(format) << " but got "
                          << baseTySize);
    return false;
  }

  DCOUT("value.type = " << value.type_name());
  DCOUT("UnderlyingTy = " << value::TypeTraits<UnderlyingTy>::type_name());
  const auto p = value.as<UnderlyingTy>();
  if (!p) {
    DCOUT("p is nullptr");
  }

  if (auto pv = value.as<UnderlyingTy>()) {
    switch (variability) {
    case VertexVariability::Constant: {
      if (value_counts != elementSize) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "{} # of items {} expected, but got {}. Variability = Constant",
            name, elementSize, value_counts));
      }
      break;
    }
    case VertexVariability::Uniform: {
      if (value_counts != (elementSize * num_face_counts)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "{} # of items {} expected, but got {}. Variability = Uniform",
            name, elementSize * num_face_counts, value_counts));
      }
      break;
    }
    case VertexVariability::Vertex: {
      if (value_counts != (elementSize * num_vertices)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "{} # of items {} expected, but got {}. Variability = Vertex",
            name, elementSize * num_vertices, value_counts));
      }
      break;
    case VertexVariability::Varying: {
      if (value_counts != (elementSize * num_vertices)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "{} # of items {} expected, but got {}. Variability = Varying",
            name, elementSize * num_vertices, value_counts));
      }
      break;
    }
    case VertexVariability::FaceVarying: {
      if (value_counts != (elementSize * num_face_vertex_indices)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "# of items {} expected, but got {}. Variability = FaceVarying",
            elementSize * num_face_vertex_indices, value_counts));
      }
      break;
    }
    case VertexVariability::Indexed: {
      PUSH_ERROR_AND_RETURN(fmt::format(
            "{} Internal error. 'Indexed' variability is not supported."));
      }
      break;
    }
    }

    size_t resize_size;
    if (!safe::mul(value_counts, baseTySize, &resize_size)) {
      PUSH_ERROR_AND_RETURN("Integer overflow: value_counts * baseTySize");
    }
    dst.data.resize(resize_size);
    size_t memcpy_size;
    if (!safe::mul(value_counts, baseTySize, &memcpy_size)) {
      PUSH_ERROR_AND_RETURN("Integer overflow in memcpy: value_counts * baseTySize");
    }
    memcpy(dst.data.data(), pv->data(), memcpy_size);

    dst.elementSize = elementSize;
    dst.stride = 0;
    dst.format = format;
    dst.variability = variability;
    dst.name = name;
    dst.indices.clear();
    return true;
  }

  PUSH_ERROR_AND_RETURN(fmt::format(
      "Requested underlying type {} but input `value` has underlying type {}.",
      value::TypeTraits<UnderlyingTy>::type_name(),
      value.underlying_type_name()));
}

#if defined(TINYUSDZ_WITH_MESHOPT)
//
// Optimize RenderMesh indices using meshoptimizer
//
[[maybe_unused]] static void OptimizeRenderMeshIndices(RenderMesh& mesh) {
  // Only optimize triangulated meshes with valid indices
  if (!mesh.is_triangulated() || mesh.triangulatedFaceVertexIndices.empty() || mesh.points.empty()) {
    return;
  }

  const size_t index_count = mesh.triangulatedFaceVertexIndices.size();
  const size_t vertex_count = mesh.points.size();

  if (index_count == 0 || vertex_count == 0) {
    return;
  }

  // Create optimized index buffer
  std::vector<unsigned int> optimized_indices(index_count);

  // Convert indices to unsigned int for meshoptimizer
  std::vector<unsigned int> indices(index_count);
  for (size_t i = 0; i < index_count; i++) {
    indices[i] = static_cast<unsigned int>(mesh.triangulatedFaceVertexIndices[i]);
  }

  // Step 1: Optimize vertex cache
  meshopt_optimizeVertexCache(optimized_indices.data(), indices.data(),
                              index_count, vertex_count);

  // Step 2: Optimize overdraw (requires vertex positions)
  if (!mesh.points.empty()) {
    std::vector<unsigned int> overdraw_optimized(index_count);
    meshopt_optimizeOverdraw(overdraw_optimized.data(), optimized_indices.data(),
                             index_count,
                             reinterpret_cast<const float*>(mesh.points.data()),
                             vertex_count,
                             sizeof(vec3), // stride
                             1.05f); // threshold (allow up to 5% vertex cache degradation)

    optimized_indices = std::move(overdraw_optimized);
  }

  // Step 3: Optimize vertex fetch
  std::vector<unsigned int> fetch_remap(vertex_count);
  size_t unique_vertices = meshopt_optimizeVertexFetchRemap(fetch_remap.data(),
                                                            optimized_indices.data(),
                                                            index_count,
                                                            vertex_count);

  // Only apply vertex fetch optimization if it reduces vertex count
  if (unique_vertices < vertex_count && unique_vertices > 0) {
    // Remap indices
    meshopt_remapIndexBuffer(optimized_indices.data(), optimized_indices.data(),
                             index_count, fetch_remap.data());

    // Remap vertex positions
    std::vector<vec3> optimized_points(unique_vertices);
    meshopt_remapVertexBuffer(optimized_points.data(), mesh.points.data(),
                              vertex_count, sizeof(vec3), fetch_remap.data());

    mesh.points = std::move(optimized_points);

    // TODO: Remap other vertex attributes (normals, texcoords, etc.) as needed
    // This would require more complex logic to handle all vertex attributes
  }

  // Convert back to uint32_t and update mesh
  for (size_t i = 0; i < index_count; i++) {
    mesh.triangulatedFaceVertexIndices[i] = static_cast<uint32_t>(optimized_indices[i]);
  }
}
#endif

}  // namespace

bool ToVertexAttribute(const GeomPrimvar &primvar, const std::string &name,
                       const uint32_t num_vertices,
                       const uint32_t num_face_counts,
                       const uint32_t num_face_vertex_indices,
                       VertexAttribute &dst, std::string *err, const double t,
                       const value::TimeSampleInterpolationType tinterp,
                       std::string *warn = nullptr) {
  uint32_t elementSize = uint32_t(primvar.get_elementSize());
  if (elementSize == 0) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("elementSize is zero for primvar: {}", primvar.name()));
  }

  VertexAttribute vattr;

  const tinyusdz::Attribute &attr = primvar.get_attribute();

  // Check if primvar has timesamples and report detailed warning
  if (attr.has_timesamples()) {
    std::string msg = fmt::format(
        "Geometry primvar '{}' has timesamples (animated values). "
        "RenderMesh conversion uses value at specified time (timecode={}). "
        "To capture animation, you need to convert at multiple timesamples. "
        "Consider using ConvertMesh() at each timeframe or implementing "
        "per-frame conversion. This is particularly important for vertex "
        "attributes like normals, tangents, texture coordinates, colors, "
        "and opacities that may change over time.",
        name, t);
    if (warn) {
      (*warn) += msg + "\n";
    }
    DCOUT("WARN: " << msg);
  }

  value::Value value;
  if (!primvar.flatten_with_indices(t, &value, tinterp)) {
    PUSH_ERROR_AND_RETURN("Failed to flatten primvar");
  }

  bool is_array = value.type_id() & value::TYPE_ID_1D_ARRAY_BIT;
  DCOUT("is_array " << (is_array ? "true" : "false"));

  VertexVariability variability;
  if (primvar.get_interpolation() == Interpolation::Varying) {
    variability = VertexVariability::Varying;
  } else if (primvar.get_interpolation() == Interpolation::Constant) {
    variability = VertexVariability::Constant;
  } else if (primvar.get_interpolation() == Interpolation::Uniform) {
    variability = VertexVariability::Uniform;
  } else if (primvar.get_interpolation() == Interpolation::Vertex) {
    variability = VertexVariability::Vertex;
  } else if (primvar.get_interpolation() == Interpolation::FaceVarying) {
    variability = VertexVariability::FaceVarying;
  } else {
    PUSH_ERROR_AND_RETURN("[Internal Error] Invalid `interpolation` type.");
  }

  uint32_t baseUnderlyingTypeId =
      value.underlying_type_id() & (~value::TYPE_ID_1D_ARRAY_BIT);
  DCOUT("flattened primvar type: " << value.type_name() << ", underlying type "
                                   << value::GetTypeName(baseUnderlyingTypeId));

  // Cast to underlying type

#define TO_TYPED_VALUE(__underlying_ty, __vfmt)                                \
  if (baseUnderlyingTypeId == value::TypeTraits<__underlying_ty>::type_id()) { \
    if (is_array) {                                                            \
      return ArrayValueToVertexAttribute<std::vector<__underlying_ty>>(        \
          value, name, elementSize, variability, num_vertices,                 \
          num_face_counts, num_face_vertex_indices, __vfmt, dst, err);         \
    } else {                                                                   \
      return ScalarValueToVertexAttribute<__underlying_ty>(value, name,        \
                                                           __vfmt, dst, err);  \
    }                                                                          \
  } else

  // specialization for bool type: bool is represented as uint8 in USD primvar
  if (baseUnderlyingTypeId == value::TypeTraits<bool>::type_id()) {
    if (is_array) {
      return ArrayValueToVertexAttribute<std::vector<uint8_t>>(
          value, name, elementSize, variability, num_vertices, num_face_counts,
          num_face_vertex_indices, VertexAttributeFormat::Bool, dst, err);
    } else {
      return ScalarValueToVertexAttribute<uint8_t>(
          value, name, VertexAttributeFormat::Bool, dst, err);
    }
  } else
    TO_TYPED_VALUE(uint8_t, VertexAttributeFormat::Byte)
  TO_TYPED_VALUE(value::uchar2, VertexAttributeFormat::Byte2)
  TO_TYPED_VALUE(value::uchar3, VertexAttributeFormat::Byte3)
  TO_TYPED_VALUE(value::uchar4, VertexAttributeFormat::Byte4)
  TO_TYPED_VALUE(char, VertexAttributeFormat::Char)
  TO_TYPED_VALUE(value::char2, VertexAttributeFormat::Char2)
  TO_TYPED_VALUE(value::char3, VertexAttributeFormat::Char3)
  TO_TYPED_VALUE(value::char4, VertexAttributeFormat::Char4)
  TO_TYPED_VALUE(short, VertexAttributeFormat::Short)
  TO_TYPED_VALUE(value::short2, VertexAttributeFormat::Short2)
  TO_TYPED_VALUE(value::short3, VertexAttributeFormat::Short3)
  TO_TYPED_VALUE(value::short4, VertexAttributeFormat::Short4)
  TO_TYPED_VALUE(uint16_t, VertexAttributeFormat::Ushort)
  TO_TYPED_VALUE(value::ushort2, VertexAttributeFormat::Ushort2)
  TO_TYPED_VALUE(value::ushort3, VertexAttributeFormat::Ushort3)
  TO_TYPED_VALUE(value::ushort4, VertexAttributeFormat::Ushort4)
  TO_TYPED_VALUE(int, VertexAttributeFormat::Int)
  TO_TYPED_VALUE(value::int2, VertexAttributeFormat::Ivec2)
  TO_TYPED_VALUE(value::int3, VertexAttributeFormat::Ivec3)
  TO_TYPED_VALUE(value::int4, VertexAttributeFormat::Ivec4)
  TO_TYPED_VALUE(uint32_t, VertexAttributeFormat::Uint)
  TO_TYPED_VALUE(value::uint2, VertexAttributeFormat::Uvec2)
  TO_TYPED_VALUE(value::uint3, VertexAttributeFormat::Uvec3)
  TO_TYPED_VALUE(value::uint4, VertexAttributeFormat::Uvec4)
  TO_TYPED_VALUE(float, VertexAttributeFormat::Float)
  TO_TYPED_VALUE(value::float2, VertexAttributeFormat::Vec2)
  TO_TYPED_VALUE(value::float3, VertexAttributeFormat::Vec3)
  TO_TYPED_VALUE(value::float4, VertexAttributeFormat::Vec4)
  TO_TYPED_VALUE(value::half, VertexAttributeFormat::Half)
  TO_TYPED_VALUE(value::half2, VertexAttributeFormat::Half2)
  TO_TYPED_VALUE(value::half3, VertexAttributeFormat::Half3)
  TO_TYPED_VALUE(value::half4, VertexAttributeFormat::Half4)
  TO_TYPED_VALUE(double, VertexAttributeFormat::Double)
  TO_TYPED_VALUE(value::double2, VertexAttributeFormat::Dvec2)
  TO_TYPED_VALUE(value::double3, VertexAttributeFormat::Dvec3)
  TO_TYPED_VALUE(value::double4, VertexAttributeFormat::Dvec4)
  TO_TYPED_VALUE(value::matrix2f, VertexAttributeFormat::Mat2)
  TO_TYPED_VALUE(value::matrix3f, VertexAttributeFormat::Mat3)
  TO_TYPED_VALUE(value::matrix4f, VertexAttributeFormat::Mat4)
  TO_TYPED_VALUE(value::matrix2d, VertexAttributeFormat::Dmat2)
  TO_TYPED_VALUE(value::matrix3d, VertexAttributeFormat::Dmat3)
  TO_TYPED_VALUE(value::matrix4d, VertexAttributeFormat::Dmat4) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Unknown or unsupported data type for Geom PrimVar: {}",
                    attr.type_name()));
  }

#undef TO_TYPED_VALUE
}


#if 1
///
/// Input: points, faceVertexCounts, faceVertexIndices
/// Output: triangulated faceVertexCounts(all filled with 3), triangulated
/// faceVertexIndices, triangulatedToOrigFaceVertexIndexMap (length =
/// triangulated faceVertexIndices. triangulatedToOrigFaceVertexIndexMap[i]
/// stores an array index to original faceVertexIndices. For remapping
/// facevarying primvar attributes.)
///
/// triangulatedFaceVertexCounts: len = len(faceVertexCounts). Records the
/// number of triangle faces. 1 = triangle. 2 = quad, ... For remapping face
/// indices(e.g. GeomSubset::indices)
///
/// triangulated*** output is generated even when input mesh is fully composed
/// from triangles(`faceVertexCounts` are all filled with 3) Return false when a
/// polygon is degenerated. No overlap check at the moment
///
/// Example:
///   - faceVertexCounts = [4]
///   - faceVertexIndices = [0, 1, 3, 2]
///
///   - triangulatedFaceVertexCounts = [3, 3]
///   - triangulatedFaceVertexIndices = [0, 1, 3, 0, 3, 2]
///   - triangulatedToOrigFaceVertexIndexMap = [0, 1, 2, 0, 2, 3]
///
/// T = value::float3 or value::double3
/// BaseTy = float or double
template <typename T, typename BaseTy>
bool TriangulatePolygon(
    const std::vector<T> &points, const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices,
    std::vector<uint32_t> &triangulatedFaceVertexCounts,
    std::vector<uint32_t> &triangulatedFaceVertexIndices,
    std::vector<uint32_t> &triangulatedToOrigFaceVertexIndexMap,
    std::vector<uint32_t> &triangulatedFaceCounts,
    MeshConverterConfig::TriangulationMethod triangulation_method,
    std::string &warn, std::string &err) {
  triangulatedFaceVertexCounts.clear();
  triangulatedFaceVertexIndices.clear();
  triangulatedToOrigFaceVertexIndexMap.clear();
  triangulatedFaceCounts.clear();

  // Pre-allocate based on estimated triangle count.
  // For each face with N vertices, we generate N-2 triangles, each with 3 indices.
  // Total triangles ≈ total_vertex_indices - 2*num_faces
  // This is an upper bound estimate.
  size_t numFaces = faceVertexCounts.size();
  size_t numFaceVertexIndices = faceVertexIndices.size();
  size_t estimatedTriangles = numFaceVertexIndices > 2 * numFaces
                             ? numFaceVertexIndices - 2 * numFaces
                             : numFaces;
  size_t estimatedIndices = estimatedTriangles * 3;

  triangulatedFaceVertexCounts.reserve(estimatedTriangles);
  triangulatedFaceVertexIndices.reserve(estimatedIndices);
  triangulatedToOrigFaceVertexIndexMap.reserve(estimatedIndices);
  triangulatedFaceCounts.reserve(numFaces);

  size_t faceIndexOffset = 0;

  // Reusable temporaries for earcut (avoid per-face heap allocation).
  using Point3D = std::array<BaseTy, 3>;
  using Point2D = std::array<BaseTy, 2>;
  std::vector<Point2D> polyline;
  std::vector<std::vector<Point2D>> polygon_2d(1);  // single ring, no holes

  // For each polygon(face)
  for (size_t i = 0; i < faceVertexCounts.size(); i++) {
    uint32_t npolys = faceVertexCounts[i];

    if (npolys < 3) {
      err = fmt::format(
          "faceVertex count must be 3(triangle) or "
          "more(polygon), but got faceVertexCounts[{}] = {}\n",
          i, npolys);
      return false;
    }

    if (faceIndexOffset + npolys > faceVertexIndices.size()) {
      err = fmt::format(
          "Invalid faceVertexIndices or faceVertexCounts. faceVertex index "
          "exceeds faceVertexIndices.size() at [{}]\n",
          i);
      return false;
    }

    if (npolys == 3) {
      // No need for triangulation.
      triangulatedFaceVertexCounts.push_back(3);
      triangulatedFaceVertexIndices.push_back(
          faceVertexIndices[faceIndexOffset + 0]);
      triangulatedFaceVertexIndices.push_back(
          faceVertexIndices[faceIndexOffset + 1]);
      triangulatedFaceVertexIndices.push_back(
          faceVertexIndices[faceIndexOffset + 2]);
      triangulatedToOrigFaceVertexIndexMap.push_back(uint32_t(faceIndexOffset + 0));
      triangulatedToOrigFaceVertexIndexMap.push_back(uint32_t(faceIndexOffset + 1));
      triangulatedToOrigFaceVertexIndexMap.push_back(uint32_t(faceIndexOffset + 2));
      triangulatedFaceCounts.push_back(1);
    } else if (npolys == 4) {
      // Split quad along the shorter diagonal for better triangle quality.
      // Diagonal 0-2 vs diagonal 1-3: compare squared lengths.
      uint32_t idx0 = faceVertexIndices[faceIndexOffset + 0];
      uint32_t idx1 = faceVertexIndices[faceIndexOffset + 1];
      uint32_t idx2 = faceVertexIndices[faceIndexOffset + 2];
      uint32_t idx3 = faceVertexIndices[faceIndexOffset + 3];

      const T &p0 = points[idx0];
      const T &p1 = points[idx1];
      const T &p2 = points[idx2];
      const T &p3 = points[idx3];

      BaseTy d02_sq = (p0[0]-p2[0])*(p0[0]-p2[0]) + (p0[1]-p2[1])*(p0[1]-p2[1]) + (p0[2]-p2[2])*(p0[2]-p2[2]);
      BaseTy d13_sq = (p1[0]-p3[0])*(p1[0]-p3[0]) + (p1[1]-p3[1])*(p1[1]-p3[1]) + (p1[2]-p3[2])*(p1[2]-p3[2]);

      triangulatedFaceVertexCounts.push_back(3);
      triangulatedFaceVertexCounts.push_back(3);

      if (d13_sq < d02_sq) {
        // Split along diagonal 1-3: triangles (0,1,3) and (1,2,3)
        triangulatedFaceVertexIndices.push_back(idx0);
        triangulatedFaceVertexIndices.push_back(idx1);
        triangulatedFaceVertexIndices.push_back(idx3);

        triangulatedFaceVertexIndices.push_back(idx1);
        triangulatedFaceVertexIndices.push_back(idx2);
        triangulatedFaceVertexIndices.push_back(idx3);

        triangulatedToOrigFaceVertexIndexMap.push_back(uint32_t(faceIndexOffset + 0));
        triangulatedToOrigFaceVertexIndexMap.push_back(uint32_t(faceIndexOffset + 1));
        triangulatedToOrigFaceVertexIndexMap.push_back(uint32_t(faceIndexOffset + 3));
        triangulatedToOrigFaceVertexIndexMap.push_back(uint32_t(faceIndexOffset + 1));
        triangulatedToOrigFaceVertexIndexMap.push_back(uint32_t(faceIndexOffset + 2));
        triangulatedToOrigFaceVertexIndexMap.push_back(uint32_t(faceIndexOffset + 3));
      } else {
        // Split along diagonal 0-2: triangles (0,1,2) and (0,2,3)
        triangulatedFaceVertexIndices.push_back(idx0);
        triangulatedFaceVertexIndices.push_back(idx1);
        triangulatedFaceVertexIndices.push_back(idx2);

        triangulatedFaceVertexIndices.push_back(idx0);
        triangulatedFaceVertexIndices.push_back(idx2);
        triangulatedFaceVertexIndices.push_back(idx3);

        triangulatedToOrigFaceVertexIndexMap.push_back(uint32_t(faceIndexOffset + 0));
        triangulatedToOrigFaceVertexIndexMap.push_back(uint32_t(faceIndexOffset + 1));
        triangulatedToOrigFaceVertexIndexMap.push_back(uint32_t(faceIndexOffset + 2));
        triangulatedToOrigFaceVertexIndexMap.push_back(uint32_t(faceIndexOffset + 0));
        triangulatedToOrigFaceVertexIndexMap.push_back(uint32_t(faceIndexOffset + 2));
        triangulatedToOrigFaceVertexIndexMap.push_back(uint32_t(faceIndexOffset + 3));
      }
      triangulatedFaceCounts.push_back(2);
    } else {
      // Polygon with 5+ vertices
      if (triangulation_method == MeshConverterConfig::TriangulationMethod::TriangleFan) {
        // Simple triangle fan triangulation
        // This assumes the polygon is convex
        // Creates triangles: (0,1,2), (0,2,3), (0,3,4), ...

        size_t ntris = npolys - 2;

        for (size_t k = 0; k < ntris; k++) {
          triangulatedFaceVertexCounts.push_back(3);

          // First vertex is always the pivot (index 0)
          triangulatedFaceVertexIndices.push_back(
              faceVertexIndices[faceIndexOffset + 0]);
          triangulatedFaceVertexIndices.push_back(
              faceVertexIndices[faceIndexOffset + k + 1]);
          triangulatedFaceVertexIndices.push_back(
              faceVertexIndices[faceIndexOffset + k + 2]);

          triangulatedToOrigFaceVertexIndexMap.push_back(uint32_t(faceIndexOffset + 0));
          triangulatedToOrigFaceVertexIndexMap.push_back(uint32_t(faceIndexOffset + k + 1));
          triangulatedToOrigFaceVertexIndexMap.push_back(uint32_t(faceIndexOffset + k + 2));
        }

        triangulatedFaceCounts.push_back(uint32_t(ntris));

      } else {
        // Use earcut algorithm (default, handles complex polygons)
        // Use double for accuracy. `float` precision may classify small-are polygon as degenerated.
        // Find the normal axis of the polygon using Newell's method
        value::double3 n = {0, 0, 0};

        for (size_t k = 0; k < npolys; ++k) {
          size_t vi0 = faceVertexIndices[faceIndexOffset + k];
          size_t vi0_2 = faceVertexIndices[faceIndexOffset + (k + 1) % npolys];

          if (vi0 >= points.size() || vi0_2 >= points.size()) {
            err = fmt::format("Invalid vertex index at face {}.\n", i);
            return false;
          }

          // Newell's method: compute entirely in double to avoid
          // float cancellation in (p1 - p2) for nearly-coplanar vertices.
          const T &p0 = points[vi0];
          const T &p1 = points[vi0_2];
          double d0x = double(p0[0]), d0y = double(p0[1]), d0z = double(p0[2]);
          double d1x = double(p1[0]), d1y = double(p1[1]), d1z = double(p1[2]);

          n[0] += (d0y - d1y) * (d0z + d1z);
          n[1] += (d0z - d1z) * (d0x + d1x);
          n[2] += (d0x - d1x) * (d0y + d1y);
          DCOUT("p0 " << p0);
          DCOUT("p1 " << p1);
          DCOUT("n " << n);
        }
        //BaseTy length_n = vlength(n);
        double length_n = vlength(n);

        // Skip degenerate polygon (zero-area) instead of aborting the
        // entire mesh.  Production meshes often have a few collapsed faces.
        if (std::fabs(length_n) < std::numeric_limits<double>::epsilon()) {
          DCOUT("length_n " << length_n);
          warn += fmt::format("Skipping degenerate polygon at face {}.\n", i);
          triangulatedFaceCounts.push_back(0);
          faceIndexOffset += npolys;
          continue;
        }

        // Negative is to flip the normal to the correct direction
        n = vnormalize(n);

        T axis_w, axis_v, axis_u;
        axis_w[0] = BaseTy(n[0]);
        axis_w[1] = BaseTy(n[1]);
        axis_w[2] = BaseTy(n[2]);
        T a;
        if (std::fabs(axis_w[0]) > BaseTy(0.9999999)) {  // TODO: use 1.0 - eps?
          a = {BaseTy(0), BaseTy(1), BaseTy(0)};
        } else {
          a = {BaseTy(1), BaseTy(0), BaseTy(0)};
        }
        axis_v = vnormalize(vcross(axis_w, a));
        axis_u = vcross(axis_w, axis_v);

        // Project polygon vertices to 2D via the computed normal frame.
        // Reuse polyline/polygon_2d across faces to avoid per-face allocation.
        polyline.clear();
        for (size_t k = 0; k < npolys; k++) {
          size_t vidx = faceVertexIndices[faceIndexOffset + k];
          const T &v = points[vidx];

          // world to local
          Point3D loc = {vdot(v, axis_u), vdot(v, axis_v), vdot(v, axis_w)};
          polyline.push_back({loc[0], loc[1]});
        }

        polygon_2d[0] = polyline;  // single ring, no holes

        std::vector<uint32_t> indices = mapbox::earcut<uint32_t>(polygon_2d);
        //  => result = 3 * faces, clockwise

        if (indices.empty() || (indices.size() % 3) != 0) {
          // Earcut failed — skip this face gracefully.
          warn += fmt::format(
              "Failed to triangulate polygon at face {} "
              "(not CCW, has holes, or invalid topology).\n", i);
          triangulatedFaceCounts.push_back(0);
          faceIndexOffset += npolys;
          continue;
        }

        size_t ntris = indices.size() / 3;

        // Up to 2GB tris.
        if (ntris > size_t((std::numeric_limits<int32_t>::max)())) {
          err = "Too many triangles are generated.\n";
          return false;
        }

        for (size_t k = 0; k < ntris; k++) {
          triangulatedFaceVertexCounts.push_back(3);
          // earcut returns clockwise triangles, but USD expects CCW
          // so we reverse the winding order by swapping indices 1 and 2
          triangulatedFaceVertexIndices.push_back(
              faceVertexIndices[faceIndexOffset + indices[3 * k + 0]]);
          triangulatedFaceVertexIndices.push_back(
              faceVertexIndices[faceIndexOffset + indices[3 * k + 2]]);
          triangulatedFaceVertexIndices.push_back(
              faceVertexIndices[faceIndexOffset + indices[3 * k + 1]]);

          triangulatedToOrigFaceVertexIndexMap.push_back(
              uint32_t(faceIndexOffset + indices[3 * k + 0]));
          triangulatedToOrigFaceVertexIndexMap.push_back(
              uint32_t(faceIndexOffset + indices[3 * k + 2]));
          triangulatedToOrigFaceVertexIndexMap.push_back(
              uint32_t(faceIndexOffset + indices[3 * k + 1]));
        }
        triangulatedFaceCounts.push_back(uint32_t(ntris));
      }
    }

    faceIndexOffset += npolys;
  }

  return true;
}
#endif

static bool ComputeTangentsAndBinormals(
    const std::vector<vec3> &vertices,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices,
    const std::vector<vec2> &texcoords, const std::vector<vec3> &normals,
    bool is_facevarying_input,  // false: 'vertex' varying
    std::vector<vec3> *tangents, std::vector<vec3> *binormals,
    std::vector<uint32_t> *out_vertex_indices, std::string *err,
    uint32_t max_vertex_valence = 16, float dedup_eps = 0.0f) {
  if (!tangents) {
    PUSH_ERROR_AND_RETURN("tangents arg is nullptr.");
  }

  if (!binormals) {
    PUSH_ERROR_AND_RETURN("binormals arg is nullptr.");
  }

  if (!out_vertex_indices) {
    PUSH_ERROR_AND_RETURN("out_indices arg is nullptr.");
  }

  if (vertices.empty()) {
    PUSH_ERROR_AND_RETURN("vertices is empty.");
  }

  // At least 1 triangle face should exist.
  if (faceVertexIndices.size() < 3) {
    PUSH_ERROR_AND_RETURN("faceVertexIndices.size < 3");
  }

  if (texcoords.empty()) {
    PUSH_ERROR_AND_RETURN("texcoords is empty");
  }

  if (normals.empty()) {
    PUSH_ERROR_AND_RETURN("normals is empty");
  }

  if (is_facevarying_input) {
    if (vertices.size() != faceVertexIndices.size()) {
      PUSH_ERROR_AND_RETURN("vertices.size (" << vertices.size() << ") != faceVertexIndices.size (" << faceVertexIndices.size() << ")");
    }
    if (texcoords.size() != faceVertexIndices.size()) {
      PUSH_ERROR_AND_RETURN("texcoords.size (" << texcoords.size() << ") != faceVertexIndices.size (" << faceVertexIndices.size() << ")");
    }
    if (normals.size() != faceVertexIndices.size()) {
      PUSH_ERROR_AND_RETURN("normals.size (" << normals.size() << ") != faceVertexIndices.size (" << faceVertexIndices.size() << ")");
    }
  } else {
    uint32_t max_vert_index =
        *std::max_element(faceVertexIndices.begin(), faceVertexIndices.end());
    if (max_vert_index >= vertices.size()) {
      PUSH_ERROR_AND_RETURN("max vertex index (" << max_vert_index << ") >= vertices.size (" << vertices.size() << ")");
    }
    if (max_vert_index >= texcoords.size()) {
      PUSH_ERROR_AND_RETURN("max vertex index (" << max_vert_index << ") >= texcoords.size (" << texcoords.size() << ")");
    }
    if (max_vert_index >= normals.size()) {
      PUSH_ERROR_AND_RETURN("max vertex index (" << max_vert_index << ") >= normals.size (" << normals.size() << ")");
    }
  }

  bool hasFaceVertexCounts = true;
  if (faceVertexCounts.size() == 0) {
    // Assume all triangle faces.
    if ((faceVertexIndices.size() % 3) != 0) {
      PUSH_ERROR_AND_RETURN(
          "Invalid faceVertexIndices. It must be all triangles: "
          "faceVertexIndices.size % 3 == 0");
    }
    hasFaceVertexCounts = false;
  }

  // Helper: check if a float is finite (not NaN, not Inf)
  auto is_finite_f = [](float x) -> bool {
    return std::isfinite(x);
  };

  // Helper: check if a vec3/normal3f has all-finite components
  auto is_finite_v3 = [&is_finite_f](const value::normal3f &v) -> bool {
    return is_finite_f(v[0]) && is_finite_f(v[1]) && is_finite_f(v[2]);
  };

  // Helper: safe length with NaN protection (returns 0 for NaN/Inf input)
  auto safe_vlength = [&is_finite_v3](const value::normal3f &v) -> float {
    if (!is_finite_v3(v)) return 0.0f;
    return vlength(v);
  };

  // Helper: safe normalize - returns zero vector if input is degenerate/NaN/Inf
  // kTangentLengthEps is a constexpr local referenced inside the lambdas
  // below; MSVC requires it to be in the capture list, so use [&] rather
  // than naming individual captures.
  constexpr float kTangentLengthEps = 1.0e-7f;
  auto safe_vnormalize = [&](const value::normal3f &v) -> value::normal3f {
    float len = safe_vlength(v);
    if (len < kTangentLengthEps) {
      return {0.0f, 0.0f, 0.0f};
    }
    float inv = 1.0f / len;
    return {v[0] * inv, v[1] * inv, v[2] * inv};
  };

  // Helper: generate a perpendicular tangent from a normal (fallback)
  auto generate_fallback_tangent =
      [&](const value::normal3f &n) -> value::normal3f {
    // Choose a reference axis not parallel to n
    value::normal3f ref = (std::fabs(n[1]) < 0.9f)
                              ? value::normal3f{0.0f, 1.0f, 0.0f}
                              : value::normal3f{1.0f, 0.0f, 0.0f};
    value::normal3f t = vcross(n, ref);
    float len = safe_vlength(t);
    if (len < kTangentLengthEps) {
      return {1.0f, 0.0f, 0.0f};  // last resort
    }
    float inv = 1.0f / len;
    return {t[0] * inv, t[1] * inv, t[2] * inv};
  };

  // tn, bn = facevarying (value-initialized to zero by constructor)
  std::vector<value::normal3f> tn(faceVertexIndices.size(), {0.0f, 0.0f, 0.0f});
  std::vector<value::normal3f> bn(faceVertexIndices.size(), {0.0f, 0.0f, 0.0f});

  //
  // 1. Compute facevarying tangent/binormal for each faceVertex.
  //
  // UV determinant epsilon: use a float-appropriate threshold.
  // Values below this produce unreliable tangent directions due to
  // amplification of floating-point noise.
  constexpr float kUVDetEps = 1.0e-6f;

  size_t faceVertexIndexOffset{0};
  for (size_t i = 0; i < faceVertexCounts.size(); i++) {
    size_t nv = hasFaceVertexCounts ? faceVertexCounts[i] : 3;

    if ((faceVertexIndexOffset + nv) > faceVertexIndices.size()) {
      // Invalid faceVertexIndices
      PUSH_ERROR_AND_RETURN("Invalid value in faceVertexOffset.");
    }

    if (nv < 3) {
      PUSH_ERROR_AND_RETURN("Degenerated facet found.");
    }

    // Process each two-edges per facet.
    //
    // Example:
    //
    // fv3
    //  o----------------o fv2
    //   \              /
    //    \            /
    //     o----------o
    //    fv0         fv1

    // facet0:  fv0, fv1, fv2
    // facet1:  fv1, fv2, fv3

    for (size_t f = 0; f < nv - 2; f++) {
      size_t fid0 = faceVertexIndexOffset + f;
      size_t fid1 = faceVertexIndexOffset + f + 1;
      size_t fid2 = faceVertexIndexOffset + f + 2;

      uint32_t vf0 =
          is_facevarying_input ? uint32_t(fid0) : faceVertexIndices[fid0];
      uint32_t vf1 =
          is_facevarying_input ? uint32_t(fid1) : faceVertexIndices[fid1];
      uint32_t vf2 =
          is_facevarying_input ? uint32_t(fid2) : faceVertexIndices[fid2];

      if ((vf0 >= vertices.size()) || (vf1 >= vertices.size()) ||
          (vf2 >= vertices.size())) {
        // index out-of-range
        PUSH_ERROR_AND_RETURN(
            "Invalid value in faceVertexIndices. some exceeds vertices.size()");
      }

      if ((vf0 >= texcoords.size()) || (vf1 >= texcoords.size()) ||
          (vf2 >= texcoords.size())) {
        // index out-of-range
        PUSH_ERROR_AND_RETURN("Invalid index. some exceeds texcoords.size()");
      }

      vec3 v1 = vertices[vf0];
      vec3 v2 = vertices[vf1];
      vec3 v3 = vertices[vf2];

      vec2 uv1 = texcoords[vf0];
      vec2 uv2 = texcoords[vf1];
      vec2 uv3 = texcoords[vf2];

      // Skip triangle if any position or UV contains NaN/Inf
      if (!is_finite_f(v1[0]) || !is_finite_f(v1[1]) || !is_finite_f(v1[2]) ||
          !is_finite_f(v2[0]) || !is_finite_f(v2[1]) || !is_finite_f(v2[2]) ||
          !is_finite_f(v3[0]) || !is_finite_f(v3[1]) || !is_finite_f(v3[2]) ||
          !is_finite_f(uv1[0]) || !is_finite_f(uv1[1]) ||
          !is_finite_f(uv2[0]) || !is_finite_f(uv2[1]) ||
          !is_finite_f(uv3[0]) || !is_finite_f(uv3[1])) {
        // Leave tn/bn as zero for this face vertex - will get fallback later
        continue;
      }

      float x1 = v2[0] - v1[0];
      float x2 = v3[0] - v1[0];
      float y1 = v2[1] - v1[1];
      float y2 = v3[1] - v1[1];
      float z1 = v2[2] - v1[2];
      float z2 = v3[2] - v1[2];

      float s1 = uv2[0] - uv1[0];
      float s2 = uv3[0] - uv1[0];
      float t1 = uv2[1] - uv1[1];
      float t2 = uv3[1] - uv1[1];

      float det = s1 * t2 - s2 * t1;

      // Skip degenerate UV triangle: determinant too small means all UV
      // vertices are collinear (or coincident).  The tangent direction is
      // undefined; leave tn/bn as zero to trigger fallback later.
      if (std::fabs(det) < kUVDetEps) {
        continue;
      }

      float r = 1.0f / det;

      vec3 tdir{(t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r,
                (t2 * z1 - t1 * z2) * r};
      vec3 bdir{(s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r,
                (s1 * z2 - s2 * z1) * r};

      // Guard against Inf/NaN from extreme edge/UV ratios
      if (!is_finite_f(tdir[0]) || !is_finite_f(tdir[1]) ||
          !is_finite_f(tdir[2]) || !is_finite_f(bdir[0]) ||
          !is_finite_f(bdir[1]) || !is_finite_f(bdir[2])) {
        continue;
      }

      // Accumulate tangent/binormal contributions.
      // For triangles, += on zero-init is equivalent to =.
      // For quads/polygons, shared facevarying vertices get correct accumulation.
      tn[fid0][0] += tdir[0];
      tn[fid0][1] += tdir[1];
      tn[fid0][2] += tdir[2];

      tn[fid1][0] += tdir[0];
      tn[fid1][1] += tdir[1];
      tn[fid1][2] += tdir[2];

      tn[fid2][0] += tdir[0];
      tn[fid2][1] += tdir[1];
      tn[fid2][2] += tdir[2];

      bn[fid0][0] += bdir[0];
      bn[fid0][1] += bdir[1];
      bn[fid0][2] += bdir[2];

      bn[fid1][0] += bdir[0];
      bn[fid1][1] += bdir[1];
      bn[fid1][2] += bdir[2];

      bn[fid2][0] += bdir[0];
      bn[fid2][1] += bdir[1];
      bn[fid2][2] += bdir[2];
    }

    faceVertexIndexOffset += nv;
  }

  //
  // 2. Build indices(use same index for shared-vertex)
  //    Position-bucketed dedup: bucket by position index, linear scan within
  //    each bucket comparing only normal + uv (typical valence 4-8).
  //
  // Build facevarying normal lookup (used by both dedup and Gram-Schmidt).
  // normals[i] is facevarying when is_facevarying_input, otherwise indexed by
  // original vertex id → expand to facevarying.
  std::vector<value::normal3f> fv_normals(faceVertexIndices.size());
  if (is_facevarying_input) {
    for (size_t i = 0; i < faceVertexIndices.size(); i++) {
      fv_normals[i] = {normals[i][0], normals[i][1], normals[i][2]};
    }
  } else {
    for (size_t i = 0; i < faceVertexIndices.size(); i++) {
      const auto &n = normals[faceVertexIndices[i]];
      fv_normals[i] = {n[0], n[1], n[2]};
    }
  }

  std::vector<uint32_t> vertex_indices(faceVertexIndices.size());
  {
    // Expand texcoords to facevarying if needed (normals already expanded above).
    // Use fv_normals as vec3* for the dedup comparison — same memory layout.
    const vec3 *nrm_ptr = reinterpret_cast<const vec3 *>(fv_normals.data());
    const vec2 *uv_ptr = nullptr;
    std::vector<vec2> fv_uvs_expanded;

    if (is_facevarying_input) {
      uv_ptr = texcoords.data();
    } else {
      fv_uvs_expanded.resize(faceVertexIndices.size());
      for (size_t i = 0; i < faceVertexIndices.size(); i++) {
        fv_uvs_expanded[i] = texcoords[faceVertexIndices[i]];
      }
      uv_ptr = fv_uvs_expanded.data();
    }

    uint32_t numPoints = *std::max_element(faceVertexIndices.begin(),
                                           faceVertexIndices.end()) + 1;
    uint32_t next_vertex_id = 0;

    // Pre-count per-position degree to detect high-valence vertices.
    bool t_flatten = false;
    if (max_vertex_valence > 0) {
      std::vector<uint32_t> degree(numPoints, 0);
      for (size_t i = 0; i < faceVertexIndices.size(); i++) {
        degree[faceVertexIndices[i]]++;
      }
      uint32_t max_deg = *std::max_element(degree.begin(), degree.end());
      if (max_deg > max_vertex_valence) {
        DCOUT("tangent dedup: max vertex degree " << max_deg
              << " exceeds threshold " << max_vertex_valence
              << ", falling back to flatten.");
        t_flatten = true;
      }
    }

    if (t_flatten) {
      // Flatten: each face-vertex is its own unique vertex.
      for (size_t i = 0; i < faceVertexIndices.size(); i++) {
        vertex_indices[i] = uint32_t(i);
      }
      next_vertex_id = uint32_t(faceVertexIndices.size());
    } else {
      auto attribs_match = [&](size_t a, size_t b) -> bool {
        if (dedup_eps > 0.0f) {
          if (!math::is_close(nrm_ptr[a], nrm_ptr[b], dedup_eps)) return false;
          if (!math::is_close(uv_ptr[a], uv_ptr[b], dedup_eps)) return false;
        } else {
          if (memcmp(&nrm_ptr[a], &nrm_ptr[b], sizeof(vec3)) != 0) return false;
          if (memcmp(&uv_ptr[a], &uv_ptr[b], sizeof(vec2)) != 0) return false;
        }
        return true;
      };

      struct BucketEntry {
        uint32_t fv_index;
        uint32_t out_vertex_id;
      };

      std::vector<std::vector<BucketEntry>> buckets(numPoints);

      for (size_t i = 0; i < faceVertexIndices.size(); i++) {
        uint32_t pid = faceVertexIndices[i];
        auto &bucket = buckets[pid];
        uint32_t matched_id = ~0u;
        for (const auto &entry : bucket) {
          if (attribs_match(i, entry.fv_index)) {
            matched_id = entry.out_vertex_id;
            break;
          }
        }
        if (matched_id == ~0u) {
          matched_id = next_vertex_id++;
          bucket.push_back({uint32_t(i), matched_id});
        }
        vertex_indices[i] = matched_id;
      }
    }

    DCOUT("faceVertexIndices.size : " << faceVertexIndices.size());
    DCOUT("tangent dedup: " << next_vertex_id << " unique vertices from "
          << faceVertexIndices.size() << " face-vertices."
          << (t_flatten ? " (flattened)" : ""));
  }

  const uint32_t num_verts =
      *std::max_element(vertex_indices.begin(), vertex_indices.end()) + 1;

  //
  // 3. normalize * orthogonalize;
  //
  // fv_normals was already built above (before dedup block).

  // per-vertex tangents/binormals
  std::vector<value::normal3f> v_tn;
  v_tn.assign(num_verts, {0.0f, 0.0f, 0.0f});

  std::vector<value::normal3f> v_bn;
  v_bn.assign(num_verts, {0.0f, 0.0f, 0.0f});

  // Accumulate facevarying tangents into per-vertex.
  // tn[i] is the facevarying tangent at position i; vertex_indices[i] maps
  // facevarying position i to the unique vertex index.
  // Skip NaN/Inf contributions to prevent poisoning the accumulator.
  for (size_t i = 0; i < vertex_indices.size(); i++) {
    value::normal3f Tn = tn[i];
    value::normal3f Bn = bn[i];

    if (is_finite_v3(Tn)) {
      v_tn[vertex_indices[i]][0] += Tn[0];
      v_tn[vertex_indices[i]][1] += Tn[1];
      v_tn[vertex_indices[i]][2] += Tn[2];
    }

    if (is_finite_v3(Bn)) {
      v_bn[vertex_indices[i]][0] += Bn[0];
      v_bn[vertex_indices[i]][1] += Bn[1];
      v_bn[vertex_indices[i]][2] += Bn[2];
    }
  }

  // Normalize accumulated tangents/binormals with proper epsilon.
  // After accumulation the sum could overflow to Inf for vertices shared by
  // many triangles with large contributions.  safe_vnormalize handles this
  // gracefully by returning zero for degenerate/NaN/Inf input.
  for (size_t i = 0; i < size_t(num_verts); i++) {
    v_tn[i] = safe_vnormalize(v_tn[i]);
    v_bn[i] = safe_vnormalize(v_bn[i]);
  }

  tangents->assign(num_verts, {0.0f, 0.0f, 0.0f});
  binormals->assign(num_verts, {0.0f, 0.0f, 0.0f});

  for (size_t i = 0; i < vertex_indices.size(); i++) {
    value::normal3f n;

    // http://www.terathon.com/code/tangent.html
    // Use facevarying normal at position i (not the unique vertex index)
    n[0] = fv_normals[i][0];
    n[1] = fv_normals[i][1];
    n[2] = fv_normals[i][2];

    // Validate normal: must be finite and non-zero.
    // If degenerate, generate a fallback normal so we can still produce a
    // valid tangent frame.
    float nlen = safe_vlength(n);
    if (nlen < kTangentLengthEps) {
      n = {0.0f, 1.0f, 0.0f};  // arbitrary up direction
    } else {
      float inv = 1.0f / nlen;
      n = {n[0] * inv, n[1] * inv, n[2] * inv};
    }

    value::normal3f Tn = v_tn[vertex_indices[i]];
    value::normal3f Bn = v_bn[vertex_indices[i]];

    // Gram-Schmidt orthogonalize: remove the component of Tn along n.
    float d = vdot(n, Tn);
    if (is_finite_f(d)) {
      Tn = {Tn[0] - n[0] * d, Tn[1] - n[1] * d, Tn[2] - n[2] * d};
    } else {
      Tn = {0.0f, 0.0f, 0.0f};
    }

    // Normalize with a proper epsilon to avoid amplifying near-zero noise
    Tn = safe_vnormalize(Tn);

    if (safe_vlength(Tn) < kTangentLengthEps) {
      // Degenerate tangent after Gram-Schmidt (e.g. tangent was parallel to
      // normal, or all contributing triangles had degenerate UVs).
      // Generate a fallback tangent perpendicular to the normal.
      Tn = generate_fallback_tangent(n);
    }

    // Calculate handedness: flip tangent if the frame is left-handed.
    // Guard against NaN in the dot product from degenerate binormals.
    {
      value::normal3f cross_n_t = vcross(n, Tn);
      float hand = vdot(cross_n_t, Bn);
      if (is_finite_f(hand) && hand < 0.0f) {
        Tn = {Tn[0] * -1.0f, Tn[1] * -1.0f, Tn[2] * -1.0f};
      }
    }

    // Binormal: recompute if degenerate, NaN, or too short.
    float blen = safe_vlength(Bn);
    if (blen < kTangentLengthEps) {
      Bn = vcross(n, Tn);
      Bn = safe_vnormalize(Bn);
      // If still degenerate (n and Tn parallel - shouldn't happen but guard)
      if (safe_vlength(Bn) < kTangentLengthEps) {
        Bn = generate_fallback_tangent(Tn);
      }
    }

    // Final validation: ensure output is finite.  If not, use fallbacks.
    if (!is_finite_v3(Tn)) {
      Tn = generate_fallback_tangent(n);
    }
    if (!is_finite_v3(Bn)) {
      Bn = vcross(n, Tn);
      Bn = safe_vnormalize(Bn);
      if (safe_vlength(Bn) < kTangentLengthEps || !is_finite_v3(Bn)) {
        Bn = generate_fallback_tangent(Tn);
      }
    }

    ((*tangents)[vertex_indices[i]])[0] = Tn[0];
    ((*tangents)[vertex_indices[i]])[1] = Tn[1];
    ((*tangents)[vertex_indices[i]])[2] = Tn[2];

    ((*binormals)[vertex_indices[i]])[0] = Bn[0];
    ((*binormals)[vertex_indices[i]])[1] = Bn[1];
    ((*binormals)[vertex_indices[i]])[2] = Bn[2];
  }

  (*out_vertex_indices) = vertex_indices;

  return true;
}

//
// Compute geometric normal in CCW(Counter Clock-Wise) manner
// Also computes the area of the input triangle.
//
inline static value::float3 GeometricNormal(const value::float3 v0,
                                            const value::float3 v1,
                                            const value::float3 v2,
                                            float &area) {
  const value::float3 v10 = {v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]};
  const value::float3 v20 = {v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]};

  value::float3 Nf = vcross(v10, v20);  // CCW
  float len = vlength(Nf);
  area = 0.5f * len;

  // Guard against degenerate triangles (collinear/coincident vertices).
  // Return zero normal; caller should check area before using.
  if (len < 1.0e-30f) {
    return {0.0f, 0.0f, 0.0f};
  }

  float inv = 1.0f / len;
  return {Nf[0] * inv, Nf[1] * inv, Nf[2] * inv};
}

//
// Compute a normal for vertices.
// Normal vector is computed as weighted(by the area of the triangle) vector.
//
// TODO: Implement better normal calculation. ref.
// http://www.bytehazard.com/articles/vertnorm.html
//
static bool ComputeNormals(const std::vector<vec3> &vertices,
                           const std::vector<uint32_t> &faceVertexCounts,
                           const std::vector<uint32_t> &faceVertexIndices,
                           std::vector<vec3> &normals, std::string *err) {
  normals.assign(vertices.size(), {0.0f, 0.0f, 0.0f});

  size_t faceVertexIndexOffset{0};
  for (size_t f = 0; f < faceVertexCounts.size(); f++) {
    size_t nv = faceVertexCounts[f];

    if (nv < 3) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Invalid face num {} at faceVertexCounts[{}]", nv, f));
    }

    // For quad/polygon, first three vertices are used to compute face normal
    // (Assume quad/polygon plane is co-planar)
    uint32_t vidx0 = faceVertexIndices[faceVertexIndexOffset + 0];
    uint32_t vidx1 = faceVertexIndices[faceVertexIndexOffset + 1];
    uint32_t vidx2 = faceVertexIndices[faceVertexIndexOffset + 2];

    if (vidx0 >= vertices.size()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("vertexIndex0 {} exceeds vertices.size {}", vidx0, vertices.size()));
    }

    if (vidx1 >= vertices.size()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("vertexIndex1 {} exceeds vertices.size {}", vidx1, vertices.size()));
    }

    if (vidx2 >= vertices.size()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("vertexIndex2 {} exceeds vertices.size {}", vidx2, vertices.size()));
    }

    float area{0.0f};
    value::float3 Nf = GeometricNormal(vertices[vidx0], vertices[vidx1],
                                       vertices[vidx2], area);

    // Skip degenerate faces (zero-area) to prevent NaN propagation.
    if (area < 1.0e-20f) {
      faceVertexIndexOffset += nv;
      continue;
    }

    for (size_t v = 0; v < nv; v++) {
      uint32_t vidx = faceVertexIndices[faceVertexIndexOffset + v];
      if (vidx >= vertices.size()) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "vertexIndex exceeds vertices.size {}", vertices.size()));
      }
      normals[vidx][0] += area * Nf[0];
      normals[vidx][1] += area * Nf[1];
      normals[vidx][2] += area * Nf[2];
    }

    faceVertexIndexOffset += nv;
  }

  // Normalize accumulated normals.  Vertices with no valid face
  // contribution keep a zero vector (no arbitrary fallback).
  for (size_t v = 0; v < normals.size(); v++) {
    float len = vlength(normals[v]);
    if (len > 1.0e-20f) {
      float inv = 1.0f / len;
      normals[v] = {normals[v][0] * inv, normals[v][1] * inv, normals[v][2] * inv};
    }
  }

  return true;
}

}  // namespace

// ---------------------------------------------------------------
// BuildIndices implementation
// (moved from render-data.hh to reduce header bloat)
// ---------------------------------------------------------------

template <class VertexInput, class VertexOutput, class PackedVert,
          class PackedVertHasher, class PackedVertEqual>
void BuildIndices(const VertexInput &input, VertexOutput &output,
                  std::vector<uint32_t> &out_indices, std::vector<uint32_t> &out_point_indices)
{
  std::unordered_map<PackedVert, uint32_t, PackedVertHasher, PackedVertEqual>
      vertexToIndexMap;

  auto GetSimilarVertex = [&](const PackedVert &v, uint32_t &out_idx) -> bool {
    auto it = vertexToIndexMap.find(v);
    if (it == vertexToIndexMap.end()) {
      return false;
    }

    out_idx = it->second;
    return true;
  };

  for (size_t i = 0; i < input.size(); i++) {
    PackedVert v;
    input.get(i, v);

    uint32_t index{0};
    bool found = GetSimilarVertex(v, index);
    if (found) {
      out_indices.push_back(index);
    } else {
      uint32_t new_index = uint32_t(output.size());
      out_indices.push_back(new_index);
      output.push_back(v);
      vertexToIndexMap[v] = new_index;
    }
    out_point_indices.push_back(v.point_index);
  }
}

// Explicit instantiation for BuildIndices
template void BuildIndices<
    DefaultVertexInput<DefaultPackedVertexData>,
    DefaultVertexOutput<DefaultPackedVertexData>,
    DefaultPackedVertexData,
    DefaultPackedVertexDataHasher,
    DefaultPackedVertexDataEqual>(
    const DefaultVertexInput<DefaultPackedVertexData> &,
    DefaultVertexOutput<DefaultPackedVertexData> &,
    std::vector<uint32_t> &, std::vector<uint32_t> &);


namespace {

bool ListUVNames(const RenderMaterial &material,
                 const std::vector<UVTexture> &textures,
                 StringAndIdMap &si_map) {
  // Helper lambdas to extract UV names from shader parameters
  auto fun_vec3 = [&](const ShaderParam<vec3> &param) {
    int32_t texId = param.texture_id;
    if ((texId >= 0) && (size_t(texId) < textures.size())) {
      const UVTexture &tex = textures[size_t(texId)];
      if (tex.varname_uv.size()) {
        if (si_map.find(tex.varname_uv) == si_map.s_end()) {
          uint64_t slotId = si_map.size();
          DCOUT("Add textureSlot: " << tex.varname_uv << ", " << slotId);
          si_map.add(tex.varname_uv, slotId);
        }
      }
    }
  };

  auto fun_float = [&](const ShaderParam<float> &param) {
    int32_t texId = param.texture_id;
    if ((texId >= 0) && (size_t(texId) < textures.size())) {
      const UVTexture &tex = textures[size_t(texId)];
      if (tex.varname_uv.size()) {
        if (si_map.find(tex.varname_uv) == si_map.s_end()) {
          uint64_t slotId = si_map.size();
          DCOUT("Add textureSlot: " << tex.varname_uv << ", " << slotId);
          si_map.add(tex.varname_uv, slotId);
        }
      }
    }
  };

  // Check UsdPreviewSurface shader
  if (material.surfaceShader.has_value()) {
    fun_vec3(material.surfaceShader->diffuseColor);
    fun_vec3(material.surfaceShader->normal);
    fun_float(material.surfaceShader->metallic);
    fun_float(material.surfaceShader->roughness);
    fun_float(material.surfaceShader->clearcoat);
    fun_float(material.surfaceShader->clearcoatRoughness);
    fun_float(material.surfaceShader->opacity);
    fun_float(material.surfaceShader->opacityThreshold);
    fun_float(material.surfaceShader->ior);
    fun_float(material.surfaceShader->displacement);
    fun_float(material.surfaceShader->occlusion);
  }

  // Check MaterialX OpenPBR shader
  if (material.openPBRShader.has_value()) {
    // Base layer
    fun_float(material.openPBRShader->base_weight);
    fun_vec3(material.openPBRShader->base_color);
    fun_float(material.openPBRShader->base_roughness);
    fun_float(material.openPBRShader->base_metalness);
    fun_float(material.openPBRShader->base_diffuse_roughness);

    // Specular layer
    fun_float(material.openPBRShader->specular_weight);
    fun_vec3(material.openPBRShader->specular_color);
    fun_float(material.openPBRShader->specular_roughness);
    fun_float(material.openPBRShader->specular_ior);
    fun_float(material.openPBRShader->specular_ior_level);
    fun_float(material.openPBRShader->specular_anisotropy);
    fun_float(material.openPBRShader->specular_rotation);

    // Transmission
    fun_float(material.openPBRShader->transmission_weight);
    fun_vec3(material.openPBRShader->transmission_color);
    fun_float(material.openPBRShader->transmission_depth);
    fun_vec3(material.openPBRShader->transmission_scatter);
    fun_float(material.openPBRShader->transmission_scatter_anisotropy);
    fun_float(material.openPBRShader->transmission_dispersion);

    // Subsurface
    fun_float(material.openPBRShader->subsurface_weight);
    fun_vec3(material.openPBRShader->subsurface_color);
    fun_float(material.openPBRShader->subsurface_radius);
    fun_vec3(material.openPBRShader->subsurface_radius_scale);
    fun_float(material.openPBRShader->subsurface_scale);
    fun_float(material.openPBRShader->subsurface_anisotropy);

    // Sheen
    fun_float(material.openPBRShader->sheen_weight);
    fun_vec3(material.openPBRShader->sheen_color);
    fun_float(material.openPBRShader->sheen_roughness);

    // Fuzz
    fun_float(material.openPBRShader->fuzz_weight);
    fun_vec3(material.openPBRShader->fuzz_color);
    fun_float(material.openPBRShader->fuzz_roughness);

    // Thin film
    fun_float(material.openPBRShader->thin_film_weight);
    fun_float(material.openPBRShader->thin_film_thickness);
    fun_float(material.openPBRShader->thin_film_ior);

    // Coat
    fun_float(material.openPBRShader->coat_weight);
    fun_vec3(material.openPBRShader->coat_color);
    fun_float(material.openPBRShader->coat_roughness);
    fun_float(material.openPBRShader->coat_anisotropy);
    fun_float(material.openPBRShader->coat_rotation);
    fun_float(material.openPBRShader->coat_ior);
    fun_float(material.openPBRShader->coat_affect_color);
    fun_float(material.openPBRShader->coat_affect_roughness);

    // Emission
    fun_float(material.openPBRShader->emission_luminance);
    fun_vec3(material.openPBRShader->emission_color);

    // Geometry
    fun_float(material.openPBRShader->opacity);
    fun_vec3(material.openPBRShader->normal);
    fun_vec3(material.openPBRShader->tangent);
  }

  return true;
}


#undef PushError

}  // namespace

///
/// Convert vertex variability either 'vertex' or 'facevarying'
///
/// @param[in] to_vertex_varying true: Convert to 'vertrex' varying. false:
/// Convert to 'facevarying'
///
bool RenderSceneConverter::ConvertVertexVariabilityImpl(
    VertexAttribute &vattr, const bool to_vertex_varying,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices) {
  if (vattr.data.empty()) {
    return true;
  }

  if (vattr.variability == VertexVariability::Uniform) {
    if (to_vertex_varying) {
      auto result = UniformToVertex(vattr.get_data(), vattr.stride_bytes(),
                                    faceVertexCounts, faceVertexIndices);

      if (!result) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Convert `{}` attribute with uniform-varying "
                        "to vertex-varying failed: {}",
                        vattr.name, result.error()));
      }

      vattr.data = result.value();
      vattr.variability = VertexVariability::Vertex;

    } else {
      auto result = UniformToFaceVarying(vattr.get_data(), vattr.stride_bytes(),
                                         faceVertexCounts);
      if (!result) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Convert uniform `{}` attribute to failed: {}",
                        vattr.name, result.error()));
      }

      vattr.data = result.value();
      vattr.variability = VertexVariability::FaceVarying;
    }
  } else if (vattr.variability == VertexVariability::Constant) {
    if (to_vertex_varying) {
      auto result = ConstantToVertex(vattr.get_data(), vattr.stride_bytes(),
                                     faceVertexCounts, faceVertexIndices);

      if (!result) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Convert `{}` attribute with uniform-varying "
                        "to vertex-varying failed: {}",
                        vattr.name, result.error()));
      }

      vattr.data = result.value();
      vattr.variability = VertexVariability::Vertex;

    } else {
      auto result = UniformToFaceVarying(vattr.get_data(), vattr.stride_bytes(),
                                         faceVertexCounts);
      if (!result) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Convert uniform `{}` attribute to failed: {}",
                        vattr.name, result.error()));
      }

      vattr.data = result.value();
      vattr.variability = VertexVariability::FaceVarying;
    }

  } else if ((vattr.variability == VertexVariability::Vertex) ||
             (vattr.variability == VertexVariability::Varying)) {
    if (!to_vertex_varying) {
      auto result = VertexToFaceVarying(vattr.get_data(), vattr.stride_bytes(),
                                        faceVertexCounts, faceVertexIndices);
      if (!result) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Convert vertex/varying `{}` attribute to failed: {}",
                        vattr.name, result.error()));
      }

      vattr.data = result.value();
      vattr.variability = VertexVariability::FaceVarying;
    }

  } else if (vattr.variability == VertexVariability::FaceVarying) {
    if (to_vertex_varying) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. `to_vertex_varying` should not be true when "
          "FaceVarying.");
    }

  } else {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Unsupported/unimplemented interpolation: {} ",
                    to_string(vattr.variability)));
  }

  return true;
}

// ---------------------------------------------------------------------------
// Reorder vertex-varying attributes (points, joint/weights, BlendShape
// targets) so that they match a new vertex layout.
//
// `vert_to_point[v]` gives the original mesh.points index for output vertex v.
// The array size determines the number of output vertices.
// ---------------------------------------------------------------------------
namespace {
#define PushError(msg) TYDRA_PUSH_ERROR(err, msg)

static bool ReorderVertexVaryingAttributes(
    RenderMesh &mesh,
    const std::vector<uint32_t> &vert_to_point,
    std::string *err) {

  const size_t num_verts = vert_to_point.size();
  if (num_verts == 0) {
    PUSH_ERROR_AND_RETURN(
        "Internal error. vert_to_point is empty in ReorderVertexVaryingAttributes.");
  }

  // --- Points ---
  {
    std::vector<value::float3> tmp_points(num_verts);
    for (size_t v = 0; v < num_verts; v++) {
      if (vert_to_point[v] >= mesh.points.size()) {
        PUSH_ERROR_AND_RETURN("Internal error. point index out-of-range.");
      }
      tmp_points[v] = mesh.points[vert_to_point[v]];
    }
    mesh.points.swap(tmp_points);
  }

  // --- Joint indices / weights ---
  if (mesh.joint_and_weights.jointIndices.size()) {
    if (mesh.joint_and_weights.elementSize < 1) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. Invalid elementSize in mesh.joint_and_weights.");
    }
    uint32_t elementSize = uint32_t(mesh.joint_and_weights.elementSize);
    std::vector<int> tmp_indices(num_verts * size_t(elementSize));
    std::vector<float> tmp_weights(num_verts * size_t(elementSize));
    for (size_t v = 0; v < num_verts; v++) {
      if ((elementSize * vert_to_point[v]) >=
          mesh.joint_and_weights.jointIndices.size()) {
        PUSH_ERROR_AND_RETURN(
            "Internal error. point index exceeds jointIndices.size.");
      }
      for (size_t k = 0; k < elementSize; k++) {
        tmp_indices[size_t(elementSize) * v + k] =
            mesh.joint_and_weights
                .jointIndices[size_t(elementSize) * size_t(vert_to_point[v]) + k];
      }

      if ((elementSize * vert_to_point[v]) >=
          mesh.joint_and_weights.jointWeights.size()) {
        PUSH_ERROR_AND_RETURN(
            "Internal error. point index exceeds jointWeights.size.");
      }

      for (size_t k = 0; k < elementSize; k++) {
        tmp_weights[size_t(elementSize) * v + k] =
            mesh.joint_and_weights
                .jointWeights[size_t(elementSize) * size_t(vert_to_point[v]) + k];
      }
    }
    mesh.joint_and_weights.jointIndices.swap(tmp_indices);
    mesh.joint_and_weights.jointWeights.swap(tmp_weights);
  }

  // --- BlendShape targets ---
  if (mesh.targets.size()) {
    // For BlendShape, reordering pointIndices, pointOffsets and normalOffsets is not enough.
    // Some points could be duplicated, so we need to find a mapping of org pointIdx -> pointIdx list in reordered points,
    // Then splat point attributes accordingly.

    // org pointIdx -> List of pointIdx in reordered points.
    std::unordered_map<uint32_t, std::vector<uint32_t>> pointIdxRemap;
    pointIdxRemap.reserve(num_verts);

    for (size_t v = 0; v < num_verts; v++) {
      pointIdxRemap[vert_to_point[v]].push_back(uint32_t(v));
    }

    for (auto &target : mesh.targets) {

      std::vector<value::float3> tmpPointOffsets;
      std::vector<value::float3> tmpNormalOffsets;
      std::vector<uint32_t> tmpPointIndices;

      for (size_t i = 0; i < target.second.pointIndices.size(); i++) {

        uint32_t orgPointIdx = target.second.pointIndices[i];
        auto remap_it = pointIdxRemap.find(orgPointIdx);
        if (remap_it == pointIdxRemap.end()) {
          PUSH_ERROR_AND_RETURN("Invalid pointIndices value.");
        }
        const std::vector<uint32_t> &dstPointIndices = remap_it->second;

        for (size_t k = 0; k < dstPointIndices.size(); k++) {
          if (target.second.pointOffsets.size()) {
            if (i >= target.second.pointOffsets.size()) {
              PUSH_ERROR_AND_RETURN("Invalid pointOffsets.size.");
            }
            tmpPointOffsets.push_back(target.second.pointOffsets[i]);
          }
          if (target.second.normalOffsets.size()) {
            if (i >= target.second.normalOffsets.size()) {
              PUSH_ERROR_AND_RETURN("Invalid normalOffsets.size.");
            }
            tmpNormalOffsets.push_back(target.second.normalOffsets[i]);
          }

          tmpPointIndices.push_back(dstPointIndices[k]);
        }
      }

      target.second.pointIndices.swap(tmpPointIndices);
      target.second.pointOffsets.swap(tmpPointOffsets);
      target.second.normalOffsets.swap(tmpNormalOffsets);

    }

    // TODO: Inbetween BlendShapes

  }

  return true;
}

#undef PushError
}  // namespace

bool RenderSceneConverter::BuildVertexIndicesImpl(RenderMesh &mesh, uint32_t max_vertex_valence, float dedup_eps) {
  //
  // - If mesh is triangulated, use triangulatedFaceVertexIndices, otherwise use
  // faceVertxIndices.
  // - Make vertex attributes 'facevarying' variability
  // - Assign same id for similar(currently identitical) vertex attribute.
  // - Reorder vertex attributes to 'vertex' variability.
  //

  //TUSDZ_LOG_I("BuildVertexIndicesImpl");

  const std::vector<uint32_t> &fvIndices =
      mesh.triangulatedFaceVertexIndices.size()
          ? mesh.triangulatedFaceVertexIndices
          : mesh.usdFaceVertexIndices;

  // Empty mesh (e.g. Blender export with points=[], faceVertexIndices=[]):
  // nothing to build, just succeed.
  if (fvIndices.empty()) {
    return true;
  }

  //std::cout << "triangulatedFaceVertexIndices.max_value: " << *std::max_element(mesh.triangulatedFaceVertexIndices.begin(), mesh.triangulatedFaceVertexIndices.end() << "\n");

  //std::cout << "usdFaceVertexIndices.min_value: " << *std::min_element(mesh.usdFaceVertexIndices.begin(), mesh.usdFaceVertexIndices.end() << "\n");
  //std::cout << "usdFaceVertexIndices.max_value: " << *std::max_element(mesh.usdFaceVertexIndices.begin(), mesh.usdFaceVertexIndices.end() << "\n");

  size_t num_verts = mesh.points.size();
  size_t num_fvs = fvIndices.size();

  // Validate that a vertex attribute is facevarying with the expected count.
#define VALIDATE_FACEVARYING_ATTR(attr, name) \
  if (attr.vertex_count()) { \
    if (!attr.is_facevarying()) { \
      PUSH_ERROR_AND_RETURN( \
          "Internal error. " name " must be 'facevarying' variability."); \
    } \
    if (attr.vertex_count() != num_fvs) { \
      PUSH_ERROR_AND_RETURN( \
          "Internal error. The number of " name " items does not match " \
          "with the number of facevarying items."); \
    } \
  }

  VALIDATE_FACEVARYING_ATTR(mesh.normals, "normals")

  const value::float2 *texcoord0_ptr = nullptr;
  const value::float2 *texcoord1_ptr = nullptr;

  for (const auto &it : mesh.texcoords) {
    if (it.second.vertex_count() > 0) {
      if (!it.second.is_facevarying()) {
        PUSH_ERROR_AND_RETURN(
            "Internal error. texcoords must be 'facevarying' variability.");
      }
      if (it.second.vertex_count() != num_fvs) {
        PUSH_ERROR_AND_RETURN(
            "Internal error. The number of texcoord items does not match "
            "with the number of facevarying items.");
      }

      if (it.first == 0) {
        texcoord0_ptr = reinterpret_cast<const value::float2 *>(
            it.second.get_data().data());
      } else if (it.first == 1) {
        texcoord1_ptr = reinterpret_cast<const value::float2 *>(
            it.second.get_data().data());
      } else {
        // ignore.
      }
    }
  }

  const value::float3 *tangents_ptr = nullptr;
  const value::float3 *binormals_ptr = nullptr;

  if (texcoord0_ptr) {
    VALIDATE_FACEVARYING_ATTR(mesh.tangents, "tangents")
    if (mesh.tangents.vertex_count()) {
      tangents_ptr = reinterpret_cast<const value::float3 *>(
          mesh.tangents.get_data().data());
    }

    VALIDATE_FACEVARYING_ATTR(mesh.binormals, "binormals")
    if (mesh.binormals.vertex_count()) {
      binormals_ptr = reinterpret_cast<const value::float3 *>(
          mesh.binormals.get_data().data());
    }
  }

  VALIDATE_FACEVARYING_ATTR(mesh.vertex_colors, "vertex_colors")
  VALIDATE_FACEVARYING_ATTR(mesh.vertex_opacities, "vertex_opacities")

  const value::float3 *normals_ptr =
      (mesh.normals.vertex_count() > 0)
          ? reinterpret_cast<const value::float3 *>(
                mesh.normals.get_data().data())
          : nullptr;
  const value::float3 *colors_ptr =
      (mesh.vertex_colors.vertex_count() > 0)
          ? reinterpret_cast<const value::float3 *>(
                mesh.vertex_colors.get_data().data())
          : nullptr;
  const float *opacities_ptr =
      (mesh.vertex_opacities.vertex_count() > 0)
          ? reinterpret_cast<const float *>(
                mesh.vertex_opacities.get_data().data())
          : nullptr;

  //
  // Position-bucketed vertex deduplication.
  // Bucket by position index (faceVertexIndices[i]), then linear-scan within
  // each bucket comparing only the attributes that are present.
  // Typical vertex valence is 4-8, so each scan is very short.
  //
  // Safeguard: if any vertex's degree (number of face-vertex references)
  // exceeds max_vertex_valence, skip dedup entirely (flatten).
  //

  std::vector<uint32_t> out_indices(num_fvs);
  std::vector<uint32_t> out_point_indices(num_fvs);
  uint32_t next_vertex_id = 0;

  // Pre-count per-position degree to detect high-valence vertices.
  bool flatten = false;
  if (max_vertex_valence > 0 && num_verts > 0) {
    std::vector<uint32_t> degree(num_verts, 0);
    for (size_t i = 0; i < num_fvs; i++) {
      uint32_t pid = fvIndices[i];
      if (pid < num_verts) {
        degree[pid]++;
      }
    }
    uint32_t max_deg = *std::max_element(degree.begin(), degree.end());
    if (max_deg > max_vertex_valence) {
      DCOUT("Max vertex degree " << max_deg << " exceeds threshold "
            << max_vertex_valence << ", falling back to flatten (no dedup).");
      flatten = true;
    }
  }

  if (flatten) {
    // Flatten: each face-vertex becomes its own unique vertex (no dedup).
    for (size_t i = 0; i < num_fvs; i++) {
      uint32_t pid = fvIndices[i];
      if (pid >= num_verts) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Invalid faceVertexIndex {}. Must be less than {}(triangulated = {})", pid, num_verts, mesh.triangulatedFaceVertexIndices.size() ? "true" : "false"));
      }
      out_indices[i] = uint32_t(i);
      out_point_indices[i] = pid;
    }
    next_vertex_id = uint32_t(num_fvs);
  } else {
    // Normal position-bucketed dedup.
    struct BucketEntry {
      uint32_t fv_index;       // source face-vertex index (for attribute comparison)
      uint32_t out_vertex_id;  // assigned output vertex index
    };

    // When dedup_eps > 0, use is_close() for approximate matching (handles
    // DCC rounding, interpolation artifacts). Otherwise use exact memcmp.
    auto attribs_match = [&](size_t a, size_t b) -> bool {
      if (dedup_eps > 0.0f) {
        if (normals_ptr   && !math::is_close(normals_ptr[a],   normals_ptr[b],   dedup_eps)) return false;
        if (texcoord0_ptr && !math::is_close(texcoord0_ptr[a],  texcoord0_ptr[b], dedup_eps)) return false;
        if (texcoord1_ptr && !math::is_close(texcoord1_ptr[a],  texcoord1_ptr[b], dedup_eps)) return false;
        if (tangents_ptr  && !math::is_close(tangents_ptr[a],   tangents_ptr[b],  dedup_eps)) return false;
        if (binormals_ptr && !math::is_close(binormals_ptr[a],  binormals_ptr[b], dedup_eps)) return false;
        if (colors_ptr    && !math::is_close(colors_ptr[a],     colors_ptr[b],    dedup_eps)) return false;
        if (opacities_ptr && !math::is_close(opacities_ptr[a],  opacities_ptr[b], dedup_eps)) return false;
      } else {
        if (normals_ptr   && memcmp(&normals_ptr[a],   &normals_ptr[b],   sizeof(value::float3)) != 0) return false;
        if (texcoord0_ptr && memcmp(&texcoord0_ptr[a],  &texcoord0_ptr[b], sizeof(value::float2)) != 0) return false;
        if (texcoord1_ptr && memcmp(&texcoord1_ptr[a],  &texcoord1_ptr[b], sizeof(value::float2)) != 0) return false;
        if (tangents_ptr  && memcmp(&tangents_ptr[a],   &tangents_ptr[b],  sizeof(value::float3)) != 0) return false;
        if (binormals_ptr && memcmp(&binormals_ptr[a],  &binormals_ptr[b], sizeof(value::float3)) != 0) return false;
        if (colors_ptr    && memcmp(&colors_ptr[a],     &colors_ptr[b],    sizeof(value::float3)) != 0) return false;
        if (opacities_ptr && memcmp(&opacities_ptr[a],  &opacities_ptr[b], sizeof(float))         != 0) return false;
      }
      return true;
    };

    std::vector<std::vector<BucketEntry>> buckets(num_verts);

    for (size_t i = 0; i < num_fvs; i++) {
      uint32_t pid = fvIndices[i];
      if (pid >= num_verts) {
        PUSH_ERROR("usdFaceVertexIndices.min_value: " << *std::min_element(mesh.usdFaceVertexIndices.begin(), mesh.usdFaceVertexIndices.end()) << "\n");
        PUSH_ERROR("usdFaceVertexIndices.max_value: " << *std::max_element(mesh.usdFaceVertexIndices.begin(), mesh.usdFaceVertexIndices.end()) << "\n");
        PUSH_ERROR("triangulatedFaceVertexIndices.min_value: " << *std::min_element(mesh.triangulatedFaceVertexIndices.begin(), mesh.triangulatedFaceVertexIndices.end()) << "\n");
        PUSH_ERROR("triangulatedFaceVertexIndices.max_value: " << *std::max_element(mesh.triangulatedFaceVertexIndices.begin(), mesh.triangulatedFaceVertexIndices.end()) << "\n");
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Invalid faceVertexIndex {}. Must be less than {}(triangulated = {})", pid, num_verts, mesh.triangulatedFaceVertexIndices.size() ? "true" : "false"));
      }

      auto &bucket = buckets[pid];
      uint32_t matched_id = ~0u;
      for (const auto &entry : bucket) {
        if (attribs_match(i, entry.fv_index)) {
          matched_id = entry.out_vertex_id;
          break;
        }
      }
      if (matched_id == ~0u) {
        matched_id = next_vertex_id++;
        bucket.push_back({uint32_t(i), matched_id});
      }
      out_indices[i] = matched_id;
      out_point_indices[i] = pid;
    }

    // Free dedup buckets — no longer needed after vertex dedup.
    { std::vector<std::vector<BucketEntry>> tmp; buckets.swap(tmp); }
  }

  DCOUT("faceVertexIndices.size : " << fvIndices.size());
  DCOUT("vertex dedup: " << next_vertex_id << " unique vertices from "
        << num_fvs << " face-vertices." << (flatten ? " (flattened)" : ""));

  // Build reordered attribute arrays from dedup results.
  // Each unique vertex is represented by its canonical face-vertex index.
  uint32_t numUniqueVerts = next_vertex_id;
  DefaultVertexOutput<DefaultPackedVertexData> vertex_output;
  vertex_output.point_indices.resize(numUniqueVerts);
  if (normals_ptr)   vertex_output.normals.resize(numUniqueVerts);
  if (texcoord0_ptr) vertex_output.uv0s.resize(numUniqueVerts);
  if (texcoord1_ptr) vertex_output.uv1s.resize(numUniqueVerts);
  if (tangents_ptr)  vertex_output.tangents.resize(numUniqueVerts);
  if (binormals_ptr) vertex_output.binormals.resize(numUniqueVerts);
  if (colors_ptr)    vertex_output.colors.resize(numUniqueVerts);
  if (opacities_ptr) vertex_output.opacities.resize(numUniqueVerts);

  // Populate vertex_output from dedup results.
  // In flatten mode, out_indices[i] == i so this is a direct copy.
  // In dedup mode, each unique vertex appears once via out_indices mapping.
  for (size_t i = 0; i < num_fvs; i++) {
    uint32_t vid = out_indices[i];
    vertex_output.point_indices[vid] = fvIndices[i];
    if (normals_ptr)   vertex_output.normals[vid]   = normals_ptr[i];
    if (texcoord0_ptr) vertex_output.uv0s[vid]      = texcoord0_ptr[i];
    if (texcoord1_ptr) vertex_output.uv1s[vid]      = texcoord1_ptr[i];
    if (tangents_ptr)  vertex_output.tangents[vid]   = tangents_ptr[i];
    if (binormals_ptr) vertex_output.binormals[vid]  = binormals_ptr[i];
    if (colors_ptr)    vertex_output.colors[vid]     = colors_ptr[i];
    if (opacities_ptr) vertex_output.opacities[vid]  = opacities_ptr[i];
  }



  //
  // Reorder 'vertex' varying attributes(points, jointIndices/jointWeights,
  // BlendShape points, ...)
  //
  if (!ReorderVertexVaryingAttributes(mesh, vertex_output.point_indices,
                                      &_err)) {
    return false;
  }

  // Other 'facevarying' attributes are now 'vertex' variability.
  // Free each vertex_output field immediately after set_buffer() copies it,
  // to avoid simultaneous peak overlap of source + destination.
  if (normals_ptr) {
    mesh.normals.set_buffer(
        reinterpret_cast<const uint8_t *>(vertex_output.normals.data()),
        vertex_output.normals.size() * sizeof(value::float3));
    mesh.normals.variability = VertexVariability::Vertex;
    { std::vector<value::float3> tmp; vertex_output.normals.swap(tmp); }
  }

  if (texcoord0_ptr) {
    mesh.texcoords[0].set_buffer(
        reinterpret_cast<const uint8_t *>(vertex_output.uv0s.data()),
        vertex_output.uv0s.size() * sizeof(value::float2));
    mesh.texcoords[0].variability = VertexVariability::Vertex;
    { std::vector<value::float2> tmp; vertex_output.uv0s.swap(tmp); }
  }

  if (texcoord1_ptr) {
    mesh.texcoords[1].set_buffer(
        reinterpret_cast<const uint8_t *>(vertex_output.uv1s.data()),
        vertex_output.uv1s.size() * sizeof(value::float2));
    mesh.texcoords[1].variability = VertexVariability::Vertex;
    { std::vector<value::float2> tmp; vertex_output.uv1s.swap(tmp); }
  }

  if (tangents_ptr) {
    mesh.tangents.set_buffer(
        reinterpret_cast<const uint8_t *>(vertex_output.tangents.data()),
        vertex_output.tangents.size() * sizeof(value::float3));
    mesh.tangents.variability = VertexVariability::Vertex;
    { std::vector<value::float3> tmp; vertex_output.tangents.swap(tmp); }
  }

  if (binormals_ptr) {
    mesh.binormals.set_buffer(
        reinterpret_cast<const uint8_t *>(vertex_output.binormals.data()),
        vertex_output.binormals.size() * sizeof(value::float3));
    mesh.binormals.variability = VertexVariability::Vertex;
    { std::vector<value::float3> tmp; vertex_output.binormals.swap(tmp); }
  }

  if (colors_ptr) {
    mesh.vertex_colors.set_buffer(
        reinterpret_cast<const uint8_t *>(vertex_output.colors.data()),
        vertex_output.colors.size() * sizeof(value::float3));
    mesh.vertex_colors.variability = VertexVariability::Vertex;
    { std::vector<value::float3> tmp; vertex_output.colors.swap(tmp); }
  }

  if (opacities_ptr) {
    mesh.vertex_opacities.set_buffer(
        reinterpret_cast<const uint8_t *>(vertex_output.opacities.data()),
        vertex_output.opacities.size() * sizeof(float));
    mesh.vertex_opacities.variability = VertexVariability::Vertex;
    { std::vector<float> tmp; vertex_output.opacities.swap(tmp); }
  }

  if (mesh.is_triangulated()) {
    mesh.triangulatedFaceVertexIndices = std::move(out_indices);
  } else {
    mesh.usdFaceVertexIndices = std::move(out_indices);
  }


  return true;
}

bool RenderSceneConverter::BuildVertexIndicesFastImpl(RenderMesh &mesh) {
  //
  // - If mesh is triangulated, use triangulatedFaceVertexIndices, otherwise use
  // faceVertxIndices.
  // - Make vertex attributes 'facevarying' variability
  // - No similarity search.
  // - Reorder vertex attributes to 'vertex' variability.
  //

  //TUSDZ_LOG_I("BuildVertexIndicesFastImpl");

  const std::vector<uint32_t> &fvIndices =
      mesh.triangulatedFaceVertexIndices.size()
          ? mesh.triangulatedFaceVertexIndices
          : mesh.usdFaceVertexIndices;

  // Empty mesh (e.g. Blender export with points=[], faceVertexIndices=[]):
  // nothing to build, just succeed.
  if (fvIndices.empty()) {
    return true;
  }

  size_t num_verts = mesh.points.size();
  size_t num_fvs = fvIndices.size();

  VALIDATE_FACEVARYING_ATTR(mesh.normals, "normals")

  for (const auto &it : mesh.texcoords) {
    if (it.second.vertex_count() > 0) {
      if (!it.second.is_facevarying()) {
        PUSH_ERROR_AND_RETURN(
            "Internal error. texcoords must be 'facevarying' variability.");
      }
      if (it.second.vertex_count() != num_fvs) {
        PUSH_ERROR_AND_RETURN(
            "Internal error. The number of texcoord items does not match "
            "with the number of facevarying items.");
      }
    }
  }

  VALIDATE_FACEVARYING_ATTR(mesh.tangents, "tangents")
  VALIDATE_FACEVARYING_ATTR(mesh.binormals, "binormals")
  VALIDATE_FACEVARYING_ATTR(mesh.vertex_colors, "vertex_colors")
  VALIDATE_FACEVARYING_ATTR(mesh.vertex_opacities, "vertex_opacities")

  // range check
  for (size_t i = 0; i < num_fvs; i++) {
    size_t fvi = fvIndices[i];
    if (fvi >= num_verts) {
      PUSH_ERROR("usdFaceVertexIndices.min_value: " << *std::min_element(mesh.usdFaceVertexIndices.begin(), mesh.usdFaceVertexIndices.end()) << "\n");
      PUSH_ERROR("usdFaceVertexIndices.max_value: " << *std::max_element(mesh.usdFaceVertexIndices.begin(), mesh.usdFaceVertexIndices.end()) << "\n");
      PUSH_ERROR("triangulatedFaceVertexIndices.min_value: " << *std::min_element(mesh.triangulatedFaceVertexIndices.begin(), mesh.triangulatedFaceVertexIndices.end()) << "\n");
      PUSH_ERROR("triangulatedFaceVertexIndices.max_value: " << *std::max_element(mesh.triangulatedFaceVertexIndices.begin(), mesh.triangulatedFaceVertexIndices.end()) << "\n");
      PUSH_ERROR_AND_RETURN(fmt::format(
          "Invalid faceVertexIndex {}. Must be less than {}(triangulated = {})", fvi, num_fvs, mesh.triangulatedFaceVertexIndices.size() ? "true" : "faise"));
    }
  }

  //
  // Reorder 'vertex' varying attributes(points, jointIndices/jointWeights,
  // BlendShape points, ...)
  //
  if (!ReorderVertexVaryingAttributes(mesh, fvIndices, &_err)) {
    return false;
  }

  //TUSDZ_LOG_I("proc normal");

  // Just change variability
  if (mesh.normals.vertex_count() > 0) {
      mesh.normals.variability = VertexVariability::Vertex;
  }

  for (auto &it : mesh.texcoords) {
    if (it.second.vertex_count() > 0) {
      it.second.variability = VertexVariability::Vertex;
    }
  }

  if (mesh.tangents.vertex_count() > 0) {
      mesh.tangents.variability = VertexVariability::Vertex;
  }

  if (mesh.binormals.vertex_count() > 0) {
      mesh.binormals.variability = VertexVariability::Vertex;
  }

  if (mesh.vertex_colors.vertex_count() > 0) {
      mesh.vertex_colors.variability = VertexVariability::Vertex;
  }

  if (mesh.vertex_opacities.vertex_count() > 0) {
      mesh.vertex_opacities.variability = VertexVariability::Vertex;
  }


  //TUSDZ_LOG_I("build indices");

  // TODO: omit indices.
  std::vector<uint32_t> out_indices;
  out_indices.resize(fvIndices.size());
  for (size_t i = 0; i < out_indices.size(); i++) {
    out_indices[i] = uint32_t(i);
  }

  if (mesh.is_triangulated()) {
    mesh.triangulatedFaceVertexIndices = std::move(out_indices);
  } else {
    mesh.usdFaceVertexIndices = std::move(out_indices);
  }

  //TUSDZ_LOG_I("done build indices");

  return true;
}

// ---------------------------------------------------------------------------
// Quantize tangents in a RenderMesh from Vec3 float to a packed format.
// Replaces tangent VertexAttribute with packed data and clears binormals.
// Returns true on success (or if no quantization is needed).
// ---------------------------------------------------------------------------
static bool QuantizeMeshTangents(
    RenderMesh &mesh,
    MeshConverterConfig::TangentStorageFormat format) {
  using namespace tangent_quantize;

  if (format == MeshConverterConfig::TangentStorageFormat::Float3) {
    return true;  // no quantization
  }

  // Only quantize from Vec3 float format
  if (mesh.tangents.format != VertexAttributeFormat::Vec3 ||
      mesh.binormals.format != VertexAttributeFormat::Vec3 ||
      mesh.normals.format != VertexAttributeFormat::Vec3) {
    return true;
  }

  size_t nT = mesh.tangents.vertex_count();
  size_t nB = mesh.binormals.vertex_count();
  size_t nN = mesh.normals.vertex_count();

  if (nT == 0 || nT != nB) return true;

  const value::float3 *T =
      reinterpret_cast<const value::float3 *>(mesh.tangents.data.data());
  const value::float3 *B =
      reinterpret_cast<const value::float3 *>(mesh.binormals.data.data());
  const value::float3 *N =
      reinterpret_cast<const value::float3 *>(mesh.normals.data.data());

  std::vector<value::float3> tangents_v(T, T + nT);
  std::vector<value::float3> binormals_v(B, B + nT);
  std::vector<value::float3> normals_v;

  if (nN == nT) {
    normals_v.assign(N, N + nT);
  } else if (nN > 0 &&
             mesh.tangents.variability == VertexVariability::FaceVarying &&
             mesh.normals.variability == VertexVariability::Vertex) {
    // Expand vertex normals to facevarying
    const auto &fvi = mesh.triangulatedFaceVertexIndices.size()
                          ? mesh.triangulatedFaceVertexIndices
                          : mesh.usdFaceVertexIndices;
    if (fvi.size() == nT) {
      normals_v.resize(nT);
      for (size_t i = 0; i < nT; i++) {
        normals_v[i] = (fvi[i] < nN) ? N[fvi[i]] : value::float3{0, 1, 0};
      }
    } else {
      return true;  // cannot match, skip
    }
  } else {
    return true;  // mismatched counts, skip
  }

  VertexVariability var = mesh.tangents.variability;
  std::string err;

  switch (format) {
    case MeshConverterConfig::TangentStorageFormat::Packed1010102: {
      std::vector<PackedTangent1010102> packed;
      if (!QuantizeTangents1010102(tangents_v, binormals_v, normals_v, &packed,
                                   &err))
        return false;
      mesh.tangents = PackToVertexAttribute(packed);
      break;
    }
    case MeshConverterConfig::TangentStorageFormat::PackedSNorm8: {
      std::vector<PackedTangentSNorm8x4> packed;
      if (!QuantizeTangentsSNorm8(tangents_v, binormals_v, normals_v, &packed,
                                  &err))
        return false;
      mesh.tangents = PackToVertexAttribute(packed);
      break;
    }
    case MeshConverterConfig::TangentStorageFormat::PackedFp16: {
      std::vector<PackedTangentFp16x4> packed;
      if (!QuantizeTangentsFp16(tangents_v, binormals_v, normals_v, &packed,
                                &err))
        return false;
      mesh.tangents = PackToVertexAttribute(packed);
      break;
    }
    default:
      return true;
  }

  mesh.tangents.variability = var;
  mesh.tangents.stride = 0;
  mesh.tangents.elementSize = 1;

  // Clear binormals — reconstructed in shader as cross(N, T.xyz) * T.w
  mesh.binormals.data.clear();
  mesh.binormals.data.shrink_to_fit();

  return true;
}

///
/// Try to convert face-varying normals to vertex by quantizing to 10-bit
/// SNORM and comparing packed uint32 values.  This succeeds when normals at
/// a shared vertex differ only by floating-point noise (< ~0.1 degree),
/// as is typical for subdivision surface limit normals.
///
/// On success the VertexAttribute is replaced with vertex-varying float3
/// normals (one per vertex).  Returns false if any vertex has mismatching
/// packed normals across its face-vertices.
///
static bool TryQuantizedNormalDedup(
    VertexAttribute &normals,
    const std::vector<uint32_t> &faceVertexIndices) {
  using namespace tangent_quantize;

  if (!normals.is_facevarying()) return false;
  if (normals.format != VertexAttributeFormat::Vec3) return false;

  size_t nFV = normals.vertex_count();
  if (nFV == 0) return false;
  if (nFV != faceVertexIndices.size()) return false;

  const value::float3 *src =
      reinterpret_cast<const value::float3 *>(normals.data.data());

  // Find max vertex index to size the output.
  uint32_t maxIdx = 0;
  for (size_t i = 0; i < faceVertexIndices.size(); i++) {
    maxIdx = (std::max)(maxIdx, faceVertexIndices[i]);
  }
  uint32_t numVerts = maxIdx + 1;

  // Per-vertex: packed normal (set on first visit) and a flag.
  std::vector<uint32_t> vertPacked(numVerts, 0);
  std::vector<uint8_t>  vertVisited(numVerts, 0);
  // Keep one representative float3 per vertex (avoids unquantize round-trip).
  std::vector<value::float3> vertNormal(numVerts, {0.0f, 0.0f, 0.0f});

  for (size_t i = 0; i < nFV; i++) {
    uint32_t vi = faceVertexIndices[i];
    uint32_t packed = pack_normal_1010102(src[i][0], src[i][1], src[i][2]);

    if (vertVisited[vi]) {
      if (vertPacked[vi] != packed) {
        return false;  // mismatch — normals are truly face-varying
      }
    } else {
      vertPacked[vi] = packed;
      vertNormal[vi] = src[i];
      vertVisited[vi] = 1;
    }
  }

  // All face-vertices agree at 10-bit resolution.  Replace with vertex data.
  VertexAttribute attr;
  attr.format = VertexAttributeFormat::Vec3;
  attr.variability = VertexVariability::Vertex;
  size_t resize_size;
  if (!safe::n_to_size<value::float3>(numVerts, &resize_size)) {
    return false;  // Error handling - return false
  }
  attr.data.resize(resize_size);
  size_t memcpy_size;
  if (!safe::mul(numVerts, sizeof(value::float3), &memcpy_size)) {
    return false;
  }
  std::memcpy(attr.data.data(), vertNormal.data(), memcpy_size);
  attr.stride = 0;
  attr.elementSize = 1;
  attr.name = normals.name;

  normals = std::move(attr);
  return true;
}

static bool QuantizeMeshNormals(
    RenderMesh &mesh,
    MeshConverterConfig::NormalStorageFormat format) {
  using namespace tangent_quantize;

  if (format == MeshConverterConfig::NormalStorageFormat::Float3) {
    return true;  // no quantization
  }

  if (mesh.normals.format != VertexAttributeFormat::Vec3) {
    return true;  // already quantized or not float3
  }

  size_t nN = mesh.normals.vertex_count();
  if (nN == 0) return true;

  const value::float3 *N =
      reinterpret_cast<const value::float3 *>(mesh.normals.data.data());

  VertexVariability var = mesh.normals.variability;

  if (format == MeshConverterConfig::NormalStorageFormat::PackedSNorm8) {
    std::vector<PackedNormalSNorm8x3> packed;
    QuantizeNormalsSNorm8x3(N, nN, &packed);

    VertexAttribute attr;
    attr.format = VertexAttributeFormat::Char3;
    size_t resize_size;
    if (!safe::mul(packed.size(), size_t(3), &resize_size)) {
      return false;
    }
    attr.data.resize(resize_size);
    size_t memcpy_size;
    if (!safe::mul(packed.size(), size_t(3), &memcpy_size)) {
      return false;
    }
    std::memcpy(attr.data.data(), packed.data(), memcpy_size);

    mesh.normals = std::move(attr);
  } else if (format == MeshConverterConfig::NormalStorageFormat::PackedSNorm16) {
    std::vector<PackedNormalSNorm16x3> packed;
    QuantizeNormalsSNorm16x3(N, nN, &packed);

    VertexAttribute attr;
    attr.format = VertexAttributeFormat::Short3;
    size_t resize_size;
    if (!safe::n_to_size<PackedNormalSNorm16x3>(packed.size(), &resize_size)) {
      return false;
    }
    attr.data.resize(resize_size);
    size_t memcpy_size;
    if (!safe::n_to_size<PackedNormalSNorm16x3>(packed.size(), &memcpy_size)) {
      return false;
    }
    std::memcpy(attr.data.data(), packed.data(), memcpy_size);

    mesh.normals = std::move(attr);
  } else {
    // Packed1010102
    std::vector<uint32_t> packed;
    QuantizeNormals1010102(N, nN, &packed);

    VertexAttribute attr;
    attr.format = VertexAttributeFormat::Uint;
    size_t resize_size;
    if (!safe::n_to_size<uint32_t>(packed.size(), &resize_size)) {
      return false;
    }
    attr.data.resize(resize_size);
    size_t memcpy_size;
    if (!safe::n_to_size<uint32_t>(packed.size(), &memcpy_size)) {
      return false;
    }
    std::memcpy(attr.data.data(), packed.data(), memcpy_size);

    mesh.normals = std::move(attr);
  }

  mesh.normals.variability = var;
  mesh.normals.stride = 0;
  mesh.normals.elementSize = 1;
  mesh.normals.name = "normals";

  return true;
}

bool RenderSceneConverter::ConvertMesh(
    const RenderSceneConverterEnv &env, const Path &abs_prim_path,
    const GeomMesh &mesh, const MaterialPath &material_path,
    const std::map<std::string, MaterialPath> &subset_material_path_map,
    // const std::map<std::string, int64_t> &rmaterial_idMap,
    const StringAndIdMap &rmaterial_map,
    const std::vector<const tinyusdz::GeomSubset *> &material_subsets,
    const std::vector<std::pair<std::string, const tinyusdz::BlendShape *>>
        &blendshapes,
    RenderMesh *dstMesh) {
  //
  // Steps:
  //
  // 1. Get points, faceVertexIndices and faceVertexOffsets at specified time.
  //   - Validate GeomSubsets
  // 2. Assign Material and list up texcoord primvars
  // 3. convert texcoord, normals, vetexcolor(displaycolors)
  //   - First try to convert it to `vertex` varying(Can be drawn with single
  //   index buffer)
  //   - Otherwise convert to `facevarying` as the last resort.
  // 4. Triangulate indices  when `triangulate` is enabled.
  //   - Triangulate texcoord, normals, vertexcolor.
  // 5. Convert Skin weights
  // 6. Convert BlendShape
  // 7. Build indices(convert 'facevarying' to 'vertrex')
  // 8. Calcualte normals(if not present in the mesh)
  // 9. Build tangent frame(for normal mapping)
  //
  //

  if (!dstMesh) {
    PUSH_ERROR_AND_RETURN("`dst` mesh pointer is nullptr");
  }

  RenderMesh dst;

  dst.is_rightHanded =
      (mesh.orientation.get_value() == tinyusdz::Orientation::RightHanded);
  dst.doubleSided = mesh.doubleSided.get_value();

  //
  // 1. Mandatory attribute: points, faceVertexCounts and faceVertexIndices.
  //
  // TODO: Make error when Mesh's indices is empty?
  //

  // Prim path string for mmap zero-copy lookups
  const std::string prim_path_str = abs_prim_path.prim_part();

  {
    std::vector<value::point3f> points;
    bool got_points = TryReadMMapArray<value::point3f>(
        env.stage, prim_path_str, "points", &points);
    if (!got_points) {
      bool ret = EvaluateTypedAnimatableAttribute(
          env.stage, mesh.points, "points", &points, &_err, env.timecode,
          value::TimeSampleInterpolationType::Linear);
      if (!ret) {
        return false;
      }
    }

    // V2 safety: deferred array but both mmap and Stage reads produced empty
    if (points.empty() && env.stage.has_mmap_zero_copy()) {
      const MMapArrayRef *ref = env.stage.mmap_table()->find(
          prim_path_str, "points");
      if (ref && ref->element_count > 0) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "mmap deferred 'points' for {} ({} elements) could not be read. "
            "mmap buffer may be invalid.", abs_prim_path, ref->element_count));
      }
    }

    if (points.empty()) {

      // maybe points is explicitly authored, but empty.
      // point3f points = []

      dst.points.clear();
      //PUSH_ERROR_AND_RETURN(
      //    fmt::format("`points` is empty. Prim {}", abs_prim_path));

    } else {
      dst.points.resize(points.size());
      memcpy(dst.points.data(), points.data(),
             sizeof(value::float3) * points.size());

      std::vector<value::point3f>().swap(points);
    }

  }

  {
    std::vector<int32_t> indices;
    bool ret = EvaluateTypedAnimatableAttribute(
        env.stage, mesh.faceVertexIndices, "faceVertexIndices", &indices, &_err,
        env.timecode, value::TimeSampleInterpolationType::Held);
    if (!ret) {
      return false;
    }

    dst.usdFaceVertexIndices.reserve(indices.size());
    for (size_t i = 0; i < indices.size(); i++) {
      if (indices[i] < 0) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "faceVertexIndices[{}] contains negative index value {}.", i,
            indices[i]));
      }
      if (size_t(indices[i]) > dst.points.size()) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("faceVertexIndices[{}] {} exceeds points.size {}.", i,
                        indices[i], dst.points.size()));
      }
      dst.usdFaceVertexIndices.push_back(uint32_t(indices[i]));
    }
  }

  {
    std::vector<int> counts;
    bool ret = EvaluateTypedAnimatableAttribute(
        env.stage, mesh.faceVertexCounts, "faceVertexCounts", &counts, &_err,
        env.timecode, value::TimeSampleInterpolationType::Held);
    if (!ret) {
      return false;
    }

    size_t sumCounts = 0;
    dst.usdFaceVertexCounts.clear();
    dst.usdFaceVertexCounts.reserve(counts.size());
    for (size_t i = 0; i < counts.size(); i++) {
      if (counts[i] < 3) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("faceVertexCounts[{}] contains invalid value {}. The "
                        "count value must be >= 3",
                        i, counts[i]));
      }

      if ((sumCounts + size_t(counts[i])) > dst.usdFaceVertexIndices.size()) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "faceVertexCounts[{}] exceeds faceVertexIndices.size {}.", i,
            dst.usdFaceVertexIndices.size()));
      }
      dst.usdFaceVertexCounts.push_back(uint32_t(counts[i]));
      sumCounts += size_t(counts[i]);
    }
  }


  //
  // 2. bindMaterial GeoMesh and GeomSubset.
  //
  // Assume Material conversion is done before ConvertMesh.
  // Here we only assign RenderMaterial id and extract GeomSubset::indices
  // information.
  //

  DCOUT("rmaterial_ap.size " << rmaterial_map.size());
  if (auto it = rmaterial_map.find(material_path.material_path);
      it != rmaterial_map.s_end()) {
    dst.material_id = int(it->second);
  }

  if (auto it = rmaterial_map.find(material_path.backface_material_path);
      it != rmaterial_map.s_end()) {
    dst.backface_material_id = int(it->second);
  }

  if (env.mesh_config.validate_geomsubset) {
    size_t elementCount = dst.usdFaceVertexCounts.size();

    if (material_subsets.size()) {
      auto family_it =
          mesh.subsetFamilyTypeMap.find(value::token("materialBind"));
      if (family_it != mesh.subsetFamilyTypeMap.end()) {
        const GeomSubset::FamilyType familyType = family_it->second;
        std::string subset_err;
        if (!GeomSubset::ValidateSubsets(material_subsets, elementCount,
                                         familyType, &subset_err)) {
          // Warn but don't fail — many DCC tools (Blender, Maya) export
          // incomplete partitions where only some faces are assigned to
          // subsets.  pxrUSD also warns without failing here.
          // Unassigned faces inherit the parent mesh's material binding.
          PUSH_WARN("GeomSubset validation: " + subset_err);
        }
      }
    }
  }

  for (const auto &psubset : material_subsets) {
    MaterialSubset ms;
    ms.prim_name = psubset->name;
    // ms.prim_index = // TODO
    ms.abs_path = abs_prim_path.prim_part() + std::string("/") + psubset->name;
    ms.display_name = psubset->meta.has_displayName() ? psubset->meta.get_displayName() : "";

    // TODO: Raise error when indices is empty?
    if (psubset->indices.authored()) {
      std::vector<int> indices;  // index to faceVertexCounts
      bool ret = EvaluateTypedAnimatableAttribute(
          env.stage, psubset->indices, "indices", &indices, &_err, env.timecode,
          value::TimeSampleInterpolationType::Held);
      if (!ret) {
        return false;
      }

      ms.usdIndices = indices;
    }

    if (auto subset_it = subset_material_path_map.find(psubset->name);
        subset_it != subset_material_path_map.end()) {
      const auto &mp = subset_it->second;
      if (auto mat_it = rmaterial_map.find(mp.material_path);
          mat_it != rmaterial_map.s_end()) {
        ms.material_id = int(mat_it->second);
        DCOUT("MaterialSubset " << psubset->name << " : material_id "
                                << ms.material_id);
      }
      if (auto backface_it = rmaterial_map.find(mp.backface_material_path);
          backface_it != rmaterial_map.s_end()) {
        ms.backface_material_id = int(backface_it->second);
        DCOUT("MaterialSubset " << psubset->name << " : backface_material_id "
                                << ms.backface_material_id);
      }
    }

    // TODO: Ensure prim_name is unique.
    dst.material_subsetMap[ms.prim_name] = ms;
  }

  uint32_t num_vertices = uint32_t(dst.points.size());
  uint32_t num_faces = uint32_t(dst.usdFaceVertexCounts.size());
  uint32_t num_face_vertex_indices = uint32_t(dst.usdFaceVertexIndices.size());

  //
  // List up texcoords in this mesh.
  // - If no material assigned to this mesh, look into
  // `default_texcoords_primvar_name`
  // - If materials are assigned, find all corresponding UV primvars in this
  // mesh.
  //

  // key:slotId, value:texcoord data
  std::unordered_map<uint32_t, VertexAttribute> uvAttrs;

  // We need Material info to get corresponding primvar name.
  if (rmaterial_map.empty()) {
    // No material assigned to the Mesh, but we may still want texcoords solely(
    // assign material after the conversion)
    // So find a primvar whose name matches default texcoord name.
    if (mesh.has_primvar(env.mesh_config.default_texcoords_primvar_name)) {
      DCOUT("uv primvar  with default_texcoords_primvar_name found.");
      auto ret = GetTextureCoordinate(
          env.stage, mesh, env.mesh_config.default_texcoords_primvar_name,
          env.timecode, env.tinterp, prim_path_str);
      if (ret) {
        //TUSDZ_LOG_I("uv attr");

        // Use slotId 0 - use move to avoid copy
        uvAttrs[0] = std::move(ret.value());
      } else {
        PUSH_WARN("Failed to get texture coordinate for `"
                  << env.mesh_config.default_texcoords_primvar_name
                  << "` : " << ret.error());
      }
    }
  } else {
    for (auto mit = rmaterial_map.i_begin(); mit != rmaterial_map.i_end();
         mit++) {
      int64_t rmaterial_id = int64_t(mit->first);

      if ((rmaterial_id > -1) && (size_t(rmaterial_id) < materials.size())) {
        const RenderMaterial &material = materials[size_t(rmaterial_id)];

        // Cache ListUVNames per material_id to avoid redundant shader walks
        auto uv_cache_it = _uvNameCache.find(rmaterial_id);
        if (uv_cache_it == _uvNameCache.end()) {
          StringAndIdMap tmp;
          if (!ListUVNames(material, textures, tmp)) {
            DCOUT("Failed to list UV names");
            return false;
          }
          uv_cache_it = _uvNameCache.emplace(rmaterial_id, std::move(tmp)).first;
        }
        const StringAndIdMap &uvname_map = uv_cache_it->second;

        for (auto it = uvname_map.i_begin(); it != uvname_map.i_end(); it++) {
          uint64_t slotId = it->first;
          std::string uvname = it->second;

          if (uvAttrs.find(uint32_t(slotId)) == uvAttrs.end()) {
            // FIXME: Use GetGeomPrimvar() & ToVertexAttribute()
            auto ret = GetTextureCoordinate(env.stage, mesh, uvname,
                                            env.timecode, env.tinterp,
                                            prim_path_str);
            if (ret) {
              VertexAttribute &vattr = ret.value();

              if (vattr.is_vertex()) {
                if (vattr.vertex_count() != num_vertices) {
                  PUSH_ERROR_AND_RETURN(fmt::format("Array length of texture coordinate `{}`(Prim path {}) must be {}, but got {}", uvname, abs_prim_path.prim_part(), num_vertices, vattr.vertex_count()));
                }
              } else if (vattr.is_constant()) {
                if (vattr.vertex_count() != 1) {
                  PUSH_ERROR_AND_RETURN(fmt::format("Array length of texture coordinate `{}`(Prim path {}) must be {}, but got {}", uvname, abs_prim_path.prim_part(), 1, vattr.vertex_count()));
                }
              } else if (vattr.is_uniform()) {
                if (vattr.vertex_count() != num_faces) {
                  PUSH_ERROR_AND_RETURN(fmt::format("Array length of texture coordinate `{}`(Prim path {}) must be {}, but got {}", uvname, abs_prim_path.prim_part(), num_faces, vattr.vertex_count()));
                }
              } else if (vattr.is_facevarying()) {
                if (vattr.vertex_count() != num_face_vertex_indices) {
                  PUSH_ERROR_AND_RETURN(fmt::format("Array length of texture coordinate `{}`(Prim path {}) must be {}, but got {}", uvname, abs_prim_path.prim_part(), num_face_vertex_indices, vattr.vertex_count()));
                }
              } else {
                PUSH_ERROR_AND_RETURN("Internal error. Unknown variability of texcoord attribute.");
                return false;
              }

              // Use move to avoid copy
              uvAttrs.emplace(uint32_t(slotId), std::move(vattr));
            } else {
              PUSH_WARN("Failed to get texture coordinate for `"
                        << uvname << "` : " << ret.error());
            }
          }
        }
      }
    }

    // Fallback: If no UV names were found via shader connections (e.g.
    // MaterialX materials without explicit texture nodes), try the default
    // texcoord primvar (usually "st") — similar to OpenUSD's implicit
    // defaultgeomprop="UV0" -> primvars:st mapping.
    if (uvAttrs.empty() &&
        mesh.has_primvar(env.mesh_config.default_texcoords_primvar_name)) {
      DCOUT("No UV names from material shader connections. "
            "Falling back to default texcoord primvar `"
            << env.mesh_config.default_texcoords_primvar_name << "`.");
      auto ret = GetTextureCoordinate(
          env.stage, mesh, env.mesh_config.default_texcoords_primvar_name,
          env.timecode, env.tinterp, prim_path_str);
      if (ret) {
        uvAttrs[0] = std::move(ret.value());
      } else {
        PUSH_WARN("Failed to get default texture coordinate `"
                  << env.mesh_config.default_texcoords_primvar_name
                  << "` : " << ret.error());
      }
    }
  }

  //TUSDZ_LOG_I("done uvAttr");

  if (mesh.has_primvar(env.mesh_config.default_tangents_primvar_name)) {
    GeomPrimvar pvar;

    if (!GetGeomPrimvar(env.stage, &mesh,
                        env.mesh_config.default_tangents_primvar_name, &pvar,
                        &_err)) {
      return false;
    }

    std::string warn_msg;
    if (!ToVertexAttribute(pvar, env.mesh_config.default_tangents_primvar_name,
                           num_vertices, num_faces, num_face_vertex_indices,
                           dst.tangents, &_err, env.timecode, env.tinterp,
                           &warn_msg)) {
      return false;
    }
    if (!warn_msg.empty()) {
      _warn += warn_msg;
    }
  }

  if (mesh.has_primvar(env.mesh_config.default_binormals_primvar_name)) {
    GeomPrimvar pvar;

    if (!GetGeomPrimvar(env.stage, &mesh,
                        env.mesh_config.default_binormals_primvar_name, &pvar,
                        &_err)) {
      return false;
    }

    std::string warn_msg;
    if (!ToVertexAttribute(pvar, env.mesh_config.default_binormals_primvar_name,
                           num_vertices, num_faces, num_face_vertex_indices,
                           dst.binormals, &_err, env.timecode, env.tinterp,
                           &warn_msg)) {
      return false;
    }
    if (!warn_msg.empty()) {
      _warn += warn_msg;
    }
  }

  constexpr auto kDisplayColor = "displayColor";
  if (mesh.has_primvar(kDisplayColor)) {
    GeomPrimvar pvar;

    if (!GetGeomPrimvar(env.stage, &mesh, kDisplayColor, &pvar, &_err)) {
      return false;
    }

    VertexAttribute vcolor;
    std::string warn_msg;
    if (!ToVertexAttribute(pvar, kDisplayColor, num_vertices, num_faces,
                           num_face_vertex_indices, vcolor, &_err, env.timecode,
                           env.tinterp, &warn_msg)) {
      return false;
    }
    if (!warn_msg.empty()) {
      _warn += warn_msg;
    }

    if ((vcolor.elementSize == 1) && (vcolor.vertex_count() == 1) &&
        (vcolor.stride_bytes() == 3 * 4)) {
      memcpy(&dst.displayColor, vcolor.data.data(), vcolor.stride_bytes());
    } else {
      dst.vertex_colors = vcolor;
    }
  }

  constexpr auto kDisplayOpacity = "displayOpacity";
  if (mesh.has_primvar(kDisplayOpacity)) {
    GeomPrimvar pvar;
    if (!GetGeomPrimvar(env.stage, &mesh, kDisplayOpacity, &pvar, &_err)) {
      return false;
    }

    VertexAttribute vopacity;
    std::string warn_msg;
    if (!ToVertexAttribute(pvar, kDisplayOpacity, num_vertices, num_faces,
                           num_face_vertex_indices, vopacity, &_err,
                           env.timecode, env.tinterp, &warn_msg)) {
      return false;
    }
    if (!warn_msg.empty()) {
      _warn += warn_msg;
    }

    if ((vopacity.elementSize == 1) && (vopacity.vertex_count() == 1) &&
        (vopacity.stride_bytes() == 4)) {
      memcpy(&dst.displayOpacity, vopacity.data.data(),
             vopacity.stride_bytes());
    } else {
      dst.vertex_opacities = vopacity;
    }
  }



  //
  // Check if the Mesh can be drawn with single index buffer during converting
  // normals/texcoords/displayColors/displayOpacities, since OpenGL and Vulkan
  // does not support drawing a primitive with multiple index buffers.
  //
  // If the Mesh contains any face-varying attribute,
  // First try to convert it 'vertex' variabily, if it fails, all attribute are
  // converted to face-varying so that the Mesh can be drawn without index
  // buffer. This will hurt the performance of rendering in OpenGL/Vulkan,
  // especially when the Mesh is animated with skinning.
  //
  // We leave user-defined primvar as-is, so no check for it.
  //
  bool is_single_indexable{true};

  //
  // Convert normals
  //
  {
    Interpolation interp = mesh.get_normalsInterpolation();
    std::vector<value::normal3f> normals;

    if (mesh.has_primvar("normals")) {  // primvars:normals
      GeomPrimvar pvar;
      if (!GetGeomPrimvar(env.stage, &mesh, "normals", &pvar, &_err)) {
        return false;
      }

      // Check if normals primvar has timesamples
      const tinyusdz::Attribute &normals_attr = pvar.get_attribute();
      if (normals_attr.has_timesamples()) {
        std::string msg = fmt::format(
            "Geometry primvar 'normals' has timesamples (animated values). "
            "RenderMesh conversion uses value at specified time (timecode={}). "
            "To capture animation, you need to convert at multiple timesamples. "
            "Consider using ConvertMesh() at each timeframe or implementing "
            "per-frame conversion. Animated normals are particularly important "
            "for correct shading and normal mapping.",
            env.timecode);
        _warn += msg + "\n";
        DCOUT("WARN: " << msg);
      }

      // V2: try mmap path for primvar normals (handles indexed + non-indexed)
      bool got_normals_pv = false;
      if (pvar.has_indices()) {
        got_normals_pv = TryReadMMapArrayWithIndices<value::normal3f>(
            env.stage, prim_path_str, "primvars:normals", pvar,
            env.timecode, &normals);
      } else {
        got_normals_pv = TryReadMMapArray<value::normal3f>(
            env.stage, prim_path_str, "primvars:normals", &normals);
      }
      if (!got_normals_pv) {
        if (!pvar.flatten_with_indices(env.timecode, &normals, env.tinterp,
                                       &_err)) {
          PUSH_ERROR_AND_RETURN("Failed to expand `normals` primvar.");
        }
      }

    } else if (mesh.normals.authored()) {  // look 'normals'
      // Try mmap zero-copy for direct normals attribute
      bool got_normals = TryReadMMapArray<value::normal3f>(
          env.stage, prim_path_str, "normals", &normals);
      if (!got_normals) {
        if (!EvaluateTypedAnimatableAttribute(env.stage, mesh.normals, "normals",
                                              &normals, &_err, env.timecode,
                                              env.tinterp)) {
        }
      }
      // V2 safety: deferred array but both mmap and Stage reads produced empty
      if (normals.empty() && env.stage.has_mmap_zero_copy()) {
        const MMapArrayRef *ref = env.stage.mmap_table()->find(
            prim_path_str, "normals");
        if (ref && ref->element_count > 0) {
          PUSH_ERROR_AND_RETURN(fmt::format(
              "mmap deferred 'normals' for {} ({} elements) could not be read. "
              "mmap buffer may be invalid.", abs_prim_path, ref->element_count));
        }
      }
    }

    size_t resize_size;
    if (!safe::n_to_size<value::normal3f>(normals.size(), &resize_size)) {
      return nonstd::make_unexpected("Integer overflow: normals.size() * sizeof(value::normal3f)");
    }
    dst.normals.get_data().resize(resize_size);
    size_t memcpy_size;
    if (!safe::n_to_size<value::normal3f>(normals.size(), &memcpy_size)) {
      return nonstd::make_unexpected("Integer overflow in memcpy");
    }
    memcpy(dst.normals.get_data().data(), normals.data(), memcpy_size);
    dst.normals.elementSize = 1;
    dst.normals.stride = sizeof(value::normal3f);
    dst.normals.format = VertexAttributeFormat::Vec3;

    if (interp == Interpolation::Varying) {
      dst.normals.variability = VertexVariability::Varying;
    } else if (interp == Interpolation::Constant) {
      dst.normals.variability = VertexVariability::Constant;
    } else if (interp == Interpolation::Uniform) {
      dst.normals.variability = VertexVariability::Uniform;
    } else if (interp == Interpolation::Vertex) {
      dst.normals.variability = VertexVariability::Vertex;
    } else if (interp == Interpolation::FaceVarying) {
      dst.normals.variability = VertexVariability::FaceVarying;
    } else {
      PUSH_ERROR_AND_RETURN(
          "[Internal Error] Invalid interpolation value for normals.");
    }
    dst.normals.indices.clear();
    dst.normals.name = "normals";

    if (is_single_indexable &&
        (dst.normals.variability == VertexVariability::FaceVarying)) {
      VertexAttribute va_normals;
      if (TryConvertFacevaryingToVertex(
              dst.normals, &va_normals, dst.usdFaceVertexIndices, &_warn,
              env.mesh_config.facevarying_to_vertex_eps)) {
        DCOUT("normals is converted to 'vertex' varying.");
        dst.normals = std::move(va_normals);
      } else {
        // Exact-eps dedup failed.  Try quantized dedup: normals that agree
        // at 10-bit SNORM resolution (~0.1°) are treated as identical.
        // This catches subdivision surface limit normals where the same
        // logical normal differs by floating-point noise across face-vertices.
        if (TryQuantizedNormalDedup(dst.normals, dst.usdFaceVertexIndices)) {
          DCOUT("normals converted to 'vertex' via quantized dedup.");
        } else {
          DCOUT(
              "normals cannot be converted to 'vertex' varying. Staying "
              "'facevarying'");
          DCOUT("warn = " << _warn);
          is_single_indexable = false;
        }
      }
    }
  }

  //
  // Convert UVs
  //

  for (auto &it : uvAttrs) {
    uint64_t slotId = it.first;
    VertexAttribute &vattr = it.second;

    if (vattr.format != VertexAttributeFormat::Vec2) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Texcoord VertexAttribute must be Vec2 type.\n"));
    }

    if (vattr.element_size() != 1) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("elementSize must be 1 for Texcoord attribute."));
    }

    DCOUT("Add texcoord attr `" << vattr.name << "` to slot Id " << slotId);

    if (is_single_indexable &&
        (vattr.variability == VertexVariability::FaceVarying)) {
      VertexAttribute va_uvs;
      if (TryConvertFacevaryingToVertex(
              vattr, &va_uvs, dst.usdFaceVertexIndices, &_warn,
              env.mesh_config.facevarying_to_vertex_eps)) {
        DCOUT("texcoord[" << slotId << "] is converted to 'vertex' varying.");
        dst.texcoords[uint32_t(slotId)] = std::move(va_uvs);
      } else {
        DCOUT("texcoord[" << slotId
                          << "] cannot be converted to 'vertex' varying. "
                             "Staying 'facevarying'");
        is_single_indexable = false;
        dst.texcoords[uint32_t(slotId)] = std::move(vattr);
      }
    } else {
      dst.texcoords[uint32_t(slotId)] = std::move(vattr);
    }
  }

  if (dst.vertex_colors.vertex_count() > 1) {
    // Use const reference instead of copy for validation
    const VertexAttribute &vattr = dst.vertex_colors;

    if (vattr.format != VertexAttributeFormat::Vec3) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Color VertexAttribute must be Vec3 type.\n"));
    }

    if (vattr.element_size() != 1) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("elementSize = 1 expected for VertexColor, but got {}",
                      vattr.element_size()));
    }

    if (is_single_indexable &&
        (vattr.variability == VertexVariability::FaceVarying)) {
      VertexAttribute va;
      if (TryConvertFacevaryingToVertex(
              dst.vertex_colors, &va, dst.usdFaceVertexIndices, &_warn,
              env.mesh_config.facevarying_to_vertex_eps)) {
        dst.vertex_colors = std::move(va);
      } else {
        DCOUT(
            "vertex_colors cannot be converted to 'vertex' varying. Staying "
            "'facevarying'");
        is_single_indexable = false;
      }
    }
  }

  if (dst.vertex_opacities.vertex_count() > 1) {
    // Use const reference instead of copy for validation
    const VertexAttribute &vattr = dst.vertex_opacities;

    if (vattr.format != VertexAttributeFormat::Float) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Opacity VertexAttribute must be Float type.\n"));
    }

    if (vattr.element_size() != 1) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("elementSize = 1 expected for VertexOpacity, but got {}",
                      vattr.element_size()));
    }

    if (is_single_indexable &&
        (vattr.variability == VertexVariability::FaceVarying)) {
      VertexAttribute va;
      if (TryConvertFacevaryingToVertex(
              dst.vertex_opacities, &va, dst.usdFaceVertexIndices, &_warn,
              env.mesh_config.facevarying_to_vertex_eps)) {
        dst.vertex_opacities = std::move(va);
      } else {
        DCOUT(
            "vertex_opacities cannot be converted to 'vertex' varying. Staying "
            "'facevarying'");
        is_single_indexable = false;
      }
    }
  }

  DCOUT(mesh.name << " : is_single_indexable = " << is_single_indexable);

  //
  // Convert built-in vertex attributes to either 'vertex' or 'facevarying'
  //
  {
    if (!ConvertVertexVariabilityImpl(dst.normals, is_single_indexable,
                                      dst.usdFaceVertexCounts,
                                      dst.usdFaceVertexIndices)) {
      return false;
    }
    for (auto &it : dst.texcoords) {
      if (!ConvertVertexVariabilityImpl(it.second, is_single_indexable,
                                        dst.usdFaceVertexCounts,
                                        dst.usdFaceVertexIndices)) {
        return false;
      }
    }

    if (!ConvertVertexVariabilityImpl(dst.vertex_colors, is_single_indexable,
                                      dst.usdFaceVertexCounts,
                                      dst.usdFaceVertexIndices)) {
      return false;
    }
    if (!ConvertVertexVariabilityImpl(dst.vertex_opacities, is_single_indexable,
                                      dst.usdFaceVertexCounts,
                                      dst.usdFaceVertexIndices)) {
      return false;
    }
  }


  ///
  /// 4. Triangulate
  ///  - triangulate faceVertexCounts, faceVertexIndices
  ///  - Remap faceIndex in MaterialSubset(GeomSubset).
  ///  - Triangulate vertex attributes(normals, uvcoords, vertex
  ///  colors/opacities).
  ///
  bool triangulate = env.mesh_config.triangulate;
  if (triangulate) {
    DCOUT("Triangulate mesh");
    std::vector<uint32_t> triangulatedFaceVertexCounts;  // should be all 3's
    std::vector<uint32_t> triangulatedFaceVertexIndices;
    std::vector<uint32_t>
        triangulatedToOrigFaceVertexIndexMap;  // used for rearrange facevertex
                                               // attrib
    std::vector<uint32_t>
        triangulatedFaceCounts;  // used for rearrange face indices(e.g
                                 // GeomSubset indices)

    std::string err;

    if (!TriangulatePolygon<value::float3, float>(
            dst.points, dst.usdFaceVertexCounts, dst.usdFaceVertexIndices,
            triangulatedFaceVertexCounts, triangulatedFaceVertexIndices,
            triangulatedToOrigFaceVertexIndexMap, triangulatedFaceCounts,
            env.mesh_config.triangulation_method,
            _warn, err)) {
      PUSH_ERROR_AND_RETURN("Triangulation failed: " + err);
    }

    if (dst.material_subsetMap.size()) {
      // Remap faceId in GeomSubsets

      //
      // size: len(triangulatedFaceCounts)
      // value: array index in triangulatedFaceVertexCounts
      // Up to 4GB faces.
      //
      std::vector<uint32_t> faceIndexOffsets;
      faceIndexOffsets.resize(triangulatedFaceCounts.size());

      size_t faceIndexOffset = 0;
      for (size_t i = 0; i < triangulatedFaceCounts.size(); i++) {
        size_t ncount = triangulatedFaceCounts[i];

        faceIndexOffsets[i] = uint32_t(faceIndexOffset);
        // DCOUT("faceIndexOffset[" << i << "] = " << faceIndexOffsets[i]);

        faceIndexOffset += ncount;

        if (faceIndexOffset >= (std::numeric_limits<uint32_t>::max)()) {
          PUSH_ERROR_AND_RETURN("Triangulated Mesh contains 4G or more faces.");
        }
      }

      // Remap indices in MaterialSubset
      //
      // example:
      //
      // faceVertexCounts = [4, 4]
      // faceVertexIndices = [0, 1, 2, 3, 4, 5, 6, 7]
      //
      // triangulatedFaceVertexCounts = [3, 3, 3, 3]
      // triangulatedFaceVertexIndices = [0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7]
      // triangulatedFaceCounts = [2, 2]
      //
      // geomsubset.indices = [0, 1] # index to faceVertexCounts
      // faceIndexOffsets = [0, 2]
      //
      // => triangulated geomsubset.indices = [0, 1, 2, 3] # index to
      // triangulatedFaceVertexCounts
      //
      //
      for (auto &it : dst.material_subsetMap) {
        std::vector<int> triangulated_indices;

        for (size_t i = 0; i < it.second.usdIndices.size(); i++) {
          int32_t srcIndex = it.second.usdIndices[i];
          if (srcIndex < 0 || size_t(srcIndex) >= faceIndexOffsets.size()) {
            PUSH_ERROR_AND_RETURN(fmt::format(
                "GeomSubset '{}': index {} out of range [0, {}).",
                it.first, srcIndex, faceIndexOffsets.size()));
          }

          uint32_t baseFaceIndex = faceIndexOffsets[size_t(srcIndex)];
          // DCOUT(i << ", baseFaceIndex = " << baseFaceIndex);

          for (size_t k = 0; k < triangulatedFaceCounts[uint32_t(srcIndex)];
               k++) {
            if ((baseFaceIndex + k) > size_t((std::numeric_limits<int32_t>::max)())) {
              PUSH_ERROR_AND_RETURN(fmt::format("Index value exceeds 2GB."));
            }
            // assume triangulated faceIndex in each polygon is monotonically
            // increasing.
            triangulated_indices.push_back(int(baseFaceIndex + k));
          }
        }

        it.second.triangulatedIndices = std::move(triangulated_indices);
      }
    }

    //
    // Triangulate built-in vertex attributes.
    //
    {
      if (!TriangulateVertexAttribute(dst.normals, dst.usdFaceVertexCounts,
                                      triangulatedToOrigFaceVertexIndexMap,
                                      triangulatedFaceCounts,
                                      triangulatedFaceVertexIndices, &_err)) {
        PUSH_ERROR_AND_RETURN("Failed to triangulate normals attribute.");
      }

      if (!TriangulateVertexAttribute(dst.tangents, dst.usdFaceVertexCounts,
                                      triangulatedToOrigFaceVertexIndexMap,
                                      triangulatedFaceCounts,
                                      triangulatedFaceVertexIndices, &_err)) {
        PUSH_ERROR_AND_RETURN("Failed to triangulate tangents attribute.");
      }

      if (!TriangulateVertexAttribute(dst.binormals, dst.usdFaceVertexCounts,
                                      triangulatedToOrigFaceVertexIndexMap,
                                      triangulatedFaceCounts,
                                      triangulatedFaceVertexIndices, &_err)) {
        PUSH_ERROR_AND_RETURN("Failed to triangulate binormals attribute.");
      }

      for (auto &it : dst.texcoords) {
        if (!TriangulateVertexAttribute(it.second, dst.usdFaceVertexCounts,
                                        triangulatedToOrigFaceVertexIndexMap,
                                        triangulatedFaceCounts,
                                        triangulatedFaceVertexIndices, &_err)) {
          PUSH_ERROR_AND_RETURN(fmt::format(
              "Failed to triangulate texcoords[{}] attribute.", it.first));
        }
      }

      if (!TriangulateVertexAttribute(
              dst.vertex_colors, dst.usdFaceVertexCounts,
              triangulatedToOrigFaceVertexIndexMap, triangulatedFaceCounts,
              triangulatedFaceVertexIndices, &_err)) {
        PUSH_ERROR_AND_RETURN("Failed to triangulate vertex_colors attribute.");
      }

      if (!TriangulateVertexAttribute(
              dst.vertex_opacities, dst.usdFaceVertexCounts,
              triangulatedToOrigFaceVertexIndexMap, triangulatedFaceCounts,
              triangulatedFaceVertexIndices, &_err)) {
        PUSH_ERROR_AND_RETURN(
            "Failed to triangulate vertopacitiesex_colors attribute.");
      }
    }

    dst.triangulatedFaceVertexCounts = std::move(triangulatedFaceVertexCounts);
    dst.triangulatedFaceVertexIndices =
        std::move(triangulatedFaceVertexIndices);

    dst.triangulatedToOrigFaceVertexIndexMap =
        std::move(triangulatedToOrigFaceVertexIndexMap);
    dst.triangulatedFaceCounts = std::move(triangulatedFaceCounts);
  }

  // Free triangulation intermediates — only needed during attribute
  // triangulation above. Safe to free unconditionally since nothing
  // downstream reads these vectors.
  { std::vector<uint32_t> tmp; dst.triangulatedToOrigFaceVertexIndexMap.swap(tmp); }
  { std::vector<uint32_t> tmp; dst.triangulatedFaceCounts.swap(tmp); }

  // Free pre-triangulation topology under lowmem guard.
  // faceVertexIndices()/faceVertexCounts() accessors return the triangulated
  // versions when is_triangulated() is true.
  if (env.mesh_config.lowmem && dst.is_triangulated()) {
    { std::vector<uint32_t> tmp; dst.usdFaceVertexCounts.swap(tmp); }
    { std::vector<uint32_t> tmp; dst.usdFaceVertexIndices.swap(tmp); }
  }

  //
  // 5. Vertex skin weights(jointIndex and jointWeights)
  //
  if (mesh.has_primvar("skel:jointIndices") &&
      mesh.has_primvar("skel:jointWeights")) {
    DCOUT("Convert skin weights");
    GeomPrimvar jointIndices;
    GeomPrimvar jointWeights;

    if (!GetGeomPrimvar(env.stage, &mesh, "skel:jointIndices", &jointIndices,
                        &_err)) {
      return false;
    }

    if (!GetGeomPrimvar(env.stage, &mesh, "skel:jointWeights", &jointWeights,
                        &_err)) {
      return false;
    }

    // interpolation must be 'vertex'
    if (!jointIndices.has_interpolation()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("`skel:jointIndices` primvar must author `interpolation` "
                      "metadata(and set it to `vertex`)"));
    }

    // TODO: Disallow Varying?
    if ((jointIndices.get_interpolation() != Interpolation::Vertex) &&
        (jointIndices.get_interpolation() != Interpolation::Varying)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("`skel:jointIndices` primvar must use `vertex` for "
                      "`interpolation` metadata, but got `{}`.",
                      to_string(jointIndices.get_interpolation())));
    }

    if (!jointWeights.has_interpolation()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("`skel:jointWeights` primvar must author `interpolation` "
                      "metadata(and set it to `vertex`)"));
    }

    // TODO: Disallow Varying?
    if ((jointWeights.get_interpolation() != Interpolation::Vertex) &&
        (jointWeights.get_interpolation() != Interpolation::Varying)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("`skel:jointWeights` primvar must use `vertex` for "
                      "`interpolation` metadata, but got `{}`.",
                      to_string(jointWeights.get_interpolation())));
    }

    uint32_t jointIndicesElementSize = jointIndices.get_elementSize();
    uint32_t jointWeightsElementSize = jointWeights.get_elementSize();

    if (jointIndicesElementSize == 0) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "`elementSize` metadata of `skel:jointIndices` is zero."));
    }

    if (jointWeightsElementSize == 0) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "`elementSize` metadata of `skel:jointWeights` is zero."));
    }

    if (jointIndicesElementSize > env.mesh_config.max_skin_elementSize) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "`elementSize` {} of `skel:jointIndices` too large. Max allowed is "
          "set to {}",
          jointIndicesElementSize, env.mesh_config.max_skin_elementSize));
    }

    if (jointWeightsElementSize > env.mesh_config.max_skin_elementSize) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "`elementSize` {} of `skel:jointWeights` too large. Max allowed is "
          "set to {}",
          jointWeightsElementSize, env.mesh_config.max_skin_elementSize));
    }

    if (jointIndicesElementSize != jointWeightsElementSize) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("`elementSize` {} of `skel:jointIndices` must equal to "
                      "`elementSize` {} of `skel:jointWeights`",
                      jointIndicesElementSize, jointWeightsElementSize));
    }

    std::vector<int> jointIndicesArray;
    if (!jointIndices.flatten_with_indices(env.timecode, &jointIndicesArray,
                                           env.tinterp)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Failed to flatten Indexed Primvar `skel:jointIndices`. "
                      "Ensure `skel:jointIndices` is type `int[]`"));
    }

    std::vector<float> jointWeightsArray;
    // V2: try mmap path for jointWeights (float array, may be deferred)
    bool got_jw = false;
    if (jointWeights.has_indices()) {
      got_jw = TryReadMMapArrayWithIndices<float>(
          env.stage, prim_path_str, "primvars:skel:jointWeights", jointWeights,
          env.timecode, &jointWeightsArray);
    } else {
      got_jw = TryReadMMapArray<float>(
          env.stage, prim_path_str, "primvars:skel:jointWeights",
          &jointWeightsArray);
    }
    if (!got_jw) {
      if (!jointWeights.flatten_with_indices(env.timecode, &jointWeightsArray,
                                             env.tinterp)) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Failed to flatten Indexed Primvar `skel:jointWeights`. "
                        "Ensure `skel:jointWeights` is type `float[]`"));
      }
    }

    if (jointIndicesArray.size() != jointWeightsArray.size()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("`skel:jointIndices` nitems {} must be equal to "
                      "`skel:jointWeights` ntems {}",
                      jointIndicesArray.size(), jointWeightsArray.size()));
    }

    if (jointIndicesArray.empty()) {
      PUSH_ERROR_AND_RETURN(fmt::format("`skel:jointIndices` is empty array."));
    }

    // TODO: Validate jointIndex.

    dst.joint_and_weights.jointIndices = jointIndicesArray;
    dst.joint_and_weights.jointWeights = jointWeightsArray;
    dst.joint_and_weights.elementSize = int(jointIndicesElementSize);

    // Helper lambda to round up bone count to standard GPU skinning values
    auto roundBoneCountUp = [](uint32_t count) -> uint32_t {
      // Standard GPU skinning bone counts: 4, 8, 16, 32, 48, 64, 80, 96, 128
      const uint32_t standardCounts[] = {4, 8, 16, 32, 48, 64, 80, 96, 128};
      for (uint32_t stdCount : standardCounts) {
        if (count <= stdCount) {
          return stdCount;
        }
      }
      return 128; // Max supported
    };

    // Skeleton binding: first try explicit relationship, then fallback to ancestor discovery
    {
      Path skelPath;
      bool hasSkelPath = false;

      // First try explicit skel:skeleton relationship
      if (mesh.skeleton.has_value()) {
        DCOUT("Convert Skeleton (explicit relationship)");

        if (mesh.skeleton.value().is_path()) {
          skelPath = mesh.skeleton.value().targetPath;
          hasSkelPath = true;
        } else if (mesh.skeleton.value().is_pathvector()) {
          const size_t target_count = mesh.skeleton.value().targetPathVector.size();
          if (target_count == 1) {
            skelPath = mesh.skeleton.value().targetPathVector[0];
            hasSkelPath = true;
          } else {
            PUSH_ERROR_AND_RETURN(fmt::format(
                "`skel:skeleton` must have exactly one target for {}, but got {}.",
                abs_prim_path.full_path_name(), target_count));
          }
        } else {
          PUSH_ERROR_AND_RETURN(fmt::format(
              "`skel:skeleton` has invalid definition for {}.",
              abs_prim_path.full_path_name()));
        }
      }

      // Fallback: find skeleton by ancestor if mesh has skinning data but no explicit binding
      const Skeleton *discoveredSkelPtr{nullptr};
      if (!hasSkelPath && !dst.joint_and_weights.jointIndices.empty()) {
        DCOUT("Mesh has skinning data but no skel:skeleton - trying ancestor discovery");
        if (_allSkeletons && _allSkelRoots) {
          Path meshPath(abs_prim_path.full_path_name(), "");
          if (SkelRootSkeletonResolver::FindByAncestor(
                  meshPath, *_allSkeletons, *_allSkelRoots,
                  &_skelRootToSkeleton, &skelPath, &discoveredSkelPtr)) {
            hasSkelPath = true;
            DCOUT("Found skeleton by ancestor: " << skelPath.prim_part());
          } else {
            PUSH_WARN(
                "Mesh has skinning data but no skeleton bound: " +
                abs_prim_path.full_path_name() +
                ". Skinning data is preserved for later skeleton attachment.");
          }
        }
      }

      if (hasSkelPath && skelPath.is_valid()) {
        // Check if skeleton already exists via O(1) hash lookup
        std::string skelPathStr = skelPath.prim_part();
        auto skel_cache_it = _skelPathToIndex.find(skelPathStr);

        if (skel_cache_it != _skelPathToIndex.end()) {
          // Skeleton already converted, reuse it
          dst.skel_id = skel_cache_it->second;
        } else {
          int32_t skel_id = int32_t(skeletons.size());

          SkelHierarchy skel;

          // Use ConvertSkeletonFromPtr if we have the skeleton pointer from discovery,
          // otherwise use ConvertSkeletonImplWithPath for explicit relationship case
          if (discoveredSkelPtr) {
            // Extract prim name from path (last component)
            std::string primName = skelPath.prim_part();
            size_t lastSlash = primName.rfind('/');
            if (lastSlash != std::string::npos) {
              primName = primName.substr(lastSlash + 1);
            }
            if (!ConvertSkeletonFromPtr(env, skelPath, *discoveredSkelPtr, primName, &skel)) {
              return false;
            }
          } else {
            if (!ConvertSkeletonImplWithPath(env, skelPath, &skel)) {
              return false;
            }
          }
          DCOUT("Converted skeleton attached to : " << abs_prim_path);

          _skelPathToIndex[skelPathStr] = skel_id;
          skeletons.emplace_back(std::move(skel));
          DCOUT("add skeleton\n");

          dst.skel_id = skel_id;
        }

      }
    }

    // Apply bone reduction if enabled (after skeleton binding so hierarchy info is available)
    if (env.mesh_config.enable_bone_reduction &&
        (env.mesh_config.target_bone_count < jointIndicesElementSize)) {
      uint32_t numVertices = uint32_t(jointIndicesArray.size() / jointIndicesElementSize);

      DCOUT("Reducing bone influences from " << jointIndicesElementSize
            << " to " << env.mesh_config.target_bone_count
            << " per vertex (" << numVertices << " vertices)");

      // Configure bone reduction with advanced settings
      BoneReductionConfig bone_config;
      bone_config.target_bone_count = env.mesh_config.target_bone_count;
      bone_config.strategy = BoneReductionStrategy::ErrorMetric;
      bone_config.min_weight_threshold = 0.001f;
      bone_config.error_tolerance = 0.5f;
      bone_config.normalize_weights = true;

      // Use pre-computed flat topology from SkelHierarchy if available
      BoneHierarchyInfo hierarchy_storage;
      BoneHierarchyInfo *hierarchy_info = nullptr;

      if (dst.skel_id >= 0 && dst.skel_id < int(skeletons.size())) {
        const auto &skelH = skeletons[size_t(dst.skel_id)];
        if (!skelH.parent_joint_indices.empty()) {
          hierarchy_storage.parent_indices = skelH.parent_joint_indices;
          hierarchy_info = &hierarchy_storage;
        }
      }

      BoneReductionStats reduction_stats;

      if (!ReduceBoneInfluences(
              dst.joint_and_weights.jointIndices,
              dst.joint_and_weights.jointWeights,
              jointIndicesElementSize,
              numVertices,
              bone_config,
              hierarchy_info,
              &reduction_stats)) {
        PushInfo("Bone reduction failed, using original bone influences.");
      } else {
        // Update elementSize to reflect reduced bone count
        dst.joint_and_weights.elementSize = int(env.mesh_config.target_bone_count);
        DCOUT("Bone reduction complete. New elementSize: " << dst.joint_and_weights.elementSize);
        DCOUT("  Modified vertices: " << reduction_stats.num_vertices_modified << " / " << numVertices);
        DCOUT("  Avg weight error: " << reduction_stats.avg_weight_error);
        DCOUT("  Max weight error: " << reduction_stats.max_weight_error);
      }
    }
    // Round bone count without reduction (pad with zeros)
    else if (env.mesh_config.round_bone_count && !env.mesh_config.enable_bone_reduction) {
      uint32_t currentElementSize = jointIndicesElementSize;
      uint32_t roundedElementSize = roundBoneCountUp(currentElementSize);

      if (roundedElementSize > currentElementSize) {
        uint32_t numVertices = uint32_t(jointIndicesArray.size() / jointIndicesElementSize);

        DCOUT("Rounding bone count from " << currentElementSize
              << " to " << roundedElementSize
              << " per vertex (" << numVertices << " vertices)");

        // Create new arrays with padded size
        std::vector<int32_t> paddedIndices(numVertices * roundedElementSize, 0);
        std::vector<float> paddedWeights(numVertices * roundedElementSize, 0.0f);

        // Copy existing data and pad with zeros
        for (uint32_t v = 0; v < numVertices; v++) {
          for (uint32_t j = 0; j < currentElementSize; j++) {
            uint32_t srcIdx = v * currentElementSize + j;
            uint32_t dstIdx = v * roundedElementSize + j;
            paddedIndices[dstIdx] = dst.joint_and_weights.jointIndices[srcIdx];
            paddedWeights[dstIdx] = dst.joint_and_weights.jointWeights[srcIdx];
          }
          // Remaining slots are already zero-initialized
        }

        dst.joint_and_weights.jointIndices = std::move(paddedIndices);
        dst.joint_and_weights.jointWeights = std::move(paddedWeights);
        dst.joint_and_weights.elementSize = int(roundedElementSize);

        DCOUT("Bone count rounded. New elementSize: " << dst.joint_and_weights.elementSize);
      }
    }

    // Explicit joint orders
    // If the mesh has `skel:joints`, remap jointIndex.
    {
      std::vector<value::token> joints = mesh.get_joints();
      if ((dst.skel_id >= 0) && (dst.skel_id < int(skeletons.size())) && joints.size()) {
        //  DCOUT("has explicit joint orders.\n");

        const auto &skel = skeletons[size_t(dst.skel_id)];

        // Cache BuildSkelNameToIndexMap per skeleton ID (avoids rebuilding per mesh)
        auto cache_it = _skelNameToIndexCache.find(dst.skel_id);
        if (cache_it == _skelNameToIndexCache.end()) {
          cache_it = _skelNameToIndexCache.emplace(dst.skel_id, BuildSkelNameToIndexMap(skel)).first;
        }
        const auto &name_to_index_map = cache_it->second;

        // Flat vector remap: index_remap[i] = skeleton joint index for mesh joint i
        std::vector<int> index_remap(joints.size(), -1);

        for (size_t i = 0; i < joints.size(); i++) {
          std::string joint_name = joints[i].str();

          auto nit = name_to_index_map.find(joint_name);
          if (nit == name_to_index_map.end()) {
            PUSH_ERROR_AND_RETURN(fmt::format("joint_name {} not found in Skeleton", joint_name));
          }

          index_remap[i] = nit->second;
        }

        for (size_t i = 0; i < dst.joint_and_weights.jointIndices.size(); i++) {
          int src_idx = dst.joint_and_weights.jointIndices[i];
          if (src_idx >= 0 && size_t(src_idx) < index_remap.size() && index_remap[size_t(src_idx)] >= 0) {
            dst.joint_and_weights.jointIndices[i] = index_remap[size_t(src_idx)];
          }
        }
      }

    }

    // geomBindTransform(optional).
    // If not authored, defaults to identity matrix (already set in struct initialization)
    if (mesh.has_primvar("skel:geomBindTransform")) {
      GeomPrimvar bindTransformPvar;

      if (!GetGeomPrimvar(env.stage, &mesh, "skel:geomBindTransform",
                          &bindTransformPvar, &_err)) {
        return false;
      }

      value::matrix4d bindTransform;
      if (!bindTransformPvar.get_value(&bindTransform)) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Failed to get `skel:geomBindTransform` attribute. "
                        "Ensure `skel:geomBindTransform` is type `matrix4d`"));
      }

      dst.joint_and_weights.geomBindTransform = bindTransform;
      dst.joint_and_weights.hasGeomBindTransform = true;
    } else {
      // geomBindTransform not authored - use identity (already set by default)
      // Flag remains false to indicate fallback was used
      DCOUT("skel:geomBindTransform not authored, using identity matrix");
    }
  }

  //
  // 6. BlendShapes
  //
  //    NOTE: (Built-in) BlendShape attributes are per-point, so it is not
  //    affected by triangulation and single-indexable indices build.
  //
  for (const auto &it : blendshapes) {
    const std::string &bs_path = it.first;
    const BlendShape *bs = it.second;

    if (!bs) {
      continue;
    }

    //
    // TODO: in-between attribs
    //

    std::vector<int> vertex_indices;
    std::vector<value::vector3f> normal_offsets;
    std::vector<value::vector3f> vertex_offsets;

    bs->pointIndices.get_value(&vertex_indices);
    bs->normalOffsets.get_value(&normal_offsets);
    bs->offsets.get_value(&vertex_offsets);

    ShapeTarget shapeTarget;
    shapeTarget.abs_path = bs_path;
    shapeTarget.prim_name = bs->name;
    shapeTarget.display_name = bs->metas().has_displayName() ? bs->metas().get_displayName() : "";

    if (vertex_indices.empty()) {
      PUSH_WARN(
          fmt::format("`pointIndices` in BlendShape `{}` is not authored or "
                      "empty. Skipping.",
                      bs->name));
    }

    // Validate and convert indices in-place.
    shapeTarget.pointIndices.reserve(vertex_indices.size());
    for (size_t i = 0; i < vertex_indices.size(); i++) {
      if (vertex_indices[i] < 0) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "negative index in `pointIndices`. Prim path: `{}`", bs_path));
      }

      if (uint32_t(vertex_indices[i]) >= dst.points.size()) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("pointIndices[{}] {} exceeds the number of points in "
                        "GeomMesh {}. Prim path: `{}`",
                        i, vertex_indices[i], dst.points.size(), bs_path));
      }

      shapeTarget.pointIndices.push_back(uint32_t(vertex_indices[i]));
    }

    if (vertex_offsets.size() &&
        (vertex_offsets.size() == vertex_indices.size())) {
      shapeTarget.pointOffsets.resize(vertex_offsets.size());
      memcpy(shapeTarget.pointOffsets.data(), vertex_offsets.data(),
             sizeof(value::normal3f) * vertex_offsets.size());
    }

    if (normal_offsets.size() &&
        (normal_offsets.size() == vertex_indices.size())) {
      shapeTarget.normalOffsets.resize(normal_offsets.size());
      memcpy(shapeTarget.normalOffsets.data(), normal_offsets.data(),
             sizeof(value::normal3f) * normal_offsets.size());
    }

    // TODO inbetweens

    // TODO: key duplicate check
    dst.targets[bs->name] = shapeTarget;
    DCOUT("Converted blendshape target: " << bs->name);
  }


  //
  // 7. Compute normals
  //
  //    Compute normals when normals is not present or compute tangents
  //    requiested but normals is not present. Normals are computed with
  //    'vertex' variability to compute smooth normals for shared vertex.
  //
  //    When triangulated, normals are computed for triangulated mesh.
  //
  bool compute_normals =
      (env.mesh_config.compute_normals && dst.normals.empty());
  bool compute_tangents =
      (env.mesh_config.compute_tangents_and_binormals && dst.tangents.empty());

  // When compute_tangents_only_with_normal_map is set, skip tangent
  // computation for meshes without a normal map texture connection.
  if (compute_tangents && env.mesh_config.compute_tangents_only_with_normal_map) {
    bool has_normal_map = false;
    // Check mesh-level material
    if (dst.material_id >= 0 &&
        dst.material_id < int(materials.size())) {
      const auto &rmat = materials[size_t(dst.material_id)];
      if (rmat.surfaceShader && rmat.surfaceShader->normal.is_texture()) {
        has_normal_map = true;
      }
      if (!has_normal_map && rmat.openPBRShader &&
          rmat.openPBRShader->normal.is_texture()) {
        has_normal_map = true;
      }
    }
    // Check per-subset materials
    if (!has_normal_map) {
      for (const auto &kv : dst.material_subsetMap) {
        int mid = kv.second.material_id;
        if (mid >= 0 && mid < int(materials.size())) {
          const auto &rmat = materials[size_t(mid)];
          if ((rmat.surfaceShader && rmat.surfaceShader->normal.is_texture()) ||
              (rmat.openPBRShader && rmat.openPBRShader->normal.is_texture())) {
            has_normal_map = true;
            break;
          }
        }
      }
    }
    if (!has_normal_map) {
      DCOUT("No normal map texture assigned — skipping tangent computation.");
      compute_tangents = false;
    }
  }

  if (compute_normals || (compute_tangents && dst.normals.empty())) {
    //TUSDZ_LOG_I("Build normals");
    DCOUT("Compute normals");
    std::vector<vec3> normals;
    if (!ComputeNormals(dst.points, dst.faceVertexCounts(),
                       dst.faceVertexIndices(), normals, &_err)) {
      DCOUT("compute normals failed.");
      return false;
    }

    dst.normals.set_buffer(reinterpret_cast<const uint8_t *>(normals.data()),
                           normals.size() * sizeof(vec3));
    dst.normals.elementSize = 1;
    dst.normals.variability = VertexVariability::Vertex;
    dst.normals.format = VertexAttributeFormat::Vec3;
    dst.normals.stride = 0;
    dst.normals.indices.clear();
    dst.normals.name = "normals";

    if (!is_single_indexable) {
      auto result = VertexToFaceVarying(
          dst.normals.get_data(), dst.normals.stride_bytes(),
          dst.faceVertexCounts(), dst.faceVertexIndices());
      if (!result) {
        PUSH_WARN(fmt::format(
            "Convert vertex/varying `normals` attribute failed for Mesh '{}': {}. Normals removed from RenderMesh.",
            abs_prim_path.full_path_name(), result.error()));
        // Clear normals from RenderMesh since conversion failed
        dst.normals.data.clear();
        dst.normals.indices.clear();
      } else {
        dst.normals.data = result.value();
        dst.normals.variability = VertexVariability::FaceVarying;
      }
    }
  }


  //
  // 8. Deferred tangent check — must happen BEFORE index build decision,
  //    because the index build is skipped when compute_tangents is true
  //    (expecting the tangent block to handle it). If we defer tangents,
  //    compute_tangents becomes false and the index build must run now.
  //
  if (compute_tangents && env.mesh_config.defer_tangent_computation) {
    // Mark for lazy computation — actual tangent work deferred until
    // ComputeDeferredTangents() is called.
    dst.tangent_computation_deferred = true;
    compute_tangents = false;
    DCOUT("Tangent computation deferred.");
  }

  //
  // 8a. Build indices
  //
  // When force_fast_index_build is set, skip tangent computation so that
  // BuildVertexIndicesFastImpl is always used (reproduces WASM code path).
  if (env.mesh_config.force_fast_index_build) {
    compute_tangents = false;
  }

  // Skip fast index build when tangent computation will follow, because
  // BuildVertexIndicesImpl (called after tangent computation) requires
  // facevarying attributes, and BuildVertexIndicesFastImpl converts them
  // to vertex variability.
  if (env.mesh_config.build_vertex_indices && (!is_single_indexable) &&
      !compute_tangents) {
    if (!env.mesh_config.prefer_non_indexed) {
      DCOUT("Build vertex indices");
      //TUSDZ_LOG_I("Build vertex indices");

      if (!BuildVertexIndicesFastImpl(dst)) {
        return false;
      }

      is_single_indexable = true;
    }
  }

  //
  // 8b. Compute tangents (immediate, not deferred).
  //
  if (compute_tangents) {
    DCOUT("Compute tangents.");

    // TODO: Support arbitrary slotID
    if (!dst.texcoords.count(0)) {
      PUSH_WARN("texcoord is not assigned to the mesh. Skipping tangent/binormal computation.\n");
    } else {

    // Access existing normals/texcoords from RenderMesh via pointer cast
    // instead of copying into separate vectors (saves ~200MB for large meshes).
    const vec3 *normals_ptr = reinterpret_cast<const vec3 *>(dst.normals.buffer());
    size_t normals_count = dst.normals.vertex_count();
    const vec2 *texcoords_ptr = reinterpret_cast<const vec2 *>(dst.texcoords[0].buffer());
    size_t texcoords_count = dst.texcoords[0].vertex_count();

    std::vector<vec3> tangents;
    std::vector<vec3> binormals;
    std::vector<uint32_t> vertex_indices;

    // When facevarying, expand per-vertex points to per-face-vertex so all
    // arrays (vertices, texcoords, normals) have the same size.
    std::vector<vec3> facevarying_points;
    const std::vector<vec3> *points_ptr = &dst.points;
    if (!is_single_indexable) {
      const auto &fvi = dst.faceVertexIndices();
      facevarying_points.resize(fvi.size());
      for (size_t i = 0; i < fvi.size(); i++) {
        if (fvi[i] < dst.points.size()) {
          facevarying_points[i] = dst.points[fvi[i]];
        }
      }
      points_ptr = &facevarying_points;
    }

    // Dispatch tangent computation based on configured method.
    // Default is Lengyel (fast, lightweight). MikkTSpace/FastMikkTSpace/Hybrid
    // are optional higher-quality methods.
    bool used_mikktspace = false;

    if (env.mesh_config.tangent_method == MeshConverterConfig::TangentComputationMethod::MikkTSpace ||
        env.mesh_config.tangent_method == MeshConverterConfig::TangentComputationMethod::FastMikkTSpace ||
        env.mesh_config.tangent_method == MeshConverterConfig::TangentComputationMethod::Hybrid) {
      std::string mikk_err;
      bool mikktspace_ok = false;
      bool use_fast = (env.mesh_config.tangent_method == MeshConverterConfig::TangentComputationMethod::FastMikkTSpace);
      bool use_hybrid = (env.mesh_config.tangent_method == MeshConverterConfig::TangentComputationMethod::Hybrid);

      if (!is_single_indexable) {
        std::vector<value::float3> fv_normals(normals_ptr, normals_ptr + normals_count);
        std::vector<value::float2> fv_texcoords(texcoords_ptr, texcoords_ptr + texcoords_count);
        if (use_hybrid) {
          mikktspace_ok = fast_mikkt::ComputeTangentsHybrid(
              *points_ptr, fv_normals, fv_texcoords, dst.faceVertexCounts(),
              &tangents, &binormals, nullptr, &mikk_err);
        } else if (use_fast) {
          mikktspace_ok = fast_mikkt::ComputeTangentsFastMikkTSpace(
              *points_ptr, fv_normals, fv_texcoords, dst.faceVertexCounts(),
              &tangents, &binormals, &mikk_err);
        } else {
          mikktspace_ok = ComputeTangentsMikkTSpace(
              *points_ptr, fv_normals, fv_texcoords, dst.faceVertexCounts(),
              &tangents, &binormals, &mikk_err);
        }
      } else {
        const auto &fvi = dst.faceVertexIndices();
        std::vector<value::float3> fv_positions(fvi.size());
        std::vector<value::float3> fv_normals(fvi.size());
        std::vector<value::float2> fv_texcoords(fvi.size());
        for (size_t i = 0; i < fvi.size(); i++) {
          if (fvi[i] < dst.points.size()) fv_positions[i] = dst.points[fvi[i]];
          if (fvi[i] < normals_count) fv_normals[i] = normals_ptr[fvi[i]];
          if (fvi[i] < texcoords_count) fv_texcoords[i] = texcoords_ptr[fvi[i]];
        }
        if (use_hybrid) {
          mikktspace_ok = fast_mikkt::ComputeTangentsHybrid(
              fv_positions, fv_normals, fv_texcoords, dst.faceVertexCounts(),
              &tangents, &binormals, nullptr, &mikk_err);
        } else if (use_fast) {
          mikktspace_ok = fast_mikkt::ComputeTangentsFastMikkTSpace(
              fv_positions, fv_normals, fv_texcoords, dst.faceVertexCounts(),
              &tangents, &binormals, &mikk_err);
        } else {
          mikktspace_ok = ComputeTangentsMikkTSpace(
              fv_positions, fv_normals, fv_texcoords, dst.faceVertexCounts(),
              &tangents, &binormals, &mikk_err);
        }
      }

      if (mikktspace_ok) {
        used_mikktspace = true;
      } else {
        DCOUT("MikkTSpace/Hybrid tangent computation failed: " << mikk_err
              << ". Falling back to Lengyel method.");
      }
    }

    if (!used_mikktspace) {
      // Lengyel method (default, or fallback if MikkTSpace failed)
      std::vector<vec2> tc_vec(texcoords_ptr, texcoords_ptr + texcoords_count);
      std::vector<vec3> nm_vec(normals_ptr, normals_ptr + normals_count);
      if (!ComputeTangentsAndBinormals(*points_ptr, dst.faceVertexCounts(),
                                       dst.faceVertexIndices(), tc_vec,
                                       nm_vec, !is_single_indexable, &tangents,
                                       &binormals, &vertex_indices, &_err,
                                       env.mesh_config.max_vertex_valence,
                                       env.mesh_config.facevarying_to_vertex_eps)) {
        PUSH_ERROR_AND_RETURN("Failed to compute tangents/binormals.");
      }
    }

    // Store tangents/binormals into dst with facevarying variability.
    if (used_mikktspace) {
      // MikkTSpace output is already facevarying
      dst.tangents.data.resize(tangents.size() * sizeof(vec3));
      memcpy(dst.tangents.data.data(), tangents.data(),
             tangents.size() * sizeof(vec3));

      dst.binormals.data.resize(binormals.size() * sizeof(vec3));
      memcpy(dst.binormals.data.data(), binormals.data(),
             binormals.size() * sizeof(vec3));
    } else {
      // Lengyel output needs index expansion
      dst.tangents.data.resize(vertex_indices.size() * sizeof(vec3));
      dst.binormals.data.resize(vertex_indices.size() * sizeof(vec3));
      vec3 *dst_tangents = reinterpret_cast<vec3 *>(dst.tangents.data.data());
      vec3 *dst_binormals = reinterpret_cast<vec3 *>(dst.binormals.data.data());
      for (size_t i = 0; i < vertex_indices.size(); i++) {
        dst_tangents[i] = tangents[vertex_indices[i]];
        dst_binormals[i] = binormals[vertex_indices[i]];
      }
    }

    dst.tangents.format = VertexAttributeFormat::Vec3;
    dst.tangents.stride = 0;
    dst.tangents.elementSize = 1;
    dst.tangents.variability = VertexVariability::FaceVarying;

    dst.binormals.format = VertexAttributeFormat::Vec3;
    dst.binormals.stride = 0;
    dst.binormals.elementSize = 1;
    dst.binormals.variability = VertexVariability::FaceVarying;

    // 2. Convert tangents/binormals to vertex variability if needed.
    if (env.mesh_config.build_vertex_indices) {
      if (is_single_indexable) {
        // Normals/texcoords are already vertex variability.
        // Convert facevarying tangents to vertex using the face-vertex indices.
        const std::vector<uint32_t> &fvIdx =
            dst.triangulatedFaceVertexIndices.size()
                ? dst.triangulatedFaceVertexIndices
                : dst.usdFaceVertexIndices;

        size_t numFvs = fvIdx.size();
        uint32_t numPts = static_cast<uint32_t>(dst.points.size());

        // tangents — accumulate then normalize
        if (dst.tangents.vertex_count() == numFvs) {
          const value::float3 *fvT = reinterpret_cast<const value::float3 *>(
              dst.tangents.data.data());
          std::vector<value::float3> vtxT(numPts, {0.0f, 0.0f, 0.0f});
          for (size_t i = 0; i < numFvs; i++) {
            if (fvIdx[i] < numPts) {
              vtxT[fvIdx[i]][0] += fvT[i][0];
              vtxT[fvIdx[i]][1] += fvT[i][1];
              vtxT[fvIdx[i]][2] += fvT[i][2];
            }
          }
          for (uint32_t vi = 0; vi < numPts; vi++) {
            float len = std::sqrt(vtxT[vi][0] * vtxT[vi][0] +
                                  vtxT[vi][1] * vtxT[vi][1] +
                                  vtxT[vi][2] * vtxT[vi][2]);
            if (len > 1e-8f) {
              vtxT[vi][0] /= len;
              vtxT[vi][1] /= len;
              vtxT[vi][2] /= len;
            }
          }
          dst.tangents.set_buffer(
              reinterpret_cast<const uint8_t *>(vtxT.data()),
              vtxT.size() * sizeof(value::float3));
          dst.tangents.variability = VertexVariability::Vertex;
        }

        // binormals — accumulate then normalize
        if (dst.binormals.vertex_count() == numFvs) {
          const value::float3 *fvB = reinterpret_cast<const value::float3 *>(
              dst.binormals.data.data());
          std::vector<value::float3> vtxB(numPts, {0.0f, 0.0f, 0.0f});
          for (size_t i = 0; i < numFvs; i++) {
            if (fvIdx[i] < numPts) {
              vtxB[fvIdx[i]][0] += fvB[i][0];
              vtxB[fvIdx[i]][1] += fvB[i][1];
              vtxB[fvIdx[i]][2] += fvB[i][2];
            }
          }
          for (uint32_t vi = 0; vi < numPts; vi++) {
            float len = std::sqrt(vtxB[vi][0] * vtxB[vi][0] +
                                  vtxB[vi][1] * vtxB[vi][1] +
                                  vtxB[vi][2] * vtxB[vi][2]);
            if (len > 1e-8f) {
              vtxB[vi][0] /= len;
              vtxB[vi][1] /= len;
              vtxB[vi][2] /= len;
            }
          }
          dst.binormals.set_buffer(
              reinterpret_cast<const uint8_t *>(vtxB.data()),
              vtxB.size() * sizeof(value::float3));
          dst.binormals.variability = VertexVariability::Vertex;
        }
      } else {
        // All attributes are still facevarying - use full rebuild.
        if (!BuildVertexIndicesImpl(dst, env.mesh_config.max_vertex_valence,
                                     env.mesh_config.facevarying_to_vertex_eps)) {
          return false;
        }
        is_single_indexable = true;
      }
    }
  } // else (texcoords available)
  } // if (compute_tangents)

  // Quantize tangents if a packed format is configured.
  if (!dst.tangents.empty() && !dst.binormals.empty()) {
    QuantizeMeshTangents(dst, env.mesh_config.tangent_storage);
  }

  // Quantize normals if a packed format is configured.
  if (!dst.normals.empty()) {
    QuantizeMeshNormals(dst, env.mesh_config.normal_storage);
  }

  dst.is_single_indexable = is_single_indexable;

  dst.prim_name = mesh.name;
  dst.abs_path = abs_prim_path.full_path_name();
  dst.display_name = mesh.metas().has_displayName() ? mesh.metas().get_displayName() : "";

  //
  // Check for MeshLightAPI - if present, mark this mesh as an area light
  //
  const auto &prim_metas = mesh.metas();
  if (prim_metas.has_apiSchemas()) {
    const auto api_schemas = prim_metas.get_apiSchemas();
    bool has_meshlight_api = false;

    for (const auto &schema_pair : api_schemas.names) {
      if (schema_pair.first == APISchemas::APIName::MeshLightAPI) {
        has_meshlight_api = true;
        break;
      }
    }

    if (has_meshlight_api) {
      DCOUT("Mesh has MeshLightAPI: " << abs_prim_path.full_path_name());

      dst.is_area_light = true;

      // Extract MeshLightAPI properties from mesh
      // MeshLightAPI inherits from LightAPI, which uses "inputs:" prefix

      // color
      if (auto it = mesh.props.find("inputs:color"); it != mesh.props.end()) {
        const Property &prop = it->second;
        const Attribute &attr = prop.get_attribute();
        const primvar::PrimVar &pvar = attr.get_var();
        if (auto val = pvar.get_value<value::color3f>()) {
          dst.light_color[0] = val.value()[0];
          dst.light_color[1] = val.value()[1];
          dst.light_color[2] = val.value()[2];
        }
      }

      // intensity
      if (auto it = mesh.props.find("inputs:intensity");
          it != mesh.props.end()) {
        const Property &prop = it->second;
        const Attribute &attr = prop.get_attribute();
        const primvar::PrimVar &pvar = attr.get_var();
        if (auto val = pvar.get_value<float>()) {
          dst.light_intensity = val.value();
        }
      }

      // exposure (optional)
      if (auto it = mesh.props.find("inputs:exposure"); it != mesh.props.end()) {
        const Property &prop = it->second;
        const Attribute &attr = prop.get_attribute();
        const primvar::PrimVar &pvar = attr.get_var();
        if (auto val = pvar.get_value<float>()) {
          dst.light_exposure = val.value();
        }
      }

      // normalize
      if (auto it = mesh.props.find("inputs:normalize");
          it != mesh.props.end()) {
        const Property &prop = it->second;
        const Attribute &attr = prop.get_attribute();
        const primvar::PrimVar &pvar = attr.get_var();
        if (auto val = pvar.get_value<bool>()) {
          dst.light_normalize = val.value();
        }
      }

      // materialSyncMode
      if (auto it = mesh.props.find("inputs:materialSyncMode");
          it != mesh.props.end()) {
        const Property &prop = it->second;
        const Attribute &attr = prop.get_attribute();
        const primvar::PrimVar &pvar = attr.get_var();
        if (auto val = pvar.get_value<value::token>()) {
          dst.light_material_sync_mode = val.value().str();
        }
      }

      // Set default if not specified
      if (dst.light_material_sync_mode.empty()) {
        dst.light_material_sync_mode = "materialGlowTintsLight";  // USD default
      }

      DCOUT("  Area light properties:"
            << " color=(" << dst.light_color[0] << "," << dst.light_color[1] << "," << dst.light_color[2] << ")"
            << " intensity=" << dst.light_intensity
            << " exposure=" << dst.light_exposure
            << " normalize=" << dst.light_normalize
            << " materialSyncMode=" << dst.light_material_sync_mode);
    }
  }


  // Free source GeomMesh large arrays to reduce peak memory.
  // The const_cast is safe here: ConvertMesh has finished reading all mesh
  // data, and the GeomMesh attribute data is no longer needed.
  if (env.mesh_config.lowmem) {
    auto *pmesh = const_cast<GeomMesh *>(&mesh);

    // Core geometry
    pmesh->points.set_value({});
    pmesh->normals.set_value({});
    pmesh->faceVertexIndices.set_value({});
    pmesh->faceVertexCounts.set_value({});
    pmesh->velocities.set_value({});

    // SubD attributes (not used in ConvertMesh, but may be large)
    pmesh->cornerIndices.set_value({});
    pmesh->cornerSharpnesses.set_value({});
    pmesh->creaseIndices.set_value({});
    pmesh->creaseLengths.set_value({});
    pmesh->creaseSharpnesses.set_value({});
    pmesh->holeIndices.set_value({});

    // All primvar data (primvars:normals, primvars:st, primvars:displayColor,
    // skel:jointIndices, skel:jointWeights, etc.) — already copied to RenderMesh.
    { std::map<std::string, Property> tmp; pmesh->props.swap(tmp); }
  }

  (*dstMesh) = std::move(dst);

  return true;
}

// static
bool RenderSceneConverter::ComputeDeferredTangents(
    RenderMesh *mesh,
    MeshConverterConfig::TangentComputationMethod method,
    MeshConverterConfig::TangentStorageFormat storage,
    std::string *err) {
  if (!mesh) {
    if (err) *err = "mesh is nullptr.";
    return false;
  }

  if (!mesh->tangent_computation_deferred) {
    // Already computed or not deferred — nothing to do.
    return true;
  }

  if (mesh->normals.empty()) {
    if (err) *err = "Cannot compute tangents: normals not available.";
    return false;
  }

  if (!mesh->texcoords.count(0)) {
    if (err) *err = "Cannot compute tangents: texcoord slot 0 not available.";
    return false;
  }

  // Use existing data from RenderMesh (no redundant copies).
  const vec3 *normals_ptr = reinterpret_cast<const vec3 *>(mesh->normals.buffer());
  size_t normals_count = mesh->normals.vertex_count();
  const vec2 *texcoords_ptr = reinterpret_cast<const vec2 *>(mesh->texcoords[0].buffer());
  size_t texcoords_count = mesh->texcoords[0].vertex_count();

  // Determine if data is already facevarying or vertex-varying.
  const auto &fvi = mesh->triangulatedFaceVertexIndices.size()
                        ? mesh->triangulatedFaceVertexIndices
                        : mesh->usdFaceVertexIndices;
  const auto &fvc = mesh->triangulatedFaceVertexCounts.size()
                        ? mesh->triangulatedFaceVertexCounts
                        : mesh->usdFaceVertexCounts;

  bool is_facevarying = (normals_count == fvi.size());

  std::vector<vec3> tangents;
  std::vector<vec3> binormals;
  std::vector<uint32_t> vertex_indices;

  bool used_mikktspace = false;

  if (method == MeshConverterConfig::TangentComputationMethod::MikkTSpace ||
      method == MeshConverterConfig::TangentComputationMethod::FastMikkTSpace ||
      method == MeshConverterConfig::TangentComputationMethod::Hybrid) {
    bool use_fast = (method == MeshConverterConfig::TangentComputationMethod::FastMikkTSpace);
    bool use_hybrid = (method == MeshConverterConfig::TangentComputationMethod::Hybrid);
    std::string mikk_err;
    bool mikktspace_ok = false;

    if (is_facevarying) {
      std::vector<value::float3> fv_positions(fvi.size());
      for (size_t i = 0; i < fvi.size(); i++) {
        if (fvi[i] < mesh->points.size()) fv_positions[i] = mesh->points[fvi[i]];
      }
      std::vector<value::float3> fv_normals(normals_ptr, normals_ptr + normals_count);
      std::vector<value::float2> fv_texcoords(texcoords_ptr, texcoords_ptr + texcoords_count);
      if (use_hybrid) {
        mikktspace_ok = fast_mikkt::ComputeTangentsHybrid(
            fv_positions, fv_normals, fv_texcoords, fvc,
            &tangents, &binormals, nullptr, &mikk_err);
      } else if (use_fast) {
        mikktspace_ok = fast_mikkt::ComputeTangentsFastMikkTSpace(
            fv_positions, fv_normals, fv_texcoords, fvc,
            &tangents, &binormals, &mikk_err);
      } else {
        mikktspace_ok = ComputeTangentsMikkTSpace(
            fv_positions, fv_normals, fv_texcoords, fvc,
            &tangents, &binormals, &mikk_err);
      }
    } else {
      std::vector<value::float3> fv_positions(fvi.size());
      std::vector<value::float3> fv_normals(fvi.size());
      std::vector<value::float2> fv_texcoords(fvi.size());
      for (size_t i = 0; i < fvi.size(); i++) {
        if (fvi[i] < mesh->points.size()) fv_positions[i] = mesh->points[fvi[i]];
        if (fvi[i] < normals_count) fv_normals[i] = normals_ptr[fvi[i]];
        if (fvi[i] < texcoords_count) fv_texcoords[i] = texcoords_ptr[fvi[i]];
      }
      if (use_hybrid) {
        mikktspace_ok = fast_mikkt::ComputeTangentsHybrid(
            fv_positions, fv_normals, fv_texcoords, fvc,
            &tangents, &binormals, nullptr, &mikk_err);
      } else if (use_fast) {
        mikktspace_ok = fast_mikkt::ComputeTangentsFastMikkTSpace(
            fv_positions, fv_normals, fv_texcoords, fvc,
            &tangents, &binormals, &mikk_err);
      } else {
        mikktspace_ok = ComputeTangentsMikkTSpace(
            fv_positions, fv_normals, fv_texcoords, fvc,
            &tangents, &binormals, &mikk_err);
      }
    }

    if (mikktspace_ok) {
      used_mikktspace = true;
    } else {
      if (err) *err = "MikkTSpace/Hybrid tangent computation failed: " + mikk_err + ". Falling back to Lengyel.";
      // Fall through to Lengyel
    }
  }

  if (!used_mikktspace) {
    // Lengyel method (default)
    // Build facevarying points if needed
    std::vector<vec3> fv_points;
    const std::vector<vec3> *points_ptr = &mesh->points;
    if (is_facevarying) {
      fv_points.resize(fvi.size());
      for (size_t i = 0; i < fvi.size(); i++) {
        if (fvi[i] < mesh->points.size()) fv_points[i] = mesh->points[fvi[i]];
      }
      points_ptr = &fv_points;
    }

    std::vector<vec2> tc_vec(texcoords_ptr, texcoords_ptr + texcoords_count);
    std::vector<vec3> nm_vec(normals_ptr, normals_ptr + normals_count);
    std::string lengyel_err;
    if (!ComputeTangentsAndBinormals(*points_ptr, fvc,
                                     fvi, tc_vec,
                                     nm_vec, is_facevarying, &tangents,
                                     &binormals, &vertex_indices, &lengyel_err)) {
      if (err) *err = "Lengyel tangent computation failed: " + lengyel_err;
      return false;
    }
  }

  // Store results
  if (used_mikktspace) {
    mesh->tangents.data.resize(tangents.size() * sizeof(vec3));
    memcpy(mesh->tangents.data.data(), tangents.data(),
           tangents.size() * sizeof(vec3));

    mesh->binormals.data.resize(binormals.size() * sizeof(vec3));
    memcpy(mesh->binormals.data.data(), binormals.data(),
           binormals.size() * sizeof(vec3));
  } else {
    // Lengyel output needs index expansion
    mesh->tangents.data.resize(vertex_indices.size() * sizeof(vec3));
    mesh->binormals.data.resize(vertex_indices.size() * sizeof(vec3));
    vec3 *dst_tangents = reinterpret_cast<vec3 *>(mesh->tangents.data.data());
    vec3 *dst_binormals = reinterpret_cast<vec3 *>(mesh->binormals.data.data());
    for (size_t i = 0; i < vertex_indices.size(); i++) {
      dst_tangents[i] = tangents[vertex_indices[i]];
      dst_binormals[i] = binormals[vertex_indices[i]];
    }
  }

  mesh->tangents.format = VertexAttributeFormat::Vec3;
  mesh->tangents.stride = 0;
  mesh->tangents.elementSize = 1;
  mesh->tangents.variability = VertexVariability::FaceVarying;

  mesh->binormals.format = VertexAttributeFormat::Vec3;
  mesh->binormals.stride = 0;
  mesh->binormals.elementSize = 1;
  mesh->binormals.variability = VertexVariability::FaceVarying;

  // Quantize tangents if a packed format is requested.
  if (!mesh->tangents.empty() && !mesh->binormals.empty()) {
    QuantizeMeshTangents(*mesh, storage);
  }

  mesh->tangent_computation_deferred = false;
  return true;
}

}  // namespace tydra
}  // namespace tinyusdz
