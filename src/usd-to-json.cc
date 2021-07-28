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
