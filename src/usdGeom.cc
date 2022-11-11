// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// UsdGeom API implementations

#include "usdGeom.hh"

#include <sstream>

#include "pprinter.hh"
#include "prim-types.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "value-types.hh"
#include "xform.hh"
//
//#include "external/simple_match/include/simple_match/simple_match.hpp"
//
#include "common-macros.inc"
#include "math-util.inc"
#include "str-util.hh"
#include "value-pprint.hh"

#define SET_ERROR_AND_RETURN(msg) \
  if (err) {                      \
    (*err) = (msg);               \
  }                               \
  return false

// NOTE: Some types are not supported on pxrUSD(e.g. string)
#define APPLY_GEOMPRIVAR_TYPE(__FUNC) \
  __FUNC(value::half)                 \
  __FUNC(value::half2)                \
  __FUNC(value::half3)                \
  __FUNC(value::half4)                \
  __FUNC(int)                         \
  __FUNC(value::int2)                 \
  __FUNC(value::int3)                 \
  __FUNC(value::int4)                 \
  __FUNC(uint32_t)                    \
  __FUNC(value::uint2)                \
  __FUNC(value::uint3)                \
  __FUNC(value::uint4)                \
  __FUNC(float)                       \
  __FUNC(value::float2)               \
  __FUNC(value::float3)               \
  __FUNC(value::float4)               \
  __FUNC(double)                      \
  __FUNC(value::double2)              \
  __FUNC(value::double3)              \
  __FUNC(value::double4)              \
  __FUNC(value::matrix2d)             \
  __FUNC(value::matrix3d)             \
  __FUNC(value::matrix4d)             \
  __FUNC(value::quath)                \
  __FUNC(value::quatf)                \
  __FUNC(value::quatd)                \
  __FUNC(value::normal3h)             \
  __FUNC(value::normal3f)             \
  __FUNC(value::normal3d)             \
  __FUNC(value::vector3h)             \
  __FUNC(value::vector3f)             \
  __FUNC(value::vector3d)             \
  __FUNC(value::point3h)              \
  __FUNC(value::point3f)              \
  __FUNC(value::point3d)              \
  __FUNC(value::color3f)              \
  __FUNC(value::color3d)              \
  __FUNC(value::color4f)              \
  __FUNC(value::color4d)              \
  __FUNC(value::texcoord2h)           \
  __FUNC(value::texcoord2f)           \
  __FUNC(value::texcoord2d)           \
  __FUNC(value::texcoord3h)           \
  __FUNC(value::texcoord3f)           \
  __FUNC(value::texcoord3d)

// TODO: Followings are not supported on pxrUSD. Enable it in TinyUSDZ?
#if 0
 __FUNC(int64_t) \
  __FUNC(uint64_t) \
  __FUNC(std::string) \
  __FUNC(bool)
#endif

namespace tinyusdz {

namespace {

constexpr auto kPrimvars = "primvars:";
constexpr auto kIndices = ":indices";
constexpr auto kPrimvarsNormals = "primvars:normals";

///
/// Computes
///
///  for i in len(indices):
///    dest[i] = values[indices[i]]
///
/// `dest` = `values` when `indices` is empty
///
template <typename T>
nonstd::expected<bool, std::string> ExpandWithIndices(
    const std::vector<T> &values, const std::vector<int32_t> &indices,
    std::vector<T> *dest) {
  if (!dest) {
    return nonstd::make_unexpected("`dest` is nullptr.");
  }

  if (indices.empty()) {
    (*dest) = values;
    return true;
  }

  dest->resize(indices.size());

  std::vector<size_t> invalidIndices;

  bool valid = true;
  for (size_t i = 0; i < indices.size(); i++) {
    int32_t idx = indices[i];
    if ((idx >= 0) && (size_t(idx) < values.size())) {
      (*dest)[i] = values[size_t(idx)];
    } else {
      invalidIndices.push_back(i);
      valid = false;
    }
  }

  if (invalidIndices.size()) {
    return nonstd::make_unexpected(
        "Invalid indices found: " +
        value::print_array_snipped(invalidIndices,
                                   /* N to display */ 5));
  }

  return valid;
}

}  // namespace

bool IsSupportedGeomPrimvarType(uint32_t tyid) {
  //
  // scalar and 1D
  //
#define SUPPORTED_TYPE_FUN(__ty)                                           \
  case value::TypeTraits<__ty>::type_id: {                                 \
    return true;                                                           \
  }                                                                        \
  case (value::TypeTraits<__ty>::type_id | value::TYPE_ID_1D_ARRAY_BIT): { \
    return true;                                                           \
  }

  switch (tyid) {
    APPLY_GEOMPRIVAR_TYPE(SUPPORTED_TYPE_FUN)
    default:
      return false;
  }

#undef SUPPORTED_TYPE_FUN
}

bool IsSupportedGeomPrimvarType(const std::string &type_name) {
  return IsSupportedGeomPrimvarType(value::GetTypeId(type_name));
}

bool GeomPrimvar::has_elementSize() const {
  return _attr.metas().elementSize.has_value();
}

uint32_t GeomPrimvar::get_elementSize() const {
  if (_attr.metas().elementSize.has_value()) {
    return _attr.metas().elementSize.value();
  }
  return 1;
}

bool GeomPrimvar::has_interpolation() const {
  return _attr.metas().interpolation.has_value();
}

Interpolation GeomPrimvar::get_interpolation() const {
  if (_attr.metas().interpolation.has_value()) {
    return _attr.metas().interpolation.value();
  }

  return Interpolation::Constant;  // unauthored
}

// TODO
// bool GeomPrimvar::has_value() const {
//  return _attr.
//}

bool GPrim::has_primvar(const std::string &varname) const {
  std::string primvar_name = kPrimvars + varname;
  return props.count(primvar_name);
}

bool GPrim::get_primvar(const std::string &varname, GeomPrimvar *out_primvar,
                        std::string *err) const {
  if (!out_primvar) {
    SET_ERROR_AND_RETURN("Output GeomPrimvar is nullptr.");
  }

  GeomPrimvar primvar;

  std::string primvar_name = kPrimvars + varname;

  const auto it = props.find(primvar_name);
  if (it != props.end()) {
    // Currently connection attribute is not supported.
    if (it->second.is_attribute()) {
      const Attribute &attr = it->second.get_attribute();

      primvar.set_value(attr);
      primvar.set_name(varname);

    } else {
      return false;
    }

    // has indices?
    std::string index_name = primvar_name + kIndices;
    const auto indexIt = props.find(index_name);

    if (indexIt != props.end()) {
      if (indexIt->second.is_attribute()) {
        const Attribute &indexAttr = indexIt->second.get_attribute();

        if (indexAttr.is_connection()) {
          SET_ERROR_AND_RETURN(
              "TODO: Connetion is not supported for index Attribute at the "
              "moment.");
        } else if (indexAttr.is_timesamples()) {
          SET_ERROR_AND_RETURN(
              "TODO: Index attribute with timeSamples is not supported yet.");
        } else if (indexAttr.is_blocked()) {
          SET_ERROR_AND_RETURN("TODO: Index attribute is blocked(ValueBlock).");
        } else if (indexAttr.is_value()) {
          // Check if int[] type.
          // TODO: Support uint[]?
          std::vector<int32_t> indices;
          if (!indexAttr.get_value(&indices)) {
            SET_ERROR_AND_RETURN(
                fmt::format("Index Attribute is not int[] type. Got {}",
                            indexAttr.type_name()));
          }

          primvar.set_indices(indices);
        } else {
          SET_ERROR_AND_RETURN("[Internal Error] Invalid Index Attribute.");
        }
      } else {
        // indices are optional, so ok to skip it.
      }
    }
  }

  (*out_primvar) = primvar;

  return true;
}

template <typename T>
bool GeomPrimvar::flatten_with_indices(std::vector<T> *dest, std::string *err) {
  if (!dest) {
    if (err) {
      (*err) += "Output value is nullptr.";
    }
    return false;
  }

  if (_attr.is_timesamples()) {
    if (err) {
      (*err) += "TimeSamples attribute is TODO.";
    }
    return false;
  }

  if (_attr.is_value()) {
    if (!IsSupportedGeomPrimvarType(_attr.type_id())) {
      if (err) {
        (*err) += fmt::format("Unsupported type for GeomPrimvar. type = `{}`",
                              _attr.type_name());
      }
      return false;
    }

    if (auto pv = _attr.get_value<std::vector<T>>()) {
      std::vector<T> expanded_val;
      auto ret = ExpandWithIndices(pv.value(), _indices, &expanded_val);
      if (ret) {
        (*dest) = expanded_val;
        // Currently we ignore ret.value()
        return true;
      } else {
        const std::string &err_msg = ret.error();
        if (err) {
          (*err) += fmt::format(
              "[Internal Error] Failed to expand for GeomPrimvar type = `{}`",
              _attr.type_name());
          if (err_msg.size()) {
            (*err) += "\n" + err_msg;
          }
        }
      }
    }
  }

  return false;
}

bool GeomPrimvar::flatten_with_indices(value::Value *dest, std::string *err) {
  // using namespace simple_match;
  // using namespace simple_match::placeholders;

  value::Value val;

  if (!dest) {
    if (err) {
      (*err) += "Output value is nullptr.";
    }
    return false;
  }

  if (_attr.is_timesamples()) {
    if (err) {
      (*err) += "TimeSamples attribute is TODO.";
    }
    return false;
  }

  bool processed = false;

  if (_attr.is_value()) {
    if (!IsSupportedGeomPrimvarType(_attr.type_id())) {
      if (err) {
        (*err) += fmt::format("Unsupported type for GeomPrimvar. type = `{}`",
                              _attr.type_name());
      }
      return false;
    }

    if (!(_attr.type_id() & value::TYPE_ID_1D_ARRAY_BIT)) {
      // Nothing to do for scalar type.
      (*dest) = _attr.get_var().value_raw();
    } else {
      std::string err_msg;

#if 0
#define APPLY_FUN(__ty)                                                      \
  value::TypeTraits<__ty>::type_id | value::TYPE_ID_1D_ARRAY_BIT,            \
      [this, &val, &processed, &err_msg]() {                                 \
        std::vector<__ty> expanded_val;                                      \
        if (auto pv = _attr.get_value<std::vector<__ty>>()) {                \
          auto ret = ExpandWithIndices(pv.value(), _indices, &expanded_val); \
          if (ret) {                                                         \
            processed = ret.value();                                         \
            if (processed) {                                                 \
              val = expanded_val;                                            \
            }                                                                \
          } else {                                                           \
            err_msg = ret.error();                                           \
          }                                                                  \
        }                                                                    \
      },

      match(_attr.type_id(), APPLY_GEOMPRIVAR_TYPE(APPLY_FUN) _,
            [&processed]() { processed = false; });
#else

#define APPLY_FUN(__ty)                                                  \
  case value::TypeTraits<__ty>::type_id | value::TYPE_ID_1D_ARRAY_BIT: { \
    std::vector<__ty> expanded_val;                                      \
    if (auto pv = _attr.get_value<std::vector<__ty>>()) {                \
      auto ret = ExpandWithIndices(pv.value(), _indices, &expanded_val); \
      if (ret) {                                                         \
        processed = ret.value();                                         \
        if (processed) {                                                 \
          val = expanded_val;                                            \
        }                                                                \
      } else {                                                           \
        err_msg = ret.error();                                           \
      }                                                                  \
    }                                                                    \
    break;                                                               \
  }

#endif

      switch (_attr.type_id()) {
        APPLY_GEOMPRIVAR_TYPE(APPLY_FUN)
        default: {
          processed = false;
        }
      }

#undef APPLY_FUN

      if (processed) {
        (*dest) = std::move(val);
      } else {
        if (err) {
          (*err) += fmt::format(
              "[Internal Error] Failed to expand for GeomPrimvar type = `{}`",
              _attr.type_name());
          if (err_msg.size()) {
            (*err) += "\n" + err_msg;
          }
        }
      }
    }
  }

  return processed;
}

template <typename T>
bool GeomPrimvar::get_value(T *dest, std::string *err) {
  if (!dest) {
    if (err) {
      (*err) += "Output value is nullptr.";
    }
    return false;
  }

  if (_attr.is_timesamples()) {
    if (err) {
      (*err) += "TimeSamples attribute is TODO.";
    }
    return false;
  }

  if (_attr.is_blocked()) {
    if (err) {
      (*err) += "Attribute is blocked.";
    }
    return false;
  }

  if (_attr.is_value()) {
    if (!IsSupportedGeomPrimvarType(_attr.type_id())) {
      if (err) {
        (*err) += fmt::format("Unsupported type for GeomPrimvar. type = `{}`",
                              _attr.type_name());
      }
      return false;
    }

    if (auto pv = _attr.get_value<T>()) {

      // copy
      (*dest) = pv.value();
      return true;

    } else {
      if (err) {
        (*err) += fmt::format("Attribute value type mismatch. Requested type `{}` but Attribute has type `{}`", value::TypeTraits<T>::type_id, _attr.type_name());
      }
      return false;
    }
  }

  return false;
}

std::vector<GeomPrimvar> GPrim::get_primvars() const {
  std::vector<GeomPrimvar> gpvars;

  for (const auto &prop : props) {
    if (startsWith(prop.first, kPrimvars)) {
      // skip `:indices`. Attribute with `:indices` suffix is handled in
      // `get_primvar`
      if (props.count(prop.first + kIndices)) {
        continue;
      }

      GeomPrimvar gprimvar;
      if (get_primvar(removePrefix(prop.first, kPrimvars), &gprimvar)) {
        gpvars.emplace_back(std::move(gprimvar));
      }
    }
  }

  return gpvars;
}

bool GPrim::set_primvar(const GeomPrimvar &primvar,
                        std::string *err) {
  if (primvar.name().empty()) {
    if (err) {
      (*err) += "GeomPrimvar.name is empty.";
    }
    return false;
  }

  if (startsWith(primvar.name(), "primvars:")) {
    if (err) {
      (*err) += "GeomPrimvar.name must not start with `primvars:` namespace. name = " + primvar.name();
    }
    return false;
  }

  std::string primvar_name = kPrimvars + primvar.name();

  // Overwrite existing primvar prop.
  // TODO: Report warn when primvar name already exists.

  const Attribute &attr = primvar.get_attribute();
  props.emplace(primvar_name, attr);

  if (primvar.has_indices()) {

    std::string index_name = primvar_name + kIndices;

    Attribute indices;
    indices.set_value(primvar.get_indices());

    props.emplace(index_name, indices);
  }

  return true;
}

const std::vector<value::point3f> GeomMesh::GetPoints(
    double time, value::TimeSampleInterpolationType interp) const {
  std::vector<value::point3f> dst;

  if (!points.authored() || points.is_blocked()) {
    return dst;
  }

  if (points.is_connection()) {
    // TODO: connection
    return dst;
  }

  if (auto pv = points.get_value()) {
    std::vector<value::point3f> val;
    if (pv.value().get(time, &val, interp)) {
      dst = std::move(val);
    }
  }

  return dst;
}

const std::vector<value::normal3f> GeomMesh::GetNormals(
    double time, value::TimeSampleInterpolationType interp) const {
  std::vector<value::normal3f> dst;

  if (props.count(kPrimvarsNormals)) {
    const auto prop = props.at(kPrimvarsNormals);
    if (prop.is_relationship()) {
      // TODO:
      return dst;
    }

    if (prop.get_attribute().get_var().is_timesamples()) {
      // TODO:
      return dst;
    }

    if (prop.get_attribute().type_name() == "normal3f[]") {
      if (auto pv =
              prop.get_attribute().get_value<std::vector<value::normal3f>>()) {
        dst = pv.value();
      }
    }
  } else if (normals.authored()) {
    if (normals.is_connection()) {
      // TODO
      return dst;
    } else if (normals.is_blocked()) {
      return dst;
    }

    if (normals.get_value()) {
      std::vector<value::normal3f> val;
      if (normals.get_value().value().get(time, &val, interp)) {
        dst = std::move(val);
      }
    }
  }

  return dst;
}

Interpolation GeomMesh::GetNormalsInterpolation() const {
  if (props.count(kPrimvarsNormals)) {
    const auto &prop = props.at(kPrimvarsNormals);
    if (prop.get_attribute().type_name() == "normal3f[]") {
      if (prop.get_attribute().metas().interpolation) {
        return prop.get_attribute().metas().interpolation.value();
      }
    }
  } else if (normals.metas().interpolation) {
    return normals.metas().interpolation.value();
  }

  return Interpolation::Vertex;  // default 'vertex'
}

const std::vector<int32_t> GeomMesh::GetFaceVertexCounts() const {
  std::vector<int32_t> dst;

  if (!faceVertexCounts.authored() || faceVertexCounts.is_blocked()) {
    return dst;
  }

  if (faceVertexCounts.is_connection()) {
    // TODO: connection
    return dst;
  }

  if (auto pv = faceVertexCounts.get_value()) {
    std::vector<int32_t> val;
    // TOOD: timesamples
    if (pv.value().get_scalar(&val)) {
      dst = std::move(val);
    }
  }
  return dst;
}

const std::vector<int32_t> GeomMesh::GetFaceVertexIndices() const {
  std::vector<int32_t> dst;

  if (!faceVertexIndices.authored() || faceVertexIndices.is_blocked()) {
    return dst;
  }

  if (faceVertexIndices.is_connection()) {
    // TODO: connection
    return dst;
  }

  if (auto pv = faceVertexIndices.get_value()) {
    std::vector<int32_t> val;
    // TOOD: timesamples
    if (pv.value().get_scalar(&val)) {
      dst = std::move(val);
    }
  }
  return dst;
}

void GeomMesh::Initialize(const GPrim &gprim) {
  name = gprim.name;
  parent_id = gprim.parent_id;

  props = gprim.props;

#if 0
  for (auto &prop_item : gprim.props) {
    std::string attr_name = std::get<0>(prop_item);
    const Property &prop = std::get<1>(prop_item);

    if (prop.is_rel) {
      //LOG_INFO("TODO: Rel property:" + attr_name);
      continue;
    }

    const PrimAttrib &attr = prop.get_attribute();

    if (attr_name == "points") {
      //if (auto p = primvar::as_vector<value::float3>(&attr.var)) {
      //  points = *p;
      //}
    } else if (attr_name == "faceVertexIndices") {
      //if (auto p = primvar::as_vector<int>(&attr.var)) {
      //  faceVertexIndices = *p;
      //}
    } else if (attr_name == "faceVertexCounts") {
      //if (auto p = primvar::as_vector<int>(&attr.var)) {
      //  faceVertexCounts = *p;
      //}
    } else if (attr_name == "normals") {
      //if (auto p = primvar::as_vector<value::float3>(&attr.var)) {
      //  normals.var = *p;
      //  normals.interpolation = attr.interpolation;
      //}
    } else if (attr_name == "velocitiess") {
      //if (auto p = primvar::as_vector<value::float3>(&attr.var)) {
      //  velocitiess.var = (*p);
      //  velocitiess.interpolation = attr.interpolation;
      //}
    } else if (attr_name == "primvars:uv") {
      //if (auto pv2f = primvar::as_vector<Vec2f>(&attr.var)) {
      //  st.buffer = (*pv2f);
      //  st.interpolation = attr.interpolation;
      //} else if (auto pv3f = primvar::as_vector<value::float3>(&attr.var)) {
      //  st.buffer = (*pv3f);
      //  st.interpolation = attr.interpolation;
      //}
    } else {
      // Generic PrimAtrr
      props[attr_name] = attr;
    }

  }
#endif

  doubleSided = gprim.doubleSided;
  orientation = gprim.orientation;
  visibility = gprim.visibility;
  extent = gprim.extent;
  purpose = gprim.purpose;

  // displayColor = gprim.displayColor;
  // displayOpacity = gprim.displayOpacity;

#if 0  // TODO


  // PrimVar(TODO: Remove)
  UVCoords st;

  //
  // Properties
  //

#endif
};

nonstd::expected<bool, std::string> GeomMesh::ValidateGeomSubset() {
  std::stringstream ss;

  if (geom_subset_children.empty()) {
    return true;
  }

  auto CheckFaceIds = [](const size_t nfaces, const std::vector<uint32_t> ids) {
    if (std::any_of(ids.begin(), ids.end(),
                    [&nfaces](uint32_t id) { return id >= nfaces; })) {
      return false;
    }

    return true;
  };

  if (!faceVertexCounts.authored()) {
    // No `faceVertexCounts` definition
    ss << "`faceVerexCounts` attribute is not present in GeomMesh.\n";
    return nonstd::make_unexpected(ss.str());
  }

  if (faceVertexCounts.authored()) {
    return nonstd::make_unexpected("TODO: Support faceVertexCounts.connect\n");
  }

  if (faceVertexCounts.get_value()) {
    const auto fvp = faceVertexCounts.get_value();
    std::vector<int32_t> fvc;

    if (fvp.value().is_timesamples()) {
      return nonstd::make_unexpected("TODO: faceVertexCounts.timeSamples\n");
    }

    if (!fvp.value().get_scalar(&fvc)) {
      return nonstd::make_unexpected("Failed to get faceVertexCounts data.");
    }

    size_t n = fvc.size();

    // Currently we only check if face ids are valid.
    for (size_t i = 0; i < geom_subset_children.size(); i++) {
      const GeomSubset &subset = geom_subset_children[i];

      if (!CheckFaceIds(n, subset.indices)) {
        ss << "Face index out-of-range.\n";
        return nonstd::make_unexpected(ss.str());
      }
    }
  }

  // TODO
  return nonstd::make_unexpected(
      "TODO: Implent GeomMesh::ValidateGeomSubset\n");
}

}  // namespace tinyusdz
