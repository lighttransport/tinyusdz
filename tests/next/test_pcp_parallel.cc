// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Unit/stress tests for next::pcp parallel per-prim index building
// (PrewarmPrimIndices with num_threads > 1). Scenes are synthetically generated
// (prims / attributes / composition arcs) so the parallel build is exercised on
// a wide variety of topologies. The core invariant under test:
//
//   a parallel build (num_threads = N) yields, for every prim, an index that is
//   structurally identical to a serial (num_threads = 1) build, and the same
//   instance/prototype groupings -- because composed output is a pure function
//   of the (parse-once, shared) layers, independent of threading.
//
// Without -DTINYUSDZ_NEXT_ENABLE_THREAD the library's parallel path compiles out
// and every "parallel" run is just another serial run, so the comparisons hold
// trivially and these tests still build + pass.

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#if defined(TINYUSDZ_ENABLE_THREAD)
#include <atomic>
#include <thread>
#endif
#include <utility>
#include <vector>

#include "next/layer/layer.hh"
#include "next/pcp/cache.hh"
#include "next/resolver/asset-resolver.hh"

using namespace tinyusdz::next;

// ---------------------------------------------------------------------------
// Canonical, interning-order-independent dumps (compare composition, not the
// internal table integers, which legitimately differ between serial/parallel).
// ---------------------------------------------------------------------------

static std::string CanonIndex(const pcp::PrimIndex *idx) {
  if (!idx) return "<null>";
  std::string s = "n=" + std::to_string(idx->GetNodeCount()) + ";";
  for (uint16_t ni : idx->GetStrengthOrder()) {
    const pcp::CompNode &node = idx->GetNode(ni);
    s += std::to_string(static_cast<int>(node.arc_type)) + ":" +
         idx->SitePath(node) + ":" + (node.has_specs() ? "1" : "0") + "|";
  }
  return s;
}

static std::string CanonInstances(const pcp::Cache &cache) {
  std::vector<Path> protos = cache.GetPrototypePaths();
  std::vector<std::string> rows;
  for (const Path &p : protos) {
    std::vector<std::string> names;
    for (const Path &i : cache.GetInstancesForPrototype(p)) names.push_back(i.str());
    std::sort(names.begin(), names.end());
    std::string row = p.str() + "=>";
    for (const std::string &n : names) row += n + ",";
    rows.push_back(row);
  }
  std::sort(rows.begin(), rows.end());
  std::string out;
  for (const std::string &r : rows) out += r + ";";
  return out;
}

static std::string CanonDeferredPayloads(const pcp::Cache &cache) {
  std::vector<Path> paths = cache.GetDeferredPayloadPaths();
  std::vector<std::string> names;
  for (const Path &p : paths) names.push_back(p.str());
  std::sort(names.begin(), names.end());
  std::string out;
  for (const std::string &n : names) out += n + ";";
  return out;
}

// ---------------------------------------------------------------------------
// Synthetic scene generation.
// ---------------------------------------------------------------------------

struct Scene {
  std::shared_ptr<Layer> root;
  std::vector<std::pair<std::string, std::shared_ptr<Layer>>> assets;  // id -> layer
  std::vector<Path> paths;        // prims to prewarm + compare
  std::vector<std::string> path_strs;
  size_t n_assets = 0;
};

// A reusable asset: prim "A" (type `type`) with `nprops` attributes of mixed
// value types and `nchildren` child prims (each with its own attribute).
static std::shared_ptr<Layer> MakeAsset(const std::string &type, int nprops,
                                        int nchildren) {
  auto a = std::make_shared<Layer>();
  LayerBuilder ab(*a);
  ab.begin_prim("A", type);
  for (int i = 0; i < nprops; ++i) {
    const std::string nm = "p" + std::to_string(i);
    switch (i % 3) {
      case 0: ab.add_property(nm, Value::MakeFloat3(float(i), 1, 2)); break;
      case 1: ab.add_property(nm, Value::MakeInt3(i, i + 1, i + 2)); break;
      default: ab.add_property(nm, Value::MakeToken("tok" + std::to_string(i))); break;
    }
  }
  for (int c = 0; c < nchildren; ++c) {
    ab.begin_prim("C" + std::to_string(c), "Sphere");
    ab.add_property("cp", Value::MakeFloat2(float(c), float(c)));
    ab.end_prim();
  }
  ab.end_prim();
  ab.finalize();
  return a;
}

// Deterministic pseudo-random spreader (no <random> / Date dependence).
static uint32_t Mix(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return x;
}

// Generate a scene with `n_root` prims under /World, each given one of several
// composition shapes (reference / inherit / reference+inherit / variant /
// payload / specialize / instanceable-reference), round-robin over `n_assets`
// shared assets. `exotic` enables variants/payloads/specializes (otherwise those
// slots fall back to plain references, for a leaner stress topology). `seed`
// shuffles which prim gets which shape (deterministically).
static Scene GenScene(int n_root, int n_assets, bool exotic, uint32_t seed) {
  Scene sc;
  sc.n_assets = static_cast<size_t>(n_assets);
  for (int i = 0; i < n_assets; ++i) {
    sc.assets.push_back({"asset_" + std::to_string(i),
                         MakeAsset("Mesh", 2 + (i % 3), 1 + (i % 2))});
  }

  auto root = std::make_shared<Layer>();
  LayerBuilder lb(*root);

  lb.begin_prim("World", "Xform");
  for (int i = 0; i < n_root; ++i) {
    const std::string rn = "R" + std::to_string(i);
    const int ai = i % n_assets;
    const std::string aref = "@asset_" + std::to_string(ai) + "@</A>";
    const uint32_t shape = Mix(static_cast<uint32_t>(i) ^ seed) % 7u;

    lb.begin_prim(rn, "");  // typeless: referenced/inherited type composes through
    PrimSpec *ps = lb.current();
    switch (shape) {
      case 0:  // external reference
        ps->meta().references.push_back(aref);
        break;
      case 1:  // inherit a top-level class
        ps->meta().inherits.push_back("</_class_Base>");
        break;
      case 2:  // reference + inherit (class wins on type)
        ps->meta().references.push_back(aref);
        ps->meta().inherits.push_back("</_class_Base>");
        break;
      case 3:  // internal reference into /Lib/Model (has child Inner)
        ps->meta().references.push_back("</Lib/Model>");
        break;
      case 4:  // variant (exotic) else reference
        if (exotic) {
          VariantSetData vss;
          vss.name = "v";
          VariantData hi;
          hi.name = "hi";
          hi.properties.push_back({"vprop", Value::MakeFloat3(3, 3, 3)});
          VariantData lo;
          lo.name = "lo";
          lo.properties.push_back({"vprop", Value::MakeFloat3(9, 9, 9)});
          vss.variants.push_back(std::move(hi));
          vss.variants.push_back(std::move(lo));
          ps->meta().variantSets().push_back(std::move(vss));
          ps->meta().variantSelection = "v=hi";
        } else {
          ps->meta().references.push_back(aref);
        }
        break;
      case 5:  // payload (exotic) else reference+specialize
        if (exotic) {
          ps->meta().payloads.push_back(aref);
        } else {
          ps->meta().references.push_back(aref);
        }
        break;
      default:  // 6: instanceable external reference (groups by asset)
        ps->meta().references.push_back(aref);
        ps->meta().instanceable = true;
        break;
    }
    lb.end_prim();

    sc.paths.push_back(Path("/World/" + rn));
    sc.path_strs.push_back("/World/" + rn);
    // Children of referenced subtrees (some compose, some are <null> -- both
    // serial and parallel agree, which is what we assert).
    sc.paths.push_back(Path("/World/" + rn + "/C0"));
    sc.path_strs.push_back("/World/" + rn + "/C0");
    sc.paths.push_back(Path("/World/" + rn + "/Inner"));
    sc.path_strs.push_back("/World/" + rn + "/Inner");
  }
  lb.end_prim();  // World

  // Internal-reference target with a child.
  lb.begin_prim("Lib", "Scope");
  lb.begin_prim("Model", "Mesh");
  lb.add_property("size", Value::MakeFloat3(1, 2, 3));
  lb.begin_prim("Inner", "Sphere");
  lb.end_prim();
  lb.end_prim();
  lb.end_prim();

  // Abstract class for inherits.
  lb.begin_prim("_class_Base", "Scope", PrimSpecifier::Class);
  lb.add_property("baseProp", Value::MakeFloat3(5, 5, 5));
  lb.end_prim();

  lb.finalize();
  sc.root = std::make_shared<Layer>(std::move(*root));

  for (const char *p : {"/World", "/Lib/Model", "/Lib/Model/Inner", "/_class_Base"}) {
    sc.paths.push_back(Path(p));
    sc.path_strs.push_back(p);
  }
  return sc;
}

// ---------------------------------------------------------------------------
// Build harness.
// ---------------------------------------------------------------------------

struct RunResult {
  std::vector<std::string> dumps;
  std::string instances;
  size_t reg_size = 0;
  size_t parse_count = 0;
  size_t index_count = 0;
  std::string deferred_payloads;
};

static RunResult RunBuild(const Scene &sc, int num_threads,
                          const pcp::LoadRules *rules = nullptr) {
  AssetResolver resolver;
  resolver.SetCustomResolver(
      [](const std::string &a, const std::string &) { return a; });
  pcp::CompositionOptions opts;
  opts.num_threads = num_threads;
  auto opened = pcp::Cache::Open(resolver, sc.root, "", opts);
  assert(opened && "Cache::Open failed");
  pcp::Cache cache = std::move(*opened);
  for (const auto &a : sc.assets) cache.PreloadLayer(a.first, a.second);
  if (rules) cache.SetLoadRules(*rules);

  std::string warn, err;
  assert(cache.PrewarmPrimIndices(sc.paths, &warn, &err));

  RunResult r;
  r.dumps.reserve(sc.paths.size());
  for (const Path &p : sc.paths) {
    r.dumps.push_back(CanonIndex(cache.ComputePrimIndex(p, &warn, &err)));
  }
  r.instances = CanonInstances(cache);
  r.reg_size = cache.layer_registry().size();
  r.parse_count = cache.layer_registry().parse_count();
  r.index_count = cache.ComputedPrimIndexCount();
  r.deferred_payloads = CanonDeferredPayloads(cache);
  return r;
}

static void Compare(const Scene &sc, const RunResult &a, const RunResult &b,
                    const char *label) {
  assert(a.dumps.size() == b.dumps.size());
  size_t mism = 0;
  for (size_t i = 0; i < a.dumps.size(); ++i) {
    if (a.dumps[i] != b.dumps[i]) {
      if (mism < 5) {
        std::cout << "  [" << label << "] MISMATCH at " << sc.path_strs[i]
                  << "\n     A: " << a.dumps[i] << "\n     B: " << b.dumps[i]
                  << std::endl;
      }
      ++mism;
    }
  }
  assert(mism == 0 && "parallel index differs from serial");
  assert(a.instances == b.instances && "instance groupings differ");
  assert(a.index_count == b.index_count && "computed-index count differs");
  assert(a.deferred_payloads == b.deferred_payloads &&
         "deferred payload state differs");
}

// ---------------------------------------------------------------------------
// Tests.
// ---------------------------------------------------------------------------

// Minimal: a handful of prims, two threads. Smallest scene that takes the
// parallel branch (needs > 1 path).
static void test_minimal_parallel() {
  std::cout << "test_minimal_parallel..." << std::endl;
  Scene sc = GenScene(/*n_root=*/3, /*n_assets=*/2, /*exotic=*/false, /*seed=*/1);
  RunResult serial = RunBuild(sc, 1);
  RunResult par = RunBuild(sc, 2);
  Compare(sc, serial, par, "minimal");
  // Every preloaded asset is shared (parse-once): registry holds exactly the
  // assets, none parsed from disk.
  assert(par.reg_size == sc.n_assets);
  assert(par.parse_count == 0);
  std::cout << "  OK" << std::endl;
}

// Complex: ~24 prims spanning every arc shape (reference, inherit, internal
// reference, variant, payload, instanceable), built with 4 threads.
static void test_complex_parallel() {
  std::cout << "test_complex_parallel..." << std::endl;
  Scene sc = GenScene(/*n_root=*/24, /*n_assets=*/5, /*exotic=*/true, /*seed=*/0xABCDu);
  RunResult serial = RunBuild(sc, 1);
  RunResult par = RunBuild(sc, 4);
  Compare(sc, serial, par, "complex");
  // The instanceable prims must have grouped into prototypes (otherwise the
  // ordered-merge instancing path wasn't exercised).
  assert(!par.instances.empty() && "expected instance groupings");
  assert(par.reg_size == sc.n_assets && par.parse_count == 0);
  std::cout << "  OK (" << sc.paths.size() << " paths)" << std::endl;
}

static void test_parallel_load_rules_match_serial() {
  std::cout << "test_parallel_load_rules_match_serial..." << std::endl;
  Scene sc;
  sc.n_assets = 3;
  for (int i = 0; i < 3; ++i) {
    sc.assets.push_back({"asset_" + std::to_string(i),
                         MakeAsset("Mesh", 2, 1)});
  }

  auto root = std::make_shared<Layer>();
  LayerBuilder lb(*root);
  lb.begin_prim("World", "Xform");
  for (int i = 0; i < 32; ++i) {
    const std::string nm = "P" + std::to_string(i);
    lb.begin_prim(nm, "");
    lb.current()->meta().payloads.push_back(
        "@asset_" + std::to_string(i % 3) + "@</A>");
    lb.end_prim();
    sc.paths.push_back(Path("/World/" + nm));
    sc.path_strs.push_back("/World/" + nm);
    sc.paths.push_back(Path("/World/" + nm + "/C0"));
    sc.path_strs.push_back("/World/" + nm + "/C0");
  }
  lb.end_prim();
  lb.finalize();
  sc.root = root;
  sc.paths.push_back(Path("/World"));
  sc.path_strs.push_back("/World");

  pcp::LoadRules rules;
  rules.Unload("/");
  rules.LoadWithDescendants("/World/P0");

  RunResult serial = RunBuild(sc, 1, &rules);
  RunResult par = RunBuild(sc, 8, &rules);
  Compare(sc, serial, par, "load-rules");
  assert(serial.deferred_payloads.find("/World/P1;") != std::string::npos);
  assert(serial.deferred_payloads.find("/World/P0;") == std::string::npos);
  std::cout << "  OK" << std::endl;
}

// Stress: a large scene (hundreds of prims, ~1k paths) referencing a small pool
// of shared assets, half of them instanceable (large prototype groups). Built
// with many threads, repeated several times -- repeated parallel runs must be
// bit-identical to each other AND to the serial baseline. The repetition is a
// strong race smoke-test even without ThreadSanitizer.
static void test_stress_parallel() {
  std::cout << "test_stress_parallel..." << std::endl;
  const int kThreads = 8;
  const int kRounds = 4;
  Scene sc = GenScene(/*n_root=*/300, /*n_assets=*/12, /*exotic=*/true, /*seed=*/7u);

  RunResult serial = RunBuild(sc, 1);

  RunResult first_par;
  for (int round = 0; round < kRounds; ++round) {
    RunResult par = RunBuild(sc, kThreads);
    Compare(sc, serial, par, "stress");
    if (round == 0) {
      first_par = par;
    } else {
      // Determinism across parallel runs (catches order-dependent races).
      assert(par.dumps == first_par.dumps && "parallel runs disagree");
      assert(par.instances == first_par.instances);
    }
  }
  assert(serial.index_count > 0);
  assert(!serial.instances.empty() && "expected prototype groups in stress scene");
  assert(serial.reg_size == sc.n_assets && serial.parse_count == 0 &&
         "shared registry should hold exactly the pooled assets, parsed once");
  std::cout << "  OK (" << sc.paths.size() << " paths, " << kThreads
            << " threads x " << kRounds << " rounds)" << std::endl;
}

static void test_parallel_same_asset_parse_once() {
  std::cout << "test_parallel_same_asset_parse_once..." << std::endl;
  const std::string asset_path = "/tmp/next_pcp_parallel_shared.usda";
  {
    std::ofstream f(asset_path);
    f << "#usda 1.0\n"
         "def Mesh \"A\"\n"
         "{\n"
         "    custom int sharedVal = 11\n"
         "}\n";
  }

  const int kNumPrims = 64;
  auto root = std::make_shared<Layer>();
  {
    LayerBuilder lb(*root);
    lb.begin_prim("World", "Xform");
    for (int i = 0; i < kNumPrims; ++i) {
      lb.begin_prim("R" + std::to_string(i), "");
      lb.current()->meta().references.push_back("@next_pcp_parallel_shared.usda@</A>");
      lb.end_prim();
    }
    lb.end_prim();
    lb.finalize();
  }

  AssetResolver resolver;
  resolver.SetWorkingDirectory("/tmp");
  pcp::CompositionOptions opts;
  opts.num_threads = 8;
  auto opened = pcp::Cache::Open(resolver, root, "", opts);
  assert(opened);
  pcp::Cache cache = std::move(*opened);

  std::vector<Path> paths;
  for (int i = 0; i < kNumPrims; ++i) {
    paths.push_back(Path("/World/R" + std::to_string(i)));
  }

  std::string warn, err;
  assert(cache.PrewarmPrimIndices(paths, &warn, &err));
  assert(cache.layer_registry().parse_count() == 1 &&
         "same referenced asset should be parsed exactly once");
  assert(cache.layer_registry().size() == 1);
  for (const Path &p : paths) {
    const pcp::PrimIndex *idx = cache.ComputePrimIndex(p, &warn, &err);
    assert(idx && idx->GetNodeCount() >= 2);
  }

  std::remove(asset_path.c_str());
  std::cout << "  OK" << std::endl;
}

static void test_layer_registry_inflight_failure_diagnostics() {
  std::cout << "test_layer_registry_inflight_failure_diagnostics..." << std::endl;
#if defined(TINYUSDZ_ENABLE_THREAD)
  const std::string asset_path = "/tmp/next_registry_bad.usda";
  {
    std::ofstream f(asset_path);
    f << "#usda 1.0\n";
    for (int i = 0; i < 2000; ++i) {
      f << "def Xform \"P" << i << "\"\n{\n";
    }
    f << "custom float broken = \n";
  }

  AssetResolver resolver;
  resolver.SetCustomResolver(
      [](const std::string &a, const std::string &) { return a; });
  pcp::LayerRegistry reg;

  const int kThreads = 16;
  std::atomic<int> ready{0};
  std::atomic<bool> go{false};
  std::vector<std::string> warn(static_cast<size_t>(kThreads));
  std::vector<std::string> err(static_cast<size_t>(kThreads));
  std::vector<std::thread> threads;
  threads.reserve(static_cast<size_t>(kThreads));
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&, i]() {
      ready.fetch_add(1);
      while (!go.load()) std::this_thread::yield();
      std::shared_ptr<Layer> layer =
          reg.GetOrLoad(resolver, asset_path, "", &warn[static_cast<size_t>(i)],
                        &err[static_cast<size_t>(i)]);
      assert(!layer);
    });
  }
  while (ready.load() != kThreads) std::this_thread::yield();
  go.store(true);
  for (auto &t : threads) t.join();

  for (int i = 0; i < kThreads; ++i) {
    assert(!err[static_cast<size_t>(i)].empty() &&
           "same-path waiters must receive parse diagnostics");
  }
  std::remove(asset_path.c_str());
#else
  std::cout << "  (skipped: build with -DTINYUSDZ_NEXT_ENABLE_THREAD=ON)\n";
#endif
  std::cout << "  OK" << std::endl;
}

int main() {
  test_minimal_parallel();
  test_complex_parallel();
  test_parallel_load_rules_match_serial();
  test_stress_parallel();
  test_parallel_same_asset_parse_once();
  test_layer_registry_inflight_failure_diagnostics();
  std::cout << "All next/pcp parallel tests passed." << std::endl;
  return 0;
}
