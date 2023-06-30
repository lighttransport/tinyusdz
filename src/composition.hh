// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment Inc.
//
// Layer and Prim composition features.
//
#pragma once

#include "prim-types.hh"
#include "asset-resolution.hh"

// TODO
// - [ ] Compose `references`
// - [ ] Compose `payloads`
// - [ ] Compose `specializes`
// - [ ] Compose `inherits`
// - [ ] Compose `variantSets`

namespace tinyusdz {

// USD asset loading state.
enum class LoadState : uint32_t
{
  Toplevel = 1, // load initial .usd(default)
  Sublayer = 1 << 1, // load USD from Stage meta sublayer
  Reference = 1 << 2, // load USD from Prim meta reference
  Payload = 1 << 3// load USD from Prim meta payload
};

struct SublayersCompositionOptions {
  // The maximum depth for nested `subLayers`
  uint32_t max_depth = 1024u;
};

struct ReferencesCompositionOptions {
  // The maximum depth for nested `references`
  uint32_t max_depth = 1024u;
};

struct PayloasCompositionOptions {
  // The maximum depth for nested `payloads`
  uint32_t max_depth = 1024u;
};

///
/// Load subLayer USD files in `layer`, and return composited(flattened) Layer to `composited_layer`
/// Supply search_path with `base_dir`
///
bool CompositeSublayers(const std::string &base_dir, const Layer &layer, Layer *composited_layer, std::string *err, const SublayersCompositionOptions options = SublayersCompositionOptions());

///
/// Load subLayer USD files in `layer`, and return composited(flattened) Layer to `composited_layer`
/// Supply AssetResolutionResolver
///
bool CompositeSublayers(const AssetResolutionResolver &resolver, const Layer &layer, Layer *composited_layer, std::string *err, const SublayersCompositionOptions options = SublayersCompositionOptions());



} // namespace tinyusdz
