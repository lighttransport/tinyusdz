// SPDX-License-Identifier: Apache-2.0
//
// tusdquicklook — application shell.
//
// Thread model (see the design doc): the App object lives on the UI thread and
// owns the window, the surface and all widget state. It never blocks on a load
// or a render; workers hand results over through queues that are drained at the
// top of each frame.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "browser.hh"
#include "budget.hh"
#include "loader.hh"
#include "camera.hh"
#include "options.hh"
#include "ql_scene.hh"
#include "render/renderer.hh"
#include "ui.hh"

extern "C" {
#include <lightui/lightui.h>
#include <lightui/window.h>
}

namespace tusdql {

class App {
 public:
  explicit App(const Options& opts);
  ~App();

  App(const App&) = delete;
  App& operator=(const App&) = delete;

  // Creates the font and (unless headless) the window. Returns false and fills
  // LastError() on failure.
  bool Init();

  // Interactive event loop. Returns a process exit code.
  int Run();

  // Headless: render `opts.frames` steps into an offscreen surface, write
  // opts.screenshot, exit. Never opens a window, so this works over ssh and in
  // CI.
  int RunHeadless();

  const std::string& LastError() const { return err_; }

 private:
  // Repaint everything into `surf`. `surf` is the window surface in the
  // interactive path and an offscreen surface when headless.
  void DrawFrame(lvg_surface_t* surf);

  void DrawToolbar(lvg_canvas_t* c, const lvg_rect_t& r);
  void DrawListPane(lvg_canvas_t* c, const lvg_rect_t& r);
  void DrawSplitter(lvg_canvas_t* c, const lvg_rect_t& r);
  void DrawViewport(lvg_canvas_t* c, const lvg_rect_t& r);
  void DrawStatusBar(lvg_canvas_t* c, const lvg_rect_t& r);

  // Returns false when the app should quit.
  bool HandleEvent(const lui_event_t& ev);

  // Number of file rows that fit in the list pane.
  int VisibleRows() const;

  // Activate the current selection: descend into a directory, or begin
  // previewing a file.
  void ActivateSelection();

  // Kick off a preview of `entry`. Refuses (and says so) when the pre-open
  // projection already exceeds the budget.
  void PreviewFile(const FileEntry& entry);

  // Apply everything the worker has published since the last frame. Returns
  // true when the scene changed and the viewport needs a repaint.
  bool DrainLoadEvents();

  // Fold one worker event into scene_. Shared by the interactive drain and the
  // synchronous headless run.
  void ApplyLoadEvent(LoadEvent&& ev);

  // Recompute the status line from the current load/render state.
  void UpdateStatus();

  // True when the worker has published something we have not applied yet.
  bool StreamHasWork() const;

  // Create the renderer (once) and keep its target sized to the viewport.
  bool EnsureRenderer(const lvg_rect_t& viewport);
  // Frame the scene bounds unless the user has taken control of the camera.
  void AutoFrameIfNeeded(const lvg_rect_t& viewport);

  // True while a load or a progressive render still has work to do; the event
  // loop polls instead of blocking in that case.
  bool Busy() const;

  Options opts_;
  const Theme& theme_;
  PreviewBudget budget_;
  Browser browser_;
  Loader loader_;
  QlScene scene_;
  bool scene_complete_ = false;

  std::unique_ptr<Renderer> renderer_;
  OrbitCamera camera_;
  RenderStatus render_status_;
  // Cleared on every new file; set the moment the user touches the camera, so
  // auto-framing stops fighting them.
  bool camera_user_controlled_ = false;
  bool camera_framed_ = false;
  uint64_t framed_at_mesh_count_ = 0;

  // Camera drag state.
  enum class DragMode : uint8_t { None, Orbit, Pan };
  DragMode drag_mode_ = DragMode::None;
  int drag_last_x_ = 0;
  int drag_last_y_ = 0;

  lui_window_t* window_ = nullptr;
  lui_font_t* font_ = nullptr;

  int surf_w_ = 0;
  int surf_h_ = 0;
  int left_pane_w_ = 300;

  // Splitter drag state.
  bool dragging_splitter_ = false;
  int drag_origin_x_ = 0;
  int drag_origin_w_ = 0;

  bool headless_ = false;
  bool platform_up_ = false;  // lui_init() succeeded; needs lui_shutdown()

  bool needs_redraw_ = true;
  bool quit_ = false;

  // Message shown in the viewport when there is nothing rendered yet (or when
  // a file was refused), and its severity.
  std::string viewport_message_;
  bool viewport_message_is_error_ = false;

  // Status line fields; filled in by later stages (loader, renderer).
  std::string status_left_;
  std::string status_right_;

  std::string err_;
};

}  // namespace tusdql
