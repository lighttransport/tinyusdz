// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 Light Transport Entertainment Inc.
//
// Prim metadata definitions and attribute metadata
// Metadata provides additional information about prims and properties

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

#include "prim-forward-decl.hh"
#include "value-types.hh"
#include "enum-types.hh"
#include "dictionary.hh"
#include "nonstd/optional.hpp"

namespace tinyusdz {

// Forward declarations
class APISchemas;
struct MetaVariable;

///
/// PrimMetas contains metadata for a prim.
/// This includes common metadata like active, hidden, kind, etc.
///
struct PrimMetas {
  PrimMetas() = default;
  ~PrimMetas();
  PrimMetas(const PrimMetas& other) { *this = other; }
  PrimMetas& operator=(const PrimMetas& other);
  
  // Common prim metadata
  nonstd::optional<bool> active;      // 'active' - Whether prim is active
  nonstd::optional<bool> hidden;      // 'hidden' - Whether prim is hidden
  nonstd::optional<bool> instanceable; // 'instanceable' - Whether prim can be instanced
  
  // Kind metadata
  nonstd::optional<Kind> kind;        // 'kind' - Prim's kind (model, group, etc.)
  std::string _kind_str;              // User-defined kind value (if not standard)
  
  // Documentation and comments
  nonstd::optional<value::StringData> doc;     // 'documentation'
  nonstd::optional<value::StringData> comment; // 'comment'
  
  // Asset and custom data
  nonstd::optional<Dictionary> assetInfo;   // 'assetInfo' - Asset-related metadata
  nonstd::optional<Dictionary> customData;  // 'customData' - User-defined metadata
  nonstd::optional<Dictionary> clips;       // 'clips' - Value clips metadata
  nonstd::optional<Dictionary> sdrMetadata; // 'sdrMetadata' - Shader metadata (usdShade)
  
  // API schemas
  APISchemas* apiSchemas{nullptr};    // 'apiSchemas' - Applied API schemas
  
  // Utility methods
  
  ///
  /// Get string representation of Kind.
  /// For user-defined Kind, it returns `_kind_str`
  ///
  const std::string get_kind() const;
  
  ///
  /// Check if any metadata is authored
  ///
  bool authored() const {
    return active || hidden || kind || instanceable || 
           doc || comment || assetInfo || customData || 
           clips || sdrMetadata || (apiSchemas != nullptr);
  }
  
  ///
  /// Clear all metadata
  ///
  void clear();
};

// Backward compatibility alias
using PrimMeta = PrimMetas;

///
/// AttrMetas contains metadata for attributes and properties.
/// This includes interpolation, visibility, connections, etc.
///
struct AttrMetas {
  // Interpolation and animation
  nonstd::optional<Interpolation> interpolation;  // 'interpolation'
  nonstd::optional<uint32_t> elementSize;         // 'elementSize' (usdSkel)
  nonstd::optional<double> weight;                // Weight for blendshapes (usdSkel)
  
  // Visibility
  nonstd::optional<bool> hidden;                  // 'hidden'
  
  // Documentation
  nonstd::optional<value::StringData> comment;    // 'comment'
  nonstd::optional<Dictionary> customData;        // 'customData'
  
  // Display metadata
  nonstd::optional<std::string> displayName;      // 'displayName'
  nonstd::optional<std::string> displayGroup;     // 'displayGroup'
  
  // UsdShade specific
  nonstd::optional<value::token> connectability;  // Connection behavior (for attributes)
  nonstd::optional<value::token> outputName;      // Output name (for relationships)
  nonstd::optional<value::token> renderType;      // Render type (for properties)
  nonstd::optional<Dictionary> sdrMetadata;       // Shader metadata
  
  // Material binding
  nonstd::optional<value::token> bindMaterialAs;  // 'bindMaterialAs' (for relationships)
  
  // Generic metadata storage
  std::map<std::string, MetaVariable> meta;       // Other metadata values
  std::vector<value::StringData> stringData;      // String-only metadata
  
  // Utility methods
  
  ///
  /// Check if colorSpace metadata exists
  ///
  bool has_colorSpace() const;
  
  ///
  /// Get colorSpace metadata value
  /// Returns empty token if not authored or not token type
  ///
  value::token get_colorSpace() const;
  
  ///
  /// Check if unauthoredValuesIndex metadata exists
  ///
  bool has_unauthoredValuesIndex() const;
  
  ///
  /// Get unauthoredValuesIndex metadata value
  /// Returns -1 if not authored or not int type
  ///
  int get_unauthoredValuesIndex() const;
  
  ///
  /// Check if any metadata is authored
  ///
  bool authored() const {
    return interpolation || elementSize || hidden || customData || weight ||
           connectability || outputName || renderType || sdrMetadata || 
           displayName || displayGroup || bindMaterialAs || 
           meta.size() || stringData.size();
  }
  
  ///
  /// Clear all metadata
  ///
  void clear() {
    interpolation.reset();
    elementSize.reset();
    weight.reset();
    hidden.reset();
    comment.reset();
    customData.reset();
    displayName.reset();
    displayGroup.reset();
    connectability.reset();
    outputName.reset();
    renderType.reset();
    sdrMetadata.reset();
    bindMaterialAs.reset();
    meta.clear();
    stringData.clear();
  }
};

// Backward compatibility aliases
using AttrMeta = AttrMetas;
using PropMetas = AttrMetas;

///
/// StageMetas contains metadata for the USD stage/layer.
/// This includes default prim, up axis, units, etc.
///
struct StageMetas {
  // Stage identification
  value::token defaultPrim;                      // 'defaultPrim'
  std::vector<value::AssetPath> subLayers;       // 'subLayers'
  
  // Documentation
  value::StringData doc;                         // 'doc' or 'documentation'
  value::StringData comment;                     // 'comment'
  
  // Coordinate system and units
  nonstd::optional<Axis> upAxis;                 // 'upAxis'
  nonstd::optional<double> metersPerUnit;        // 'metersPerUnit'
  nonstd::optional<double> kilogramsPerUnit;     // 'kilogramsPerUnit'
  
  // Time and animation
  nonstd::optional<double> timeCodesPerSecond;   // 'timeCodesPerSecond'
  nonstd::optional<double> startTimeCode;        // 'startTimeCode'
  nonstd::optional<double> endTimeCode;          // 'endTimeCode'
  nonstd::optional<double> framesPerSecond;      // 'framesPerSecond'
  
  // Playback
  nonstd::optional<bool> autoPlay;               // 'autoPlay'
  nonstd::optional<value::token> playbackMode;   // 'playbackMode' ('none' or 'loop')
  
  // Custom metadata
  std::map<std::string, MetaVariable> customLayerData; // 'customLayerData'
  
  ///
  /// Check if any stage metadata is authored
  ///
  bool authored() const {
    return !defaultPrim.str().empty() || !subLayers.empty() ||
           !doc.value.empty() || !comment.value.empty() ||
           upAxis || metersPerUnit || kilogramsPerUnit ||
           timeCodesPerSecond || startTimeCode || endTimeCode ||
           framesPerSecond || autoPlay || playbackMode ||
           !customLayerData.empty();
  }
  
  ///
  /// Clear all stage metadata
  ///
  void clear() {
    defaultPrim = value::token();
    subLayers.clear();
    doc = value::StringData();
    comment = value::StringData();
    upAxis.reset();
    metersPerUnit.reset();
    kilogramsPerUnit.reset();
    timeCodesPerSecond.reset();
    startTimeCode.reset();
    endTimeCode.reset();
    framesPerSecond.reset();
    autoPlay.reset();
    playbackMode.reset();
    customLayerData.clear();
  }
};

///
/// Utility functions for metadata
///

///
/// Check if a metadata key is valid
///
bool IsValidMetadataKey(const std::string& key);

///
/// Check if a metadata value is valid for a given key
///
bool IsValidMetadataValue(const std::string& key, const MetaVariable& value);

///
/// Get default value for a metadata key
///
nonstd::optional<MetaVariable> GetDefaultMetadataValue(const std::string& key);

///
/// Merge metadata from source to destination
/// Existing values in destination are overwritten
///
void MergeMetadata(AttrMetas& dest, const AttrMetas& src);
void MergeMetadata(PrimMetas& dest, const PrimMetas& src);
void MergeMetadata(StageMetas& dest, const StageMetas& src);

} // namespace tinyusdz