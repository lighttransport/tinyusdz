// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment, Inc.

#include "composition.hh"

#include <set>
#include <stack>

#if defined(__linux__)
#include <unistd.h>
#endif

#include "asset-resolution.hh"
#include "common-macros.inc"
#include "io-util.hh"
#include "pprinter.hh"
#include "prim-pprint.hh"
#include "prim-reconstruct.hh"
#include "prim-types.hh"
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
  if (err) {         \
    (*err) += s;     \
  }

#define PushWarn(s) \
  if (warn) {       \
    (*warn) += s;   \
  }

namespace tinyusdz {

namespace prim {

// template specialization forward decls.
// implimentations will be located in prim-reconstruct.cc
#define RECONSTRUCT_PRIM_DECL(__ty)                                   \
  template <>                                                         \
  bool ReconstructPrim<__ty>(PrimSpec &, __ty *, std::string *, \
                             std::string *, const PrimReconstructOptions &)

RECONSTRUCT_PRIM_DECL(Xform);
RECONSTRUCT_PRIM_DECL(Model);
RECONSTRUCT_PRIM_DECL(Scope);
RECONSTRUCT_PRIM_DECL(GeomPoints);
RECONSTRUCT_PRIM_DECL(GeomMesh);
RECONSTRUCT_PRIM_DECL(GeomCapsule);
RECONSTRUCT_PRIM_DECL(GeomCube);
RECONSTRUCT_PRIM_DECL(GeomCone);
RECONSTRUCT_PRIM_DECL(GeomCylinder);
RECONSTRUCT_PRIM_DECL(GeomSphere);
RECONSTRUCT_PRIM_DECL(GeomBasisCurves);
RECONSTRUCT_PRIM_DECL(GeomCamera);
RECONSTRUCT_PRIM_DECL(GeomSubset);
RECONSTRUCT_PRIM_DECL(SphereLight);
RECONSTRUCT_PRIM_DECL(DomeLight);
RECONSTRUCT_PRIM_DECL(DiskLight);
RECONSTRUCT_PRIM_DECL(DistantLight);
RECONSTRUCT_PRIM_DECL(RectLight);
RECONSTRUCT_PRIM_DECL(CylinderLight);
RECONSTRUCT_PRIM_DECL(SkelRoot);
RECONSTRUCT_PRIM_DECL(SkelAnimation);
RECONSTRUCT_PRIM_DECL(Skeleton);
RECONSTRUCT_PRIM_DECL(BlendShape);
RECONSTRUCT_PRIM_DECL(Material);
RECONSTRUCT_PRIM_DECL(Shader);

#undef RECONSTRUCT_PRIM_DECL

}  // namespace prim

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
  uint32_t depth,
  const Path &srcPrefix,
  const Path &dstPrefix,
  PrimSpec &ps,
  std::string *warn,
  std::string *err) {

  (void)warn;

  DCOUT("srcPrefix: " << srcPrefix);
  DCOUT("dstPrefix: " << dstPrefix);

  if (depth > (1024 * 1024 * 128)) {
    PUSH_ERROR_AND_RETURN("PrimSpec tree too deep.");
  }

  for (auto &prop : ps.props()) {

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

  // Combine child primspecs.
  for (auto &child : ps.children()) {
    if (!ReplaceRootPrimPathRec(depth + 1, srcPrefix, dstPrefix, child, warn, err)) {
      return false;
    }
  }

  return true;
}

// Copy assetresolver state to all PrimSpec in the tree.
bool PropagateAssetResolverState(uint32_t depth, PrimSpec &ps,
                                 const std::string &cwp,
                                 const std::vector<std::string> &search_paths) {
  if (depth > (1024 * 1024 * 512)) {
    return false;
  }

  if (depth == 0) {
    DCOUT("current_working_path: " << cwp);
    DCOUT("search_paths: " << search_paths);
  }

  ps.set_asset_resolution_state(cwp, search_paths);

  for (auto &child : ps.children()) {
    if (!PropagateAssetResolverState(depth + 1, child, cwp, search_paths)) {
      return false;
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

    if (!PropagateAssetResolverState(0, *const_cast<PrimSpec *>(src_ps),
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

  // Combine metadataum
  dst.metas().update_from(src.metas(), false);

  // Combine properties
  for (const auto &prop : src.props()) {
    // add if not existent
    if (dst.props().count(prop.first) == 0) {
      dst.props()[prop.first] = prop.second;
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

  for (const auto &layer : in_layer.metas().subLayers) {
    // TODO: subLayerOffset
    std::string sublayer_asset_path = layer.assetPath.GetAssetPath();

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


bool CompositeReferencesRec(uint32_t depth, AssetResolutionResolver &resolver,
                            const std::vector<std::string> &asset_search_paths,
                            const Path &dst_prim_path,
                            const Layer &in_layer,
                            PrimSpec &primspec /* [inout] */, std::string *warn,
                            std::string *err,
                            const ReferencesCompositionOptions &options) {
  if (depth > options.max_depth) {
    PUSH_ERROR_AND_RETURN("Too deep.");
  }

  // Traverse children first.
  for (auto &child : primspec.children()) {
    const Path parent_prim_path = dst_prim_path.AppendPrim(child.name());
    if (!CompositeReferencesRec(depth + 1, resolver, asset_search_paths, parent_prim_path, in_layer, child,
                                warn, err, options)) {
      return false;
    }
  }

  // Use PrimSpec's AssetResolution state.
  std::string cwp = primspec.get_current_working_path();
  std::vector<std::string> search_paths = primspec.get_asset_search_paths();

  if (primspec.metas().references) {
    // Process all listops in order (supports multiple listops per arc)
    for (const auto &ref_op : primspec.metas().references.value()) {
      const ListEditQual &qual = ref_op.first;
      const auto &refecences = ref_op.second;

      if ((qual == ListEditQual::ResetToExplicit) ||
          (qual == ListEditQual::Prepend)) {
        for (const auto &reference : refecences) {
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
            if (!LoadAsset(resolver, cwp, search_paths, options.fileformats,
                           reference.asset_path, reference.prim_path, &layer,
                           &src_ps, /* error_when_no_prims_found */ true,
                           options.error_when_asset_not_found,
                           options.error_when_unsupported_fileformat, warn, err)) {
              PUSH_ERROR_AND_RETURN(
                  fmt::format("Failed to `references` asset `{}`",
                              reference.asset_path.GetAssetPath()));
            }
          }

          if (!src_ps) {
            // LoadAsset allowed not-found or unsupported file. so do nothing.
            continue;
          }

          // Replace prim path prefix
          if (!ReplaceRootPrimPathRec(0, reference.prim_path, dst_prim_path, *const_cast<PrimSpec *>(src_ps), warn, err)) {
            return false;
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
        PUSH_ERROR_AND_RETURN("`delete` references are not supported yet.");
      } else if (qual == ListEditQual::Add) {
        PUSH_ERROR_AND_RETURN("`add` references are not supported yet.");
      } else if (qual == ListEditQual::Order) {
        PUSH_ERROR_AND_RETURN("`order` references are not supported yet.");
      } else if (qual == ListEditQual::Invalid) {
        PUSH_ERROR_AND_RETURN("Invalid listedit qualifier to for `references`.");
      } else if (qual == ListEditQual::Append) {
        for (const auto &reference : refecences) {
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
            if (!LoadAsset(resolver, cwp, search_paths, options.fileformats,
                           reference.asset_path, reference.prim_path, &layer,
                           &src_ps, /* error_when_no_prims */ true,
                           options.error_when_asset_not_found,
                           options.error_when_unsupported_fileformat, warn, err)) {
              PUSH_ERROR_AND_RETURN(
                  fmt::format("Failed to `references` asset `{}`",
                              reference.asset_path.GetAssetPath()));
            }
          }

          if (!src_ps) {
            // LoadAsset allowed not-found or unsupported file. so do nothing.
            continue;
          }

          // Replace prim path prefix
          if (!ReplaceRootPrimPathRec(0, reference.prim_path, dst_prim_path, *const_cast<PrimSpec *>(src_ps), warn, err)) {
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
                         const PayloadCompositionOptions &options) {
  if (depth > options.max_depth) {
    PUSH_ERROR_AND_RETURN("Too deep.");
  }

  // Traverse children first.
  for (auto &child : primspec.children()) {
    const Path parent_prim_path = dst_prim_path.AppendPrim(child.name());
    if (!CompositePayloadRec(depth + 1, resolver, asset_search_paths, parent_prim_path, in_layer, child,
                             warn, err, options)) {
      return false;
    }
  }

  // Use PrimSpec's AssetResolution state.
  std::string cwp = primspec.get_current_working_path();
  std::vector<std::string> search_paths = primspec.get_asset_search_paths();

  if (primspec.metas().payload) {
    // Process all listops in order (supports multiple listops per arc)
    for (const auto &payload_op : primspec.metas().payload.value()) {
      const ListEditQual &qual = payload_op.first;
      const auto &payloads = payload_op.second;

      if ((qual == ListEditQual::ResetToExplicit) ||
          (qual == ListEditQual::Prepend)) {
        for (const auto &pl : payloads) {
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

            if (!LoadAsset(resolver, cwp, search_paths, options.fileformats,
                           pl.asset_path, pl.prim_path, &layer, &src_ps,
                           /* error_when_no_prims_found */ true,
                           options.error_when_asset_not_found,
                           options.error_when_unsupported_fileformat, warn, err)) {
              PUSH_ERROR_AND_RETURN(fmt::format("Failed to `references` asset `{}`",
                                                pl.asset_path.GetAssetPath()));
            }
          }

          if (!src_ps) {
            // LoadAsset allowed not-found or unsupported file. so do nothing.
            continue;
          }

          // Replace prim path prefix
          if (!ReplaceRootPrimPathRec(0, pl.prim_path, dst_prim_path, *const_cast<PrimSpec *>(src_ps), warn, err)) {
            return false;
          }

          // `inherits` op
          if (!InheritPrimSpec(primspec, *src_ps, warn, err)) {
            PUSH_ERROR_AND_RETURN(
                fmt::format("Failed to reference layer `{}`", asset_path));
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
        PUSH_ERROR_AND_RETURN("`delete` references are not supported yet.");
      } else if (qual == ListEditQual::Add) {
        PUSH_ERROR_AND_RETURN("`add` references are not supported yet.");
      } else if (qual == ListEditQual::Order) {
        PUSH_ERROR_AND_RETURN("`order` references are not supported yet.");
      } else if (qual == ListEditQual::Invalid) {
        PUSH_ERROR_AND_RETURN("Invalid listedit qualifier to for `references`.");
      } else if (qual == ListEditQual::Append) {
        for (const auto &pl : payloads) {
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

            if (!LoadAsset(resolver, cwp, search_paths, options.fileformats,
                           pl.asset_path, pl.prim_path, &layer, &src_ps,
                           /* error_when_no_prims_found */ true,
                           options.error_when_asset_not_found,
                           options.error_when_unsupported_fileformat, warn, err)) {
              PUSH_ERROR_AND_RETURN(fmt::format("Failed to `references` asset `{}`",
                                                pl.asset_path.GetAssetPath()));
            }
          }

          if (!src_ps) {
            // LoadAsset allowed not-found or unsupported file. so do nothing.
            continue;
          }

          // Replace prim path prefix
          if (!ReplaceRootPrimPathRec(0, pl.prim_path, dst_prim_path, *const_cast<PrimSpec *>(src_ps), warn, err)) {
            return false;
          }

          // `over` op
          if (!OverridePrimSpec(primspec, *src_ps, warn, err)) {
            PUSH_ERROR_AND_RETURN(
                fmt::format("Failed to reference layer `{}`", asset_path));
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

bool CompositeInheritsRec(uint32_t depth, const Layer &layer,
                          PrimSpec &primspec /* [inout] */, std::string *warn,
                          std::string *err) {
  if (depth > (1024 * 1024)) {
    PUSH_ERROR_AND_RETURN("Too deep.");
  }

  // Traverse children first.
  for (auto &child : primspec.children()) {
    if (!CompositeInheritsRec(depth + 1, layer, child, warn, err)) {
      return false;
    }
  }

  if (primspec.metas().inherits) {
    // Process all listops in order (supports multiple listops per arc)
    for (const auto &inherit_op : primspec.metas().inherits.value()) {
      const auto &qual = inherit_op.first;
      const auto &inherits = inherit_op.second;

      if (inherits.size() == 0) {
        // no-op, continue to next listop
        continue;
      }

      if (inherits.size() != 1) {
        if (err) {
          (*err) += "Multiple inheritance is not supporetd.\n";
        }
        return false;
      }

      const Path &inheritPath = inherits[0];

      const PrimSpec *inheritPrimSpec{nullptr};

      if (!layer.find_primspec_at(inheritPath, &inheritPrimSpec, err)) {
        if (err) {
          (*err) += "Inheirt primspec failed since Path <" +
                    inheritPath.prim_part() + "> not found or is invalid.\n";
        }

        return false;
      }

      // TODO: listEdit
      DCOUT("TODO: listEdit in `inherits`");
      (void)qual;

      if (inheritPrimSpec) {
        if (!InheritPrimSpec(primspec, *inheritPrimSpec, warn, err)) {
          return false;
        }
      } else {
        // ???
        if (err) {
          (*err) +=
              "Inernal error. PrimSpec is nullptr in CompositeInehritsRec.\n";
        }
        return false;
      }
    }

    // remove `inherits` metadataum after processing all listops.
    primspec.metas().inherits.reset();
  }

  return true;
}

bool ExtractReferencesAssetPathsImpl(uint32_t depth, const PrimSpec &primspec, std::vector<std::string> &paths) {

  if (depth > 1024*1024) {
    return false;
  }

  // Traverse children first.
  for (auto &child : primspec.children()) {
    if (!ExtractReferencesAssetPathsImpl(depth + 1, child, paths)) {
      return false;
    }
  }

  if (primspec.metas().references) {
    // Iterate over all listops (supports multiple listops per arc)
    for (const auto &ref_op : primspec.metas().references.value()) {
      // TODO: qualifier
      //const ListEditQual &qual = ref_op.first;
      const auto &refecences = ref_op.second;

      for (const auto &reference : refecences) {
        paths.push_back(reference.asset_path.GetAssetPath());
      }
    }
  }

  return true;

}

} // namespace


std::vector<std::string> ExtractReferencesAssetPaths(const Layer &layer) {

  std::vector<std::string> paths;

  for (const auto &ps : layer.primspecs()) {
    ExtractReferencesAssetPathsImpl(0, ps.second, paths);
  }

  return paths;

}


bool CompositeReferences(AssetResolutionResolver &resolver,
                         const Layer &in_layer, Layer *composited_layer,
                         std::string *warn, std::string *err,
                         ReferencesCompositionOptions options) {
  if (!composited_layer) {
    return false;
  }

  std::vector<std::string> search_paths = in_layer.get_asset_search_paths();

  Layer dst = in_layer;  // deep copy

  for (auto &item : dst.primspecs()) {
    Path primPath("/" + item.first, "");
    if (!CompositeReferencesRec(/* depth */ 0, resolver, search_paths, primPath, in_layer,
                                item.second, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Composite `references` failed.");
    }
  }

  (*composited_layer) = dst;

  DCOUT("Composite `references` ok.");
  return true;
}

namespace {

bool ExtractPayloadAssetPathsImpl(uint32_t depth, const PrimSpec &primspec, std::vector<std::string> &paths) {

  if (depth > 1024*1024) {
    return false;
  }

  // Traverse children first.
  for (auto &child : primspec.children()) {
    if (!ExtractPayloadAssetPathsImpl(depth + 1, child, paths)) {
      return false;
    }
  }

  if (primspec.metas().payload) {
    // Iterate over all listops (supports multiple listops per arc)
    for (const auto &payload_op : primspec.metas().payload.value()) {
      // TODO: qualifier
      //const ListEditQual &qual = payload_op.first;
      const auto &payload = payload_op.second;

      for (const auto &pl : payload) {
        paths.push_back(pl.asset_path.GetAssetPath());
      }
    }
  }

  return true;

}

} // namespace


std::vector<std::string> ExtractPayloadAssetPaths(const Layer &layer) {

  std::vector<std::string> paths;

  for (const auto &ps : layer.primspecs()) {
    ExtractPayloadAssetPathsImpl(0, ps.second, paths);
  }

  return paths;

}


bool CompositePayload(AssetResolutionResolver &resolver, const Layer &in_layer,
                      Layer *composited_layer, std::string *warn,
                      std::string *err, PayloadCompositionOptions options) {
  if (!composited_layer) {
    return false;
  }

  Layer dst = in_layer;  // deep copy

  for (auto &item : dst.primspecs()) {
    Path primPath("/" + item.first, "");
    if (!CompositePayloadRec(/* depth */ 0, resolver,
                             item.second.get_asset_search_paths(), primPath, in_layer, item.second,
                             warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Composite `payload` failed.");
    }
  }

  (*composited_layer) = dst;

  DCOUT("Composite `payload` ok.");
  return true;
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

  for (auto &item : dst.primspecs()) {
    if (!CompositeInheritsRec(/* depth */ 0, dst, item.second, warn, err)) {
      PUSH_ERROR_AND_RETURN("Composite `inherits` failed.");
    }
  }

  (*composited_layer) = dst;

  DCOUT("Composite `inherits` ok.");
  return true;
}

namespace detail {

static nonstd::optional<Prim> ReconstructPrimFromPrimSpec(
    PrimSpec &primspec, std::string *warn, std::string *err) {
  (void)warn;

  // - propertyNames()
  // - primChildrenNames()


#define RECONSTRUCT_PRIM(__primty)                                       \
  if (primspec.typeName() == value::TypeTraits<__primty>::type_name()) { \
    __primty typed_prim;                                                 \
    if (!prim::ReconstructPrim(primspec, &typed_prim, warn, err)) {      \
      PUSH_ERROR("Failed to reconstruct Prim from PrimSpec "             \
                 << primspec.typeName()                                  \
                 << " elementName: " << primspec.name());                \
      return nonstd::nullopt;                                            \
    }                                                                    \
    typed_prim.meta = primspec.metas();                                  \
    typed_prim.name = primspec.name();                                   \
    typed_prim.spec = primspec.specifier();                              \
    /*typed_prim.propertyNames() = properties; */                        \
    /*typed_prim.primChildrenNames() = primChildren;*/                   \
    value::Value primdata = typed_prim;                                  \
    Prim prim(primspec.name(), primdata);                                \
    prim.prim_type_name() = primspec.typeName();                         \
    /* also add primChildren to Prim */                                  \
    /* prim.metas().primChildren = primChildren; */                      \
    return std::move(prim);                                              \
  } else

  if (primspec.typeName().empty() || primspec.typeName() == "Model") {
    // Code is mostly identical to RECONSTRUCT_PRIM.
    // Difference is store primTypeName to Model class itself.
    Model typed_prim;
    if (!prim::ReconstructPrim(primspec, &typed_prim, warn, err)) {
      PUSH_ERROR("Failed to reconstruct Model");
      return nonstd::nullopt;
    }
    typed_prim.meta = primspec.metas();
    typed_prim.name = primspec.name();
    typed_prim.prim_type_name = primspec.typeName();
    typed_prim.spec = primspec.specifier();
    // typed_prim.propertyNames() = properties;
    // typed_prim.primChildrenNames() = primChildren;
    value::Value primdata = typed_prim;
    Prim prim(primspec.name(), primdata);
    prim.prim_type_name() = primspec.typeName();
    /* also add primChildren to Prim */
    // prim.metas().primChildren = primChildren;
    return std::move(prim);
  } else

    RECONSTRUCT_PRIM(Xform)
  RECONSTRUCT_PRIM(Model)
  RECONSTRUCT_PRIM(Scope)
  RECONSTRUCT_PRIM(GeomMesh)
  RECONSTRUCT_PRIM(GeomPoints)
  RECONSTRUCT_PRIM(GeomCylinder)
  RECONSTRUCT_PRIM(GeomCube)
  RECONSTRUCT_PRIM(GeomCone)
  RECONSTRUCT_PRIM(GeomSphere)
  RECONSTRUCT_PRIM(GeomCapsule)
  RECONSTRUCT_PRIM(GeomBasisCurves)
  RECONSTRUCT_PRIM(GeomCamera)
  RECONSTRUCT_PRIM(GeomSubset)
  RECONSTRUCT_PRIM(SphereLight)
  RECONSTRUCT_PRIM(DomeLight)
  RECONSTRUCT_PRIM(CylinderLight)
  RECONSTRUCT_PRIM(DiskLight)
  RECONSTRUCT_PRIM(DistantLight)
  RECONSTRUCT_PRIM(RectLight)
  RECONSTRUCT_PRIM(SkelRoot)
  RECONSTRUCT_PRIM(Skeleton)
  RECONSTRUCT_PRIM(SkelAnimation)
  RECONSTRUCT_PRIM(BlendShape)
  RECONSTRUCT_PRIM(Shader)
  RECONSTRUCT_PRIM(Material) {
    PUSH_WARN("TODO or unsupported prim type: " << primspec.typeName());
    return nonstd::nullopt;
  }

#undef RECONSTRUCT_PRIM
}

static nonstd::optional<Prim> ReconstructPrimFromPrimSpecRec(
    PrimSpec &primspec, std::string *warn, std::string *err,
    uint32_t depth = 0) {

  if (size_t(depth) > kMaxDefaultTraversalLimit) {
    if (err) {
      (*err) += "ReconstructPrimFromPrimSpecRec: recursion too deep.\n";
    }
    return nonstd::nullopt;
  }

  auto pprim = ReconstructPrimFromPrimSpec(primspec, warn, err);

  if (pprim) {
    for (size_t i = 0; i < primspec.children().size(); i++) {
      if (auto pv = ReconstructPrimFromPrimSpecRec(primspec.children()[i], warn, err, depth + 1)) {
        pprim.value().children().emplace_back(std::move(pv.value()));
      }
    }
  }

  return pprim;
}

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

  // Override properties
  for (const auto &prop : src.props()) {
    // replace
    dst.props()[prop.first] = prop.second;
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

  // Keep PrimSpec name, typeName (if not empty) and spec from `dst`
  ps.name() = dst.name();
  if (!dst.typeName().empty()) {
    ps.typeName() = dst.typeName();
  }
  ps.specifier() = dst.specifier();

  // Override metadataum
  ps.metas().update_from(dst.metas());

  // Override properties
  for (const auto &prop : dst.props()) {
    if (ps.props().count(prop.first)) {
      // replace
      ps.props().at(prop.first) = prop.second;
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

bool LayerToStage(Layer &&layer, Stage *stage_out, std::string *warn,
                  std::string *err) {
  if (!stage_out) {
    if (err) {
      (*err) += "`stage_ptr` is nullptr.";
    }
    return false;
  }

  Stage stage;

  stage.metas() = layer.metas();

  // TODO: primChildren metadatum
  for (auto &primspec : layer.primspecs()) {
    if (auto pv =
            detail::ReconstructPrimFromPrimSpecRec(primspec.second, warn, err)) {
      stage.add_root_prim(std::move(pv.value()));
    }
  }

  (*stage_out) = stage;

  return true;
}

namespace detail {

// In-place conversion helper: Move PrimSpec data to Prim and free source memory
static nonstd::optional<Prim> ReconstructPrimFromPrimSpecInPlace(
    std::unique_ptr<PrimSpec> primspec, std::string *warn, std::string *err) {

  nonstd::optional<Prim> prim_opt;

  if (!primspec) {
    if (err) {
      (*err) += "PrimSpec is null";
    }
  } else {

    // First reconstruct the prim normally
    prim_opt = ReconstructPrimFromPrimSpec(*primspec, warn, err);

    if (prim_opt) {

      // Now we can clear the primspec data to free memory
      // The data has been copied to the Prim, so we can safely clear it

      // Clear properties (these can be large)
      primspec->props().clear();

      // Clear metadata
      primspec->metas() = PrimMeta();

      // Clear relationships (if they exist)
      // primspec->relationships().clear();

      // Clear variant sets
      primspec->variantSets().clear();

      // Process children recursively
      for (auto& child : primspec->children()) {
        auto child_ptr = std::make_unique<PrimSpec>(std::move(child));
        if (auto child_prim = ReconstructPrimFromPrimSpecInPlace(std::move(child_ptr), warn, err)) {
          prim_opt.value().children().emplace_back(std::move(child_prim.value()));
        }
      }

      // Clear children vector
      primspec->children().clear();
      primspec->children().shrink_to_fit();
    }
  }

  return prim_opt;
}

} // namespace detail

bool LayerToStageInPlace(std::unique_ptr<Layer> layer, Stage *stage_out,
                         std::string *warn, std::string *err) {
  if (!stage_out) {
    if (err) {
      (*err) += "`stage_out` is nullptr.";
    }
    return false;
  }

  if (!layer) {
    if (err) {
      (*err) += "`layer` is nullptr.";
    }
    return false;
  }

  Stage stage;

  // Move metadata (cheap operation)
  stage.metas() = std::move(layer->metas());

  // Convert primspecs in-place
  // We need to iterate carefully since we're modifying the map
  auto& primspecs = layer->primspecs();
  std::vector<std::string> paths_to_process;

  for (const auto& item : primspecs) {
    paths_to_process.push_back(item.first);
  }

  for (const auto& path : paths_to_process) {
    auto it = primspecs.find(path);
    if (it != primspecs.end()) {
      // Extract the PrimSpec from the map
      auto primspec_ptr = std::make_unique<PrimSpec>(std::move(it->second));

      // Remove from map immediately to free memory
      primspecs.erase(it);

      // Convert to Prim in-place
      if (auto pv = detail::ReconstructPrimFromPrimSpecInPlace(std::move(primspec_ptr), warn, err)) {
        stage.add_root_prim(std::move(pv.value()));
      }
    }
  }

  // Clear the layer completely
  layer->primspecs().clear();
  layer.reset();  // Release the Layer object itself

  (*stage_out) = std::move(stage);

  return true;
}

bool PrimSpecToPrimInPlace(std::unique_ptr<PrimSpec> primspec, Prim *prim_out,
                           std::string *warn, std::string *err) {
  if (!prim_out) {
    if (err) {
      (*err) += "`prim_out` is nullptr.";
    }
    return false;
  }

  if (!primspec) {
    if (err) {
      (*err) += "`primspec` is nullptr.";
    }
    return false;
  }

  auto prim_opt = detail::ReconstructPrimFromPrimSpecInPlace(std::move(primspec), warn, err);

  if (!prim_opt) {
    return false;
  }

  (*prim_out) = std::move(prim_opt.value());

  return true;
}

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

bool ExtractVariantsRec(uint32_t depth, const std::string &root_path,
                        const PrimSpec &ps, Dictionary &dict,
                        const uint32_t max_depth, std::string *err) {
  if (depth > max_depth) {
    if (err) {
      (*err) += "Too deep\n";
    }
    return false;
  }

  Dictionary variantInfos;

  if (ps.name().empty()) {
    if (err) {
      (*err) += "PrimSpec name is empty.\n";
    }
    return false;
  }

  std::string full_prim_path = root_path + "/" + ps.name();

  if (ps.metas().variantSets) {
    // Collect all variant sets from all listops
    std::vector<std::string> vsets;
    for (const auto &variantSets_op : ps.metas().variantSets.value()) {
      const auto &items = variantSets_op.second;
      for (const auto &vs : items) {
        vsets.push_back(vs);
      }
    }
    MetaVariable var;
    var.set_value(vsets);
    variantInfos["variantSets"] = var;
  }

  if (ps.metas().variants) {
    Dictionary values;

    const VariantSelectionMap &vsmap = ps.metas().variants.value();
    for (const auto &item : vsmap) {
      MetaVariable var;
      var.set_value(item.second);

      values[item.first] = item.second;
    }

    variantInfos["variants"] = values;
  }

  if (variantInfos.size()) {
    dict[full_prim_path] = variantInfos;
  }

  // Traverse children
  for (const auto &child : ps.children()) {
    if (!ExtractVariantsRec(depth + 1, full_prim_path, child, dict, max_depth,
                            err)) {
      return false;
    }
  }

  return true;
}

bool ExtractVariantsRec(uint32_t depth, const std::string &root_path,
                        const Prim &prim, Dictionary &dict,
                        const uint32_t max_depth, std::string *err) {
  if (depth > max_depth) {
    if (err) {
      (*err) += "Too deep\n";
    }
    return false;
  }

  Dictionary variantInfos;

  if (prim.element_name().empty()) {
    if (err) {
      (*err) += "Prim name is empty.\n";
    }
    return false;
  }

  std::string full_prim_path = root_path + "/" + prim.element_name();

  if (prim.metas().variantSets) {
    // Collect all variant sets from all listops
    std::vector<std::string> vsets;
    for (const auto &variantSets_op : prim.metas().variantSets.value()) {
      const auto &items = variantSets_op.second;
      for (const auto &vs : items) {
        vsets.push_back(vs);
      }
    }
    MetaVariable var;
    var.set_value(vsets);
    variantInfos["variantSets"] = var;
  }

  if (prim.metas().variants) {
    Dictionary values;

    const VariantSelectionMap &vsmap = prim.metas().variants.value();
    for (const auto &item : vsmap) {
      MetaVariable var;
      var.set_value(item.second);

      values[item.first] = item.second;
    }

    variantInfos["variants"] = values;
  }

  // variantSetChildren Prim metadataum supercedes Prim's variantSets Stmt
  if (prim.metas().variantSetChildren) {
    const std::vector<value::token> &vsets =
        prim.metas().variantSetChildren.value();
    // to string
    std::vector<std::string> vsetchildren;
    for (const auto &item : vsets) {
      if (!item.valid()) {
        if (err) {
          (*err) += "Invalid variantSetChildren token found.\n";
        }
        return false;
      }
      vsetchildren.push_back(item.str());
    }
    variantInfos["variantSet"] = vsetchildren;
  } else if (prim.variantSets().size()) {
    Dictionary vsetdict;

    for (const auto &item : prim.variantSets()) {
      if (item.second.variantSet.size()) {
        std::vector<std::string> variantStmtNames;

        if (item.second.name.empty()) {
          if (err) {
            (*err) += "Invalid variantSets Statements found.\n";
          }
          return false;
        }

        for (const auto &v : item.second.variantSet) {
          variantStmtNames.push_back(v.first);
        }

        vsetdict[item.first] = variantStmtNames;
      }
    }

    if (vsetdict.size()) {
      variantInfos["variantSet"] = vsetdict;
    }
  }

  if (variantInfos.size()) {
    dict[full_prim_path] = variantInfos;
  }

  // Traverse children
  for (const auto &child : prim.children()) {
    if (!ExtractVariantsRec(depth + 1, full_prim_path, child, dict, max_depth,
                            err)) {
      return false;
    }
  }

  return true;
}

}  // namespace

bool ExtractVariants(const Layer &layer, Dictionary *dict, std::string *err) {
  if (!dict) {
    if (err) {
      (*err) += "`dict` argument is nullptr.\n";
    }

    return false;
  }

  for (const auto &primspec : layer.primspecs()) {
    if (!ExtractVariantsRec(/* depth */ 0, /* root path */ "", primspec.second,
                            (*dict), /* max_depth */ 1024 * 1024, err)) {
      return false;
    }
  }

  return true;
}

bool ExtractVariants(const Stage &stage, Dictionary *dict, std::string *err) {
  if (!dict) {
    if (err) {
      (*err) += "`dict` argument is nullptr.\n";
    }

    return false;
  }

  for (const auto &prim : stage.root_prims()) {
    if (!ExtractVariantsRec(/* depth */ 0, /* root path */ "", prim, (*dict),
                            /* max_depth */ 1024 * 1024, err)) {
      return false;
    }
  }

  return true;
}

bool VariantSelectPrimSpec(
    PrimSpec &dst, const PrimSpec &src,
    const std::map<std::string, std::string> &variant_selection,
    std::string *warn, std::string *err) {
  if (src.metas().variants && src.metas().variantSets) {
    // do variant compsotion
  } else if (src.metas().variants) {
    if (warn) {
      (*warn) +=
          "`variants` are authored, but `variantSets` is not authored.\n";
    }
    dst = src;
    dst.metas().variants.reset();
    dst.metas().variantSets.reset();
    dst.variantSets().clear();
    return true;
  } else if (src.metas().variantSets) {
    if (warn) {
      (*warn) +=
          "`variantSets` are authored, but `variants` is not authored.\n";
    }
    dst = src;
    dst.metas().variants.reset();
    dst.metas().variantSets.reset();
    dst.variantSets().clear();
    // nothing to do.
    return true;
  } else {
    dst = src;
    return true;
  }

  // Collect all variant set names from all listops
  std::vector<std::string> allVariantSetNames;
  for (const auto &variantSets_op : src.metas().variantSets.value()) {
    // TODO: handle different list edit qualifiers appropriately
    const auto &items = variantSets_op.second;
    for (const auto &vs : items) {
      allVariantSetNames.push_back(vs);
    }
  }

  dst = src;

  PrimSpec ps = src;  // temp PrimSpec. Init with src.

  // Evaluate from the last element.
  for (int64_t i = int64_t(allVariantSetNames.size()) - 1; i >= 0; i--) {
    const auto &variantSetName = allVariantSetNames[size_t(i)];

    // 1. look into `variant_selection`.
    // 2. look into variant setting in this PrimSpec.

    std::string variantName;
    if (variant_selection.count(variantSetName)) {
      variantName = variant_selection.at(variantSetName);
    } else if (dst.current_variant_selection(variantSetName, &variantName)) {
      // ok
    } else {
      continue;
    }

    if (dst.variantSets().count(variantSetName)) {
      const auto &vss = dst.variantSets().at(variantSetName);

      if (vss.variantSet.count(variantName)) {
        const PrimSpec &vs = vss.variantSet.at(variantName);

        DCOUT(fmt::format("variantSet[{}] Select variant: {}", variantSetName,
                          variantName));

        //
        // Promote variant content to PrimSpec.
        //

        // over-like operation
        ps.metas().update_from(vs.metas(), /* override_authored */ true);

        for (const auto &prop : vs.props()) {
          DCOUT("prop: " << prop.first);
          // override existing prop
          ps.props()[prop.first] = prop.second;
        }

        for (const auto &child : vs.children()) {
          // Override if PrimSpec has same name
          // simple linear scan.
          auto it = std::find_if(ps.children().begin(), ps.children().end(),
                                 [&child](const PrimSpec &item) {
                                   return (item.name() == child.name());
                                 });

          if (it != ps.children().end()) {
            (*it) = child;  // replace
          } else {
            ps.children().push_back(child);
          }
        }

        // - [ ] update `primChildren` and `properties` metadataum if required.
      }
    }
  }

  DCOUT("Variant resolved prim: " << prim::print_primspec(ps));

  // Local properties/metadatum wins against properties/metadataum from Variant
  ps.specifier() = Specifier::Over;
  if (!OverridePrimSpec(dst, ps, warn, err)) {
    PUSH_ERROR_AND_RETURN("Failed to override PrimSpec.");
  }

  dst.metas().variants.reset();
  dst.metas().variantSets.reset();
  dst.variantSets().clear();

  return true;
}

}  // namespace tinyusdz
