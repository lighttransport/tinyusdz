// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// TODO:
//   - [ ] Subdivision surface to polygon mesh conversion.
//     - [ ] Correctly handle primvar with 'vertex' interpolation(Use the basis
//     function of subd surface)
//   - [x] Support time-varying shader attribute(timeSamples)
//   - [ ] Wide gamut colorspace conversion support
//     - [ ] sRGB <-> DisplayP3
//   - [ ] tangentes and binormals
//   - [ ] displayColor, displayOpacity primvar(vertex color)
//   - [ ] Support Inbetween BlendShape
//   - [ ] Support material binding collection(Collection API)
//   - [ ] Support multiple skel animation
//   - [ ] Adjust normal vector computation with handness?
//   - [ ] Node xform animation
//
#include <numeric>

#include "image-loader.hh"
#include "image-util.hh"
#include "linear-algebra.hh"
#include "pprinter.hh"
#include "prim-types.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "tinyusdz.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "value-pprint.hh"

#if defined(TINYUSDZ_WITH_COLORIO)
#include "external/tiny-color-io.h"
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// For triangulation.
// TODO: Use tinyobjloader's triangulation
#include "external/mapbox/earcut/earcut.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

//
#include "common-macros.inc"
#include "math-util.inc"

//
#include "tydra/render-data.hh"
#include "tydra/scene-access.hh"
#include "tydra/shader-network.hh"

// #define PushError(msg) if (err) { (*err) += msg; }

#if 0
#define SET_ERROR_AND_RETURN(msg) \
  if (err) {                      \
    (*err) = (msg);               \
  }                               \
  return false
#endif

namespace tinyusdz {

namespace tydra {

//#define EXTERN_EVALUATE_TYPED_ATTRIBUTE_INSTANCE(__ty) \
//extern template bool EvaluateTypedAnimatableAttribute<__ty>( const tinyusdz::Stage &stage, const TypedAttribute<Animatable<__ty>> &attr, const std::string &attr_name, __ty *value, std::string *err, const double t = value::TimeCode::Default(), const tinyusdz::value::TimeSampleInterpolationType tinterp = value::TimeSampleInterpolationType::Linear);
//
//APPLY_FUNC_TO_VALUE_TYPES(EXTERN_EVALUATE_TYPED_ATTRIBUTE_INSTANCE)
//
//#undef EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE

namespace {

inline std::string to_string(const UVTexture::Channel channel) {
  if (channel == UVTexture::Channel::RGB) {
    return "rgb";
  } else if (channel == UVTexture::Channel::R) {
    return "r";
  } else if (channel == UVTexture::Channel::G) {
    return "g";
  } else if (channel == UVTexture::Channel::B) {
    return "b";
  } else if (channel == UVTexture::Channel::A) {
    return "a";
  }

  return "[[InternalError. Invalid UVTexture::Channel]]";
}

#if 0
template <typename T>
inline T Get(const nonstd::optional<T> &nv, const T &default_value) {
  if (nv) {
    return nv.value();
  }
  return default_value;
}
#endif

//
// Convert vertex attribute with Uniform variability(interpolation) to
// facevarying variability, by replicating uniform value per face over face
// vertices.
//
template <typename T>
nonstd::expected<std::vector<T>, std::string> UniformToFaceVarying(
    const std::vector<T> &inputs,
    const std::vector<uint32_t> &faceVertexCounts) {
  std::vector<T> dst;

  if (inputs.size() == faceVertexCounts.size()) {
    return nonstd::make_unexpected(
        fmt::format("The number of inputs {} must be the same with "
                    "faceVertexCounts.size() {}",
                    inputs.size(), faceVertexCounts.size()));
  }

  for (size_t i = 0; i < faceVertexCounts.size(); i++) {
    size_t cnt = faceVertexCounts[i];

    // repeat cnt times.
    for (size_t k = 0; k < cnt; k++) {
      dst.emplace_back(inputs[i]);
    }
  }

  return dst;
}

//
// Convert vertex attribute with Uniform variability(interpolation) to vertex
// variability, by replicating uniform value for vertices of a face. For shared
// vertex, the value will be overwritten.
//
template <typename T>
nonstd::expected<std::vector<T>, std::string> UniformToVertex(
    const std::vector<T> &inputs, const size_t elementSize,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices) {
  std::vector<T> dst;

  if (faceVertexIndices.size() < 3) {
    return nonstd::make_unexpected(
        fmt::format("faceVertexIndices.size must be 3 or greater, but got {}.",
                    faceVertexCounts.size()));
  }

  if (faceVertexCounts.empty()) {
    return nonstd::make_unexpected("faceVertexCounts.size is zero");
  }

  if (elementSize == 0) {
    return nonstd::make_unexpected("`elementSize` is zero.");
  }

  if ((inputs.size() % elementSize) != 0) {
    return nonstd::make_unexpected(
        fmt::format("input bytes {} must be dividable by elementSize {}.",
                    inputs.size(), elementSize));
  }

  size_t num_uniforms = faceVertexCounts.size();

  dst.resize(num_uniforms * elementSize);

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

      if (v_idx >= inputs.size()) {
        return nonstd::make_unexpected(
            fmt::format("vertexIndex {} is out-of-range for inputs.size {}.",
                        v_idx, inputs.size()));
      }

      // may overwrite the value
      memcpy(&dst[v_idx * elementSize], &inputs[i * elementSize],
             sizeof(T) * elementSize);
    }

    fvIndexOffset += cnt;
  }

  return dst;
}

#if 1  // not used atm.
nonstd::expected<std::vector<uint8_t>, std::string> UniformToVertex(
    const std::vector<uint8_t> &inputs, const size_t stride_bytes,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices) {
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

  if (num_uniforms == faceVertexCounts.size()) {
    return nonstd::make_unexpected(fmt::format(
        "The number of input uniform attributes {} must be the same with "
        "faceVertexCounts.size() {}",
        num_uniforms, faceVertexCounts.size()));
  }

  dst.resize(num_uniforms * stride_bytes);

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

      if (v_idx >= inputs.size()) {
        return nonstd::make_unexpected(
            fmt::format("vertexIndex {} is out-of-range for inputs.size {}.",
                        v_idx, inputs.size()));
      }

      // may overwrite the value
      memcpy(dst.data() + v_idx * stride_bytes,
             inputs.data() + i * stride_bytes, stride_bytes);
    }

    fvIndexOffset += cnt;
  }

  return dst;
}
#endif

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

  if (num_uniforms == faceVertexCounts.size()) {
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
template <typename T>
nonstd::expected<std::vector<T>, std::string> VertexToFaceVarying(
    const std::vector<T> &inputs, const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices) {
  std::vector<T> dst;

  size_t face_offset{0};
  for (size_t i = 0; i < faceVertexCounts.size(); i++) {
    size_t cnt = faceVertexCounts[i];

    for (size_t k = 0; k < cnt; k++) {
      size_t idx = k + face_offset;

      if (idx >= faceVertexIndices.size()) {
        return nonstd::make_unexpected(fmt::format(
            "faeVertexIndex out-of-range at faceVertexCount[{}]", i));
      }

      size_t v_idx = faceVertexIndices[idx];

      if (v_idx >= inputs.size()) {
        return nonstd::make_unexpected(
            fmt::format("faeVertexIndices[{}] {} exceeds input array size {}",
                        idx, v_idx, inputs.size()));
      }

      dst.emplace_back(inputs[v_idx]);
    }

    face_offset += cnt;
  }

  return dst;
}

// Generic vertex to facevarying conversion
nonstd::expected<std::vector<uint8_t>, std::string> VertexToFaceVarying(
    const std::vector<uint8_t> &src, const size_t stride_bytes,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices) {
  std::vector<uint8_t> dst;

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

  std::vector<uint8_t> buf;
  buf.resize(stride_bytes);

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

      memcpy(buf.data(), src.data() + v_idx * stride_bytes, stride_bytes);
      dst.insert(dst.end(), buf.begin(), buf.end());
    }

    faceVertexIndexOffset += cnt;
  }

  return dst;
}

#if 0  // unused a.t.m
// Copy single value to facevarying vertices.
template <typename T>
static nonstd::expected<std::vector<T>, std::string> ConstantToFaceVarying(
    const T &input, const std::vector<uint32_t> &faceVertexCounts) {
  std::vector<T> dst;

  for (size_t i = 0; i < faceVertexCounts.size(); i++) {
    size_t cnt = faceVertexCounts[i];

    for (size_t k = 0; k < cnt; k++) {
      dst.emplace_back(input);
    }
  }

  return dst;
}
#endif

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
      *std::max_element(faceVertexIndices.cbegin(), faceVertexIndices.cend());

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

  dst.resize(stride_bytes * num_vertices);

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

#if 0
static nonstd::expected<std::vector<uint8_t>, std::string>
ConstantToFaceVarying(const std::vector<uint8_t> &src,
                      const size_t stride_bytes,
                      const std::vector<uint32_t> &faceVertexCounts) {
  std::vector<uint8_t> dst;

  if (src.empty()) {
    return nonstd::make_unexpected("src data is empty.");
  }

  if (stride_bytes == 0) {
    return nonstd::make_unexpected("stride_bytes must be non-zero.");
  }

  if ((src.size() != stride_bytes)) {
    return nonstd::make_unexpected(
        fmt::format("src size {} must be equal to stride_bytes {}", src.size(),
                    stride_bytes));
  }

  std::vector<uint8_t> buf;
  buf.resize(stride_bytes);

  for (size_t i = 0; i < faceVertexCounts.size(); i++) {
    size_t cnt = faceVertexCounts[i];

    for (size_t k = 0; k < cnt; k++) {
      dst.insert(dst.end(), buf.begin(), buf.end());
    }
  }

  return dst;
}
#endif

#if 0  // Not used atm.
static bool ToFaceVaryingAttribute(const std::string &attr_name,
  const VertexAttribute &src,
  const std::vector<uint32_t> &faceVertexCounts,
  const std::vector<uint32_t> &faceVertexIndices,
  VertexAttribute *dst,
  std::string *err) {

#define PushError(msg) \
  if (err) {           \
    (*err) += msg;     \
  }

  if (!dst) {
    PUSH_ERROR_AND_RETURN("'dest' parameter is nullptr.");
  }

  if (src.variability == VertexVariability::Indexed) {
    PUSH_ERROR_AND_RETURN(fmt::format("'indexed' variability for {} is not supported.", attr_name));
  } else if (src.variability == VertexVariability::Constant) {

    auto result = ConstantToFaceVarying(src.get_data(), src.stride_bytes(),
            faceVertexCounts);

    if (!result) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to convert vertex data with 'constant' variability to 'facevarying': name {}.", attr_name));
    }

    dst->data = result.value();
    dst->elementSize = src.elementSize;
    dst->format = src.format;
    dst->stride = src.stride;
    dst->variability = VertexVariability::FaceVarying;

    return true;

  } else if (src.variability == VertexVariability::Uniform) {

    auto result = UniformToFaceVarying(src.get_data(), src.stride_bytes(),
            faceVertexCounts);

    if (!result) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to convert vertex data with 'uniform' variability to 'facevarying': name {}.", attr_name));
    }

    dst->data = result.value();
    dst->elementSize = src.elementSize;
    dst->format = src.format;
    dst->stride = src.stride;
    dst->variability = VertexVariability::FaceVarying;

    return true;

  } else if (src.variability == VertexVariability::Vertex) {

    auto result = VertexToFaceVarying(src.get_data(), src.stride_bytes(),
            faceVertexCounts, faceVertexIndices);

    if (!result) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to convert vertex data with 'vertex' variability to 'facevarying': name {}.", attr_name));
    }

    dst->data = result.value();
    dst->elementSize = src.elementSize;
    dst->format = src.format;
    dst->stride = src.stride;
    dst->variability = VertexVariability::FaceVarying;

    return true;
  } else if (src.variability == VertexVariability::FaceVarying) {
    (*dst) = src;
    return true;
  }

#undef PushError

  return false;
}

static bool ToVertexVaryingAttribute(
  const std::string &attr_name,
  const VertexAttribute &src,
  const std::vector<uint32_t> &faceVertexCounts,
  const std::vector<uint32_t> &faceVertexIndices,
  VertexAttribute *dst,
  std::string *err) {

#define PushError(msg) \
  if (err) {           \
    (*err) += msg;     \
  }

  if (!dst) {
    PUSH_ERROR_AND_RETURN("'dest' parameter is nullptr.");
  }

  if (src.variability == VertexVariability::Indexed) {
    PUSH_ERROR_AND_RETURN(fmt::format("'indexed' variability for {} is not supported.", attr_name));
  } else if (src.variability == VertexVariability::Constant) {

    auto result = ConstantToVertex(src.get_data(), src.stride_bytes(),
            faceVertexCounts, faceVertexIndices);

    if (!result) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to convert vertex data with 'constant' variability to 'facevarying': name {}.", attr_name));
    }

    dst->data = result.value();
    dst->elementSize = src.elementSize;
    dst->format = src.format;
    dst->stride = src.stride;
    dst->variability = VertexVariability::Vertex;

    return true;

  } else if (src.variability == VertexVariability::Uniform) {

    auto result = UniformToVertex(src.get_data(), src.stride_bytes(),
            faceVertexCounts, faceVertexIndices);

    if (!result) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to convert vertex data with 'uniform' variability to 'facevarying': name {}.", attr_name));
    }

    dst->data = result.value();
    dst->elementSize = src.elementSize;
    dst->format = src.format;
    dst->stride = src.stride;
    dst->variability = VertexVariability::Vertex;

    return true;

  } else if (src.variability == VertexVariability::Vertex) {

    (*dst) = src;
    return true;
  } else if (src.variability == VertexVariability::FaceVarying) {

    PUSH_ERROR_AND_RETURN(fmt::format("'facevarying' variability cannot be converted to 'vertex' variability: name {}.", attr_name));

  }

#undef PushError

  return false;

}
#endif

std::vector<const tinyusdz::GeomSubset *> GetMaterialBindGeomSubsets(
    const tinyusdz::Prim &prim) {
  std::vector<const tinyusdz::GeomSubset *> dst;

  // GeomSubet Prim must be a child Prim of GeomMesh.
  for (const auto &child : prim.children()) {
    if (const tinyusdz::GeomSubset *psubset =
            child.as<tinyusdz::GeomSubset>()) {
      value::token tok;
      if (!psubset->familyName.get_value(&tok)) {
        continue;
      }

      if (tok.str() != "materialBind") {
        continue;
      }

      dst.push_back(psubset);
    }
  }

  return dst;
}

//
// name does not include "primvars:" prefix.
// TODO: connected attribute.
//
nonstd::expected<VertexAttribute, std::string> GetTextureCoordinate(
    const Stage &stage, const GeomMesh &mesh, const std::string &name) {
  VertexAttribute vattr;

  (void)stage;

  GeomPrimvar primvar;
  if (!mesh.get_primvar(name, &primvar)) {
    return nonstd::make_unexpected(fmt::format("No primvars:{}\n", name));
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

  std::vector<value::texcoord2f> uvs;
  if (!primvar.flatten_with_indices(&uvs)) {
    return nonstd::make_unexpected(
        "Failed to retrieve texture coordinate primvar with concrete type.\n");
  }

  vattr.format = VertexAttributeFormat::Vec2;
  vattr.data.resize(uvs.size() * sizeof(value::texcoord2f));
  memcpy(vattr.data.data(), uvs.data(), vattr.data.size());
  vattr.indices.clear();  // just in case.

  vattr.name = name;  // TODO: add "primvars:" namespace?

  return std::move(vattr);
}

#if 0  // not used at the moment.
///
/// For GeomSubset. Build offset table to corresponding array index in
/// mesh.faceVertexIndices. No need to use this function for triangulated mesh,
/// since the index can be easily computed as `3 * subset.indices[i]`
///
bool BuildFaceVertexIndexOffsets(const std::vector<uint32_t> &faceVertexCounts,
                                 std::vector<size_t> &faceVertexIndexOffsets) {
  size_t offset = 0;
  for (size_t i = 0; i < faceVertexCounts.size(); i++) {
    uint32_t npolys = faceVertexCounts[i];

    faceVertexIndexOffsets.push_back(offset);
    offset += npolys;
  }

  return true;
}
#endif

#if 0  // unused a.t.m.
bool ToVertexAttributeData(const GeomPrimvar &primvar, VertexAttribute *dst, std::string *err)
{
  size_t elementSize = primvar.get_elementSize();
  if (elementSize == 0) {
    SET_ERROR_AND_RETURN(fmt::format("elementSize is zero for primvar: {}", primvar.name()));
  }

  VertexAttribute vattr;

  const tinyusdz::Attribute &attr = primvar.get_attribute();

#define TO_TYPED_VALUE(__ty, __va_ty)                                \
  if (attr.type_id() == value::TypeTraits<__ty>::type_id()) {        \
    if (sizeof(__ty) != VertexAttributeFormatSize(__va_ty)) {        \
      SET_ERROR_AND_RETURN("Internal error. type size mismatch.\n"); \
    }                                                                \
    __ty value;                                                      \
    if (!primvar.get_value(&value, err)) {                           \
      return false;                                                  \
    }                                                                \
    vattr.format = __va_ty;                                          \
  } else if (attr.type_id() == (value::TypeTraits<__ty>::type_id() & \
                                value::TYPE_ID_1D_ARRAY_BIT)) {      \
    std::vector<__ty> flattened;                                     \
    if (!primvar.flatten_with_indices(&flattened, err)) {            \
      return false;                                                  \
    }                                                                \
  } else

  TO_TYPED_VALUE(int, VertexAttributeFormat::Int)
  {
    SET_ERROR_AND_RETURN(fmt::format("Unknown or unsupported data type for Geom PrimVar: {}", attr.type_name()));
  }

#undef TO_TYPED_VALUE

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

  // TODO
  vattr.indices.clear();  // just in case.

  (*dst) = std::move(vattr);

  return false;
}
#endif

#if 0  // TODO: Remove
///
/// Triangulate Geom primvar.
///
/// triangulatted indices are computed in `TriangulatePolygon` API.
///
/// @param[in] mesh Geom mesh
/// @param[in] name Geom Primvar name.
/// @param[in] triangulatedFaceVertexIndices Triangulated faceVertexIndices(len
/// = 3 * triangles)
/// @param[in] triangulatedToOrigFaceVertexIndexMap Triangulated faceVertexIndex
/// to original faceVertexIndex remapping table. len = 3 * triangles.
///
nonstd::expected<VertexAttribute, std::string> TriangulateGeomPrimvar(
    const GeomMesh &mesh, const std::string &name,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices,
    const std::vector<uint32_t> &triangulatedFaceVertexIndices,
    const std::vector<size_t> &triangulatedToOrigFaceVertexIndexMap) {
  GeomPrimvar primvar;

  if (triangulatedFaceVertexIndices.size() % 3 != 0) {
    return nonstd::make_unexpected(fmt::format(
        "triangulatedFaceVertexIndices.size {} must be the multiple of 3.\n",
        triangulatedFaceVertexIndices.size()));
  }

  if (!mesh.get_primvar(name, &primvar)) {
    return nonstd::make_unexpected(
        fmt::format("No primvars:{} found in GeomMesh {}\n", name, mesh.name));
  }

  if (!primvar.has_value()) {
    // TODO: Create empty VertexAttribute?
    return nonstd::make_unexpected(
        fmt::format("No value exist for primvars:{}\n", name));
  }

  //
  // Flatten Indexed PrimVar(return raw primvar for non-Indexed PrimVar)
  //
  std::string err;
  value::Value flattened;
  if (!primvar.flatten_with_indices(&flattened, &err)) {
    return nonstd::make_unexpected(fmt::format(
        "Failed to flatten Indexed PrimVar: {}. Error = {}\n", name, err));
  }

  VertexAttribute vattr;

  if (!ToVertexAttributeData(primvar, &vattr, &err)) {
    return nonstd::make_unexpected(fmt::format(
        "Failed to convert Geom PrimVar to VertexAttribute for {}. Error = {}\n", name, err));
  }

  return vattr;
}
#endif

#if 1
///
/// Input: points, faceVertexCounts, faceVertexIndices
/// Output: triangulated faceVertexCounts(all filled with 3), triangulated
/// faceVertexIndices, triangulatedToOrigFaceVertexIndexMap (length =
/// triangulated faceVertexIndices. triangulatedToOrigFaceVertexIndexMap[i]
/// stores array index in original faceVertexIndices. For remapping primvar
/// attributes.)
/// triangulatedFaceCounts: len = len(faceVertexCounts). Records the number of
/// triangle faces. 1 = triangle. 2 = quad, ... For remapping face indices(e.g.
/// GeomSubset::indices)
///
///
/// Return false when a polygon is degenerated.
/// No overlap check at the moment
///
/// T = value::float3 or value::double3
/// BaseTy = float or double
template <typename T, typename BaseTy>
bool TriangulatePolygon(
    const std::vector<T> &points, const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices,
    std::vector<uint32_t> &triangulatedFaceVertexCounts,
    std::vector<uint32_t> &triangulatedFaceVertexIndices,
    std::vector<size_t> &triangulatedToOrigFaceVertexIndexMap,
    std::vector<uint32_t> &triangulatedFaceCounts, std::string &err) {
  triangulatedFaceVertexCounts.clear();
  triangulatedFaceVertexIndices.clear();

  triangulatedToOrigFaceVertexIndexMap.clear();

  size_t faceIndexOffset = 0;

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
      triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 0);
      triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 1);
      triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 2);
      triangulatedFaceCounts.push_back(1);
#if 1
    } else if (npolys == 4) {
      // Use simple split
      // TODO: Split at shortest edge for better triangulation.
      triangulatedFaceVertexCounts.push_back(3);
      triangulatedFaceVertexCounts.push_back(3);

      triangulatedFaceVertexIndices.push_back(
          faceVertexIndices[faceIndexOffset + 0]);
      triangulatedFaceVertexIndices.push_back(
          faceVertexIndices[faceIndexOffset + 1]);
      triangulatedFaceVertexIndices.push_back(
          faceVertexIndices[faceIndexOffset + 2]);

      triangulatedFaceVertexIndices.push_back(
          faceVertexIndices[faceIndexOffset + 0]);
      triangulatedFaceVertexIndices.push_back(
          faceVertexIndices[faceIndexOffset + 2]);
      triangulatedFaceVertexIndices.push_back(
          faceVertexIndices[faceIndexOffset + 3]);

      triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 0);
      triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 1);
      triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 2);
      triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 0);
      triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 2);
      triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset + 3);
      triangulatedFaceCounts.push_back(2);
#endif
    } else {
      // Find the normal axis of the polygon using Newell's method
      T n = {BaseTy(0), BaseTy(0), BaseTy(0)};

      size_t vi0;
      size_t vi0_2;

      for (size_t k = 0; k < npolys; ++k) {
        vi0 = faceVertexIndices[faceIndexOffset + k];

        size_t j = (k + 1) % npolys;
        vi0_2 = faceVertexIndices[faceIndexOffset + j];

        if (vi0 >= points.size()) {
          err = fmt::format("Invalid vertex index.\n");
          return false;
        }

        if (vi0_2 >= points.size()) {
          err = fmt::format("Invalid vertex index.\n");
          return false;
        }

        T v0 = points[vi0];
        T v1 = points[vi0_2];

        const T point1 = {v0[0], v0[1], v0[2]};
        const T point2 = {v1[0], v1[1], v1[2]};

        T a = {point1[0] - point2[0], point1[1] - point2[1],
               point1[2] - point2[2]};
        T b = {point1[0] + point2[0], point1[1] + point2[1],
               point1[2] + point2[2]};

        n[0] += (a[1] * b[2]);
        n[1] += (a[2] * b[0]);
        n[2] += (a[0] * b[1]);
      }
      BaseTy length_n = vlength(n);
      // Check if zero length normal
      if (std::fabs(length_n) < std::numeric_limits<BaseTy>::epsilon()) {
        err = "Degenerated polygon found.\n";
        return false;
      }

      // Negative is to flip the normal to the correct direction
      n = vnormalize(n);

      T axis_w, axis_v, axis_u;
      axis_w = n;
      T a;
      if (std::fabs(axis_w[0]) > BaseTy(0.9999999)) {  // TODO: use 1.0 - eps?
        a = {BaseTy(0), BaseTy(1), BaseTy(0)};
      } else {
        a = {BaseTy(1), BaseTy(0), BaseTy(0)};
      }
      axis_v = vnormalize(vcross(axis_w, a));
      axis_u = vcross(axis_w, axis_v);

      using Point3D = std::array<BaseTy, 3>;
      using Point2D = std::array<BaseTy, 2>;
      std::vector<Point2D> polyline;

      // TMW change: Find best normal and project v0x and v0y to those
      // coordinates, instead of picking a plane aligned with an axis (which
      // can flip polygons).

      // Fill polygon data.
      for (size_t k = 0; k < npolys; k++) {
        size_t vidx = faceVertexIndices[faceIndexOffset + k];

        value::float3 v = points[vidx];
        // Point3 polypoint = {v0[0],v0[1],v0[2]};

        // world to local
        Point3D loc = {vdot(v, axis_u), vdot(v, axis_v), vdot(v, axis_w)};

        polyline.push_back({loc[0], loc[1]});
      }

      std::vector<std::vector<Point2D>> polygon_2d;
      // Single polygon only(no holes)

      std::vector<uint32_t> indices = mapbox::earcut<uint32_t>(polygon_2d);
      //  => result = 3 * faces, clockwise

      if ((indices.size() % 3) != 0) {
        // This should not be happen, though.
        err = "Failed to triangulate.\n";
        return false;
      }

      size_t ntris = indices.size() / 3;

      if (ntris > (std::numeric_limits<uint32_t>::max)()) {
        err = "Too many triangles are generated.\n";
        return false;
      }

      for (size_t k = 0; k < ntris; k++) {
        triangulatedFaceVertexCounts.push_back(3);
        triangulatedFaceVertexIndices.push_back(
            faceVertexIndices[faceIndexOffset + indices[3 * k + 0]]);
        triangulatedFaceVertexIndices.push_back(
            faceVertexIndices[faceIndexOffset + indices[3 * k + 1]]);
        triangulatedFaceVertexIndices.push_back(
            faceVertexIndices[faceIndexOffset + indices[3 * k + 2]]);

        triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset +
                                                       indices[3 * k + 0]);
        triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset +
                                                       indices[3 * k + 1]);
        triangulatedToOrigFaceVertexIndexMap.push_back(faceIndexOffset +
                                                       indices[3 * k + 2]);
      }
      triangulatedFaceCounts.push_back(uint32_t(ntris));
    }

    faceIndexOffset += npolys;
  }

  return true;
}
#endif

#if 0
//
// `Shader` may be nested, so first list up all Shader nodes under Material.
//
struct ShaderNode
{
  std::name
};

nonstd::optional<UsdPrimvarReader_float2> FindPrimvarReader_float2Rec(
  const Prim &root,
  RenderMesh &mesh)
{
  if (auto sv = root.data.as<Shader>()) {
    const Shader &shader = (*sv);

    if (auto pv = shader.value.as<UsdUVTexture>()) {
      const UsdUVTexture &tex = (*pv);
      (void)tex;
    }
  }

  for (const auto &child : root.children) {
    auto ret = ListUpShaderGraphRec(child, mesh);
    if (!ret) {
      return nonstd::make_unexpected(ret.error());
    }
  }

  return true;
}
#endif

#if 0  // not used a.t.m.
// Building an Orthonormal Basis, Revisited
// http://jcgt.org/published/0006/01/01/
static void GenerateBasis(const vec3 &n, vec3 *tangent,
                         vec3 *binormal)
{
  if (n[2] < 0.0f) {
    const float a = 1.0f / (1.0f - n[2]);
    const float b = n[0] * n[1] * a;
    (*tangent) = vec3{1.0f - n[0] * n[0] * a, -b, n[0]};
    (*binormal) = vec3{b, n[1] * n[1] * a - 1.0f, -n[1]};
  } else {
    const float a = 1.0f / (1.0f + n[2]);
    const float b = -n[0] * n[1] * a;
    (*tangent) = vec3{1.0f - n[0] * n[0] * a, b, -n[0]};
    (*binormal) = vec3{b, 1.0f - n[1] * n[1] * a, -n[1]};
  }
}

///
/// Compute facevarying tangent and facevarying binormal.
///
/// Reference:
/// http://www.opengl-tutorial.org/intermediate-tutorials/tutorial-13-normal-mapping
///
/// Implemented code uses two adjacent edge composed from three vertices v_{i}, v_{i+1}, v_{i+2} for i < (N - 1)
/// , where N is the number of vertices per facet.
///
/// This may produce unwanted tangent/binormal frame for ill-defined polygon(quad, pentagon, ...).
/// Also, we assume input mesh has well-formed and has no or few vertices with similar property(position, uvs and normals)
///
/// TODO:
/// - [ ] Implement getSimilarVertexIndex in the above opengl-tutorial to better average tangent/binormal.
///   - https://github.com/huamulan/OpenGL-tutorial/blob/master/common/vboindexer.cpp uses simple linear-search,
///   - so could become quite slow when the input mesh has large number of vertices.
///   - Use kNN search(e.g. nanoflann https://github.com/jlblancoc/nanoflann ), or point-query by building BVH over the mesh points.
///     - BVH builder candidate:
///       - NanoRT https://github.com/lighttransport/nanort
///       - bvh https://github.com/madmann91/bvh
///   - Or we can quantize vertex attributes and compute locally sensitive hashsing? https://dl.acm.org/doi/10.1145/3188745.3188846
/// - [ ] Implement more robust tangent/binormal frame computation algorithm for arbitrary mesh?
///
///
static bool ComputeTangentsAndBinormals(
    const std::vector<vec3> &vertices,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices,
    const std::vector<vec2> &facevarying_texcoords,
    const std::vector<vec3> &facevarying_normals,
    std::vector<vec3> *facevarying_tangents,
    std::vector<vec3> *facevarying_binormals,
    std::string *err) {


  if (vertices.empty()) {
    SET_ERROR_AND_RETURN("vertices is empty.");
  }

  // At least 1 triangle face should exist.
  if (faceVertexIndices.size() < 9) {
    SET_ERROR_AND_RETURN("faceVertexIndices.size < 9");
  }

  if (facevarying_texcoords.empty()) {
    SET_ERROR_AND_RETURN("facevarying_texcoords is empty");
  }

  if (facevarying_normals.empty()) {
    SET_ERROR_AND_RETURN("facevarying_normals is empty");
  }

  std::vector<value::normal3f> tn(vertices.size());
  memset(&tn.at(0), 0, sizeof(value::normal3f) * tn.size());
  std::vector<value::normal3f> bn(vertices.size());
  memset(&bn.at(0), 0, sizeof(value::normal3f) * bn.size());


  bool hasFaceVertexCounts{true};

  size_t num_faces = faceVertexCounts.size();
  if (num_faces == 0) {
    // Assume all triangle faces.
    if ((faceVertexIndices.size() % 3) != 0) {
      SET_ERROR_AND_RETURN("Invalid faceVertexIndices. It must be all triangles: faceVertexIndices.size % 3 == 0");
    }

    num_faces = faceVertexIndices.size() / 3;
    hasFaceVertexCounts = false;
  }

  //
  // 1. Compute tangent/binormal for each faceVertex.
  //
  size_t faceVertexIndexOffset{0};
  for (size_t i = 0; i < num_faces; i++) {
    size_t nv = hasFaceVertexCounts ? faceVertexCounts[i] : 3;

    if ((faceVertexIndexOffset + nv) >= faceVertexIndices.size()) {
      // Invalid faceVertexIndices
      SET_ERROR_AND_RETURN("Invalid value in faceVertexOffset.");
    }

    if (nv < 3) {
      SET_ERROR_AND_RETURN("Degenerated facet found.");
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

      size_t fid0 = f;
      size_t fid1 = f+1;
      size_t fid2 = f+2;

      uint32_t vf0 = faceVertexIndices[fid0];
      uint32_t vf1 = faceVertexIndices[fid1];
      uint32_t vf2 = faceVertexIndices[fid2];

      if ((vf0 >= vertices.size()) ||
          (vf1 >= vertices.size()) ||
          (vf2 >= vertices.size()) ) {

        // index out-of-range
        SET_ERROR_AND_RETURN("Invalid value in faceVertexIndices. some exceeds vertices.size()");
      }

      vec3 v1 = vertices[vf0];
      vec3 v2 = vertices[vf1];
      vec3 v3 = vertices[vf2];

      float v1x = v1[0];
      float v1y = v1[1];
      float v1z = v1[2];

      float v2x = v2[0];
      float v2y = v2[1];
      float v2z = v2[2];

      float v3x = v3[0];
      float v3y = v3[1];
      float v3z = v3[2];

      float w1x = 0.0f;
      float w1y = 0.0f;
      float w2x = 0.0f;
      float w2y = 0.0f;
      float w3x = 0.0f;
      float w3y = 0.0f;

      if ((fid0 >= facevarying_texcoords.size()) ||
          (fid1 >= facevarying_texcoords.size()) ||
          (fid2 >= facevarying_texcoords.size()) ) {

        // index out-of-range
        SET_ERROR_AND_RETURN("Invalid value in faceVertexCounts. some exceeds facevarying_texcoords.size()");
      }

      {
        vec2 uv1 = facevarying_texcoords[fid0];
        vec2 uv2 = facevarying_texcoords[fid1];
        vec2 uv3 = facevarying_texcoords[fid2];

        w1x = uv1[0];
        w1y = uv1[1];
        w2x = uv2[0];
        w2y = uv2[1];
        w3x = uv3[0];
        w3y = uv3[1];
      }

      float x1 = v2x - v1x;
      float x2 = v3x - v1x;
      float y1 = v2y - v1y;
      float y2 = v3y - v1y;
      float z1 = v2z - v1z;
      float z2 = v3z - v1z;

      float s1 = w2x - w1x;
      float s2 = w3x - w1x;
      float t1 = w2y - w1y;
      float t2 = w3y - w1y;

      float r = 1.0;

      if (std::fabs(double(s1 * t2 - s2 * t1)) > 1.0e-20) {
        r /= (s1 * t2 - s2 * t1);
      }

      vec3 tdir{(t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r,
                  (t2 * z1 - t1 * z2) * r};
      vec3 bdir{(s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r,
                  (s1 * z2 - s2 * z1) * r};


      tn[vf0][0] += tdir[0];
      tn[vf0][1] += tdir[1];
      tn[vf0][2] += tdir[2];

      tn[vf1][0] += tdir[0];
      tn[vf1][1] += tdir[1];
      tn[vf1][2] += tdir[2];

      tn[vf2][0] += tdir[0];
      tn[vf2][1] += tdir[1];
      tn[vf2][2] += tdir[2];

      bn[vf0][0] += bdir[0];
      bn[vf0][1] += bdir[1];
      bn[vf0][2] += bdir[2];

      bn[vf1][0] += bdir[0];
      bn[vf1][1] += bdir[1];
      bn[vf1][2] += bdir[2];

      bn[vf2][0] += bdir[0];
      bn[vf2][1] += bdir[1];
      bn[vf2][2] += bdir[2];
    }

    faceVertexIndexOffset += nv;
  }

  //
  // 2. normalize * orthogonalize;
  //
  facevarying_tangents->resize(facevarying_normals.size());
  facevarying_binormals->resize(facevarying_normals.size());

  faceVertexIndexOffset = 0;
  for (size_t i = 0; i < num_faces; i++) {
    size_t nv = hasFaceVertexCounts ? faceVertexCounts[i] : 3;

    for (size_t f = 0; f < nv - 2; f++) {
      uint32_t vf[3];

      vf[0] = faceVertexIndices[faceVertexIndexOffset + f];
      vf[1] = faceVertexIndices[faceVertexIndexOffset + f + 1];
      vf[2] = faceVertexIndices[faceVertexIndexOffset + f + 2];

      value::normal3f n[3];

      // http://www.terathon.com/code/tangent.html

      {
        n[0][0] = facevarying_normals[faceVertexIndexOffset + f + 0][0];
        n[0][1] = facevarying_normals[faceVertexIndexOffset + f + 0][1];
        n[0][2] = facevarying_normals[faceVertexIndexOffset + f + 0][2];

        n[1][0] = facevarying_normals[faceVertexIndexOffset + f + 1][0];
        n[1][1] = facevarying_normals[faceVertexIndexOffset + f + 1][1];
        n[1][2] = facevarying_normals[faceVertexIndexOffset + f + 1][2];

        n[2][0] = facevarying_normals[faceVertexIndexOffset + f + 2][0];
        n[2][1] = facevarying_normals[faceVertexIndexOffset + f + 2][1];
        n[2][2] = facevarying_normals[faceVertexIndexOffset + f + 2][2];
      }

      size_t dst_idx[3];
      dst_idx[0] = f;
      dst_idx[1] = f+1;
      dst_idx[2] = f+2;

      for (size_t k = 0; k < 3; k++) {
        value::normal3f Tn = tn[vf[k]];
        value::normal3f Bn = bn[vf[k]];

        if (vlength(Bn) > 0.0f) {
          Bn = vnormalize(Bn);
        }

        // Gram-Schmidt orthogonalize
        Tn = (Tn - n[k] * vdot(n[k], Tn));
        if (vlength(Tn) > 0.0f) {
          Tn = vnormalize(Tn);
        }

        // Calculate handedness
        if (vdot(vcross(n[k], Tn), Bn) < 0.0f) {
          Tn = Tn * -1.0f;
        }

        ((*facevarying_tangents)[faceVertexIndexOffset + dst_idx[k]])[0] = Tn[0];
        ((*facevarying_tangents)[faceVertexIndexOffset + dst_idx[k]])[1] = Tn[1];
        ((*facevarying_tangents)[faceVertexIndexOffset + dst_idx[k]])[2] = Tn[2];

        ((*facevarying_binormals)[faceVertexIndexOffset + dst_idx[k]])[0] = Bn[0];
        ((*facevarying_binormals)[faceVertexIndexOffset + dst_idx[k]])[1] = Bn[1];
        ((*facevarying_binormals)[faceVertexIndexOffset + dst_idx[k]])[2] = Bn[2];
      }
    }

    faceVertexIndexOffset += nv;
  }

  return true;
}
#endif

}  // namespace

#if 0
// Currently float2 only
std::vector<UsdPrimvarReader_float2> ExtractPrimvarReadersFromMaterialNode(
    const Prim &node) {
  std::vector<UsdPrimvarReader_float2> dst;

  if (!node.is<Material>()) {
    return dst;
  }

  for (const auto &child : node.children()) {
    (void)child;
  }

  // Traverse and find PrimvarReader_float2 shader.
  return dst;
}

nonstd::expected<Node, std::string> Convert(const Stage &stage,
                                            const Xform &xform) {
  (void)stage;

  // TODO: timeSamples

  Node node;
  if (auto m = xform.GetLocalMatrix()) {
    node.local_matrix = m.value();
  }

  return std::move(node);
}
#endif

namespace {

bool ListUVNames(const RenderMaterial &material,
                 const std::vector<UVTexture> &textures,
                 StringAndIdMap &si_map) {
  // TODO: Use auto
  auto fun_vec3 = [&](const ShaderParam<vec3> &param) {
    int32_t texId = param.textureId;
    if ((texId >= 0) && (size_t(texId) < textures.size())) {
      const UVTexture &tex = textures[size_t(texId)];
      if (tex.varname_uv.size()) {
        if (!si_map.count(tex.varname_uv)) {
          uint64_t slotId = si_map.size();
          DCOUT("Add textureSlot: " << tex.varname_uv << ", " << slotId);
          si_map.add(tex.varname_uv, slotId);
        }
      }
    }
  };

  auto fun_float = [&](const ShaderParam<float> &param) {
    int32_t texId = param.textureId;
    if ((texId >= 0) && (size_t(texId) < textures.size())) {
      const UVTexture &tex = textures[size_t(texId)];
      if (tex.varname_uv.size()) {
        if (!si_map.count(tex.varname_uv)) {
          uint64_t slotId = si_map.size();
          DCOUT("Add textureSlot: " << tex.varname_uv << ", " << slotId);
          si_map.add(tex.varname_uv, slotId);
        }
      }
    }
  };

  fun_vec3(material.surfaceShader.diffuseColor);
  fun_vec3(material.surfaceShader.normal);
  fun_float(material.surfaceShader.metallic);
  fun_float(material.surfaceShader.roughness);
  fun_float(material.surfaceShader.clearcoat);
  fun_float(material.surfaceShader.clearcoatRoughness);
  fun_float(material.surfaceShader.opacity);
  fun_float(material.surfaceShader.opacityThreshold);
  fun_float(material.surfaceShader.ior);
  fun_float(material.surfaceShader.displacement);
  fun_float(material.surfaceShader.occlusion);

  return true;
}

}  // namespace

bool RenderSceneConverter::ConvertVertexVariabilityImpl(
    const VertexAttribute &vattr, const bool to_vertex_varying,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices, VertexAttribute &dst) {
  if (vattr.variability == VertexVariability::Uniform) {
    if (to_vertex_varying) {
      auto result = UniformToVertex(vattr.get_data(), vattr.stride_bytes(),
                                    faceVertexCounts, faceVertexIndices);

      if (!result) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Convert `normals` attribute with uniform-varying "
                        "to vertex-varying failed: {}",
                        result.error()));
      }

      dst.data = result.value();
      dst.elementSize = vattr.elementSize;
      dst.stride = vattr.stride;
      dst.variability = VertexVariability::Vertex;
      dst.format = vattr.format;

    } else {
      auto result = UniformToFaceVarying(vattr.get_data(), vattr.stride_bytes(),
                                         faceVertexCounts);
      if (!result) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Convert uniform `normals` attribute to failed: {}",
                        result.error()));
      }

      dst.data = result.value();
      dst.elementSize = vattr.elementSize;
      dst.stride = vattr.stride;
      dst.variability = VertexVariability::FaceVarying;
      dst.format = vattr.format;
    }
  } else if (vattr.variability == VertexVariability::Constant) {
    if (to_vertex_varying) {
      auto result = ConstantToVertex(vattr.get_data(), vattr.stride_bytes(),
                                     faceVertexCounts, faceVertexIndices);

      if (!result) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Convert `normals` attribute with uniform-varying "
                        "to vertex-varying failed: {}",
                        result.error()));
      }

      dst.data = result.value();
      dst.elementSize = vattr.elementSize;
      dst.stride = vattr.stride;
      dst.variability = VertexVariability::Vertex;
      dst.format = vattr.format;

    } else {
      auto result = UniformToFaceVarying(vattr.get_data(), vattr.stride_bytes(),
                                         faceVertexCounts);
      if (!result) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Convert uniform `normals` attribute to failed: {}",
                        result.error()));
      }

      dst.data = result.value();
      dst.elementSize = vattr.elementSize;
      dst.stride = vattr.stride;
      dst.variability = VertexVariability::FaceVarying;
      dst.format = vattr.format;
    }

  } else if ((vattr.variability == VertexVariability::Vertex) ||
             (vattr.variability == VertexVariability::Varying)) {
    if (to_vertex_varying) {
      dst = vattr;
    } else {
      auto result = VertexToFaceVarying(vattr.get_data(), vattr.stride_bytes(),
                                        faceVertexCounts, faceVertexIndices);
      if (!result) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Convert vertex/varying `normals` attribute to failed: {}",
            result.error()));
      }

      dst.data = result.value();
      dst.elementSize = vattr.elementSize;
      dst.stride = vattr.stride;
      dst.variability = VertexVariability::FaceVarying;
      dst.format = vattr.format;
    }

  } else if (vattr.variability == VertexVariability::FaceVarying) {
    if (to_vertex_varying) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. `to_vertex_varying` should not be true when "
          "FaceVarying.");
    }

    std::vector<uint8_t> buf;
    buf.resize(vattr.stride_bytes());

    size_t nsrcs = vattr.vertex_count();

    if (faceVertexIndices.empty()) {
      dst.data = vattr.get_data();
    } else {
      dst.data.clear();
      // rearrange
      for (size_t i = 0; i < faceVertexIndices.size(); i++) {
        size_t vidx = faceVertexIndices[i];

        if (vidx >= nsrcs) {
          PUSH_ERROR_AND_RETURN(
              "Internal error. Invalid index in faceVertexIndices.");
        }

        memcpy(buf.data(),
               vattr.get_data().data() + vidx * vattr.stride_bytes(),
               vattr.stride_bytes());

        dst.data.insert(dst.data.end(), buf.begin(), buf.end());
      }
    }

    dst.elementSize = vattr.elementSize;
    dst.stride = vattr.stride;
    dst.variability = VertexVariability::FaceVarying;
    dst.format = vattr.format;
  } else {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Unsupported/unimplemented interpolation: {} ",
                    to_string(vattr.variability)));
  }

  return true;
}

bool RenderSceneConverter::ConvertMesh(
    const Path &abs_path, const GeomMesh &mesh,
    const MaterialPath &material_path,
    const std::map<std::string, MaterialPath> &subset_material_path_map,
    const std::map<std::string, int64_t> &rmaterial_idMap,
    const std::vector<const tinyusdz::GeomSubset *> &material_subsets,
    const std::vector<std::pair<std::string, const tinyusdz::BlendShape *>>
        &blendshapes,
    RenderMesh *dstMesh) {

  //
  // Steps:
  //
  // - Get points, faceVertexIndices and faceVertexOffsets at specified time.
  // - Validate GeomSubsets
  // - Assign Material and list up texcoord primvars
  // - convert texcoord, normals, vetexcolor(displaycolors)
  //   - First try to convert it to `vertex` varying(Can be drawn with single
  //   index buffer)
  //   - Otherwise convert to `facevarying` as the last resort.
  // - Triangulate indices  when `triangulate` is enabled.
  //   - Triangulate texcoord, normals, vertexcolor.
  // - Convert Skin weights
  // - Convert BlendShape
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

  {
    std::vector<value::point3f> points;
    bool ret = EvaluateTypedAnimatableAttribute(*_stage, mesh.points, "points", &points, &_err, _timecode, value::TimeSampleInterpolationType::Linear);
    if (!ret) {
      return false;
    }

    if (points.empty()) {
      PUSH_ERROR_AND_RETURN(fmt::format("`points` is empty. Prim {}", abs_path));
    }

    dst.points.resize(points.size());
    memcpy(dst.points.data(), points.data(),
           sizeof(value::float3) * points.size());

  }

  {
    std::vector<int32_t> indices;
    bool ret = EvaluateTypedAnimatableAttribute(*_stage, mesh.faceVertexIndices, "faceVertexIndices", &indices, &_err, _timecode, value::TimeSampleInterpolationType::Held);
    if (!ret) {
      return false;
    }

    for (size_t i = 0; i < indices.size(); i++) {
      if (indices[i] < 0) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "faceVertexIndices[{}] contains negative index value {}.", i,
            indices[i]));
      }
      if (size_t(indices[i]) > dst.points.size()) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "faceVertexIndices[{}] {} exceeds points.size {}.", i, indices[i], dst.points.size()));
      }
      dst.faceVertexIndices.push_back(uint32_t(indices[i]));
    }
  }

  {
    std::vector<int> counts;
    bool ret = EvaluateTypedAnimatableAttribute(*_stage, mesh.faceVertexCounts, "faceVertexCounts", &counts, &_err, _timecode, value::TimeSampleInterpolationType::Held);
    if (!ret) {
      return false;
    }

    size_t sumCounts = 0;
    dst.faceVertexCounts.clear();
    for (size_t i = 0; i < counts.size(); i++) {
      if (counts[i] < 3) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("faceVertexCounts[{}] contains invalid value {}. The "
                        "count value must be >= 3",
                        i, counts[i]));
      }

      if ((sumCounts + size_t(counts[i])) > dst.faceVertexIndices.size()) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("faceVertexCounts[{}] exceeds faceVertexIndices.size {}.",
                        i, dst.faceVertexIndices.size()));
      }
      dst.faceVertexCounts.push_back(uint32_t(counts[i]));
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

  if (rmaterial_idMap.count(material_path.material_path)) {
    dst.material_id = int(rmaterial_idMap.at(material_path.material_path));
  }

  if (rmaterial_idMap.count(material_path.backface_material_path)) {
    dst.backface_material_id =
        int(rmaterial_idMap.at(material_path.backface_material_path));
  }

  if (_mesh_config.validate_geomsubset) {
    size_t elementCount = dst.faceVertexCounts.size();

    if (material_subsets.size() &&
        mesh.subsetFamilyTypeMap.count(value::token("materialBind"))) {
      const GeomSubset::FamilyType familyType =
          mesh.subsetFamilyTypeMap.at(value::token("materialBind"));
      if (!GeomSubset::ValidateSubsets(material_subsets, elementCount,
                                       familyType, &_err)) {
        PUSH_ERROR_AND_RETURN("GeomSubset validation failed.");
      }
    }
  }

  for (const auto &psubset : material_subsets) {
    MaterialSubset ms;
    ms.prim_name = psubset->name;
    //ms.prim_index = // TODO
    ms.abs_path = abs_path.prim_part() + std::string("/") + psubset->name;
    ms.display_name = psubset->meta.displayName.value_or("");

    // TODO: Raise error when indices is empty?
    if (psubset->indices.authored()) {

      std::vector<int> indices; // index to faceVertexCounts
      bool ret = EvaluateTypedAnimatableAttribute(*_stage, psubset->indices, "indices", &indices, &_err, _timecode, value::TimeSampleInterpolationType::Held);
      if (!ret) {
        return false;
      }

      ms.indices = indices;
    }

    if (subset_material_path_map.count(psubset->name)) {
      const auto &mp = subset_material_path_map.at(psubset->name);
      if (rmaterial_idMap.count(mp.material_path)) {
        ms.material_id = int(rmaterial_idMap.at(mp.material_path));
      }
      if (rmaterial_idMap.count(mp.backface_material_path)) {
        ms.backface_material_id = int(rmaterial_idMap.at(mp.backface_material_path));
      }
    }

    // TODO: Ensure prim_name is unique.
    dst.material_subsetMap[ms.prim_name] = ms;
  }

  bool triangulate = _mesh_config.triangulate;

  if (triangulate) {
    std::vector<uint32_t> triangulatedFaceVertexCounts;  // should be all 3's
    std::vector<uint32_t> triangulatedFaceVertexIndices;
    std::vector<size_t>
        triangulatedToOrigFaceVertexIndexMap;  // used for rearrange facevertex
                                               // attrib
    std::vector<uint32_t>
        triangulatedFaceCounts;  // used for rearrange face indices(e.g
                                 // GeomSubset indices)

    std::string err;

    if (!TriangulatePolygon<value::float3, float>(
            dst.points, dst.faceVertexCounts, dst.faceVertexIndices,
            triangulatedFaceVertexCounts, triangulatedFaceVertexIndices,
            triangulatedToOrigFaceVertexIndexMap, triangulatedFaceCounts,
            err)) {
      PUSH_ERROR_AND_RETURN("Triangulation failed: " + err);
    }

    if (dst.material_subsetMap.size()) {
      // Juist in case.
      if (dst.faceVertexCounts.size() != triangulatedFaceCounts.size()) {
        PUSH_ERROR_AND_RETURN("Internal error in triangulation logic.");
      }


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

        faceIndexOffset += ncount;

        if (faceIndexOffset >= std::numeric_limits<uint32_t>::max()) {
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

        for (size_t i = 0; i < it.second.indices.size(); i++) {
          int32_t srcIndex = it.second.indices[i];
          if (srcIndex < 0) {
            PUSH_ERROR_AND_RETURN("Invalid index value in GeomSubset.");
          }

          uint32_t baseFaceIndex = faceIndexOffsets[i];

          for (size_t k = 0; k < triangulatedFaceCounts[uint32_t(srcIndex)];
               k++) {
            if ((baseFaceIndex + k) > (std::numeric_limits<int32_t>::max)()) {
              PUSH_ERROR_AND_RETURN(fmt::format("Index value exceeds 2GB."));
            }
            // assume faceIndex in each polygon is monotonically increasing.
            triangulated_indices.push_back(int(baseFaceIndex + k));
          }
        }
      }
    }

    dst.faceVertexCounts = std::move(triangulatedFaceVertexCounts);
    dst.faceVertexIndices = std::move(triangulatedFaceVertexIndices);

    dst.triangulatedToOrigFaceVertexIndexMap =
        std::move(triangulatedToOrigFaceVertexIndexMap);
    dst.triangulatedFaceCounts = std::move(triangulatedFaceCounts);

  }  // end triangulate

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
  if (rmaterial_idMap.empty()) {
    // No material assigned to the Mesh, but we may still want texcoords solely(
    // assign material after the conversion)
    // So find a primvar whose name matches default texcoord name.
    if (mesh.has_primvar(_mesh_config.default_texcoords_primvar_name)) {
      DCOUT("uv primvar  with default_texcoords_primvar_name found.");
      auto ret = GetTextureCoordinate(
          *_stage, mesh, _mesh_config.default_texcoords_primvar_name);
      if (ret) {
        const VertexAttribute vattr = ret.value();

        // Use slotId 0
        uvAttrs[0] = vattr;
      }
    }
  } else {
    for (auto rmat : rmaterial_idMap) {
      int64_t rmaterial_id = rmat.second;

      if ((rmaterial_id > -1) && (size_t(rmaterial_id) < materials.size())) {
        const RenderMaterial &material = materials[size_t(rmaterial_id)];

        StringAndIdMap uvname_map;
        if (!ListUVNames(material, textures, uvname_map)) {
          return false;
        }

        for (auto it = uvname_map.i_begin(); it != uvname_map.i_end(); it++) {
          uint64_t slotId = it->first;
          std::string uvname = it->second;

          if (!uvAttrs.count(uint32_t(slotId))) {
            auto ret = GetTextureCoordinate(*_stage, mesh, uvname);
            if (ret) {
              const VertexAttribute vattr = ret.value();

              uvAttrs[uint32_t(slotId)] = vattr;
            }
          }
        }
      }
    }
  }

  if (mesh.has_primvar("displayColor")) {
    GeomPrimvar pvar;
    if (!mesh.get_primvar("displayColor", &pvar)) {
      PUSH_ERROR_AND_RETURN("Failed to get `displayColor` primvar.");
    }

    std::vector<value::color3f> displayColors;
    if (!pvar.flatten_with_indices(&displayColors, &_err)) {
      PUSH_ERROR_AND_RETURN("Failed to expand `displayColor` primvar.");
    }

    if (displayColors.size() == 1) {
      dst.displayColor = displayColors[0];
    } else {
      dst.vertex_colors.elementSize = 1;
      dst.vertex_colors.format = VertexAttributeFormat::Float;
      dst.vertex_colors.stride = 0;

      if (pvar.get_interpolation() == Interpolation::Varying) {
        dst.vertex_colors.variability = VertexVariability::Varying;
      } else if (pvar.get_interpolation() == Interpolation::Constant) {
        dst.vertex_colors.variability = VertexVariability::Constant;
      } else if (pvar.get_interpolation() == Interpolation::Uniform) {
        dst.vertex_colors.variability = VertexVariability::Uniform;
      } else if (pvar.get_interpolation() == Interpolation::Vertex) {
        dst.vertex_colors.variability = VertexVariability::Vertex;
      } else if (pvar.get_interpolation() == Interpolation::FaceVarying) {
        dst.vertex_colors.variability = VertexVariability::FaceVarying;
      }
      dst.vertex_colors.indices.clear();
      dst.vertex_colors.name = "displayColor";
    }
  }

  if (mesh.has_primvar("displayOpacity")) {
    GeomPrimvar pvar;
    if (!mesh.get_primvar("displayOpacity", &pvar)) {
      PUSH_ERROR_AND_RETURN("Failed to get `displayOpacity` primvar.");
    }

    std::vector<float> displayOpacity;
    if (!pvar.flatten_with_indices(&displayOpacity, &_err)) {
      PUSH_ERROR_AND_RETURN("Failed to expand `displayColor` primvar.");
    }

    if (displayOpacity.size() == 1) {
      dst.displayOpacity = displayOpacity[0];
    } else {
      dst.vertex_opacities.elementSize = 1;
      dst.vertex_opacities.format = VertexAttributeFormat::Float;
      dst.vertex_opacities.stride = 0;

      if (pvar.get_interpolation() == Interpolation::Varying) {
        dst.vertex_opacities.variability = VertexVariability::Varying;
      } else if (pvar.get_interpolation() == Interpolation::Constant) {
        dst.vertex_opacities.variability = VertexVariability::Constant;
      } else if (pvar.get_interpolation() == Interpolation::Uniform) {
        dst.vertex_opacities.variability = VertexVariability::Uniform;
      } else if (pvar.get_interpolation() == Interpolation::Vertex) {
        dst.vertex_opacities.variability = VertexVariability::Vertex;
      } else if (pvar.get_interpolation() == Interpolation::FaceVarying) {
        dst.vertex_opacities.variability = VertexVariability::FaceVarying;
      }
      dst.vertex_opacities.indices.clear();
      dst.vertex_opacities.name = "displayOpacity";
    }
  }

  //
  // Check if the Mesh can be drawn with single index buffer,
  // since OpenGL and Vulkan does not support drawing a Primitive with multiple
  // index buffers.
  //
  // The check means that normal and texcoord are not face-varying attribute.
  // If the Mesh contains any face-varying attribute, all attribute are
  // converted to face-varying so that the Mesh can be drawn without index
  // buffer. This will hurt the performance of rendering in OpenGL/Vulkan,
  // especially when the Mesh is animated with skinning.
  //
  // We leave user-defined primvar as-is, so no check for it.
  //
  bool is_single_indexable{true};
  {
    Interpolation interp = mesh.get_normalsInterpolation();
    if (interp == Interpolation::FaceVarying) {
      is_single_indexable = false;
    }

    for (const auto &uv : uvAttrs) {
      // 'indexed' should not appear, just in case.
      if ((uv.second.variability == VertexVariability::Varying) ||
          (uv.second.variability == VertexVariability::Indexed)) {
        is_single_indexable = false;
      }
    }

    if (dst.vertex_colors.vertex_count() > 1) {
      if ((dst.vertex_colors.variability == VertexVariability::Varying) ||
          (dst.vertex_colors.variability == VertexVariability::Indexed)) {
        is_single_indexable = false;
      }
    }

    if (dst.vertex_opacities.vertex_count() > 1) {
      if ((dst.vertex_opacities.variability == VertexVariability::Varying) ||
          (dst.vertex_opacities.variability == VertexVariability::Indexed)) {
        is_single_indexable = false;
      }
    }
  }

  //
  // Convert normals
  //
  {
    std::vector<value::normal3f> normals = mesh.get_normals();
    Interpolation interp = mesh.get_normalsInterpolation();

    if (normals.size()) {
      if (interp == Interpolation::Uniform) {
        if (is_single_indexable) {
          auto result =
              UniformToVertex(normals, /* elementSize */ 1,
                              dst.faceVertexCounts, dst.faceVertexIndices);

          if (!result) {
            PUSH_ERROR_AND_RETURN(
                fmt::format("Convert `normals` attribute with uniform-varying "
                            "to vertex-varying failed: {}",
                            result.error()));
          }

          dst.normals.get_data().resize(result.value().size() *
                                        sizeof(value::normal3f));
          memcpy(dst.normals.get_data().data(), result.value().data(),
                 result.value().size() * sizeof(value::normal3f));
          dst.normals.elementSize = 1;
          dst.normals.stride = sizeof(value::normal3f);
          dst.normals.variability = VertexVariability::Vertex;
          dst.normals.format = VertexAttributeFormat::Vec3;

        } else {
          auto result = UniformToFaceVarying(normals, dst.faceVertexCounts);
          if (!result) {
            PUSH_ERROR_AND_RETURN(
                fmt::format("Convert uniform `normals` attribute to failed: {}",
                            result.error()));
          }

          dst.normals.get_data().resize(result.value().size() *
                                        sizeof(value::normal3f));
          memcpy(dst.normals.get_data().data(), result.value().data(),
                 sizeof(value::normal3f) * result.value().size());
          dst.normals.elementSize = 1;
          dst.normals.stride = sizeof(value::normal3f);
          dst.normals.variability = VertexVariability::FaceVarying;
          dst.normals.format = VertexAttributeFormat::Vec3;
        }

      } else if ((interp == Interpolation::Vertex) ||
                 (interp == Interpolation::Varying)) {
        if (is_single_indexable) {
          dst.normals.get_data().resize(normals.size() *
                                        sizeof(value::normal3f));
          memcpy(dst.normals.get_data().data(), normals.data(),
                 sizeof(value::normal3f) * normals.size());
          dst.normals.elementSize = 1;
          dst.normals.stride = sizeof(value::normal3f);
          dst.normals.variability = VertexVariability::Vertex;
          dst.normals.format = VertexAttributeFormat::Vec3;

        } else {
          auto result = VertexToFaceVarying(normals, dst.faceVertexCounts,
                                            dst.faceVertexIndices);
          if (!result) {
            PUSH_ERROR_AND_RETURN(fmt::format(
                "Convert vertex/varying `normals` attribute to failed: {}",
                result.error()));
          }

          dst.normals.get_data().resize(result.value().size() *
                                        sizeof(value::normal3f));
          memcpy(dst.normals.get_data().data(), result.value().data(),
                 sizeof(value::normal3f) * result.value().size());
          dst.normals.elementSize = 1;
          dst.normals.stride = sizeof(value::normal3f);
          dst.normals.variability = VertexVariability::FaceVarying;
          dst.normals.format = VertexAttributeFormat::Vec3;
        }

      } else if (interp == Interpolation::FaceVarying) {
        if (is_single_indexable) {
          PUSH_ERROR_AND_RETURN(
              "Internal error. `is_single_indexable` should not be true when "
              "FaceVarying.");
        }

        if (triangulate) {
          size_t nsrcs = normals.size() / sizeof(value::normal3f);
          value::normal3f *src_ptr =
              reinterpret_cast<value::normal3f *>(normals.data());

          std::vector<value::normal3f> triangulated_fvnormals;

          // rearrange normals
          for (size_t i = 0;
               i < dst.triangulatedToOrigFaceVertexIndexMap.size(); i++) {
            size_t vidx = dst.triangulatedToOrigFaceVertexIndexMap[i];

            if (vidx >= nsrcs) {
              PUSH_ERROR_AND_RETURN("Internal error. Invalid triangulation.");
            }

            triangulated_fvnormals.push_back(src_ptr[vidx]);
          }

          dst.normals.get_data().resize(triangulated_fvnormals.size() *
                                        sizeof(value::normal3f));
          memcpy(dst.normals.get_data().data(), triangulated_fvnormals.data(),
                 sizeof(value::normal3f) * triangulated_fvnormals.size());

        } else {
          dst.normals.get_data().resize(normals.size() *
                                        sizeof(value::normal3f));
          memcpy(dst.normals.get_data().data(), normals.data(),
                 sizeof(value::normal3f) * normals.size());
        }

        dst.normals.elementSize = 1;
        dst.normals.stride = sizeof(value::normal3f);
        dst.normals.variability = VertexVariability::FaceVarying;
        dst.normals.format = VertexAttributeFormat::Vec3;
      } else {
        PUSH_ERROR_AND_RETURN(
            "Unsupported/unimplemented interpolation for `normals` "
            "attribute: " +
            to_string(interp) + ".\n");
      }
    } else {
      dst.normals.get_data().clear();
      dst.normals.stride = 0;
    }
  }

  // Compute total faceVarying elements;
  size_t num_fvs = 0;
  for (size_t i = 0; i < dst.faceVertexCounts.size(); i++) {
    num_fvs += dst.faceVertexCounts[i];
  }
  (void)num_fvs;

  DCOUT("num_fvs = " << num_fvs);

  //
  // Convert UVs
  //

  for (const auto &it : uvAttrs) {
    uint64_t slotId = it.first;
    const VertexAttribute &vattr = it.second;

    if (vattr.format != VertexAttributeFormat::Vec2) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Texcoord VertexAttribute must be Vec2 type.\n"));
    }

    if (vattr.element_size() != 1) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("elementSize must be 1 for Texcoord attribute."));
    }

    DCOUT("Add texcoord attr `" << vattr.name << "` to slot Id " << slotId);

#if 0
    if (vattr.variability == VertexVariability::Constant) {
      auto result = ConstantToFaceVarying(
          vattr.get_data(), vattr.stride_bytes(), dst.faceVertexCounts);
      if (!result) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Failed to convert 'constant' attribute to 'facevarying': {}",
            result.error()));
      }

      VertexAttribute uvAttr;
      uvAttr.get_data().resize(result.value().size() * sizeof(vec2));
      memcpy(uvAttr.get_data().data(), result.value().data(),
             result.value().size() * sizeof(vec2));

      uvAttr.elementSize = 1;
      uvAttr.format = vattr.format;
      uvAttr.variability = VertexAttribute::V
      dst.texcoords[uint32_t(slotId)] = uvAttr;

    } else if (vattr.variability == VertexVariability::Uniform) {
      auto result = UniformToFaceVarying(vattr.get_data(), vattr.stride_bytes(),
                                         dst.faceVertexCounts);
      if (!result) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Failed to convert 'uniform' attribute to 'facevarying': {}",
            result.error()));
      }

      VertexAttribute uvAttr;
      uvAttr.get_data().resize(result.value().size() * sizeof(vec2));
      memcpy(uvAttr.get_data().data(), result.value().data(),
             result.value().size() * sizeof(vec2));

      dst.texcoords[uint32_t(slotId)] = uvAttr;
    } else if ((vattr.variability == VertexVariability::Varying) ||
               (vattr.variability == VertexVariability::Vertex)) {
      auto result =
          VertexToFaceVarying(vattr.get_data(), vattr.stride_bytes(),
                              dst.faceVertexCounts, dst.faceVertexIndices);
      if (!result) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Failed to convert 'vertex' or 'varying' attribute to "
                        "'facevarying': {}",
                        result.error()));
      }

      VertexAttribute uvAttr;
      uvAttr.get_data().resize(result.value().size() * sizeof(vec2));
      memcpy(uvAttr.get_data().data(), result.value().data(),
             result.value().size() * sizeof(vec2));

      dst.texcoords[uint32_t(slotId)] = uvAttr;
    } else if (vattr.variability == VertexVariability::FaceVarying) {
      if (triangulate) {
        size_t nsrcs = vattr.get_data().size() / sizeof(vec2);
        const vec2 *src_ptr =
            reinterpret_cast<const vec2 *>(vattr.get_data().data());

        std::vector<vec2> triangulated_fvtexcoords;

        // rearrange indices
        for (size_t i = 0; i < dst.triangulatedToOrigFaceVertexIndexMap.size();
             i++) {
          size_t vidx = dst.triangulatedToOrigFaceVertexIndexMap[i];

          if (vidx >= nsrcs) {
            PUSH_ERROR_AND_RETURN("Internal error. Invalid triangulation.");
          }

          triangulated_fvtexcoords.push_back(src_ptr[vidx]);
        }

        VertexAttribute uvAttr;
        uvAttr.get_data().resize(triangulated_fvtexcoords.size() *
                                 sizeof(vec2));
        memcpy(uvAttr.get_data().data(), triangulated_fvtexcoords.data(),
               triangulated_fvtexcoords.size() * sizeof(vec2));

        dst.texcoords[uint32_t(slotId)] = uvAttr;
      } else {
        if (vattr.vertex_count() != num_fvs) {
          PUSH_ERROR_AND_RETURN(
              fmt::format("The number of UV texcoord attributes {} does not "
                          "match to the number of facevarying elements {}\n",
                          vattr.vertex_count(), num_fvs));
        }

        dst.texcoords[uint32_t(slotId)] = vattr;
      }
    } else {
      PUSH_ERROR_AND_RETURN(
          "Internal error. Invalid variability value in TexCoord attribute.");
    }
#else
    VertexAttribute uvAttr;
    if (!ConvertVertexVariabilityImpl(vattr, is_single_indexable,
      dst.faceVertexCounts, dst.faceVertexIndices, uvAttr)) {
      PUSH_ERROR_AND_RETURN(
          "Failed to convert uvAttr variability.");
    }

    dst.texcoords[uint32_t(slotId)] = uvAttr;
#endif
  }

  if (dst.vertex_colors.vertex_count() > 1) {
    VertexAttribute vattr = dst.vertex_colors;  // copy

    if (vattr.format != VertexAttributeFormat::Vec3) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Color VertexAttribute must be Vec3 type.\n"));
    }

    if (vattr.element_size() != 1) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "elementSize = 1 expected for VertexColor, but got {}", vattr.element_size()));
    }

#if 0
    if (vattr.variability == VertexVariability::Constant) {
      auto result = ConstantToFaceVarying(
          vattr.get_data(), vattr.stride_bytes(), dst.faceVertexCounts);
      if (!result) {
        PUSH_ERROR_AND_RETURN(
            fmt::format(eFailed to convert 'constant' attribute to 'facevarying'
                        : {} ",
                          result.error()));
      }

      VertexAttribute uvAttr;
      uvAttr.get_data().resize(result.value().size() * sizeof(vec2));
      memcpy(uvAttr.get_data().data(), result.value().data(),
             result.value().size() * sizeof(vec2));

      dst.vertex_colors = std::move(vattr
      dst.texcoords[uint32_t(slotId)] = uvAttr;

    } else if (vattr.variability == VertexVariability::Uniform) {
      auto result = UniformToFaceVarying(vattr.get_data(), vattr.stride_bytes(),
                                         dst.faceVertexCounts);
      if (!result) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Failed to convert 'uniform' attribute to 'facevarying': {}",
            result.error()));
      }

      VertexAttribute uvAttr;
      uvAttr.get_data().resize(result.value().size() * sizeof(vec2));
      memcpy(uvAttr.get_data().data(), result.value().data(),
             result.value().size() * sizeof(vec2));

      dst.texcoords[uint32_t(slotId)] = uvAttr;
    } else if ((vattr.variability == VertexVariability::Varying) ||
               (vattr.variability == VertexVariability::Vertex)) {
      auto result =
          VertexToFaceVarying(vattr.get_data(), vattr.stride_bytes(),
                              dst.faceVertexCounts, dst.faceVertexIndices);
      if (!result) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Failed to convert 'vertex' or 'varying' attribute to "
                        "'facevarying': {}",
                        result.error()));
      }

      VertexAttribute uvAttr;
      uvAttr.get_data().resize(result.value().size() * sizeof(vec2));
      memcpy(uvAttr.get_data().data(), result.value().data(),
             result.value().size() * sizeof(vec2));

      dst.texcoords[uint32_t(slotId)] = uvAttr;
    } else if (vattr.variability == VertexVariability::FaceVarying) {
      if (triangulate) {
        size_t nsrcs = vattr.get_data().size() / sizeof(vec2);
        const vec2 *src_ptr =
            reinterpret_cast<const vec2 *>(vattr.get_data().data());

        std::vector<vec2> triangulated_fvtexcoords;

        // rearrange indices
        for (size_t i = 0; i < dst.triangulatedToOrigFaceVertexIndexMap.size();
             i++) {
          size_t vidx = dst.triangulatedToOrigFaceVertexIndexMap[i];

          if (vidx >= nsrcs) {
            PUSH_ERROR_AND_RETURN("Internal error. Invalid triangulation.");
          }

          triangulated_fvtexcoords.push_back(src_ptr[vidx]);
        }

        VertexAttribute uvAttr;
        uvAttr.get_data().resize(triangulated_fvtexcoords.size() *
                                 sizeof(vec2));
        memcpy(uvAttr.get_data().data(), triangulated_fvtexcoords.data(),
               triangulated_fvtexcoords.size() * sizeof(vec2));

        dst.texcoords[uint32_t(slotId)] = uvAttr;
      } else {
        if (vattr.vertex_count() != num_fvs) {
          PUSH_ERROR_AND_RETURN(
              fmt::format("The number of UV texcoord attributes {} does not "
                          "match to the number of facevarying elements {}\n",
                          vattr.vertex_count(), num_fvs));
        }

        dst.texcoords[uint32_t(slotId)] = vattr;
      }
    } else {
      PUSH_ERROR_AND_RETURN(
          "Internal error. Invalid variability value in TexCoord attribute.");
    }
#else
    VertexAttribute dstAttr;
    if (!ConvertVertexVariabilityImpl(vattr, is_single_indexable,
      dst.faceVertexCounts, dst.faceVertexIndices, dstAttr)) {
      PUSH_ERROR_AND_RETURN(
          "Failed to convert displayColor attribute's variability.");
    }

    dst.vertex_colors = std::move(dstAttr);
#endif
  }

  if (dst.vertex_opacities.vertex_count() > 1) {
    VertexAttribute vattr = dst.vertex_opacities;  // copy

    if (vattr.format != VertexAttributeFormat::Float) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Opacity VertexAttribute must be Float type.\n"));
    }

    if (vattr.element_size() != 1) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "elementSize = 1 expected for VertexOpacity, but got {}", vattr.element_size()));
    }

    VertexAttribute dstAttr;
    if (!ConvertVertexVariabilityImpl(vattr, is_single_indexable,
      dst.faceVertexCounts, dst.faceVertexIndices, dstAttr)) {
      PUSH_ERROR_AND_RETURN(
          "Failed to convert displayOpacity attribute's variability.");
    }

    dst.vertex_opacities = std::move(dstAttr);
  }

  //
  // Vertex skin weights(jointIndex and jointWeights)
  //
  if (mesh.has_primvar("skel:jointIndices") &&
      mesh.has_primvar("skel:jointWeights")) {
    GeomPrimvar jointIndices;
    GeomPrimvar jointWeights;

    if (!mesh.get_primvar("skel:jointIndices", &jointIndices)) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. Failed to get `skel:jointIndices` primvar.");
    }

    if (!mesh.get_primvar("skel:jointWeights", &jointWeights)) {
      PUSH_ERROR_AND_RETURN(
          "Internal error. Failed to get `skel:jointIndices` primvar.");
    }

    // interpolation must be 'vertex'
    if (!jointIndices.has_interpolation()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("`skel:jointIndices` primvar must author `interpolation` "
                      "metadata(and set it to `vertex`)"));
    }

    if (jointIndices.get_interpolation() != Interpolation::Vertex) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("`skel:jointIndices` primvar must use `vertex` for "
                      "`interpolation` metadata, but got `{}`.",
                      to_string(jointIndices.get_interpolation())));
    }

    uint32_t jointIndicesElementSize = jointIndices.get_elementSize();
    uint32_t jointWeightsElementSize = jointWeights.get_elementSize();

    if (jointIndicesElementSize == 0) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("`elementSize` of `skel:jointIndices` is zero."));
    }

    if (jointWeightsElementSize == 0) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("`elementSize` of `skel:jointWeights` is zero."));
    }

    if (jointIndicesElementSize > _mesh_config.max_skin_elementSize) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "`elementSize` {} of `skel:jointIndices` too large. Max allowed is "
          "set to {}",
          jointIndicesElementSize, _mesh_config.max_skin_elementSize));
    }

    if (jointWeightsElementSize > _mesh_config.max_skin_elementSize) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "`elementSize` {} of `skel:jointWeights` too large. Max allowed is "
          "set to {}",
          jointWeightsElementSize, _mesh_config.max_skin_elementSize));
    }

    if (jointIndicesElementSize != jointWeightsElementSize) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("`elementSize` {} of `skel:jointIndices` must equal to "
                      "`elementSize` {} of `skel:jointWeights`",
                      jointIndicesElementSize, jointWeightsElementSize));
    }

    std::vector<int> jointIndicesArray;
    if (!jointIndices.flatten_with_indices(&jointIndicesArray)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Failed to flatten Indexed Primvar `skel:jointIndices`. "
                      "Ensure `skel:jointIndices` is type `int[]`"));
    }

    std::vector<float> jointWeightsArray;
    if (!jointWeights.flatten_with_indices(&jointWeightsArray)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Failed to flatten Indexed Primvar `skel:jointWeights`. "
                      "Ensure `skel:jointWeights` is type `float[]`"));
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

    if (mesh.skeleton.has_value()) {
      Path skelPath;

      if (mesh.skeleton.value().is_path()) {
        skelPath = mesh.skeleton.value().targetPath;
      } else if (mesh.skeleton.value().is_pathvector()) {
        // Use the first tone
        if (mesh.skeleton.value().targetPathVector.size()) {
          skelPath = mesh.skeleton.value().targetPathVector[0];
        } else {
          PUSH_WARN("`skel:skeleton` has invalid definition.");
        }
      } else {
        PUSH_WARN("`skel:skeleton` has invalid definition.");
      }

      if (skelPath.is_valid()) {
        // TODO
      }
    }

    // optional. geomBindTransform.
    if (mesh.has_primvar("skel:geomBindTransform")) {
      GeomPrimvar bindTransformPvar;

      if (!mesh.get_primvar("skel:geomBindTransform", &bindTransformPvar)) {
        PUSH_ERROR_AND_RETURN(
            "Internal error. Failed to get `skel:geomBindTransform` primvar.");
      }

      value::matrix4d bindTransform;
      if (!bindTransformPvar.get_value(&bindTransform)) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Failed to get `skel:geomBindTransform` attribute. "
                        "Ensure `skel:geomBindTransform` is type `matrix4d`"));
      }

      dst.joint_and_weights.geomBindTransform = bindTransform;
    }
  }

  //
  // BlendShapes
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
    shapeTarget.display_name = bs->metas().displayName.value_or("");

    if (vertex_indices.empty()) {
      PUSH_WARN(
          fmt::format("`pointIndices` in BlendShape `{}` is not authored or "
                      "empty. Skipping.",
                      bs->name));
    }

    // Check if index is valid.
    std::vector<uint32_t> indices;
    indices.resize(vertex_indices.size());

    for (size_t i = 0; i < vertex_indices.size(); i++) {
      if (vertex_indices[i] < 0) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "negative index in `pointIndices`. Prim path: `{}`", bs_path));
      }

      if (uint32_t(vertex_indices[i]) > dst.points.size()) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("pointIndices[{}] {} exceeds the number of points in "
                        "GeomMesh {}. Prim path: `{}`",
                        i, vertex_indices[i], dst.points.size(), bs_path));
      }

      indices[i] = uint32_t(vertex_indices[i]);
    }
    shapeTarget.pointIndices = indices;

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
  }

  //
  // NOTE: For other primvars, the app must convert it manually.
  //

  (*dstMesh) = std::move(dst);
  return true;
}

namespace {

#if 1
// Convert UsdTranform2d -> PrimvarReader_float2 shader network.
nonstd::expected<bool, std::string> ConvertTexTransform2d(
    const Stage &stage, const Path &tx_abs_path, const UsdTransform2d &tx,
    UVTexture *tex_out, double timecode) {
  float rotation;  // in angles
  if (!tx.rotation.get_value().get(timecode, &rotation)) {
    return nonstd::make_unexpected(
        fmt::format("Failed to retrieve rotation attribute from {}\n",
                    tx_abs_path.full_path_name()));
  }

  value::float2 scale;
  if (!tx.scale.get_value().get(timecode, &scale)) {
    return nonstd::make_unexpected(
        fmt::format("Failed to retrieve scale attribute from {}\n",
                    tx_abs_path.full_path_name()));
  }

  value::float2 translation;
  if (!tx.translation.get_value().get(timecode, &translation)) {
    return nonstd::make_unexpected(
        fmt::format("Failed to retrieve translation attribute from {}\n",
                    tx_abs_path.full_path_name()));
  }

  // must be authored and connected to PrimvarReader.
  if (!tx.in.authored()) {
    return nonstd::make_unexpected("`inputs:in` must be authored.\n");
  }

  if (!tx.in.is_connection()) {
    return nonstd::make_unexpected("`inputs:in` must be a connection.\n");
  }

  const auto &paths = tx.in.get_connections();
  if (paths.size() != 1) {
    return nonstd::make_unexpected(
        "`inputs:in` must be a single connection Path.\n");
  }

  std::string prim_part = paths[0].prim_part();
  std::string prop_part = paths[0].prop_part();

  if (prop_part != "outputs:result") {
    return nonstd::make_unexpected(
        "`inputs:in` connection Path's property part must be "
        "`outputs:result`\n");
  }

  std::string err;

  const Prim *pprim{nullptr};
  if (!stage.find_prim_at_path(Path(prim_part, ""), pprim, &err)) {
    return nonstd::make_unexpected(fmt::format(
        "`inputs:in` connection Path not found in the Stage. {}\n", prim_part));
  }

  if (!pprim) {
    return nonstd::make_unexpected(
        fmt::format("[InternalError] Prim is nullptr: {}\n", prim_part));
  }

  const Shader *pshader = pprim->as<Shader>();
  if (!pshader) {
    return nonstd::make_unexpected(
        fmt::format("{} must be Shader Prim, but got {}\n", prim_part,
                    pprim->prim_type_name()));
  }

  const UsdPrimvarReader_float2 *preader =
      pshader->value.as<UsdPrimvarReader_float2>();
  if (preader) {
    return nonstd::make_unexpected(fmt::format(
        "Shader {} must be UsdPrimvarReader_float2 type, but got {}\n",
        prim_part, pshader->info_id));
  }

  // Get value producing attribute(i.e, follow .connection and return
  // terminal Attribute value)
  value::token varname;
#if 0
  if (!tydra::EvaluateShaderAttribute(stage, *pshader, "inputs:varname",
                                      &varname, &err)) {
    return nonstd::make_unexpected(
        fmt::format("Failed to evaluate UsdPrimvarReader_float2's "
                    "inputs:varname: {}\n",
                    err));
  }
#else
  TerminalAttributeValue attr;
  if (!tydra::EvaluateAttribute(stage, *pprim, "inputs:varname", &attr, &err)) {
    return nonstd::make_unexpected(
        "`inputs:varname` evaluation failed: " + err + "\n");
  }
  if (auto pv = attr.as<value::token>()) {
    varname = *pv;
  } else {
    return nonstd::make_unexpected(
        "`inputs:varname` must be `token` type, but got " + attr.type_name() +
        "\n");
  }
  if (varname.str().empty()) {
    return nonstd::make_unexpected("`inputs:varname` is empty token\n");
  }
  DCOUT("inputs:varname = " << varname);
#endif

  // Build transform matrix.
  // https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_texture_transform
  // Since USD uses post-multiply,
  //
  // matrix = scale * rotate * translate
  //
  {
    mat3 s;
    s.set_scale(scale[0], scale[1], 1.0f);

    mat3 r = mat3::identity();

    r.m[0][0] = std::cos(math::radian(rotation));
    r.m[0][1] = std::sin(math::radian(rotation));

    r.m[1][0] = -std::sin(math::radian(rotation));
    r.m[1][1] = std::cos(math::radian(rotation));

    mat3 t = mat3::identity();
    t.set_translation(translation[0], translation[1], 1.0f);

    tex_out->transform = s * r * t;
  }

  tex_out->tx_rotation = rotation;
  tex_out->tx_translation = translation;
  tex_out->tx_scale = scale;
  tex_out->has_transform2d = true;

  tex_out->varname_uv = varname.str();

  return true;
}
#endif

template <typename T>
nonstd::expected<bool, std::string> GetConnectedUVTexture(
    const Stage &stage, const TypedAnimatableAttributeWithFallback<T> &src,
    Path *tex_abs_path, const UsdUVTexture **dst, const Shader **shader_out) {
  if (!dst) {
    return nonstd::make_unexpected("[InternalError] dst is nullptr.\n");
  }

  if (!src.is_connection()) {
    return nonstd::make_unexpected("Attribute must be connection.\n");
  }

  if (src.get_connections().size() != 1) {
    return nonstd::make_unexpected(
        "Attribute connections must be single connection Path.\n");
  }

  //
  // Example: color3f inputs:diffuseColor.connect = </path/to/tex.outputs:rgb>
  //
  // => path.prim_part : /path/to/tex
  // => path.prop_part : outputs:rgb
  //

  const Path &path = src.get_connections()[0];

  const std::string prim_part = path.prim_part();
  const std::string prop_part = path.prop_part();

  // NOTE: no `outputs:rgba` in the spec.
  constexpr auto kOutputsRGB = "outputs:rgb";
  constexpr auto kOutputsR = "outputs:r";
  constexpr auto kOutputsG = "outputs:g";
  constexpr auto kOutputsB = "outputs:b";
  constexpr auto kOutputsA = "outputs:a";

  if (prop_part == kOutputsRGB) {
    // ok
  } else if (prop_part == kOutputsR) {
    // ok
  } else if (prop_part == kOutputsG) {
    // ok
  } else if (prop_part == kOutputsB) {
    // ok
  } else if (prop_part == kOutputsA) {
    // ok
  } else {
    return nonstd::make_unexpected(fmt::format(
        "connection Path's property part must be `{}`, `{}`, `{}` or `{}` "
        "for "
        "UsdUVTexture, but got `{}`\n",
        kOutputsRGB, kOutputsR, kOutputsG, kOutputsB, kOutputsA, prop_part));
  }

  const Prim *prim{nullptr};
  std::string err;
  if (!stage.find_prim_at_path(Path(prim_part, ""), prim, &err)) {
    return nonstd::make_unexpected(
        fmt::format("Prim {} not found in the Stage: {}\n", prim_part, err));
  }

  if (!prim) {
    return nonstd::make_unexpected("[InternalError] Prim ptr is null.\n");
  }

  if (tex_abs_path) {
    (*tex_abs_path) = Path(prim_part, "");
  }

  if (const Shader *pshader = prim->as<Shader>()) {
    if (const UsdUVTexture *ptex = pshader->value.as<UsdUVTexture>()) {
      DCOUT("ptex = " << ptex);
      (*dst) = ptex;

      if (shader_out) {
        (*shader_out) = pshader;
      }

      return true;
    }
  }

  return nonstd::make_unexpected(
      fmt::format("Prim {} must be `Shader` Prim type, but got `{}`", prim_part,
                  prim->prim_type_name()));
}

}  // namespace

// W.I.P.
// Convert UsdUVTexture shader node.
// @return true upon conversion success(textures.back() contains the converted
// UVTexture)
//
// Possible network configuration
//
// - UsdUVTexture -> UsdPrimvarReader
// - UsdUVTexture -> UsdTransform2d -> UsdPrimvarReader
bool RenderSceneConverter::ConvertUVTexture(const Path &tex_abs_path,
                                            const AssetInfo &assetInfo,
                                            const UsdUVTexture &texture,
                                            UVTexture *tex_out) {
  DCOUT("ConvertUVTexture " << tex_abs_path);

  if (!tex_out) {
    PUSH_ERROR_AND_RETURN("tex_out arg is nullptr.");
  }
  std::string err;

  UVTexture tex;

  // First load texture file.
  if (!texture.file.authored()) {
    PUSH_ERROR_AND_RETURN(fmt::format("`asset:file` is not authored. Path = {}",
                                      tex_abs_path.prim_part()));
  }

  value::AssetPath assetPath;
  if (auto apath = texture.file.get_value()) {
    if (!apath.value().get(_timecode, &assetPath)) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "Failed to get `asset:file` value from Path {} at time {}",
          tex_abs_path.prim_part(), _timecode));
    }
  } else {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Failed to get `asset:file` value from Path {}",
                    tex_abs_path.prim_part()));
  }

  // TextureImage and BufferData
  {
    TextureImage texImage;
    BufferData assetImageBuffer;

    // Texel data is treated as byte array
    assetImageBuffer.componentType = ComponentType::UInt8;
    assetImageBuffer.count = 1;

    if (_scene_config.load_texture_assets) {
      std::string warn;

      TextureImageLoaderFunction tex_loader_fun =
          _material_config.texture_image_loader_function;

      if (!tex_loader_fun) {
        tex_loader_fun = DefaultTextureImageLoaderFunction;
      }

      bool tex_ok = tex_loader_fun(
          assetPath, assetInfo, _asset_resolver, &texImage,
          &assetImageBuffer.data,
          _material_config.texture_image_loader_function_userdata, &warn, &err);

      if (!tex_ok && !_material_config.allow_texture_load_failure) {
        PUSH_ERROR_AND_RETURN("Failed to load texture image: " + err);
      }

      if (warn.size()) {
        DCOUT("WARN: " << warn);
        PushWarn(warn);
      }

      if (err.size()) {
        // report as warning.
        PushWarn(err);
      }

      // store unresolved asset path.
      texImage.asset_identifier = assetPath.GetAssetPath();

    } else {
      // store resolved asset path.
      texImage.asset_identifier =
          _asset_resolver.resolve(assetPath.GetAssetPath());
    }

    // colorSpace.
    // First look into `colorSpace` metadata of asset, then
    // look into `inputs:sourceColorSpace' attribute.
    if (texture.file.metas().has_colorSpace()) {
      ColorSpace cs;
      value::token cs_token = texture.file.metas().get_colorSpace();
      if (!from_token(cs_token, &cs)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Invalid or unsupported token value for 'colorSpace': `{}` ",
            cs_token.str()));
      }
      texImage.usdColorSpace = cs;
    } else {
      if (texture.sourceColorSpace.authored()) {
        UsdUVTexture::SourceColorSpace cs;
        if (texture.sourceColorSpace.get_value().get(_timecode, &cs)) {
          if (cs == UsdUVTexture::SourceColorSpace::SRGB) {
            texImage.usdColorSpace = tydra::ColorSpace::sRGB;
          } else if (cs == UsdUVTexture::SourceColorSpace::Raw) {
            texImage.usdColorSpace = tydra::ColorSpace::Linear;
          } else if (cs == UsdUVTexture::SourceColorSpace::Auto) {
            // TODO: Read colorspace from a file.
            if ((texImage.assetTexelComponentType == ComponentType::UInt8) ||
                (texImage.assetTexelComponentType == ComponentType::Int8)) {
              texImage.usdColorSpace = tydra::ColorSpace::sRGB;
            } else {
              texImage.usdColorSpace = tydra::ColorSpace::Linear;
            }
          }
        }
      }
    }

    BufferData imageBuffer;

    // Linearlization and widen texel bit depth if required.
    if (_material_config.linearize_color_space) {
      size_t width = size_t(texImage.width);
      size_t height = size_t(texImage.height);
      size_t channels = size_t(texImage.channels);
      if (channels == 4) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("TODO: RGBA color channels are not supported yet."));
      }
      if (channels > 4) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("TODO: Multiband color channels(5 or more) are not "
                        "supported(yet)."));
      }

      if (assetImageBuffer.componentType == tydra::ComponentType::UInt8) {
        if (texImage.usdColorSpace == tydra::ColorSpace::sRGB) {
          if (_material_config.preserve_texel_bitdepth) {
            // u8 sRGB -> u8 Linear
            imageBuffer.componentType = tydra::ComponentType::UInt8;

            bool ret = srgb_8bit_to_linear_8bit(
                assetImageBuffer.data, width, height, channels,
                /* channel stride */ channels, &imageBuffer.data);
            if (!ret) {
              PUSH_ERROR_AND_RETURN(
                  "Failed to convert sRGB u8 image to Linear u8 image.");
            }

            imageBuffer.count = 1;

          } else {
            // u8 sRGB -> fp32 Linear
            imageBuffer.componentType = tydra::ComponentType::Float;

            std::vector<float> buf;
            bool ret = srgb_8bit_to_linear_f32(
                assetImageBuffer.data, width, height, channels,
                /* channel stride */ channels, &buf);
            if (!ret) {
              PUSH_ERROR_AND_RETURN(
                  "Failed to convert sRGB u8 image to Linear f32 image.");
            }

            imageBuffer.data.resize(buf.size() * sizeof(float));
            memcpy(imageBuffer.data.data(), buf.data(),
                   sizeof(float) * buf.size());
            imageBuffer.count = 1;
          }

          texImage.colorSpace = tydra::ColorSpace::Linear;

        } else if (texImage.usdColorSpace == tydra::ColorSpace::Linear) {
          if (_material_config.preserve_texel_bitdepth) {
            // no op.
            imageBuffer = std::move(assetImageBuffer);

          } else {
            // u8 -> fp32
            imageBuffer.componentType = tydra::ComponentType::Float;

            std::vector<float> buf;
            bool ret = u8_to_f32_image(assetImageBuffer.data, width, height,
                                       channels, &buf);
            if (!ret) {
              PUSH_ERROR_AND_RETURN("Failed to convert u8 image to f32 image.");
            }

            imageBuffer.data.resize(buf.size() * sizeof(float));
            memcpy(imageBuffer.data.data(), buf.data(),
                   sizeof(float) * buf.size());
            imageBuffer.count = 1;
          }

          texImage.colorSpace = tydra::ColorSpace::Linear;

        } else {
          PUSH_ERROR(fmt::format("TODO: Color space {}",
                                 to_string(texImage.usdColorSpace)));
        }

      } else if (assetImageBuffer.componentType ==
                 tydra::ComponentType::Float) {
        // ignore preserve_texel_bitdepth

        if (texImage.usdColorSpace == tydra::ColorSpace::sRGB) {
          // srgb f32 -> linear f32
          std::vector<float> in_buf;
          std::vector<float> out_buf;
          in_buf.resize(assetImageBuffer.data.size() / sizeof(float));
          memcpy(in_buf.data(), assetImageBuffer.data.data(),
                 in_buf.size() * sizeof(float));

          out_buf.resize(assetImageBuffer.data.size() / sizeof(float));

          bool ret =
              srgb_f32_to_linear_f32(in_buf, width, height, channels,
                                     /* channel stride */ channels, &out_buf);

          imageBuffer.data.resize(assetImageBuffer.data.size());
          memcpy(imageBuffer.data.data(), out_buf.data(),
                 imageBuffer.data.size());

          if (!ret) {
            PUSH_ERROR_AND_RETURN(
                "Failed to convert sRGB f32 image to Linear f32 image.");
          }

        } else if (texImage.usdColorSpace == tydra::ColorSpace::Linear) {
          // no op
          imageBuffer = std::move(assetImageBuffer);

        } else {
          PUSH_ERROR(fmt::format("TODO: Color space {}",
                                 to_string(texImage.usdColorSpace)));
        }

      } else {
        PUSH_ERROR(fmt::format("TODO: asset texture texel format {}",
                               to_string(assetImageBuffer.componentType)));
      }

    } else {
      // Same color space.

      if (assetImageBuffer.componentType == tydra::ComponentType::UInt8) {
        if (_material_config.preserve_texel_bitdepth) {
          // Do nothing.
          imageBuffer = std::move(assetImageBuffer);

        } else {
          // u8 to f32
          imageBuffer.componentType = tydra::ComponentType::Float;
        }

        texImage.colorSpace = texImage.usdColorSpace;

      } else if (assetImageBuffer.componentType ==
                 tydra::ComponentType::Float) {
        // ignore preserve_texel_bitdepth

        // f32 to f32, so no op
        imageBuffer = std::move(assetImageBuffer);

      } else {
        PUSH_ERROR(fmt::format("TODO: asset texture texel format {}",
                               to_string(assetImageBuffer.componentType)));
      }
    }

    // Assign buffer id
    texImage.buffer_id = int64_t(buffers.size());

    // TODO: Share image data as much as possible.
    // e.g. Texture A and B uses same image file, but texturing parameter is
    // different.
    buffers.emplace_back(imageBuffer);

    tex.texture_image_id = int64_t(images.size());

    images.emplace_back(texImage);

    std::stringstream ss;
    ss << "Loaded texture image " << assetPath.GetAssetPath()
       << " : buffer_id " + std::to_string(texImage.buffer_id) << "\n";
    ss << "  width x height x components " << texImage.width << " x "
       << texImage.height << " x " << texImage.channels << "\n";
    ss << "  colorSpace " << tinyusdz::tydra::to_string(texImage.colorSpace)
       << "\n";
    PushInfo(ss.str());
  }

  //
  // Set outputChannel
  //
  if (texture.outputsRGB.authored()) {
    tex.outputChannel = UVTexture::Channel::RGB;
  } else if (texture.outputsA.authored()) {
    tex.outputChannel = UVTexture::Channel::A;
  } else if (texture.outputsR.authored()) {
    tex.outputChannel = UVTexture::Channel::R;
  } else if (texture.outputsG.authored()) {
    tex.outputChannel = UVTexture::Channel::G;
  } else if (texture.outputsB.authored()) {
    tex.outputChannel = UVTexture::Channel::B;
  } else {
    PUSH_WARN("No valid output channel attribute authored. Default to RGB");
    tex.outputChannel = UVTexture::Channel::RGB;
  }

  //
  // Convert other UVTexture parameters
  //

  if (texture.bias.authored()) {
    tex.bias = texture.bias.get_value();
  }

  if (texture.scale.authored()) {
    tex.scale = texture.scale.get_value();
  }

  if (texture.st.authored()) {
    if (texture.st.is_connection()) {
      const auto &paths = texture.st.get_connections();
      if (paths.size() != 1) {
        PUSH_ERROR_AND_RETURN(
            "UsdUVTexture inputs:st connection must be single Path.");
      }
      const Path &path = paths[0];

      const Prim *readerPrim{nullptr};
      if (!_stage->find_prim_at_path(Path(path.prim_part(), ""), readerPrim,
                                     &err)) {
        PUSH_ERROR_AND_RETURN(
            "UsdUVTexture inputs:st connection targetPath not found in the "
            "Stage: " +
            err);
      }

      if (!readerPrim) {
        PUSH_ERROR_AND_RETURN(
            "[InternlError] Invalid Prim connected to inputs:st");
      }

      const Shader *pshader = readerPrim->as<Shader>();
      if (!pshader) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("UsdUVTexture inputs:st connected Prim must be "
                        "Shader Prim, but got {} Prim",
                        readerPrim->prim_type_name()));
      }

      // currently UsdTranform2d or PrimvarReaer_float2 only for inputs:st
      if (const UsdPrimvarReader_float2 *preader =
              pshader->value.as<UsdPrimvarReader_float2>()) {
        if (!preader) {
          PUSH_ERROR_AND_RETURN(
              fmt::format("Shader's info:id must be UsdPrimvarReader_float2, "
                          "but got {}",
                          pshader->info_id));
        }

        // Get value producing attribute(i.e, follow .connection and return
        // terminal Attribute value)
        std::string varname;
        TerminalAttributeValue attr;
        if (!tydra::EvaluateAttribute(*_stage, *readerPrim, "inputs:varname",
                                      &attr, &err)) {
          PUSH_ERROR_AND_RETURN(
              fmt::format("Failed to evaluate UsdPrimvarReader_float2's "
                          "inputs:varname.\n{}",
                          err));
        }

        if (auto pv = attr.as<value::token>()) {
          varname = (*pv).str();
        } else if (auto pvs = attr.as<std::string>()) {
          varname = (*pvs);
        } else if (auto pvsd = attr.as<value::StringData>()) {
          varname = (*pvsd).value;
        } else {
          PUSH_ERROR_AND_RETURN(
              "`inputs:varname` must be `string` or `token` type, but got " +
              attr.type_name());
        }
        if (varname.empty()) {
          PUSH_ERROR_AND_RETURN("`inputs:varname` is empty token.");
        }
        DCOUT("inputs:varname = " << varname);

        tex.varname_uv = varname;
      } else if (const UsdTransform2d *ptransform =
                     pshader->value.as<UsdTransform2d>()) {
        auto result =
            ConvertTexTransform2d(*_stage, path, *ptransform, &tex, _timecode);
        if (!result) {
          PUSH_ERROR_AND_RETURN(result.error());
        }
      } else {
        PUSH_ERROR_AND_RETURN(
            "Unsupported Shader type for `inputs:st` connection: " +
            pshader->info_id + "\n");
      }

    } else {
      Animatable<value::texcoord2f> fallbacks = texture.st.get_value();
      value::texcoord2f uv;
      if (fallbacks.get(_timecode, &uv)) {
        tex.fallback_uv[0] = uv[0];
        tex.fallback_uv[1] = uv[1];
      } else {
        // TODO: report warning.
        PUSH_WARN("Failed to get fallback `st` texcoord attribute.");
      }
    }
  }

  if (texture.wrapS.authored()) {
    tinyusdz::UsdUVTexture::Wrap wrap;

    if (!texture.wrapS.get_value().get(_timecode, &wrap)) {
      PUSH_ERROR_AND_RETURN("Invalid UsdUVTexture inputs:wrapS value.");
    }

    if (wrap == UsdUVTexture::Wrap::Repeat) {
      tex.wrapS = UVTexture::WrapMode::REPEAT;
    } else if (wrap == UsdUVTexture::Wrap::Mirror) {
      tex.wrapS = UVTexture::WrapMode::MIRROR;
    } else if (wrap == UsdUVTexture::Wrap::Clamp) {
      tex.wrapS = UVTexture::WrapMode::CLAMP_TO_EDGE;
    } else if (wrap == UsdUVTexture::Wrap::Black) {
      tex.wrapS = UVTexture::WrapMode::CLAMP_TO_BORDER;
    } else {
      tex.wrapS = UVTexture::WrapMode::CLAMP_TO_EDGE;
    }
  }

  if (texture.wrapT.authored()) {
    tinyusdz::UsdUVTexture::Wrap wrap;

    if (!texture.wrapT.get_value().get(_timecode, &wrap)) {
      PUSH_ERROR_AND_RETURN("Invalid UsdUVTexture inputs:wrapT value.");
    }

    if (wrap == UsdUVTexture::Wrap::Repeat) {
      tex.wrapT = UVTexture::WrapMode::REPEAT;
    } else if (wrap == UsdUVTexture::Wrap::Mirror) {
      tex.wrapT = UVTexture::WrapMode::MIRROR;
    } else if (wrap == UsdUVTexture::Wrap::Clamp) {
      tex.wrapT = UVTexture::WrapMode::CLAMP_TO_EDGE;
    } else if (wrap == UsdUVTexture::Wrap::Black) {
      tex.wrapT = UVTexture::WrapMode::CLAMP_TO_BORDER;
    } else {
      tex.wrapT = UVTexture::WrapMode::CLAMP_TO_EDGE;
    }
  }

  DCOUT("Converted UVTexture.");

  (*tex_out) = tex;
  return true;
}

template <typename T, typename Dty>
bool RenderSceneConverter::ConvertPreviewSurfaceShaderParam(
    const Path &shader_abs_path,
    const TypedAttributeWithFallback<Animatable<T>> &param,
    const std::string &param_name, ShaderParam<Dty> &dst_param) {
  if (!param.authored()) {
    return true;
  }

  if (param.is_blocked()) {
    PUSH_ERROR_AND_RETURN(fmt::format("{} attribute is blocked.", param_name));
  } else if (param.is_connection()) {
    DCOUT(fmt::format("{] is attribute connection.", param_name));

    const UsdUVTexture *ptex{nullptr};
    const Shader *pshader{nullptr};
    Path texPath;
    auto result =
        GetConnectedUVTexture(*_stage, param, &texPath, &ptex, &pshader);

    if (!result) {
      PUSH_ERROR_AND_RETURN(result.error());
    }

    if (!ptex) {
      PUSH_ERROR_AND_RETURN("[InternalError] ptex is nullptr.");
    }
    DCOUT("ptex = " << ptex->name);

    if (!pshader) {
      PUSH_ERROR_AND_RETURN("[InternalError] pshader is nullptr.");
    }

    DCOUT("Get connected UsdUVTexture Prim: " << texPath);

    UVTexture rtex;
    const AssetInfo &assetInfo = pshader->metas().get_assetInfo();
    if (!ConvertUVTexture(texPath, assetInfo, *ptex, &rtex)) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "Failed to convert UVTexture connected to {}", param_name));
    }

    uint64_t texId = textures.size();
    textures.push_back(rtex);

    textureMap.add(texId, shader_abs_path.prim_part() + "." + param_name);

    DCOUT(fmt::format("TexId {} = {}",
                      shader_abs_path.prim_part() + ".diffuseColor", texId));

    dst_param.textureId = int32_t(texId);

    return true;
  } else {
    T val;
    if (!param.get_value().get(_timecode, &val)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Failed to get {} at `default` timecode.", param_name));
    }

    dst_param.set_value(val);

    return true;
  }
}

bool RenderSceneConverter::ConvertPreviewSurfaceShader(
    const Path &shader_abs_path, const UsdPreviewSurface &shader,
    PreviewSurfaceShader *rshader_out) {
  if (!rshader_out) {
    PUSH_ERROR_AND_RETURN("rshader_out arg is nullptr.");
  }

  PreviewSurfaceShader rshader;

  if (shader.useSpecularWorkflow.authored()) {
    if (shader.useSpecularWorkflow.is_blocked()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("useSpecularWorkflow attribute is blocked."));
    } else if (shader.useSpecularWorkflow.is_connection()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("TODO: useSpecularWorkflow with connection."));
    } else {
      int val;
      if (!shader.useSpecularWorkflow.get_value().get(_timecode, &val)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Failed to get useSpcularWorkFlow value at time `{}`.", _timecode));
      }

      rshader.useSpecularWorkFlow = val ? true : false;
    }
  }

  if (!ConvertPreviewSurfaceShaderParam(shader_abs_path, shader.diffuseColor,
                                        "diffuseColor", rshader.diffuseColor)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(shader_abs_path, shader.emissiveColor,
                                        "emissiveColor",
                                        rshader.emissiveColor)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(shader_abs_path, shader.specularColor,
                                        "specularColor",
                                        rshader.specularColor)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(shader_abs_path, shader.normal,
                                        "normal", rshader.normal)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(shader_abs_path, shader.roughness,
                                        "roughness", rshader.roughness)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(shader_abs_path, shader.metallic,
                                        "metallic", rshader.metallic)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(shader_abs_path, shader.clearcoat,
                                        "clearcoat", rshader.clearcoat)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(
          shader_abs_path, shader.clearcoatRoughness, "clearcoatRoughness",
          rshader.clearcoatRoughness)) {
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(shader_abs_path, shader.opacity,
                                        "opacity", rshader.opacity)) {
    return false;
  }
  if (!ConvertPreviewSurfaceShaderParam(
          shader_abs_path, shader.opacityThreshold, "opacityThreshold",
          rshader.opacityThreshold)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(shader_abs_path, shader.ior, "ior",
                                        rshader.ior)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(shader_abs_path, shader.occlusion,
                                        "occlusion", rshader.occlusion)) {
    return false;
  }

  if (!ConvertPreviewSurfaceShaderParam(shader_abs_path, shader.displacement,
                                        "displacement", rshader.displacement)) {
    return false;
  }

  (*rshader_out) = rshader;
  return true;
}

bool RenderSceneConverter::ConvertMaterial(const Path &mat_abs_path,
                                           const tinyusdz::Material &material,
                                           RenderMaterial *rmat_out) {
  if (!_stage) {
    PUSH_ERROR_AND_RETURN("stage is nullptr.");
  }

  if (!rmat_out) {
    PUSH_ERROR_AND_RETURN("rmat_out argument is nullptr.");
  }

  RenderMaterial rmat;
  rmat.abs_path = mat_abs_path.prim_part();
  rmat.name = mat_abs_path.element_name();
  DCOUT("rmat.abs_path = " << rmat.abs_path);
  DCOUT("rmat.name = " << rmat.name);
  std::string err;
  Path surfacePath;

  //
  // surface shader
  {
    if (material.surface.authored()) {
      auto paths = material.surface.get_connections();
      DCOUT("paths = " << paths);
      // must have single targetPath.
      if (paths.size() != 1) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("{}'s outputs:surface must be connection with single "
                        "target Path.\n",
                        mat_abs_path.full_path_name()));
      }
      surfacePath = paths[0];
    } else {
      PUSH_ERROR_AND_RETURN(
          fmt::format("{}'s outputs:surface isn't authored.\n",
                      mat_abs_path.full_path_name()));
    }

    const Prim *shaderPrim{nullptr};
    if (!_stage->find_prim_at_path(
            Path(surfacePath.prim_part(), /* prop part */ ""), shaderPrim,
            &err)) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "{}'s outputs:surface isn't connected to exising Prim path.\n",
          mat_abs_path.full_path_name()));
    }

    if (!shaderPrim) {
      // this should not happen though.
      PUSH_ERROR_AND_RETURN("[InternalError] invalid Shader Prim.\n");
    }

    const Shader *shader = shaderPrim->as<Shader>();

    if (!shader) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("{}'s outputs:surface must be connected to Shader Prim, "
                      "but connected to `{}` Prim.\n",
                      shaderPrim->prim_type_name()));
    }

    // Currently must be UsdPreviewSurface
    const UsdPreviewSurface *psurface = shader->value.as<UsdPreviewSurface>();
    if (!psurface) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Shader's info:id must be UsdPreviewSurface, but got {}",
                      shader->info_id));
    }

    // prop part must be `outputs:surface` for now.
    if (surfacePath.prop_part() != "outputs:surface") {
      PUSH_ERROR_AND_RETURN(
          fmt::format("{}'s outputs:surface connection must point to property "
                      "`outputs:surface`, but got `{}`",
                      mat_abs_path.full_path_name(), surfacePath.prop_part()));
    }

    PreviewSurfaceShader pss;
    if (!ConvertPreviewSurfaceShader(surfacePath, *psurface, &pss)) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "Failed to convert UsdPreviewSurface : {}", surfacePath.prim_part()));
    }

    rmat.surfaceShader = pss;
  }

  DCOUT("Converted Material: " << mat_abs_path);

  (*rmat_out) = rmat;
  return true;
}

namespace {

bool MeshVisitor(const tinyusdz::Path &abs_path, const tinyusdz::Prim &prim,
                 const int32_t level, void *userdata, std::string *err) {
  if (!userdata) {
    return false;
  }

  RenderSceneConverter *converter =
      reinterpret_cast<RenderSceneConverter *>(userdata);

  if (level > 1024 * 1024) {
    if (err) {
      (*err) += "Scene graph is too deep.\n";
    }
    // Too deep
    return false;
  }

  if (const tinyusdz::GeomMesh *pmesh = prim.as<tinyusdz::GeomMesh>()) {
    // Collect GeomSubsets
    // std::vector<const tinyusdz::GeomSubset *> subsets = GetGeomSubsets(;

    DCOUT("Material: " << abs_path);

    //
    // First convert Material.
    //
    // - If prim has materialBind, convert it to RenderMesh's material.
    // - If prim has GeomSubset with materialBind, convert it to per-face
    // material.
    //

    {
      const std::string mesh_path_str = abs_path.full_path_name();

      std::vector<RenderMaterial> &rmaterials = converter->materials;

      tinyusdz::Path bound_material_path;
      const tinyusdz::Material *bound_material{nullptr};
      bool ret = tinyusdz::tydra::GetBoundMaterial(
          *converter->GetStagePtr(), /* GeomMesh prim path */ abs_path,
          /* purpose */ "", &bound_material_path, &bound_material, err);

      int64_t rmaterial_id = -1;

      if (ret && bound_material) {
        DCOUT("Bound material path: " << bound_material_path);

        const auto matIt =
            converter->materialMap.find(bound_material_path.full_path_name());

        if (matIt != converter->materialMap.s_end()) {
          // Got material in the cache.
          uint64_t mat_id = matIt->second;
          if (mat_id >=
              converter->materials.size()) {  // this should not happen though
            if (err) {
              (*err) += "Material index out-of-range.\n";
            }
            return false;
          }

          if (mat_id >= (std::numeric_limits<int64_t>::max)()) {
            if (err) {
              (*err) += "Material index too large.\n";
            }
            return false;
          }

          rmaterial_id = int64_t(mat_id);

        } else {
          RenderMaterial rmat;
          if (!converter->ConvertMaterial(bound_material_path, *bound_material,
                                          &rmat)) {
            if (err) {
              (*err) += fmt::format("Material conversion failed: {}",
                                    bound_material_path);
            }
            return false;
          }

          // Assign new material ID
          uint64_t mat_id = rmaterials.size();

          if (mat_id >= (std::numeric_limits<int64_t>::max)()) {
            if (err) {
              (*err) += "Material index too large.\n";
            }
            return false;
          }
          rmaterial_id = int64_t(mat_id);

          converter->materialMap.add(bound_material_path.full_path_name(),
                                     uint64_t(rmaterial_id));
          DCOUT("Add material: " << mat_id << " " << rmat.abs_path << " ( "
                                 << rmat.name << " ) ");

          rmaterials.push_back(rmat);
        }
      }

      RenderMesh rmesh;

      // TODO
      MaterialPath material_path;
      std::map<std::string, MaterialPath> subset_material_path_map;
      std::map<std::string, int64_t> rmaterial_idMap;
      std::vector<const GeomSubset *> material_subsets;
      std::vector<std::pair<std::string, const BlendShape *>> blendshapes;

      {
        material_subsets = GetMaterialBindGeomSubsets(prim);

        for (const auto &psubset : material_subsets) {
          MaterialSubset ms;
          ms.prim_name = psubset->name;
          ms.abs_path = abs_path.prim_part() + std::string("/") + psubset->name;
          ms.display_name = psubset->meta.displayName.value_or("");
        }
      }

      if (!converter->ConvertMesh(abs_path, *pmesh, material_path,
                                  subset_material_path_map, rmaterial_idMap,
                                  material_subsets, blendshapes, &rmesh)) {
        if (err) {
          (*err) += fmt::format("Mesh conversion failed: {}",
                                abs_path.full_path_name());
        }
        return false;
      }

      converter->meshes.emplace_back(std::move(rmesh));
    }
  }

  return true;  // continue traversal
}

}  // namespace

bool RenderSceneConverter::ConvertToRenderScene(const Stage &stage,
                                                RenderScene *scene,
                                                const double timecode) {
  if (!scene) {
    PUSH_ERROR_AND_RETURN("nullptr for RenderScene argument.");
  }

  _stage = &stage;

  // Build Xform at specified time.
  XformNode xform_node;
  if (!BuildXformNodeFromStage(stage, &xform_node, timecode)) {
    PUSH_ERROR_AND_RETURN("Failed to build Xform node hierarchy.\n");
  }

  // W.I.P.

  RenderScene render_scene;

  // 1. Visit GeomMesh
  // 2. If the mesh has bound material
  //   1. Create Material
  //
  // TODO: GeomSubset(per-face material)

  std::string err;

  // Pass `this` through userdata ptr
  bool ret = tydra::VisitPrims(stage, MeshVisitor, this, &err);

  if (!ret) {
    _err += err;

    return false;
  }

  // render_scene.meshMap = std::move(meshMap);
  // render_scene.materialMap = std::move(materialMap);
  // render_scene.textureMap = std::move(textureMap);
  // render_scene.imageMap = std::move(imageMap);
  // render_scene.bufferMap = std::move(bufferMap);

  render_scene.nodes = std::move(nodes);
  render_scene.meshes = std::move(meshes);
  render_scene.textures = std::move(textures);
  render_scene.images = std::move(images);
  render_scene.buffers = std::move(buffers);
  render_scene.materials = std::move(materials);

  (*scene) = std::move(render_scene);
  return true;
}

bool DefaultTextureImageLoaderFunction(const value::AssetPath &assetPath,
                                       const AssetInfo &assetInfo,
                                       AssetResolutionResolver &assetResolver,
                                       TextureImage *texImageOut,
                                       std::vector<uint8_t> *imageData,
                                       void *userdata, std::string *warn,
                                       std::string *err) {
  if (!texImageOut) {
    if (err) {
      (*err) = "`imageOut` argument is nullptr\n";
    }
    return false;
  }

  if (!imageData) {
    if (err) {
      (*err) = "`imageData` argument is nullptr\n";
    }
    return false;
  }

  // TODO: assetInfo
  (void)assetInfo;
  (void)userdata;
  (void)warn;

  std::string resolvedPath = assetResolver.resolve(assetPath.GetAssetPath());

  if (resolvedPath.empty()) {
    if (err) {
      (*err) += fmt::format("Failed to resolve asset path: {}\n",
                            assetPath.GetAssetPath());
    }
    return false;
  }

  DCOUT("Resolved asset path = " << resolvedPath);
  auto result = tinyusdz::image::LoadImageFromFile(resolvedPath);
  if (!result) {
    if (err) {
      (*err) += "Failed to load image file: " + result.error() + "\n";
    }
    return false;
  }

  TextureImage texImage;

  texImage.asset_identifier = resolvedPath;
  texImage.channels = result.value().image.channels;

  if (result.value().image.bpp == 8) {
    // assume uint8
    texImage.assetTexelComponentType = ComponentType::UInt8;
  } else {
    DCOUT("TODO: bpp = " << result.value().image.bpp);
    if (err) {
      (*err) = "TODO or unsupported bpp: " +
               std::to_string(result.value().image.bpp) + "\n";
    }
    return false;
  }

  texImage.channels = result.value().image.channels;
  texImage.width = result.value().image.width;
  texImage.height = result.value().image.height;

  (*texImageOut) = texImage;

  // raw image data
  (*imageData) = result.value().image.data;

  return true;
}

std::string to_string(ColorSpace cty) {
  std::string s;
  switch (cty) {
    case ColorSpace::sRGB: {
      s = "srgb";
      break;
    }
    case ColorSpace::Linear: {
      s = "linear";
      break;
    }
    case ColorSpace::Rec709: {
      s = "rec709";
      break;
    }
    case ColorSpace::OCIO: {
      s = "ocio";
      break;
    }
    case ColorSpace::Lin_DisplayP3: {
      s = "lin_displayp3";
      break;
    }
    case ColorSpace::sRGB_DisplayP3: {
      s = "srgb_displayp3";
      break;
    }
    case ColorSpace::Custom: {
      s = "custom";
      break;
    }
  }

  return s;
}

bool from_token(const value::token &tok, ColorSpace *cty) {
  if (!cty) {
    return false;
  }

  if (tok.str() == "srgb") {
    (*cty) = ColorSpace::sRGB;
  } else if (tok.str() == "linear") {
    (*cty) = ColorSpace::Linear;
  } else if (tok.str() == "rec709") {
    (*cty) = ColorSpace::Rec709;
  } else if (tok.str() == "ocio") {
    (*cty) = ColorSpace::OCIO;
  } else if (tok.str() == "lin_displayp3") {
    (*cty) = ColorSpace::Lin_DisplayP3;
  } else if (tok.str() == "srgb_displayp3") {
    (*cty) = ColorSpace::sRGB_DisplayP3;
  } else if (tok.str() == "custom") {
    (*cty) = ColorSpace::Custom;
  } else {
    return false;
  }

  return true;
}

std::string to_string(ComponentType cty) {
  std::string s;
  switch (cty) {
    case ComponentType::UInt8: {
      s = "uint8";
      break;
    }
    case ComponentType::Int8: {
      s = "int8";
      break;
    }
    case ComponentType::UInt16: {
      s = "uint16";
      break;
    }
    case ComponentType::Int16: {
      s = "int16";
      break;
    }
    case ComponentType::UInt32: {
      s = "uint32";
      break;
    }
    case ComponentType::Int32: {
      s = "int32";
      break;
    }
    case ComponentType::Half: {
      s = "half";
      break;
    }
    case ComponentType::Float: {
      s = "float";
      break;
    }
    case ComponentType::Double: {
      s = "double";
      break;
    }
  }

  return s;
}

std::string to_string(UVTexture::WrapMode mode) {
  std::string s;
  switch (mode) {
    case UVTexture::WrapMode::REPEAT: {
      s = "repeat";
      break;
    }
    case UVTexture::WrapMode::CLAMP_TO_BORDER: {
      s = "clamp_to_border";
      break;
    }
    case UVTexture::WrapMode::CLAMP_TO_EDGE: {
      s = "clamp_to_edge";
      break;
    }
    case UVTexture::WrapMode::MIRROR: {
      s = "mirror";
      break;
    }
  }

  return s;
}

std::string to_string(VertexVariability v) {
  std::string s;

  switch (v) {
    case VertexVariability::Constant: {
      s = "constant";
      break;
    }
    case VertexVariability::Uniform: {
      s = "uniform";
      break;
    }
    case VertexVariability::Varying: {
      s = "varying";
      break;
    }
    case VertexVariability::Vertex: {
      s = "vertex";
      break;
    }
    case VertexVariability::FaceVarying: {
      s = "facevarying";
      break;
    }
    case VertexVariability::Indexed: {
      s = "indexed";
      break;
    }
  }

  return s;
}

std::string to_string(VertexAttributeFormat f) {
  std::string s;

  switch (f) {
    case VertexAttributeFormat::Bool: {
      s = "bool";
      break;
    }
    case VertexAttributeFormat::Char: {
      s = "int8";
      break;
    }
    case VertexAttributeFormat::Char2: {
      s = "int8x2";
      break;
    }
    case VertexAttributeFormat::Char3: {
      s = "int8x3";
      break;
    }
    case VertexAttributeFormat::Char4: {
      s = "int8x4";
      break;
    }
    case VertexAttributeFormat::Byte: {
      s = "uint8";
      break;
    }
    case VertexAttributeFormat::Byte2: {
      s = "uint8x2";
      break;
    }
    case VertexAttributeFormat::Byte3: {
      s = "uint8x3";
      break;
    }
    case VertexAttributeFormat::Byte4: {
      s = "uint8x4";
      break;
    }
    case VertexAttributeFormat::Short: {
      s = "int16";
      break;
    }
    case VertexAttributeFormat::Short2: {
      s = "int16x2";
      break;
    }
    case VertexAttributeFormat::Short3: {
      s = "int16x2";
      break;
    }
    case VertexAttributeFormat::Short4: {
      s = "int16x2";
      break;
    }
    case VertexAttributeFormat::Ushort: {
      s = "uint16";
      break;
    }
    case VertexAttributeFormat::Ushort2: {
      s = "uint16x2";
      break;
    }
    case VertexAttributeFormat::Ushort3: {
      s = "uint16x2";
      break;
    }
    case VertexAttributeFormat::Ushort4: {
      s = "uint16x2";
      break;
    }
    case VertexAttributeFormat::Half: {
      s = "half";
      break;
    }
    case VertexAttributeFormat::Half2: {
      s = "half2";
      break;
    }
    case VertexAttributeFormat::Half3: {
      s = "half3";
      break;
    }
    case VertexAttributeFormat::Half4: {
      s = "half4";
      break;
    }
    case VertexAttributeFormat::Float: {
      s = "float";
      break;
    }
    case VertexAttributeFormat::Vec2: {
      s = "float2";
      break;
    }
    case VertexAttributeFormat::Vec3: {
      s = "float3";
      break;
    }
    case VertexAttributeFormat::Vec4: {
      s = "float4";
      break;
    }
    case VertexAttributeFormat::Int: {
      s = "int";
      break;
    }
    case VertexAttributeFormat::Ivec2: {
      s = "int2";
      break;
    }
    case VertexAttributeFormat::Ivec3: {
      s = "int3";
      break;
    }
    case VertexAttributeFormat::Ivec4: {
      s = "int4";
      break;
    }
    case VertexAttributeFormat::Uint: {
      s = "uint";
      break;
    }
    case VertexAttributeFormat::Uvec2: {
      s = "uint2";
      break;
    }
    case VertexAttributeFormat::Uvec3: {
      s = "uint3";
      break;
    }
    case VertexAttributeFormat::Uvec4: {
      s = "uint4";
      break;
    }
    case VertexAttributeFormat::Double: {
      s = "double";
      break;
    }
    case VertexAttributeFormat::Dvec2: {
      s = "double2";
      break;
    }
    case VertexAttributeFormat::Dvec3: {
      s = "double3";
      break;
    }
    case VertexAttributeFormat::Dvec4: {
      s = "double4";
      break;
    }
    case VertexAttributeFormat::Mat2: {
      s = "mat2";
      break;
    }
    case VertexAttributeFormat::Mat3: {
      s = "mat3";
      break;
    }
    case VertexAttributeFormat::Mat4: {
      s = "mat4";
      break;
    }
    case VertexAttributeFormat::Dmat2: {
      s = "dmat2";
      break;
    }
    case VertexAttributeFormat::Dmat3: {
      s = "dmat3";
      break;
    }
    case VertexAttributeFormat::Dmat4: {
      s = "dmat4";
      break;
    }
  }

  return s;
}

namespace {

template <typename T>
std::string DumpVertexAttributeDataImpl(const T *data, const size_t nbytes,
                                        const size_t stride_bytes,
                                        uint32_t indent) {
  size_t itemsize;

  if (stride_bytes != 0) {
    if ((nbytes % stride_bytes) != 0) {
      return fmt::format(
          "[Invalid VertexAttributeData. input bytes {} must be dividable by "
          "stride_bytes {}(Type {})]",
          nbytes, stride_bytes, value::TypeTraits<T>::type_name());
    }
    itemsize = stride_bytes;
  } else {
    if ((nbytes % sizeof(T)) != 0) {
      return fmt::format(
          "[Invalid VertexAttributeData. input bytes {} must be dividable by "
          "size {}(Type {})]",
          nbytes, sizeof(T), value::TypeTraits<T>::type_name());
    }
    itemsize = sizeof(T);
  }

  size_t nitems = nbytes / itemsize;
  std::string s;
  s += pprint::Indent(indent);
  if (stride_bytes != 0) {
    s += value::print_strided_array_snipped<T>(
        reinterpret_cast<const uint8_t *>(data), stride_bytes, nitems);
  } else {
    s += value::print_array_snipped(data, nitems);
  }
  s += "\n";
  return s;
}

std::string DumpVertexAttributeData(const VertexAttribute &vattr,
                                    uint32_t indent) {
  // Ignore elementSize
#define APPLY_FUNC(__fmt, __basety)                            \
  if (__fmt == vattr.format) {                                 \
    return DumpVertexAttributeDataImpl(                        \
        reinterpret_cast<const __basety *>(vattr.data.data()), \
        vattr.data.size(), vattr.stride, indent);              \
  }

  APPLY_FUNC(VertexAttributeFormat::Bool, uint8_t)
  APPLY_FUNC(VertexAttributeFormat::Char, char)
  APPLY_FUNC(VertexAttributeFormat::Char2, value::char2)
  APPLY_FUNC(VertexAttributeFormat::Char3, value::char3)
  APPLY_FUNC(VertexAttributeFormat::Char4, value::char4)
  APPLY_FUNC(VertexAttributeFormat::Byte, uint8_t)
  APPLY_FUNC(VertexAttributeFormat::Byte2, value::uchar2)
  APPLY_FUNC(VertexAttributeFormat::Byte3, value::uchar3)
  APPLY_FUNC(VertexAttributeFormat::Byte4, value::uchar4)
  APPLY_FUNC(VertexAttributeFormat::Short, int16_t)
  APPLY_FUNC(VertexAttributeFormat::Short2, value::short2)
  APPLY_FUNC(VertexAttributeFormat::Short3, value::short3)
  APPLY_FUNC(VertexAttributeFormat::Short4, value::short4)
  APPLY_FUNC(VertexAttributeFormat::Ushort, uint16_t)
  APPLY_FUNC(VertexAttributeFormat::Ushort2, value::ushort2)
  APPLY_FUNC(VertexAttributeFormat::Ushort3, value::ushort3)
  APPLY_FUNC(VertexAttributeFormat::Ushort4, value::ushort4)
  APPLY_FUNC(VertexAttributeFormat::Half, value::half)
  APPLY_FUNC(VertexAttributeFormat::Half2, value::half2)
  APPLY_FUNC(VertexAttributeFormat::Half3, value::half3)
  APPLY_FUNC(VertexAttributeFormat::Half4, value::half4)
  APPLY_FUNC(VertexAttributeFormat::Float, float)
  APPLY_FUNC(VertexAttributeFormat::Vec2, value::float2)
  APPLY_FUNC(VertexAttributeFormat::Vec3, value::float3)
  APPLY_FUNC(VertexAttributeFormat::Vec4, value::float4)
  APPLY_FUNC(VertexAttributeFormat::Int, int)
  APPLY_FUNC(VertexAttributeFormat::Ivec2, value::int2)
  APPLY_FUNC(VertexAttributeFormat::Ivec3, value::int3)
  APPLY_FUNC(VertexAttributeFormat::Ivec4, value::int4)
  APPLY_FUNC(VertexAttributeFormat::Uint, uint32_t)
  APPLY_FUNC(VertexAttributeFormat::Uvec2, value::half)
  APPLY_FUNC(VertexAttributeFormat::Uvec3, value::half)
  APPLY_FUNC(VertexAttributeFormat::Uvec4, value::half)
  APPLY_FUNC(VertexAttributeFormat::Double, double)
  APPLY_FUNC(VertexAttributeFormat::Dvec2, value::double2)
  APPLY_FUNC(VertexAttributeFormat::Dvec3, value::double2)
  APPLY_FUNC(VertexAttributeFormat::Dvec4, value::double2)
  APPLY_FUNC(VertexAttributeFormat::Mat2, value::matrix2f)
  APPLY_FUNC(VertexAttributeFormat::Mat3, value::matrix3f)
  APPLY_FUNC(VertexAttributeFormat::Mat4, value::matrix4f)
  APPLY_FUNC(VertexAttributeFormat::Dmat2, value::matrix2d)
  APPLY_FUNC(VertexAttributeFormat::Dmat3, value::matrix3d)
  APPLY_FUNC(VertexAttributeFormat::Dmat4, value::matrix4d)
  else {
    return fmt::format("[InternalError. Invalid VertexAttributeFormat: Id{}]",
                       int(vattr.format));
  }

#undef APPLY_FUNC
}

std::string DumpVertexAttribute(const VertexAttribute &vattr, uint32_t indent) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "Count(" << vattr.get_data().size() << ")\n";
  ss << pprint::Indent(indent) << "Format(" << to_string(vattr.format) << ")\n";
  ss << pprint::Indent(indent) << "Variability(" << to_string(vattr.variability)
     << ")\n";
  ss << pprint::Indent(indent) << "ElementSize(" << vattr.elementSize << ")\n";
  ss << DumpVertexAttributeData(vattr, indent) << "\n";
  if (vattr.indices.size()) {
    ss << pprint::Indent(indent)
       << "Indices = " << value::print_array_snipped(vattr.indices) << "\n";
  }

  return ss.str();
}

std::string DumpMesh(const RenderMesh &mesh, uint32_t indent) {
  std::stringstream ss;

  ss << "RenderMesh j{\n";

  ss << pprint::Indent(indent + 1) << "prim_name `" << mesh.prim_name << "`\n";
  ss << pprint::Indent(indent + 1) << "abs_path `" << mesh.abs_path << "`\n";
  ss << pprint::Indent(indent + 1) << "display_name `" << mesh.display_name
     << "`\n";
  ss << pprint::Indent(indent + 1) << "num_points "
     << std::to_string(mesh.points.size()) << "\n";
  ss << pprint::Indent(indent + 1) << "points \""
     << value::print_array_snipped(mesh.points) << "\"\n";
  ss << pprint::Indent(indent + 1) << "num_faceVertexCounts "
     << std::to_string(mesh.faceVertexCounts.size()) << "\n";
  ss << pprint::Indent(indent + 1) << "faceVertexCounts \""
     << value::print_array_snipped(mesh.faceVertexCounts) << "\"\n";
  ss << pprint::Indent(indent + 1) << "num_faceVertexIndices "
     << std::to_string(mesh.faceVertexIndices.size()) << "\n";
  ss << pprint::Indent(indent + 1) << "faceVertexIndices \""
     << value::print_array_snipped(mesh.faceVertexIndices) << "\"\n";
  ss << pprint::Indent(indent + 1) << "materialId "
     << std::to_string(mesh.material_id) << "\n";
  ss << pprint::Indent(indent + 1) << "normals \n"
     << DumpVertexAttribute(mesh.normals, indent + 2) << "\n";
  ss << pprint::Indent(indent + 1) << "num_texcoordSlots "
     << std::to_string(mesh.texcoords.size()) << "\n";
  for (const auto &uvs : mesh.texcoords) {
    ss << pprint::Indent(indent + 1) << "texcoords_"
       << std::to_string(uvs.first) << "\n"
       << DumpVertexAttribute(uvs.second, indent + 2) << "\n";
  }
  if (mesh.binormals.data.size()) {
    ss << pprint::Indent(indent + 1) << "binormals\n"
       << DumpVertexAttribute(mesh.binormals, indent + 2) << "\n";
  }
  if (mesh.tangents.data.size()) {
    ss << pprint::Indent(indent + 1) << "tangents\n"
       << DumpVertexAttribute(mesh.tangents, indent + 2) << "\n";
  }

  // TODO: primvars

  ss << "\n";

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

std::string DumpPreviewSurface(const PreviewSurfaceShader &shader,
                               uint32_t indent) {
  std::stringstream ss;

  ss << "PreviewSurfaceShader {\n";

  ss << pprint::Indent(indent + 1)
     << "useSpecularWorkFlow = " << std::to_string(shader.useSpecularWorkFlow)
     << "\n";

  ss << pprint::Indent(indent + 1) << "diffuseColor = ";
  if (shader.diffuseColor.is_texture()) {
    ss << "textureId[" << shader.diffuseColor.textureId << "]";
  } else {
    ss << shader.diffuseColor.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "metallic = ";
  if (shader.metallic.is_texture()) {
    ss << "textureId[" << shader.metallic.textureId << "]";
  } else {
    ss << shader.metallic.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "roughness = ";
  if (shader.roughness.is_texture()) {
    ss << "textureId[" << shader.roughness.textureId << "]";
  } else {
    ss << shader.roughness.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "ior = ";
  if (shader.ior.is_texture()) {
    ss << "textureId[" << shader.ior.textureId << "]";
  } else {
    ss << shader.ior.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "clearcoat = ";
  if (shader.clearcoat.is_texture()) {
    ss << "textureId[" << shader.clearcoat.textureId << "]";
  } else {
    ss << shader.clearcoat.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "clearcoatRoughness = ";
  if (shader.clearcoatRoughness.is_texture()) {
    ss << "textureId[" << shader.clearcoatRoughness.textureId << "]";
  } else {
    ss << shader.clearcoatRoughness.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "opacity = ";
  if (shader.opacity.is_texture()) {
    ss << "textureId[" << shader.opacity.textureId << "]";
  } else {
    ss << shader.opacity.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "opacityThreshold = ";
  if (shader.opacityThreshold.is_texture()) {
    ss << "textureId[" << shader.opacityThreshold.textureId << "]";
  } else {
    ss << shader.opacityThreshold.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "normal = ";
  if (shader.normal.is_texture()) {
    ss << "textureId[" << shader.normal.textureId << "]";
  } else {
    ss << shader.normal.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "displacement = ";
  if (shader.displacement.is_texture()) {
    ss << "textureId[" << shader.displacement.textureId << "]";
  } else {
    ss << shader.displacement.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent + 1) << "occlusion = ";
  if (shader.occlusion.is_texture()) {
    ss << "textureId[" << shader.occlusion.textureId << "]";
  } else {
    ss << shader.occlusion.value;
  }
  ss << "\n";

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

std::string DumpMaterial(const RenderMaterial &material, uint32_t indent) {
  std::stringstream ss;

  ss << "RenderMaterial " << material.abs_path << " ( " << material.name
     << " ) {\n";

  ss << pprint::Indent(indent + 1) << "surfaceShader = ";
  ss << DumpPreviewSurface(material.surfaceShader, indent + 1);
  ss << "\n";

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

std::string DumpUVTexture(const UVTexture &texture, uint32_t indent) {
  std::stringstream ss;

  // TODO
  ss << "UVTexture {\n";
  ss << pprint::Indent(indent + 1) << "primvar_name " << texture.varname_uv
     << "\n";
  ss << pprint::Indent(indent + 1) << "outputChannel "
     << to_string(texture.outputChannel) << "\n";
  ss << pprint::Indent(indent + 1) << "bias " << texture.bias << "\n";
  ss << pprint::Indent(indent + 1) << "scale " << texture.scale << "\n";
  ss << pprint::Indent(indent + 1) << "wrapS " << to_string(texture.wrapS)
     << "\n";
  ss << pprint::Indent(indent + 1) << "wrapT " << to_string(texture.wrapT)
     << "\n";
  ss << pprint::Indent(indent + 1) << "fallback_uv " << texture.fallback_uv
     << "\n";
  ss << pprint::Indent(indent + 1) << "textureImageID "
     << std::to_string(texture.texture_image_id) << "\n";
  ss << pprint::Indent(indent + 1) << "has UsdTransform2d "
     << std::to_string(texture.has_transform2d) << "\n";
  if (texture.has_transform2d) {
    ss << pprint::Indent(indent + 2) << "rotation " << texture.tx_rotation
       << "\n";
    ss << pprint::Indent(indent + 2) << "scale " << texture.tx_scale << "\n";
    ss << pprint::Indent(indent + 2) << "translation " << texture.tx_translation
       << "\n";
    ss << pprint::Indent(indent + 2) << "computed_transform "
       << texture.transform << "\n";
  }

  ss << "\n";

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

std::string DumpImage(const TextureImage &image, uint32_t indent) {
  std::stringstream ss;

  ss << "TextureImage {\n";
  ss << pprint::Indent(indent + 1) << "asset_identifier \""
     << image.asset_identifier << "\"\n";
  ss << pprint::Indent(indent + 1) << "channels "
     << std::to_string(image.channels) << "\n";
  ss << pprint::Indent(indent + 1) << "width " << std::to_string(image.width)
     << "\n";
  ss << pprint::Indent(indent + 1) << "height " << std::to_string(image.height)
     << "\n";
  ss << pprint::Indent(indent + 1) << "miplevel "
     << std::to_string(image.miplevel) << "\n";
  ss << pprint::Indent(indent + 1) << "colorSpace "
     << to_string(image.colorSpace) << "\n";
  ss << pprint::Indent(indent + 1) << "bufferID "
     << std::to_string(image.buffer_id) << "\n";

  ss << "\n";

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

std::string DumpBuffer(const BufferData &buffer, uint32_t indent) {
  std::stringstream ss;

  ss << "Buffer {\n";
  ss << pprint::Indent(indent + 1) << "bytes " << buffer.data.size() << "\n";
  ss << pprint::Indent(indent + 1) << "count " << std::to_string(buffer.count)
     << "\n";
  ss << pprint::Indent(indent + 1) << "componentType "
     << to_string(buffer.componentType) << "\n";

  ss << "\n";

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

}  // namespace

std::string DumpRenderScene(const RenderScene &scene,
                            const std::string &format) {
  std::stringstream ss;

  // Currently kdl only.
  if (format == "json") {
    // not supported yet.
  }

  // KDL does not support array, so quote it as done in USD

  ss << "title RenderScene\n";
  ss << "// # of Meshes : " << scene.meshes.size() << "\n";
  ss << "// # of Animations : " << scene.animations.size() << "\n";
  ss << "// # of Materials : " << scene.materials.size() << "\n";
  ss << "// # of UVTextures : " << scene.textures.size() << "\n";
  ss << "// # of TextureImages : " << scene.images.size() << "\n";
  ss << "// # of Buffers : " << scene.buffers.size() << "\n";

  ss << "\n";

  ss << "meshes {\n";
  for (size_t i = 0; i < scene.meshes.size(); i++) {
    ss << "[" << i << "] " << DumpMesh(scene.meshes[i], 1);
  }
  ss << "}\n";

  ss << "\n";
  ss << "materials {\n";
  for (size_t i = 0; i < scene.materials.size(); i++) {
    ss << "[" << i << "] " << DumpMaterial(scene.materials[i], 1);
  }
  ss << "}\n";

  ss << "\n";
  ss << "textures {\n";
  for (size_t i = 0; i < scene.textures.size(); i++) {
    ss << "[" << i << "] " << DumpUVTexture(scene.textures[i], 1);
  }
  ss << "}\n";

  ss << "\n";
  ss << "images {\n";
  for (size_t i = 0; i < scene.images.size(); i++) {
    ss << "[" << i << "] " << DumpImage(scene.images[i], 1);
  }
  ss << "}\n";

  ss << "\n";
  ss << "buffers {\n";
  for (size_t i = 0; i < scene.buffers.size(); i++) {
    ss << "[" << i << "] " << DumpBuffer(scene.buffers[i], 1);
  }
  ss << "}\n";

  // ss << "TODO: Animations, ...\n";

  return ss.str();
}

}  // namespace tydra
}  // namespace tinyusdz
