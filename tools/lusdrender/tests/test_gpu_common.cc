// SPDX-License-Identifier: Apache-2.0
// Unit test for lusdrender's shared GPU geometry flattening helper.
#include <cstdio>
#include <vector>

#include "lightrt_c_tri.h"
#include "lusdr_gpu_common.hh"

using namespace lusdr;

namespace lusdr {
unsigned WorkerThreadCount(int requested) {
  return requested > 0 ? unsigned(requested) : 1u;
}
}  // namespace lusdr

static int fails = 0;
#define CHECK(c, m) \
  do { \
    if (!(c)) { \
      std::printf("FAIL: %s (line %d)\n", m, __LINE__); \
      ++fails; \
    } \
  } while (0)

static RTPreviewStats::MeshGeometry Triangle() {
  RTPreviewStats::MeshGeometry g;
  g.positions = {0.0f, 0.0f, 0.0f,
                 1.0f, 0.0f, 0.0f,
                 0.0f, 1.0f, 0.0f};
  g.normals = {0.0f, 0.0f, 1.0f,
               0.0f, 0.0f, 1.0f,
               0.0f, 0.0f, 1.0f};
  g.indices = {0, 1, 2};
  return g;
}

static void FreeScene(GpuTriScene *s) {
  if (s->scene) {
    lrt_tri_scene_free(s->scene);
    s->scene = nullptr;
  }
}

int main() {
  const std::vector<Vec3> colors = {Vec3{0.25f, 0.5f, 0.75f}};
  std::vector<RTPreviewStats::MeshGeometry> geos;
  geos.push_back(Triangle());

  GpuTriScene flat;
  CHECK(BuildGpuTriScene(colors, geos, 1, false, &flat), "valid flatten");
  CHECK(flat.scene == nullptr, "skip CPU scene");
  CHECK(flat.ntris == 1, "triangle count");
  CHECK(flat.flat_verts.size() == 9, "vertex storage");
  CHECK(flat.flat_idx.size() == 3, "index storage");
  CHECK(flat.flat_idx[0] == 0 && flat.flat_idx[1] == 1 && flat.flat_idx[2] == 2,
        "index values");
  CHECK(flat.base_colors.size() == 1, "base color count");
  CHECK(flat.normals.size() == 1 && flat.vn0.size() == 1 && flat.vn1.size() == 1 &&
            flat.vn2.size() == 1,
        "normal counts");

  GpuTriScene cpu;
  CHECK(BuildGpuTriScene(colors, geos, 1, true, &cpu), "valid CPU BVH build");
  CHECK(cpu.scene != nullptr, "CPU scene built");
  FreeScene(&cpu);

  std::vector<RTPreviewStats::MeshGeometry> bad_pos = geos;
  bad_pos[0].positions.pop_back();
  GpuTriScene out_bad_pos;
  CHECK(!BuildGpuTriScene(colors, bad_pos, 1, false, &out_bad_pos),
        "reject malformed positions");

  std::vector<RTPreviewStats::MeshGeometry> bad_idx_count = geos;
  bad_idx_count[0].indices.pop_back();
  GpuTriScene out_bad_idx_count;
  CHECK(!BuildGpuTriScene(colors, bad_idx_count, 1, false, &out_bad_idx_count),
        "reject malformed index count");

  std::vector<RTPreviewStats::MeshGeometry> bad_idx = geos;
  bad_idx[0].indices[2] = 3;
  GpuTriScene out_bad_idx;
  CHECK(!BuildGpuTriScene(colors, bad_idx, 1, false, &out_bad_idx),
        "reject out-of-range index");

  std::vector<RTPreviewStats::MeshGeometry> empty;
  GpuTriScene out_empty;
  CHECK(!BuildGpuTriScene(colors, empty, 1, false, &out_empty), "reject empty scene");

  size_t nrays = 0;
  CHECK(ValidateGpuFrameSize(200, 150, 2, "test", &nrays), "valid frame size");
  CHECK(nrays == 60000, "valid ray count");
  CHECK(!ValidateGpuFrameSize(0, 150, 2, "test", &nrays), "reject zero width");
  CHECK(!ValidateGpuFrameSize(65536, 65536, 1, "test", &nrays),
        "reject uint32 pixel overflow");
  CHECK(!ValidateGpuFrameSize(65535, 65535, 2, "test", &nrays),
        "reject uint32 sample overflow");

  if (fails == 0) {
    std::printf("test_lusdr_gpu_common: ALL PASS\n");
    return 0;
  }
  std::printf("test_lusdr_gpu_common: %d FAIL(s)\n", fails);
  return 1;
}
