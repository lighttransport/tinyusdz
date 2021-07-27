// SPDX-License-Identifier: MIT
#include "usd-to-json.hh"

#include "tinyusdz.hh"

// nlohmann json
#include "nlohmann/json.hpp"

using namespace nlohmann;

namespace tinyusd {

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

  { auto normals = mesh.normals.buffer.GetAsVec3fArray();
    if (normals.size()) {
      j["normals"] = normals;
    }
  }
  // TODO: Subdivision surface
  j["doubleSided"] = mesh.doubleSided;

  j["purpose"] = mesh.purpose;

	return j;
}

nonstd::expected<std::string, std::string> ToJSON() {
  return nonstd::make_unexpected("TODO");
}


} // namespace tinyusd