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

const char *GetAOUSDCoreSpecVersionString();

USDValidationResult ValidateLayerAgainstAOUSDCore(const Layer &layer);

std::string FormatValidationResult(const USDValidationResult &result);

}  // namespace tinyusdz
