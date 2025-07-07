// SPDX-License-Identifier: Apache 2.0
// Copyright 2025-Present Light Transport Entertainment, Inc.
//
#pragma once

#include <unordered_map>

#include "prim-types.hh"

namespace tinyusdz {
namespace tydra {

struct PropDiff
{
  std::vector<std::string> addedProps;
  std::vector<std::string> modifiedProps;
  std::vector<std::string> deletedProps;
};

struct PrimSpecDiff
{
  std::vector<std::string> addedPS;
  std::vector<std::string> modifiedPS;
  std::vector<std::string> deletedPS;
};

void Diff(const Layer &lhs, const Layer &rhs,

  /* key = primspec path */
  std::unordered_map<std::string, PrimSpecDiff> &psDiffs,

  /* key = primspec path */
  std::unordered_map<std::string, PropDiff> &propDiffs);

} // namespace tydra
} // namespace tinyusdz
