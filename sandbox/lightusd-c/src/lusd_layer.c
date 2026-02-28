/*
 * lusd_layer.c - Layer creation, loading, and destruction
 *
 * Implements the public LusdLayer API using the native pure-C USDC reader
 * (lusd_usdc_reader.c / lusd_read_usdc.c).  The layer struct (LusdLayer_T)
 * holds flat parsed tables from the USDC binary; value materialization is
 * delegated to Lydra (C++).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lightusd/lusd_layer.h"
#include "internal/lusd_internal.h"
#include "internal/lusd_layer_internal.h"

#include <stdlib.h>
#include <string.h>

/* Forward declarations */
LusdResult lusd__read_usdc_file(LusdLayer_T* layer, const char* path);
LusdResult lusd__read_usda_file(LusdLayer_T* layer, const char* path);
LusdResult lusd__read_usdz_file(LusdLayer_T* layer, const char* path);

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

/* Returns non-zero if `path` ends with `suffix` (case-sensitive). */
static int str_ends_with(const char* path, const char* suffix) {
    if (!path || !suffix) return 0;
    size_t pl = strlen(path);
    size_t sl = strlen(suffix);
    if (sl > pl) return 0;
    return memcmp(path + pl - sl, suffix, sl) == 0;
}

/* -----------------------------------------------------------------------
 * lusdCreateLayer
 *
 * Allocates a LusdLayer_T, resolves the identifier to a file path, loads
 * the file using the appropriate reader, and builds the prim tree.
 *
 * Only USDC (.usdc) files are supported by the native reader; all other
 * formats return LUSD_ERROR_FEATURE_NOT_PRESENT.
 * ----------------------------------------------------------------------- */
LusdResult lusdCreateLayer(LusdInstance inst,
                            const LusdLayerCreateInfo* pCI,
                            LusdLayer* pLayer) {
    if (!inst || !pCI || !pLayer) return LUSD_ERROR_INVALID_ARGUMENT;
    if (pCI->sType != LUSD_STRUCTURE_TYPE_LAYER_CREATE_INFO)
        return LUSD_ERROR_INVALID_STRUCTURE_TYPE;
    if (!pCI->pIdentifier) return LUSD_ERROR_INVALID_ARGUMENT;

    /* When the user passed NULL for pAllocator at instance creation, inst->alloc
     * is zero-initialised (all function pointers NULL).  We must pass NULL to
     * lusd_alloc/free so they fall back to the built-in malloc/free. */
    const LusdAllocationCallbacks* alloc =
        inst->alloc.pfnAllocation ? &inst->alloc : NULL;

    LusdLayer_T* L = (LusdLayer_T*)calloc(1, sizeof(LusdLayer_T));
    if (!L) return LUSD_ERROR_OUT_OF_MEMORY;

    L->inst       = inst;
    L->identifier = lusd_strdup(alloc, pCI->pIdentifier);
    if (!L->identifier) { free(L); return LUSD_ERROR_OUT_OF_MEMORY; }

    LusdResult res;

    if (str_ends_with(pCI->pIdentifier, ".usdc")) {
        res = lusd__read_usdc_file(L, pCI->pIdentifier);
        if (res != LUSD_SUCCESS) goto fail;

        res = lusd__layer_build_prims(L);
        if (res != LUSD_SUCCESS) goto fail;
    } else if (str_ends_with(pCI->pIdentifier, ".usda")) {
        res = lusd__read_usda_file(L, pCI->pIdentifier);
        if (res != LUSD_SUCCESS) goto fail;

        res = lusd__layer_build_prims(L);
        if (res != LUSD_SUCCESS) goto fail;
    } else if (str_ends_with(pCI->pIdentifier, ".usdz")) {
        res = lusd__read_usdz_file(L, pCI->pIdentifier);
        if (res != LUSD_SUCCESS) goto fail;

        res = lusd__layer_build_prims(L);
        if (res != LUSD_SUCCESS) goto fail;
    } else {
        /* .usd, etc. not yet supported */
        res = LUSD_ERROR_FEATURE_NOT_PRESENT;
        goto fail;
    }

    *pLayer = L;
    return LUSD_SUCCESS;

fail:
    lusd__layer_free_tables(L);
    lusd_free(alloc, L->identifier);
    free(L);
    return res;
}

/* -----------------------------------------------------------------------
 * lusdDestroyLayer
 * ----------------------------------------------------------------------- */
void lusdDestroyLayer(LusdInstance inst, LusdLayer layer) {
    if (!layer) return;
    LusdLayer_T* L = layer;

    lusd__layer_free_tables(L);

    if (L->identifier) {
        const LusdAllocationCallbacks* alloc =
            (inst && inst->alloc.pfnAllocation) ? &inst->alloc : NULL;
        lusd_free(alloc, L->identifier);
        L->identifier = NULL;
    }

    free(L);
}

/* -----------------------------------------------------------------------
 * lusdLayerGetIdentifier
 * ----------------------------------------------------------------------- */
LusdResult lusdLayerGetIdentifier(LusdLayer layer, const char** ppId) {
    if (!layer || !ppId) return LUSD_ERROR_INVALID_ARGUMENT;
    *ppId = layer->identifier;
    return LUSD_SUCCESS;
}

/* -----------------------------------------------------------------------
 * Stage → Layer queries (stubs — full stage implementation is separate)
 * ----------------------------------------------------------------------- */

LusdResult lusdStageGetRootLayer(LusdStage stage, LusdLayer* pLayer) {
    LUSD_UNUSED(stage); LUSD_UNUSED(pLayer);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageGetSubLayerCount(LusdStage stage, uint32_t* pCount) {
    LUSD_UNUSED(stage); LUSD_UNUSED(pCount);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageGetSubLayers(LusdStage stage, uint32_t count, LusdLayer* pLayers) {
    LUSD_UNUSED(stage); LUSD_UNUSED(count); LUSD_UNUSED(pLayers);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
