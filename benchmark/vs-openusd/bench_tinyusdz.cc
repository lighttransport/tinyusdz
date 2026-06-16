// TinyUSDZ side of the benchmark harness.
//
// Usage: bench_tinyusdz <input.usd[a|c|z]> [--iters N]
// Output: CSV lines `op,status,iters,median_ms,min_ms,max_ms`

#include <cstdio>
#include <string>

#include "bench_common.hh"

#include "tinyusdz.hh"
#include "composition.hh"
#include "io-util.hh"
#include "usda-writer.hh"
#include "usdc-writer.hh"

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: %s <input.usd> [--iters N]\n", argv[0]);
    return 1;
  }
  const std::string input = argv[1];
  const int iters = bench::parse_iters(argc, argv);

  std::string warn, err;

  // Parse: file -> Stage
  tinyusdz::Stage stage;
  bench::run("parse", iters, [&]() {
    stage = tinyusdz::Stage();
    warn.clear();
    err.clear();
    return tinyusdz::LoadUSDFromFile(input, &stage, &warn, &err);
  });
  if (!err.empty()) {
    std::fprintf(stderr, "parse error: %s\n", err.c_str());
    return 1;
  }

  // Write USDA: Stage -> ASCII file
  const std::string out_usda = "/tmp/bench_tinyusdz_out.usda";
  bench::run("write_usda", iters, [&]() {
    warn.clear();
    err.clear();
    return tinyusdz::usda::SaveAsUSDA(out_usda, stage, &warn, &err);
  });

  // Write USDC: Stage -> Crate binary (in-memory)
  bench::run("write_usdc", iters, [&]() {
    warn.clear();
    err.clear();
    std::vector<uint8_t> data;
    return tinyusdz::usdc::SaveAsUSDCToMemory(stage, &data, &warn, &err);
  });

  // Composite: load as layer, apply composition arcs, build Stage
  bench::run("composite", iters, [&]() {
    warn.clear();
    err.clear();
    tinyusdz::Layer root_layer;
    if (!tinyusdz::LoadLayerFromFile(input, &root_layer, &warn, &err)) {
      return false;
    }
    tinyusdz::AssetResolutionResolver resolver;
    resolver.set_current_working_path(
        tinyusdz::io::GetBaseDir(input).empty()
            ? "."
            : tinyusdz::io::GetBaseDir(input));
    resolver.set_search_paths({resolver.current_working_path()});

    tinyusdz::Layer sublayered, referenced, payloaded;
    if (!tinyusdz::CompositeSublayers(resolver, root_layer, &sublayered, &warn,
                                      &err)) {
      return false;
    }
    if (!tinyusdz::CompositeReferences(resolver, sublayered, &referenced,
                                       &warn, &err)) {
      return false;
    }
    if (!tinyusdz::CompositePayload(resolver, referenced, &payloaded, &warn,
                                    &err)) {
      return false;
    }
    tinyusdz::Stage composited;
    return tinyusdz::LayerToStage(std::move(payloaded), &composited, &warn,
                                  &err);
  });
  if (!err.empty()) {
    std::fprintf(stderr, "composite error: %s\n", err.c_str());
  }

  return 0;
}
