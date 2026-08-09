/*
 * TinyEXR - reusable generic EXR codec context.
 *
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "exr_internal.h"

struct exr_context {
    exr_allocator alloc;
    void *jph_state;
    void *piz_state;
};

void **exr_context_jph_slot(exr_context *ctx) {
    return ctx ? &ctx->jph_state : NULL;
}

void **exr_context_piz_slot(exr_context *ctx) {
    return ctx ? &ctx->piz_state : NULL;
}

const exr_allocator *exr_context_allocator(const exr_context *ctx) {
    return ctx ? &ctx->alloc : NULL;
}

exr_result exr_context_create(const exr_allocator *alloc, exr_context **out) {
    exr_context *ctx;
    const exr_allocator *a = alloc ? alloc : exr_default_allocator();
    if (!out || !a) return EXR_ERROR_INVALID_ARGUMENT;
    *out = NULL;
    ctx = (exr_context *)exr_calloc(a, 1, sizeof(*ctx));
    if (!ctx) return EXR_ERROR_OUT_OF_MEMORY;
    ctx->alloc = *a;
    *out = ctx;
    return EXR_SUCCESS;
}

void exr_context_destroy(exr_context *ctx) {
    if (!ctx) return;
#ifndef EXR_NO_JPH
    exr_jph_context_free(ctx);
#endif
    exr_piz_context_free(ctx);
    exr_free(&ctx->alloc, ctx);
}
