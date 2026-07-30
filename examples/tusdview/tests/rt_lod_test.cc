// SPDX-License-Identifier: Apache-2.0
#include "rt_lod.hh"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;
#define CHECK(expr)                                                          \
  do {                                                                       \
    if (!(expr)) {                                                           \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                   #expr);                                                   \
      ++failures;                                                            \
    }                                                                        \
  } while (false)

void TestSubpixelAggregateBound() {
  tusdview::RtLodGridCell cell{};
  cell.wmn[0] = cell.wmn[1] = -0.2f;
  cell.wmx[0] = cell.wmx[1] = 0.2f;
  cell.wmn[2] = -100.2f;
  cell.wmx[2] = -99.8f;
  cell.maxInstanceRadius = 0.05f;
  tusdview::RtLodCamera cam;
  cam.eye = {0.0f, 0.0f, 0.0f};
  cam.forward = {0.0f, 0.0f, -1.0f};
  cam.nearPlane = 0.1f;
  cam.focalPx = 1000.0f;
  cam.cullPx = 1.0f;
  CHECK(tusdview::IsSubpixelAggregateCell(cell, cam));
  cell.maxInstanceRadius = 0.2f;
  CHECK(!tusdview::IsSubpixelAggregateCell(cell, cam));
  cell.maxInstanceRadius = 0.05f;
  cell.wmn[0] = -1.0f;
  cell.wmx[0] = 1.0f;
  CHECK(!tusdview::IsSubpixelAggregateCell(cell, cam));
}

void TestGridTracksMaximumInstanceRadius() {
  constexpr uint32_t kInstances = 512;
  std::vector<float> xforms(kInstances * 12, 0.0f);
  for (uint32_t i = 0; i < kInstances; ++i) {
    float* x = &xforms[i * 12];
    x[0] = x[5] = x[10] = 1.0f;
    x[3] = static_cast<float>(i * 10);
  }
  const float lo[3] = {-1.0f, -1.0f, -1.0f};
  const float hi[3] = {1.0f, 1.0f, 1.0f};
  tusdview::RtLodProto proto;
  proto.instanceXforms = xforms.data();
  proto.instanceCount = kInstances;
  proto.protoAabbMin = lo;
  proto.protoAabbMax = hi;
  tusdview::RtLodGrid grid;
  tusdview::BuildRtLodGrid(proto, 2, &grid);
  CHECK(grid.valid);
  CHECK(!grid.cells.empty());
  for (const tusdview::RtLodGridCell& cell : grid.cells)
    CHECK(std::fabs(cell.maxInstanceRadius - std::sqrt(3.0f)) < 1e-5f);
}

}  // namespace

int main() {
  TestSubpixelAggregateBound();
  TestGridTracksMaximumInstanceRadius();
  return failures == 0 ? 0 : 1;
}
