// SPDX-License-Identifier: Apache-2.0
//
// tusdquicklook application shell.
//
// Thread model (see the design doc): the App object lives on the UI thread and
// owns the window, the surface and all widget state. It never blocks on a load
// or a render; workers hand results over through queues that are drained at the
// top of each frame.
#pragma once

#include <cstddef>
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "browser.hh"
#include "budget.hh"
#include "loader.hh"
#include "camera.hh"
#include "options.hh"
#include "ql_scene.hh"
#include "image_decode.hh"
#include "render/renderer.hh"
#include "ui.hh"

#if defined(TUSDQUICKLOOK_HAVE_MCP)
#include "mcp/mcp_host.hh"
#include "mcp/mcp_server.hh"
#endif

extern "C" {
#include <lightui/lightui.h>
#include <lightui/window.h>
#include <lightui/combo.h>
#include <lightui/statusbar.h>
#include <lightui/toast.h>
}

namespace tusdql {

class App
#if defined(TUSDQUICKLOOK_HAVE_MCP)
    : public McpHost
#endif
{
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

#if defined(TUSDQUICKLOOK_HAVE_MCP)
  nlohmann::json mcpLoadUsd(const nlohmann::json& args,
                            std::string& err) override;
  nlohmann::json mcpSceneInfo(const nlohmann::json& args,
                              std::string& err) override;
  nlohmann::json mcpListPrims(const nlohmann::json& args,
                              std::string& err) override;
  nlohmann::json mcpViewport(const nlohmann::json& args,
                             std::string& err) override;
  nlohmann::json mcpScreenshot(const nlohmann::json& args,
                               std::string& err) override;
  nlohmann::json mcpRenderSettings(const nlohmann::json& args,
                                   std::string& err) override;
  nlohmann::json mcpQuit(const nlohmann::json& args,
                         std::string& err) override;
#endif

 private:
  struct ImageTask;
  struct ImageThumbnailEvent;

  // Repaint everything into `surf`. `surf` is the window surface in the
  // interactive path and an offscreen surface when headless.
  void DrawFrame(lvg_surface_t* surf);

  void DrawToolbar(lvg_canvas_t* c, const lvg_rect_t& r);
  void DrawListPane(lvg_canvas_t* c, const lvg_rect_t& r);
  void DrawSplitter(lvg_canvas_t* c, const lvg_rect_t& r);
  void DrawViewport(lvg_canvas_t* c, const lvg_rect_t& r);
  void DrawStatusBar(lvg_canvas_t* c, const lvg_rect_t& r);
  void DrawImageBrowser(lvg_canvas_t* c, const lvg_rect_t& r);
  lvg_rect_t ResetButtonRect(const lvg_rect_t& toolbar) const;
  lvg_rect_t RefreshButtonRect(const lvg_rect_t& toolbar) const;

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
  // Apply one completed thumbnail event from background workers.
  bool DrainImageThumbnailEvents();
  void ApplyImageThumbnailEvent(ImageThumbnailEvent&& ev);

  // Recompute the status line from the current load/render state.
  void UpdateStatus();

  // True when the worker has published something we have not applied yet.
  bool StreamHasWork() const;

  // Create the renderer (once) and keep its target sized to the viewport.
  bool EnsureRenderer(const lvg_rect_t& viewport);
  // Frame the scene bounds unless the user has taken control of the camera.
  void AutoFrameIfNeeded(const lvg_rect_t& viewport);
  // Reset the viewport framing and rebuild shading/light caches.
  void ResetShadingAndViewport();
  void ToggleViewMode();
  void BuildImageItems();
  // Navigates the browser into dir_path (a folder tile clicked/activated in
  // the image grid) and rebuilds the grid for the new location.
  void EnterImageFolder(const std::string& dir_path);
  void StartImageWorkers();
  void StopImageWorkers();
  void ThumbnailWorkerLoop();
  void EnsureImageCacheDir();
  std::string ImageCacheDir() const;
  std::string ImageCachePathForPath(const std::string& path) const;
  std::uint64_t Hash64(std::string_view s) const;
  void EnforceImageCacheLimit() const;
  bool SpaceAvailableForCache(std::size_t bytes_needed) const;
  bool ReadFileBytes(const std::string& path, std::vector<uint8_t>* out,
                     uint64_t max_bytes = 64ull << 20) const;
  bool LoadCachedThumbnail(const std::string& cache_path,
                          DecodedImage* out) const;
  bool LoadAndDownscaleImage(const std::string& path, DecodedImage* out) const;
  bool SaveThumbnailToCache(const std::string& cache_path,
                           const DecodedImage& image) const;
  void UpdateImageStatus();
  bool HasImageBrowserWork() const;
  bool MoveImageSelection(int delta);
  bool ScrollImageByMouseWheel(int delta_y);
  void DrawImageStatusOverlay(lvg_canvas_t* c, const lvg_rect_t& r);
  struct ImageGridGeometry {
    int columns = 4;
    int cell_w = 0;
    int cell_h = 0;
    int image_sz = 0;
    int label_h = 0;
  };
  ImageGridGeometry ComputeImageGridGeometry(int viewport_w, int viewport_h) const;

  // True while a load or a progressive render still has work to do; the event
  // loop polls instead of blocking in that case.
  bool Busy() const;

  // Re-scan the current folder and refresh the current selection preview.
  void RefreshFolder();

  // The single funnel for everything that changes the image but is not the
  // scene or the camera. CurrentRenderSettings() folds the CLI options together
  // with the live UI state; ApplyRenderSettings() pushes the result at the
  // renderer, which resets accumulation. Nothing else may touch renderer
  // settings.
  RenderSettings CurrentRenderSettings() const;
  void ApplyRenderSettings();

  // Advance camera_ toward camera_goal_. Returns true while still moving.
  // Always a no-op assignment when headless or --no-smoothing, so a screenshot
  // never depends on wall-clock timing.
  bool StepCameraMotion(float dt);
  // Pick the surface under (x, y) in the viewport and orbit around it.
  bool PickAt(const lvg_rect_t& viewport, int x, int y);

  // Backend management. The renderer can be created, destroyed and re-created
  // at any point in the session, so `desired_backend_` is a live preference
  // rather than a one-shot startup decision.
  bool GlAffordable() const;
  // Fold the user's preference together with what is actually usable.
  BackendChoice ResolveBackend() const;
  bool CreateRenderer(BackendChoice backend, const lvg_rect_t& viewport,
                      std::string* err);
  void SwitchBackend(BackendChoice backend, const lvg_rect_t& viewport);
  // A backend failure mid-session: drop to CPU, permanently for driver loss or
  // temporarily for a scene-specific resource-cap refusal.
  void DemoteToCpu(const std::string& why, const lvg_rect_t& viewport);
  // "gl · llvmpipe" / "cpu (gl: no EGL display)". For the status bar.
  std::string BackendStatusText() const;
  // Advance desired_backend_ auto -> cpu -> gl -> auto.
  void CycleBackend();

  Options opts_;
  const Theme& theme_;
  PreviewBudget budget_;
  Browser browser_;
  Loader loader_;
  QlScene scene_;
  bool scene_complete_ = false;

  std::unique_ptr<Renderer> renderer_;
  // Shared with the CPU renderer. Owned here so picking keeps working while
  // the GL backend is live, and so a backend switch never rebuilds the BVH.
  std::shared_ptr<PickAccel> accel_ = std::make_shared<PickAccel>();

  // camera_ is what is displayed; camera_goal_ is what input edits. They are
  // the same object unless smoothing is on and motion is still settling.
  OrbitCamera camera_;
  OrbitCamera camera_goal_;
  bool camera_animating_ = false;
  // Last picked surface, for shift+F framing and '.' refocus.
  bool have_pick_ = false;
  float pick_point_[3] = {0, 0, 0};
  size_t pick_mesh_ = 0;
  RenderStatus render_status_;

  // Live UI state seeded from opts_, so a toolbar change and a CLI flag reach
  // the renderer by the same path.
  ShadingMode shading_mode_ = ShadingMode::Shaded;
  bool ibl_enabled_ = true;
  bool shadows_enabled_ = true;
  float exposure_ = 0.0f;

  // What the user wants vs what is actually running. They differ whenever GL
  // was asked for and could not be delivered.
  BackendChoice desired_backend_ = BackendChoice::Auto;
  BackendChoice live_backend_ = BackendChoice::Cpu;
  std::string live_device_;
  // Set once GL has failed at the driver/context level, so `auto` stops trying
  // and the UI can say why instead of silently rendering on the CPU.
  bool gl_disabled_ = false;
  // A residency-cap refusal is scene-specific, not a broken driver. Keep the
  // CPU fallback for this scene without retrying every frame; PreviewFile()
  // clears it when the user selects another asset.
  bool gl_budget_blocked_ = false;
  std::string gl_error_;
  GlProbeResult gl_probe_;
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

  // lightui widgets. These are placed by hand into the rects ComputeLayout
  // produces rather than through lui_layout_compute: the shell geometry is a
  // fixed toolbar/list/splitter/viewport/status split that the flow layout
  // would only re-derive. A parentless widget's `computed` rect is already its
  // absolute rect, so setting it directly is all the placement they need.
  bool widgets_ready_ = false;
  lui_combo_t mode_combo_{};     // shading mode
  lui_combo_t backend_combo_{};  // auto / cpu / gl
  lui_statusbar_t statusbar_{};
  lui_toast_t toast_{};

  void InitWidgets();
  void PlaceWidgets(const Layout& l);
  void SyncWidgetState();
  // True while a toast is still on screen, so Busy() keeps the loop animating
  // until it expires and then lets the app go idle again.
  bool ToastActive() const;
  void Notify(const std::string& message, lui_toast_type_t type);
  static void OnModeComboChanged(int index, const char* item, void* user);
  static void OnBackendComboChanged(int index, const char* item, void* user);

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

  enum class ViewMode : uint8_t { UsdPreview, ImageBrowser };
  enum class ImageStatus : uint8_t { Pending, Loading, Ready, Error };

  struct ImageItem {
    // Thumbnail source: the image file itself, or -- for a folder tile --
    // a representative image found inside it (empty if none, in which case
    // the tile falls back to a plain folder icon). Decoded the same way
    // either way, so folder tiles and image tiles share one load path.
    std::string path;
    std::string name;
    std::string cache_path;
    ImageStatus status = ImageStatus::Pending;
    std::shared_ptr<std::vector<uint32_t>> pixels;
    int width = 0;
    int height = 0;
    std::string error;
    // Subfolder of the scanned directory, shown as a folder tile in the
    // grid; nav_path is where activating it navigates (EnterImageFolder).
    bool is_dir = false;
    std::string nav_path;
  };

  struct ImageTask {
    std::size_t index = 0;
    std::uint64_t generation = 0;
    std::string path;
  };

  struct ImageThumbnailEvent {
    std::size_t index = 0;
    std::uint64_t generation = 0;
    bool ok = false;
    std::string path;
    std::vector<uint32_t> argb_pixels;
    int width = 0;
    int height = 0;
    std::string error;
  };

  ViewMode view_mode_ = ViewMode::UsdPreview;
  std::vector<ImageItem> image_items_;
  int selected_image_ = -1;
  int image_scroll_ = 0;
  int image_columns_ = 4;
  std::atomic<std::size_t> image_generation_{0};
  std::deque<ImageTask> image_task_queue_;
  std::deque<ImageThumbnailEvent> image_events_;
  std::vector<std::thread> image_workers_;
  std::condition_variable image_cv_;
  mutable std::mutex image_mu_;
  bool image_workers_stop_ = false;
  bool image_workers_started_ = false;
  std::string image_cache_dir_;
  std::atomic<bool> image_cache_disabled_{false};
  static constexpr std::uint64_t kImageCacheMaxBytes = 100ull * 1024ull * 1024ull;
  static constexpr int kImageCacheMaxDim = 320;
  static constexpr int kImageMinColumns = 2;
  static constexpr int kImageMaxColumns = 10;

  std::string err_;

#if defined(TUSDQUICKLOOK_HAVE_MCP)
  std::unique_ptr<MCPServer> mcp_;
#endif
};

}  // namespace tusdql
