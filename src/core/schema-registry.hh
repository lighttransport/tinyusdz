// SPDX-License-Identifier: Apache 2.0
// Copyright 2026 - Present, Syoyo Fujita.
//
// schema-registry.hh - Schema property registry for variability resolution
//
// AOUSD Core Spec 12.2.3: Variability is determined from the prim definition
// (schema). When not explicitly authored, the schema-defined variability
// applies as the weakest-opinion fallback.
//
#pragma once

#include <string>
#include <unordered_map>

#include "prim-enums.hh"  // Variability

namespace tinyusdz {

struct SchemaPropertyInfo {
  Variability variability{Variability::Varying};
  std::string type_name;  // e.g. "bool", "token", etc.
};

///
/// Singleton registry mapping (prim_type, property_name) -> SchemaPropertyInfo.
///
/// Used during composition to apply schema-defined variability when not
/// explicitly authored. Per AOUSD Core Spec 12.2.3, variability comes from
/// the prim definition and is resolved as the weakest opinion.
///
class SchemaRegistry {
 public:
  static SchemaRegistry &instance() {
    static SchemaRegistry reg;
    return reg;
  }

  /// Look up schema-defined property info.
  /// @return pointer to info if found, nullptr otherwise.
  const SchemaPropertyInfo *find(const std::string &prim_type,
                                  const std::string &prop_name) const {
    // Try exact (prim_type, prop_name) first
    std::string key = prim_type + ":" + prop_name;
    auto it = _registry.find(key);
    if (it != _registry.end()) {
      return &it->second;
    }

    // Fall back to wildcard ("*", prop_name) for properties common to all types
    key = "*:" + prop_name;
    it = _registry.find(key);
    if (it != _registry.end()) {
      return &it->second;
    }

    return nullptr;
  }

  /// Register a schema property.
  void register_property(const std::string &prim_type,
                          const std::string &prop_name,
                          const SchemaPropertyInfo &info) {
    std::string key = prim_type + ":" + prop_name;
    _registry[key] = info;
  }

 private:
  SchemaRegistry() { register_builtins(); }

  SchemaRegistry(const SchemaRegistry &) = delete;
  SchemaRegistry &operator=(const SchemaRegistry &) = delete;

  void register_builtins() {
    // UsdGeom common uniform properties (wildcard = all gprim types)
    register_property("*", "doubleSided",
                      {Variability::Uniform, "bool"});
    register_property("*", "orientation",
                      {Variability::Uniform, "token"});
    register_property("*", "purpose",
                      {Variability::Uniform, "token"});

    // UsdGeomMesh
    register_property("Mesh", "subdivisionScheme",
                      {Variability::Uniform, "token"});
    register_property("Mesh", "interpolateBoundary",
                      {Variability::Uniform, "token"});
    register_property("Mesh", "faceVaryingLinearInterpolation",
                      {Variability::Uniform, "token"});
    register_property("Mesh", "triangleSubdivisionRule",
                      {Variability::Uniform, "token"});

    // UsdGeomBasisCurves
    register_property("BasisCurves", "type",
                      {Variability::Uniform, "token"});
    register_property("BasisCurves", "basis",
                      {Variability::Uniform, "token"});
    register_property("BasisCurves", "wrap",
                      {Variability::Uniform, "token"});

    // UsdGeomSubset
    register_property("GeomSubset", "elementType",
                      {Variability::Uniform, "token"});
    register_property("GeomSubset", "familyName",
                      {Variability::Uniform, "token"});

    // UsdGeomCamera
    register_property("Camera", "projection",
                      {Variability::Varying, "token"});
    register_property("Camera", "stereoRole",
                      {Variability::Uniform, "token"});

    // UsdGeom primitives (axis alignment)
    register_property("Capsule", "axis",
                      {Variability::Uniform, "token"});
    register_property("Cylinder", "axis",
                      {Variability::Uniform, "token"});
    register_property("Cone", "axis",
                      {Variability::Uniform, "token"});

    // UsdShade
    register_property("Material", "surface",
                      {Variability::Uniform, "token"});
    register_property("Material", "displacement",
                      {Variability::Uniform, "token"});
    register_property("Material", "volume",
                      {Variability::Uniform, "token"});

    // UsdSkel
    register_property("Skeleton", "joints",
                      {Variability::Uniform, "token[]"});
    register_property("Skeleton", "jointNames",
                      {Variability::Uniform, "token[]"});
    register_property("SkelRoot", "purpose",
                      {Variability::Uniform, "token"});
  }

  std::unordered_map<std::string, SchemaPropertyInfo> _registry;
};

}  // namespace tinyusdz
