/*
 * dopesheet.c — Dopesheet / keyframe editor widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/dopesheet.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static int frame_to_px(const lui_dopesheet_t *ds, lvg_rect_t wr, int frame)
{
    return wr.x + ds->header_width +
           (int)((frame - ds->view_start) * ds->pixels_per_frame);
}

static int px_to_frame(const lui_dopesheet_t *ds, lvg_rect_t wr, int px)
{
    float f = (float)(px - wr.x - ds->header_width) / ds->pixels_per_frame
              + ds->view_start;
    return (int)(f + 0.5f);
}

/* Sort keyframes by frame number (insertion sort). */
static void sort_keys(lui_dopesheet_channel_t *ch)
{
    for (int i = 1; i < ch->key_count; i++) {
        lui_keyframe_t tmp = ch->keys[i];
        int j = i - 1;
        while (j >= 0 && ch->keys[j].frame > tmp.frame) {
            ch->keys[j + 1] = ch->keys[j];
            j--;
        }
        ch->keys[j + 1] = tmp;
    }
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int ds_measure(const lui_widget_t *w, int *out_w, int *out_h,
                       void *user)
{
    const lui_dopesheet_t *ds = (const lui_dopesheet_t *)w;
    (void)user;
    *out_w = 400;
    *out_h = ds->ruler_height + ds->channel_count * ds->row_height;
    return 0;
}

static void ds_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_dopesheet_t *ds = (lui_dopesheet_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, ds->bg);

    int grid_x = r.x + ds->header_width;
    int grid_w = r.width - ds->header_width;

    /* Ruler */
    lvg_canvas_fill_rect(canvas, grid_x, r.y, grid_w, ds->ruler_height,
                          ds->ruler_bg);

    /* Frame grid lines */
    int frame_step = (int)(40.0f / ds->pixels_per_frame);
    if (frame_step < 1) frame_step = 1;
    int first = ds->view_start - (ds->view_start % frame_step);
    for (int f = first; ; f += frame_step) {
        int px = frame_to_px(ds, r, f);
        if (px < grid_x) continue;
        if (px > r.x + r.width) break;
        lvg_canvas_draw_line(canvas, px, r.y, px,
                              r.y + r.height, ds->grid_color, 1);
        /* Ruler tick mark */
        lvg_canvas_fill_rect(canvas, px, r.y + ds->ruler_height - 4,
                              1, 4, ds->ruler_text);
    }

    /* Channel rows */
    for (int i = 0; i < ds->channel_count; i++) {
        lui_dopesheet_channel_t *ch = &ds->channels[i];
        int ry = r.y + ds->ruler_height + i * ds->row_height;

        /* Row background */
        lvg_color_t row_bg = (i & 1) ? ds->row_bg_alt : ds->row_bg;
        lvg_canvas_fill_rect(canvas, r.x, ry, r.width, ds->row_height,
                              row_bg);

        /* Channel label (char rectangles) */
        int label_len = (int)strlen(ch->label);
        int lx = r.x + 4;
        int ly = ry + (ds->row_height - 10) / 2;
        for (int c = 0; c < label_len && c < 12; c++) {
            lvg_canvas_fill_rect(canvas, lx + c * 7, ly, 5, 10,
                                  ds->text_color);
        }

        /* Keyframe diamonds */
        int hs = ds->key_size / 2;
        int cy = ry + ds->row_height / 2;
        for (int k = 0; k < ch->key_count; k++) {
            int px = frame_to_px(ds, r, ch->keys[k].frame);
            if (px < grid_x - hs || px > r.x + r.width + hs) continue;

            lvg_color_t kc;
            if (ch->keys[k].selected)
                kc = ds->key_selected;
            else if (i == ds->hovered_channel && k == ds->hovered_key)
                kc = ds->key_hovered;
            else
                kc = ch->color;

            /* Diamond shape: 4 triangles approximated as filled rects */
            for (int dy = -hs; dy <= hs; dy++) {
                int hw = hs - (dy < 0 ? -dy : dy);
                if (hw <= 0) continue;
                lvg_canvas_fill_rect(canvas, px - hw, cy + dy,
                                      hw * 2, 1, kc);
            }
        }
    }

    /* Box selection overlay */
    if (ds->box_selecting) {
        int bx = ds->box_x0 < ds->box_x1 ? ds->box_x0 : ds->box_x1;
        int by = ds->box_y0 < ds->box_y1 ? ds->box_y0 : ds->box_y1;
        int bw = (ds->box_x0 < ds->box_x1 ? ds->box_x1 - ds->box_x0
                                            : ds->box_x0 - ds->box_x1);
        int bh = (ds->box_y0 < ds->box_y1 ? ds->box_y1 - ds->box_y0
                                            : ds->box_y0 - ds->box_y1);
        lvg_canvas_fill_rect(canvas, bx, by, bw, bh, ds->selection_box);
        lvg_canvas_stroke_rect(canvas, bx, by, bw, bh,
                                ds->key_selected, 1);
    }

    /* Border */
    lvg_canvas_stroke_rect(canvas, r.x, r.y, r.width, r.height,
                            ds->border_color, 1);
}

static int ds_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_dopesheet_t *ds = (lui_dopesheet_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    int grid_x = r.x + ds->header_width;

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;

        if (mx < grid_x) return 0;

        /* Check if clicking on a keyframe */
        int hs = ds->key_size / 2;
        for (int i = 0; i < ds->channel_count; i++) {
            int ry = r.y + ds->ruler_height + i * ds->row_height;
            if (my < ry || my >= ry + ds->row_height) continue;

            int cy = ry + ds->row_height / 2;
            for (int k = 0; k < ds->channels[i].key_count; k++) {
                int px = frame_to_px(ds, r, ds->channels[i].keys[k].frame);
                if (mx >= px - hs && mx <= px + hs &&
                    my >= cy - hs && my <= cy + hs) {
                    /* Toggle selection */
                    ds->channels[i].keys[k].selected =
                        !ds->channels[i].keys[k].selected;
                    ds->dragging = true;
                    ds->drag_start_frame = px_to_frame(ds, r, mx);
                    ds->drag_delta = 0;
                    return 1;
                }
            }
        }

        /* Start box selection */
        ds->box_selecting = true;
        ds->box_x0 = mx; ds->box_y0 = my;
        ds->box_x1 = mx; ds->box_y1 = my;
        return 1;
    }

    if (event->type == LUI_EVENT_MOUSE_MOVE) {
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;

        if (ds->box_selecting) {
            ds->box_x1 = mx;
            ds->box_y1 = my;
            return 1;
        }

        if (ds->dragging) {
            int frame = px_to_frame(ds, r, mx);
            ds->drag_delta = frame - ds->drag_start_frame;
            return 1;
        }

        /* Hover detection */
        ds->hovered_channel = -1;
        ds->hovered_key = -1;
        int hs = ds->key_size / 2;
        for (int i = 0; i < ds->channel_count; i++) {
            int ry = r.y + ds->ruler_height + i * ds->row_height;
            if (my < ry || my >= ry + ds->row_height) continue;
            ds->hovered_channel = i;
            int cy = ry + ds->row_height / 2;
            for (int k = 0; k < ds->channels[i].key_count; k++) {
                int px = frame_to_px(ds, r, ds->channels[i].keys[k].frame);
                if (mx >= px - hs && mx <= px + hs &&
                    my >= cy - hs && my <= cy + hs) {
                    ds->hovered_key = k;
                    break;
                }
            }
            break;
        }
        return ds->hovered_channel >= 0 ? 1 : 0;
    }

    if (event->type == LUI_EVENT_MOUSE_UP) {
        if (ds->dragging && ds->drag_delta != 0) {
            /* Apply drag to all selected keyframes */
            for (int i = 0; i < ds->channel_count; i++) {
                for (int k = 0; k < ds->channels[i].key_count; k++) {
                    if (ds->channels[i].keys[k].selected) {
                        ds->channels[i].keys[k].frame += ds->drag_delta;
                        if (ds->channels[i].keys[k].frame < 0)
                            ds->channels[i].keys[k].frame = 0;
                    }
                }
                sort_keys(&ds->channels[i]);
            }
            if (ds->on_change) ds->on_change(ds->on_change_user);
        }
        ds->dragging = false;
        ds->drag_delta = 0;

        if (ds->box_selecting) {
            /* Select keys inside box */
            int bx0 = ds->box_x0 < ds->box_x1 ? ds->box_x0 : ds->box_x1;
            int by0 = ds->box_y0 < ds->box_y1 ? ds->box_y0 : ds->box_y1;
            int bx1 = ds->box_x0 < ds->box_x1 ? ds->box_x1 : ds->box_x0;
            int by1 = ds->box_y0 < ds->box_y1 ? ds->box_y1 : ds->box_y0;

            for (int i = 0; i < ds->channel_count; i++) {
                int ry = r.y + ds->ruler_height + i * ds->row_height;
                int cy = ry + ds->row_height / 2;
                if (cy < by0 || cy > by1) continue;
                for (int k = 0; k < ds->channels[i].key_count; k++) {
                    int px = frame_to_px(ds, r, ds->channels[i].keys[k].frame);
                    if (px >= bx0 && px <= bx1)
                        ds->channels[i].keys[k].selected = true;
                }
            }
            ds->box_selecting = false;
        }
        return 0;
    }

    /* Delete selected keyframes */
    if (event->type == LUI_EVENT_KEY_DOWN &&
        (event->data.key.key == LUI_KEY_DELETE ||
         event->data.key.key == LUI_KEY_BACKSPACE)) {
        bool changed = false;
        for (int i = 0; i < ds->channel_count; i++) {
            int w_idx = 0;
            for (int k = 0; k < ds->channels[i].key_count; k++) {
                if (!ds->channels[i].keys[k].selected)
                    ds->channels[i].keys[w_idx++] = ds->channels[i].keys[k];
                else
                    changed = true;
            }
            ds->channels[i].key_count = w_idx;
        }
        if (changed && ds->on_change) ds->on_change(ds->on_change_user);
        return changed ? 1 : 0;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

static void *ds_default_alloc(void *u, size_t n) { (void)u; return malloc(n); }
static void  ds_default_free(void *u, void *p)    { (void)u; free(p); }

bool lui_dopesheet_init_ex(lui_dopesheet_t *ds, int max_channels,
                            lui_alloc_fn alloc_fn,
                            lui_free_fn  free_fn,
                            void        *alloc_user)
{
    if (!ds || max_channels <= 0) return false;
    if ((alloc_fn == NULL) != (free_fn == NULL)) return false;
    if (!alloc_fn) {
        alloc_fn   = ds_default_alloc;
        free_fn    = ds_default_free;
        alloc_user = NULL;
    }

    size_t bytes = (size_t)max_channels * sizeof(lui_dopesheet_channel_t);
    if (bytes / sizeof(lui_dopesheet_channel_t) != (size_t)max_channels) return false;
    lui_dopesheet_channel_t *channels =
        (lui_dopesheet_channel_t *)alloc_fn(alloc_user, bytes);
    if (!channels) return false;
    memset(channels, 0, bytes);

    memset(ds, 0, sizeof(*ds));
    ds->channels     = channels;
    ds->max_channels = max_channels;
    ds->alloc_fn     = alloc_fn;
    ds->free_fn      = free_fn;
    ds->alloc_user   = alloc_user;

    lui_widget_init(&ds->widget);
    ds->widget.width   = lvg_size_fill(1);
    ds->widget.height  = lvg_size_hug(0);
    ds->widget.measure = ds_measure;
    ds->widget.draw    = ds_draw;
    ds->widget.on_event = ds_event;
    ds->widget.flags   = LUI_WIDGET_FOCUSABLE;

    ds->channel_count = 0;
    ds->view_start = 0;
    ds->pixels_per_frame = 6.0f;
    ds->total_frames = 300;

    ds->hovered_channel = -1;
    ds->hovered_key = -1;
    ds->dragging = false;
    ds->drag_start_frame = 0;
    ds->drag_delta = 0;
    ds->box_selecting = false;
    ds->box_x0 = ds->box_y0 = ds->box_x1 = ds->box_y1 = 0;

    ds->header_width = 100;
    ds->row_height = 22;
    ds->key_size = 8;
    ds->ruler_height = 20;

    ds->bg            = LVG_COLOR_RGB(0x1E, 0x1E, 0x2E);
    ds->ruler_bg      = LVG_COLOR_RGB(0x24, 0x28, 0x30);
    ds->ruler_text    = LVG_COLOR_RGB(0x80, 0x84, 0x8A);
    ds->row_bg        = LVG_COLOR_RGB(0x22, 0x24, 0x2C);
    ds->row_bg_alt    = LVG_COLOR_RGB(0x26, 0x28, 0x30);
    ds->grid_color    = LVG_COLOR_RGB(0x30, 0x34, 0x3C);
    ds->key_color     = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    ds->key_selected  = LVG_COLOR_RGB(0xFF, 0xD0, 0x30);
    ds->key_hovered   = LVG_COLOR_RGB(0x80, 0xC0, 0xFF);
    ds->selection_box = LVG_COLOR_ARGB(0x30, 0x58, 0x9C, 0xE0);
    ds->text_color    = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    ds->border_color  = LVG_COLOR_RGB(0x40, 0x44, 0x4C);

    ds->on_change = NULL;
    ds->on_change_user = NULL;

    return true;
}

bool lui_dopesheet_init(lui_dopesheet_t *ds)
{
    return lui_dopesheet_init_ex(ds, LUI_DOPESHEET_MAX_CHANNELS,
                                  NULL, NULL, NULL);
}

void lui_dopesheet_destroy(lui_dopesheet_t *ds)
{
    if (!ds) return;
    if (ds->free_fn && ds->channels)
        ds->free_fn(ds->alloc_user, ds->channels);
    ds->channels      = NULL;
    ds->channel_count = 0;
    ds->max_channels  = 0;
}

int lui_dopesheet_add_channel(lui_dopesheet_t *ds, const char *label,
                               lvg_color_t color)
{
    if (!ds || !label || ds->channel_count >= ds->max_channels)
        return -1;

    lui_dopesheet_channel_t *ch = &ds->channels[ds->channel_count];
    int len = (int)strlen(label);
    if (len > LUI_DOPESHEET_MAX_LABEL) len = LUI_DOPESHEET_MAX_LABEL;
    memcpy(ch->label, label, len);
    ch->label[len] = '\0';
    ch->key_count = 0;
    ch->expanded = true;
    ch->color = color;

    return ds->channel_count++;
}

int lui_dopesheet_add_key(lui_dopesheet_t *ds, int channel, int frame)
{
    if (!ds || channel < 0 || channel >= ds->channel_count) return -1;
    lui_dopesheet_channel_t *ch = &ds->channels[channel];
    if (ch->key_count >= LUI_DOPESHEET_MAX_KEYFRAMES) return -1;

    int idx = ch->key_count;
    ch->keys[idx].frame = frame;
    ch->keys[idx].selected = false;
    ch->key_count++;
    sort_keys(ch);
    return idx;
}

void lui_dopesheet_remove_key(lui_dopesheet_t *ds, int channel, int key_index)
{
    if (!ds || channel < 0 || channel >= ds->channel_count) return;
    lui_dopesheet_channel_t *ch = &ds->channels[channel];
    if (key_index < 0 || key_index >= ch->key_count) return;

    for (int i = key_index; i < ch->key_count - 1; i++)
        ch->keys[i] = ch->keys[i + 1];
    ch->key_count--;
}

void lui_dopesheet_clear_channel(lui_dopesheet_t *ds, int channel)
{
    if (!ds || channel < 0 || channel >= ds->channel_count) return;
    ds->channels[channel].key_count = 0;
}

void lui_dopesheet_set_view(lui_dopesheet_t *ds, int start_frame,
                             float pixels_per_frame)
{
    if (!ds) return;
    ds->view_start = start_frame;
    if (pixels_per_frame > 0.1f)
        ds->pixels_per_frame = pixels_per_frame;
}

void lui_dopesheet_select_range(lui_dopesheet_t *ds, int frame_start,
                                 int frame_end)
{
    if (!ds) return;
    if (frame_start > frame_end) {
        int tmp = frame_start; frame_start = frame_end; frame_end = tmp;
    }
    for (int i = 0; i < ds->channel_count; i++)
        for (int k = 0; k < ds->channels[i].key_count; k++)
            if (ds->channels[i].keys[k].frame >= frame_start &&
                ds->channels[i].keys[k].frame <= frame_end)
                ds->channels[i].keys[k].selected = true;
}

void lui_dopesheet_deselect_all(lui_dopesheet_t *ds)
{
    if (!ds) return;
    for (int i = 0; i < ds->channel_count; i++)
        for (int k = 0; k < ds->channels[i].key_count; k++)
            ds->channels[i].keys[k].selected = false;
}

int lui_dopesheet_frame_to_x(const lui_dopesheet_t *ds, int frame)
{
    if (!ds) return 0;
    return ds->header_width +
           (int)((frame - ds->view_start) * ds->pixels_per_frame);
}

int lui_dopesheet_x_to_frame(const lui_dopesheet_t *ds, int x)
{
    if (!ds) return 0;
    return ds->view_start +
           (int)((float)(x - ds->header_width) / ds->pixels_per_frame + 0.5f);
}
