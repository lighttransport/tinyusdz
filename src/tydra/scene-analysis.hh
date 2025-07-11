// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment, Inc.
//
// Scene access API
//
// NOTE: Tydra API does not use nonstd::optional and nonstd::expected,
// std::functions and other non basic STL feature for easier language bindings.
//
#pragma once

#include "prim-types.hh"
#include "value-types.hh"

namespace tinyusdz {
namespace tydra {

//
// Compute the bounding box of the scene(Layer) at specified time.
//
// use_extent: Use `extent` attribute instead of computing the bounding box when
// true.
//
// Return true upon success, false when a Layer does not contain any
// Boundable(e.g. GeomMesh) PrimSpec.
//
bool ComputeBound(const Layer &layer, const bool use_extent, Extent &bbox,
                  const double t = value::TimeCode::Default());

//
// Compute the bounding box of the PrimSpec at specified time.
//
// use_extent: Use `extent` attribute instead of computing the bounding box when
// true.
//
// Return true upon success, false when failure(e.g. PrimSpec is not Boundable
// type).
//
// Limitation: Current implementation does not consider skinning transform
bool ComputePrimSpecBound(const PrimSpec &ps, const bool use_extent, Extent &bbox,
                  const double t = value::TimeCode::Default());

}  // namespace tydra
}  // namespace tinyusdz
