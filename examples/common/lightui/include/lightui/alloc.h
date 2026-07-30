/*
 * lightui/alloc.h — Widget-level allocator callbacks
 *
 * Used by widgets that own large heap buffers (e.g. table cell grids)
 * and want to let the caller plug in a custom allocator (arena, pool,
 * tracking malloc, etc.). Mirrors the lui_vg_alloc_fn pair in
 * <lightui/vg/surface.h>.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_ALLOC_H
#define LIGHTUI_ALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Allocator: returns at least `size` bytes, or NULL on failure.
 *  `userdata` is passed through unchanged. */
typedef void *(*lui_alloc_fn)(void *userdata, size_t size);

/** Deallocator paired with lui_alloc_fn. Passing NULL `ptr` is a no-op. */
typedef void  (*lui_free_fn)(void *userdata, void *ptr);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_ALLOC_H */
