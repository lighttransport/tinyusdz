/*
 * lightvg/canvas_backend.h — Custom canvas backend registration
 *
 * The well-known canvas backends (SOFTWARE / BLEND2D / THORVG) are baked
 * into the lvg_canvas_backend_t enum. This header lets an embedder slot
 * an additional backend (e.g. Skia, Cairo, a hardware-accelerated one)
 * into runtime by handing the library a small descriptor.
 *
 * Slot indices ≥ LVG_CANVAS_BACKEND_CUSTOM_FIRST are returned by
 * lvg_canvas_register_custom_backend() and can be passed unchanged to
 * lvg_canvas_init_backend().
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTVG_CANVAS_BACKEND_H
#define LIGHTVG_CANVAS_BACKEND_H

#include "canvas.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Backend descriptor. The library stores the pointer by reference; the
 * caller must keep the descriptor alive for the lifetime of the
 * registration (typically a `static const` instance).
 *
 *   - `name` is a short identifier shown by lvg_canvas_backend_name().
 *   - `init` is invoked by lvg_canvas_init_backend() AFTER the canvas
 *     has been zero-initialised by lvg_canvas_init(). The callback is
 *     responsible for installing canvas->_ops and any backend state.
 *     Return true on success; false leaves the canvas on the software
 *     fast path.
 */
typedef struct {
    const char *name;
    bool      (*init)(lvg_canvas_t *canvas);
} lvg_canvas_backend_desc_t;

/* Range of valid runtime-registered slot indices. The library reserves a
 * fixed set of custom slots; registering more than that range allows
 * returns LVG_ERR_NOMEM. */
#define LVG_CANVAS_BACKEND_CUSTOM_FIRST 16
#define LVG_CANVAS_BACKEND_CUSTOM_LAST  31

/**
 * Register a custom backend. Returns the new slot index on success
 * (always ≥ LVG_CANVAS_BACKEND_CUSTOM_FIRST), or a negative
 * lvg_result_t error code (LVG_ERR_INVALID for a malformed
 * descriptor, LVG_ERR_NOMEM if all custom slots are taken).
 *
 * The slot index can be passed to lvg_canvas_init_backend() directly.
 */
int lvg_canvas_register_custom_backend(const lvg_canvas_backend_desc_t *desc);

/**
 * Look up a backend descriptor by slot. Returns NULL for empty slots
 * and for the built-in well-known indices (use lvg_canvas_backend_name()
 * for those).
 */
const lvg_canvas_backend_desc_t *lvg_canvas_backend_describe(int slot);

/**
 * Clear all registered custom backends. Mostly useful for tests; in
 * normal operation backends are registered once at startup and never
 * removed.
 */
void lvg_canvas_reset_custom_backends(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTVG_CANVAS_BACKEND_H */
