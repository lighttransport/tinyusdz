// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - Present, Syoyo Fujita.
//
// composition-types.hh - USD composition types (AssetInfo, Reference, Payload, etc.)
//
#pragma once

#include <string>
#include <vector>

#include "path.hh"
#include "meta-variable.hh"
#include "prim-enums.hh"
#include "value-types.hh"

namespace tinyusdz {

struct AssetInfo {
  // builtin fields
  value::AssetPath identifier;
  std::string name;
  std::vector<value::AssetPath> payloadAssetDependencies;
  std::string version;

  // Other fields
  Dictionary _fields;
};

struct APISchemas {
  // TinyUSDZ does not allow user-supplied API schema for now
  enum class APIName {
    // usdShade
    MaterialBindingAPI,  // "MaterialBindingAPI"
    ConnectableAPI, // "ConnectableAPI"
    CoordSysAPI, // "CoordSysAPI"
    NodeDefAPI, // "NodeDefAPI"

    CollectionAPI,      // "CollectionAPI"
    // usdGeom
    GeomModelAPI, // "GeomModelAPI"
    MotionAPI, // "MotionAPI"
    PrimvarsAPI, // "PrimvarsAPI"
    VisibilityAPI, // "VisibilityAPI"
    XformCommonAPI, // "XformCommonAPI"

    // usdLux
    LightAPI, // "LightAPI"
    LightListAPI, // "LightListAPI"
    ListAPI, // "ListAPI"
    MeshLightAPI, // "MeshLightAPI"
    ShapingAPI, // "ShapingAPI"
    ShadowAPI,  // "ShadowAPI"
    VolumeLightAPI,  // "VolumeLightAPI"

    // usdSkel
    SkelBindingAPI,      // "SkelBindingAPI"

    // USDZ AR extensions
    Preliminary_AnchoringAPI,
    Preliminary_PhysicsColliderAPI,
    Preliminary_PhysicsMaterialAPI,
    Preliminary_PhysicsRigidBodyAPI,
  };

  ListEditQual listOpQual{ListEditQual::ResetToExplicit};  // must be 'prepend'

  // std::get<1>: instance name. For Multi-apply API Schema e.g.
  // `material:MainMaterial` for `CollectionAPI:material:MainMaterial`
  std::vector<std::pair<APIName, std::string>> names;

  // Unknown/unsupported API schemas - stored as raw strings to preserve them
  // Each entry is (schema_name, instance_name) where instance_name may be empty
  std::vector<std::pair<std::string, std::string>> unknownSchemas;
};

// SdfLayerOffset
struct LayerOffset {
  double _offset{0.0};
  double _scale{1.0};
};

// SdfReference
struct Reference {
  value::AssetPath asset_path;
  Path prim_path;
  LayerOffset layerOffset;
  Dictionary customData;
};

// SdfPayload
struct Payload {
  value::AssetPath asset_path;  // std::string in SdfPayload
  Path prim_path;
  LayerOffset layerOffset;  // from 0.8.0
  // No customData for Payload

  // NOTE: pxrUSD encodes `payload = None` as Payload with empty paths in USDC(Crate).
  // (Not ValueBlock)
  bool is_none() const {
    return asset_path.GetAssetPath().empty() && !prim_path.is_valid();
  }
};

namespace value {

#include "define-type-trait.inc"

DEFINE_TYPE_TRAIT(Reference, "ref", TYPE_ID_REFERENCE, 1);
DEFINE_TYPE_TRAIT(Payload, "payload", TYPE_ID_PAYLOAD, 1);
DEFINE_TYPE_TRAIT(LayerOffset, "LayerOffset", TYPE_ID_LAYER_OFFSET, 1);

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

}  // namespace value

}  // namespace tinyusdz
