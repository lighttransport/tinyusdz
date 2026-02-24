/*
 * lusd_arena.c - Arena allocator (stub)
 */
#include "lightusd/lusd_arena.h"
#include "internal/lusd_internal.h"

LusdResult lusdCreateArena(LusdInstance inst, const LusdArenaCreateInfo* pCI, LusdArena* pArena) {
    LUSD_UNUSED(inst); LUSD_UNUSED(pCI); LUSD_UNUSED(pArena); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
void lusdDestroyArena(LusdInstance inst, LusdArena arena) {
    LUSD_UNUSED(inst); LUSD_UNUSED(arena);
}
void* lusdArenaAlloc(LusdArena arena, size_t size, size_t alignment) {
    LUSD_UNUSED(arena); LUSD_UNUSED(size); LUSD_UNUSED(alignment); return NULL;
}
void lusdArenaReset(LusdArena arena) {
    LUSD_UNUSED(arena);
}
uint64_t lusdArenaGetUsedBytes(LusdArena arena) {
    LUSD_UNUSED(arena); return 0;
}
