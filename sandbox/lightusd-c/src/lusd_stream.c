/*
 * lusd_stream.c - Progressive/streaming loader (stub)
 */
#include "lightusd/lusd_stream.h"
#include "internal/lusd_internal.h"

LusdResult lusdCreateStreamLoader(LusdInstance inst, const LusdStreamLoaderCreateInfo* pCI, LusdStreamLoader* pLoader) {
    LUSD_UNUSED(inst); LUSD_UNUSED(pCI); LUSD_UNUSED(pLoader); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
void lusdDestroyStreamLoader(LusdInstance inst, LusdStreamLoader loader) {
    LUSD_UNUSED(inst); LUSD_UNUSED(loader);
}
LusdResult lusdStreamLoaderFeed(LusdStreamLoader loader, const void* pData, uint64_t dataSize) {
    LUSD_UNUSED(loader); LUSD_UNUSED(pData); LUSD_UNUSED(dataSize); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdStreamLoaderFinish(LusdStreamLoader loader, LusdStage* pStage) {
    LUSD_UNUSED(loader); LUSD_UNUSED(pStage); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
