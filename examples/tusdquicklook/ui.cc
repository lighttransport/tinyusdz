// SPDX-License-Identifier: Apache-2.0
#include "ui.hh"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace tusdql {

const Theme& DefaultTheme() {
  static const Theme th = [] {
    Theme t{};
    t.bg = LVG_COLOR_RGB(0x1e, 0x20, 0x24);
    t.panel = LVG_COLOR_RGB(0x26, 0x29, 0x2e);
    t.panel_alt = LVG_COLOR_RGB(0x2b, 0x2e, 0x34);
    t.selection = LVG_COLOR_RGB(0x35, 0x4a, 0x66);
    t.border = LVG_COLOR_RGB(0x14, 0x15, 0x18);
    t.text = LVG_COLOR_RGB(0xd8, 0xdc, 0xe0);
    t.text_dim = LVG_COLOR_RGB(0x8a, 0x90, 0x98);
    t.accent = LVG_COLOR_RGB(0x62, 0xa0, 0xea);
    t.warn = LVG_COLOR_RGB(0xe8, 0xb3, 0x39);
    t.error = LVG_COLOR_RGB(0xe0, 0x6c, 0x6c);
    t.viewport_top = LVG_COLOR_RGB(0x3a, 0x3f, 0x47);
    t.viewport_bottom = LVG_COLOR_RGB(0x1a, 0x1c, 0x20);
    t.pad = 8;
    t.row_h = 22;
    t.statusbar_h = 24;
    t.toolbar_h = 26;
    t.font_px = 14;
    return t;
  }();
  return th;
}

int ClampPaneWidth(int requested, int surf_w) {
  const int kMin = 160;
  const int kMax = 520;
  int max_allowed = std::max(kMin, surf_w / 2);
  int hi = std::min(kMax, max_allowed);
  return std::max(kMin, std::min(requested, hi));
}

Layout ComputeLayout(const Theme& th, int surf_w, int surf_h,
                     int left_pane_w) {
  const int kSplitterW = 4;
  Layout l{};
  const int pane_w = ClampPaneWidth(left_pane_w, surf_w);

  l.toolbar = lvg_rect_make(0, 0, surf_w, th.toolbar_h);

  const int body_y = th.toolbar_h;
  const int body_h = std::max(0, surf_h - th.toolbar_h - th.statusbar_h);

  l.list = lvg_rect_make(0, body_y, pane_w, body_h);
  l.splitter = lvg_rect_make(pane_w, body_y, kSplitterW, body_h);

  const int vp_x = pane_w + kSplitterW;
  l.viewport = lvg_rect_make(vp_x, body_y, std::max(0, surf_w - vp_x), body_h);

  l.statusbar =
      lvg_rect_make(0, surf_h - th.statusbar_h, surf_w, th.statusbar_h);
  return l;
}

void FillRect(lvg_canvas_t* c, const lvg_rect_t& r, lvg_color_t color) {
  if (r.width <= 0 || r.height <= 0) return;
  lvg_canvas_fill_rect(c, r.x, r.y, r.width, r.height, color);
}

void StrokeRect(lvg_canvas_t* c, const lvg_rect_t& r, lvg_color_t color) {
  if (r.width <= 0 || r.height <= 0) return;
  lvg_canvas_stroke_rect(c, r.x, r.y, r.width, r.height, color, 1);
}

void FillRectVGradient(lvg_canvas_t* c, const lvg_rect_t& r, lvg_color_t top,
                       lvg_color_t bottom) {
  if (r.width <= 0 || r.height <= 0) return;
  // One fill_rect per scanline. The viewport is repainted only when the
  // renderer has nothing to show yet, so this is not on the hot path.
  for (int y = 0; y < r.height; y++) {
    const float t =
        (r.height > 1) ? (static_cast<float>(y) / static_cast<float>(r.height - 1))
                       : 0.0f;
    lvg_canvas_fill_rect(c, r.x, r.y + y, r.width, 1,
                         lvg_color_lerp(top, bottom, t));
  }
}

int DrawTextEllipsized(lvg_canvas_t* c, lui_font_t* font, int x, int baseline_y,
                       int max_w, const std::string& text, lvg_color_t color) {
  if (!font || text.empty() || max_w <= 0) return 0;

  const int full = lui_font_measure_text(font, text.c_str(), -1);
  if (full <= max_w) {
    lui_canvas_draw_text(c, x, baseline_y, text.c_str(), -1, font, color);
    return full;
  }

  static const char kEllipsis[] = "...";
  const int ell_w = lui_font_measure_text(font, kEllipsis, -1);
  if (ell_w > max_w) return 0;

  // Trim by bytes, stepping back over UTF-8 continuation bytes so we never
  // split a multi-byte sequence.
  size_t len = text.size();
  while (len > 0) {
    do {
      len--;
    } while (len > 0 &&
             (static_cast<unsigned char>(text[len]) & 0xC0) == 0x80);
    const int w = lui_font_measure_text(font, text.c_str(),
                                        static_cast<int>(len));
    if (w + ell_w <= max_w) break;
  }

  std::string out = text.substr(0, len) + kEllipsis;
  lui_canvas_draw_text(c, x, baseline_y, out.c_str(), -1, font, color);
  return lui_font_measure_text(font, out.c_str(), -1);
}

int DrawTextRight(lvg_canvas_t* c, lui_font_t* font, int right_x,
                  int baseline_y, const std::string& text, lvg_color_t color) {
  if (!font || text.empty()) return 0;
  const int w = lui_font_measure_text(font, text.c_str(), -1);
  lui_canvas_draw_text(c, right_x - w, baseline_y, text.c_str(), -1, font,
                       color);
  return w;
}

void DrawTextCentered(lvg_canvas_t* c, lui_font_t* font, const lvg_rect_t& r,
                      const std::string& text, lvg_color_t color) {
  if (!font || text.empty() || r.width <= 0 || r.height <= 0) return;
  const int w = lui_font_measure_text(font, text.c_str(), -1);
  const int x = r.x + (r.width - w) / 2;
  const int baseline =
      r.y + r.height / 2 + lui_font_ascent(font) / 2;
  DrawTextEllipsized(c, font, x, baseline, r.width, text, color);
}

void DrawTextWrappedCentered(lvg_canvas_t* c, lui_font_t* font,
                             const lvg_rect_t& r, const std::string& text,
                             lvg_color_t color, int max_lines) {
  if (!font || text.empty() || r.width <= 0 || r.height <= 0) return;
  if (max_lines < 1) max_lines = 1;

  // Greedy wrap on spaces. A single word longer than the line is left to the
  // per-line ellipsizer.
  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos < text.size() && static_cast<int>(lines.size()) < max_lines) {
    const bool last_line = static_cast<int>(lines.size()) == max_lines - 1;
    if (last_line) {
      lines.push_back(text.substr(pos));
      pos = text.size();
      break;
    }

    size_t take = 0;       // bytes committed to this line
    size_t scan = pos;
    while (scan < text.size()) {
      const size_t sp = text.find(' ', scan);
      const size_t end = (sp == std::string::npos) ? text.size() : sp;
      const int w = lui_font_measure_text(font, text.c_str() + pos,
                                          static_cast<int>(end - pos));
      if (w > r.width && take > 0) break;
      take = end - pos;
      if (sp == std::string::npos) {
        scan = text.size();
        break;
      }
      scan = sp + 1;
    }
    if (take == 0) take = text.size() - pos;  // unbreakable: emit and ellipsize

    lines.push_back(text.substr(pos, take));
    pos += take;
    while (pos < text.size() && text[pos] == ' ') pos++;
  }

  const int line_h = lui_font_line_height(font);
  const int block_h = static_cast<int>(lines.size()) * line_h;
  int baseline = r.y + (r.height - block_h) / 2 + lui_font_ascent(font);

  for (const std::string& line : lines) {
    const int w = lui_font_measure_text(font, line.c_str(), -1);
    const int x = r.x + (r.width - std::min(w, r.width)) / 2;
    DrawTextEllipsized(c, font, x, baseline, r.width, line, color);
    baseline += line_h;
  }
}

std::string FormatBytes(uint64_t bytes) {
  char buf[64];
  const double b = static_cast<double>(bytes);
  if (bytes >= (1ull << 30)) {
    std::snprintf(buf, sizeof(buf), "%.1f GB", b / (1024.0 * 1024.0 * 1024.0));
  } else if (bytes >= (1ull << 20)) {
    std::snprintf(buf, sizeof(buf), "%.0f MB", b / (1024.0 * 1024.0));
  } else if (bytes >= (1ull << 10)) {
    std::snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
  } else {
    std::snprintf(buf, sizeof(buf), "%llu B",
                  static_cast<unsigned long long>(bytes));
  }
  return buf;
}

std::string FormatCount(uint64_t n) {
  char raw[32];
  std::snprintf(raw, sizeof(raw), "%llu", static_cast<unsigned long long>(n));
  std::string s(raw);
  std::string out;
  out.reserve(s.size() + s.size() / 3);
  const size_t lead = s.size() % 3 ? s.size() % 3 : 3;
  for (size_t i = 0; i < s.size(); i++) {
    if (i && (i - lead) % 3 == 0 && i >= lead) out.push_back(',');
    out.push_back(s[i]);
  }
  return out;
}

}  // namespace tusdql
