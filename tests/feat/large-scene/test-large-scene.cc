// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
// Regression test for cross-directory cwp (current-working-path) anchoring in
// composition, exercised through LargeSceneLoader.
//
// fixture/root.usda  (sublayers sub/mid.usda; authors `over P`)
//   -> fixture/sub/mid.usda  (`def P` references `./leaf.usda`)
//        -> fixture/sub/leaf.usda  (`def Leaf { def Mesh M }`)
//
// The `./leaf.usda` reference is authored in sub/mid.usda and must anchor to
// sub/, NOT to the root fixture/ directory. After sublayer flattening the merged
// `P` prim (root `over` + sub `def`) must keep sub/ as its working path so the
// reference resolves and /P/M appears in the composed stage.

#include <iostream>
#include <string>

#include "large-scene-loader.hh"
#include "stage.hh"

using namespace tinyusdz;

namespace {
int g_failures = 0;
#define CHECK(cond, msg)                                                  \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::cerr << "FAIL: " << (msg) << "  [" << #cond << "] (line "      \
                << __LINE__ << ")\n";                                     \
      ++g_failures;                                                       \
    }                                                                     \
  } while (0)

const char *kRoot = "tests/feat/large-scene/fixture/root.usda";
}  // namespace

int main(int argc, char **argv) {
  const std::string root = (argc > 1) ? argv[1] : kRoot;

  LargeSceneLoadOptions opts;
  // Follow references eagerly; there are no payloads in this fixture.
  opts.payload_mode = LargeSceneLoadOptions::PayloadMode::LoadAll;
  opts.allow_parent_relative_paths = true;
  opts.dedup_layers = true;  // route arc loads through the parse-once registry

  LargeSceneLoader loader;
  std::string warn, err;
  if (!loader.Load(root, opts, &warn, &err)) {
    std::cerr << "Load failed: " << err << "\n";
    return 1;
  }
  if (!warn.empty()) std::cerr << "warn: " << warn << "\n";

  // /P must compose (it exists as the root `over` merged with sub/mid.usda's
  // `def P`).
  CHECK(bool(loader.stage().GetPrimAtPath(Path("/P", ""))), "/P composed");

  // The crux: sub/mid.usda's `def P` references `@./leaf.usda@`, anchored to
  // sub/. After sublayer flattening the merged P (root `over` + sub `def`) must
  // keep sub/ as its working path, so the reference resolves and leaf.usda is
  // actually loaded. The parse-once registry counts exactly the files loaded via
  // composition arcs, so a non-zero parse count proves the cross-directory
  // reference resolved. Without correct cwp anchoring the relative `./leaf.usda`
  // would resolve against the root fixture/ dir, fail silently, and parse 0.
  CHECK(loader.layer_parse_count() >= 1,
        "cross-directory reference ./leaf.usda resolved (leaf.usda parsed)");
  std::cout << "  layer_parse_count = " << loader.layer_parse_count() << "\n";

  // The referenced prim's descendants must be reconstructed into the namespace:
  // /P references </Leaf>, and Leaf has a child Mesh M, so /P/M must exist.
  {
    auto m = loader.stage().GetPrimAtPath(Path("/P/M", ""));
    CHECK(bool(m), "/P/M reconstructed (referenced-prim descendant present)");
    if (m) CHECK((*m)->type_name() == "Mesh", "/P/M is a Mesh");
  }

  if (g_failures == 0) {
    std::cout << "Cross-directory cwp-anchoring test passed.\n";
    return 0;
  }
  std::cerr << g_failures << " check(s) failed.\n";
  return 1;
}
