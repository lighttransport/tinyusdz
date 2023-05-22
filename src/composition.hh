// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment Inc.
//
// Layer and Prim composition features.
//
#pragma once

#include "prim-types.hh"

namespace tinyusdz {

// USD asset loading state.
enum class LoadState : uint32_t
{
  Toplevel = 1, // load initial .usd(default)
  Sublayer = 1 << 1, // load USD from Stage meta sublayer
  Reference = 1 << 2, // load USD from Prim meta reference
  Payload = 1 << 3// load USD from Prim meta payload
};

} // namespace tinyusdz
