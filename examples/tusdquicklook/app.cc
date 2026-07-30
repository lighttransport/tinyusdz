// SPDX-License-Identifier: Apache-2.0
#include "app.hh"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

extern "C" {
#include "hack_regular_ttf.h"
}

namespace tusdql {

App::App(const Options& opts) : opts_(opts), theme_(DefaultTheme()) {}

App::~App() {
  if (font_) {
    lui_font_destroy(font_);
    font_ = nullptr;
  }
  if (window_) {
    lui_window_destroy(window_);
    window_ = nullptr;
  }
  if (platform_up_) {
    lui_shutdown();
    platform_up_ = false;
  }
}

bool App::Init() {
  // Fonts, canvas and surfaces are all platform-independent; only windows need
  // the platform layer, and lui_init() opens the display. Skipping it when
  // headless is what lets --screenshot work over ssh and in CI with no X.
  headless_ = !opts_.screenshot.empty();
  if (!headless_) {
    if (!lui_init()) {
      err_ =
          "lui_init() failed (no display? use --screenshot for headless "
          "rendering)";
      return false;
    }
    platform_up_ = true;
  }

  font_ = lui_font_create_from_memory(lui_embedded_font_data(),
                                      lui_embedded_font_size(),
                                      theme_.font_px);
  if (!font_) {
    err_ = "failed to create the embedded font";
    return false;
  }

  // Install the process cap first: everything downstream (browser projections,
  // loader, renderer) sizes itself against the *clamped* value, which may be
  // lower than requested on a small machine.
  budget_ = PreviewBudget::Install(opts_.max_mem_bytes);
  if (budget_.total != opts_.max_mem_bytes && opts_.verbose) {
    std::fprintf(stderr,
                 "[tusdquicklook] memory budget clamped to %s "
                 "(requested %s, limited by available system memory)\n",
                 FormatBytes(budget_.total).c_str(),
                 FormatBytes(opts_.max_mem_bytes).c_str());
  }

  browser_.SetRecursive(opts_.recursive);
  browser_.SetMemoryBudget(budget_.total);

  std::string berr;
  if (!browser_.Open(opts_.path, &berr)) {
    err_ = berr;
    return false;
  }
  status_left_ = browser_.dir();

  if (const FileEntry* sel = browser_.SelectedEntry()) {
    // Headless runs the load synchronously in RunHeadless(); starting a worker
    // here would just race it.
    if (headless_) {
      viewport_message_ = sel->name;
      if (sel->over_budget) {
        viewport_message_ = sel->name + " — too large for quick look (projected " +
                            FormatBytes(sel->projected_bytes) + ", budget " +
                            FormatBytes(budget_.total) + ")";
        viewport_message_is_error_ = true;
      }
    } else {
      PreviewFile(*sel);
    }
  } else {
    viewport_message_ = "no USD files in this folder";
  }

  if (headless_) {
    surf_w_ = opts_.width;
    surf_h_ = opts_.height;
    return true;
  }

  window_ = lui_window_create("tusdquicklook", opts_.width, opts_.height,
                              LUI_WINDOW_RESIZABLE);
  if (!window_) {
    err_ =
        "failed to create a window (no display? try --screenshot for headless "
        "rendering)";
    return false;
  }
  lui_window_get_physical_size(window_, &surf_w_, &surf_h_);
  lui_window_show(window_);
  return true;
}

bool App::Busy() const {
  // While a load is in flight, or the image has not converged, the loop polls
  // instead of blocking so results appear as they arrive rather than on the
  // next mouse move.
  if (loader_.running() || !scene_complete_) return true;
  return renderer_ && !scene_.meshes.empty() && !render_status_.converged;
}

int App::Run() {
  // Paint BEFORE blocking for input, not after. The obvious ordering (wait for
  // an event, then draw) never paints the first frame: at startup there is
  // nothing pending, so the loop blocks in wait_event and the window stays
  // blank until the user happens to move the mouse. Headless --screenshot runs
  // cannot catch that, since they never touch the event loop.
  while (!quit_) {
    if (DrainLoadEvents()) needs_redraw_ = true;

    // Progressive refinement: keep repainting until the image converges.
    if (renderer_ && !scene_.meshes.empty() && !render_status_.converged) {
      needs_redraw_ = true;
    }

    if (needs_redraw_) {
      lvg_surface_t* surf = lui_window_get_surface(window_);
      if (surf) {
        surf_w_ = surf->width;
        surf_h_ = surf->height;
        DrawFrame(surf);
        lui_window_present(window_);
      }
      needs_redraw_ = false;
    }

    lui_event_t ev;
    if (Busy()) {
      bool got_event = false;
      while (lui_window_poll_event(window_, &ev)) {
        got_event = true;
        if (!HandleEvent(ev)) {
          quit_ = true;
          break;
        }
      }
      // Nothing to do this tick: yield rather than spin. A previewer that pins
      // a core while it waits on a worker would defeat the point.
      if (!got_event && !needs_redraw_ && !StreamHasWork()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
      }
    } else {
      // Idle and converged: block until something actually happens.
      if (!lui_window_wait_event(window_, &ev)) break;
      if (!HandleEvent(ev)) break;
      // Coalesce whatever else arrived with it (mouse-move floods especially).
      while (lui_window_poll_event(window_, &ev)) {
        if (!HandleEvent(ev)) {
          quit_ = true;
          break;
        }
      }
    }
  }
  return 0;
}

int App::RunHeadless() {
  lvg_surface_t* surf = lvg_surface_create(surf_w_, surf_h_);
  if (!surf) {
    err_ = "failed to allocate the offscreen surface";
    return 1;
  }

  // Run the load on this thread rather than a worker: headless output must be
  // deterministic, not a race between the loader and a frame counter.
  if (const FileEntry* sel = browser_.SelectedEntry()) {
    if (!sel->is_dir && !sel->over_budget) {
      auto ctrl = std::make_shared<LoadControl>();
      ctrl->Reset();
      ctrl->max_stage_bytes = budget_.stage;
      ctrl->max_geometry_bytes = budget_.geometry;
      ctrl->max_texture_bytes = budget_.textures;
      // Unbounded queue: nothing is draining concurrently, so a byte bound
      // here would deadlock the single-threaded run.
      auto stream = std::make_shared<LoadStream>(UINT64_MAX);
      RunLoad(sel->path, opts_, budget_, ctrl, stream);

      LoadEvent ev;
      while (stream->TryPop(&ev)) {
        ApplyLoadEvent(std::move(ev));
        ev = LoadEvent{};
      }
      UpdateStatus();
    }
  }

  for (int i = 0; i < opts_.frames; i++) {
    DrawFrame(surf);
  }

  const int rc = lvg_surface_save_png(surf, opts_.screenshot.c_str());
  lvg_surface_destroy(surf);
  if (rc != 0) {
    err_ = "failed to write " + opts_.screenshot;
    return 1;
  }
  if (opts_.verbose) {
    std::fprintf(stderr,
                 "[tusdquicklook] wrote %s (%dx%d) spp=%d/%d tiles=%d/%d "
                 "converged=%d\n",
                 opts_.screenshot.c_str(), surf_w_, surf_h_,
                 render_status_.samples_done, render_status_.samples_target,
                 render_status_.tiles_done, render_status_.tiles_total,
                 render_status_.converged ? 1 : 0);
  }
  return 0;
}

bool App::HandleEvent(const lui_event_t& ev) {
  switch (ev.type) {
    case LUI_EVENT_QUIT:
      return false;

    case LUI_EVENT_WINDOW_EXPOSE:
      needs_redraw_ = true;
      break;

    case LUI_EVENT_WINDOW_RESIZE:
      needs_redraw_ = true;
      break;

    case LUI_EVENT_KEY_DOWN: {
      if (ev.data.key.key == LUI_KEY_ESCAPE) return false;
      if ((ev.data.key.mods & LUI_MOD_CTRL) && ev.data.key.key == 'q') {
        return false;
      }
      const int rows = VisibleRows();
      switch (ev.data.key.key) {
        case LUI_KEY_UP:
          if (browser_.MoveSelection(-1)) {
            browser_.EnsureSelectionVisible(rows);
            if (const FileEntry* s = browser_.SelectedEntry()) PreviewFile(*s);
            needs_redraw_ = true;
          }
          break;
        case LUI_KEY_DOWN:
          if (browser_.MoveSelection(1)) {
            browser_.EnsureSelectionVisible(rows);
            if (const FileEntry* s = browser_.SelectedEntry()) PreviewFile(*s);
            needs_redraw_ = true;
          }
          break;
        case LUI_KEY_PAGE_UP:
          if (browser_.MoveSelection(-std::max(1, rows - 1))) {
            browser_.EnsureSelectionVisible(rows);
            if (const FileEntry* s = browser_.SelectedEntry()) PreviewFile(*s);
            needs_redraw_ = true;
          }
          break;
        case LUI_KEY_PAGE_DOWN:
          if (browser_.MoveSelection(std::max(1, rows - 1))) {
            browser_.EnsureSelectionVisible(rows);
            if (const FileEntry* s = browser_.SelectedEntry()) PreviewFile(*s);
            needs_redraw_ = true;
          }
          break;
        case LUI_KEY_HOME:
          if (browser_.Select(0)) {
            browser_.EnsureSelectionVisible(rows);
            if (const FileEntry* s = browser_.SelectedEntry()) PreviewFile(*s);
            needs_redraw_ = true;
          }
          break;
        case LUI_KEY_END:
          if (browser_.Select(browser_.RowCount() - 1)) {
            browser_.EnsureSelectionVisible(rows);
            if (const FileEntry* s = browser_.SelectedEntry()) PreviewFile(*s);
            needs_redraw_ = true;
          }
          break;
        case LUI_KEY_RETURN:
          ActivateSelection();
          browser_.EnsureSelectionVisible(rows);
          break;
        default:
          if (ev.data.key.key == 'r' && !(ev.data.key.mods & LUI_MOD_CTRL)) {
            std::string berr;
            if (browser_.Refresh(&berr)) needs_redraw_ = true;
          } else if (ev.data.key.key == 'f') {
            // Give the camera back to auto-framing.
            camera_user_controlled_ = false;
            camera_framed_ = false;
            needs_redraw_ = true;
          }
          break;
      }
      break;
    }

    case LUI_EVENT_SCROLL: {
      const Layout l = ComputeLayout(theme_, surf_w_, surf_h_, left_pane_w_);
      const int x = ev.data.scroll.x;
      const int y = ev.data.scroll.y;
      if (lvg_rect_contains_point(&l.list, x, y)) {
        // delta_y > 0 scrolls down.
        const int rows_delta =
            static_cast<int>(ev.data.scroll.delta_y > 0.0f ? 3 : -3);
        if (ev.data.scroll.delta_y != 0.0f &&
            browser_.ScrollBy(rows_delta, VisibleRows())) {
          needs_redraw_ = true;
        }
      } else if (lvg_rect_contains_point(&l.viewport, x, y) &&
                 ev.data.scroll.delta_y != 0.0f) {
        camera_user_controlled_ = true;
        camera_.Dolly(ev.data.scroll.delta_y > 0.0f ? 1.12f : 1.0f / 1.12f);
        needs_redraw_ = true;
      }
      break;
    }

    case LUI_EVENT_MOUSE_DOWN: {
      const Layout l = ComputeLayout(theme_, surf_w_, surf_h_, left_pane_w_);
      const int x = ev.data.mouse_button.x;
      const int y = ev.data.mouse_button.y;
      if (lvg_rect_contains_point(&l.splitter, x, y)) {
        dragging_splitter_ = true;
        drag_origin_x_ = x;
        drag_origin_w_ = left_pane_w_;
      } else if (lvg_rect_contains_point(&l.viewport, x, y)) {
        // Left drag orbits, middle/right drag pans.
        drag_mode_ = (ev.data.mouse_button.button == LUI_MOUSE_LEFT)
                         ? DragMode::Orbit
                         : DragMode::Pan;
        drag_last_x_ = x;
        drag_last_y_ = y;
      } else if (lvg_rect_contains_point(&l.list, x, y)) {
        const int row = browser_.scroll() + (y - l.list.y) / theme_.row_h;
        if (row >= 0 && row < browser_.RowCount()) {
          const bool changed = browser_.Select(row);
          // A double-click on a directory descends; on a file it re-previews,
          // which is harmless.
          if (ev.data.mouse_button.clicks >= 2) {
            ActivateSelection();
          } else if (changed) {
            if (const FileEntry* s = browser_.SelectedEntry()) PreviewFile(*s);
          }
          needs_redraw_ = true;
        }
      }
      break;
    }

    case LUI_EVENT_MOUSE_UP:
      dragging_splitter_ = false;
      drag_mode_ = DragMode::None;
      break;

    case LUI_EVENT_MOUSE_MOVE:
      if (dragging_splitter_) {
        const int delta = ev.data.mouse_move.x - drag_origin_x_;
        const int w = ClampPaneWidth(drag_origin_w_ + delta, surf_w_);
        if (w != left_pane_w_) {
          left_pane_w_ = w;
          needs_redraw_ = true;
        }
      } else if (drag_mode_ != DragMode::None) {
        const int dx = ev.data.mouse_move.x - drag_last_x_;
        const int dy = ev.data.mouse_move.y - drag_last_y_;
        drag_last_x_ = ev.data.mouse_move.x;
        drag_last_y_ = ev.data.mouse_move.y;
        if (dx != 0 || dy != 0) {
          camera_user_controlled_ = true;
          const Layout l = ComputeLayout(theme_, surf_w_, surf_h_, left_pane_w_);
          if (drag_mode_ == DragMode::Orbit) {
            // ~half a turn across the viewport width.
            const float scale = 3.14159265f / std::max(1, l.viewport.width);
            camera_.Orbit(-float(dx) * scale, float(dy) * scale);
          } else {
            camera_.Pan(float(dx), float(dy), l.viewport.height);
          }
          needs_redraw_ = true;
        }
      }
      break;

    default:
      break;
  }
  return true;
}

void App::DrawFrame(lvg_surface_t* surf) {
  lvg_canvas_t canvas;
  lvg_canvas_init(&canvas, surf);

  const Layout l = ComputeLayout(theme_, surf->width, surf->height,
                                 left_pane_w_);

  lvg_canvas_clear(&canvas, theme_.bg);
  DrawToolbar(&canvas, l.toolbar);
  DrawListPane(&canvas, l.list);
  DrawSplitter(&canvas, l.splitter);
  DrawViewport(&canvas, l.viewport);
  DrawStatusBar(&canvas, l.statusbar);

  lvg_canvas_flush(&canvas);
}

void App::DrawToolbar(lvg_canvas_t* c, const lvg_rect_t& r) {
  FillRect(c, r, theme_.panel);
  lvg_canvas_fill_rect(c, r.x, r.y + r.height - 1, r.width, 1, theme_.border);

  const int baseline = r.y + (r.height + lui_font_ascent(font_)) / 2 - 2;
  DrawTextEllipsized(c, font_, r.x + theme_.pad, baseline,
                     r.width - 2 * theme_.pad, status_left_, theme_.text);
}

void App::DrawListPane(lvg_canvas_t* c, const lvg_rect_t& r) {
  FillRect(c, r, theme_.panel);

  const auto& entries = browser_.entries();
  if (entries.empty()) {
    DrawTextCentered(c, font_, r, "(empty folder)", theme_.text_dim);
    return;
  }

  lvg_canvas_set_clip(c, &r);

  const int rows = VisibleRows();
  const int first = browser_.scroll();
  const int ascent = lui_font_ascent(font_);

  for (int i = 0; i < rows; i++) {
    const int idx = first + i;
    if (idx >= static_cast<int>(entries.size())) break;
    const FileEntry& e = entries[static_cast<size_t>(idx)];

    const lvg_rect_t row =
        lvg_rect_make(r.x, r.y + i * theme_.row_h, r.width, theme_.row_h);

    if (idx == browser_.selected()) {
      FillRect(c, row, theme_.selection);
    } else if (idx & 1) {
      FillRect(c, row, theme_.panel_alt);
    }

    const int baseline = row.y + (theme_.row_h + ascent) / 2 - 2;

    // Right-hand column: size for files, nothing for directories.
    int size_w = 0;
    if (!e.is_dir) {
      const lvg_color_t size_color = e.over_budget ? theme_.warn
                                                   : theme_.text_dim;
      size_w = DrawTextRight(c, font_, row.x + row.width - theme_.pad, baseline,
                             FormatBytes(e.size), size_color);
      size_w += theme_.pad;
    }

    const std::string label = e.is_dir ? (e.name + "/") : e.name;
    lvg_color_t name_color = theme_.text;
    if (e.is_dir) {
      name_color = theme_.accent;
    } else if (e.over_budget) {
      name_color = theme_.warn;
    }

    const int name_max = row.width - 2 * theme_.pad - size_w;
    DrawTextEllipsized(c, font_, row.x + theme_.pad, baseline, name_max, label,
                       name_color);
  }

  lvg_canvas_reset_clip(c);

  // Scroll indicator: a thin thumb on the right edge, drawn only when the list
  // does not fit.
  const int total = static_cast<int>(entries.size());
  if (total > rows && rows > 0) {
    const int track_h = r.height;
    int thumb_h = std::max(16, track_h * rows / total);
    const int max_first = total - rows;
    const int thumb_y =
        r.y + (max_first > 0 ? (track_h - thumb_h) * first / max_first : 0);
    lvg_canvas_fill_rect(c, r.x + r.width - 3, thumb_y, 2, thumb_h,
                         theme_.text_dim);
  }
}

int App::VisibleRows() const {
  const Layout l = ComputeLayout(theme_, surf_w_, surf_h_, left_pane_w_);
  return std::max(0, l.list.height / theme_.row_h);
}

void App::ActivateSelection() {
  const FileEntry* sel = browser_.SelectedEntry();
  if (!sel) return;

  if (sel->is_dir) {
    std::string berr;
    if (browser_.DescendSelected(&berr)) {
      status_left_ = browser_.dir();
      viewport_message_.clear();
      viewport_message_is_error_ = false;
      if (const FileEntry* next = browser_.SelectedEntry()) {
        PreviewFile(*next);
      } else {
        viewport_message_ = "no USD files in this folder";
      }
    } else if (!berr.empty()) {
      viewport_message_ = berr;
      viewport_message_is_error_ = true;
    }
    needs_redraw_ = true;
    return;
  }

  PreviewFile(*sel);
  needs_redraw_ = true;
}

void App::PreviewFile(const FileEntry& entry) {
  if (entry.is_dir) return;

  viewport_message_is_error_ = false;

  if (entry.over_budget) {
    // Refused before opening: no allocation, no partial load. The user still
    // gets a specific reason and the numbers behind it.
    viewport_message_ = entry.name + " — too large for quick look (projected " +
                        FormatBytes(entry.projected_bytes) + ", budget " +
                        FormatBytes(budget_.total) + ")";
    viewport_message_is_error_ = true;
    status_right_ = "skipped (over budget)";
    return;
  }

  scene_.Clear();
  scene_complete_ = false;
  camera_user_controlled_ = false;
  camera_framed_ = false;
  framed_at_mesh_count_ = 0;
  if (renderer_) renderer_->SetScene(&scene_);
  viewport_message_ = entry.name;
  loader_.Start(entry.path, opts_, budget_);
  UpdateStatus();
}

bool App::StreamHasWork() const {
  LoadStream* stream = const_cast<App*>(this)->loader_.stream();
  return stream && stream->queued_bytes() > 0;
}

bool App::DrainLoadEvents() {
  LoadStream* stream = loader_.stream();
  if (!stream) return false;

  bool changed = false;
  LoadEvent ev;
  while (stream->TryPop(&ev)) {
    ApplyLoadEvent(std::move(ev));
    ev = LoadEvent{};
    changed = true;
  }

  if (changed) UpdateStatus();
  return changed;
}

void App::ApplyLoadEvent(LoadEvent&& ev) {
  switch (ev.kind) {
    case LoadEvent::Kind::Progress:
      break;

    case LoadEvent::Kind::Resources:
      scene_.materials = std::move(ev.materials);
      scene_.textures = std::move(ev.textures);
      scene_.lights = std::move(ev.lights);
      scene_.cameras = std::move(ev.cameras);
      scene_.stats.material_count = scene_.materials.size();
      scene_.stats.texture_count = scene_.textures.size();
      scene_.stats.light_count = scene_.lights.size();
      break;

    case LoadEvent::Kind::Mesh:
      scene_.stats.triangle_count += ev.mesh.triangle_count();
      scene_.stats.vertex_count += ev.mesh.vertex_count();
      scene_.stats.geometry_bytes += ev.mesh.byte_size();
      scene_.bounds.Expand(ev.mesh.bounds);
      scene_.meshes.push_back(std::move(ev.mesh));
      scene_.stats.mesh_count = scene_.meshes.size();
      break;

    case LoadEvent::Kind::Bounds:
      if (ev.bounds.valid) scene_.bounds = ev.bounds;
      scene_.y_up = ev.y_up;
      break;

    case LoadEvent::Kind::Complete: {
      // Keep the counts accumulated from the mesh events: the worker's totals
      // cover geometry it converted, which is not always geometry we kept.
      const uint64_t tris = scene_.stats.triangle_count;
      const uint64_t verts = scene_.stats.vertex_count;
      const uint64_t gbytes = scene_.stats.geometry_bytes;
      const uint64_t mcount = scene_.stats.mesh_count;
      scene_.stats = ev.stats;
      scene_.stats.triangle_count = tris;
      scene_.stats.vertex_count = verts;
      scene_.stats.geometry_bytes = gbytes;
      scene_.stats.mesh_count = mcount;
      scene_.degraded = ev.degraded;
      if (ev.bounds.valid) scene_.bounds = ev.bounds;
      scene_.y_up = ev.y_up;
      scene_complete_ = true;
      if (scene_.meshes.empty() && !viewport_message_is_error_) {
        viewport_message_ = "no renderable geometry in this file";
        viewport_message_is_error_ = true;
      }
      break;
    }

    case LoadEvent::Kind::Failed:
      viewport_message_ = ev.error;
      viewport_message_is_error_ = true;
      scene_complete_ = true;
      break;
  }
}

void App::UpdateStatus() {
  const LoadPhase phase = loader_.control_valid()
                              ? loader_.control().phase.load()
                              : LoadPhase::Idle;

  char buf[256];
  if (loader_.running() && phase != LoadPhase::Done) {
    const int permille = loader_.control().phase_permille.load();
    const uint32_t done = loader_.control().meshes_done.load();
    const uint32_t total = loader_.control().meshes_total.load();
    if (total > 0) {
      std::snprintf(buf, sizeof(buf), "%s %u/%u meshes", LoadPhaseName(phase),
                    done, total);
    } else {
      std::snprintf(buf, sizeof(buf), "%s %d%%", LoadPhaseName(phase),
                    permille / 10);
    }
    status_right_ = buf;
    return;
  }

  if (scene_.meshes.empty() && scene_complete_) {
    status_right_ = "empty";
    return;
  }
  if (scene_.meshes.empty()) {
    status_right_ = "ready";
    return;
  }

  std::snprintf(buf, sizeof(buf), "%s tris  %s meshes  %s mats  %s tex",
                FormatCount(scene_.stats.triangle_count).c_str(),
                FormatCount(scene_.stats.mesh_count).c_str(),
                FormatCount(scene_.stats.material_count).c_str(),
                FormatCount(scene_.stats.texture_count).c_str());
  status_right_ = buf;

  if (scene_.degraded.any()) {
    std::string why;
    if (scene_.degraded.uncomposed) why = "uncomposed";
    else if (scene_.degraded.triangle_cap_hit) why = "triangle cap";
    else if (scene_.degraded.proxy_geometry) why = "proxy geometry";
    else if (scene_.degraded.geometry_skipped) why = "geometry skipped";
    else if (scene_.degraded.textures_dropped) why = "textures dropped";
    status_right_ += "  [degraded: " + why + "]";
  }
}

void App::DrawSplitter(lvg_canvas_t* c, const lvg_rect_t& r) {
  FillRect(c, r, theme_.border);
  if (dragging_splitter_) FillRect(c, r, theme_.accent);
}

bool App::EnsureRenderer(const lvg_rect_t& viewport) {
  if (viewport.width <= 0 || viewport.height <= 0) return false;

  if (!renderer_) {
    RenderSettings rs;
    rs.spp = opts_.spp;
    rs.threads = ResolveThreadCount(opts_);
    rs.shadows = opts_.shadows;
    rs.ao = opts_.ao;

    // GPU first when asked for, but never at the cost of starting up: a missing
    // libEGL, an unusable driver or too little free VRAM all fall back to the
    // CPU tracer, which needs no GPU at all.
    // A GL driver costs ~100 MB of RSS that the budget cannot track (it lives
    // in the driver, not behind PoolAlloc). Under a tight cap that overhead
    // dwarfs the preview itself, so `auto` stays on the CPU; an explicit
    // --backend gl still gets what it asked for.
    const bool gl_affordable = budget_.total >= (256ull << 20);
    if (opts_.backend == BackendChoice::Gl ||
        (opts_.backend == BackendChoice::Auto && gl_affordable)) {
      // Rough VRAM need: geometry + textures, plus the framebuffers.
      const uint64_t needed = scene_.ByteSize() +
                              uint64_t(viewport.width) * viewport.height * 8;
      std::string gerr;
      renderer_ = CreateGlRenderer(viewport.width, viewport.height, rs, needed,
                                   &gerr);
      if (!renderer_) {
        if (opts_.backend == BackendChoice::Gl) {
          // Explicitly requested: say why we could not honour it, then fall
          // back rather than refusing to show anything.
          std::fprintf(stderr,
                       "[tusdquicklook] GL backend unavailable (%s); using the "
                       "CPU renderer\n",
                       gerr.c_str());
        } else if (opts_.verbose) {
          std::fprintf(stderr, "[tusdquicklook] GL backend unavailable: %s\n",
                       gerr.c_str());
        }
      }
    }

    if (!renderer_) {
      renderer_ = CreateCpuRenderer();
      if (!renderer_) return false;
      std::string rerr;
      if (!renderer_->Init(viewport.width, viewport.height, rs, &rerr)) {
        renderer_.reset();
        return false;
      }
    }

    if (opts_.verbose) {
      std::fprintf(stderr, "[tusdquicklook] renderer: %s\n", renderer_->Name());
    }
    renderer_->SetScene(&scene_);
  }

  renderer_->Resize(viewport.width, viewport.height);
  return true;
}

void App::AutoFrameIfNeeded(const lvg_rect_t& viewport) {
  if (camera_user_controlled_) return;
  if (!scene_.bounds.valid) return;
  // Re-frame while geometry is still arriving: the bounds of the first mesh are
  // rarely the bounds of the scene.
  if (camera_framed_ && scene_.stats.mesh_count == framed_at_mesh_count_ &&
      scene_complete_) {
    return;
  }

  if (!camera_framed_) {
    camera_.y_up = scene_.y_up;
    // Default to a front-three-quarter view. "Front" differs by up axis: +Z for
    // Y-up stages, -Y for Z-up ones (the DCC convention most Z-up content is
    // authored to), so the same yaw would otherwise show the back of the model.
    camera_.yaw = scene_.y_up ? 0.7f : 3.1415927f - 0.7f;
    camera_.pitch = 0.32f;
  }
  const float aspect =
      viewport.height > 0 ? float(viewport.width) / float(viewport.height)
                          : 1.0f;
  if (!scene_.cameras.empty() && !camera_framed_) {
    camera_.FromDesc(scene_.cameras.front(), scene_.bounds);
  } else {
    camera_.FrameBounds(scene_.bounds, aspect);
  }
  camera_framed_ = true;
  framed_at_mesh_count_ = scene_.stats.mesh_count;
}

void App::DrawViewport(lvg_canvas_t* c, const lvg_rect_t& r) {
  if (!scene_.meshes.empty() && EnsureRenderer(r)) {
    AutoFrameIfNeeded(r);
    renderer_->SyncScene();
    renderer_->SetCamera(camera_);

    // Two budgets: generous when headless (the image is the deliverable),
    // tight when interactive (the UI is).
    const double budget_ms = headless_ ? 2000.0 : 12.0;
    render_status_ = renderer_->RenderStep(budget_ms);

    const uint32_t* px = renderer_->Pixels();
    if (px && renderer_->width() == r.width &&
        renderer_->height() == r.height) {
      // The renderer already produces lightvg's native 0xAARRGGBB, so this is a
      // straight copy with no format conversion.
      lvg_surface_t src = lvg_surface_wrap(const_cast<uint32_t*>(px), r.width,
                                           r.height, r.width);
      lvg_canvas_blit(c, r.x, r.y, &src, nullptr);
      return;
    }
  }

  FillRectVGradient(c, r, theme_.viewport_top, theme_.viewport_bottom);

  const std::string msg = viewport_message_.empty()
                              ? std::string("select a USD file to preview")
                              : viewport_message_;
  const lvg_color_t color =
      viewport_message_is_error_ ? theme_.warn : theme_.text_dim;

  // Message cards can be long ("too large ..."); give them the pane width
  // minus a margin rather than letting the ellipsis eat the numbers.
  lvg_rect_t inner = r;
  inner.x += theme_.pad * 2;
  inner.width = std::max(0, r.width - theme_.pad * 4);
  DrawTextWrappedCentered(c, font_, inner, msg, color, 3);
}

void App::DrawStatusBar(lvg_canvas_t* c, const lvg_rect_t& r) {
  FillRect(c, r, theme_.panel);
  lvg_canvas_fill_rect(c, r.x, r.y, r.width, 1, theme_.border);

  const int baseline = r.y + (r.height + lui_font_ascent(font_)) / 2 - 2;

  // Show tracked-vs-cap plus process RSS: the first is what the budget governs,
  // the second is the number that actually gets a process OOM-killed.
  const std::string right = "mem " + budget_.FormatUsage() + "  rss " +
                            FormatBytes(MemBudget::ProcessRSS());

  const int right_w = DrawTextRight(c, font_, r.x + r.width - theme_.pad,
                                    baseline, right, theme_.text_dim);
  // Leave a clear gap so the two halves never read as one run-on string on a
  // narrow window.
  const int left_max = r.width - 2 * theme_.pad - right_w - theme_.pad * 3;
  DrawTextEllipsized(c, font_, r.x + theme_.pad, baseline, left_max,
                     status_right_.empty() ? std::string("ready")
                                           : status_right_,
                     theme_.text_dim);
}

}  // namespace tusdql
