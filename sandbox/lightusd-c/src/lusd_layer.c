/*
 * lusd_layer.c - Layer operations (stub)
 */
#include "lightusd/lusd_layer.h"
#include "internal/lusd_internal.h"

LusdResult lusdCreateLayer(LusdInstance inst, const LusdLayerCreateInfo* pCI, LusdLayer* pLayer) {
    LUSD_UNUSED(inst); LUSD_UNUSED(pCI); LUSD_UNUSED(pLayer); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
void lusdDestroyLayer(LusdInstance inst, LusdLayer layer) {
    LUSD_UNUSED(inst); LUSD_UNUSED(layer);
}
LusdResult lusdLayerGetIdentifier(LusdLayer layer, const char** ppId) {
    LUSD_UNUSED(layer); LUSD_UNUSED(ppId); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdStageGetRootLayer(LusdStage stage, LusdLayer* pLayer) {
    LUSD_UNUSED(stage); LUSD_UNUSED(pLayer); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdStageGetSubLayerCount(LusdStage stage, uint32_t* pCount) {
    LUSD_UNUSED(stage); LUSD_UNUSED(pCount); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdStageGetSubLayers(LusdStage stage, uint32_t count, LusdLayer* pLayers) {
    LUSD_UNUSED(stage); LUSD_UNUSED(count); LUSD_UNUSED(pLayers); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
