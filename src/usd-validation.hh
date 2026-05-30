// SPDX-License-Identifier: Apache 2.0
// Copyright 2026 Light Transport Entertainment Inc.
//
// AOUSD Core semantic validation helpers.
//
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/layer-types.hh"
#include "layer.hh"

namespace tinyusdz {

enum class USDValidationSeverity {
  Error,
  Warning,
};

struct USDValidationIssue {
  USDValidationSeverity severity{USDValidationSeverity::Error};
  std::string rule_id;
  std::string location;
  std::string message;
};

struct USDValidationResult {
  std::vector<USDValidationIssue> issues;

  size_t error_count() const;
  size_t warning_count() const;
  bool ok() const;
};

// Selects which rule groups to run.
//
// `core` rules (default on) preserve the historical AOUSD Core behavior:
// layer metadata, prim-name/xformOp integrity, CollectionAPI, ColorSpace.
//
// `geom` and `shade` rules are modeled on OpenUSD `usdchecker` (geometry and
// shading encapsulation). They are warning-heavy and composition-sensitive, so
// they are off by default and must be opted into explicitly.
struct ValidationOptions {
  bool core{true};
  bool geom{false};
  bool shade{false};
};

const char *GetAOUSDCoreSpecVersionString();

// Validate an uncomposed Layer. The no-options overload runs `core` rules only,
// preserving the original behavior.
USDValidationResult ValidateLayerAgainstAOUSDCore(const Layer &layer);
USDValidationResult ValidateLayerAgainstAOUSDCore(const Layer &layer,
                                                  const ValidationOptions &options);

std::string FormatValidationResult(const USDValidationResult &result);

}  // namespace tinyusdz
