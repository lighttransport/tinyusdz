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

///
/// Generate text-based diff output similar to 'diff' command
///
std::string DiffToText(const Layer &lhs, const Layer &rhs, 
                       const std::string &lhs_name = "left",
                       const std::string &rhs_name = "right");

///
/// Generate JSON-based diff output
///
std::string DiffToJSON(const Layer &lhs, const Layer &rhs,
                       const std::string &lhs_name = "left", 
                       const std::string &rhs_name = "right");

} // namespace tydra
} // namespace tinyusdz
