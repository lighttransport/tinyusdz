// SPDX-License-Identifier: Apache-2.0
//
// lusdquicklook — CPU ray tracer on the vendored lightrt C kernel.
//
// Structure:
//   * one BLAS per QlMesh, built with lrt_tri_scene_build_indexed (the
//     lightusd-local indexed build, which keeps the indexed data instead of
//     materializing a 9-float-per-triangle vertex soup)
//   * a TLAS with identity transforms, instance_id = mesh index, so a hit
//     reports (mesh, triangle) directly and geometry appended during a
//     progressive load only costs a TLAS rebuild
//
// Progression: an eighth-resolution pass first (the Quick Look "first pixel"),
// then full-resolution 32x32 tiles accumulating one sample at a time. Any
// camera or scene change resets the accumulator.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "render/pick.hh"
#include "render/renderer.hh"
#include "render/shade.hh"

extern "C" {
#include "lightrt_c_tri.h"
}

namespace lusdql {

namespace {

constexpr int kTileSize = 32;
constexpr int kCoarseFactor = 8;

// Stand-in for a mesh with no material binding. Opaque, so unbound geometry
// never takes the transparency path.
const QlMaterial kFallbackMaterial{};

class CpuRenderer final : public Renderer {
 public:
  explicit CpuRenderer(std::shared_ptr<PickAccel> accel)
      : accel_(std::move(accel)) {}

  ~CpuRenderer() override { StopWorkers(); }

  bool Init(int width, int height, const RenderSettings& settings,
            std::string* err) override {
    settings_ = settings;
    if (settings_.threads < 1) settings_.threads = 1;
    EnsureWorkers(settings_.threads);
    Resize(width, height);
    (void)err;
    return true;
  }

  void Resize(int width, int height) override {
    width = std::max(1, width);
    height = std::max(1, height);
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;
    accum_.assign(size_t(width_) * height_ * 3, 0.0f);
    pixels_.assign(size_t(width_) * height_, 0xFF101014u);
    ResetProgression();
  }

  void SetScene(const QlScene* scene) override {
    scene_ = scene;
    // The acceleration structure is shared with the app (which picks against
    // it) and survives a backend switch, so it is pointed at the scene here
    // but never owned or rebuilt from inside the renderer.
    if (accel_) accel_->SetScene(scene);
    ResetProgression();
  }

  void SyncScene() override {
    if (!scene_ || !accel_) return;
    if (accel_->Sync(settings_.threads)) {
      shading_valid_ = false;
      ResetProgression();
    }
  }

  void SetCamera(const OrbitCamera& camera) override {
    if (!camera.Differs(camera_)) return;
    camera_ = camera;
    ResetProgression();
  }

  void SetSettings(const RenderSettings& settings) override {
    const int threads = std::max(1, settings.threads);
    settings_ = settings;
    settings_.threads = threads;
    EnsureWorkers(settings_.threads);
    // The light rig folds in shadows/IBL, so it has to be rebuilt too.
    shading_valid_ = false;
    ResetProgression();
    device_name_.clear();
  }

  RenderStatus RenderStep(double budget_ms) override;

  const uint32_t* Pixels() const override { return pixels_.data(); }
  int width() const override { return width_; }
  int height() const override { return height_; }
  const char* Name() const override { return "cpu"; }

  const char* DeviceName() const override {
    if (device_name_.empty()) {
      device_name_ = std::to_string(settings_.threads) + " threads";
    }
    return device_name_.c_str();
  }

 private:
  void ResetProgression();
  void PrepareShading();

  void RenderCoarse();
  void RenderTile(int tile_index, int sample_index);
  void ResolveTileToPixels(int tile_index);
  void EnsureWorkers(int count);
  void StopWorkers();
  void RunParallel(int count, const std::function<void(int)>& fn);
  void WorkerLoop();

  int SampleTarget() const {
    // AOVs are direct views of one input and do not benefit from repeated
    // lighting samples. Keep the coarse pass for fast coverage, then resolve
    // one full sample and become idle.
    return settings_.mode == ShadingMode::Shaded ? settings_.spp : 1;
  }

  // Trace one primary ray; returns linear RGB.
  void TracePixel(int px, int py, int sample_index, float out_rgb[3]) const;

  bool Occluded(const float origin[3], const float direction[3],
                float max_distance) const;
  static bool OccludedThunk(void* user, const float origin[3],
                            const float direction[3], float max_distance);

  int TileCount() const {
    const int tx = (width_ + kTileSize - 1) / kTileSize;
    const int ty = (height_ + kTileSize - 1) / kTileSize;
    return tx * ty;
  }

  const QlScene* scene_ = nullptr;
  RenderSettings settings_;
  mutable std::string device_name_;
  OrbitCamera camera_;

  int width_ = 1;
  int height_ = 1;
  std::vector<float> accum_;      // linear RGB accumulation
  std::vector<uint32_t> pixels_;  // 0xAARRGGBB, blit ready

  std::shared_ptr<PickAccel> accel_;

  ShadingContext shading_;
  bool shading_valid_ = false;
  // Whether any material uses cutout. When nothing does, shadow rays take the
  // cheap boolean occlusion test instead of walking hits.
  bool any_cutout_ = false;

  // Progression state
  bool coarse_done_ = false;
  int sample_index_ = 0;
  int next_tile_ = 0;

  std::mutex workers_mu_;
  std::condition_variable workers_cv_;
  std::condition_variable workers_done_cv_;
  std::vector<std::thread> workers_;
  std::function<void(int)> worker_job_;
  std::atomic<int> worker_next_{0};
  int worker_job_count_ = 0;
  int worker_finished_ = 0;
  uint64_t worker_generation_ = 0;
  bool worker_job_active_ = false;
  bool worker_stop_ = false;
};

// ---------------------------------------------------------------------------

void CpuRenderer::ResetProgression() {
  coarse_done_ = false;
  sample_index_ = 0;
  next_tile_ = 0;
  shading_valid_ = false;
  std::fill(accum_.begin(), accum_.end(), 0.0f);
}

void CpuRenderer::EnsureWorkers(int count) {
  count = std::max(1, count);
  if (static_cast<int>(workers_.size()) == count) return;

  StopWorkers();
  {
    std::lock_guard<std::mutex> lock(workers_mu_);
    worker_stop_ = false;
  }
  workers_.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; i++) {
    workers_.emplace_back(&CpuRenderer::WorkerLoop, this);
  }
}

void CpuRenderer::StopWorkers() {
  if (workers_.empty()) return;
  {
    std::lock_guard<std::mutex> lock(workers_mu_);
    worker_stop_ = true;
    worker_job_active_ = false;
  }
  workers_cv_.notify_all();
  for (std::thread& worker : workers_) {
    if (worker.joinable()) worker.join();
  }
  workers_.clear();
  worker_job_ = nullptr;
  worker_stop_ = false;
}

void CpuRenderer::WorkerLoop() {
  uint64_t seen_generation = 0;
  for (;;) {
    std::function<void(int)> job;
    int job_count = 0;
    {
      std::unique_lock<std::mutex> lock(workers_mu_);
      workers_cv_.wait(lock, [&] {
        return worker_stop_ ||
               (worker_job_active_ && worker_generation_ != seen_generation);
      });
      if (worker_stop_) return;
      seen_generation = worker_generation_;
      job = worker_job_;
      job_count = worker_job_count_;
    }

    for (;;) {
      const int index = worker_next_.fetch_add(1);
      if (index >= job_count) break;
      job(index);
    }

    std::lock_guard<std::mutex> lock(workers_mu_);
    worker_finished_++;
    if (worker_finished_ == static_cast<int>(workers_.size())) {
      worker_job_active_ = false;
      workers_done_cv_.notify_one();
    }
  }
}

void CpuRenderer::RunParallel(int count, const std::function<void(int)>& fn) {
  if (count <= 0) return;
  EnsureWorkers(std::max(1, settings_.threads));
  {
    std::lock_guard<std::mutex> lock(workers_mu_);
    worker_job_ = fn;
    worker_job_count_ = count;
    worker_next_.store(0);
    worker_finished_ = 0;
    worker_generation_++;
    worker_job_active_ = true;
  }
  workers_cv_.notify_all();

  std::unique_lock<std::mutex> lock(workers_mu_);
  workers_done_cv_.wait(lock, [&] { return !worker_job_active_; });
}

void CpuRenderer::PrepareShading() {
  if (shading_valid_ || !scene_) return;
  float eye[3], right[3], up[3], fwd[3];
  camera_.Eye(eye);
  camera_.Basis(right, up, fwd);
  BuildLightRig(*scene_, eye, fwd, right, up, &shading_);
  // After the rig: BuildEnvironment reads y_up, which BuildLightRig sets.
  BuildEnvironment(*scene_, settings_.ibl, &shading_);
  shading_.shadows = settings_.shadows;
  shading_.mode = settings_.mode;
  shading_.depth_near = camera_.near_clip;
  shading_.depth_far = camera_.far_clip;

  any_cutout_ = false;
  for (const QlMaterial& m : scene_->materials) {
    if (m.alpha_mode == QlMaterial::AlphaMode::Mask) {
      any_cutout_ = true;
      break;
    }
  }

  shading_valid_ = true;
}

// ---------------------------------------------------------------------------
// Tracing
// ---------------------------------------------------------------------------

bool CpuRenderer::Occluded(const float origin[3], const float direction[3],
                           float max_distance) const {
  const lrt_tlas* tlas = accel_ ? accel_->tlas() : nullptr;
  if (!tlas) return false;
  lrt_ray ray{};
  ray.org[0] = origin[0];
  ray.org[1] = origin[1];
  ray.org[2] = origin[2];
  ray.dir[0] = direction[0];
  ray.dir[1] = direction[1];
  ray.dir[2] = direction[2];
  ray.tmin = 0.0f;
  ray.tmax = max_distance;

  if (!scene_ || !any_cutout_) {
    // Nothing in the scene can be cut away, so the cheap boolean test is
    // exact.
    return lrt_tlas_occluded1(tlas, &ray, 0xFFFFFFFFu) != 0;
  }

  // A cutout surface below its threshold is not there, and something that is
  // not there cannot cast a shadow. lrt_tlas_occluded1 answers "is anything in
  // the way", which is the wrong question once alpha can remove a hit, so walk
  // forward and skip the ones that were cut away.
  constexpr int kMaxCutoutLayers = 8;
  for (int layer = 0; layer < kMaxCutoutLayers; layer++) {
    lrt_tlas_hit hit{};
    if (!lrt_tlas_intersect1(tlas, &ray, 0xFFFFFFFFu, &hit)) return false;
    if (hit.inst_id >= accel_->instance_count()) return true;

    const size_t mesh_index = accel_->mesh_index(hit.inst_id);
    if (mesh_index >= scene_->meshes.size()) return true;
    const QlMesh& mesh = scene_->meshes[mesh_index];
    const QlMaterial& mat =
        (mesh.material_id >= 0 &&
         mesh.material_id < static_cast<int>(scene_->materials.size()))
            ? scene_->materials[size_t(mesh.material_id)]
            : kFallbackMaterial;

    if (mat.alpha_mode != QlMaterial::AlphaMode::Mask) return true;

    // Evaluate the cutout at the hit. Only the UV is needed, so this builds a
    // minimal SurfaceHit rather than the full shading one.
    SurfaceHit surf;
    surf.material_id = mesh.material_id;
    const size_t tri = hit.prim_id;
    if (tri * 3 + 2 >= mesh.indices.size()) return true;
    if (!mesh.uvs.empty()) {
      const uint32_t i0 = mesh.indices[tri * 3 + 0];
      const uint32_t i1 = mesh.indices[tri * 3 + 1];
      const uint32_t i2 = mesh.indices[tri * 3 + 2];
      if (size_t(i2) * 2 + 1 < mesh.uvs.size()) {
        const float bu = hit.u, bv = hit.v, bw = 1.0f - bu - bv;
        const float* t0 = &mesh.uvs[size_t(i0) * 2];
        const float* t1 = &mesh.uvs[size_t(i1) * 2];
        const float* t2 = &mesh.uvs[size_t(i2) * 2];
        surf.uv[0] = t0[0] * bw + t1[0] * bu + t2[0] * bv;
        surf.uv[1] = t0[1] * bw + t1[1] * bu + t2[1] * bv;
        surf.has_uv = true;
      }
    }

    EvaluatedMaterial em;
    EvaluateMaterial(shading_, surf, direction, &em);
    if (em.alpha >= mat.alpha_cutoff) return true;

    // Cut away: keep looking past it.
    ray.tmin = hit.t + std::max(shading_.scene_radius * 1e-5f, 1e-6f);
    if (ray.tmin >= ray.tmax) return false;
  }
  return true;
}

bool CpuRenderer::OccludedThunk(void* user, const float origin[3],
                                const float direction[3], float max_distance) {
  return static_cast<const CpuRenderer*>(user)->Occluded(origin, direction,
                                                         max_distance);
}

void CpuRenderer::TracePixel(int px, int py, int sample_index,
                             float out_rgb[3]) const {
  // Deterministic stratified jitter: the same (pixel, sample) always produces
  // the same offset, so --threads changes cannot change the image.
  float jx = 0.5f, jy = 0.5f;
  if (sample_index > 0) {
    // Radical-inverse (van der Corput) in bases 2 and 3.
    auto radical = [](uint32_t n, uint32_t base) {
      float inv = 1.0f / float(base);
      float f = inv;
      float r = 0.0f;
      while (n) {
        r += float(n % base) * f;
        n /= base;
        f *= inv;
      }
      return r;
    };
    jx = radical(uint32_t(sample_index), 2u);
    jy = radical(uint32_t(sample_index), 3u);
  }

  float origin[3], direction[3];
  camera_.GenerateRay(px, py, width_, height_, jx, jy, origin, direction);

  const lrt_tlas* tlas = accel_ ? accel_->tlas() : nullptr;
  if (!tlas) {
    ShadeBackground(shading_, direction, out_rgb);
    return;
  }

  // Transparency is resolved by walking the ray forward through successive
  // hits rather than by sorting: a cutout hit below its threshold is skipped
  // entirely, and a blended hit contributes its share and lets the rest of the
  // ray keep going. Bounded so a pathological stack of transparent shells
  // cannot stall a frame.
  constexpr int kMaxAlphaLayers = 8;
  float transmittance = 1.0f;
  out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
  float ray_tmin = 0.0f;

  for (int layer = 0; layer < kMaxAlphaLayers; layer++) {
  lrt_ray ray{};
  std::memcpy(ray.org, origin, sizeof(ray.org));
  std::memcpy(ray.dir, direction, sizeof(ray.dir));
  ray.tmin = ray_tmin;
  ray.tmax = 1e30f;

  lrt_tlas_hit hit{};
  if (!lrt_tlas_intersect1(tlas, &ray, 0xFFFFFFFFu, &hit)) {
    float bg[3];
    ShadeBackground(shading_, direction, bg);
    for (int i = 0; i < 3; i++) out_rgb[i] += bg[i] * transmittance;
    return;
  }

  if (hit.inst_id >= accel_->instance_count()) {
    float bg[3];
    ShadeBackground(shading_, direction, bg);
    for (int i = 0; i < 3; i++) out_rgb[i] += bg[i] * transmittance;
    return;
  }
  const QlMesh& mesh = scene_->meshes[accel_->mesh_index(hit.inst_id)];
  const size_t tri = hit.prim_id;
  if (tri * 3 + 2 >= mesh.indices.size()) {
    float bg[3];
    ShadeBackground(shading_, direction, bg);
    for (int i = 0; i < 3; i++) out_rgb[i] += bg[i] * transmittance;
    return;
  }

  const uint32_t i0 = mesh.indices[tri * 3 + 0];
  const uint32_t i1 = mesh.indices[tri * 3 + 1];
  const uint32_t i2 = mesh.indices[tri * 3 + 2];

  SurfaceHit surf;
  surf.material_id = mesh.material_id;
  for (int i = 0; i < 3; i++) {
    surf.position[i] = origin[i] + direction[i] * hit.t;
  }

  const float* p0 = &mesh.positions[size_t(i0) * 3];
  const float* p1 = &mesh.positions[size_t(i1) * 3];
  const float* p2 = &mesh.positions[size_t(i2) * 3];

  // Geometric normal from the triangle plane.
  const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
  const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
  float gn[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                 e1[0] * e2[1] - e1[1] * e2[0]};
  {
    const float len = std::sqrt(gn[0] * gn[0] + gn[1] * gn[1] + gn[2] * gn[2]);
    if (len > 1e-20f) {
      gn[0] /= len;
      gn[1] /= len;
      gn[2] /= len;
    }
  }
  std::memcpy(surf.geometric_normal, gn, sizeof(gn));
  std::memcpy(surf.normal, gn, sizeof(gn));

  // Moller-Trumbore barycentrics: u,v are the weights of v1 and v2.
  const float bu = hit.u;
  const float bv = hit.v;
  const float bw = 1.0f - bu - bv;

  if (!mesh.normals.empty() &&
      size_t(i2) * 3 + 2 < mesh.normals.size()) {
    const float* n0 = &mesh.normals[size_t(i0) * 3];
    const float* n1 = &mesh.normals[size_t(i1) * 3];
    const float* n2 = &mesh.normals[size_t(i2) * 3];
    float n[3];
    for (int i = 0; i < 3; i++) {
      n[i] = n0[i] * bw + n1[i] * bu + n2[i] * bv;
    }
    const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len > 1e-12f) {
      surf.normal[0] = n[0] / len;
      surf.normal[1] = n[1] / len;
      surf.normal[2] = n[2] / len;
    }
  }

  if (!mesh.uvs.empty() && size_t(i2) * 2 + 1 < mesh.uvs.size()) {
    const float* t0 = &mesh.uvs[size_t(i0) * 2];
    const float* t1 = &mesh.uvs[size_t(i1) * 2];
    const float* t2 = &mesh.uvs[size_t(i2) * 2];
    surf.uv[0] = t0[0] * bw + t1[0] * bu + t2[0] * bv;
    surf.uv[1] = t0[1] * bw + t1[1] * bu + t2[1] * bv;
    surf.has_uv = true;
  }

  // Interpolated tangent frame, only carried for normal-mapped materials.
  if (!mesh.tangents.empty() && size_t(i2) * 4 + 3 < mesh.tangents.size()) {
    const float* g0 = &mesh.tangents[size_t(i0) * 4];
    const float* g1 = &mesh.tangents[size_t(i1) * 4];
    const float* g2 = &mesh.tangents[size_t(i2) * 4];
    for (int i = 0; i < 3; i++) {
      surf.tangent[i] = g0[i] * bw + g1[i] * bu + g2[i] * bv;
    }
    // The handedness is constant per vertex; interpolating it would produce a
    // meaningless value across a seam, so take the provoking vertex's sign.
    surf.tangent[3] = g0[3];
    surf.has_tangent = true;
  }

  if (shading_.mode != ShadingMode::Shaded) {
    // Debug AOVs are constant per pixel, so accumulation converges on the
    // first sample. The normal loop still runs, which keeps the sample
    // sequence -- and therefore the thread-count independence -- identical to
    // the shaded path.
    float eye[3];
    camera_.Eye(eye);
    const float dx = surf.position[0] - eye[0];
    const float dy = surf.position[1] - eye[1];
    const float dz = surf.position[2] - eye[2];
    ShadeAov(shading_, surf, direction,
             std::sqrt(dx * dx + dy * dy + dz * dz), out_rgb);
    return;
  }

  const QlMaterial& hit_mat =
      (mesh.material_id >= 0 &&
       mesh.material_id < static_cast<int>(scene_->materials.size()))
          ? scene_->materials[size_t(mesh.material_id)]
          : kFallbackMaterial;

  float alpha = 1.0f;
  if (hit_mat.alpha_mode != QlMaterial::AlphaMode::Opaque) {
    EvaluatedMaterial em;
    EvaluateMaterial(shading_, surf, direction, &em);
    alpha = em.alpha;
    if (hit_mat.alpha_mode == QlMaterial::AlphaMode::Mask) {
      // Cutout is binary: below the threshold the surface is not there at all.
      if (alpha < hit_mat.alpha_cutoff) {
        ray_tmin = hit.t + std::max(shading_.scene_radius * 1e-5f, 1e-6f);
        continue;
      }
      alpha = 1.0f;
    }
  }

  float shaded[3];
  ShadeSurface(shading_, surf, direction, &CpuRenderer::OccludedThunk,
               const_cast<CpuRenderer*>(this), shaded);
  for (int i = 0; i < 3; i++) out_rgb[i] += shaded[i] * alpha * transmittance;

  transmittance *= (1.0f - alpha);
  if (transmittance <= 1.0f / 255.0f) return;

  // Keep going for whatever shows through this surface.
  ray_tmin = hit.t + std::max(shading_.scene_radius * 1e-5f, 1e-6f);
  }

  // Ran out of layers: whatever is still transmitting sees the background.
  if (transmittance > 0.0f) {
    float bg[3];
    ShadeBackground(shading_, direction, bg);
    for (int i = 0; i < 3; i++) out_rgb[i] += bg[i] * transmittance;
  }
}

// ---------------------------------------------------------------------------
// Passes
// ---------------------------------------------------------------------------

void CpuRenderer::RenderCoarse() {
  // One ray per kCoarseFactor^2 block, expanded to fill. This is the sub-100ms
  // "first pixel" that makes the app feel instant.
  const int bw = (width_ + kCoarseFactor - 1) / kCoarseFactor;
  const int bh = (height_ + kCoarseFactor - 1) / kCoarseFactor;

  RunParallel(bh, [&](int by) {
    for (int bx = 0; bx < bw; bx++) {
        const int px = std::min(width_ - 1, bx * kCoarseFactor + kCoarseFactor / 2);
        const int py = std::min(height_ - 1, by * kCoarseFactor + kCoarseFactor / 2);
        float rgb[3];
        TracePixel(px, py, 0, rgb);
        const uint32_t packed = PackLinearToArgb(rgb);

        const int x0 = bx * kCoarseFactor;
        const int y0 = by * kCoarseFactor;
        const int x1 = std::min(width_, x0 + kCoarseFactor);
        const int y1 = std::min(height_, y0 + kCoarseFactor);
        for (int y = y0; y < y1; y++) {
          uint32_t* row = pixels_.data() + size_t(y) * width_;
          for (int x = x0; x < x1; x++) row[x] = packed;
        }
    }
  });
}

void CpuRenderer::RenderTile(int tile_index, int sample_index) {
  const int tiles_x = (width_ + kTileSize - 1) / kTileSize;
  const int tx = tile_index % tiles_x;
  const int ty = tile_index / tiles_x;
  const int x0 = tx * kTileSize;
  const int y0 = ty * kTileSize;
  const int x1 = std::min(width_, x0 + kTileSize);
  const int y1 = std::min(height_, y0 + kTileSize);

  const float inv_n = 1.0f / float(sample_index + 1);

  for (int y = y0; y < y1; y++) {
    for (int x = x0; x < x1; x++) {
      float rgb[3];
      TracePixel(x, y, sample_index, rgb);

      const size_t ai = (size_t(y) * width_ + size_t(x)) * 3;
      float avg[3];
      for (int i = 0; i < 3; i++) {
        accum_[ai + i] += rgb[i];
        avg[i] = accum_[ai + i] * inv_n;
      }
      pixels_[size_t(y) * width_ + size_t(x)] = PackLinearToArgb(avg);
    }
  }
}

RenderStatus CpuRenderer::RenderStep(double budget_ms) {
  RenderStatus status;
  const int sample_target = SampleTarget();
  status.samples_target = sample_target;
  status.tiles_total = TileCount();

  if (!scene_) {
    status.converged = true;
    return status;
  }

  PrepareShading();

  const auto start = std::chrono::steady_clock::now();
  auto elapsed_ms = [&] {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - start)
        .count();
  };

  if (!coarse_done_) {
    RenderCoarse();
    coarse_done_ = true;
    status.produced_pixels = true;
    status.samples_done = 0;
    status.tiles_done = 0;
    return status;
  }

  if (sample_index_ >= sample_target) {
    status.converged = true;
    status.samples_done = sample_target;
    status.tiles_done = status.tiles_total;
    return status;
  }

  // Render whole tiles until the millisecond budget runs out, so the UI stays
  // responsive no matter how heavy the scene is.
  const int total_tiles = status.tiles_total;

  while (elapsed_ms() < budget_ms && sample_index_ < sample_target) {
    // Grab a batch of tiles proportional to the worker count; one batch is the
    // granularity at which we re-check the time budget.
    const int batch = std::min(total_tiles - next_tile_,
                               std::max(1, settings_.threads) * 2);
    if (batch <= 0) break;

    const int batch_end = next_tile_ + batch;
    const int sample = sample_index_;

    RunParallel(batch, [&](int offset) {
      RenderTile(next_tile_ + offset, sample);
    });

    next_tile_ = batch_end;
    status.produced_pixels = true;

    if (next_tile_ >= total_tiles) {
      next_tile_ = 0;
      sample_index_++;
    }
  }

  status.samples_done = sample_index_;
  status.tiles_done = next_tile_;
  status.converged = sample_index_ >= sample_target;
  if (status.converged) status.tiles_done = total_tiles;
  return status;
}

}  // namespace

std::unique_ptr<Renderer> CreateCpuRenderer(
    std::shared_ptr<PickAccel> accel) {
  return std::unique_ptr<Renderer>(new CpuRenderer(std::move(accel)));
}

}  // namespace lusdql
