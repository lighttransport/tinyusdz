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
//
#include "common-macros.inc"
#include "math-util.inc"

namespace tinyusdz {

namespace {

constexpr auto kPrimvarsNormals = "primvars:normals";

}  // namespace

const std::vector<value::point3f> GeomMesh::GetPoints(
    double time, TimeSampleInterpolationType interp) const {
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
    double time, TimeSampleInterpolationType interp) const {
  std::vector<value::normal3f> dst;

  if (props.count(kPrimvarsNormals)) {
    const auto prop = props.at(kPrimvarsNormals);
    if (prop.is_relationship()) {
      // TODO:
      return dst;
    }

    if (prop.get_attribute().get_var().is_timesample()) {
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
    if (pv.value().get(&val)) {
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
    if (pv.value().get(&val)) {
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

    if (!fvp.value().get(&fvc)) {
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
