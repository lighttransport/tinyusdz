// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Property Index Implementation

#include "property-index.hh"
#include <algorithm>
#include <cstring>

namespace tinyusdz {
namespace next {

// ============================================================
// PropNameTable
// ============================================================

PropNameTable::PropNameTable() {
  // Reserve space for common names
  names_.reserve(256);
}

PropNameTable::~PropNameTable() = default;

PropNameId PropNameTable::intern(const std::string& name) {
  auto it = name_to_id_.find(name);
  if (it != name_to_id_.end()) {
    return PropNameId{it->second};
  }

  uint32_t id = static_cast<uint32_t>(names_.size());
  names_.push_back(name);
  name_to_id_[name] = id;
  return PropNameId{id};
}

PropNameId PropNameTable::intern(const char* name) {
  if (!name) return PropNameId{};
  return intern(std::string(name));
}

const std::string& PropNameTable::get(PropNameId id) const {
  static const std::string empty;
  if (id.id >= names_.size()) return empty;
  return names_[id.id];
}

PropNameId PropNameTable::find(const std::string& name) const {
  auto it = name_to_id_.find(name);
  if (it != name_to_id_.end()) {
    return PropNameId{it->second};
  }
  return PropNameId{};
}

PropNameId PropNameTable::find(const char* name) const {
  if (!name) return PropNameId{};
  return find(std::string(name));
}

void PropNameTable::register_common_names() {
  // Pre-register common USD property names
  // These get the lowest IDs for fastest lookup
  id_points = intern("points");
  id_normals = intern("normals");
  id_primvars_st = intern("primvars:st");
  id_extent = intern("extent");
  id_visibility = intern("visibility");
  id_purpose = intern("purpose");
  id_xformOpOrder = intern("xformOpOrder");
  id_faceVertexCounts = intern("faceVertexCounts");
  id_faceVertexIndices = intern("faceVertexIndices");
  id_subdivisionScheme = intern("subdivisionScheme");
  id_interpolateBoundary = intern("interpolateBoundary");
  id_radius = intern("radius");
  id_width = intern("width");
  id_height = intern("height");
  id_size = intern("size");

  // Additional common names
  intern("doubleSided");
  intern("orientation");
  intern("displayColor");
  intern("displayOpacity");
  intern("velocities");
  intern("accelerations");
  intern("ids");
  intern("widths");
  intern("cornerIndices");
  intern("cornerSharpnesses");
  intern("creaseIndices");
  intern("creaseLengths");
  intern("creaseSharpnesses");
  intern("holeIndices");

  // XformOp names
  intern("xformOp:translate");
  intern("xformOp:rotateX");
  intern("xformOp:rotateY");
  intern("xformOp:rotateZ");
  intern("xformOp:rotateXYZ");
  intern("xformOp:rotateXZY");
  intern("xformOp:rotateYXZ");
  intern("xformOp:rotateYZX");
  intern("xformOp:rotateZXY");
  intern("xformOp:rotateZYX");
  intern("xformOp:scale");
  intern("xformOp:orient");
  intern("xformOp:transform");

  // Material binding
  intern("material:binding");
  intern("material:binding:preview");
  intern("material:binding:full");

  // Shader inputs/outputs
  intern("inputs:diffuseColor");
  intern("inputs:emissiveColor");
  intern("inputs:metallic");
  intern("inputs:roughness");
  intern("inputs:opacity");
  intern("inputs:normal");
  intern("inputs:occlusion");
  intern("inputs:clearcoat");
  intern("inputs:clearcoatRoughness");
  intern("inputs:ior");
  intern("inputs:specularColor");
  intern("inputs:useSpecularWorkflow");
  intern("inputs:file");
  intern("inputs:st");
  intern("inputs:wrapS");
  intern("inputs:wrapT");
  intern("inputs:fallback");
  intern("inputs:varname");
  intern("outputs:surface");
  intern("outputs:displacement");
  intern("outputs:volume");
  intern("outputs:rgb");
  intern("outputs:r");
  intern("outputs:g");
  intern("outputs:b");
  intern("outputs:a");

  // Light properties
  intern("intensity");
  intern("exposure");
  intern("color");
  intern("enableColorTemperature");
  intern("colorTemperature");
  intern("diffuse");
  intern("specular");
  intern("normalize");
  intern("inputs:intensity");
  intern("inputs:exposure");
  intern("inputs:color");
  intern("inputs:radius");
  intern("inputs:angle");
  intern("inputs:softness");
  intern("inputs:length");
  intern("inputs:width");
  intern("inputs:height");
  intern("inputs:texture:file");
  intern("inputs:texture:format");

  // Skeleton properties
  intern("joints");
  intern("jointNames");
  intern("bindTransforms");
  intern("restTransforms");
  intern("rotations");
  intern("translations");
  intern("scales");
  intern("blendShapes");
  intern("blendShapeWeights");
  intern("skeleton");
  intern("animationSource");
  intern("primvars:skel:jointIndices");
  intern("primvars:skel:jointWeights");
  intern("primvars:skel:geomBindTransform");
}

// Global singleton
PropNameTable& GetPropNameTable() {
  static PropNameTable table;
  static bool initialized = false;
  if (!initialized) {
    table.register_common_names();
    initialized = true;
  }
  return table;
}

// ============================================================
// PropIndex
// ============================================================

void PropIndex::reserve(size_t count) {
  slots_.reserve(count);
}

void PropIndex::add(PropSlot slot) {
  slots_.push_back(slot);
  sorted_ = false;
}

const PropSlot* PropIndex::find(PropNameId name_id) const {
  if (!name_id.is_valid()) return nullptr;

  if (sorted_) {
    // Binary search
    auto it = std::lower_bound(slots_.begin(), slots_.end(), name_id,
        [](const PropSlot& slot, PropNameId id) {
          return slot.name_id < id;
        });
    if (it != slots_.end() && it->name_id == name_id) {
      return &(*it);
    }
  } else {
    // Linear search
    for (const auto& slot : slots_) {
      if (slot.name_id == name_id) {
        return &slot;
      }
    }
  }
  return nullptr;
}

const PropSlot* PropIndex::find(const std::string& name) const {
  PropNameId id = GetPropNameTable().find(name);
  if (!id.is_valid()) {
    // Name not in table - do linear search by string
    const auto& table = GetPropNameTable();
    for (const auto& slot : slots_) {
      if (table.get(slot.name_id) == name) {
        return &slot;
      }
    }
    return nullptr;
  }
  return find(id);
}

void PropIndex::sort() {
  std::sort(slots_.begin(), slots_.end(),
      [](const PropSlot& a, const PropSlot& b) {
        return a.name_id < b.name_id;
      });
  sorted_ = true;
}

}  // namespace next
}  // namespace tinyusdz
