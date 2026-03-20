// SPDX-License-Identifier: Apache 2.0
// Copyright 2020-2023 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Common Prim reconstruction modules both for USDA and USDC.
//
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>
#include "core/prim-spec.hh"  // PrimSpec, PropertyMap, ReferenceList, Specifier (transitively: property, composition-types, prim-enums)
#include "core/xform-op.hh"   // XformOp

namespace tinyusdz {
namespace prim {

struct PrimReconstructOptions
{
  bool strict_allowedToken_check{false};

  // MaterialX validation options
  bool validate_mtlx_connection_types{false};
  bool validate_mtlx_info_id{false};
  bool validate_mtlx_connection_targets{false};
  bool validate_mtlx_duplicate_names{false};
  bool validate_mtlx_index_bounds{false};
  bool strict_mtlx_check{false};  // Enable all above
};


///
/// Reconstruct property with `xformOp:***` namespace in `properties` to `XformOp` class.
/// Corresponding property are looked up from names in `xformOpOrder`(`token[]`) property.
/// Name of processed xformOp properties are added to `table`
/// TODO: Move to prim-reconstruct.cc?
///
bool ReconstructXformOpsFromProperties(
      const Specifier &spec,
      std::set<std::string> &table, /* inout */
      PropertyMap &properties,
      std::vector<XformOp> *xformOps,
      std::string *err);

///
/// Reconstruct concrete Prim(e.g. Xform, GeomMesh) from `properties`.
///
template <typename T>
bool ReconstructPrim(
    const Specifier &spec,
    PropertyMap &properties, // modified
    const ReferenceList &references,
    T *out,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options = PrimReconstructOptions());

///
/// Reconstruct concrete Prim(e.g. Xform, GeomMesh) from PrimSpec.
///
template <typename T>
bool ReconstructPrim(
    PrimSpec &primspec,
    T *out,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options = PrimReconstructOptions());


} // namespace prim
} // namespace tinyusdz
