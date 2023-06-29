// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment Inc.
//
// Layer and Prim composition features.
//
#pragma once

#include "prim-types.hh"
#include "asset-resolution.hh"

namespace tinyusdz {

// USD asset loading state.
enum class LoadState : uint32_t
{
  Toplevel = 1, // load initial .usd(default)
  Sublayer = 1 << 1, // load USD from Stage meta sublayer
  Reference = 1 << 2, // load USD from Prim meta reference
  Payload = 1 << 3// load USD from Prim meta payload
};

///
/// Load subLayer USD files in `layer`, and return composited(flattened) Layer to `composited_layer`
/// Supply search_path with `base_dir`
///
bool CompositeSublayers(const std::string &base_dir, const Layer &layer, Layer *composited_layer, std::string *err);

///
/// Load subLayer USD files in `layer`, and return composited(flattened) Layer to `composited_layer`
/// Supply AssetResolutionResolver
///
bool CompositeSublayers(const AssetResolutionResolver &resolver, const Layer &layer, Layer *composited_layer, std::string *err);

} // namespace tinyusdz
