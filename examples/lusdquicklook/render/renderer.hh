// SPDX-License-Identifier: Apache-2.0
//
// lusdquicklook — the renderer interface.
//
// Both backends (CPU ray tracing, optional offscreen GL raster) satisfy this,
// so the app never branches on which one is active. Pixels come back in
// lightvg's native 0xAARRGGBB so the viewport blit is a straight copy.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "camera.hh"
#include "render/pick.hh"
#include "options.hh"
#include "ql_scene.hh"

namespace lusdql {

struct RenderStatus {
  bool converged = false;   // no further refinement would change the image
  int samples_done = 0;     // accumulated samples per pixel
  int samples_target = 0;
  int tiles_done = 0;
  int tiles_total = 0;
  bool produced_pixels = false;  // something new to show since the last step

  // The backend can no longer render (context lost, framebuffer incomplete,
  // driver error). The app demotes to CPU and shows `error`.
  bool device_lost = false;
  std::string error;
};

struct RenderSettings {
  int spp = 16;
  int threads = 4;
  bool shadows = true;
  bool ao = false;

  ShadingMode mode = ShadingMode::Shaded;
  bool ibl = true;
  float exposure = 0.0f;  // stops
};

class Renderer {
 public:
  virtual ~Renderer() = default;

  virtual bool Init(int width, int height, const RenderSettings& settings,
                    std::string* err) = 0;
  virtual void Resize(int width, int height) = 0;

  // Replace the scene. Invalidates all accumulated samples.
  virtual void SetScene(const QlScene* scene) = 0;
  // Rebuild acceleration structures for geometry appended since the last call.
  // Cheap when nothing changed, so it is safe to call every frame during a
  // progressive load.
  virtual void SyncScene() = 0;

  virtual void SetCamera(const OrbitCamera& camera) = 0;

  // Replace the render settings. Like SetScene/SetCamera this invalidates all
  // accumulated samples, so it is the single funnel for anything that changes
  // the image (shading mode, IBL, exposure, shadows).
  virtual void SetSettings(const RenderSettings& settings) = 0;

  // Do up to `budget_ms` of work and return what changed. Called from the UI
  // thread between event drains, so it must respect the budget.
  virtual RenderStatus RenderStep(double budget_ms) = 0;

  // Blit-ready 0xAARRGGBB, `width * height`, tightly packed.
  virtual const uint32_t* Pixels() const = 0;
  virtual int width() const = 0;
  virtual int height() const = 0;

  virtual const char* Name() const = 0;
  // Human-readable device behind this backend ("llvmpipe", "NVIDIA ...", or a
  // CPU thread count). Shown in the status bar. Never null.
  virtual const char* DeviceName() const { return ""; }
};

// Backed by lightrt's CPU BVH kernel; always available, no GPU required.
// Takes the shared acceleration structure rather than building its own, so
// exactly one BVH exists however often the backend is switched.
std::unique_ptr<Renderer> CreateCpuRenderer(std::shared_ptr<PickAccel> accel);

struct GlProbeResult {
  bool available = false;
  std::string device;      // GL_RENDERER, when we got that far
  std::string version;     // GL_VERSION
  std::string error;       // why not, when !available
  uint64_t free_vram = 0;  // 0 when the driver does not report it
  uint64_t required_vram = 0;
  uint64_t gpu_budget = 0;
};

// Offscreen GL 3.3 raster. Context creation and the resource-budget check are
// intentionally lazy: GL is only initialized when this backend is selected,
// and `probe` receives the resulting device/resource diagnostics.
// Returns nullptr (reason in `err`) when no headless context can be created or
// the resource estimate exceeds the configured GPU budget.
std::unique_ptr<Renderer> CreateGlRenderer(int width, int height,
                                           const RenderSettings& settings,
                                           uint64_t required_vram_bytes,
                                           uint64_t max_gpu_mem_bytes,
                                           GlProbeResult* probe,
                                           std::string* err);

}  // namespace lusdql
