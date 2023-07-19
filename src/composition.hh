// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment Inc.
//
// Layer and Prim composition features.
//
#pragma once

#include "asset-resolution.hh"
#include "prim-types.hh"

// TODO
// - [ ] Compose `references`
// - [ ] Compose `payloads`
// - [ ] Compose `specializes`
// - [ ] Compose `inherits`
// - [ ] Compose `variantSets`
// - [ ] Compose `over`

namespace tinyusdz {

// Forward decl.
class Stage;

// USD asset loading state.
enum class LoadState : uint32_t {
  Toplevel = 1,        // load initial .usd(default)
  Sublayer = 1 << 1,   // load USD from Stage meta sublayer
  Reference = 1 << 2,  // load USD from Prim meta reference
  Payload = 1 << 3     // load USD from Prim meta payload
};

struct SublayersCompositionOptions {
  // The maximum depth for nested `subLayers`
  uint32_t max_depth = 1024u;
};

struct ReferencesCompositionOptions {
  // The maximum depth for nested `references`
  uint32_t max_depth = 1024u;
};

struct PayloadCompositionOptions {
  // The maximum depth for nested `payload`
  uint32_t max_depth = 1024u;
};

#if 0 // deprecate it.
///
/// Load subLayer USD files in `layer`, and return composited(flattened) Layer
/// to `composited_layer` Supply search_path with `base_dir`
///
bool CompositeSublayers(
    const std::string &base_dir, const Layer &layer, Layer *composited_layer,
    std::string *warn, std::string *err,
    const SublayersCompositionOptions options = SublayersCompositionOptions());
#endif

///
/// Load subLayer USD files in `layer`, and return composited(flattened) Layer
/// to `composited_layer` Supply AssetResolutionResolver
///
bool CompositeSublayers(
    const AssetResolutionResolver &resolver, const Layer &layer,
    Layer *composited_layer, std::string *warn, std::string *err,
    const SublayersCompositionOptions options = SublayersCompositionOptions());

///
/// Resolve `references` for each PrimSpe, and return composited(flattened)
/// Layer to `composited_layer` in `layer`.
///
bool CompositeReferences(const AssetResolutionResolver &resolver,
                         const Layer &layer, Layer *composited_layer,
                         std::string *warn, std::string *err,
                         const ReferencesCompositionOptions options =
                             ReferencesCompositionOptions());

///
/// Resolve `payload` for each PrimSpe, and return composited(flattened) Layer
/// to `composited_layer` in `layer`.
///
bool CompositePayload(
    const AssetResolutionResolver &resolver, const Layer &layer,
    Layer *composited_layer, std::string *warn, std::string *err,
    const PayloadCompositionOptions options = PayloadCompositionOptions());

///
/// Override a PrimSpec with another PrimSpec.
///
/// @param[inout] dst PrimSpec to be override(must be `def` or `class` spec)
/// @param[in] src PrimSpec for override(must be `over` spec)
///
/// @return true upon success. false when error.
///
bool OverridePrimSpec(PrimSpec &dst, const PrimSpec &src, std::string *warn,
                      std::string *err);

///
/// Inherit PrimSpec. All PrimSpec tree in `src` PrimSpec will be inheritated to
/// `dst` PrimSpec.
///
/// @param[inout] dst PrimSpec to be inheritated
/// @param[in] src Source PrimSpec. Source PrimSpec can be any specifier(i.e,
/// `class`, `def` or `over`), but `class` recommended.
///
/// @return true upon success. false when error.
///
bool InheritPrimSpec(PrimSpec &dst, const PrimSpec &src, std::string *warn,
                     std::string *err);

///
/// Build USD Stage from Layer
///
bool LayerToStage(const Layer &layer, Stage *stage, std::string *warn,
                  std::string *err);

///
/// Build USD Stage from Layer
///
/// `layer` object will be destroyed after `stage` is being build.
///
bool LayerToStage(Layer &&layer, Stage *stage, std::string *warn,
                  std::string *err);

struct VariantSelector {
  std::string selection;  // current selection
  VariantSelectionMap vsmap;
};

using VariantSelectorMap = std::map<Path, VariantSelector>;

///
/// Recursively traverse PrimSpec tree and collect variantSelection information.
///
/// key : PrimSpec path(e.g. "/root/xform0")
/// value : VariantSelectionInfo
///
/// TODO: Move to Tydra API?
///
bool ListVariantSelectionMaps(const Layer &layer, VariantSelectorMap &m);

///
/// Select variant(PrimSpec subtree) `variant_name` from `src` PrimSpec and
/// write it to `dst` PrimSpec.
///
/// @param[inout] dst PrimSpec where selected variant are written.
/// @param[in] src Source PrimSpec. Source PrimSpec.
///
/// @return true upon success. false when error(e.g. no corresponding
/// `variant_name` exists in `src` PrimSpec).
///
bool VariantSelectPrimSpec(PrimSpec &dst, const PrimSpec &src,
                           const std::string &variant_name, std::string *warn,
                           std::string *err);

///
/// Resolve variant in PrimSpec tree and write result to `dst`.
/// `dst` does not contain any variant info.
///
bool ApplyVariantSelector(const Layer &layer, const VariantSelectorMap &vsmap,
                          Layer *dst, std::string *warn, std::string *err);

///
/// Handy version of ApplyVariantSelector.
/// Use same variant name for all variantSets in Prim tree.
///
bool ApplyVariantSelector(const Layer &layer, const std::string &variant_name,
                          Layer *dst, std::string *warn, std::string *err);

///
/// Implementation of `references`
///
/// Import `layer` to this PrimSpec.
///
/// @param[inout] dst PrimSpec to be referenced.
/// @param[in] layer Layer(PrimSpec tree) to reference.
/// @param[in] primPath root Prim path in `layer`. Default = invalid Path =
/// defaultPrim in `layer`.
/// @param[in] layerOffset Layer(PrimSpec tree) to reference.
///
/// Use `defaultPrim` in `layer` as the root PrimSpec to import
///
///
bool ReferenceLayerToPrimSpec(PrimSpec &dst, const Layer &layer,
                              const Path primPath = Path(),
                              const LayerOffset layerOffset = LayerOffset());

#if 0  // TODO
///
/// Implementation of `references`
///
bool ReferenceLayersToPrimSpec(PrimSpec &dst, const std::vector<Layer> &layers
#endif

}  // namespace tinyusdz
