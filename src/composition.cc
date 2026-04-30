// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment, Inc.

#include "composition.hh"

#include <algorithm>
#include <set>
#include <stack>

#if defined(__linux__)
#include <unistd.h>
#endif

#include "asset-resolution.hh"
#include "common-macros.inc"
#include "core/schema-registry.hh"
#include "namespace-mapping.hh"
#include "str-util.hh"
#include "io-util.hh"
#include "pprinter.hh"
#include "prim-pprint.hh"
#include "security-policy.hh"
#include "core/prim.hh"
#include "core/prim-spec.hh"
#include "layer.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "tinyusdz.hh"
#include "usdGeom.hh"
#include "usdLux.hh"
#include "usdMtlx.hh"
#include "usdShade.hh"
#include "usda-reader.hh"

#define PushError(s) \
  do { if (err) {         \
    (*err) += s;     \
  } } while(0)

#define PushWarn(s) \
  do { if (warn) {       \
    (*warn) += s;   \
  } } while(0)

namespace tinyusdz {

namespace {

bool IsVisited(const std::vector<std::set<std::string>> layer_names_stack,
               const std::string &name) {
  for (size_t i = 0; i < layer_names_stack.size(); i++) {
    if (layer_names_stack[i].count(name)) {
      return true;
    }
  }
  return false;
}

std::string GetExtension(const std::string &name) {
  return to_lower(io::GetFileExtension(name));
}

bool IsUSDFileFormat(const std::string &name) {
  std::string ext = GetExtension(name);

  return (ext.compare("usd") == 0) || (ext.compare("usda") == 0) ||
         (ext.compare("usdc") == 0) || (ext.compare("usdz") == 0);
}

#if defined(TINYUSDZ_WITH_USDOBJ)
bool IsWavefrontObjFileFormat(const std::string &name) {
  std::string ext = GetExtension(name);

  return ext.compare("obj") == 0;
}
#endif

bool IsMtlxFileFormat(const std::string &name) {
  std::string ext = GetExtension(name);

  return ext.compare("mtlx") == 0;
}

bool IsBuiltinFileFormat(const std::string &name) {
  if (IsUSDFileFormat(name)) {
    return true;
  }

  if (IsMtlxFileFormat(name)) {
    return true;
  }

#if defined(TINYUSDZ_WITH_USDOBJ)
  if (IsWavefrontObjFileFormat(name)) {
    return true;
  }
#endif

  return false;
}

bool ReplaceRootPrimPathRec(
  const Path &srcPrefix,
  const Path &dstPrefix,
  PrimSpec &ps,
  std::string *warn,
  std::string *err) {

  (void)warn;

  DCOUT("srcPrefix: " << srcPrefix);
  DCOUT("dstPrefix: " << dstPrefix);

  constexpr size_t kMaxIter = 1024 * 1024 * 128;

  std::vector<PrimSpec *> stack;
  stack.push_back(&ps);
  size_t iter = 0;

  while (!stack.empty()) {
    if (iter++ > kMaxIter) {
      PUSH_ERROR_AND_RETURN("PrimSpec tree too deep.");
    }

    PrimSpec *current = stack.back();
    stack.pop_back();

    for (auto &prop : current->props()) {

      if (prop.second.is_relationship()) {

        Relationship &rel = prop.second.relationship();

        if (rel.is_path()) {
          if (rel.targetPath.has_prefix(srcPrefix)) {
            rel.targetPath.replace_prefix(srcPrefix, dstPrefix);
          }
        } else if (rel.is_pathvector()) {

          for (auto &path : rel.targetPathVector) {
            if (path.has_prefix(srcPrefix)) {
              path.replace_prefix(srcPrefix, dstPrefix);
            }
          }
        }

      } else if (prop.second.is_attribute_connection()) {

        Attribute &attr = prop.second.attribute();
        for (auto &connPath : attr.connections()) {
          if (connPath.has_prefix(srcPrefix)) {
            connPath.replace_prefix(srcPrefix, dstPrefix);
          }
        }
      }

    }

    // Push children in reverse order to preserve DFS order.
    auto &children = current->children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
      stack.push_back(&(*it));
    }
  }

  return true;
}

// Copy assetresolver state to all PrimSpec in the tree.
bool PropagateAssetResolverState(PrimSpec &ps,
                                 const std::string &cwp,
                                 const std::vector<std::string> &search_paths) {
  constexpr size_t kMaxIter = 1024 * 1024 * 512;

  DCOUT("current_working_path: " << cwp);
  DCOUT("search_paths: " << search_paths);

  std::vector<PrimSpec *> stack;
  stack.push_back(&ps);
  size_t iter = 0;

  while (!stack.empty()) {
    if (iter++ > kMaxIter) {
      return false;
    }

    PrimSpec *current = stack.back();
    stack.pop_back();

    current->set_asset_resolution_state(cwp, search_paths);

    // Push children in reverse order to preserve DFS order.
    auto &children = current->children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
      stack.push_back(&(*it));
    }
  }

  return true;
}

// TODO: support loading non-USD asset
bool LoadAsset(AssetResolutionResolver &resolver,
               const std::string &current_working_path,
               const std::vector<std::string> &search_paths,
               const std::unordered_map<std::string, FileFormatHandler> &fileformats,
               const value::AssetPath &assetPath, const Path &primPath,
               Layer *dst_layer, const PrimSpec **dst_primspec_root,
               const bool error_when_no_prims_found,
               const bool error_when_asset_not_found,
               const bool error_when_unsupported_fileformat, std::string *warn,
               std::string *err) {
  if (!dst_layer) {
    PUSH_ERROR_AND_RETURN(
        "[Internal error]. `dst_layer` output arg is nullptr.");
  }

  std::string asset_path = assetPath.GetAssetPath();
  if (!security_policy::ValidateAndNormalizeAssetPath(asset_path, &asset_path)) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Unsafe asset path in composition: `{}`",
                    assetPath.GetAssetPath()));
  }
  std::string ext = GetExtension(asset_path);

  if (asset_path.empty()) {
    PUSH_ERROR_AND_RETURN(
        "TODO: No assetPath but Prim path(e.g. </xform>) in references.");
  }

  // TODO: Use std::stack to manage AssetResolutionResolver state?
  if (current_working_path.size()) {
    resolver.set_current_working_path(current_working_path);
  }

  if (search_paths.size()) {
    resolver.set_search_paths(search_paths);
  }

  // resolve path
  // TODO: Store resolved path to Reference?
  std::string resolved_path = resolver.resolve(asset_path);

  DCOUT("Loading references: " << resolved_path
                               << ", asset_path: " << asset_path);

  if (resolved_path.empty()) {
    if (error_when_asset_not_found) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Failed to resolve asset path `{}`", asset_path));
    } else {
      PUSH_WARN(fmt::format("Asset not found: `{}`", asset_path));
      PUSH_WARN(
          fmt::format("  current working path: `{}`", current_working_path));
      PUSH_WARN(fmt::format("  resolver.current_working_path: `{}`",
                            resolver.current_working_path()));
      PUSH_WARN(fmt::format("  search_paths: `{}`", search_paths));
      PUSH_WARN(fmt::format("  resolver.search_paths: `{}`",
                            resolver.search_paths()));
      (*dst_primspec_root) = nullptr;
      return true;
    }
  }

  resolver.set_search_paths(search_paths);

  // Use resolved asset_path's basedir for current working path.
  // Add resolved asset_path's basedir to search path.
  std::string base_dir = io::GetBaseDir(resolved_path);
  if (base_dir.size()) {
    DCOUT(fmt::format("Add `{}' to asset search path.", base_dir));

    resolver.set_current_working_path(base_dir);

    resolver.add_search_path(base_dir);
  }

  Asset asset;
  if (!resolver.open_asset(resolved_path, asset_path, &asset, warn, err)) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Failed to open asset `{}`.", resolved_path));
  }

  if (asset.size() > security_policy::kResolverMaxAssetReadBytes) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Resolved asset exceeds max bytes ({} > {}).",
                    asset.size(), security_policy::kResolverMaxAssetReadBytes));
  }

  DCOUT("Opened resolved assst: " << resolved_path
                                  << ", asset_path: " << asset_path);

  if (IsBuiltinFileFormat(asset_path)) {
    if (IsUSDFileFormat(asset_path) || IsMtlxFileFormat(asset_path)) {
      // ok
    } else {
      // TODO: obj
      if (error_when_unsupported_fileformat) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "TODO: Unknown/unsupported asset file format: {}", asset_path));
      } else {
        PUSH_WARN(fmt::format(
            "TODO: Unknown/unsupported asset file format. Skipped: {}",
            asset_path));
        return true;
      }
    }
  } else {
    if (fileformats.count(ext)) {
      DCOUT("Fileformat handler found for: " + ext);

    } else {
      DCOUT("Unknown/unsupported fileformat: " + ext);
      if (error_when_unsupported_fileformat) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Unknown/unsupported asset file format: {}", asset_path));
      } else {
        PUSH_WARN(fmt::format(
            "Unknown/unsupported asset file format. Skipped: {}", asset_path));
        return true;
      }
    }
  }

  Layer layer;
  std::string _warn;
  std::string _err;

  if (IsUSDFileFormat(asset_path)) {
    if (!LoadLayerFromMemory(asset.data(), asset.size(), asset_path, &layer,
                             &_warn, &_err)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Failed to open `{}` as Layer: {}", asset_path, _err));
    }
  } else if (IsMtlxFileFormat(asset_path)) {
    // primPath must be '</MaterialX>'
    if (primPath.prim_part() != "/MaterialX") {
      PUSH_ERROR_AND_RETURN("Prim path must be </MaterialX>, but got: " +
                            primPath.prim_part());
    }

    PrimSpec ps;
    if (!LoadMaterialXFromAsset(asset, asset_path, ps, &_warn, &_err)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Failed to open mtlx asset `{}`", asset_path));
    }

    ps.name() = "MaterialX";
    layer.primspecs()["MaterialX"] = ps;

  } else {
    if (fileformats.count(ext)) {
      PrimSpec ps;
      const FileFormatHandler &handler = fileformats.at(ext);

      if (!handler.reader(asset, ps, &_warn, &_err, handler.userdata)) {
        PUSH_ERROR_AND_RETURN(fmt::format("Failed to read asset `{}` error: {}",
                                          asset_path, _err));
      }

      if (ps.name().empty()) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "PrimSpec element_name is empty. asset `{}`", asset_path));
      }

      layer.primspecs()[ps.name()] = ps;
      DCOUT("Read asset from custom fileformat handler: " << ext);
    } else {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "FileFormat handler not found for asset `{}`", asset_path));
    }
  }

  DCOUT("layer = " << print_layer(layer, 0));

  // TODO: Recursively resolve `references`

  if (_warn.size()) {
    if (warn) {
      (*warn) += _warn;
    }
  }

  if (layer.primspecs().empty()) {
    if (error_when_no_prims_found) {
      PUSH_ERROR_AND_RETURN(fmt::format("No prims in layer `{}`", asset_path));
    }

    if (dst_primspec_root) {
      (*dst_primspec_root) = nullptr;
    }

    (*dst_layer) = std::move(layer);

    return true;
  }

  const PrimSpec *src_ps{nullptr};

  if (dst_primspec_root) {
    std::string default_prim;
    if (primPath.is_valid()) {
      default_prim = primPath.prim_part();
      DCOUT("primPath = " << default_prim);
    } else {
      // Use `defaultPrim` metadatum
      if (layer.metas().defaultPrim.valid()) {
        default_prim = "/" + layer.metas().defaultPrim.str();
        DCOUT("layer.meta.defaultPrim = " << default_prim);
      } else {
        // Use the first Prim in the layer.
        default_prim = "/" + layer.primspecs().begin()->first;
        DCOUT("layer.primspecs[0].name = " << default_prim);
      }
    }

    if (!layer.find_primspec_at(Path(default_prim, ""), &src_ps, err)) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "Failed to find PrimSpec `{}` in layer `{}`(resolved path: `{}`)",
          default_prim, asset_path, resolved_path));
    }

    if (!src_ps) {
      PUSH_ERROR_AND_RETURN("Internal error: PrimSpec pointer is nullptr.");
    }

    if (!PropagateAssetResolverState(*const_cast<PrimSpec *>(src_ps),
                                     resolver.current_working_path(),
                                     resolver.search_paths())) {
      PUSH_ERROR_AND_RETURN(
          "Store AssetResolver state to each PrimSpec failed.\n");
    }

    (*dst_primspec_root) = src_ps;
  }

  // FIXME: This may be redundant, since assetresulution state is stored in
  // each PrimSpec.
  // TODO: Remove layer-level assetresulution state store?
  //
  // save assetresolution state for nested composition.
  layer.set_asset_resolution_state(resolver.current_working_path(),
                                   resolver.search_paths(),
                                   resolver.get_userdata());

  (*dst_layer) = std::move(layer);

  return true;
}

bool CombinePrimSpecRec(uint32_t depth, PrimSpec &dst, const PrimSpec &src, std::string *warn,
                      std::string *err) {
  (void)warn;

  if (depth > (1024 * 1024 * 128)) {
    PUSH_ERROR_AND_RETURN("PrimSpec tree too deep.");
  }

  // Combine metadataum (weaker fills in where stronger is not authored)
  dst.metas().update_from(src.metas(), /* override_authored */ false);

  // AOUSD Core Spec 12.2.1 (specifier): Composed specifier resolution.
  // If dst (stronger) is `over` but src (weaker) is defining (def/class),
  // the composed specifier becomes `def` -- the prim IS defined because
  // at least one opinion provides a definition.
  if (dst.specifier() == Specifier::Over &&
      (src.specifier() == Specifier::Def ||
       src.specifier() == Specifier::Class)) {
    dst.specifier() = src.specifier();
  }

  // AOUSD Core Spec 12.2.2 (typeName): Use typeName from defining spec.
  // If dst has no typeName and src is defining (def/class), take src's.
  if (dst.typeName().empty() && !src.typeName().empty()) {
    if (src.specifier() == Specifier::Def ||
        src.specifier() == Specifier::Class) {
      dst.typeName() = src.typeName();
    }
  }

  // Combine properties
  for (const auto &prop : src.props()) {
    if (dst.props().count(prop.first) == 0) {
      // add if not existent
      dst.props()[prop.first] = prop.second;
    } else {
      // AOUSD Core Spec 12.2.4 (custom): true if ANY opinion says true
      if (prop.second.has_custom() && !dst.props().at(prop.first).has_custom()) {
        dst.props()[prop.first].set_custom(true);
      }

      // AOUSD Core Spec 12.4 (relationships): Compose relationship targets
      // using list-op semantics across opinions.
      if (dst.props().at(prop.first).is_relationship() &&
          prop.second.is_relationship()) {
        Relationship &dst_rel = dst.props()[prop.first].relationship();
        const Relationship &src_rel = prop.second.get_relationship();

        // If weaker has targets and stronger doesn't block them,
        // merge using list-edit semantics
        if (!dst_rel.is_blocked() && src_rel.is_pathvector() &&
            dst_rel.is_pathvector()) {
          ListEditQual src_qual = src_rel.get_listedit_qual();

          if (src_qual == ListEditQual::Prepend) {
            // Prepend weaker targets before stronger
            auto combined = src_rel.targetPathVector;
            for (const auto &p : dst_rel.targetPathVector) {
              bool dup = false;
              for (const auto &c : combined) { if (c == p) { dup = true; break; } }
              if (!dup) combined.push_back(p);
            }
            dst_rel.targetPathVector = combined;
          } else if (src_qual == ListEditQual::Append) {
            // Append weaker targets after stronger
            auto combined = dst_rel.targetPathVector;
            for (const auto &p : src_rel.targetPathVector) {
              bool dup = false;
              for (const auto &c : combined) { if (c == p) { dup = true; break; } }
              if (!dup) combined.push_back(p);
            }
            dst_rel.targetPathVector = combined;
          } else if (src_qual == ListEditQual::Delete) {
            // Delete weaker targets from stronger
            std::vector<Path> filtered;
            for (const auto &p : dst_rel.targetPathVector) {
              bool del = false;
              for (const auto &d : src_rel.targetPathVector) {
                if (d == p) { del = true; break; }
              }
              if (!del) filtered.push_back(p);
            }
            dst_rel.targetPathVector = filtered;
          }
          // ResetToExplicit: stronger already wins (default behavior)
        }
      }

      // AOUSD Core Spec 6.5 (type agreement): Warn if composed property types
      // disagree. Role types (color3f) agree with their underlying type (float3)
      // but are not equivalent; other mismatches are errors.
      if (dst.props().at(prop.first).is_attribute() &&
          prop.second.is_attribute()) {
        const std::string &dst_type = dst.props().at(prop.first).get_attribute().type_name();
        const std::string &src_type = prop.second.get_attribute().type_name();
        if (!dst_type.empty() && !src_type.empty() && dst_type != src_type) {
          // Check if types agree via role-type relationship
          // (e.g., color3f agrees with float3, point3f with float3, etc.)
          uint32_t dst_underlying = value::GetUnderlyingTypeId(dst_type);
          uint32_t src_underlying = value::GetUnderlyingTypeId(src_type);
          if (dst_underlying != src_underlying) {
            PUSH_WARN(fmt::format(
                "Type mismatch for property `{}`: stronger has `{}`, "
                "weaker has `{}`. Composed value may be incorrect.",
                prop.first, dst_type, src_type));
          }
        }
      }

      // AOUSD Core Spec 12.2.3 (variability): If the stronger opinion did not
      // explicitly author variability, use the weaker opinion's variability.
      // Also consult the schema registry as the weakest fallback.
      if (dst.props().at(prop.first).is_attribute() &&
          prop.second.is_attribute()) {
        Attribute &dst_attr = dst.props()[prop.first].attribute();
        const Attribute &src_attr = prop.second.get_attribute();

        // If dst (stronger) has default variability (Varying) and src (weaker)
        // has explicit uniform, check schema to determine correct variability.
        if (dst_attr.variability() == Variability::Varying &&
            src_attr.variability() == Variability::Uniform) {
          // Weaker opinion has uniform -- use it (spec says weaker fills)
          dst_attr.variability() = Variability::Uniform;
        } else if (dst_attr.variability() == Variability::Varying &&
                   src_attr.variability() == Variability::Varying) {
          // Neither explicitly set uniform; consult schema registry
          const auto *schema_info = SchemaRegistry::instance().find(
              dst.typeName(), prop.first);
          if (schema_info &&
              schema_info->variability == Variability::Uniform) {
            dst_attr.variability() = Variability::Uniform;
          }
        }
      }
    }
  }

  // Combine child primspecs.
  for (auto &child : src.children()) {
    auto dst_it = std::find_if(
        dst.children().begin(), dst.children().end(),
        [&child](const PrimSpec &ps) { return ps.name() == child.name(); });

    // if exists, combine properties and children
    if (dst_it != dst.children().end()) {
      if (!CombinePrimSpecRec(depth + 1, (*dst_it), child, warn, err)) {
        return false;
      }
    }
    // otherwise add it
    else {
      dst.children().push_back(child);
    }
  }

  return true;
}


// AOUSD Core Spec 10.3.1 / 10.3.2.2: Apply LayerOffset to all timeSamples
// in a PrimSpec tree. The formula per spec is: t_stage = t_layer * scale + offset.
// So when importing layer times, we transform: t_new = t_old * scale + offset.
void ApplyLayerOffsetRec(PrimSpec &ps, const LayerOffset &offset) {
  if (offset._offset == 0.0 && offset._scale == 1.0) {
    return;  // Identity — nothing to do
  }

  for (auto &prop_item : ps.props()) {
    if (prop_item.second.is_attribute()) {
      Attribute &attr = prop_item.second.attribute();
      if (attr.has_timesamples()) {
        auto &samples = attr.get_var().ts_raw().samples();
        for (auto &sample : samples) {
          sample.t = sample.t * offset._scale + offset._offset;
        }
      }
    }
  }

  for (auto &child : ps.children()) {
    ApplyLayerOffsetRec(child, offset);
  }
}

// AOUSD Core Spec 10.3.2.3: Tag PrimSpecs with source layer ID for implied arc tracking.
void TagPrimSpecArcOriginRec(PrimSpec &ps, const std::string &layer_id) {
  ArcOrigin origin;
  origin.source_layer_id = layer_id;
  origin.source_prim_path = Path("/" + ps.name(), "");
  ps.metas().arc_origins.push_back(origin);

  for (auto &child : ps.children()) {
    TagPrimSpecArcOriginRec(child, layer_id);
  }
}

bool CompositeSublayersRec(AssetResolutionResolver &resolver,
                           const Layer &in_layer,
                           std::vector<std::set<std::string>> layer_names_stack,
                           Layer *composited_layer, std::string *warn,
                           std::string *err,
                           const SublayersCompositionOptions &options) {
  if (layer_names_stack.size() > options.max_depth) {
    if (err) {
      (*err) += "subLayer is nested too deeply.";
    }
    return false;
  }

  layer_names_stack.emplace_back(std::set<std::string>());
  std::set<std::string> &curr_layer_names = layer_names_stack.back();

  for (auto const &prim : in_layer.primspecs()) {
    if (composited_layer->has_primspec(prim.first))
    {
      if (!CombinePrimSpecRec(0, composited_layer->primspecs().at(prim.first), prim.second, warn, err)) {
        return false;
      }
    }
    else {
      composited_layer->add_primspec(prim.first, prim.second);
    }
  }

  // AOUSD Core Spec 9.5: Build expression variables map for asset path substitution
  std::map<std::string, std::string> expr_vars;
  if (in_layer.metas().expressionVariables) {
    for (const auto &var : in_layer.metas().expressionVariables.value()) {
      if (auto sv = var.second.get_value<std::string>()) {
        expr_vars[var.first] = sv.value();
      }
    }
  }

  for (const auto &layer : in_layer.metas().subLayers) {
    std::string sublayer_asset_path = layer.assetPath.GetAssetPath();

    // AOUSD Core Spec 9.5: Substitute expression variables in asset paths
    if (!expr_vars.empty()) {
      sublayer_asset_path = SubstituteExpressionVariables(sublayer_asset_path, expr_vars);
    }

    // Do cyclic referencing check.
    // TODO: Use resolved name?
    if (IsVisited(layer_names_stack, sublayer_asset_path)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Circular referenceing detected for subLayer: {} in {}",
                      sublayer_asset_path, in_layer.name()));
    }

    std::string layer_filepath = resolver.resolve(sublayer_asset_path);
    if (layer_filepath.empty()) {
      PUSH_ERROR_AND_RETURN(fmt::format("{} not found in path: {}",
                                        sublayer_asset_path,
                                        resolver.search_paths_str()));
    }

    tinyusdz::Layer sublayer;
    if (!LoadAsset(resolver, in_layer.get_current_working_path(),
                   in_layer.get_asset_search_paths(), options.fileformats,
                   layer.assetPath, /* not_used */ Path::make_root_path(),
                   &sublayer, /* primspec_root */ nullptr,
                   options.error_when_no_prims_in_sublayer,
                   options.error_when_asset_not_found,
                   options.error_when_unsupported_fileformat, warn, err)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Load asset in subLayer failed: `{}`", layer.assetPath));
    }

    // AOUSD Core Spec 10.3.1: Apply sublayer offset/scale to timeSamples.
    // t_stage = t_layer * scale + offset
    if (layer.layerOffset._offset != 0.0 || layer.layerOffset._scale != 1.0) {
      for (auto &ps_item : sublayer.primspecs()) {
        ApplyLayerOffsetRec(ps_item.second, layer.layerOffset);
      }
    }

    // AOUSD Core Spec 10.3.2.3: Tag sublayer PrimSpecs with source layer ID
    // for implied inherit/specialize arc propagation.
    for (auto &ps_item : sublayer.primspecs()) {
      TagPrimSpecArcOriginRec(ps_item.second, sublayer_asset_path);
    }

    curr_layer_names.insert(sublayer_asset_path);

    // Recursively load subLayer
    if (!CompositeSublayersRec(resolver, sublayer, layer_names_stack,
                               composited_layer, warn, err, options)) {
      return false;
    }
  }

  layer_names_stack.pop_back();

  return true;
}

}  // namespace

std::vector<std::string> ExtractSublayerAssetPaths(const Layer &layer) {

  std::vector<std::string> paths;

  for (const auto &sublayer : layer.metas().subLayers) {
    std::string sublayer_asset_path = sublayer.assetPath.GetAssetPath();

    paths.push_back(sublayer_asset_path);
  }

  return paths;

}



bool CompositeSublayers(AssetResolutionResolver &resolver,
                        const Layer &in_layer, Layer *composited_layer,
                        std::string *warn, std::string *err,
                        SublayersCompositionOptions options) {
  if (!composited_layer) {
    return false;
  }

  std::vector<std::set<std::string>> layer_names_stack;

  // keep metas from the root layer
  composited_layer->metas() = in_layer.metas();

  DCOUT("Resolve subLayers..");
  if (!CompositeSublayersRec(resolver, in_layer, layer_names_stack,
                             composited_layer, warn, err, options)) {
    PUSH_ERROR_AND_RETURN("Composite subLayers failed.");
  }

  composited_layer->metas().subLayers.clear();

  DCOUT("Composite subLayers ok.");
  return true;
}

namespace {


// Visited set for cycle detection in references/payloads.
// Tracks (asset_path, prim_path) pairs. Grows only with arc nesting depth (typically < 10).
using ArcVisitedSet = std::set<std::pair<std::string, std::string>>;

// AOUSD Core Spec 10.3.2.3 / 10.3.2.4: Propagate implied inherit/specialize
// paths from a referenced/payload PrimSpec to the referencing prim.
// When prim P references prim Q, and Q has `inherits = [/C]`, then P should
// also implicitly inherit from /C if it exists in P's layer stack.
static void PropagateImpliedArcPaths(const PrimSpec &src_ps,
                                     PrimSpec &dst_ps) {
  // Propagate inherits paths
  if (src_ps.metas().inherits) {
    for (const auto &op : src_ps.metas().inherits.value()) {
      if (op.second.empty()) continue;
      if (!dst_ps.metas().inheritPaths) {
        dst_ps.metas().inheritPaths.emplace();
      }
      dst_ps.metas().inheritPaths->push_back(op);
    }
  }

  // Propagate specializes paths
  if (src_ps.metas().specializes) {
    for (const auto &op : src_ps.metas().specializes.value()) {
      if (op.second.empty()) continue;
      if (!dst_ps.metas().specializePaths) {
        dst_ps.metas().specializePaths.emplace();
      }
      dst_ps.metas().specializePaths->push_back(op);
    }
  }
}

bool CompositeReferencesRec(uint32_t depth, AssetResolutionResolver &resolver,
                            const std::vector<std::string> &asset_search_paths,
                            const Path &dst_prim_path,
                            const Layer &in_layer,
                            PrimSpec &primspec /* [inout] */, std::string *warn,
                            std::string *err,
                            const ReferencesCompositionOptions &options,
                            ArcVisitedSet &visited) {
  if (depth > options.max_depth) {
    PUSH_ERROR_AND_RETURN("Too deep.");
  }

  // Traverse children first.
  for (auto &child : primspec.children()) {
    const Path parent_prim_path = dst_prim_path.AppendPrim(child.name());
    if (!CompositeReferencesRec(depth + 1, resolver, asset_search_paths, parent_prim_path, in_layer, child,
                                warn, err, options, visited)) {
      return false;
    }
  }

  // Use PrimSpec's AssetResolution state.
  std::string cwp = primspec.get_current_working_path();
  std::vector<std::string> search_paths = primspec.get_asset_search_paths();

  if (primspec.metas().references) {
    // Process all listops in order (supports multiple listops per arc)
    // Pre-pass: collect deleted reference targets so we can skip them.
    std::set<std::pair<std::string, std::string>> ref_deleted;
    for (const auto &ref_op : primspec.metas().references.value()) {
      if (ref_op.first == ListEditQual::Delete) {
        for (const auto &r : ref_op.second) {
          ref_deleted.insert({r.asset_path.GetAssetPath(), r.prim_path.prim_part()});
        }
      }
    }

    for (const auto &ref_op : primspec.metas().references.value()) {
      const ListEditQual &qual = ref_op.first;
      const auto &refecences = ref_op.second;

      if ((qual == ListEditQual::ResetToExplicit) ||
          (qual == ListEditQual::Prepend)) {
        for (const auto &reference : refecences) {
          // Skip if this reference was deleted
          if (ref_deleted.count({reference.asset_path.GetAssetPath(),
                                 reference.prim_path.prim_part()})) {
            continue;
          }
          Layer layer;
          const PrimSpec *src_ps{nullptr};

          if (reference.asset_path.GetAssetPath().empty()) {
            if (reference.prim_path.is_absolute_path()) {
              // Inherit-like operation.

              if (!in_layer.find_primspec_at(reference.prim_path, &src_ps, err)) {
                return false;
              }

            } else {
              PUSH_ERROR_AND_RETURN(
                  fmt::format("Invalid asset path. assetPath is empty and "
                              "primPath is not absolute path: {}",
                              reference.prim_path.full_path_name()));
            }
          } else {

            DCOUT("reference.prim_path = " << reference.prim_path);
            DCOUT("primspec.cwp = " << cwp);
            DCOUT("primspec.search_paths = " << search_paths);

            // Cycle detection: check if this (asset, prim) pair has already been visited.
            auto visit_key = std::make_pair(
                reference.asset_path.GetAssetPath(),
                reference.prim_path.prim_part());
            if (visited.count(visit_key)) {
              PUSH_ERROR_AND_RETURN(fmt::format(
                  "Cycle detected in `references`: asset `{}` prim <{}>",
                  visit_key.first, visit_key.second));
            }
            visited.insert(visit_key);

            if (!LoadAsset(resolver, cwp, search_paths, options.fileformats,
                           reference.asset_path, reference.prim_path, &layer,
                           &src_ps, /* error_when_no_prims_found */ true,
                           options.error_when_asset_not_found,
                           options.error_when_unsupported_fileformat, warn, err)) {
              visited.erase(visit_key);
              PUSH_ERROR_AND_RETURN(
                  fmt::format("Failed to `references` asset `{}`",
                              reference.asset_path.GetAssetPath()));
            }
            visited.erase(visit_key);
          }

          if (!src_ps) {
            // LoadAsset allowed not-found or unsupported file. so do nothing.
            continue;
          }

          // AOUSD Core Spec 10.3.2.3/10.3.2.4: Propagate implied inherit/specialize
          // paths from the referenced prim before flattening consumes them.
          PropagateImpliedArcPaths(*src_ps, primspec);

          // Replace prim path prefix
          if (!ReplaceRootPrimPathRec(reference.prim_path, dst_prim_path, *const_cast<PrimSpec *>(src_ps), warn, err)) {
            return false;
          }

          // AOUSD Core Spec 10.3.2.2: Apply reference layerOffset to timeSamples.
          if (reference.layerOffset._offset != 0.0 ||
              reference.layerOffset._scale != 1.0) {
            ApplyLayerOffsetRec(*const_cast<PrimSpec *>(src_ps), reference.layerOffset);
          }

          // AOUSD Core Spec 10.3.2.3: Record arc origin for implied inherit propagation
          if (!reference.asset_path.GetAssetPath().empty()) {
            ArcOrigin origin;
            origin.source_layer_id = reference.asset_path.GetAssetPath();
            origin.source_prim_path = reference.prim_path;
            primspec.metas().arc_origins.push_back(origin);
          }

          // `inherits` op
          if (!InheritPrimSpec(primspec, *src_ps, warn, err)) {
            PUSH_ERROR_AND_RETURN(fmt::format("Failed to reference layer `{}`",
                                              reference.asset_path));
          }

          // Modify Prim type if this PrimSpec is Model type.
          if (primspec.typeName().empty() || primspec.typeName() == "Model") {
            if (src_ps->typeName().empty() || src_ps->typeName() == "Model") {
              // pass
            } else {
              primspec.typeName() = src_ps->typeName();
            }
          }

          DCOUT("inherit done: primspec = " << primspec.name());
        }

      } else if (qual == ListEditQual::Delete) {
        // Handled in pre-pass above — deleted refs are filtered from prepend/append.
      } else if (qual == ListEditQual::Order) {
        PushWarn("`order` references list edit: reordering not supported. Skipping.\n");
      } else if (qual == ListEditQual::Invalid) {
        PUSH_ERROR_AND_RETURN("Invalid listedit qualifier for `references`.");
      } else if (qual == ListEditQual::Add ||
                 qual == ListEditQual::Append) {
        // AOUSD Core Spec 6.6.3.10: `add` is deprecated, treat as `append`
        if (qual == ListEditQual::Add) {
          PUSH_WARN("`add` list edit qualifier is deprecated (AOUSD Core Spec 6.6.3.10). Treating as `append`.");
        }
        for (const auto &reference : refecences) {
          // Skip if this reference was deleted
          if (ref_deleted.count({reference.asset_path.GetAssetPath(),
                                 reference.prim_path.prim_part()})) {
            continue;
          }
          Layer layer;
          const PrimSpec *src_ps{nullptr};

          if (reference.asset_path.GetAssetPath().empty()) {
            if (reference.prim_path.is_absolute_path()) {
              // Inherit-like operation.

              if (!in_layer.find_primspec_at(reference.prim_path, &src_ps, err)) {
                return false;
              }

            } else {
              PUSH_ERROR_AND_RETURN(
                  fmt::format("Invalid asset path. assetPath is empty and "
                              "primPath is not absolute path: {}",
                              reference.prim_path.full_path_name()));
            }
          } else {
            // Cycle detection for append references
            auto visit_key = std::make_pair(
                reference.asset_path.GetAssetPath(),
                reference.prim_path.prim_part());
            if (visited.count(visit_key)) {
              PUSH_ERROR_AND_RETURN(fmt::format(
                  "Cycle detected in `references`: asset `{}` prim <{}>",
                  visit_key.first, visit_key.second));
            }
            visited.insert(visit_key);

            if (!LoadAsset(resolver, cwp, search_paths, options.fileformats,
                           reference.asset_path, reference.prim_path, &layer,
                           &src_ps, /* error_when_no_prims */ true,
                           options.error_when_asset_not_found,
                           options.error_when_unsupported_fileformat, warn, err)) {
              visited.erase(visit_key);
              PUSH_ERROR_AND_RETURN(
                  fmt::format("Failed to `references` asset `{}`",
                              reference.asset_path.GetAssetPath()));
            }
            visited.erase(visit_key);
          }

          if (!src_ps) {
            // LoadAsset allowed not-found or unsupported file. so do nothing.
            continue;
          }

          // AOUSD Core Spec 10.3.2.3/10.3.2.4: Propagate implied arc paths.
          PropagateImpliedArcPaths(*src_ps, primspec);

          // AOUSD Core Spec 10.3.2.3: Record arc origin for implied inherit propagation
          if (!reference.asset_path.GetAssetPath().empty()) {
            ArcOrigin origin;
            origin.source_layer_id = reference.asset_path.GetAssetPath();
            origin.source_prim_path = reference.prim_path;
            primspec.metas().arc_origins.push_back(origin);
          }

          // AOUSD Core Spec 10.3.2.2: Apply reference layerOffset to timeSamples.
          if (reference.layerOffset._offset != 0.0 ||
              reference.layerOffset._scale != 1.0) {
            ApplyLayerOffsetRec(*const_cast<PrimSpec *>(src_ps), reference.layerOffset);
          }

          // Replace prim path prefix
          if (!ReplaceRootPrimPathRec(reference.prim_path, dst_prim_path, *const_cast<PrimSpec *>(src_ps), warn, err)) {
            return false;
          }

          // `over` op
          if (!OverridePrimSpec(primspec, *src_ps, warn, err)) {
            PUSH_ERROR_AND_RETURN(fmt::format("Failed to reference layer `{}`",
                                              reference.asset_path));
          }

          // Modify Prim type if this PrimSpec is Model type.
          if (primspec.typeName().empty() || primspec.typeName() == "Model") {
            if (src_ps->typeName().empty() || src_ps->typeName() == "Model") {
              // pass
            } else {
              primspec.typeName() = src_ps->typeName();
            }
          }
        }
      }
    }
  }

  // Remove `references`.
  primspec.metas().references.reset();

  return true;
}

bool CompositePayloadRec(uint32_t depth, AssetResolutionResolver &resolver,
                         const std::vector<std::string> &asset_search_paths,
                         const Path &dst_prim_path,
                         const Layer &in_layer,
                         PrimSpec &primspec /* [inout] */, std::string *warn,
                         std::string *err,
                         const PayloadCompositionOptions &options,
                         ArcVisitedSet &visited) {
  if (depth > options.max_depth) {
    PUSH_ERROR_AND_RETURN("Too deep.");
  }

  // Traverse children first.
  for (auto &child : primspec.children()) {
    const Path parent_prim_path = dst_prim_path.AppendPrim(child.name());
    if (!CompositePayloadRec(depth + 1, resolver, asset_search_paths, parent_prim_path, in_layer, child,
                             warn, err, options, visited)) {
      return false;
    }
  }

  // Use PrimSpec's AssetResolution state.
  std::string cwp = primspec.get_current_working_path();
  std::vector<std::string> search_paths = primspec.get_asset_search_paths();

  if (primspec.metas().payload) {
    // Pre-pass: collect deleted payload targets so we can skip them.
    std::set<std::pair<std::string, std::string>> pl_deleted;
    for (const auto &payload_op : primspec.metas().payload.value()) {
      if (payload_op.first == ListEditQual::Delete) {
        for (const auto &p : payload_op.second) {
          pl_deleted.insert({p.asset_path.GetAssetPath(), p.prim_path.prim_part()});
        }
      }
    }

    for (const auto &payload_op : primspec.metas().payload.value()) {
      const ListEditQual &qual = payload_op.first;
      const auto &payloads = payload_op.second;

      if ((qual == ListEditQual::ResetToExplicit) ||
          (qual == ListEditQual::Prepend)) {
        for (const auto &pl : payloads) {
          // Skip if this payload was deleted
          if (pl_deleted.count({pl.asset_path.GetAssetPath(),
                                pl.prim_path.prim_part()})) {
            continue;
          }
          // Lazy payload: check load_policy callback
          if (options.load_policy &&
              !options.load_policy(dst_prim_path, pl)) {
            DCOUT("Payload skipped by load_policy: " << pl.asset_path.GetAssetPath());
            continue;
          }

          std::string asset_path = pl.asset_path.GetAssetPath();
          DCOUT("asset_path = " << asset_path);

          Layer layer;
          const PrimSpec *src_ps{nullptr};

          if (pl.asset_path.GetAssetPath().empty()) {
            if (pl.prim_path.is_absolute_path()) {
              // Inherit-like operation.

              if (!in_layer.find_primspec_at(pl.prim_path, &src_ps, err)) {
                return false;
              }

            } else {
              PUSH_ERROR_AND_RETURN(
                  fmt::format("primPath is not absolute path: {}",
                              pl.prim_path.full_path_name()));
            }
          } else {

            // Cycle detection for payload
            auto visit_key = std::make_pair(
                pl.asset_path.GetAssetPath(),
                pl.prim_path.prim_part());
            if (visited.count(visit_key)) {
              PUSH_ERROR_AND_RETURN(fmt::format(
                  "Cycle detected in `payload`: asset `{}` prim <{}>",
                  visit_key.first, visit_key.second));
            }
            visited.insert(visit_key);

            if (!LoadAsset(resolver, cwp, search_paths, options.fileformats,
                           pl.asset_path, pl.prim_path, &layer, &src_ps,
                           /* error_when_no_prims_found */ true,
                           options.error_when_asset_not_found,
                           options.error_when_unsupported_fileformat, warn, err)) {
              visited.erase(visit_key);
              PUSH_ERROR_AND_RETURN(fmt::format("Failed to `payload` asset `{}`",
                                                pl.asset_path.GetAssetPath()));
            }
            visited.erase(visit_key);
          }

          if (!src_ps) {
            // LoadAsset allowed not-found or unsupported file. so do nothing.
            continue;
          }

          // AOUSD Core Spec 10.3.2.3/10.3.2.4: Propagate implied arc paths.
          PropagateImpliedArcPaths(*src_ps, primspec);

          // AOUSD Core Spec 10.3.2.2: Apply payload layerOffset to timeSamples.
          if (pl.layerOffset._offset != 0.0 || pl.layerOffset._scale != 1.0) {
            ApplyLayerOffsetRec(*const_cast<PrimSpec *>(src_ps), pl.layerOffset);
          }

          // Replace prim path prefix
          if (!ReplaceRootPrimPathRec(pl.prim_path, dst_prim_path, *const_cast<PrimSpec *>(src_ps), warn, err)) {
            return false;
          }

          // `inherits` op
          if (!InheritPrimSpec(primspec, *src_ps, warn, err)) {
            PUSH_ERROR_AND_RETURN(
                fmt::format("Failed to payload layer `{}`", asset_path));
          }

          // Modify Prim type if this PrimSpec is Model type.
          if (primspec.typeName().empty() || primspec.typeName() == "Model") {
            if (src_ps->typeName().empty() || src_ps->typeName() == "Model") {
              // pass
            } else {
              primspec.typeName() = src_ps->typeName();
            }
          }

          DCOUT("inherit done: primspec = " << primspec.name());
        }

      } else if (qual == ListEditQual::Delete) {
        // Handled in pre-pass above — deleted payloads are filtered from prepend/append.
      } else if (qual == ListEditQual::Order) {
        PushWarn("`order` payloads list edit: reordering not supported. Skipping.\n");
      } else if (qual == ListEditQual::Invalid) {
        PUSH_ERROR_AND_RETURN("Invalid listedit qualifier for `payload`.");
      } else if (qual == ListEditQual::Add ||
                 qual == ListEditQual::Append) {
        // AOUSD Core Spec 6.6.3.10: `add` is deprecated, treat as `append`
        if (qual == ListEditQual::Add) {
          PUSH_WARN("`add` list edit qualifier is deprecated (AOUSD Core Spec 6.6.3.10). Treating as `append`.");
        }
        for (const auto &pl : payloads) {
          // Skip if this payload was deleted
          if (pl_deleted.count({pl.asset_path.GetAssetPath(),
                                pl.prim_path.prim_part()})) {
            continue;
          }
          // Lazy payload: check load_policy callback
          if (options.load_policy &&
              !options.load_policy(dst_prim_path, pl)) {
            DCOUT("Payload skipped by load_policy: " << pl.asset_path.GetAssetPath());
            continue;
          }

          std::string asset_path = pl.asset_path.GetAssetPath();

          Layer layer;
          const PrimSpec *src_ps{nullptr};

          if (pl.asset_path.GetAssetPath().empty()) {
            if (pl.prim_path.is_absolute_path()) {
              // Inherit-like operation.

              if (!in_layer.find_primspec_at(pl.prim_path, &src_ps, err)) {
                return false;
              }

            } else {
              PUSH_ERROR_AND_RETURN(
                  fmt::format("primPath is not absolute path: {}",
                              pl.prim_path.full_path_name()));
            }
          } else {

            // Cycle detection for append payload
            auto visit_key = std::make_pair(
                pl.asset_path.GetAssetPath(),
                pl.prim_path.prim_part());
            if (visited.count(visit_key)) {
              PUSH_ERROR_AND_RETURN(fmt::format(
                  "Cycle detected in `payload`: asset `{}` prim <{}>",
                  visit_key.first, visit_key.second));
            }
            visited.insert(visit_key);

            if (!LoadAsset(resolver, cwp, search_paths, options.fileformats,
                           pl.asset_path, pl.prim_path, &layer, &src_ps,
                           /* error_when_no_prims_found */ true,
                           options.error_when_asset_not_found,
                           options.error_when_unsupported_fileformat, warn, err)) {
              visited.erase(visit_key);
              PUSH_ERROR_AND_RETURN(fmt::format("Failed to `payload` asset `{}`",
                                                pl.asset_path.GetAssetPath()));
            }
            visited.erase(visit_key);
          }

          if (!src_ps) {
            // LoadAsset allowed not-found or unsupported file. so do nothing.
            continue;
          }

          // AOUSD Core Spec 10.3.2.3/10.3.2.4: Propagate implied arc paths.
          PropagateImpliedArcPaths(*src_ps, primspec);

          // AOUSD Core Spec 10.3.2.2: Apply payload layerOffset to timeSamples.
          if (pl.layerOffset._offset != 0.0 || pl.layerOffset._scale != 1.0) {
            ApplyLayerOffsetRec(*const_cast<PrimSpec *>(src_ps), pl.layerOffset);
          }

          // Replace prim path prefix
          if (!ReplaceRootPrimPathRec(pl.prim_path, dst_prim_path, *const_cast<PrimSpec *>(src_ps), warn, err)) {
            return false;
          }

          // `over` op
          if (!OverridePrimSpec(primspec, *src_ps, warn, err)) {
            PUSH_ERROR_AND_RETURN(
                fmt::format("Failed to payload layer `{}`", asset_path));
          }

          // Modify Prim type if this PrimSpec is Model type.
          if (primspec.typeName().empty() || primspec.typeName() == "Model") {
            if (src_ps->typeName().empty() || src_ps->typeName() == "Model") {
              // pass
            } else {
              primspec.typeName() = src_ps->typeName();
            }
          }
        }
      }
    }
  }

  // Remove `payload`.
  primspec.metas().payload.reset();

  return true;
}

bool CompositeVariantRec(uint32_t depth, PrimSpec &primspec /* [inout] */,
                         std::string *warn, std::string *err) {
  if (depth > (1024 * 1024)) {
    PUSH_ERROR_AND_RETURN("Too deep.");
  }

  // Traverse children first.
  for (auto &child : primspec.children()) {
    if (!CompositeVariantRec(depth + 1, child, warn, err)) {
      return false;
    }
  }

  PrimSpec dst;
  std::map<std::string, std::string>
      variant_selection;  // empty = use variant settings in PrimSpec.

  if (!VariantSelectPrimSpec(dst, primspec, variant_selection, warn, err)) {
    return false;
  }

  primspec = std::move(dst);

  return true;
}

// Visited set for cycle detection in inherits/specializes.
// Tracks prim paths within the same layer.
using PathVisitedSet = std::set<std::string>;

// Resolve ListEditQual operations into a final ordered list.
// Implements: resetToExplicit clears + adds, prepend prepends, append appends,
// delete removes matching items, order is ignored (warn).
// Template version uses EqPred for matching delete targets.
template <typename T, typename EqPred>
static std::vector<T> ResolveListOpsT(
    const std::vector<std::pair<ListEditQual, std::vector<T>>> &listops,
    EqPred eq, std::string *warn) {
  std::vector<T> result;

  for (const auto &op : listops) {
    const auto &qual = op.first;
    const auto &items = op.second;

    if (qual == ListEditQual::ResetToExplicit) {
      result.clear();
      result.insert(result.end(), items.begin(), items.end());
    } else if (qual == ListEditQual::Prepend) {
      result.insert(result.begin(), items.begin(), items.end());
    } else if (qual == ListEditQual::Append ||
               qual == ListEditQual::Add) {
      if (qual == ListEditQual::Add && warn) {
        (*warn) += "`add` list edit qualifier is deprecated (AOUSD Core Spec 6.6.3.10). Treating as `append`.\n";
      }
      result.insert(result.end(), items.begin(), items.end());
    } else if (qual == ListEditQual::Delete) {
      for (const auto &del_item : items) {
        result.erase(
            std::remove_if(result.begin(), result.end(),
                           [&del_item, &eq](const T &x) {
                             return eq(x, del_item);
                           }),
            result.end());
      }
    } else if (qual == ListEditQual::Order) {
      // Reorder items per the order vector.
      // Items in the order vector are placed in that relative order.
      // Items NOT in the order vector are prepended in their original order.
      // This follows the deprecated SdfListOp reorder semantics.
      if (items.empty() || result.empty()) continue;

      // Build a position map: for each item in `items`, what rank?
      // Items in `items` get moved to that position, rest stay at front.
      std::vector<T> ordered;
      std::vector<T> unordered;

      // Collect items that appear in the order vector (in order)
      for (const auto &anchor : items) {
        for (const auto &r : result) {
          if (eq(r, anchor)) {
            ordered.push_back(r);
            break;
          }
        }
      }

      // Collect items NOT in the order vector (preserve original order)
      for (const auto &r : result) {
        bool found = false;
        for (const auto &anchor : items) {
          if (eq(r, anchor)) { found = true; break; }
        }
        if (!found) {
          unordered.push_back(r);
        }
      }

      // Reassemble: unordered first, then ordered
      result.clear();
      result.insert(result.end(), unordered.begin(), unordered.end());
      result.insert(result.end(), ordered.begin(), ordered.end());
    }
  }

  return result;
}

// Path version: match by prim_part()
static std::vector<Path> ResolveListOps(
    const std::vector<std::pair<ListEditQual, std::vector<Path>>> &listops,
    std::string *warn) {
  return ResolveListOpsT<Path>(listops,
      [](const Path &a, const Path &b) {
        return a.prim_part() == b.prim_part();
      }, warn);
}

// Note: Reference and Payload use a pre-pass approach (collect deleted items
// into a set, then skip them during processing) rather than ResolveListOpsT,
// because they need to preserve the prepend/append distinction for different
// merge semantics (InheritPrimSpec vs OverridePrimSpec).

bool CompositeInheritsRec(uint32_t depth, const Layer &layer,
                          PrimSpec &primspec /* [inout] */, std::string *warn,
                          std::string *err,
                          PathVisitedSet &visited) {
  if (depth > (1024 * 1024)) {
    PUSH_ERROR_AND_RETURN("Too deep.");
  }

  // Traverse children first.
  for (auto &child : primspec.children()) {
    if (!CompositeInheritsRec(depth + 1, layer, child, warn, err, visited)) {
      return false;
    }
  }

  if (primspec.metas().inherits) {
    // Resolve all ListEditQual operations (prepend, append, delete, order)
    // into a single ordered list of paths before processing.
    auto inherits_copy = primspec.metas().inherits.value();
    std::vector<Path> resolved = ResolveListOps(inherits_copy, warn);

    // Process the resolved path list — each inherits target fills in defaults.
    for (const auto &inheritPath : resolved) {
      // Cycle detection
      std::string key = inheritPath.prim_part();
      if (visited.count(key)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Cycle detected in `inherits`: prim <{}>", key));
      }
      visited.insert(key);

      const PrimSpec *src_ps{nullptr};

      if (!layer.find_primspec_at(inheritPath, &src_ps, err)) {
        visited.erase(key);
        if (err) {
          (*err) += "Inherit failed: Path <" +
                    inheritPath.prim_part() + "> not found or is invalid.\n";
        }
        return false;
      }

      if (!src_ps) {
        visited.erase(key);
        PUSH_ERROR_AND_RETURN(
            "Internal error: PrimSpec is nullptr in CompositeInheritsRec.\n");
      }

      if (!InheritPrimSpec(primspec, *src_ps, warn, err)) {
        visited.erase(key);
        return false;
      }
      visited.erase(key);
    }

    // remove `inherits` metadataum after processing.
    primspec.metas().inherits.reset();
  }

  // AOUSD Core Spec 10.3.2.3: Implied inherits.
  // If this prim has arc_origins (e.g., from references), and the referenced
  // layer's prim had inherits, those inherits should be "implied" here.
  // For single-level case: walk arc_origins and check if the source prim had
  // inherits that were already applied in the referenced layer. The inherit
  // targets in the referenced layer should also be applied as implied inherits
  // in this (upstream) layer stack, if the target classes exist here too.
  //
  // Implementation: For each arc origin, look for class prims (class specifier)
  // in the current layer that match the inherit targets from the referenced layer.
  // This is the single-level implied inherit case.
  // AOUSD Core Spec 10.3.2.3: Implied inherits (multi-level).
  // Process inheritPaths propagated from referenced/payload layers.
  // After applying an implied inherit, check if the inherited prim itself
  // has further inheritPaths that should cascade (multi-level propagation).
  {
    std::set<std::string> visited_inherits;
    constexpr size_t kMaxImpliedDepth = 32;
    size_t implied_depth = 0;

    auto process_inheritPaths = [&](auto &&self) -> bool {
      if (!primspec.metas().inheritPaths || implied_depth >= kMaxImpliedDepth) {
        return true;
      }
      implied_depth++;

      auto ip_copy = primspec.metas().inheritPaths.value();
      primspec.metas().inheritPaths.reset();

      for (const auto &inherit_op : ip_copy) {
        const auto &ip_paths = inherit_op.second;
        for (const auto &inheritPath : ip_paths) {
          std::string key = inheritPath.prim_part();
          if (visited_inherits.count(key)) continue;  // prevent cycles

          const PrimSpec *src_ps{nullptr};
          std::string _err;
          if (layer.find_primspec_at(inheritPath, &src_ps, &_err) && src_ps) {
            visited_inherits.insert(key);
            DCOUT("Applying implied inherit (level " << implied_depth
                  << ") from " << key);

            // If the inherited prim has its own inheritPaths, propagate them
            // to this prim before applying the inherit.
            if (src_ps->metas().inheritPaths) {
              for (const auto &nested_op : src_ps->metas().inheritPaths.value()) {
                if (!primspec.metas().inheritPaths) {
                  primspec.metas().inheritPaths.emplace();
                }
                primspec.metas().inheritPaths->push_back(nested_op);
              }
            }

            if (!InheritPrimSpec(primspec, *src_ps, warn, err)) {
              return false;
            }

            // Recursively process any newly added inheritPaths
            if (!self(self)) {
              return false;
            }
          }
        }
      }
      return true;
    };

    if (!process_inheritPaths(process_inheritPaths)) {
      return false;
    }
  }

  return true;
}

bool ExtractReferencesAssetPathsImpl(const PrimSpec &primspec, std::vector<std::string> &paths) {

  constexpr size_t kMaxIter = 1024 * 1024;

  std::vector<const PrimSpec *> stack;
  stack.push_back(&primspec);
  size_t iter = 0;

  while (!stack.empty()) {
    if (iter++ > kMaxIter) {
      return false;
    }

    const PrimSpec *current = stack.back();
    stack.pop_back();

    if (current->metas().references) {
      // Iterate over all listops (supports multiple listops per arc)
      for (const auto &ref_op : current->metas().references.value()) {
        // TODO: qualifier
        //const ListEditQual &qual = ref_op.first;
        const auto &refecences = ref_op.second;

        for (const auto &reference : refecences) {
          paths.push_back(reference.asset_path.GetAssetPath());
        }
      }
    }

    // Push children in reverse order to preserve DFS order.
    const auto &children = current->children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
      stack.push_back(&(*it));
    }
  }

  return true;

}

} // namespace


std::vector<std::string> ExtractReferencesAssetPaths(const Layer &layer) {

  std::vector<std::string> paths;

  for (const auto &ps : layer.primspecs()) {
    ExtractReferencesAssetPathsImpl(ps.second, paths);
  }

  return paths;

}


namespace {

// Internal implementation that accepts a shared visited set for cross-arc cycle detection.
bool CompositeReferencesImpl(AssetResolutionResolver &resolver,
                             const Layer &in_layer, Layer *composited_layer,
                             std::string *warn, std::string *err,
                             const ReferencesCompositionOptions &options,
                             ArcVisitedSet &visited) {
  if (!composited_layer) {
    return false;
  }

  std::vector<std::string> search_paths = in_layer.get_asset_search_paths();

  Layer dst = in_layer;  // deep copy

  for (auto &item : dst.primspecs()) {
    Path primPath("/" + item.first, "");
    if (!CompositeReferencesRec(/* depth */ 0, resolver, search_paths, primPath, in_layer,
                                item.second, warn, err, options, visited)) {
      if (err) { (*err) += "Composite `references` failed.\n"; }
      return false;
    }
  }

  (*composited_layer) = dst;

  DCOUT("Composite `references` ok.");
  return true;
}

}  // namespace

bool CompositeReferences(AssetResolutionResolver &resolver,
                         const Layer &in_layer, Layer *composited_layer,
                         std::string *warn, std::string *err,
                         ReferencesCompositionOptions options) {
  ArcVisitedSet visited;
  return CompositeReferencesImpl(resolver, in_layer, composited_layer,
                                 warn, err, options, visited);
}

namespace {

bool ExtractPayloadAssetPathsImpl(const PrimSpec &primspec, std::vector<std::string> &paths) {

  constexpr size_t kMaxIter = 1024 * 1024;

  std::vector<const PrimSpec *> stack;
  stack.push_back(&primspec);
  size_t iter = 0;

  while (!stack.empty()) {
    if (iter++ > kMaxIter) {
      return false;
    }

    const PrimSpec *current = stack.back();
    stack.pop_back();

    if (current->metas().payload) {
      // Iterate over all listops (supports multiple listops per arc)
      for (const auto &payload_op : current->metas().payload.value()) {
        // TODO: qualifier
        //const ListEditQual &qual = payload_op.first;
        const auto &payload = payload_op.second;

        for (const auto &pl : payload) {
          paths.push_back(pl.asset_path.GetAssetPath());
        }
      }
    }

    // Push children in reverse order to preserve DFS order.
    const auto &children = current->children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
      stack.push_back(&(*it));
    }
  }

  return true;

}

} // namespace


std::vector<std::string> ExtractPayloadAssetPaths(const Layer &layer) {

  std::vector<std::string> paths;

  for (const auto &ps : layer.primspecs()) {
    ExtractPayloadAssetPathsImpl(ps.second, paths);
  }

  return paths;

}


namespace {

bool CompositePayloadImpl(AssetResolutionResolver &resolver, const Layer &in_layer,
                          Layer *composited_layer, std::string *warn,
                          std::string *err, const PayloadCompositionOptions &options,
                          ArcVisitedSet &visited) {
  if (!composited_layer) {
    return false;
  }

  Layer dst = in_layer;  // deep copy

  for (auto &item : dst.primspecs()) {
    Path primPath("/" + item.first, "");
    if (!CompositePayloadRec(/* depth */ 0, resolver,
                             item.second.get_asset_search_paths(), primPath, in_layer, item.second,
                             warn, err, options, visited)) {
      if (err) { (*err) += "Composite `payload` failed.\n"; }
      return false;
    }
  }

  (*composited_layer) = dst;

  DCOUT("Composite `payload` ok.");
  return true;
}

}  // namespace

bool CompositePayload(AssetResolutionResolver &resolver, const Layer &in_layer,
                      Layer *composited_layer, std::string *warn,
                      std::string *err, PayloadCompositionOptions options) {
  ArcVisitedSet visited;
  return CompositePayloadImpl(resolver, in_layer, composited_layer,
                              warn, err, options, visited);
}

bool CompositeVariant(const Layer &in_layer, Layer *composited_layer,
                      std::string *warn, std::string *err) {
  if (!composited_layer) {
    return false;
  }

  Layer dst = in_layer;  // deep copy

  for (auto &item : dst.primspecs()) {
    if (!CompositeVariantRec(/* depth */ 0, item.second, warn, err)) {
      PUSH_ERROR_AND_RETURN("Composite `variantSet` failed.");
    }
  }

  (*composited_layer) = dst;

  DCOUT("Composite `variantSet` ok.");
  return true;
}

bool CompositeInherits(const Layer &in_layer, Layer *composited_layer,
                       std::string *warn, std::string *err) {
  if (!composited_layer) {
    return false;
  }

  Layer dst = in_layer;  // deep copy

  PathVisitedSet visited;  // Cycle detection set

  for (auto &item : dst.primspecs()) {
    if (!CompositeInheritsRec(/* depth */ 0, dst, item.second, warn, err, visited)) {
      PUSH_ERROR_AND_RETURN("Composite `inherits` failed.");
    }
  }

  (*composited_layer) = dst;

  DCOUT("Composite `inherits` ok.");
  return true;
}

// Forward declare InheritPrimSpecImpl — reused by Specializes (same semantics)
namespace detail {
static bool InheritPrimSpecImpl(PrimSpec &dst, const PrimSpec &src,
                                std::string *warn, std::string *err);
}  // namespace detail

static bool CompositeSpecializesRec(uint32_t depth, const Layer &layer,
                                    PrimSpec &primspec /* [inout] */,
                                    std::string *warn, std::string *err,
                                    PathVisitedSet &visited) {
  if (depth > (1024 * 1024)) {
    PUSH_ERROR_AND_RETURN("Too deep in CompositeSpecializesRec.");
  }

  // Traverse children first.
  for (auto &child : primspec.children()) {
    if (!CompositeSpecializesRec(depth + 1, layer, child, warn, err, visited)) {
      return false;
    }
  }

  if (primspec.metas().specializes) {
    // Resolve all ListEditQual operations into a single ordered path list.
    auto specializes_copy = primspec.metas().specializes.value();
    std::vector<Path> resolved = ResolveListOps(specializes_copy, warn);

    // Process the resolved path list.
    for (const auto &specializePath : resolved) {
      // Cycle detection
      std::string key = specializePath.prim_part();
      if (visited.count(key)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Cycle detected in `specializes`: prim <{}>", key));
      }
      visited.insert(key);

      const PrimSpec *src_ps{nullptr};

      if (!layer.find_primspec_at(specializePath, &src_ps, err)) {
        visited.erase(key);
        if (err) {
          (*err) += "Specialize failed: Path <" +
                    specializePath.prim_part() + "> not found.\n";
        }
        return false;
      }

      if (!src_ps) {
        visited.erase(key);
        PUSH_ERROR_AND_RETURN(
            "Internal error: PrimSpec is nullptr in CompositeSpecializesRec.\n");
      }

      if (!detail::InheritPrimSpecImpl(primspec, *src_ps, warn, err)) {
        visited.erase(key);
        return false;
      }
      visited.erase(key);
    }

    // Clear specializes metadata after processing.
    primspec.metas().specializes.reset();
  }

  // AOUSD Core Spec 10.3.2.4: Implied specializes.
  // If this prim has specializePaths propagated from referenced layers,
  // apply those specializes from matching prims in the current layer.
  if (primspec.metas().specializePaths) {
    auto sp_copy = primspec.metas().specializePaths.value();
    for (const auto &sp_op : sp_copy) {
      const auto &paths = sp_op.second;
      for (const auto &spPath : paths) {
        const PrimSpec *src_ps{nullptr};
        std::string _err;
        if (layer.find_primspec_at(spPath, &src_ps, &_err)) {
          if (src_ps) {
            std::string key = spPath.prim_part();
            if (!visited.count(key)) {
              visited.insert(key);
              DCOUT("Applying implied specialize from " << spPath.prim_part());
              if (!detail::InheritPrimSpecImpl(primspec, *src_ps, warn, err)) {
                visited.erase(key);
                return false;
              }
              visited.erase(key);
            }
          }
        }
        // If not found, that's OK -- the class may not exist in this layer stack
      }
    }
    primspec.metas().specializePaths.reset();
  }

  return true;
}

bool CompositeSpecializes(const Layer &in_layer, Layer *composited_layer,
                          std::string *warn, std::string *err) {
  if (!composited_layer) {
    return false;
  }

  Layer dst = in_layer;

  PathVisitedSet visited;  // Cycle detection set

  for (auto &item : dst.primspecs()) {
    if (!CompositeSpecializesRec(/* depth */ 0, dst, item.second, warn, err, visited)) {
      PUSH_ERROR_AND_RETURN("Composite `specializes` failed.");
    }
  }

  (*composited_layer) = dst;

  DCOUT("Composite `specializes` ok.");
  return true;
}

namespace detail {

static bool OverridePrimSpecRec(uint32_t depth, PrimSpec &dst,
                                const PrimSpec &src, std::string *warn,
                                std::string *err) {
  (void)warn;

  if (depth > (1024 * 1024 * 128)) {
    PUSH_ERROR_AND_RETURN("PrimSpec tree too deep.");
  }

  DCOUT("update_from");
  DCOUT(print_prim_metas(src.metas(), 1));
  // Override metadataum
  dst.metas().update_from(src.metas());
  DCOUT("update_from done");

  // Override properties with AOUSD Core Spec 12.2.4 (custom) handling:
  // The `custom` flag is true if ANY opinion in the stack says true.
  for (const auto &prop : src.props()) {
    if (dst.props().count(prop.first)) {
      // AOUSD Core Spec 12.2.3: Preserve uniform variability from weaker opinion
      Variability preserved_variability = Variability::Varying;
      if (dst.props().at(prop.first).is_attribute() && prop.second.is_attribute()) {
        preserved_variability = dst.props().at(prop.first).get_attribute().variability();
      }

      bool dst_custom = dst.props().at(prop.first).has_custom();
      dst.props()[prop.first] = prop.second;
      if (dst_custom && !prop.second.has_custom()) {
        dst.props()[prop.first].set_custom(true);
      }

      // AOUSD Core Spec 12.2.3: If overriding property had uniform variability
      // and the override source is varying, preserve uniform.
      if (dst.props()[prop.first].is_attribute() &&
          preserved_variability == Variability::Uniform &&
          dst.props()[prop.first].attribute().variability() == Variability::Varying) {
        dst.props()[prop.first].attribute().variability() = Variability::Uniform;
      }
    } else {
      dst.props()[prop.first] = prop.second;
    }
  }

  // Override child primspecs.
  for (auto &child : dst.children()) {
    auto src_it = std::find_if(
        src.children().begin(), src.children().end(),
        [&child](const PrimSpec &ps) { return ps.name() == child.name(); });

    if (src_it != src.children().end()) {
      if (!OverridePrimSpecRec(depth + 1, child, (*src_it), warn, err)) {
        return false;
      }
    }
  }

  // Add child not exists in dst.
  for (auto &child : src.children()) {
    auto dst_it = std::find_if(
        dst.children().begin(), dst.children().end(),
        [&child](const PrimSpec &ps) { return ps.name() == child.name(); });

    if (dst_it == dst.children().end()) {
      dst.children().push_back(child);
    }
  }

  return true;
}

//
// TODO: Support nested inherits?
//
static bool InheritPrimSpecImpl(PrimSpec &dst, const PrimSpec &src,
                                std::string *warn, std::string *err) {
  DCOUT("inherit begin\n");
  (void)warn;

  DCOUT("src = " << prim::print_primspec(src));

  // Create PrimSpec from `src`,
  // Then override it with `dst`
  PrimSpec ps = src;  // copy

  // Keep PrimSpec name from `dst`
  ps.name() = dst.name();

  // AOUSD Core Spec 12.2.2 (typeName): typeName is determined from the
  // "prim definition" -- only use typeName from defining specs (def/class),
  // not from `over` specs. Stronger defining opinion wins.
  if (!dst.typeName().empty()) {
    // dst has a typeName -- if dst is defining (def/class), it wins
    ps.typeName() = dst.typeName();
  } else if (dst.specifier() == Specifier::Over && !src.typeName().empty()) {
    // dst is an over with no typeName: inherit from src (the definition)
    // ps.typeName() already has src's typeName from the copy
  }

  // AOUSD Core Spec 12.2.1 (specifier): Composed specifier resolution.
  // - If dst (stronger opinion) is `over`, the result depends on whether
  //   any defining spec exists. For now, keep dst specifier as the
  //   strongest opinion, which matches the simple case.
  // - If dst is `def` or `class`, it takes precedence.
  ps.specifier() = dst.specifier();

  // Override metadataum
  ps.metas().update_from(dst.metas());

  // Override properties with AOUSD Core Spec 12.2.4 (custom) handling:
  // The `custom` flag is true if ANY opinion in the stack says true.
  for (const auto &prop : dst.props()) {
    if (ps.props().count(prop.first)) {
      // AOUSD Core Spec 12.2.3: Preserve uniform variability from weaker (inherited) opinion
      Variability inherited_variability = Variability::Varying;
      if (ps.props().at(prop.first).is_attribute() && prop.second.is_attribute()) {
        inherited_variability = ps.props().at(prop.first).get_attribute().variability();
      }

      // AOUSD Core Spec 12.2.4: OR the custom flags before replacing
      bool src_custom = ps.props().at(prop.first).has_custom();
      ps.props().at(prop.first) = prop.second;
      if (src_custom && !prop.second.has_custom()) {
        ps.props().at(prop.first).set_custom(true);
      }

      // AOUSD Core Spec 12.2.3: If inherited property had uniform variability
      // and the overriding (stronger) is varying, preserve uniform.
      if (ps.props().at(prop.first).is_attribute() &&
          inherited_variability == Variability::Uniform &&
          ps.props().at(prop.first).attribute().variability() == Variability::Varying) {
        ps.props().at(prop.first).attribute().variability() = Variability::Uniform;
      }
    }
    else {
      // re-add
      ps.props()[prop.first] = prop.second;
    }
  }

  // Overide child primspecs.
  for (auto &child : ps.children()) {
    auto src_it = std::find_if(dst.children().begin(), dst.children().end(),
                               [&child](const PrimSpec &primspec) {
                                 return primspec.name() == child.name();
                               });

    if (src_it != dst.children().end()) {
      if (!OverridePrimSpecRec(1, child, (*src_it), warn, err)) {
        return false;
      }
    }
  }

  DCOUT("move");
  dst = std::move(ps);
  DCOUT("move done");

  return true;
}

}  // namespace detail

bool OverridePrimSpec(PrimSpec &dst, const PrimSpec &src, std::string *warn,
                      std::string *err) {
  if (src.specifier() != Specifier::Over) {
    PUSH_ERROR("src PrimSpec must be qualified with `over` specifier.\n");
  }

  return detail::OverridePrimSpecRec(0, dst, src, warn, err);
}

bool InheritPrimSpec(PrimSpec &dst, const PrimSpec &src, std::string *warn,
                     std::string *err) {
  return detail::InheritPrimSpecImpl(dst, src, warn, err);
}


bool HasReferences(const Layer &layer, const bool force_check,
                   const ReferencesCompositionOptions options) {
  if (!force_check) {
    return layer.has_unresolved_references();
  }

  return layer.check_unresolved_references(options.max_depth);
}

bool HasPayload(const Layer &layer, const bool force_check,
                const PayloadCompositionOptions options) {
  if (!force_check) {
    return layer.has_unresolved_payload();
  }

  return layer.check_unresolved_payload(options.max_depth);
}

bool HasInherits(const Layer &layer) {
  return layer.check_unresolved_inherits();
}

bool HasVariants(const Layer &layer) {
  return layer.check_unresolved_variant();
}

bool HasOver(const Layer &layer) { return layer.check_over_primspec(); }

bool HasSpecializes(const Layer &layer) {
  return layer.check_unresolved_specializes();
}

namespace {

// AOUSD Core Spec 10.3.2.5: Collect variant selection opinions from a PrimSpec tree.
// Returns a map of (prim_path -> VariantSelectionMap) for all prims that have
// variant selections authored.
void CollectVariantSelectionOpinionsRec(
    const std::string &path_prefix,
    const PrimSpec &ps,
    std::map<std::string, std::vector<VariantSelectionMap>> &opinions) {
  std::string prim_path = path_prefix + "/" + ps.name();

  if (ps.metas().variants) {
    opinions[prim_path].push_back(ps.metas().variants.value());
  }

  for (const auto &child : ps.children()) {
    CollectVariantSelectionOpinionsRec(prim_path, child, opinions);
  }
}

void CollectVariantSelectionOpinions(
    const Layer &layer,
    std::map<std::string, std::vector<VariantSelectionMap>> &opinions) {
  for (const auto &item : layer.primspecs()) {
    CollectVariantSelectionOpinionsRec("", item.second, opinions);
  }
}

// AOUSD Core Spec 10.3.2.5: Compute final variant selections from collected opinions.
// Strongest opinion (first in vector) wins per variant set name.
VariantSelectionMap ComputeVariantSelections(
    const std::vector<VariantSelectionMap> &opinions) {
  VariantSelectionMap result;

  // Iterate from weakest to strongest so strongest overwrites
  for (auto it = opinions.rbegin(); it != opinions.rend(); ++it) {
    for (const auto &sel : *it) {
      result[sel.first] = sel.second;
    }
  }

  return result;
}

// Apply pre-computed variant selections to a PrimSpec tree.
bool ApplyDeferredVariantSelectionsRec(
    uint32_t depth,
    const std::string &path_prefix,
    PrimSpec &primspec,
    const std::map<std::string, VariantSelectionMap> &resolved_selections,
    std::string *warn, std::string *err) {
  if (depth > (1024 * 1024)) {
    if (err) { *err += "Too deep in ApplyDeferredVariantSelectionsRec.\n"; }
    return false;
  }

  std::string prim_path = path_prefix + "/" + primspec.name();

  // Traverse children first.
  for (auto &child : primspec.children()) {
    if (!ApplyDeferredVariantSelectionsRec(depth + 1, prim_path, child,
                                            resolved_selections, warn, err)) {
      return false;
    }
  }

  // Apply variant selection for this prim
  std::map<std::string, std::string> selection;
  auto sel_it = resolved_selections.find(prim_path);
  if (sel_it != resolved_selections.end()) {
    selection = sel_it->second;
  }

  PrimSpec dst;
  if (!VariantSelectPrimSpec(dst, primspec, selection, warn, err)) {
    return false;
  }

  primspec = std::move(dst);
  return true;
}

// AOUSD Core Spec 10.3.2.6: Apply relocates to a PrimSpec tree.
// Relocates rename prims in the namespace according to layerRelocates entries.
// Helper: find a mutable PrimSpec at a given absolute path in a PrimSpec tree.
// Returns nullptr if not found.
PrimSpec *FindMutablePrimSpec(PrimSpec &root, const std::string &root_path,
                              const std::string &target_path) {
  if (root_path == target_path) return &root;

  // Check if target is a descendant of root
  if (target_path.size() <= root_path.size()) return nullptr;
  if (target_path.substr(0, root_path.size()) != root_path) return nullptr;
  if (target_path[root_path.size()] != '/') return nullptr;

  // Walk down the tree
  std::string remaining = target_path.substr(root_path.size() + 1);
  PrimSpec *current = &root;
  while (!remaining.empty()) {
    auto slash = remaining.find('/');
    std::string segment = (slash == std::string::npos)
        ? remaining : remaining.substr(0, slash);

    PrimSpec *found = nullptr;
    for (auto &child : current->children()) {
      if (child.name() == segment) {
        found = &child;
        break;
      }
    }
    if (!found) return nullptr;
    current = found;

    if (slash == std::string::npos) break;
    remaining = remaining.substr(slash + 1);
  }
  return current;
}

// Helper: remove a child PrimSpec by name from a parent's children vector.
// Returns the detached PrimSpec, or nullopt if not found.
nonstd::optional<PrimSpec> DetachChild(PrimSpec &parent, const std::string &child_name) {
  auto &children = parent.children();
  for (auto it = children.begin(); it != children.end(); ++it) {
    if (it->name() == child_name) {
      PrimSpec detached = std::move(*it);
      children.erase(it);
      return detached;
    }
  }
  return nonstd::nullopt;
}

// Helper: get or create a PrimSpec at the given path under a root.
// Creates intermediate `over` prims as needed.
PrimSpec *GetOrCreatePrimSpec(PrimSpec &root, const std::string &root_path,
                              const std::string &target_path) {
  if (root_path == target_path) return &root;
  if (target_path.size() <= root_path.size()) return nullptr;
  if (target_path.substr(0, root_path.size()) != root_path) return nullptr;
  if (target_path[root_path.size()] != '/') return nullptr;

  std::string remaining = target_path.substr(root_path.size() + 1);
  PrimSpec *current = &root;
  while (!remaining.empty()) {
    auto slash = remaining.find('/');
    std::string segment = (slash == std::string::npos)
        ? remaining : remaining.substr(0, slash);

    PrimSpec *found = nullptr;
    for (auto &child : current->children()) {
      if (child.name() == segment) {
        found = &child;
        break;
      }
    }
    if (!found) {
      // Create intermediate over prim
      current->children().emplace_back(Specifier::Over, "", segment);
      found = &current->children().back();
    }
    current = found;

    if (slash == std::string::npos) break;
    remaining = remaining.substr(slash + 1);
  }
  return current;
}

// Helper: get parent path from a full path. "/Root/Child" -> "/Root"
std::string GetParentPath(const std::string &path) {
  auto last_slash = path.rfind('/');
  if (last_slash == std::string::npos || last_slash == 0) return "";
  return path.substr(0, last_slash);
}

// Helper: get element name from a full path. "/Root/Child" -> "Child"
std::string GetElementName(const std::string &path) {
  auto last_slash = path.rfind('/');
  if (last_slash == std::string::npos) return path;
  return path.substr(last_slash + 1);
}

// Remap all path references in a PrimSpec tree using a namespace mapping.
// Handles: relationships, connections, inherits, specializes, references, payloads.
void RemapPathsInPrimSpecTree(PrimSpec &ps, const NamespaceMapping &mapping) {
  std::vector<PrimSpec *> stack;
  stack.push_back(&ps);

  while (!stack.empty()) {
    PrimSpec *current = stack.back();
    stack.pop_back();

    // Remap relationship targets and attribute connections
    for (auto &prop : current->props()) {
      if (prop.second.is_relationship()) {
        Relationship &rel = prop.second.relationship();
        if (rel.is_path()) {
          rel.targetPath = mapping.Apply(rel.targetPath);
        } else if (rel.is_pathvector()) {
          for (auto &p : rel.targetPathVector) {
            p = mapping.Apply(p);
          }
        }
      } else if (prop.second.is_attribute_connection()) {
        Attribute &attr = prop.second.attribute();
        for (auto &conn : attr.connections()) {
          conn = mapping.Apply(conn);
        }
      }
    }

    // Remap composition arc target paths
    if (current->metas().inherits) {
      for (auto &op : current->metas().inherits.value()) {
        for (auto &p : op.second) {
          p = mapping.Apply(p);
        }
      }
    }
    if (current->metas().specializes) {
      for (auto &op : current->metas().specializes.value()) {
        for (auto &p : op.second) {
          p = mapping.Apply(p);
        }
      }
    }
    if (current->metas().references) {
      for (auto &op : current->metas().references.value()) {
        for (auto &ref : op.second) {
          if (ref.asset_path.GetAssetPath().empty() && ref.prim_path.is_valid()) {
            // Internal reference — remap the prim path
            ref.prim_path = mapping.Apply(ref.prim_path);
          }
        }
      }
    }
    if (current->metas().payload) {
      for (auto &op : current->metas().payload.value()) {
        for (auto &pl : op.second) {
          if (pl.asset_path.GetAssetPath().empty() && pl.prim_path.is_valid()) {
            pl.prim_path = mapping.Apply(pl.prim_path);
          }
        }
      }
    }

    for (auto &child : current->children()) {
      stack.push_back(&child);
    }
  }
}

}  // namespace

// AOUSD Core Spec 10.3.2.6: Apply layerRelocates to a composed layer.
// Relocates are applied after all other composition arcs.
//
// Implementation:
// 1. Validate relocate entries
// 2. For each relocate (src → tgt): detach the prim at src, reattach at tgt
// 3. Remap all path references throughout the tree
bool CompositeRelocates(const Layer &in_layer, Layer *composited_layer,
                        std::string *warn, std::string * /* err */) {
  if (!composited_layer) {
    return false;
  }

  const auto &relocates = in_layer.metas().layerRelocates;
  if (relocates.empty()) {
    *composited_layer = in_layer;
    return true;
  }

  // Validate
  auto validation_errors = ValidateRelocates(relocates);
  for (const auto &ve : validation_errors) {
    PushWarn("Relocates validation: " + ve + "\n");
  }

  Layer dst = in_layer;

  // Build namespace mapping for path remapping
  NamespaceMapping relocate_mapping;
  for (const auto &entry : relocates) {
    relocate_mapping.entries.push_back(entry);
  }

  // Phase 1: Move prims from source to target locations.
  // Process relocates sorted by path depth (deepest first) to avoid
  // invalidating parent paths when moving children.
  auto sorted_relocates = relocates;
  std::sort(sorted_relocates.begin(), sorted_relocates.end(),
            [](const std::pair<Path, Path> &a, const std::pair<Path, Path> &b) {
              return a.first.prim_part().size() > b.first.prim_part().size();
            });

  for (const auto &entry : sorted_relocates) {
    const std::string &src = entry.first.prim_part();
    const std::string &tgt = entry.second.prim_part();
    std::string src_parent = GetParentPath(src);
    std::string src_name = GetElementName(src);
    std::string tgt_parent = GetParentPath(tgt);
    std::string tgt_name = GetElementName(tgt);

    if (src_parent.empty() || tgt_parent.empty()) {
      // Root-level relocate: handle via primspecs map
      if (src_parent.empty() && tgt_parent.empty()) {
        // Root → Root rename (e.g., /Old → /New)
        auto it = dst.primspecs().find(src_name);
        if (it != dst.primspecs().end()) {
          PrimSpec ps = std::move(it->second);
          ps.name() = tgt_name;
          dst.primspecs().erase(it);
          dst.primspecs()[tgt_name] = std::move(ps);
          DCOUT("Relocate root: " << src_name << " -> " << tgt_name);
        }
      } else if (src_parent.empty()) {
        // Root → nested: detach from root map, attach under target parent
        auto it = dst.primspecs().find(src_name);
        if (it != dst.primspecs().end()) {
          PrimSpec ps = std::move(it->second);
          ps.name() = tgt_name;
          dst.primspecs().erase(it);

          // Find target parent (must be a root prim)
          std::string tgt_root = tgt_parent.substr(1); // strip leading /
          auto slash = tgt_root.find('/');
          std::string tgt_root_name = (slash == std::string::npos) ? tgt_root : tgt_root.substr(0, slash);
          auto tgt_root_it = dst.primspecs().find(tgt_root_name);
          if (tgt_root_it != dst.primspecs().end()) {
            PrimSpec *parent_ps = GetOrCreatePrimSpec(
                tgt_root_it->second, "/" + tgt_root_name, tgt_parent);
            if (parent_ps) {
              parent_ps->children().push_back(std::move(ps));
            }
          }
        }
      }
      continue;
    }

    // Non-root relocate: find source parent, detach child, find target parent, reattach
    // Find root prim for source
    std::string src_root_name = src.size() > 1 ? src.substr(1) : "";
    auto slash = src_root_name.find('/');
    if (slash != std::string::npos) src_root_name = src_root_name.substr(0, slash);

    auto src_root_it = dst.primspecs().find(src_root_name);
    if (src_root_it == dst.primspecs().end()) continue;

    // Find the source parent PrimSpec
    PrimSpec *src_parent_ps = nullptr;
    if (src_parent == "/" + src_root_name) {
      src_parent_ps = &src_root_it->second;
    } else {
      src_parent_ps = FindMutablePrimSpec(
          src_root_it->second, "/" + src_root_name, src_parent);
    }
    if (!src_parent_ps) continue;

    // Detach the child
    auto detached = DetachChild(*src_parent_ps, src_name);
    if (!detached) continue;

    detached->name() = tgt_name;

    // Find/create target parent
    std::string tgt_root_name = tgt.size() > 1 ? tgt.substr(1) : "";
    auto tslash = tgt_root_name.find('/');
    if (tslash != std::string::npos) tgt_root_name = tgt_root_name.substr(0, tslash);

    auto tgt_root_it = dst.primspecs().find(tgt_root_name);
    if (tgt_root_it == dst.primspecs().end()) continue;

    if (tgt_parent == "/" + tgt_root_name) {
      tgt_root_it->second.children().push_back(std::move(*detached));
    } else {
      PrimSpec *tgt_parent_ps = GetOrCreatePrimSpec(
          tgt_root_it->second, "/" + tgt_root_name, tgt_parent);
      if (tgt_parent_ps) {
        tgt_parent_ps->children().push_back(std::move(*detached));
      }
    }

    DCOUT("Relocate: " << src << " -> " << tgt);
  }

  // Phase 2: Remap all path references throughout the tree
  for (auto &item : dst.primspecs()) {
    RemapPathsInPrimSpecTree(item.second, relocate_mapping);
  }

  // Clear layerRelocates after processing
  dst.metas().layerRelocates.clear();

  *composited_layer = dst;
  DCOUT("Composite `relocates` ok.");
  return true;
}

// ---------------------------------------------------------------------------
// ListVariantSelectionMaps / ApplyVariantSelector
// ---------------------------------------------------------------------------

namespace {

void ListVariantSelectionMapsRec(const std::string &path_prefix,
                                  const PrimSpec &ps,
                                  VariantSelectorMap &m) {
  std::string prim_path = path_prefix + "/" + ps.name();

  if (ps.metas().variants || !ps.variantSets().empty()) {
    VariantSelector sel;
    if (ps.metas().variants) {
      sel.vsmap = ps.metas().variants.value();
      // Use the first variant set's selection as `selection`
      if (!sel.vsmap.empty()) {
        sel.selection = sel.vsmap.begin()->second;
      }
    }
    m[Path(prim_path, "")] = sel;
  }

  for (const auto &child : ps.children()) {
    ListVariantSelectionMapsRec(prim_path, child, m);
  }
}

bool ApplyVariantSelectorRec(uint32_t depth,
                              PrimSpec &primspec,
                              const VariantSelectorMap &vsmap,
                              const std::string &path_prefix,
                              std::string *warn, std::string *err) {
  if (depth > (1024 * 1024)) {
    if (err) { (*err) += "Too deep in ApplyVariantSelectorRec.\n"; }
    return false;
  }

  std::string prim_path = path_prefix + "/" + primspec.name();

  // Check if this prim has a variant selector entry
  auto it = vsmap.find(Path(prim_path, ""));
  if (it != vsmap.end()) {
    // Use the vsmap from the selector, or fall back to the PrimSpec's own variants
    const auto &selections = it->second.vsmap.empty()
        ? (primspec.metas().variants ? primspec.metas().variants.value()
                                     : VariantSelectionMap{})
        : it->second.vsmap;

    if (!selections.empty()) {
      PrimSpec dst;
      if (!VariantSelectPrimSpec(dst, primspec, selections, warn, err)) {
        return false;
      }
      primspec = std::move(dst);
    }
  }

  // Recurse into children
  for (auto &child : primspec.children()) {
    if (!ApplyVariantSelectorRec(depth + 1, child, vsmap, prim_path, warn, err)) {
      return false;
    }
  }

  return true;
}

}  // namespace

bool ListVariantSelectionMaps(const Layer &layer, VariantSelectorMap &m) {
  m.clear();
  for (const auto &item : layer.primspecs()) {
    ListVariantSelectionMapsRec("", item.second, m);
  }
  return true;
}

bool ApplyVariantSelector(const Layer &layer, const VariantSelectorMap &vsmap,
                          Layer *dst, std::string *warn, std::string *err) {
  if (!dst) {
    if (err) { (*err) += "dst is nullptr.\n"; }
    return false;
  }

  *dst = layer;  // deep copy

  for (auto &item : dst->primspecs()) {
    if (!ApplyVariantSelectorRec(0, item.second, vsmap, "", warn, err)) {
      return false;
    }
  }

  return true;
}

bool ApplyVariantSelector(const Layer &layer, const std::string &variant_name,
                          Layer *dst, std::string *warn, std::string *err) {
  // Build a VariantSelectorMap from the layer's existing variant info,
  // overriding all selections with the given variant_name.
  VariantSelectorMap vsmap;
  ListVariantSelectionMaps(layer, vsmap);

  for (auto &item : vsmap) {
    // Override all variant set selections with the given name
    for (auto &sel : item.second.vsmap) {
      sel.second = variant_name;
    }
    item.second.selection = variant_name;
  }

  return ApplyVariantSelector(layer, vsmap, dst, warn, err);
}

// Remove prims (and their descendants) that have `active = false`.
// This is a post-composition pass — active is a composed metadatum.
static void RemoveInactivePrimsRec(std::vector<PrimSpec> &children) {
  // Erase inactive children first.
  children.erase(
      std::remove_if(children.begin(), children.end(),
                     [](const PrimSpec &ps) {
                       return ps.metas().has_active() &&
                              !ps.metas().get_active();
                     }),
      children.end());

  // Recurse into remaining children.
  for (auto &child : children) {
    RemoveInactivePrimsRec(child.children());
  }
}

// AOUSD Core Spec 10.4: Composite all arcs in LIVERPS order.
// L(ocal/sublayers) is assumed already done before calling this function.
// Order: I(nherits) > V(ariants) > R(eferences) > P(ayloads) > S(pecializes)
//
// AOUSD Core Spec 10.3.2.5: Variant selection is deferred -- selections are
// computed using opinions from ALL arcs (I, R, P, S), not just local.
//
// Specializes (S) is applied last and is globally weaker than all other
// opinions per Spec 10.4.1.
bool CompositeAllArcs(AssetResolutionResolver &resolver, const Layer &layer,
                      Layer *composited_layer, std::string *warn,
                      std::string *err) {
  if (!composited_layer) {
    if (err) { *err = "composited_layer is nullptr."; }
    return false;
  }

  // Start with a copy of the input layer
  Layer working = layer;

  // Phase 1: Collect variant selection opinions from local layer (strongest)
  std::map<std::string, std::vector<VariantSelectionMap>> variant_opinions;
  CollectVariantSelectionOpinions(working, variant_opinions);

  // I: Inherits (strongest arc type after Local)
  // Process in-place to avoid deep copy.
  if (HasInherits(working)) {
    PathVisitedSet inh_visited;
    for (auto &item : working.primspecs()) {
      if (!CompositeInheritsRec(0, working, item.second, warn, err, inh_visited)) {
        PushError("Composite `inherits` failed.\n");
        return false;
      }
    }
    // Collect additional variant opinions from inherited content
    CollectVariantSelectionOpinions(working, variant_opinions);
  }

  // Skip V for now -- defer variant evaluation until after R and P

  // Shared visited set for cross-arc cycle detection between R and P phases.
  // This catches cycles where an asset is loaded as both a reference and payload.
  ArcVisitedSet arc_visited;

  // R: References
  if (HasReferences(working)) {
    Layer tmp;
    ReferencesCompositionOptions ref_opts;
    if (!CompositeReferencesImpl(resolver, working, &tmp, warn, err,
                                 ref_opts, arc_visited)) {
      return false;
    }
    working = std::move(tmp);
    // Collect variant opinions from referenced content
    CollectVariantSelectionOpinions(working, variant_opinions);
  }

  // P: Payloads
  if (HasPayload(working)) {
    Layer tmp;
    PayloadCompositionOptions pl_opts;
    if (!CompositePayloadImpl(resolver, working, &tmp, warn, err,
                              pl_opts, arc_visited)) {
      return false;
    }
    working = std::move(tmp);
    // Collect variant opinions from payload content
    CollectVariantSelectionOpinions(working, variant_opinions);
  }

  // Phase 2: Compute variant selections with full opinion stack (strongest wins)
  // and apply them. This implements AOUSD Core Spec 10.3.2.5 deferred evaluation.
  if (HasVariants(working)) {
    // Resolve per-prim selections
    std::map<std::string, VariantSelectionMap> resolved;
    for (const auto &item : variant_opinions) {
      resolved[item.first] = ComputeVariantSelections(item.second);
    }

    // Apply resolved selections
    for (auto &ps_item : working.primspecs()) {
      if (!ApplyDeferredVariantSelectionsRec(
              0, "", ps_item.second, resolved, warn, err)) {
        PushError("Deferred variant evaluation failed.\n");
        return false;
      }
    }
  }

  // S: Specializes (globally weaker per Spec 10.4.1)
  // Applied last so all other opinions take precedence.
  // Process in-place to avoid deep copy.
  if (HasSpecializes(working)) {
    PathVisitedSet sp_visited;
    for (auto &item : working.primspecs()) {
      if (!CompositeSpecializesRec(0, working, item.second, warn, err, sp_visited)) {
        PushError("Composite `specializes` failed.\n");
        return false;
      }
    }
  }

  // Relocates (Spec 10.3.2.6): Applied after all other composition arcs.
  // Relocates rename prims in the composed namespace.
  // CompositeRelocates needs to copy because it modifies prim names/paths,
  // which would invalidate the primspecs map during iteration.
  if (!working.metas().layerRelocates.empty()) {
    Layer tmp;
    if (!CompositeRelocates(working, &tmp, warn, err)) {
      return false;
    }
    working = std::move(tmp);
  }

  // Post-composition: Remove prims with `active = false` and their descendants.
  // Active is a composed metadatum — it must be evaluated after all arcs.
  for (auto &ps_item : working.primspecs()) {
    if (ps_item.second.metas().has_active() &&
        !ps_item.second.metas().get_active()) {
      // Root-level prim is inactive — mark for removal
      // We can't erase during iteration of unordered_map, so handle below.
    }
    RemoveInactivePrimsRec(ps_item.second.children());
  }

  // Remove inactive root-level primspecs.
  {
    auto &ps_map = working.primspecs();
    for (auto it = ps_map.begin(); it != ps_map.end(); ) {
      if (it->second.metas().has_active() &&
          !it->second.metas().get_active()) {
        it = ps_map.erase(it);
      } else {
        ++it;
      }
    }
  }

  *composited_layer = std::move(working);
  return true;
}

}  // namespace tinyusdz
