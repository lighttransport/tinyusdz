/*
 * lightvg/src/backend_registry.c — Custom canvas backend registry
 *
 * Holds runtime-registered backend descriptors. The well-known backends
 * (SOFTWARE / BLEND2D / THORVG) bypass the registry — they are dispatched
 * directly from lvg_canvas_init_backend() in canvas.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightvg/canvas_backend.h>

#include <stddef.h>

#define LVG_CANVAS_BACKEND_CUSTOM_COUNT \
    (LVG_CANVAS_BACKEND_CUSTOM_LAST - LVG_CANVAS_BACKEND_CUSTOM_FIRST + 1)

static const lvg_canvas_backend_desc_t *
    g_custom_backends[LVG_CANVAS_BACKEND_CUSTOM_COUNT];

int lvg_canvas_register_custom_backend(const lvg_canvas_backend_desc_t *desc)
{
    if (!desc || !desc->name || !desc->init) return LVG_ERR_INVALID;
    for (int i = 0; i < LVG_CANVAS_BACKEND_CUSTOM_COUNT; i++) {
        if (!g_custom_backends[i]) {
            g_custom_backends[i] = desc;
            return LVG_CANVAS_BACKEND_CUSTOM_FIRST + i;
        }
    }
    return LVG_ERR_NOMEM;
}

const lvg_canvas_backend_desc_t *lvg_canvas_backend_describe(int slot)
{
    if (slot < LVG_CANVAS_BACKEND_CUSTOM_FIRST ||
        slot > LVG_CANVAS_BACKEND_CUSTOM_LAST) return NULL;
    return g_custom_backends[slot - LVG_CANVAS_BACKEND_CUSTOM_FIRST];
}

void lvg_canvas_reset_custom_backends(void)
{
    for (int i = 0; i < LVG_CANVAS_BACKEND_CUSTOM_COUNT; i++)
        g_custom_backends[i] = NULL;
}
