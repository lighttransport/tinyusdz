// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// UsdGeom API implementations


#include <cstring>
#include <sstream>
#include <type_traits>

#include "pprinter.hh"
#include "value-types.hh"
#include "core/prim.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "xform.hh"
#include "usdGeom.hh"
//
//#include "external/simple_match/include/simple_match/simple_match.hpp"
//
#include "common-macros.inc"
#include "math-util.inc"
#include "str-util.hh"
#include "value-pprint.hh"
#include "safe-arithmetic.hh"

#define SET_ERROR_AND_RETURN(msg) \
  if (err) {                      \
    (*err) = (msg);               \
  }                               \
  return false

namespace tinyusdz {

namespace {

constexpr auto kPrimvars = "primvars:";
constexpr auto kIndices = ":indices";


}  // namespace

// GeomPrimvar template bodies + index-expansion helpers. The per-type
// flatten_with_indices/get_value instantiations live in usdGeom-primvar-inst-*.cc;
// usdGeom.hh's EXTERN_TEMPLATE_GET_VALUE suppresses implicit instantiation here.
// Included for the in-file ExpandWithIndices use (GeomMesh::get_normals).
#include "usdGeom-primvar-impl.inc"

bool IsSupportedGeomPrimvarType(uint32_t tyid) {
  //
  // scalar and 1D
  //
#define SUPPORTED_TYPE_FUN(__ty)                                           \
  case value::TypeTraits<__ty>::type_id(): {                                 \
    return true;                                                           \
  }                                                                        \
  case (value::TypeTraits<__ty>::type_id() | value::TYPE_ID_1D_ARRAY_BIT): { \
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
  return _elementSize.has_value();
}

uint32_t GeomPrimvar::get_elementSize() const {
  if (_elementSize.has_value()) {
    return _elementSize.value();
  }
  return 1;
}

bool GeomPrimvar::has_interpolation() const {
  return _interpolation.has_value();
}

Interpolation GeomPrimvar::get_interpolation() const {
  if (_interpolation.has_value()) {
    return _interpolation.value();
  }

  return Interpolation::Constant;  // unauthored
}

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
  if (it == props.end()) {
    return false;
  }

  if (it->second.is_attribute()) {
    const Attribute &attr = it->second.get_attribute();

    primvar.set_value(attr);
    primvar.set_name(varname);
    if (attr.metas().has_interpolation()) {
      primvar.set_interpolation(attr.metas().get_interpolation_enum());
    }
    if (attr.metas().has_elementSize()) {
      primvar.set_elementSize(attr.metas().get_elementSize());
    }
    if (attr.metas().has_unauthoredValuesIndex()) {
      primvar.set_unauthoredValuesIndex(attr.metas().get_unauthoredValuesIndex());
    }

  } else {
    SET_ERROR_AND_RETURN(fmt::format("{} is not Attribute. Maybe Relationship?", primvar_name));
  }

  // has indices?
  std::string index_name = primvar_name + kIndices;
  const auto indexIt = props.find(index_name);

  if (indexIt != props.end()) {
    if (indexIt->second.is_attribute()) {

      if (!(primvar.get_attribute().type_id() & value::TYPE_ID_1D_ARRAY_BIT)) {
        SET_ERROR_AND_RETURN(
            fmt::format("Indexed GeomPrimVar with scalar PrimVar Attribute is not supported. PrimVar name: {}", primvar_name));
      }

      const Attribute &indexAttr = indexIt->second.get_attribute();

      if (indexAttr.is_connection()) {
        SET_ERROR_AND_RETURN(
            "Attribute Connetion is not supported for index Attribute, since we need Stage info to find Prim referred by targetPath. Use Tydra API tydra::GetGeomPrimvar.");
      }

      if (indexAttr.is_blocked()) {
        // ignore Index attribute.
      } else {

        if (indexAttr.has_timesamples()) {
          // Indices timesamples are stored type-erased; the int[] element type
          // is validated lazily on read (resolve_indices_at).
          primvar.set_timesampled_indices(indexAttr.get_var().ts_raw());
        }

        if (indexAttr.has_value()) {
          // Check if int[] type.
          // Note: OpenUSD only uses int[] for primvar indices.
          std::vector<int32_t> indices;
          if (!indexAttr.get_value(&indices)) {
            SET_ERROR_AND_RETURN(
                fmt::format("Index Attribute is not int[] type. Got {}",
                            indexAttr.type_name()));
          }

          primvar.set_default_indices(indices);
        }
      }
    } else {
      // indices are optional, so ok to skip it.
    }
  }

  (*out_primvar) = primvar;

  return true;
}

const std::vector<int32_t> &GeomPrimvar::resolve_indices_at(
    double t, value::TimeSampleInterpolationType tinterp,
    std::vector<int32_t> &buf) const {
  if (value::TimeCode(t).is_default()) {
    if (has_default_indices()) {
      return _indices;  // zero-copy
    }
    if (has_timesampled_indices()) {
      _ts_indices.get(&buf, t, tinterp);
      return buf;
    }
  } else {
    if (has_timesampled_indices()) {
      _ts_indices.get(&buf, t, tinterp);
      return buf;
    }
  }
  static const std::vector<int32_t> empty;
  return empty;
}


std::vector<int32_t> GeomPrimvar::get_indices(const double t) const {
  if (value::TimeCode(t).is_default()) {
    if (has_default_indices()) {
      return get_default_indices();
    }
  }

  if (has_timesampled_indices()) {
    std::vector<int32_t> indices;
    if (get_timesampled_indices().get(&indices, t)) {
      return indices;
    }
  }

  if (has_default_indices()) {
    return get_default_indices();
  }

  return std::vector<int32_t>();
}

void GeomPrimvar::set_indices(const std::vector<int32_t> &indices, const double t) {
  if (value::TimeCode(t).is_default()) {
    _indices = indices;
  } else {
    // add_sample overwrites an existing sample at the same time.
    _ts_indices.add_sample(t, value::Value(indices));
  }
}


bool GeomPrimvar::flatten_with_indices(const double t, value::Value *dest, const value::TimeSampleInterpolationType tinterp, std::string *err) const {
  if (!dest) {
    if (err) { (*err) += "Output value is nullptr."; }
    return false;
  }

  if (!(_attr.has_value() || _attr.has_timesamples())) {
    return false;
  }

  if (!IsSupportedGeomPrimvarType(_attr.type_id())) {
    if (err) {
      (*err) += fmt::format("Unsupported type for GeomPrimvar. type = `{}`",
                            _attr.type_name());
    }
    return false;
  }

  // Scalar type: pass through (matches OpenUSD ComputeFlattened).
  if (!(_attr.type_id() & value::TYPE_ID_1D_ARRAY_BIT)) {
    value::Value v;
    if (!_attr.get_var().get_interpolated_value(t, tinterp, &v)) {
      if (err) { (*err) += "Failed to evaluate Attribute value."; }
      return false;
    }
    (*dest) = std::move(v);
    return true;
  }

  // Array type: delegate to typed flatten_with_indices, then wrap in Value.
#define APPLY_FUN(__ty)                                                    \
  case value::TypeTraits<__ty>::type_id() | value::TYPE_ID_1D_ARRAY_BIT: { \
    std::vector<__ty> result;                                              \
    if (flatten_with_indices<__ty>(t, &result, tinterp, err)) {            \
      (*dest) = std::move(result);                                         \
      return true;                                                         \
    }                                                                      \
    return false;                                                          \
  }

  switch (_attr.type_id()) {
    APPLY_GEOMPRIVAR_TYPE(APPLY_FUN)
    default: break;
  }

#undef APPLY_FUN

  if (err) {
    (*err) += fmt::format("Unsupported array type for GeomPrimvar. type = `{}`",
                          _attr.type_name());
  }
  return false;
}

bool GeomPrimvar::flatten_with_indices(value::Value *dest, std::string *err) const {
  return flatten_with_indices(value::TimeCode::Default(), dest, value::TimeSampleInterpolationType::Linear, err);
}

// Non-template value extraction. The heavy logic lives here once; the templated
// GeomPrimvar::get_value<T> overloads are thin forwarders that call these and
// cast the result with value::Value::as<T>(). (flatten_with_indices keeps its own
// templated fast path.)
bool GeomPrimvar::get_value(value::Value *dest, std::string *err) const {
  if (!dest) {
    if (err) { (*err) += "Output value is nullptr."; }
    return false;
  }

  if (_attr.is_blocked()) {
    if (err) { (*err) += "Attribute is blocked."; }
    return false;
  }

  if (_attr.has_value()) {
    if (!IsSupportedGeomPrimvarType(_attr.type_id())) {
      if (err) {
        (*err) += fmt::format("Unsupported type for GeomPrimvar. type = `{}`",
                              _attr.type_name());
      }
      return false;
    }
    (*dest) = _attr.get_var().value_raw();
    return true;
  }

  if (_attr.has_timesamples()) {
    const value::TimeSamples &ts = _attr.get_var().ts_raw();
    if (ts.empty()) {
      if (err) { (*err) += "No TimeSample value in Attribute."; }
      return false;
    }
    // First sample (matches the previous templated get_value behavior).
    (*dest) = ts.get_samples().at(0).value;
    return true;
  }

  return false;
}

bool GeomPrimvar::get_value(double timecode, value::Value *dest,
                            value::TimeSampleInterpolationType interp,
                            std::string *err) const {
  if (!dest) {
    if (err) { (*err) += "Output value is nullptr."; }
    return false;
  }

  if (_attr.is_blocked()) {
    if (err) { (*err) += "Attribute is blocked."; }
    return false;
  }

  if (!IsSupportedGeomPrimvarType(_attr.type_id())) {
    if (err) {
      (*err) += fmt::format("Unsupported type for GeomPrimvar. type = `{}`",
                            _attr.type_name());
    }
    return false;
  }

  // Mirror Attribute::get(t, dst, interp): default time uses the default value,
  // otherwise interpolate timesamples, otherwise fall back to the default value.
  const primvar::PrimVar &pv = _attr.get_var();
  if (value::TimeCode(timecode).is_default() && _attr.has_value()) {
    (*dest) = pv.value_raw();
    return true;
  }
  if (_attr.has_timesamples()) {
    return pv.get_interpolated_value(timecode, interp, dest);
  }
  if (_attr.has_value()) {
    (*dest) = pv.value_raw();
    return true;
  }

  if (err) {
    (*err) += fmt::format("Get Attribute value at time {} failed.", timecode);
  }
  return false;
}


std::vector<GeomPrimvar> GPrim::get_primvars() const {
  std::vector<GeomPrimvar> gpvars;

  for (const auto &prop : props) {
    if (startsWith(prop.first, kPrimvars)) {
      // skip `:indices`. Attribute with `:indices` suffix is handled in
      // `get_primvar`
      // Also skips primvar attribute with `:indices` suffix only.
      if (endsWith(prop.first, kIndices)) {
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
  DCOUT("Setting primvar `" << primvar_name << "`" <<
        (props.count(primvar_name) ? " (overwriting existing)" : ""));

  Attribute attr = primvar.get_attribute();

  if (primvar.has_interpolation()) {
    attr.metas().set_interpolation_enum(primvar.get_interpolation());
  }

  if (primvar.has_elementSize()) {
    attr.metas().set_elementSize(primvar.get_elementSize());
  }

  props[primvar_name] = attr;

  {
    primvar::PrimVar var;

    if (primvar.has_default_indices()) {
      var.set_value(primvar.get_default_indices());
    }

    if (primvar.has_timesampled_indices()) {
      for (const auto &sample : primvar.get_timesampled_indices().get_samples()) {
        var.set_timesample(sample.t, sample.value);
      }
    }

    if (primvar.has_default_indices() || primvar.has_timesampled_indices()) {
      Attribute indices;
      indices.set_var(var);
      std::string index_name = primvar_name + kIndices;
      props[index_name] = indices;
    }

  }

  return true;
}

bool GPrim::get_displayColor(value::color3f *dst, double t, const value::TimeSampleInterpolationType tinterp) const
{
  GeomPrimvar primvar;
  std::string err;
  if (!get_primvar("displayColor", &primvar, &err)) {
    return false;
  }

  return primvar.get_value(t, dst, tinterp);
}

bool GPrim::get_displayOpacity(float *dst, double t, const value::TimeSampleInterpolationType tinterp) const
{
  GeomPrimvar primvar;
  std::string err;
  if (!get_primvar("displayOpacity", &primvar, &err)) {
    return false;
  }

  return primvar.get_value(t, dst, tinterp);
}

const std::vector<value::point3f> GeomMesh::get_points(
    double time, value::TimeSampleInterpolationType interp) const {
  std::vector<value::point3f> dst;

  if (!points.authored() || points.is_blocked()) {
    return dst;
  }

  if (points.is_connection()) {
    // Connection-sourced attributes require composition resolution;
    // callers should resolve connections at the Stage/Tydra level.
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

const std::vector<value::normal3f> GeomMesh::get_normals(
    double time, value::TimeSampleInterpolationType interp) const {
  std::vector<value::normal3f> dst;

  std::string err;
  if (has_primvar("normals")) {
    GeomPrimvar primvar;
    if (!get_primvar("normals", &primvar, &err)) {
      return dst;
    }

    primvar.flatten_with_indices(time, &dst, interp);
    return dst;
  } else if (normals.authored()) {
    if (normals.is_connection()) {
      // Not supported
      return dst;
    } else if (normals.is_blocked()) {
      return dst;
    }

    std::vector<int> indices;
    if (props.count("normals:indices")) {
      Attribute indexAttr = props.at("normals:indices").get_attribute();

      if (indexAttr.is_connection()) {
        // not supported.
        return dst;
      }

      if (!indexAttr.get_value(time, &indices, interp)) {
        // err
        return dst;
      }

    }

    auto pv = normals.get_value();
    if (!pv) return dst;
    std::vector<value::normal3f> value;
    if (!pv.value().get(time, &value, interp)) {
      return dst;
    }

    if (indices.size()) {
      uint32_t elementSize = normals.metas().has_elementSize() ? normals.metas().get_elementSize() : 1;

      std::vector<value::normal3f> expanded_normals;
      auto ret = ExpandWithIndices(value, elementSize, indices, &expanded_normals);

      if (!ret) {
        return dst;
      }

      dst = expanded_normals;
    } else {
      dst = value;
    }
  }

  return dst;
}

const std::vector<value::color3f> GPrim::get_displayColors(
    double time, value::TimeSampleInterpolationType interp) const {
  std::vector<value::color3f> dst;

  std::string err;
  if (has_primvar("displayColor")) {
    GeomPrimvar primvar;
    if (!get_primvar("displayColor", &primvar, &err)) {
      return dst;
    }

    primvar.flatten_with_indices(time, &dst, interp);
  }

  return dst;
}

Interpolation GeomMesh::get_normalsInterpolation() const {
  if (props.count("primvars:normals")) {
    const auto &prop = props.at("primvars:normals");
    if (prop.get_attribute().metas().has_interpolation()) {
      return prop.get_attribute().metas().get_interpolation_enum();
    }
  } else if (normals.metas().has_interpolation()) {
    return normals.metas().get_interpolation_enum();
  }

  return Interpolation::Vertex;  // default 'vertex'
}

Interpolation GPrim::get_displayColorsInterpolation() const {
  if (props.count("primvars:displayColor")) {
    const auto &prop = props.at("primvars:displayColor");
    if (prop.get_attribute().metas().has_interpolation()) {
      return prop.get_attribute().metas().get_interpolation_enum();
    }
  }

  return Interpolation::Vertex;  // default 'vertex'
}

const std::vector<int32_t> GeomMesh::get_faceVertexCounts(double time) const {
  std::vector<int32_t> dst;

  if (!faceVertexCounts.authored() || faceVertexCounts.is_blocked()) {
    return dst;
  }

  if (faceVertexCounts.is_connection()) {
    // Connection-sourced topology attributes are not supported at this level.
    return dst;
  }

  if (auto pv = faceVertexCounts.get_value()) {
    std::vector<int32_t> val;
    if (pv.value().get(time, &val, value::TimeSampleInterpolationType::Held)) {
      dst = std::move(val);
    }
  }
  return dst;
}

const std::vector<int32_t> GeomMesh::get_faceVertexIndices(double time) const {
  std::vector<int32_t> dst;

  if (!faceVertexIndices.authored() || faceVertexIndices.is_blocked()) {
    return dst;
  }

  if (faceVertexIndices.is_connection()) {
    // Connection-sourced topology attributes are not supported at this level.
    return dst;
  }

  if (auto pv = faceVertexIndices.get_value()) {
    std::vector<int32_t> val;
    if (pv.value().get(time, &val, value::TimeSampleInterpolationType::Held)) {
      dst = std::move(val);
    }
  }
  return dst;
}

// --- Convenience getter helper ---
// Extracts a typed array value from an animated attribute, handling
// authored/blocked/connection guards uniformly.
template <typename T, typename AttrT>
static std::vector<T> GetAnimatedArrayValue(
    const AttrT &attr, double time,
    value::TimeSampleInterpolationType interp) {
  std::vector<T> dst;
  if (!attr.authored() || attr.is_blocked() || attr.is_connection()) return dst;
  if (auto pv = attr.get_value()) {
    std::vector<T> val;
    if (pv.value().get(time, &val, interp)) dst = std::move(val);
  }
  return dst;
}

// --- GeomBasisCurves convenience getters ---

const std::vector<value::point3f> GeomBasisCurves::get_points(double time, value::TimeSampleInterpolationType interp) const {
  return GetAnimatedArrayValue<value::point3f>(points, time, interp);
}
const std::vector<value::normal3f> GeomBasisCurves::get_normals(double time, value::TimeSampleInterpolationType interp) const {
  return GetAnimatedArrayValue<value::normal3f>(normals, time, interp);
}
const std::vector<int> GeomBasisCurves::get_curveVertexCounts(double time) const {
  return GetAnimatedArrayValue<int>(curveVertexCounts, time, value::TimeSampleInterpolationType::Held);
}
const std::vector<float> GeomBasisCurves::get_widths(double time, value::TimeSampleInterpolationType interp) const {
  return GetAnimatedArrayValue<float>(widths, time, interp);
}

// --- GeomNurbsCurves convenience getters ---

const std::vector<value::point3f> GeomNurbsCurves::get_points(double time, value::TimeSampleInterpolationType interp) const {
  return GetAnimatedArrayValue<value::point3f>(points, time, interp);
}
const std::vector<value::normal3f> GeomNurbsCurves::get_normals(double time, value::TimeSampleInterpolationType interp) const {
  return GetAnimatedArrayValue<value::normal3f>(normals, time, interp);
}
const std::vector<int> GeomNurbsCurves::get_curveVertexCounts(double time) const {
  return GetAnimatedArrayValue<int>(curveVertexCounts, time, value::TimeSampleInterpolationType::Held);
}
const std::vector<float> GeomNurbsCurves::get_widths(double time, value::TimeSampleInterpolationType interp) const {
  return GetAnimatedArrayValue<float>(widths, time, interp);
}
const std::vector<int> GeomNurbsCurves::get_order(double time) const {
  return GetAnimatedArrayValue<int>(order, time, value::TimeSampleInterpolationType::Held);
}
const std::vector<double> GeomNurbsCurves::get_knots(double time) const {
  return GetAnimatedArrayValue<double>(knots, time, value::TimeSampleInterpolationType::Held);
}

// --- GeomPoints convenience getters ---

const std::vector<value::point3f> GeomPoints::get_points(double time, value::TimeSampleInterpolationType interp) const {
  return GetAnimatedArrayValue<value::point3f>(points, time, interp);
}
const std::vector<value::normal3f> GeomPoints::get_normals(double time, value::TimeSampleInterpolationType interp) const {
  return GetAnimatedArrayValue<value::normal3f>(normals, time, interp);
}
const std::vector<float> GeomPoints::get_widths(double time, value::TimeSampleInterpolationType interp) const {
  return GetAnimatedArrayValue<float>(widths, time, interp);
}
const std::vector<int64_t> GeomPoints::get_ids(double time) const {
  return GetAnimatedArrayValue<int64_t>(ids, time, value::TimeSampleInterpolationType::Held);
}

std::vector<value::token> GeomMesh::get_joints() const {
  constexpr auto kSkelJoints = "skel:joints";
  std::vector<value::token> dst;
  
  {
    // lookup `skel:joints` prop
    if (!props.count(kSkelJoints)) {
      return dst;
    }

    const auto &prop = props.at(kSkelJoints);
    if (prop.get_attribute().is_uniform() && prop.get_attribute().type_name() == "token[]") {

      if (!prop.get_attribute().get_value(&dst)) {
        return dst;
      }

      return dst;

    }
    DCOUT("`skel:joints` must be uniform token[] attribute, but got " << prop.value_type_name() << " (or Relationship))");
  }
  return dst;
}

// --- H2: ValidateTopology ---

bool GeomMesh::ValidateTopology(std::string *err, double time) const {
  auto fvc = get_faceVertexCounts(time);
  auto fvi = get_faceVertexIndices(time);
  auto pts = get_points(time);

  if (fvc.empty() && fvi.empty()) {
    // No topology authored
    return true;
  }

  // 1. sum(faceVertexCounts) == faceVertexIndices.size()
  size_t totalVerts = 0;
  for (size_t i = 0; i < fvc.size(); i++) {
    if (fvc[i] < 3) {
      if (err) {
        (*err) += fmt::format("faceVertexCounts[{}] = {} is less than 3.\n", i, fvc[i]);
      }
      return false;
    }
    totalVerts += static_cast<size_t>(fvc[i]);
  }

  if (totalVerts != fvi.size()) {
    if (err) {
      (*err) += fmt::format("sum(faceVertexCounts) = {} != faceVertexIndices.size() = {}.\n",
        totalVerts, fvi.size());
    }
    return false;
  }

  // 2. All faceVertexIndices in range [0, points.size())
  if (!pts.empty()) {
    for (size_t i = 0; i < fvi.size(); i++) {
      if (fvi[i] < 0 || static_cast<size_t>(fvi[i]) >= pts.size()) {
        if (err) {
          (*err) += fmt::format("faceVertexIndices[{}] = {} is out of range [0, {}).\n",
            i, fvi[i], pts.size());
        }
        return false;
      }
    }
  }

  // 3. Subdivision surface validation
  size_t numFaces = fvc.size();

  // cornerIndices validation
  if (props.count("cornerIndices") && props.count("cornerSharpnesses")) {
    const auto &ciProp = props.at("cornerIndices");
    const auto &csProp = props.at("cornerSharpnesses");
    std::vector<int32_t> ci_vals;
    std::vector<float> cs_vals;
    if (ciProp.get_attribute().get_value(&ci_vals) &&
        csProp.get_attribute().get_value(&cs_vals)) {
      if (ci_vals.size() != cs_vals.size()) {
        if (err) {
          (*err) += fmt::format("cornerIndices.size() = {} != cornerSharpnesses.size() = {}.\n",
            ci_vals.size(), cs_vals.size());
        }
        return false;
      }
      if (!pts.empty()) {
        for (size_t i = 0; i < ci_vals.size(); i++) {
          if (ci_vals[i] < 0 || static_cast<size_t>(ci_vals[i]) >= pts.size()) {
            if (err) {
              (*err) += fmt::format("cornerIndices[{}] = {} is out of range [0, {}).\n",
                i, ci_vals[i], pts.size());
            }
            return false;
          }
        }
      }
    }
  }

  // creaseIndices/creaseLengths/creaseSharpnesses validation
  if (props.count("creaseIndices") && props.count("creaseLengths")) {
    const auto &crIdxProp = props.at("creaseIndices");
    const auto &crLenProp = props.at("creaseLengths");
    std::vector<int32_t> cr_idx;
    std::vector<int32_t> cr_len;
    if (crIdxProp.get_attribute().get_value(&cr_idx) &&
        crLenProp.get_attribute().get_value(&cr_len)) {
      size_t totalCreaseVerts = 0;
      for (const auto &cl : cr_len) {
        totalCreaseVerts += static_cast<size_t>(cl);
      }
      if (totalCreaseVerts != cr_idx.size()) {
        if (err) {
          (*err) += fmt::format("sum(creaseLengths) = {} != creaseIndices.size() = {}.\n",
            totalCreaseVerts, cr_idx.size());
        }
        return false;
      }
      if (props.count("creaseSharpnesses")) {
        const auto &crShProp = props.at("creaseSharpnesses");
        std::vector<float> cr_sharp;
        if (crShProp.get_attribute().get_value(&cr_sharp)) {
          if (cr_len.size() != cr_sharp.size()) {
            if (err) {
              (*err) += fmt::format("creaseLengths.size() = {} != creaseSharpnesses.size() = {}.\n",
                cr_len.size(), cr_sharp.size());
            }
            return false;
          }
        }
      }
    }
  }

  // holeIndices validation
  if (props.count("holeIndices")) {
    const auto &hiProp = props.at("holeIndices");
    std::vector<int32_t> hi_vals;
    if (hiProp.get_attribute().get_value(&hi_vals)) {
      for (size_t i = 0; i < hi_vals.size(); i++) {
        if (hi_vals[i] < 0 || static_cast<size_t>(hi_vals[i]) >= numFaces) {
          if (err) {
            (*err) += fmt::format("holeIndices[{}] = {} is out of range [0, {}).\n",
              i, hi_vals[i], numFaces);
          }
          return false;
        }
      }
    }
  }

  return true;
}

// --- H1: ComputeExtent ---

// Helper: set axis-aligned extent for Cone/Cylinder/Capsule
static void SetAxisAlignedExtent(Axis axis, float radial, float axial_lo, float axial_hi, Extent *extent) {
  if (axis == Axis::X) {
    *extent = Extent(value::float3{{axial_lo, -radial, -radial}}, value::float3{{axial_hi, radial, radial}});
  } else if (axis == Axis::Y) {
    *extent = Extent(value::float3{{-radial, axial_lo, -radial}}, value::float3{{radial, axial_hi, radial}});
  } else {
    *extent = Extent(value::float3{{-radial, -radial, axial_lo}}, value::float3{{radial, radial, axial_hi}});
  }
}

// Helper: compute extent from points expanded by max half-width (for curves)
static void ComputeExtentFromPointsWithWidths(
    const std::vector<value::point3f> &pts,
    const std::vector<float> &widths,
    Extent *extent) {
  float maxHalfW = 0.0f;
  for (float w : widths) {
    float hw = w * 0.5f;
    if (hw > maxHalfW) maxHalfW = hw;
  }
  Extent e;
  for (const auto &p : pts) {
    e.union_with(value::float3{{p.x - maxHalfW, p.y - maxHalfW, p.z - maxHalfW}});
    e.union_with(value::float3{{p.x + maxHalfW, p.y + maxHalfW, p.z + maxHalfW}});
  }
  *extent = e;
}

bool ComputeExtent(const GeomMesh &mesh, Extent *extent,
    double time, std::string *err) {
  if (!extent) return false;
  auto pts = mesh.get_points(time);
  if (pts.empty()) {
    if (err) (*err) = "No points in mesh.\n";
    return false;
  }
  Extent e;
  for (const auto &p : pts) {
    e.union_with(p);
  }
  *extent = e;
  return true;
}

bool ComputeExtent(const GeomPoints &geom, Extent *extent,
    double time, std::string *err) {
  if (!extent) return false;
  auto pts = geom.get_points(time);
  if (pts.empty()) {
    if (err) (*err) = "No points.\n";
    return false;
  }
  auto ws = geom.get_widths(time);
  Extent e;
  for (size_t i = 0; i < pts.size(); i++) {
    float halfW = (i < ws.size()) ? ws[i] * 0.5f : 0.0f;
    e.union_with(value::float3{{pts[i].x - halfW, pts[i].y - halfW, pts[i].z - halfW}});
    e.union_with(value::float3{{pts[i].x + halfW, pts[i].y + halfW, pts[i].z + halfW}});
  }
  *extent = e;
  return true;
}

bool ComputeExtent(const GeomSphere &sphere, Extent *extent,
    double time, std::string *err) {
  if (!extent) return false;
  (void)err;
  double r = 2.0;
  sphere.radius.get_value().get(time, &r);
  float rf = static_cast<float>(r);
  *extent = Extent(value::float3{{-rf, -rf, -rf}}, value::float3{{rf, rf, rf}});
  return true;
}

bool ComputeExtent(const GeomCube &cube, Extent *extent,
    double time, std::string *err) {
  if (!extent) return false;
  (void)err;
  double s = 2.0;
  cube.size.get_value().get(time, &s);
  float h = static_cast<float>(s) * 0.5f;
  *extent = Extent(value::float3{{-h, -h, -h}}, value::float3{{h, h, h}});
  return true;
}

bool ComputeExtent(const GeomCone &cone, Extent *extent,
    double time, std::string *err) {
  if (!extent) return false;
  (void)err;
  double h = 2.0, r = 1.0;
  cone.height.get_value().get(time, &h);
  cone.radius.get_value().get(time, &r);
  SetAxisAlignedExtent(cone.axis.get_value(), static_cast<float>(r), 0.0f, static_cast<float>(h), extent);
  return true;
}

bool ComputeExtent(const GeomCylinder &cylinder, Extent *extent,
    double time, std::string *err) {
  if (!extent) return false;
  (void)err;
  double h = 2.0, r = 1.0;
  cylinder.height.get_value().get(time, &h);
  cylinder.radius.get_value().get(time, &r);
  float hh = static_cast<float>(h) * 0.5f;
  SetAxisAlignedExtent(cylinder.axis.get_value(), static_cast<float>(r), -hh, hh, extent);
  return true;
}

bool ComputeExtent(const GeomCapsule &capsule, Extent *extent,
    double time, std::string *err) {
  if (!extent) return false;
  (void)err;
  double h = 2.0, r = 0.5;
  capsule.height.get_value().get(time, &h);
  capsule.radius.get_value().get(time, &r);
  float rf = static_cast<float>(r);
  float hh = static_cast<float>(h) * 0.5f + rf;  // extend by radius for hemicaps
  SetAxisAlignedExtent(capsule.axis.get_value(), rf, -hh, hh, extent);
  return true;
}

bool ComputeExtent(const GeomBasisCurves &curves, Extent *extent,
    double time, std::string *err) {
  if (!extent) return false;
  auto pts = curves.get_points(time);
  if (pts.empty()) { if (err) (*err) = "No points in curves.\n"; return false; }
  ComputeExtentFromPointsWithWidths(pts, curves.get_widths(time), extent);
  return true;
}

bool ComputeExtent(const GeomNurbsCurves &curves, Extent *extent,
    double time, std::string *err) {
  if (!extent) return false;
  auto pts = curves.get_points(time);
  if (pts.empty()) { if (err) (*err) = "No points in NURBS curves.\n"; return false; }
  ComputeExtentFromPointsWithWidths(pts, curves.get_widths(time), extent);
  return true;
}

// static
bool GeomSubset::ValidateSubsets(
    const std::vector<const GeomSubset *> &subsets,
    const size_t elementCount,
    const FamilyType &familyType, std::string *err) {

  if (subsets.empty()) {
    return true;
  }

  // All subsets must have the same elementType.
  GeomSubset::ElementType elementType = subsets[0]->elementType.get_value();
  for (const auto psubset : subsets) {
    if (psubset->elementType.get_value() != elementType) {
      if (err) {
        (*err) = fmt::format("GeomSubset {}'s elementType must be `{}`, but got `{}`.\n",
          psubset->name, to_string(elementType), to_string(psubset->elementType.get_value()));
      }

      return false;
    }
  }

  std::set<int32_t> indicesInFamily;

  bool valid = true;
  std::stringstream ss;

  // Note: Currently validates default-value indices only.
  // TimeSampled indices would need per-frame validation at the application level.
  for (const auto psubset : subsets) {
    Animatable<std::vector<int32_t>> indices;
    if (!psubset->indices.get_value(&indices)) {
      ss << fmt::format("GeomSubset {}'s indices is not value Attribute. Connection or ValueBlock?\n",
          psubset->name);

      valid = false;
    }

    if (indices.is_blocked()) {
      ss << fmt::format("GeomSubset {}'s indices is Value Blocked.\n", psubset->name);
      valid = false;
    }

    if (indices.is_timesamples() || !indices.has_value()) {
      ss << fmt::format("ValidateSubsets: TimeSampled GeomSubset.indices is not yet supported.\n");
      valid = false;
    }

    std::vector<int32_t> subsetIndices;
    if (!indices.get_scalar(&subsetIndices)) {
      ss << fmt::format("ValidateSubsets: Internal error. Failed to get GeomSubset.indices.\n");
      valid = false;
    }

    for (const int32_t index : subsetIndices) {
      if (!indicesInFamily.insert(index).second && (familyType != FamilyType::Unrestricted)) {
        ss << fmt::format("Found overlapping index {} in GeomSubset `{}`\n", index, psubset->name);
        valid = false;
      }
    }
  }


  // Make sure every index appears exactly once if it's a partition.
  if ((familyType == FamilyType::Partition) && (indicesInFamily.size() != elementCount)) {
    ss << fmt::format("ValidateSubsets: The number of unique indices {} must be equal to input elementCount {}\n", indicesInFamily.size(), elementCount);
    valid = false;
  }

  // Ensure that the indices are in the range [0, faceCount)
  size_t maxIndex = static_cast<size_t>(*indicesInFamily.rbegin());
  int minIndex = *indicesInFamily.begin();

  if (maxIndex >= elementCount) {
    ss << fmt::format("ValidateSubsets: All indices must be in range [0, elementSize {}), but one or more indices are greater than elementSize. Maximum = {}\n", elementCount, maxIndex);

    valid = false;
  }

  if (minIndex < 0) {
    ss << fmt::format("ValidateSubsets: Found one or more indices that are less than 0. Minumum = {}\n", minIndex);

    valid = false;
  }

  if (!valid) {
    if (err) {
      (*err) += ss.str();
    }
  }

  return valid;

}

}  // namespace tinyusdz
