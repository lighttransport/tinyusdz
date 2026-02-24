/*
 * lusd_timesamples.c - Time samples operations (stub)
 */
#include "lightusd/lusd_timesamples.h"
#include "internal/lusd_internal.h"

LusdResult lusdCreateTimeSamples(LusdInstance inst, LusdValueType vt, LusdTimeSamples* pTS) {
    LUSD_UNUSED(inst); LUSD_UNUSED(vt); LUSD_UNUSED(pTS); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
void lusdDestroyTimeSamples(LusdInstance inst, LusdTimeSamples ts) {
    LUSD_UNUSED(inst); LUSD_UNUSED(ts);
}
LusdResult lusdTimeSamplesAddSample(LusdTimeSamples ts, double time, LusdValue value) {
    LUSD_UNUSED(ts); LUSD_UNUSED(time); LUSD_UNUSED(value); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdTimeSamplesGetSampleCount(LusdTimeSamples ts, uint32_t* pCount) {
    LUSD_UNUSED(ts); LUSD_UNUSED(pCount); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdTimeSamplesGetTimes(LusdTimeSamples ts, uint32_t count, double* pTimes) {
    LUSD_UNUSED(ts); LUSD_UNUSED(count); LUSD_UNUSED(pTimes); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdTimeSamplesGetValueAtTime(LusdTimeSamples ts, double time, LusdValue* pValue) {
    LUSD_UNUSED(ts); LUSD_UNUSED(time); LUSD_UNUSED(pValue); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdTimeSamplesGetValueAtIndex(LusdTimeSamples ts, uint32_t index, double* pTime, LusdValue* pValue) {
    LUSD_UNUSED(ts); LUSD_UNUSED(index); LUSD_UNUSED(pTime); LUSD_UNUSED(pValue); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
