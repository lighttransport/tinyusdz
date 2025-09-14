// SPDX-License-Identifier: Apache 2.0

///
/// @file metadata.cc
/// @brief USD metadata types implementation
///
/// Implementation of metadata structures for Prims and Layers.
///
#include "metadata.hh"
#include "dictionary.hh"
#include "enum-types.hh"
#include "str-util.hh"
#include "api-schemas.hh"

namespace tinyusdz {

AssetInfo PrimMetas::get_assetInfo(bool *is_authored) const {
  AssetInfo ainfo;

  if (is_authored) {
    (*is_authored) = authored();
  }

  if (authored()) {
    ainfo._fields = meta;

    {
      MetaVariable identifier_var;
      if (GetCustomDataByKey(meta, "identifier", &identifier_var)) {
        std::string identifier;
        if (identifier_var.get_value<std::string>(&identifier)) {
          ainfo.identifier = identifier;
          ainfo._fields.erase("identifier");
        }
      }
    }

    {
      MetaVariable name_var;
      if (GetCustomDataByKey(meta, "name", &name_var)) {
        std::string name;
        if (name_var.get_value<std::string>(&name)) {
          ainfo.name = name;
          ainfo._fields.erase("name");
        }
      }
    }

    {
      MetaVariable payloadDeps_var;
      if (GetCustomDataByKey(meta, "payloadAssetDependencies",
                             &payloadDeps_var)) {
        std::vector<value::AssetPath> assets;
        if (payloadDeps_var.get_value<std::vector<value::AssetPath>>(&assets)) {
          ainfo.payloadAssetDependencies = assets;
          ainfo._fields.erase("payloadAssetDependencies");
        }
      }
    }

    {
      MetaVariable version_var;
      if (GetCustomDataByKey(meta, "version", &version_var)) {
        std::string version;
        if (version_var.get_value<std::string>(&version)) {
          ainfo.version = version;
          ainfo._fields.erase("version");
        }
      }
    }
  }

  return ainfo;
}

const std::string PrimMetas::get_kind() const {

  if (kind.has_value()) {
    if (kind.value() == Kind::UserDef) {
      return _kind_str;
    } else {
      return to_string(kind.value());
    }
  }

  return "";
}

PrimMetas::~PrimMetas() {
  delete apiSchemas;
}

PrimMetas& PrimMetas::operator=(const PrimMetas& rhs) {
  if (this == &rhs) return *this;
  
  // Copy all members
  active = rhs.active;
  hidden = rhs.hidden;
  kind = rhs.kind;
  _kind_str = rhs._kind_str;
  assetInfo = rhs.assetInfo;
  customData = rhs.customData;
  doc = rhs.doc;
  comment = rhs.comment;
  sdrMetadata = rhs.sdrMetadata;
  instanceable = rhs.instanceable;
  clips = rhs.clips;
  references = rhs.references;
  payload = rhs.payload;
  inherits = rhs.inherits;
  variantSets = rhs.variantSets;
  variants = rhs.variants;
  specializes = rhs.specializes;
  sceneName = rhs.sceneName;
  displayName = rhs.displayName;
  unregisteredMetas = rhs.unregisteredMetas;
  meta = rhs.meta;
  primChildren = rhs.primChildren;
  properties = rhs.properties;
  inheritPaths = rhs.inheritPaths;
  variantChildren = rhs.variantChildren;
  variantSetChildren = rhs.variantSetChildren;
  
  // Handle apiSchemas pointer
  delete apiSchemas;
  apiSchemas = rhs.apiSchemas ? new APISchemas(*rhs.apiSchemas) : nullptr;
  
  return *this;
}

void PrimMetas::update_from(const PrimMetas &rhs, const bool override_authored) {
  if (rhs.active.has_value()) {
    if (override_authored || !active.has_value()) {
      active = rhs.active;
    }
  }

  if (rhs.hidden.has_value()) {
    if (override_authored || !hidden.has_value()) {
      hidden = rhs.hidden;
    }
  }

  if (rhs.kind.has_value()) {
    if (override_authored || !kind.has_value()) {
      kind = rhs.kind;
    }
  }

  if (rhs.instanceable.has_value()) {
    if (override_authored || !instanceable.has_value()) {
      instanceable = rhs.instanceable;
    }
  }

  if (rhs.assetInfo) {
    if (assetInfo) {
      OverrideDictionary(assetInfo.value(), rhs.assetInfo.value(), override_authored);
    } else if (override_authored) {
      assetInfo = rhs.assetInfo;
    }
  }

  if (rhs.clips) {
    if (clips) {
      OverrideDictionary(clips.value(), rhs.clips.value(), override_authored);
    } else if (override_authored) {
      clips = rhs.clips;
    }
  }

  if (rhs.customData) {
    if (customData) {
      OverrideDictionary(customData.value(), rhs.customData.value(), override_authored);
    } else if (override_authored) {
      customData = rhs.customData;
    }
  }

  if (rhs.doc) {
    if (override_authored || !doc.has_value()) {
      doc = rhs.doc;
    }
  }

  if (rhs.comment) {
    if (override_authored || !comment.has_value()) {
      comment = rhs.comment;
    }
  }

  if (rhs.apiSchemas) {
    if (override_authored || !apiSchemas) {
      delete apiSchemas;
      // Note: This creates a copy - in production code you'd want proper copy semantics
      apiSchemas = rhs.apiSchemas ? new APISchemas(*rhs.apiSchemas) : nullptr;
    }
  }

  if (rhs.sdrMetadata) {
    if (sdrMetadata) {
      OverrideDictionary(sdrMetadata.value(), rhs.sdrMetadata.value(), override_authored);
    } else if (override_authored) {
      sdrMetadata = rhs.sdrMetadata;
    }
  }

  if (rhs.sceneName) {
    if (override_authored || !sceneName.has_value()) {
      sceneName = rhs.sceneName;
    }
  }

  if (rhs.displayName) {
    if (override_authored || !displayName.has_value()) {
      displayName = rhs.displayName;
    }
  }

  if (rhs.references) {
    if (override_authored || !references.has_value()) {
      references = rhs.references;
    }
  }
  if (rhs.payload) {
    if (override_authored || !payload.has_value()) {
      payload = rhs.payload;
    }
  }
  if (rhs.inherits) {
    if (override_authored || !inherits.has_value()) {
      inherits = rhs.inherits;
    }
  }
  if (rhs.variantSets) {
    if (override_authored || !variantSets.has_value()) {
      variantSets = rhs.variantSets;
    }
  }
  if (rhs.variants) {
    if (override_authored || !variants.has_value()) {
      variants = rhs.variants;
    }
  }
  if (rhs.specializes) {
    if (override_authored || !specializes.has_value()) {
      specializes = rhs.specializes;
    }
  }

  if (rhs.unregisteredMetas.size()) {
    for (const auto &item : rhs.unregisteredMetas) {
      if (unregisteredMetas.count(item.first)) {
        if (override_authored) {
          unregisteredMetas[item.first] = item.second;
        } 
      } else {
        unregisteredMetas[item.first] = item.second;
      }
    }
  }

  OverrideDictionary(meta, rhs.meta, override_authored);
}

}  // namespace tinyusdz