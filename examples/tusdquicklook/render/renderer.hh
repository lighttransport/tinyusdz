// SPDX-License-Identifier: Apache-2.0
//
// tusdquicklook — the renderer interface.
//
// Both backends (CPU ray tracing, optional offscreen GL raster) satisfy this,
// so the app never branches on which one is active. Pixels come back in
// lightvg's native 0xAARRGGBB so the viewport blit is a straight copy.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "camera.hh"
#include "ql_scene.hh"

namespace tusdql {

struct RenderStatus {
  bool converged = false;   // no further refinement would change the image
  int samples_done = 0;     // accumulated samples per pixel
  int samples_target = 0;
  int tiles_done = 0;
  int tiles_total = 0;
  bool produced_pixels = false;  // something new to show since the last step
};

struct RenderSettings {
  int spp = 16;
  int threads = 4;
  bool shadows = true;
  bool ao = false;
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

  // Do up to `budget_ms` of work and return what changed. Called from the UI
  // thread between event drains, so it must respect the budget.
  virtual RenderStatus RenderStep(double budget_ms) = 0;

  // Blit-ready 0xAARRGGBB, `width * height`, tightly packed.
  virtual const uint32_t* Pixels() const = 0;
  virtual int width() const = 0;
  virtual int height() const = 0;

  virtual const char* Name() const = 0;
};

// Backed by lightrt's CPU BVH kernel; always available, no GPU required.
std::unique_ptr<Renderer> CreateCpuRenderer();

// Offscreen GL 3.3 raster. Returns nullptr (reason in `err`) when no headless
// context can be created or the GPU reports too little free VRAM for
// `required_vram_bytes` of scene data — both are ordinary outcomes on a machine
// without a GPU, and the caller falls back to the CPU renderer.
std::unique_ptr<Renderer> CreateGlRenderer(int width, int height,
                                           const RenderSettings& settings,
                                           uint64_t required_vram_bytes,
                                           std::string* err);

}  // namespace tusdql
