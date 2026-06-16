// OpenUSD (pxr) side of the benchmark harness.
//
// Usage: bench_openusd <input.usd[a|c|z]> [--iters N]
// Output: CSV lines `op,status,iters,median_ms,min_ms,max_ms`

#include <cstdio>
#include <string>

#include "bench_common.hh"

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/sdf/layer.h>

PXR_NAMESPACE_USING_DIRECTIVE

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: %s <input.usd> [--iters N]\n", argv[0]);
    return 1;
  }
  const std::string input = argv[1];
  const int iters = bench::parse_iters(argc, argv);

  // Parse: file -> single layer (no composition), closest to a raw parse.
  // SdfLayer caches by identifier, so Reload(force=true) on repeat runs.
  bench::run("parse", iters, [&]() {
    static SdfLayerRefPtr layer;
    if (!layer) {
      layer = SdfLayer::FindOrOpen(input);
      return bool(layer);
    }
    return layer->Reload(/*force=*/true);
  });

  UsdStageRefPtr stage = UsdStage::Open(input);
  if (!stage) {
    std::fprintf(stderr, "failed to open stage: %s\n", input.c_str());
    return 1;
  }

  // Write USDA: composed root layer -> ASCII file
  bench::run("write_usda", iters, [&]() {
    return stage->GetRootLayer()->Export("/tmp/bench_openusd_out.usda");
  });

  // Write USDC: composed root layer -> Crate binary file
  bench::run("write_usdc", iters, [&]() {
    return stage->GetRootLayer()->Export("/tmp/bench_openusd_out.usdc");
  });

  // Composite: open stage (full composition) + flatten
  bench::run("composite", iters, [&]() {
    UsdStageRefPtr s = UsdStage::Open(input, UsdStage::LoadAll);
    if (!s) {
      return false;
    }
    SdfLayerRefPtr flat = s->Flatten();
    return bool(flat);
  });

  return 0;
}
