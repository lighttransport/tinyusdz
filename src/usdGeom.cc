// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// UsdGeom API implementations

#include "usdGeom.hh"

#include <sstream>

#include "pprinter.hh"
#include "prim-types.hh"
#include "tiny-format.hh"
#include "xform.hh"
#include "str-util.hh"
//
#include "common-macros.inc"
#include "math-util.inc"

#define SET_ERROR_AND_RETURN(msg) if (err) { \
      (*err) = (msg); \
    } \
    return false

namespace tinyusdz {

namespace {

constexpr auto kPrimvars = "primvars:";
constexpr auto kIndices = ":indices";
constexpr auto kPrimvarsNormals = "primvars:normals";

}  // namespace

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

  return Interpolation::Constant; // unauthored
}

// TODO
//bool GeomPrimvar::has_value() const {
//  return _attr.
//}

bool GPrim::has_primvar(
    const std::string &varname) const {

  std::string primvar_name = kPrimvars + varname;
  return props.count(primvar_name);
}

bool GPrim::get_primvar(
    const std::string &varname, GeomPrimvar *out_primvar, std::string *err) const {
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

      primvar.set_attribute(attr);
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

          SET_ERROR_AND_RETURN("TODO: Connetion is not supported for index Attribute at the "
              "moment.");
        } else if (indexAttr.is_timesamples()) {
          SET_ERROR_AND_RETURN("TODO: Index attribute with timeSamples is not supported yet.");
        } else if (indexAttr.is_blocked()) {
          SET_ERROR_AND_RETURN("TODO: Index attribute is blocked(ValueBlock).");
        } else if (indexAttr.is_value()) {

          // Check if int[] type.
          // TODO: Support uint[]?
          std::vector<int32_t> indices;
          if (!indexAttr.get_value(&indices)) {
                SET_ERROR_AND_RETURN(fmt::format("Index Attribute is not int[] type. Got {}",
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

std::vector<GeomPrimvar> GPrim::get_primvars() const {
  std::vector<GeomPrimvar> gpvars;

  for (const auto &prop : props) {

    if (startsWith(prop.first, kPrimvars)) {

      // skip `:indices`. Attribute with `:indices` suffix is handled in `get_primvar`
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
