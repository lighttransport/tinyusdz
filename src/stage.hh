// SPDX-License-Identifier: Apache 2.
// Copyright 2022 - Present, Light Transport Entertainment, Inc.
//
// Stage: Similar to Scene or Scene graph
#pragma once

#include "prim-types.hh"

namespace tinyusdz {

struct StageMetas {
  // TODO: Support more predefined properties: reference = <pxrUSD>/pxr/usd/sdf/wrapLayer.cpp
  // Scene global setting
  TypedAttributeWithFallback<Axis> upAxis{Axis::Y}; // This can be changed by plugInfo.json in USD: https://graphics.pixar.com/usd/dev/api/group___usd_geom_up_axis__group.html#gaf16b05f297f696c58a086dacc1e288b5
  value::token defaultPrim;           // prim node name
  TypedAttributeWithFallback<double> metersPerUnit{1.0};        // default [m]
  TypedAttributeWithFallback<double> timeCodesPerSecond {24.0};  // default 24 fps
  TypedAttributeWithFallback<double> framesPerSecond {24.0};  // FIXME: default 24 fps
  TypedAttributeWithFallback<double> startTimeCode{0.0}; // FIXME: default = -inf?
  TypedAttributeWithFallback<double> endTimeCode{std::numeric_limits<double>::infinity()};
  std::vector<value::AssetPath> subLayers; // `subLayers`
  StringData comment; // 'comment'
  StringData doc; // `documentation`

  CustomDataType customLayerData; // customLayerData

  // String only metadataum.
  // TODO: Represent as `MetaVariable`?
  std::vector<StringData> stringData;
};

class PrimRange;

// Similar to UsdStage, but much more something like a Scene(scene graph)
class Stage {
 public:

  static Stage CreateInMemory() {
    return Stage();
  }

  ///
  /// Traverse by depth-first order.
  /// NOTE: Not yet implementd
  ///
  PrimRange Traverse();

  ///
  /// Get Prim at a Path.
  /// Path must be absolute Path.
  ///
  /// @returns pointer to Prim(to avoid a copy). Never return nullptr upon success.
  ///
  nonstd::expected<const Prim *, std::string> GetPrimAtPath(const Path &path) const;

  ///
  /// Get Prim from a children of given root Prim.
  /// Path must be relative Path.
  ///
  /// @returns pointer to Prim(to avoid a copy). Never return nullptr upon success.
  ///
  nonstd::expected<const Prim *, std::string> GetPrimFromRelativePath(const Prim &root, const Path &path) const;

  ///
  /// Dump Stage as ASCII(USDA) representation.
  ///
  std::string ExportToString() const;


  const std::vector<Prim> &GetRootPrims() const {
    return root_nodes;
  }

  std::vector<Prim> &GetRootPrims() {
    return root_nodes;
  }

  const StageMetas &GetMetas() const {
    return stage_metas;
  }

  StageMetas &GetMetas() {
    return stage_metas;
  }

  ///
  /// Compose scene.
  ///
  bool Compose(bool addSourceFileComment = true) const;

  ///
  /// pxrUSD Compat API
  ///
  bool Flatten(bool addSourceFileComment = true) const {
    return Compose(addSourceFileComment);
  }


 private:

  // Root nodes
  std::vector<Prim> root_nodes;

  std::string name;       // Scene name
  int64_t default_root_node{-1};  // index to default root node

  StageMetas stage_metas;

  mutable std::string _err;
  mutable std::string _warn;

  // Cached prim path.
  // key : prim_part string (e.g. "/path/bora")
  mutable std::map<std::string, const Prim *> _prim_path_cache;

  mutable bool _dirty{false}; // True when Stage content changes(addition, deletion, composition/flatten, etc.)

};

} // namespace tinyusdz
