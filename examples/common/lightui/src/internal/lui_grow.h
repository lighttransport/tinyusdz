/*
 * src/internal/lui_grow.h — Generic doubling-realloc scratch buffer helper
 *
 * Pattern used in places that own a (buf, cap) pair and want to ensure
 * @need slots are available, doubling the capacity from a base of 256
 * until it fits. On allocation failure leaves the existing buffer
 * untouched and returns NULL.
 *
 *   if (!lui_grow_scratch((void **)&buf, &cap, need, sizeof(*buf)))
 *       return out_of_memory_error;
 *
 * Marked static inline because callers are typically hot-path render
 * loops and we want the call to inline cleanly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_INTERNAL_LUI_GROW_H
#define LIGHTUI_INTERNAL_LUI_GROW_H

#include <stdlib.h>

static inline void *lui_grow_scratch(void **bufp, int *cap,
                                      int need, size_t elem_size)
{
    if (need <= *cap) return *bufp;
    int new_cap = *cap ? *cap : 256;
    while (new_cap < need) new_cap *= 2;
    void *p = realloc(*bufp, (size_t)new_cap * elem_size);
    if (!p) return NULL;
    *bufp = p;
    *cap = new_cap;
    return p;
}

#endif /* LIGHTUI_INTERNAL_LUI_GROW_H */
