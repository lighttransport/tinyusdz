// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
#include "usd-to-json.hh"

#include "tinyusdz.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// nlohmann json
#include "external/jsonhpp/nlohmann/json.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "common-macros.inc"
#include "pprinter.hh"

using namespace nlohmann;

namespace tinyusdz {

namespace {

json ToJSON(tinyusdz::Xform& xform) {
  json j;

  j["name"] = xform.name;
  j["typeName"] = "Xform";

  if (xform.xformOps.size()) {
    json jxformOpOrder;

    std::vector<std::string> ops;
    for (const auto &xformOp : xform.xformOps) {
      ops.push_back(xformOp.suffix);
    }

    j["xformOpOrder"] = ops;
  }

  return j;
}

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

json ToJSON(const tinyusdz::value::Value &v) {
  if (auto pv = v.get_value<tinyusdz::Xform>()) {
    return ToJSON(pv.value());
  }


  return json();


}

nonstd::expected<json, std::string> ToJSON(const tinyusdz::StageMetas& metas) {
  json j;

  if (metas.upAxis.authored()) {
    j["upAxis"] = to_string(metas.upAxis.get_value());
  }

  if (metas.comment.value.size()) {
    // TODO: escape and quote
    j["comment"] = metas.comment.value;
  }

  return j;
}

bool PrimToJSONRec(json &root, const tinyusdz::Prim& prim, int depth) {
  json j = ToJSON(prim.data());

  json jchildren = json::object();

  // TODO: Traverse Prim according to primChildren.
  for (const auto &child : prim.children()) {
    json cj;
    if (!PrimToJSONRec(cj, child, depth+1)) {
      return false;
    }
    std::string cname = child.element_name();
    jchildren[cname] = cj;
  }

  if (jchildren.size()) {
    j["primChildren"] = jchildren;
  }

  root[prim.element_name()] = j;

  return true;
}

json ToJSON(const tinyusdz::Layer& layer) {
  json j;

  // Layer name
  j["name"] = layer.name();
  j["typeName"] = "Layer";

  // Layer metadata
  const LayerMetas& metas = layer.metas();
  
  json layerMetas;
  
  // Basic layer properties
  if (metas.upAxis.authored()) {
    layerMetas["upAxis"] = to_string(metas.upAxis.get_value());
  }
  
  if (!metas.defaultPrim.str().empty()) {
    layerMetas["defaultPrim"] = metas.defaultPrim.str();
  }
  
  if (metas.metersPerUnit.authored()) {
    layerMetas["metersPerUnit"] = metas.metersPerUnit.get_value();
  }
  
  if (metas.timeCodesPerSecond.authored()) {
    layerMetas["timeCodesPerSecond"] = metas.timeCodesPerSecond.get_value();
  }
  
  if (metas.framesPerSecond.authored()) {
    layerMetas["framesPerSecond"] = metas.framesPerSecond.get_value();
  }
  
  if (metas.startTimeCode.authored()) {
    layerMetas["startTimeCode"] = metas.startTimeCode.get_value();
  }
  
  if (metas.endTimeCode.authored()) {
    layerMetas["endTimeCode"] = metas.endTimeCode.get_value();
  }
  
  if (metas.kilogramsPerUnit.authored()) {
    layerMetas["kilogramsPerUnit"] = metas.kilogramsPerUnit.get_value();
  }
  
  // SubLayers
  if (metas.subLayers.size() > 0) {
    json subLayersArray = json::array();
    for (const auto& subLayer : metas.subLayers) {
      json subLayerObj;
      subLayerObj["assetPath"] = subLayer.assetPath.GetAssetPath();
      // TODO: layerOffset
      //if (subLayer.layerOffset.offset != 0.0 || subLayer.layerOffset.scale != 1.0) {
      //  json layerOffsetObj;
      //  layerOffsetObj["offset"] = subLayer.layerOffset.offset;
      //  layerOffsetObj["scale"] = subLayer.layerOffset.scale;
      //  subLayerObj["layerOffset"] = layerOffsetObj;
      //}
      subLayersArray.push_back(subLayerObj);
    }
    layerMetas["subLayers"] = subLayersArray;
  }
  
  // Documentation and comment
  if (!metas.doc.value.empty()) {
    layerMetas["doc"] = metas.doc.value;
  }
  
  if (!metas.comment.value.empty()) {
    layerMetas["comment"] = metas.comment.value;
  }
  
  // Custom layer data
  if (metas.customLayerData.size() > 0) {
    json customData;
    for (const auto& item : metas.customLayerData) {
      // TODO: Implement proper custom data serialization
      customData[item.first] = "[CustomData]";
    }
    layerMetas["customLayerData"] = customData;
  }
  
  // USDZ extensions
  if (metas.autoPlay.authored()) {
    layerMetas["autoPlay"] = metas.autoPlay.get_value();
  }
  
  if (metas.playbackMode.authored()) {
    auto playbackMode = metas.playbackMode.get_value();
    if (playbackMode == LayerMetas::PlaybackMode::PlaybackModeLoop) {
      layerMetas["playbackMode"] = "loop";
    } else {
      layerMetas["playbackMode"] = "none";
    }
  }
  
  // PrimChildren
  if (metas.primChildren.size() > 0) {
    json primChildrenArray = json::array();
    for (const auto& primChild : metas.primChildren) {
      primChildrenArray.push_back(primChild.str());
    }
    layerMetas["primChildren"] = primChildrenArray;
  }
  
  // Only add metas if there's content
  if (!layerMetas.empty()) {
    j["metas"] = layerMetas;
  }
  
  // PrimSpecs
  const auto& primspecs = layer.primspecs();
  if (primspecs.size() > 0) {
    json primSpecsObj;
    for (const auto& item : primspecs) {
      // TODO: Implement PrimSpec to JSON conversion
      json primSpecJson;
      primSpecJson["name"] = item.first;
      primSpecJson["typeName"] = "PrimSpec";
      // Add basic PrimSpec info - would need ToJSON for PrimSpec
      primSpecsObj[item.first] = primSpecJson;
    }
    j["primSpecs"] = primSpecsObj;
  }

  return j;
}

}  // namespace


nonstd::expected<std::string, std::string> ToJSON(
    const tinyusdz::Stage& stage) {
  json j;  // root

  auto jstageMetas = ToJSON(stage.metas());
  if (!jstageMetas) {
    return nonstd::make_unexpected(jstageMetas.error());
  }
  
  // Stage metadatum is represented as properties.
  if (!jstageMetas->is_null()) {
    j["properties"] = *jstageMetas;
  }

  j["version"] = 1.0;

  json cj;
  for (const auto& item : stage.root_prims()) {
    if (!PrimToJSONRec(cj, item, 0)) {
      return nonstd::make_unexpected("Failed to convert Prim to JSON.");
    }
  }

  j["primChildren"] = cj;

  tinyusdz::GeomMesh mesh;
  json jmesh = ToJSON(mesh);

  (void)jmesh;

  tinyusdz::GeomBasisCurves curves;
  json jcurves = ToJSON(curves);

  (void)jcurves;

  std::string str = j.dump(/* indent*/ 2);

  return str;
}

}  // namespace tinyusdz
