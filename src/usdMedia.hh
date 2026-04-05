// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
/// @file usdMedia.hh
/// @brief USD Media schema definitions
///
/// Implements the SpatialAudio schema from UsdMedia.
///
#pragma once

#include "value-types.hh"
#include "core/prim-enums.hh"
#include "core/composition-types.hh"
#include "core/prim-metas.hh"
#include "core/typed-attribute.hh"
#include "core/property.hh"
#include "core/variant-types.hh"

namespace tinyusdz {

constexpr auto kSpatialAudio = "SpatialAudio";

struct SpatialAudio {
  std::string name;
  Specifier spec{Specifier::Def};
  int64_t parent_id{-1};

  void set_name(const std::string &name_) { name = name_; }
  const std::string &get_name() const { return name; }
  Specifier &specifier() { return spec; }
  const Specifier &specifier() const { return spec; }

  TypedAttribute<value::AssetPath> filePath;  // asset filePath
  // "spatial", "nonSpatial"
  TypedAttributeWithFallback<value::token> auralMode{value::token("spatial")};
  // "onceFromStart", "onceFromStartToEnd", "loopFromStart",
  // "loopFromStartToEnd", "loopFromStage"
  TypedAttributeWithFallback<value::token> playbackMode{value::token("onceFromStart")};
  TypedAttribute<double> startTime;   // timecode startTime
  TypedAttribute<double> endTime;     // timecode endTime
  TypedAttributeWithFallback<double> mediaOffset{0.0};  // double mediaOffset [seconds]
  TypedAttributeWithFallback<double> gain{1.0};          // double gain

  std::pair<ListEditQual, std::vector<Reference>> references;
  std::pair<ListEditQual, std::vector<Payload>> payload;
  std::map<std::string, VariantSet> variantSet;
  std::map<std::string, Property> props;

  PrimMeta meta;
  PrimMeta &metas() { return meta; }
  const PrimMeta &metas() const { return meta; }

  const std::vector<value::token> &primChildrenNames() const { return _primChildren; }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

// AssetPreviewsAPI — marker schema for thumbnail/preview metadata.
// Preview data is stored in assetInfo metadata, not as prim properties.
struct AssetPreviewsAPI {};

namespace value {

#include "define-type-trait.inc"
DEFINE_TYPE_TRAIT(SpatialAudio, kSpatialAudio, TYPE_ID_SPATIAL_AUDIO, 1);

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

}  // namespace value

}  // namespace tinyusdz
