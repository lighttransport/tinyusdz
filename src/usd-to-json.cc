// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
#include "usd-to-json.hh"

#include "tinyusdz.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// nlohmann json
#include "nlohmann/json.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "pprinter.hh"
#include "common-macros.inc"

using namespace nlohmann;

namespace tinyusdz {

namespace {

json ToJSON(tinyusdz::GeomMesh& mesh) {
  json j;

#if 0

  if (mesh.points.size()) {
    j["points"] = mesh.points;
  }

  if (mesh.faceVertexCounts.size()) {
    j["faceVertexCounts"] = mesh.faceVertexCounts;
  }

  if (mesh.faceVertexIndices.size()) {
    j["faceVertexIndices"] = mesh.faceVertexIndices;
  }

  {
    auto normals = mesh.normals.buffer.GetAsVec3fArray();
    if (normals.size()) {
      j["normals"] = normals;
    }
  }
  // TODO: Subdivision surface
  j["doubleSided"] = mesh.doubleSided;

  j["purpose"] = mesh.purpose;

  {
    std::string v = "inherited";
    if (mesh.visibility == tinyusdz::VisibilityInvisible) {
      v = "invisible";
    }
    j["visibility"] = v;
  }


  if (mesh.extent.Valid()) {
    j["extent"] = mesh.extent.to_array();
  }

  // subd
  {
    std::string scheme = "none";
    if (mesh.subdivisionScheme == tinyusdz::SubdivisionSchemeCatmullClark) {
      scheme = "catmullClark";
    } else if (mesh.subdivisionScheme == tinyusdz::SubdivisionSchemeLoop) {
      scheme = "loop";
    } else if (mesh.subdivisionScheme == tinyusdz::SubdivisionSchemeBilinear) {
      scheme = "bilinear";
    }

    j["subdivisionScheme"] = scheme;

    if (mesh.cornerIndices.size()) {
      j["cornerIndices"] = mesh.cornerIndices;
    }
    if (mesh.cornerSharpnesses.size()) {
      j["cornerSharpness"] = mesh.cornerSharpnesses;
    }

    if (mesh.creaseIndices.size()) {
      j["creaseIndices"] = mesh.creaseIndices;
    }

    if (mesh.creaseLengths.size()) {
      j["creaseLengths"] = mesh.creaseLengths;
    }

    if (mesh.creaseSharpnesses.size()) {
      j["creaseSharpnesses"] = mesh.creaseSharpnesses;
    }

    if (mesh.interpolateBoundary.size()) {
      j["interpolateBoundary"] = mesh.interpolateBoundary;
    }

  }

#endif

  return j;
}

json ToJSON(tinyusdz::GeomBasisCurves& curves) {
  json j;

  return j;
}

nonstd::expected<json, std::string> ToJSON(const tinyusdz::StageMetas& metas) {
  json j;

  if (metas.upAxis.authored()) {
    j["upAxis"] = to_string(metas.upAxis.get());
  }

  return j;
}

}  // namespace

nonstd::expected<std::string, std::string> ToJSON(const tinyusdz::Stage &stage) {
  json j; // root

  auto jstageMetas = ToJSON(stage.stage_metas);
  if (!jstageMetas) {
    return nonstd::make_unexpected(jstageMetas.error());
  }
  j["stageMeta"] = *jstageMetas;

  j["version"] = 1.0;

  tinyusdz::GeomMesh mesh;
  json jmesh = ToJSON(mesh);

  (void)jmesh;

  tinyusdz::GeomBasisCurves curves;
  json jcurves = ToJSON(curves);

  (void)jcurves;

  std::string str = j.dump(/* indent*/2);

  return str;
}

}  // namespace tinyusd
