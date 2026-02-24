/*
 * lusd_timesamples.h - Time-keyed animation data
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_TIMESAMPLES_H
#define LUSD_TIMESAMPLES_H

#include "lusd_platform.h"
#include "lusd_result.h"
#include "lusd_handles.h"
#include "lusd_enums.h"

LUSD_EXTERN_C_BEGIN

/* -------------------------------------------------------------------
 * TimeSamples lifecycle
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdCreateTimeSamples(
    LusdInstance        instance,
    LusdValueType       valueType,
    LusdTimeSamples*    pTimeSamples);

LUSD_API void lusdDestroyTimeSamples(
    LusdInstance        instance,
    LusdTimeSamples     timeSamples);

/* -------------------------------------------------------------------
 * Add/query samples
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdTimeSamplesAddSample(
    LusdTimeSamples     timeSamples,
    double              time,
    LusdValue           value);

LUSD_API LusdResult lusdTimeSamplesGetSampleCount(
    LusdTimeSamples     timeSamples,
    uint32_t*           pCount);

LUSD_API LusdResult lusdTimeSamplesGetTimes(
    LusdTimeSamples     timeSamples,
    uint32_t            count,
    double*             pTimes);

LUSD_API LusdResult lusdTimeSamplesGetValueAtTime(
    LusdTimeSamples     timeSamples,
    double              time,
    LusdValue*          pValue);

LUSD_API LusdResult lusdTimeSamplesGetValueAtIndex(
    LusdTimeSamples     timeSamples,
    uint32_t            index,
    double*             pTime,
    LusdValue*          pValue);

LUSD_EXTERN_C_END

#endif /* LUSD_TIMESAMPLES_H */
