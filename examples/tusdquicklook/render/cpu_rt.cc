// SPDX-License-Identifier: Apache-2.0
//
// tusdquicklook — CPU ray tracer on the vendored lightrt C kernel.
//
// Structure:
//   * one BLAS per QlMesh, built with lrt_tri_scene_build_indexed (the
//     tinyusdz-local indexed build, which keeps the indexed data instead of
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
#include <mutex>
#include <thread>
#include <vector>

#include "render/renderer.hh"
#include "render/shade.hh"

extern "C" {
#include "lightrt_c_tri.h"
}

namespace tusdql {

namespace {

constexpr int kTileSize = 32;
constexpr int kCoarseFactor = 8;

struct BlasEntry {
  lrt_tri_scene* scene = nullptr;
  size_t mesh_index = 0;
};

class CpuRenderer final : public Renderer {
 public:
  ~CpuRenderer() override { ReleaseAccel(); }

  bool Init(int width, int height, const RenderSettings& settings,
            std::string* err) override {
    settings_ = settings;
    if (settings_.threads < 1) settings_.threads = 1;
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
    ReleaseAccel();
    synced_mesh_count_ = 0;
    ResetProgression();
  }

  void SyncScene() override {
    if (!scene_) return;
    if (scene_->meshes.size() == synced_mesh_count_) return;
    BuildAccel();
    ResetProgression();
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
  void ReleaseAccel();
  void BuildAccel();
  void ResetProgression();
  void PrepareShading();

  void RenderCoarse();
  void RenderTile(int tile_index, int sample_index);
  void ResolveTileToPixels(int tile_index);

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

  std::vector<BlasEntry> blas_;
  std::vector<lrt_tri_scene*> blas_ptrs_;
  lrt_tlas* tlas_ = nullptr;
  size_t synced_mesh_count_ = 0;

  ShadingContext shading_;
  bool shading_valid_ = false;

  // Progression state
  bool coarse_done_ = false;
  int sample_index_ = 0;
  int next_tile_ = 0;
};

// ---------------------------------------------------------------------------

void CpuRenderer::ReleaseAccel() {
  if (tlas_) {
    lrt_tlas_free(tlas_);
    tlas_ = nullptr;
  }
  for (BlasEntry& e : blas_) {
    if (e.scene) lrt_tri_scene_free(e.scene);
  }
  blas_.clear();
  blas_ptrs_.clear();
}

void CpuRenderer::BuildAccel() {
  ReleaseAccel();
  if (!scene_ || scene_->meshes.empty()) {
    synced_mesh_count_ = scene_ ? scene_->meshes.size() : 0;
    return;
  }

  lrt_tri_build_options opts{};
  // LBVH: a previewer wants the image on screen, not the last 10% of traversal
  // speed, and the build is the user-visible latency here.
  opts.quality = LRT_TRI_BUILD_FAST;
  opts.layout = LRT_TRI_LAYOUT_AUTO;
  opts.max_leaf_size = 0;
  opts.num_threads = static_cast<unsigned>(settings_.threads);

  blas_.reserve(scene_->meshes.size());
  blas_ptrs_.reserve(scene_->meshes.size());
  std::vector<lrt_instance> instances;
  instances.reserve(scene_->meshes.size());

  for (size_t i = 0; i < scene_->meshes.size(); i++) {
    const QlMesh& m = scene_->meshes[i];
    if (m.triangle_count() == 0) continue;

    lrt_result err = LRT_RESULT_OK;
    lrt_tri_scene* bs = lrt_tri_scene_build_indexed(
        m.positions.data(), m.vertex_count(), m.indices.data(),
        m.triangle_count(), &opts, &err);
    if (!bs) continue;

    BlasEntry entry;
    entry.scene = bs;
    entry.mesh_index = i;
    const uint32_t blas_id = static_cast<uint32_t>(blas_.size());
    blas_.push_back(entry);
    blas_ptrs_.push_back(bs);

    lrt_instance inst{};
    inst.blas_id = blas_id;
    // Identity 3x4: positions were already baked into world space by the
    // loader, so the TLAS exists purely to key hits back to a mesh.
    inst.obj2world[0] = 1.0f;
    inst.obj2world[5] = 1.0f;
    inst.obj2world[10] = 1.0f;
    inst.instance_id = blas_id;
    inst.mask = 0xFFFFFFFFu;
    instances.push_back(inst);
  }

  if (!instances.empty()) {
    lrt_result err = LRT_RESULT_OK;
    tlas_ = lrt_tlas_build(blas_ptrs_.data(), blas_ptrs_.size(),
                           instances.data(), instances.size(), &opts, &err);
  }

  synced_mesh_count_ = scene_->meshes.size();
  shading_valid_ = false;
}

void CpuRenderer::ResetProgression() {
  coarse_done_ = false;
  sample_index_ = 0;
  next_tile_ = 0;
  shading_valid_ = false;
  std::fill(accum_.begin(), accum_.end(), 0.0f);
}

void CpuRenderer::PrepareShading() {
  if (shading_valid_ || !scene_) return;
  float eye[3], right[3], up[3], fwd[3];
  camera_.Eye(eye);
  camera_.Basis(right, up, fwd);
  BuildLightRig(*scene_, eye, fwd, right, up, &shading_);
  shading_.shadows = settings_.shadows;
  shading_valid_ = true;
}

// ---------------------------------------------------------------------------
// Tracing
// ---------------------------------------------------------------------------

bool CpuRenderer::Occluded(const float origin[3], const float direction[3],
                           float max_distance) const {
  if (!tlas_) return false;
  lrt_ray ray{};
  ray.org[0] = origin[0];
  ray.org[1] = origin[1];
  ray.org[2] = origin[2];
  ray.dir[0] = direction[0];
  ray.dir[1] = direction[1];
  ray.dir[2] = direction[2];
  ray.tmin = 0.0f;
  ray.tmax = max_distance;
  return lrt_tlas_occluded1(tlas_, &ray, 0xFFFFFFFFu) != 0;
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

  if (!tlas_) {
    ShadeBackground(shading_, direction, out_rgb);
    return;
  }

  lrt_ray ray{};
  std::memcpy(ray.org, origin, sizeof(ray.org));
  std::memcpy(ray.dir, direction, sizeof(ray.dir));
  ray.tmin = 0.0f;
  ray.tmax = 1e30f;

  lrt_tlas_hit hit{};
  if (!lrt_tlas_intersect1(tlas_, &ray, 0xFFFFFFFFu, &hit)) {
    ShadeBackground(shading_, direction, out_rgb);
    return;
  }

  if (hit.inst_id >= blas_.size()) {
    ShadeBackground(shading_, direction, out_rgb);
    return;
  }
  const QlMesh& mesh = scene_->meshes[blas_[hit.inst_id].mesh_index];
  const size_t tri = hit.prim_id;
  if (tri * 3 + 2 >= mesh.indices.size()) {
    ShadeBackground(shading_, direction, out_rgb);
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

  ShadeSurface(shading_, surf, direction, &CpuRenderer::OccludedThunk,
               const_cast<CpuRenderer*>(this), out_rgb);
}

// ---------------------------------------------------------------------------
// Passes
// ---------------------------------------------------------------------------

void CpuRenderer::RenderCoarse() {
  // One ray per kCoarseFactor^2 block, expanded to fill. This is the sub-100ms
  // "first pixel" that makes the app feel instant.
  const int bw = (width_ + kCoarseFactor - 1) / kCoarseFactor;
  const int bh = (height_ + kCoarseFactor - 1) / kCoarseFactor;

  const int nthreads = std::max(1, settings_.threads);
  std::atomic<int> next_row{0};
  std::vector<std::thread> workers;
  workers.reserve(size_t(nthreads - 1));

  auto body = [&] {
    for (;;) {
      const int by = next_row.fetch_add(1);
      if (by >= bh) return;
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
    }
  };

  for (int i = 0; i < nthreads - 1; i++) workers.emplace_back(body);
  body();
  for (std::thread& t : workers) t.join();
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
  status.samples_target = settings_.spp;
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

  if (sample_index_ >= settings_.spp) {
    status.converged = true;
    status.samples_done = settings_.spp;
    status.tiles_done = status.tiles_total;
    return status;
  }

  // Render whole tiles until the millisecond budget runs out, so the UI stays
  // responsive no matter how heavy the scene is.
  const int nthreads = std::max(1, settings_.threads);
  const int total_tiles = status.tiles_total;

  while (elapsed_ms() < budget_ms && sample_index_ < settings_.spp) {
    // Grab a batch of tiles proportional to the worker count; one batch is the
    // granularity at which we re-check the time budget.
    const int batch = std::min(total_tiles - next_tile_, nthreads * 2);
    if (batch <= 0) break;

    std::atomic<int> cursor{next_tile_};
    const int batch_end = next_tile_ + batch;
    const int sample = sample_index_;

    auto body = [&] {
      for (;;) {
        const int t = cursor.fetch_add(1);
        if (t >= batch_end) return;
        RenderTile(t, sample);
      }
    };

    std::vector<std::thread> workers;
    workers.reserve(size_t(nthreads - 1));
    for (int i = 0; i < nthreads - 1; i++) workers.emplace_back(body);
    body();
    for (std::thread& t : workers) t.join();

    next_tile_ = batch_end;
    status.produced_pixels = true;

    if (next_tile_ >= total_tiles) {
      next_tile_ = 0;
      sample_index_++;
    }
  }

  status.samples_done = sample_index_;
  status.tiles_done = next_tile_;
  status.converged = sample_index_ >= settings_.spp;
  return status;
}

}  // namespace

std::unique_ptr<Renderer> CreateCpuRenderer() {
  return std::unique_ptr<Renderer>(new CpuRenderer());
}

}  // namespace tusdql
