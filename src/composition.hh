// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment Inc.
//
// Layer and Prim composition features.
//
#pragma once

#include "prim-types.hh"

namespace tinyusdz {

// USD asset loading state.
enum class LoadState
{
  Toplevel, // load initial .usd(default)
  Sublayer, // load USD from Stage meta sublayer
  Reference, // load USD from Prim meta reference
  Payload // load USD from Prim meta payload
};

} // namespace tinyusdz
