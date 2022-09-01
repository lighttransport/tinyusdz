// SPDX-License-Identifier: MIT
// Copyright 2020-Present Syoyo Fujita.
//
// Common Prim reconstruction modules both for USDA and USDC.
//
#pragma once

#include <string>
#include <vector>
#include <map>
#include "prim-types.hh"

namespace tinyusdz {
namespace prim {

///
/// Reconstruct property with `xformOp:***` namespace in `properties` to `XformOp` class.
/// Corresponding property are looked up from names in `xformOpOrder`(`token[]`) property.
/// Name of processed xformOp properties are added to `table`
///
bool ReconstructXformOpsFromProperties(
      std::set<std::string> &table, /* inout */
      const std::map<std::string, Property> &properties,
      std::vector<XformOp> *xformOps,
      std::string *err);


} // namespace prim
} // namespace tinyusdz
