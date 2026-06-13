// SPDX-License-Identifier: Apache 2.0
// Copyright 2025-Present Light Transport Entertainment, Inc.
//
#pragma once

#include <cstdint>

#include "core/prim-spec.hh"
#include "core/layer-types.hh"
#include "../tiny-hashmap.hh"

namespace tinyusdz {
namespace tydra {

///
/// Options controlling value-level comparison.
///
/// Floating-point values are compared with a ULP (units-in-the-last-place)
/// tolerance by default. This absorbs the ~1 ULP rounding differences that
/// pxrUSD introduces in quaternion / xform (matrix4d, quatf) math, which would
/// otherwise show up as spurious diffs. The ULP tolerance is applied
/// element-wise to every float-backed type (scalars, vecN, matrices, quats,
/// and their array forms).
///
struct DiffOptions {
  // Per-component ULP tolerance. 0 == bitwise-exact.
  uint32_t floatUlps = 1;   // float (and half, compared in float space)
  uint64_t doubleUlps = 1;  // double

  // If >= 0, two floating values also count as equal when |a-b| <= absEps.
  // OR'd with the ULP test. Negative (default) => pure ULP.
  double absEps = -1.0;

  // ULP tolerance for the timeSample time axis (double).
  uint64_t timeUlps = 2;

  // Compare metadata at every level (attribute / relationship / prim / layer).
  bool compareMetadata = true;

  // Keep the existing asset-path suffix/leaf equivalence heuristic.
  bool fuzzyAssetPaths = true;
};

struct PropDiff
{
  struct ModifiedProp {
    std::string name;
    std::string lhs;
    std::string rhs;
    // What aspects changed: "value", "type", "variability", "connections",
    // "timeSamples", "custom", "listOp", "meta:<field>", ...
    std::vector<std::string> reasons;
  };

  std::vector<std::string> addedProps;
  std::vector<std::string> modifiedProps;
  std::vector<ModifiedProp> modifiedPropDetails;
  std::vector<std::string> deletedProps;
};

// A PrimSpec that was modified, with the list of reasons describing what about
// the prim itself changed: "typeName", "specifier", "propCount", "childCount",
// "meta:<field>", ... (property and child changes are reported separately).
struct ModifiedPrimSpec
{
  std::string name;
  std::vector<std::string> reasons;
};

struct PrimSpecDiff
{
  std::vector<std::string> addedPS;
  std::vector<std::string> modifiedPS;
  std::vector<std::string> deletedPS;
  // Parallel to modifiedPS: per-modified-child reason lists.
  std::vector<ModifiedPrimSpec> modifiedDetails;
};

// Layer (stage) metadata differences (metersPerUnit, upAxis, defaultPrim, ...).
struct LayerMetaDiff
{
  // e.g. "~metersPerUnit", "+customLayerData:foo", "-upAxis".
  std::vector<std::string> changedFields;
  std::vector<PropDiff::ModifiedProp> details;

  bool changed() const { return !changedFields.empty(); }
};

void Diff(const Layer &lhs, const Layer &rhs,

  /* key = primspec path */
  tinyusdz::HashMap<std::string, PrimSpecDiff> &psDiffs,

  /* key = primspec path */
  tinyusdz::HashMap<std::string, PropDiff> &propDiffs,

  const DiffOptions &opts = {},

  /* optional: stage/layer-metadata diff (nullptr to skip reporting) */
  LayerMetaDiff *layerMetaDiff = nullptr);

///
/// Generate text-based diff output similar to 'diff' command
///
std::string DiffToText(const Layer &lhs, const Layer &rhs,
                       const std::string &lhs_name = "left",
                       const std::string &rhs_name = "right",
                       const DiffOptions &opts = {});

///
/// Generate JSON-based diff output
///
std::string DiffToJSON(const Layer &lhs, const Layer &rhs,
                       const std::string &lhs_name = "left",
                       const std::string &rhs_name = "right",
                       const DiffOptions &opts = {});

} // namespace tydra
} // namespace tinyusdz
