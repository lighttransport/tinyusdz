/*
 * lightvg/src/dirty.c — Dirty rectangle tracker
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightvg/dirty.h>
#include <string.h>

void lvg_dirty_init(lvg_dirty_t *d)  { memset(d, 0, sizeof(*d)); }
void lvg_dirty_reset(lvg_dirty_t *d) { d->count = 0; d->all = false; }
void lvg_dirty_mark_all(lvg_dirty_t *d) { d->all = true; d->count = 0; }

bool lvg_dirty_any(const lvg_dirty_t *d) { return d->all || d->count > 0; }

/* Merge rects[0..count-1] into a single bounding box at rects[0]. */
static void merge_to_one(lvg_dirty_t *d)
{
    if (d->count <= 1) return;
    int x0 = d->rects[0].x, y0 = d->rects[0].y;
    int x1 = x0 + d->rects[0].width, y1 = y0 + d->rects[0].height;
    for (int i = 1; i < d->count; i++) {
        int rx1 = d->rects[i].x + d->rects[i].width;
        int ry1 = d->rects[i].y + d->rects[i].height;
        if (d->rects[i].x < x0) x0 = d->rects[i].x;
        if (d->rects[i].y < y0) y0 = d->rects[i].y;
        if (rx1 > x1) x1 = rx1;
        if (ry1 > y1) y1 = ry1;
    }
    d->rects[0].x = x0; d->rects[0].y = y0;
    d->rects[0].width = x1 - x0; d->rects[0].height = y1 - y0;
    d->count = 1;
}

void lvg_dirty_add(lvg_dirty_t *d, const lvg_rect_t *r)
{
    if (!d || !r) return;
    if (d->all || lvg_rect_is_empty(r)) return;

    /* Skip if already fully covered by an existing rect. */
    for (int i = 0; i < d->count; i++) {
        const lvg_rect_t *e = &d->rects[i];
        if (r->x >= e->x && r->y >= e->y &&
            r->x + r->width  <= e->x + e->width &&
            r->y + r->height <= e->y + e->height)
            return;
    }

    if (d->count < LVG_DIRTY_MAX_RECTS) {
        d->rects[d->count++] = *r;
    } else {
        /* Overflow: merge all into one bbox, then absorb the new rect. */
        merge_to_one(d);
        int x0 = d->rects[0].x < r->x ? d->rects[0].x : r->x;
        int y0 = d->rects[0].y < r->y ? d->rects[0].y : r->y;
        int x1 = (d->rects[0].x + d->rects[0].width)  > (r->x + r->width)  ?
                  (d->rects[0].x + d->rects[0].width)  : (r->x + r->width);
        int y1 = (d->rects[0].y + d->rects[0].height) > (r->y + r->height) ?
                  (d->rects[0].y + d->rects[0].height) : (r->y + r->height);
        d->rects[0].x = x0; d->rects[0].y = y0;
        d->rects[0].width = x1 - x0; d->rects[0].height = y1 - y0;
    }
}

bool lvg_dirty_test(const lvg_dirty_t *d, const lvg_rect_t *r)
{
    if (d->all) return true;
    for (int i = 0; i < d->count; i++)
        if (lvg_rect_overlaps(r, &d->rects[i])) return true;
    return false;
}

lvg_rect_t lvg_dirty_bounds(const lvg_dirty_t *d, const lvg_rect_t *fallback)
{
    if (d->all) return fallback ? *fallback : lvg_rect_make(0,0,0,0);
    if (d->count == 0) return lvg_rect_make(0,0,0,0);

    int x0 = d->rects[0].x, y0 = d->rects[0].y;
    int x1 = x0 + d->rects[0].width, y1 = y0 + d->rects[0].height;
    for (int i = 1; i < d->count; i++) {
        int rx1 = d->rects[i].x + d->rects[i].width;
        int ry1 = d->rects[i].y + d->rects[i].height;
        if (d->rects[i].x < x0) x0 = d->rects[i].x;
        if (d->rects[i].y < y0) y0 = d->rects[i].y;
        if (rx1 > x1) x1 = rx1;
        if (ry1 > y1) y1 = ry1;
    }
    return lvg_rect_make(x0, y0, x1 - x0, y1 - y0);
}

void lvg_dirty_foreach(const lvg_dirty_t *d, const lvg_rect_t *bounds,
                       lvg_dirty_clip_fn fn, void *user)
{
    if (d->all) { fn(bounds, user); return; }
    for (int i = 0; i < d->count; i++) {
        lvg_rect_t clip = lvg_rect_intersect(bounds, &d->rects[i]);
        if (!lvg_rect_is_empty(&clip)) fn(&clip, user);
    }
}

void lvg_dirty_translate(lvg_dirty_t *dst, const lvg_dirty_t *src, int dx, int dy)
{
    *dst = *src;
    if (src->all) return;
    for (int i = 0; i < src->count; i++) {
        dst->rects[i].x += dx;
        dst->rects[i].y += dy;
    }
}
