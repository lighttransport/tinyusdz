// SPDX-License-Identifier: Apache-2.0
#include "lightusd-validation-c.h"
#include "c-internal.hh"
#include "next/validation/usd-validation.hh"
namespace n = lightusd::next;
using lightusd_internal::Fail;
extern "C" {
lightusd_status lightusd_stage_validate_core(const lightusd_stage* stage,
                                              size_t* errors,
                                              size_t* warnings) {
  if (!stage || !errors || !warnings) {
    return Fail(LIGHTUSD_ERR_INVALID_ARG, "stage/errors/warnings is null");
  }
  *errors = 0;
  *warnings = 0;
  const n::Layer* root = stage->ReadStage().GetRootLayer();
  if (!root) return Fail(LIGHTUSD_ERR_NOT_FOUND, "stage has no root layer");
  const n::USDValidationResult result = n::ValidateLayerAgainstAOUSDCore(*root);
  *errors = result.error_count();
  *warnings = result.warning_count();
  return LIGHTUSD_OK;
}

}
