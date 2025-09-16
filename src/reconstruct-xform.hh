// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Transform and scene primitive reconstruction
#pragma once

#include <string>
#include <set>
#include <vector>
#include "prim-types.hh"

namespace tinyusdz {
namespace prim {

struct PrimReconstructOptions;

// Transform/scene reconstruction functions
// Note: These are template specializations defined in reconstruct-xform.cc

// Helper function for xform operations
bool ReconstructXformOpsFromProperties(
    const Specifier &spec,
    std::set<std::string> &table,
    const PropertyMap &properties,
    std::vector<XformOp> *xformOps,
    std::string *err);

} // namespace prim
} // namespace tinyusdz