// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment, Inc.

#include "composition.hh"

#include <set>
#include <stack>

#include "asset-resolution.hh"
#include "common-macros.inc"
#include "io-util.hh"
#include "pprinter.hh"
#include "prim-reconstruct.hh"
#include "prim-types.hh"
#include "prim-pprint.hh"
#include "tiny-format.hh"
#include "usdGeom.hh"
#include "usdLux.hh"
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
  bool ReconstructPrim<__ty>(const PrimSpec &, __ty *, std::string *, \
                             std::string *)

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
RECONSTRUCT_PRIM_DECL(SphereLight);
RECONSTRUCT_PRIM_DECL(DomeLight);
RECONSTRUCT_PRIM_DECL(DiskLight);
RECONSTRUCT_PRIM_DECL(DistantLight);
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

bool CompositeSublayersRec(const AssetResolutionResolver &resolver,
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

  for (const auto &layer : in_layer.metas().subLayers) {
    // TODO: subLayerOffset
    std::string sublayer_asset_path = layer.assetPath.GetAssetPath();
    DCOUT("Load subLayer " << sublayer_asset_path);

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

    std::vector<uint8_t> sublayer_data;
    if (!tinyusdz::io::ReadWholeFile(&sublayer_data, err, layer_filepath,
                                     /* filesize_max */ 0)) {
      PUSH_ERROR_AND_RETURN("Failed to read file: " << layer_filepath);
    }

    tinyusdz::StreamReader ssr(sublayer_data.data(), sublayer_data.size(),
                               /* swap endian */ false);
    tinyusdz::usda::USDAReader sublayer_reader(&ssr);

#if 0  // not used
    // Use the first path as base_dir.
    // TODO: Use AssetResolutionResolver in USDAReder.
    //std::string base_dir;
    //if (resolver.search_paths().size()) {
    //  base_dir = resolver.search_paths()[0];
    //}
    //sublayer_reader.SetBaseDir(base_dir);
#endif

    uint32_t sublayer_load_states =
        static_cast<uint32_t>(tinyusdz::LoadState::Sublayer);

    tinyusdz::Layer sublayer;
    {
      // TODO: ReaderConfig.
      bool ret =
          sublayer_reader.read(sublayer_load_states, /* as_primspec */ true);

      if (!ret) {
        PUSH_ERROR_AND_RETURN("Failed to parse : "
                              << layer_filepath << sublayer_reader.GetError());
      }

      ret = sublayer_reader.get_as_layer(&sublayer);
      if (!ret) {
        PUSH_ERROR_AND_RETURN("Failed to get " << layer_filepath
                                               << " as subLayer");
      }

      // std::cout << sublayer << "\n";
    }

    curr_layer_names.insert(sublayer_asset_path);

    Layer composited_sublayer;

    // Recursively load subLayer
    if (!CompositeSublayersRec(resolver, sublayer, layer_names_stack,
                               &composited_sublayer, warn, err, options)) {
      return false;
    }

    {
      // 1/2. merge sublayer's sublayers

      // NOTE: `over` specifier is ignored when merging Prims among different
      // subLayers
      for (auto &prim : composited_sublayer.primspecs()) {
        if (composited_layer->has_primspec(prim.first)) {
          // Skip
        } else {
          if (!composited_layer->emplace_primspec(prim.first,
                                                  std::move(prim.second))) {
            PUSH_ERROR_AND_RETURN(
                fmt::format("Compositing PrimSpec {} in {} failed.", prim.first,
                            layer_filepath));
          }
          DCOUT("add primspec: " << prim.first);
        }
      }

      // 2/2. merge sublayer
      for (auto &prim : sublayer.primspecs()) {
        if (composited_layer->has_primspec(prim.first)) {
          // Skip
        } else {
          if (!composited_layer->emplace_primspec(prim.first,
                                                  std::move(prim.second))) {
            PUSH_ERROR_AND_RETURN(
                fmt::format("Compositing PrimSpec {} in {} failed.", prim.first,
                            layer_filepath));
          }
          DCOUT("add primspec: " << prim.first);
        }
      }
    }
  }

  layer_names_stack.pop_back();

  return true;
}

}  // namespace

#if 0 // TODO: remove
bool CompositeSublayers(const std::string &base_dir, const Layer &in_layer,
                        Layer *composited_layer, std::string *warn,
                        std::string *err, SublayersCompositionOptions options) {
  tinyusdz::AssetResolutionResolver resolver;
  resolver.set_search_paths({base_dir});

  return CompositeSublayers(resolver, in_layer, composited_layer, warn, err,
                            options);
}
#endif

bool CompositeSublayers(const AssetResolutionResolver &resolver,
                        const Layer &in_layer, Layer *composited_layer,
                        std::string *warn, std::string *err,
                        SublayersCompositionOptions options) {
  if (!composited_layer) {
    return false;
  }

  std::vector<std::set<std::string>> layer_names_stack;

  DCOUT("Resolve subLayers..");
  if (!CompositeSublayersRec(resolver, in_layer, layer_names_stack,
                             composited_layer, warn, err, options)) {
    PUSH_ERROR_AND_RETURN("Composite subLayers failed.");
  }

  // merge Prims in root layer.
  // NOTE: local Prims(prims in root layer) wins against subLayer's Prim
  DCOUT("in_layer # of primspecs: " << in_layer.primspecs().size());
  for (auto &prim : in_layer.primspecs()) {
    DCOUT("in_layer.prim: " << prim.first);
    if (composited_layer->has_primspec(prim.first)) {
      // over
      if (prim.second.specifier() == Specifier::Class) {
        // TODO: Simply ignore?
        DCOUT("TODO: `class` Prim");
      } else if (prim.second.specifier() == Specifier::Over) {
        // TODO `over` compisition
        DCOUT("TODO: `over` Prim");
      } else if (prim.second.specifier() == Specifier::Def) {
        DCOUT("overewrite prim: " << prim.first);
        // overwrite
        if (!composited_layer->replace_primspec(prim.first, prim.second)) {
          PUSH_ERROR_AND_RETURN(
              fmt::format("Failed to replace PrimSpec: {}", prim.first));
        }
      } else {
        /// ???
        PUSH_ERROR_AND_RETURN(fmt::format("Prim {} has invalid Prim specifier.",
                                          prim.second.name()));
      }
    } else {
      if (!composited_layer->add_primspec(prim.first, prim.second)) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Compositing PrimSpec {} in {} failed.", prim.first,
                        in_layer.name()));
      }
      DCOUT("added primspec: " << prim.first);
    }
  }

  composited_layer->metas() = in_layer.metas();
  // Remove subLayers metadatum
  composited_layer->metas().subLayers.clear();

  DCOUT("Composite subLayers ok.");
  return true;
}

namespace {

bool CompositeReferencesRec(uint32_t depth,
                            const AssetResolutionResolver &resolver,
                            PrimSpec &primspec /* [inout] */, std::string *warn,
                            std::string *err,
                            ReferencesCompositionOptions options) {
  if (depth > options.max_depth) {
    PUSH_ERROR_AND_RETURN("Too deep.");
  }

  // Traverse children first.
  for (auto &child : primspec.children()) {
    if (!CompositeReferencesRec(depth + 1, resolver, child, warn, err,
                                options)) {
    }
  }

  if (primspec.metas().references) {
    const ListEditQual &qual = primspec.metas().references.value().first;
    const auto &refecences = primspec.metas().references.value().second;

    if ((qual == ListEditQual::ResetToExplicit) ||
        (qual == ListEditQual::Prepend)) {
      for (const auto &reference : refecences) {
        std::string asset_path = reference.asset_path.GetAssetPath();

        if (asset_path.empty()) {
          PUSH_ERROR_AND_RETURN(
              "TODO: Prim path(e.g. </xform>) in references.");
        }

        Layer layer;
        std::string _warn;
        std::string _err;

        // resolve path
        // TODO: Store resolved path to Reference?
        std::string resolved_path = resolver.resolve(asset_path);

        DCOUT("Loading references: " << resolved_path << ", asset_path: " << asset_path);
        if (!LoadLayerFromFile(resolved_path, &layer, &_warn, &_err)) {
          PUSH_ERROR_AND_RETURN(fmt::format("Failed to open `{}` as Layer: {}",
                                            asset_path, _err));
        }

        DCOUT("layer = " << print_layer(layer, 0));

        // TODO: Recursively resolve `references`

        if (_warn.size()) {
          if (warn) {
            (*warn) += _warn;
          }
        }

        if (layer.primspecs().empty()) {
          PUSH_ERROR_AND_RETURN(fmt::format("No prims in `{}`", asset_path));
        }

        std::string default_prim;
        if (reference.prim_path.is_valid()) {
          default_prim = reference.prim_path.prim_part();
        } else {
          // Use `defaultPrim` metadatum
          if (layer.metas().defaultPrim.valid()) {
            default_prim = "/" + layer.metas().defaultPrim.str();
          } else {
            // Use the first Prim in the layer.
            default_prim = "/" + layer.primspecs().begin()->first;
          }
        }

        const PrimSpec *src_ps{nullptr};
        if (!layer.find_primspec_at(Path(default_prim, ""), &src_ps, err)) {
          PUSH_ERROR_AND_RETURN(fmt::format("Failed to find PrimSpec `{}` in layer `{}`", default_prim, asset_path));
        }

        if (!src_ps) {
          PUSH_ERROR_AND_RETURN("Internal error: PrimSpec pointer is nullptr.");
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
      for (const auto &reference : refecences) {
        std::string asset_path = reference.asset_path.GetAssetPath();

        if (asset_path.empty()) {
          PUSH_ERROR_AND_RETURN(
              "TODO: Prim path(e.g. </xform>) in references.");
        }

        Layer layer;
        std::string _warn;
        std::string _err;

        // resolve path
        // TODO: Store resolved path to Reference?
        std::string resolved_path = resolver.resolve(asset_path);

        DCOUT("Loading references: " << resolved_path << ", asset_path " << asset_path);
        if (!LoadLayerFromFile(asset_path, &layer, &_warn, &_err)) {
          PUSH_ERROR_AND_RETURN(fmt::format("Failed to open `{}` as Layer: {}",
                                            asset_path, _err));
        }

        if (_warn.size()) {
          if (warn) {
            (*warn) += _warn;
          }
        }

        std::string default_prim;
        if (reference.prim_path.is_valid()) {
          default_prim = reference.prim_path.prim_part();
        } else {
          // Use `defaultPrim` metadatum
          if (layer.metas().defaultPrim.valid()) {
            default_prim = "/" + layer.metas().defaultPrim.str();
          } else {
            // Use the first Prim in the layer.
            default_prim = "/" + layer.primspecs().begin()->first;
          }
        }

        const PrimSpec *src_ps{nullptr};
        if (!layer.find_primspec_at(Path(default_prim, ""), &src_ps, err)) {
          PUSH_ERROR_AND_RETURN(fmt::format("Failed to find PrimSpec `{}` in layer `{}`", default_prim, asset_path));
        }

        if (!src_ps) {
          PUSH_ERROR_AND_RETURN("Internal error: PrimSpec pointer is nullptr.");
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

    // Remove `references`.
    primspec.metas().references = nonstd::nullopt;
  }

  return true;
}

bool CompositePayloadRec(uint32_t depth,
                            const AssetResolutionResolver &resolver,
                            PrimSpec &primspec /* [inout] */, std::string *warn,
                            std::string *err,
                            PayloadCompositionOptions options) {
  if (depth > options.max_depth) {
    PUSH_ERROR_AND_RETURN("Too deep.");
  }

  // Traverse children first.
  for (auto &child : primspec.children()) {
    if (!CompositePayloadRec(depth + 1, resolver, child, warn, err,
                                options)) {
    }
  }

  if (primspec.metas().payload) {
    const ListEditQual &qual = primspec.metas().payload.value().first;
    const auto &payloads = primspec.metas().payload.value().second;

    if ((qual == ListEditQual::ResetToExplicit) ||
        (qual == ListEditQual::Prepend)) {
      for (const auto &pl : payloads) {
        std::string asset_path = pl.asset_path.GetAssetPath();

        if (asset_path.empty()) {
          PUSH_ERROR_AND_RETURN(
              "TODO: Prim path(e.g. </xform>) in references.");
        }

        Layer layer;
        std::string _warn;
        std::string _err;

        // resolve path
        // TODO: Store resolved path to Reference?
        std::string resolved_path = resolver.resolve(asset_path);

        DCOUT("Loading payload: " << resolved_path << ", asset_path: " << asset_path);
        if (!LoadLayerFromFile(resolved_path, &layer, &_warn, &_err)) {
          PUSH_ERROR_AND_RETURN(fmt::format("Failed to open `{}` as Layer: {}",
                                            asset_path, _err));
        }

        DCOUT("layer = " << print_layer(layer, 0));

        // TODO: Recursively resolve `payload`

        if (_warn.size()) {
          if (warn) {
            (*warn) += _warn;
          }
        }

        if (layer.primspecs().empty()) {
          PUSH_ERROR_AND_RETURN(fmt::format("No prims in `{}`", asset_path));
        }

        std::string default_prim;
        if (pl.prim_path.is_valid()) {
          default_prim = pl.prim_path.prim_part();
        } else {
          // Use `defaultPrim` metadatum
          if (layer.metas().defaultPrim.valid()) {
            default_prim = "/" + layer.metas().defaultPrim.str();
          } else {
            // Use the first Prim in the layer.
            default_prim = "/" + layer.primspecs().begin()->first;
          }
        }

        const PrimSpec *src_ps{nullptr};
        if (!layer.find_primspec_at(Path(default_prim, ""), &src_ps, err)) {
          PUSH_ERROR_AND_RETURN(fmt::format("Failed to find PrimSpec `{}` in layer `{}`", default_prim, asset_path));
        }

        if (!src_ps) {
          PUSH_ERROR_AND_RETURN("Internal error: PrimSpec pointer is nullptr.");
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

        if (asset_path.empty()) {
          PUSH_ERROR_AND_RETURN(
              "TODO: Prim path(e.g. </xform>) in references.");
        }

        Layer layer;
        std::string _warn;
        std::string _err;

        // resolve path
        // TODO: Store resolved path to Reference?
        std::string resolved_path = resolver.resolve(asset_path);

        DCOUT("Loading payload: " << resolved_path << ", asset_path " << asset_path);
        if (!LoadLayerFromFile(asset_path, &layer, &_warn, &_err)) {
          PUSH_ERROR_AND_RETURN(fmt::format("Failed to open `{}` as Layer: {}",
                                            asset_path, _err));
        }

        if (_warn.size()) {
          if (warn) {
            (*warn) += _warn;
          }
        }

        std::string default_prim;
        if (pl.prim_path.is_valid()) {
          default_prim = pl.prim_path.prim_part();
        } else {
          // Use `defaultPrim` metadatum
          if (layer.metas().defaultPrim.valid()) {
            default_prim = "/" + layer.metas().defaultPrim.str();
          } else {
            // Use the first Prim in the layer.
            default_prim = "/" + layer.primspecs().begin()->first;
          }
        }

        const PrimSpec *src_ps{nullptr};
        if (!layer.find_primspec_at(Path(default_prim, ""), &src_ps, err)) {
          PUSH_ERROR_AND_RETURN(fmt::format("Failed to find PrimSpec `{}` in layer `{}`", default_prim, asset_path));
        }

        if (!src_ps) {
          PUSH_ERROR_AND_RETURN("Internal error: PrimSpec pointer is nullptr.");
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

    // Remove `payload`.
    primspec.metas().payload = nonstd::nullopt;
  }

  return true;
}

}  // namespace

bool CompositeReferences(const AssetResolutionResolver &resolver,
                         const Layer &in_layer, Layer *composited_layer,
                         std::string *warn, std::string *err,
                         ReferencesCompositionOptions options) {
  if (!composited_layer) {
    return false;
  }

  Layer dst = in_layer; // deep copy

  for (auto &item : dst.primspecs()) {

    if (!CompositeReferencesRec(/* depth */0, resolver, item.second, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Composite `references` failed.");
    }
  }

  (*composited_layer) = dst;

  DCOUT("Composite `references` ok.");
  return true;
}

bool CompositePayload(const AssetResolutionResolver &resolver,
                         const Layer &in_layer, Layer *composited_layer,
                         std::string *warn, std::string *err,
                         PayloadCompositionOptions options) {
  if (!composited_layer) {
    return false;
  }

  Layer dst = in_layer; // deep copy

  for (auto &item : dst.primspecs()) {

    if (!CompositePayloadRec(/* depth */0, resolver, item.second, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Composite `payload` failed.");
    }
  }

  (*composited_layer) = dst;

  DCOUT("Composite `payload` ok.");
  return true;
}

namespace detail {

static nonstd::optional<Prim> ReconstructPrimFromPrimSpec(
    const PrimSpec &primspec, std::string *warn, std::string *err) {
  (void)warn;

  // TODO:
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

  if (primspec.typeName() == "Model") {
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
  // RECONSTRUCT_PRIM(GeomSubset)
  RECONSTRUCT_PRIM(SphereLight)
  RECONSTRUCT_PRIM(DomeLight)
  RECONSTRUCT_PRIM(CylinderLight)
  RECONSTRUCT_PRIM(DiskLight)
  RECONSTRUCT_PRIM(DistantLight)
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
    if (dst.props().count(prop.first)) {
      // replace
      dst.props().at(prop.first) = prop.second;
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
  
  // keep PrimSpec name
  ps.name() = dst.name();

  // Override metadataum
  ps.metas().update_from(dst.metas());

  // Override properties
  for (const auto &prop : dst.props()) {
    if (ps.props().count(prop.first)) {
      // replace
      ps.props().at(prop.first) = prop.second;
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

bool LayerToStage(const Layer &layer, Stage *stage_out, std::string *warn,
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
  for (const auto &primspec : layer.primspecs()) {
    if (auto pv =
            detail::ReconstructPrimFromPrimSpec(primspec.second, warn, err)) {
      // TODO
      (void)pv;
    }
  }

  (*stage_out) = stage;

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

bool ReferenceLayerToPrimSpec(PrimSpec &dst, const Layer &layer,
                              const Path primPath,
                              const LayerOffset layerOffset) {
  if (layer.primspecs().empty()) {
    // nothing to do
    return true;
  }

  std::string src_root_prim_name = "";
  if (!primPath.is_valid()) {
    // Use the defaultPrim
    if (!layer.metas().defaultPrim.str().empty()) {
      src_root_prim_name = layer.metas().defaultPrim.str();
    } else {
      // Use the first Prim.
      src_root_prim_name = (layer.primspecs().begin())->first;
    }
  } else {
    src_root_prim_name = primPath.prim_part();
  }

  DCOUT("TODO");
  (void)dst;
  (void)layerOffset;

  return false;
}

}  // namespace tinyusdz
