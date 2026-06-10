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

  // --- Scenario 2: list-op reference merge across sublayers + sublayer-compose
  // on a referenced asset (the ALab shot pattern). ---
  {
    const char *multi = "tests/feat/large-scene/fixture/multi/root.usda";
    LargeSceneLoadOptions o2;
    o2.payload_mode = LargeSceneLoadOptions::PayloadMode::LoadAll;
    o2.allow_parent_relative_paths = true;
    LargeSceneLoader l2;
    std::string w2, e2;
    if (!l2.Load(multi, o2, &w2, &e2)) {
      std::cerr << "multi load failed: " << e2 << "\n";
      ++g_failures;
    } else {
      // Two sublayers each prepend a reference to /P. Both must compose
      // (list-op accumulation), so /P has content from BOTH assets:
      //   /P/MA  -- from asset_a.usda, whose geometry is aggregated through its
      //             OWN subLayers (tests sublayer-compose on a reference).
      //   /P/MB  -- from asset_b.usda (the WEAKER sublayer's reference, which
      //             was previously dropped before list-op merging).
      CHECK(bool(l2.stage().GetPrimAtPath(Path("/P/MA", ""))),
            "/P/MA (referenced asset's own subLayers composed)");
      CHECK(bool(l2.stage().GetPrimAtPath(Path("/P/MB", ""))),
            "/P/MB (weaker sublayer's reference merged via list-op)");
    }
  }

  // --- Scenario 3: variant-content composition. The variantSet content is in a
  // weaker sublayer, the selection in the root; the selected variant's child
  // prims must reconstruct (the ALab baked_procedurals pattern). ---
  {
    const char *vfix = "tests/feat/large-scene/fixture/variant/root.usda";
    LargeSceneLoadOptions o3;
    o3.payload_mode = LargeSceneLoadOptions::PayloadMode::LoadAll;
    LargeSceneLoader l3;
    std::string w3, e3;
    if (!l3.Load(vfix, o3, &w3, &e3)) {
      std::cerr << "variant load failed: " << e3 << "\n";
      ++g_failures;
    } else {
      CHECK(bool(l3.stage().GetPrimAtPath(Path("/P/GEO", ""))),
            "/P/GEO (selected variant's child composed)");
      CHECK(bool(l3.stage().GetPrimAtPath(Path("/P/GEO/M", ""))),
            "/P/GEO/M (variant child's descendant composed)");
    }
  }

  // --- Scenario 4: reference-introduced variant set. The variant set + content
  // are defined in a referenced asset; the selection is on the stronger
  // referencing layer (the ALab character pattern). The DAG must compose the
  // selected variant's content across nodes. ---
  {
    const char *rvfix = "tests/feat/large-scene/fixture/variant-ref/root.usda";
    LargeSceneLoadOptions o4;
    o4.payload_mode = LargeSceneLoadOptions::PayloadMode::LoadAll;
    LargeSceneLoader l4;
    std::string w4, e4;
    if (!l4.Load(rvfix, o4, &w4, &e4)) {
      std::cerr << "variant-ref load failed: " << e4 << "\n";
      ++g_failures;
    } else {
      CHECK(bool(l4.stage().GetPrimAtPath(Path("/P/GEO", ""))),
            "/P/GEO (reference-introduced variant content composed)");
      CHECK(bool(l4.stage().GetPrimAtPath(Path("/P/GEO/M", ""))),
            "/P/GEO/M (reference variant child's descendant composed)");
    }
  }

  if (g_failures == 0) {
    std::cout << "Large-scene composition tests passed.\n";
    return 0;
  }
  std::cerr << g_failures << " check(s) failed.\n";
  return 1;
}
