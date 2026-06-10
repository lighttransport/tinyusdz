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
#include <vector>

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

  // --- Scenario 5: variant content can author composition arcs on the selected
  // prim itself. After resolving the variant, the DAG must re-scan the resolved
  // PrimSpec so self-authored references/payloads are evaluated. ---
  {
    const char *safix =
        "tests/feat/large-scene/fixture/variant-self-arc/root.usda";

    LargeSceneLoadOptions o5;
    o5.payload_mode = LargeSceneLoadOptions::PayloadMode::LoadAll;
    LargeSceneLoader l5;
    std::string w5, e5;
    if (!l5.Load(safix, o5, &w5, &e5)) {
      std::cerr << "variant-self-arc load failed: " << e5 << "\n";
      ++g_failures;
    } else {
      CHECK(bool(l5.stage().GetPrimAtPath(Path("/P/M", ""))),
            "/P/M (selected variant's self-reference composed)");
      CHECK(bool(l5.stage().GetPrimAtPath(Path("/Q/PM", ""))),
            "/Q/PM (selected variant's self-payload composed)");
    }

    LargeSceneLoadOptions o5d;
    o5d.payload_mode = LargeSceneLoadOptions::PayloadMode::LoadNone;
    LargeSceneLoader l5d;
    std::string w5d, e5d;
    if (!l5d.Load(safix, o5d, &w5d, &e5d)) {
      std::cerr << "variant-self-arc deferred load failed: " << e5d << "\n";
      ++g_failures;
    } else {
      CHECK(l5d.deferred_count() == 1,
            "selected variant's self-payload deferred");
      const std::vector<Path> deferred = l5d.deferred_payload_paths();
      if (!deferred.empty()) {
        CHECK(deferred.front() == Path("/Q", ""),
              "deferred self-payload recorded on /Q");
      }
      CHECK(!bool(l5d.stage().GetPrimAtPath(Path("/Q/PM", ""))),
            "/Q/PM absent while self-payload is deferred");

      std::string lwarn, lerr;
      if (!l5d.load_payload(Path("/Q", ""), &lwarn, &lerr)) {
        std::cerr << "variant-self-arc load_payload failed: " << lerr << "\n";
        ++g_failures;
      } else {
        std::string rwarn, rerr;
        if (!l5d.rebuild_stage(&rwarn, &rerr)) {
          std::cerr << "variant-self-arc rebuild after load failed: " << rerr
                    << "\n";
          ++g_failures;
        } else {
          CHECK(l5d.deferred_count() == 0,
                "deferred self-payload cleared after load_payload");
          CHECK(bool(l5d.stage().GetPrimAtPath(Path("/Q/PM", ""))),
                "/Q/PM appears after load_payload + rebuild_stage");
        }
      }

      std::string uwarn, uerr;
      if (!l5d.unload_payload(Path("/Q", ""), &uwarn, &uerr)) {
        std::cerr << "variant-self-arc unload_payload failed: " << uerr
                  << "\n";
        ++g_failures;
      } else {
        std::string rwarn, rerr;
        if (!l5d.rebuild_stage(&rwarn, &rerr)) {
          std::cerr << "variant-self-arc rebuild after unload failed: " << rerr
                    << "\n";
          ++g_failures;
        } else {
          CHECK(l5d.deferred_count() == 1,
                "deferred self-payload restored after unload_payload");
          CHECK(!bool(l5d.stage().GetPrimAtPath(Path("/Q/PM", ""))),
                "/Q/PM removed after unload_payload + rebuild_stage");
        }
      }
    }
  }

  // --- Scenario 6: lazy payload loading must stamp the loaded payload layer
  // with its resolved directory. Otherwise nested relative arcs authored inside
  // the streamed payload resolve against the process/root cwd instead of the
  // payload file's directory. ---
  {
    const char *nfix =
        "tests/feat/large-scene/fixture/deferred-nested/root.usda";

    LargeSceneLoadOptions o6;
    o6.payload_mode = LargeSceneLoadOptions::PayloadMode::LoadNone;
    o6.allow_parent_relative_paths = true;
    LargeSceneLoader l6;
    std::string w6, e6;
    if (!l6.Load(nfix, o6, &w6, &e6)) {
      std::cerr << "deferred-nested load failed: " << e6 << "\n";
      ++g_failures;
    } else {
      CHECK(l6.deferred_count() == 1,
            "cross-directory payload deferred before streaming");
      CHECK(!bool(l6.stage().GetPrimAtPath(Path("/P/Nested/M", ""))),
            "/P/Nested/M absent before streaming nested payload");

      std::string lwarn, lerr;
      if (!l6.load_payload(Path("/P", ""), &lwarn, &lerr)) {
        std::cerr << "deferred-nested load_payload failed: " << lerr << "\n";
        ++g_failures;
      } else {
        std::string rwarn, rerr;
        if (!l6.rebuild_stage(&rwarn, &rerr)) {
          std::cerr << "deferred-nested rebuild after load failed: " << rerr
                    << "\n";
          ++g_failures;
        } else {
          CHECK(bool(l6.stage().GetPrimAtPath(Path("/P/Nested/M", ""))),
                "/P/Nested/M appears after streamed payload follows nested ./leaf.usda");
        }
      }
    }
  }

  // --- Scenario 7: resolver file descriptor budget. Large scene loading should
  // fail with a clear error when the configured descriptor/handle limit is
  // reached during sublayer/reference/payload loading. ---
  {
    LargeSceneLoadOptions olimit;
    olimit.payload_mode = LargeSceneLoadOptions::PayloadMode::LoadAll;
    olimit.max_file_descriptors = 0;
    LargeSceneLoader llimit;
    std::string wlimit, elimit;
    if (llimit.Load(kRoot, olimit, &wlimit, &elimit)) {
      std::cerr << "descriptor-limit load unexpectedly succeeded\n";
      ++g_failures;
    } else {
      CHECK(elimit.find("file descriptor limit reached") != std::string::npos,
            "descriptor-limit error is reported");
    }
  }

  // --- Scenario 8: descriptor-limit errors from reference/payload graph
  // composition are fatal even though ordinary missing assets are otherwise
  // skippable. ---
  {
    const char *fdref =
        "tests/feat/large-scene/fixture/fd-limit-ref/root.usda";

    LargeSceneLoadOptions okopts;
    okopts.payload_mode = LargeSceneLoadOptions::PayloadMode::LoadAll;
    LargeSceneLoader lok;
    std::string wok, eok;
    if (!lok.Load(fdref, okopts, &wok, &eok)) {
      std::cerr << "fd-limit-ref baseline load failed: " << eok << "\n";
      ++g_failures;
    } else {
      CHECK(bool(lok.stage().GetPrimAtPath(Path("/P/M", ""))),
            "fd-limit-ref baseline reference composed");
    }

    LargeSceneLoadOptions failopts;
    failopts.payload_mode = LargeSceneLoadOptions::PayloadMode::LoadAll;
    failopts.max_file_descriptors = 0;
    LargeSceneLoader lfail;
    std::string wfail, efail;
    if (lfail.Load(fdref, failopts, &wfail, &efail)) {
      std::cerr << "fd-limit-ref graph load unexpectedly succeeded\n";
      ++g_failures;
    } else {
      CHECK(efail.find("file descriptor limit reached") != std::string::npos,
            "descriptor-limit graph composition error is reported");
    }
  }

  if (g_failures == 0) {
    std::cout << "Large-scene composition tests passed.\n";
    return 0;
  }
  std::cerr << g_failures << " check(s) failed.\n";
  return 1;
}
