/*
 * lusd_write_usda.c - USDA writer (stub)
 */
#include "lightusd/lusd_write.h"
#include "internal/lusd_internal.h"

LusdResult lusdCreateWriter(LusdInstance inst, const LusdWriterCreateInfo* pCI, LusdWriter* pWriter) {
    LUSD_UNUSED(inst); LUSD_UNUSED(pCI); LUSD_UNUSED(pWriter); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
void lusdDestroyWriter(LusdInstance inst, LusdWriter writer) {
    LUSD_UNUSED(inst); LUSD_UNUSED(writer);
}
LusdResult lusdStageExportToString(LusdInstance inst, LusdStage stage, char** ppOutput, uint64_t* pLength) {
    LUSD_UNUSED(inst); LUSD_UNUSED(stage); LUSD_UNUSED(ppOutput); LUSD_UNUSED(pLength); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdWriterWriteStage(LusdWriter writer, LusdStage stage) {
    LUSD_UNUSED(writer); LUSD_UNUSED(stage); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
