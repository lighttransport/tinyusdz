// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "lightusd-cpp.hh"
#include "lightusd-validation-c.h"
namespace lightusd { namespace api {
inline lightusd_status validate_core(const Stage& stage, size_t* errors, size_t* warnings) {
  return lightusd_stage_validate_core(stage.get(), errors, warnings);
}
} }
