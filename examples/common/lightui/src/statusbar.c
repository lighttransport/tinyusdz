/*
 * statusbar.c — Application status bar widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/statusbar.h>
#include <lightvg/canvas.h>

#include <string.h>

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int statusbar_measure(const lui_widget_t *w, int *out_w, int *out_h,
                               void *user)
{
    const lui_statusbar_t *sb = (const lui_statusbar_t *)w;
    (void)user;
    *out_w = 400;
    *out_h = sb->bar_height;
    return 0;
}

static void statusbar_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_statusbar_t *sb = (lui_statusbar_t *)w;
    lvg_rect_t wr = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&wr)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, wr.x, wr.y, wr.width, wr.height,
                          sb->bg_color);

    /* Top border */
    lvg_canvas_fill_rect(canvas, wr.x, wr.y, wr.width, 1, sb->border_color);

    if (sb->section_count <= 0) return;

    /* Compute total fixed width and count of auto-stretch sections */
    int fixed_total = 0;
    int stretch_count = 0;
    for (int i = 0; i < sb->section_count; i++) {
        if (sb->sections[i].width > 0)
            fixed_total += sb->sections[i].width;
        else
            stretch_count++;
    }

    /* Separators consume space */
    int sep_total = (sb->section_count - 1) * sb->separator_width;
    int avail = wr.width - fixed_total - sep_total;
    int stretch_w = stretch_count > 0 ? avail / stretch_count : 0;
    if (stretch_w < 0) stretch_w = 0;

    int sx = wr.x;
    for (int i = 0; i < sb->section_count; i++) {
        int sec_w = sb->sections[i].width > 0
                  ? sb->sections[i].width : stretch_w;

        /* Draw text in section */
        int text_len = sb->sections[i].text_len;
        int text_w = text_len * 7;
        int tx;

        switch (sb->sections[i].alignment) {
        case LUI_STATUSBAR_ALIGN_CENTER:
            tx = sx + (sec_w - text_w) / 2;
            break;
        case LUI_STATUSBAR_ALIGN_RIGHT:
            tx = sx + sec_w - text_w - 4;
            break;
        default: /* LEFT */
            tx = sx + 4;
            break;
        }

        int ty = wr.y + (wr.height - 10) / 2;
        for (int c = 0; c < text_len && tx + 5 <= sx + sec_w; c++) {
            lvg_canvas_fill_rect(canvas, tx, ty, 5, 10, sb->text_color);
            tx += 7;
        }

        sx += sec_w;

        /* Draw separator (except after last section) */
        if (i < sb->section_count - 1) {
            lvg_canvas_fill_rect(canvas, sx, wr.y + 2,
                                  sb->separator_width, wr.height - 4,
                                  sb->separator_color);
            sx += sb->separator_width;
        }
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_statusbar_init(lui_statusbar_t *sb)
{
    if (!sb) return;

    lui_widget_init(&sb->widget);
    sb->widget.width    = lvg_size_fill(1);
    sb->widget.height   = lvg_size_hug(0);
    sb->widget.measure  = statusbar_measure;
    sb->widget.draw     = statusbar_draw;

    sb->section_count   = 0;
    sb->bar_height      = 22;
    sb->separator_width = 1;

    sb->bg_color        = LVG_COLOR_RGB(0x22, 0x25, 0x29);
    sb->text_color      = LVG_COLOR_RGB(0xB0, 0xB3, 0xB7);
    sb->separator_color = LVG_COLOR_RGB(0x40, 0x44, 0x4C);
    sb->border_color    = LVG_COLOR_RGB(0x40, 0x44, 0x4C);
}

int lui_statusbar_add_section(lui_statusbar_t *sb, const char *text, int width)
{
    if (!sb || sb->section_count >= LUI_STATUSBAR_MAX_SECTIONS)
        return -1;

    lui_statusbar_section_t *sec = &sb->sections[sb->section_count];
    sec->width     = width;
    sec->alignment = LUI_STATUSBAR_ALIGN_LEFT;

    if (text) {
        int len = (int)strlen(text);
        if (len > 127) len = 127;
        memcpy(sec->text, text, len);
        sec->text[len] = '\0';
        sec->text_len  = len;
    } else {
        sec->text[0]   = '\0';
        sec->text_len  = 0;
    }

    return sb->section_count++;
}

void lui_statusbar_set_text(lui_statusbar_t *sb, int section, const char *text)
{
    if (!sb || section < 0 || section >= sb->section_count || !text)
        return;

    int len = (int)strlen(text);
    if (len > 127) len = 127;
    memcpy(sb->sections[section].text, text, len);
    sb->sections[section].text[len] = '\0';
    sb->sections[section].text_len  = len;
}
