// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - Present, Syoyo Fujita.
//
// layer-types.hh - SubLayer and LayerMetas types
//
#pragma once

#include <limits>
#include <string>
#include <vector>

#include "composition-types.hh"
#include "typed-attribute.hh"
#include "prim-enums.hh"
#include "meta-variable.hh"
#include "animatable.hh"
#include "value-types.hh"

namespace tinyusdz {

struct SubLayer
{
  value::AssetPath assetPath;
  LayerOffset layerOffset;
};


struct LayerMetas {
  enum class PlaybackMode {
    PlaybackModeNone,
    PlaybackModeLoop,
  };

  // TODO: Support more predefined properties: reference =
  // <pxrUSD>/pxr/usd/sdf/wrapLayer.cpp Scene global setting
  TypedAttributeWithFallback<Axis> upAxis{
      Axis::
          Y};  // This can be changed by plugInfo.json in USD:
               // https://graphics.pixar.com/usd/dev/api/group___usd_geom_up_axis__group.html#gaf16b05f297f696c58a086dacc1e288b5
  value::token defaultPrim;                               // prim node name
  TypedAttributeWithFallback<double> metersPerUnit{1.0};  // default [m]
  TypedAttributeWithFallback<double> timeCodesPerSecond{
      24.0};  // default 24 fps
  TypedAttributeWithFallback<double> framesPerSecond{24.0};
  TypedAttributeWithFallback<double> startTimeCode{
      0.0};  // FIXME: default = -inf?
  TypedAttributeWithFallback<double> endTimeCode{
      std::numeric_limits<double>::infinity()};
  std::vector<SubLayer> subLayers;  // `subLayers`
  value::StringData comment;  // 'comment' In Stage meta, comment must be string
                              // only(`comment = "..."` is not allowed)
  value::StringData doc;      // `documentation`

  // UsdPhysics
  TypedAttributeWithFallback<double> kilogramsPerUnit{1.0};

  CustomDataType customLayerData;  // customLayerData
  bool customLayerDataAuthored{false};  // Track if customLayerData was explicitly authored (even if empty)

  // USDZ extension
  TypedAttributeWithFallback<bool> autoPlay{
      true};  // default(or not authored) = auto play
  TypedAttributeWithFallback<PlaybackMode> playbackMode{
      PlaybackMode::PlaybackModeLoop};

  // Indirectly used.
  std::vector<value::token> primChildren;

  ///
  /// Estimate memory usage of this LayerMetas in bytes
  ///
  size_t estimate_memory_usage() const {
    size_t total = sizeof(LayerMetas);
    // defaultPrim token
    total += defaultPrim.str().capacity();
    // subLayers
    for (const auto& sl : subLayers) {
      total += sizeof(SubLayer);
      total += sl.assetPath.GetAssetPath().capacity();
      total += sl.assetPath.GetResolvedPath().capacity();
    }
    // comment and doc strings
    total += comment.value.capacity();
    total += doc.value.capacity();
    // customLayerData (rough estimate - map of string to MetaVariable)
    for (const auto& kv : customLayerData) {
      total += kv.first.capacity();
      total += sizeof(MetaVariable);  // MetaVariable internal estimation not detailed
    }
    // primChildren tokens
    for (const auto& tok : primChildren) {
      total += tok.str().capacity();
    }
    return total;
  }
};

// Forward declaration for Layer class
// Layer class has been moved to layer.hh
class Layer;

}  // namespace tinyusdz
