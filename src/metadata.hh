// SPDX-License-Identifier: Apache 2.0

///
/// @file metadata.hh
/// @brief USD metadata types and structures
///
/// Contains metadata structures for Prims and Layers, including AssetInfo,
/// PrimMetas, and LayerMetas. These structures hold metadata information
/// that describes properties and characteristics of USD elements.
///
#pragma once

#include <string>
#include <vector>
#include <map>
#include <limits>
#include <memory>

//
#include "value-types.hh"
#include "path.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "nonstd/expected.hpp"
#include "nonstd/optional.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "dictionary.hh"
#include "list-op.hh"
#include "enum-types.hh"
#include "attribute.hh"

namespace tinyusdz {

// Forward declarations
struct APISchemas;
using VariantSelectionMap = std::map<std::string, std::string>;

struct AssetInfo {
  // builtin fields
  value::AssetPath identifier;
  std::string name;
  std::vector<value::AssetPath> payloadAssetDependencies;
  std::string version;

  // Other fields
  Dictionary _fields;
};

// SdfLayerOffset
struct LayerOffset {
  double _offset{0.0};
  double _scale{1.0};
  
  // Constructors for refactored modules
  LayerOffset() = default;
  LayerOffset(double offset, double scale) : _offset(offset), _scale(scale) {}
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

// Metadata for Prim
struct PrimMetas {
  PrimMetas() = default;
  ~PrimMetas();
  PrimMetas(const PrimMetas& other) { *this = other; }
  PrimMetas& operator=(const PrimMetas& other);
  
  nonstd::optional<bool> active;  // 'active'
  nonstd::optional<bool> hidden;  // 'hidden'
  nonstd::optional<Kind> kind;    // 'kind'. user-defined kind value is stored in _kind_str;
  std::string _kind_str;

  nonstd::optional<Dictionary>
      assetInfo;  // 'assetInfo' // TODO: Use AssetInfo?
  nonstd::optional<Dictionary> customData;  // `customData`
  nonstd::optional<value::StringData> doc;  // 'documentation'
  nonstd::optional<value::StringData>
      comment;  // 'comment'  (String only metadata value)
  APISchemas* apiSchemas{nullptr};  // 'apiSchemas'
  nonstd::optional<Dictionary>
      sdrMetadata;  // 'sdrMetadata' (usdShade Prim only?)

  nonstd::optional<bool> instanceable; // 'instanceable'
  nonstd::optional<Dictionary> clips; // 'clips'

  // String representation of Kind.
  // For user-defined Kind, it returns `_kind_str`
  const std::string get_kind() const;

  //
  // AssetInfo utility function
  //
  // Convert CustomDataType to AssetInfo
  AssetInfo get_assetInfo(bool *authored = nullptr) const;

  //
  // Compositions
  //
  nonstd::optional<std::pair<ListEditQual, std::vector<Reference>>> references;
  nonstd::optional<std::pair<ListEditQual, std::vector<Payload>>>
      payload;  // NOTE: not `payloads`
  nonstd::optional<std::pair<ListEditQual, std::vector<Path>>>
      inherits;  // 'inherits'
  nonstd::optional<std::pair<ListEditQual, std::vector<std::string>>>
      variantSets;  // 'variantSets'. Could be `token` but treat as
                    // `string`(Crate format uses `string`)

  nonstd::optional<VariantSelectionMap> variants;  // `variants`

  nonstd::optional<std::pair<ListEditQual, std::vector<Path>>>
      specializes;  // 'specializes'

  // USDZ extensions
  nonstd::optional<std::string> sceneName;  // 'sceneName'

  // Omniverse extensions(TODO: Use UTF8 string type?)
  // https://github.com/PixarAnimationStudios/USD/pull/2055
  nonstd::optional<std::string> displayName;  // 'displayName'

  // Unregistered metadatum. value is represented as string.
  std::map<std::string, std::string> unregisteredMetas;

  Dictionary meta;  // other non-buitin meta values. TODO: remove this variable
                    // and use `customData` instead, since pxrUSD does not allow
                    // non-builtin Prim metadatum

  ///
  /// Update metadatum with rhs(authored metadataum only)
  ///
  /// @param[in] override_authored true: override this.metadataum(authored or not-authored) when rhs.metadatum is authoerd, false override only when this.metadatum is not authored and rhs.metadataum is authored.
  ///
  void update_from(const PrimMetas &rhs, bool override_authored = true);

  // FIXME: Find a better way to detect Prim meta is authored...
  bool authored() const {
    return (active || hidden || kind || customData || references || payload ||
            inherits || variants || variantSets || specializes || displayName ||
            sceneName || doc || comment || unregisteredMetas.size() || meta.size() || apiSchemas ||
            sdrMetadata || assetInfo || instanceable || clips);
  }

  //
  // Infos used indirectly.
  //

  // Used to display/traverse Prim items based on this array
  // USDA: By appearance. USDC: "primChildren" TokenVector field
  std::vector<value::token> primChildren;

  // Used to display/traverse Property items based on this array
  // USDA: By appearance. USDC: "properties" TokenVector field
  std::vector<value::token> properties;

  nonstd::optional<std::pair<ListEditQual, std::vector<Path>>> inheritPaths;

  nonstd::optional<std::vector<value::token>> variantChildren;
  nonstd::optional<std::vector<value::token>> variantSetChildren;
};

// For backward compatibility
using PrimMeta = PrimMetas;

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

  // USDZ extension
  TypedAttributeWithFallback<bool> autoPlay{
      true};  // default(or not authored) = auto play
  TypedAttributeWithFallback<PlaybackMode> playbackMode{
      PlaybackMode::PlaybackModeLoop};

  // Indirectly used.
  std::vector<value::token> primChildren;
};

}  // namespace tinyusdz