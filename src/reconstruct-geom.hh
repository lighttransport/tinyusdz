// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Geometry primitive reconstruction
#pragma once

#include <string>
#include "prim-types.hh"
#include "usdGeom.hh"

namespace tinyusdz {
namespace prim {

struct PrimReconstructOptions;

// Geometry reconstruction functions
// Note: These are template specializations defined in reconstruct-geom.cc

// Helper function for GPrim properties
bool ReconstructGPrimProperties(
    const Specifier &spec,
    std::set<std::string> &table,
    const PropertyMap &properties,
    GPrim *gprim,
    std::string *warn,
    std::string *err,
    bool strict_allowedToken_check);

} // namespace prim
} // namespace tinyusdz