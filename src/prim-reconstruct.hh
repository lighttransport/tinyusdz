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

using PropertyMap = std::map<std::string, Property>;
using ReferenceList = std::vector<std::pair<ListEditQual, Reference>>;


///
/// Reconstruct property with `xformOp:***` namespace in `properties` to `XformOp` class.
/// Corresponding property are looked up from names in `xformOpOrder`(`token[]`) property.
/// Name of processed xformOp properties are added to `table`
///
bool ReconstructXformOpsFromProperties(
      std::set<std::string> &table, /* inout */
      const PropertyMap &properties,
      std::vector<XformOp> *xformOps,
      std::string *err);

///
/// Reconstruct Prim(e.g. Xform, GeomMesh) from `properties`.
///
template <typename T>
bool ReconstructPrim(
    const PropertyMap &properties,
    const ReferenceList &references,
    T *out,
    std::string *err);


} // namespace prim
} // namespace tinyusdz
