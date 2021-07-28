// SPDX-License-Identifier: MIT
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

using namespace nlohmann;

namespace tinyusd {

namespace {

json ToJSON(tinyusdz::GeomMesh& mesh) {
  json j;

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


  return j;
}

json ToJSON(tinyusdz::GeomBasisCurves& curves) {
  json j;

  if (curves.points.size()) {
    j["points"] = curves.points;
  }

  j["purpose"] = curves.purpose;

  return j;
}

}  // namespace

nonstd::expected<std::string, std::string> ToJSON() {
  tinyusdz::GeomMesh mesh;
  json jmesh = ToJSON(mesh);

  (void)jmesh;

  tinyusdz::GeomBasisCurves curves;
  json jcurves = ToJSON(curves);

  (void)jcurves;

  return nonstd::make_unexpected("TODO");
}

}  // namespace tinyusd
