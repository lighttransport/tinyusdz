// SPDX-License-Identifier: Apache-2.0
#ifndef LIGHTUSD_VALIDATION_C_H_
#define LIGHTUSD_VALIDATION_C_H_
#include "lightusd-c.h"
#ifdef __cplusplus
extern "C" {
#endif
/* AOUSD Core validation of the stage's root layer. Validation findings are
 * reported as counts; a successfully checked but invalid layer still returns
 * LIGHTUSD_OK. */
LIGHTUSD_API lightusd_status lightusd_stage_validate_core(
    const lightusd_stage* stage, size_t* errors, size_t* warnings);
#ifdef __cplusplus
}
#endif
#endif
