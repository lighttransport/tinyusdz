// SPDX-License-Identifier: Apache 2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// unit-pcp-cache.cc - Tests for the cached / lazy composition engine
//                     (tinyusdz::pcp::Cache).
//

#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-pcp-cache.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "unit-common.hh"

#include "asset-resolution.hh"
#include "composition-graph.hh"
#include "layer.hh"
#include "pcp/cache.hh"
#include "stage.hh"

using namespace tinyusdz;

namespace {

// ---------------------------------------------------------------------------
// In-memory asset resolver handler (hermetic; no filesystem dependency).
// Serves a single referenced layer's content for any requested asset name.
// ---------------------------------------------------------------------------

struct MemAsset {
  std::string content;
};

int mem_resolve(const char *asset_name, const std::vector<std::string> & /*sp*/,
                std::string *resolved, std::string * /*err*/, void * /*ud*/) {
  *resolved = std::string(asset_name);
  return 0;
}
int mem_size(const char * /*resolved*/, uint64_t *nbytes, std::string * /*err*/,
             void *ud) {
  *nbytes = static_cast<uint64_t>(static_cast<MemAsset *>(ud)->content.size());
  return 0;
}
int mem_read(const char * /*resolved*/, uint64_t req, uint8_t *out,
             uint64_t *nbytes, std::string * /*err*/, void *ud) {
  auto *m = static_cast<MemAsset *>(ud);
  uint64_t n = (std::min)(req, static_cast<uint64_t>(m->content.size()));
  std::memcpy(out, m->content.data(), n);
  *nbytes = n;
  return 0;
}

void install_mem_handler(AssetResolutionResolver *resolver, MemAsset *mem) {
  AssetResolutionHandler h;
  h.resolve_fun = mem_resolve;
  h.size_fun = mem_size;
  h.read_fun = mem_read;
  h.userdata = mem;
  resolver->register_wildcard_asset_resolution_handler(h);
}

bool open_cache(const char *usda, AssetResolutionResolver &resolver,
                pcp::Cache &out, const pcp::CacheOptions &opts,
                std::string *err) {
  Layer layer;
  std::string warn;
  if (!tinyusdz_test::parse_usda_to_layer(usda, &layer, &warn, err)) {
    return false;
  }
  auto result = pcp::Cache::Open(resolver, layer, opts);
  if (!result) {
    *err = result.error();
    return false;
  }
  out = std::move(*result);
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// pcp_lazy_compute_caches_pointer_test
// ComputePrimIndex is lazy (only requested prims are cached) and returns a
// stable cached pointer on repeated calls.
// ---------------------------------------------------------------------------

void pcp_lazy_compute_caches_pointer_test(void) {
  const char *usda = R"(#usda 1.0
def Xform "A"
{
    custom int a = 1
    def Scope "Child" { custom int c = 2 }
}
def Xform "B"
{
    custom int b = 3
}
)";

  AssetResolutionResolver resolver;
  pcp::Cache cache;
  std::string err;
  TEST_CHECK(open_cache(usda, resolver, cache, {}, &err));
  if (!err.empty()) { TEST_MSG("open err: %s", err.c_str()); return; }

  // Nothing computed yet (fully lazy).
  TEST_CHECK(cache.ComputedPrimIndexCount() == 0);

  std::string w, e;
  const auto *a1 = cache.ComputePrimIndex(Path("/A", ""), &w, &e);
  TEST_CHECK(a1 != nullptr);
  TEST_CHECK(cache.ComputedPrimIndexCount() == 1);  // only /A

  // Second request returns the SAME cached pointer.
  const auto *a2 = cache.ComputePrimIndex(Path("/A", ""), &w, &e);
  TEST_CHECK(a1 == a2);
  TEST_CHECK(cache.ComputedPrimIndexCount() == 1);

  // A different prim yields a different index and grows the cache.
  const auto *b1 = cache.ComputePrimIndex(Path("/B", ""), &w, &e);
  TEST_CHECK(b1 != nullptr);
  TEST_CHECK(b1 != a1);
  TEST_CHECK(cache.ComputedPrimIndexCount() == 2);
  TEST_CHECK(cache.HasComputedPrimIndex(Path("/A", "")));
  TEST_CHECK(cache.HasComputedPrimIndex(Path("/B", "")));

  // Missing prim returns nullptr and does not add a cache entry.
  const auto *missing = cache.ComputePrimIndex(Path("/Nope", ""), &w, &e);
  TEST_CHECK(missing == nullptr);
  TEST_CHECK(cache.ComputedPrimIndexCount() == 2);
}

// ---------------------------------------------------------------------------
// pcp_layer_parsed_once_test
// Two prims referencing the same external asset cause exactly one parse
// (LayerRegistry shares the parsed layer).
// ---------------------------------------------------------------------------

void pcp_layer_parsed_once_test(void) {
  const char *root_usda = R"(#usda 1.0
def Xform "A" (
    prepend references = @ref.usda@</Ref>
)
{
    custom int a = 1
}
def Xform "B" (
    prepend references = @ref.usda@</Ref>
)
{
    custom int b = 2
}
)";

  MemAsset mem;
  mem.content =
      "#usda 1.0\n"
      "def Xform \"Ref\"\n"
      "{\n"
      "    custom int refVal = 7\n"
      "}\n";

  AssetResolutionResolver resolver;
  install_mem_handler(&resolver, &mem);

  pcp::Cache cache;
  std::string err;
  TEST_CHECK(open_cache(root_usda, resolver, cache, {}, &err));
  if (!err.empty()) { TEST_MSG("open err: %s", err.c_str()); return; }

  std::string w, e;
  const auto *a = cache.ComputePrimIndex(Path("/A", ""), &w, &e);
  const auto *b = cache.ComputePrimIndex(Path("/B", ""), &w, &e);
  TEST_CHECK(a != nullptr);
  TEST_CHECK(b != nullptr);

  // Both prims pulled the same external asset -> parsed exactly once.
  TEST_CHECK_(cache.layer_registry().parse_count() == 1,
              "expected 1 parse, got %zu", cache.layer_registry().parse_count());
  TEST_CHECK(cache.layer_registry().size() == 1);

  // Each prim should carry a Reference arc node.
  bool a_has_ref = false, b_has_ref = false;
  for (uint16_t i = 0; i < a->GetNodeCount(); i++) {
    if (a->GetNode(i).arc_type == composition_graph::ArcType::Reference)
      a_has_ref = true;
  }
  for (uint16_t i = 0; i < b->GetNodeCount(); i++) {
    if (b->GetNode(i).arc_type == composition_graph::ArcType::Reference)
      b_has_ref = true;
  }
  TEST_CHECK(a_has_ref);
  TEST_CHECK(b_has_ref);
}

// ---------------------------------------------------------------------------
// pcp_invalidate_drops_dependents_test
// Invalidate(/Source) drops /Source AND /Target (which references it), then a
// fresh ComputePrimIndex rebuilds with a new pointer.
// ---------------------------------------------------------------------------

void pcp_invalidate_drops_dependents_test(void) {
  const char *usda = R"(#usda 1.0
def Xform "Source"
{
    custom int sourceAttr = 100
}
def Xform "Target" (
    prepend references = </Source>
)
{
    custom int targetAttr = 200
}
)";

  AssetResolutionResolver resolver;
  pcp::Cache cache;
  std::string err;
  TEST_CHECK(open_cache(usda, resolver, cache, {}, &err));
  if (!err.empty()) { TEST_MSG("open err: %s", err.c_str()); return; }

  std::string w, e;
  const auto *src = cache.ComputePrimIndex(Path("/Source", ""), &w, &e);
  const auto *tgt0 = cache.ComputePrimIndex(Path("/Target", ""), &w, &e);
  TEST_CHECK(src != nullptr);
  TEST_CHECK(tgt0 != nullptr);
  TEST_CHECK(cache.ComputedPrimIndexCount() == 2);

  // /Target reads an opinion at /Source (internal reference), so invalidating
  // /Source must drop BOTH cached indices.
  cache.Invalidate(Path("/Source", ""));
  TEST_CHECK(!cache.HasComputedPrimIndex(Path("/Source", "")));
  TEST_CHECK_(!cache.HasComputedPrimIndex(Path("/Target", "")),
              "/Target depends on /Source and should have been dropped");
  TEST_CHECK(cache.ComputedPrimIndexCount() == 0);

  // Recompute works after invalidation.
  const auto *tgt1 = cache.ComputePrimIndex(Path("/Target", ""), &w, &e);
  TEST_CHECK(tgt1 != nullptr);
  TEST_CHECK(cache.HasComputedPrimIndex(Path("/Target", "")));
}

// ---------------------------------------------------------------------------
// pcp_payload_load_unload_test
// A deferred payload toggles deferred<->loaded via LoadPayload/UnloadPayload.
// ---------------------------------------------------------------------------

void pcp_payload_load_unload_test(void) {
  // Internal payload (no external file needed).
  const char *usda = R"(#usda 1.0
def Xform "PayloadSource"
{
    custom int payloadAttr = 42
}
def Xform "Consumer" (
    payload = </PayloadSource>
)
{
    custom int localAttr = 10
}
)";

  AssetResolutionResolver resolver;
  pcp::CacheOptions opts;
  // Defer ALL payloads at initial composition.
  opts.composition.payload_policy = [](const Path &, const Payload &) {
    return false;
  };

  pcp::Cache cache;
  std::string err;
  TEST_CHECK(open_cache(usda, resolver, cache, opts, &err));
  if (!err.empty()) { TEST_MSG("open err: %s", err.c_str()); return; }

  std::string w, e;
  const auto *idx = cache.ComputePrimIndex(Path("/Consumer", ""), &w, &e);
  TEST_CHECK(idx != nullptr);
  if (!idx) return;

  // Payload should be deferred initially.
  TEST_CHECK(cache.HasDeferredPayload(Path("/Consumer", "")));
  auto deferred = cache.GetDeferredPayloadPaths();
  TEST_CHECK(!deferred.empty());

  // Load it -> a payload-loaded node appears, no longer deferred.
  auto loaded = cache.LoadPayload(Path("/Consumer", ""), &w, &e);
  TEST_CHECK_(loaded.has_value(), "LoadPayload failed: %s",
              loaded ? "" : loaded.error().c_str());
  TEST_CHECK(!cache.HasDeferredPayload(Path("/Consumer", "")));

  bool any_loaded = false;
  for (uint16_t i = 0; i < idx->GetNodeCount(); i++) {
    if (idx->GetNode(i).is_payload_loaded()) any_loaded = true;
  }
  TEST_CHECK_(any_loaded, "expected a payload-loaded node after LoadPayload");

  // Unload -> back to deferred.
  auto unloaded = cache.UnloadPayload(Path("/Consumer", ""));
  TEST_CHECK(unloaded.has_value());
  TEST_CHECK(cache.HasDeferredPayload(Path("/Consumer", "")));
}

// ---------------------------------------------------------------------------
// pcp_buildstage_matches_compgraph_test
// pcp::Cache::BuildStage produces the same prim set as
// CompositionGraph::Compose().BuildStage() on the same input.
// ---------------------------------------------------------------------------

void pcp_buildstage_matches_compgraph_test(void) {
  const char *usda = R"(#usda 1.0
def Xform "Root"
{
    custom int myAttr = 42
    def Scope "Child"
    {
        custom float childVal = 1.5
        def Scope "Grand" { custom int g = 9 }
    }
}
def Xform "Other"
{
    custom int o = 7
}
)";

  std::string warn, err;

  // pcp::Cache path.
  AssetResolutionResolver resolver_a;
  pcp::Cache cache;
  TEST_CHECK(open_cache(usda, resolver_a, cache, {}, &err));
  if (!err.empty()) { TEST_MSG("open err: %s", err.c_str()); return; }
  Stage pcp_stage;
  TEST_CHECK(cache.BuildStage(&pcp_stage, &warn, &err));
  if (!err.empty()) { TEST_MSG("pcp BuildStage err: %s", err.c_str()); return; }

  // CompositionGraph path.
  Layer layer;
  TEST_CHECK(tinyusdz_test::parse_usda_to_layer(usda, &layer, &warn, &err));
  AssetResolutionResolver resolver_b;
  auto cg = composition_graph::CompositionGraph::Compose(
      resolver_b, layer, composition_graph::CompositionGraphOptions());
  TEST_CHECK(cg.has_value());
  if (!cg) { TEST_MSG("cg err: %s", cg.error().c_str()); return; }
  Stage cg_stage;
  TEST_CHECK(cg->BuildStage(&cg_stage, &warn, &err));

  // Same number of root prims.
  TEST_CHECK_(pcp_stage.root_prims().size() == cg_stage.root_prims().size(),
              "pcp roots=%zu cg roots=%zu", pcp_stage.root_prims().size(),
              cg_stage.root_prims().size());

  // Both should resolve the same representative paths.
  const char *paths[] = {"/Root", "/Root/Child", "/Root/Child/Grand", "/Other"};
  for (const char *p : paths) {
    auto a = pcp_stage.GetPrimAtPath(Path(p, ""));
    auto b = cg_stage.GetPrimAtPath(Path(p, ""));
    TEST_CHECK_(a.has_value() == b.has_value(),
                "path %s: pcp=%d cg=%d", p, int(a.has_value()),
                int(b.has_value()));
  }
}

// ---------------------------------------------------------------------------
// pcp_singlethread_vs_multithread_identical_test
// Building the same scene single-threaded and multi-threaded yields identical
// per-prim composition graphs. (When threads are disabled, num_threads=-1 just
// runs single-threaded, so this still validates the batch path.)
// ---------------------------------------------------------------------------

void pcp_singlethread_vs_multithread_identical_test(void) {
  // >= min_paths_for_parallel (8) prims so the parallel branch engages.
  std::string usda = "#usda 1.0\n";
  const int kNumPrims = 12;
  for (int i = 0; i < kNumPrims; i++) {
    usda += "def Xform \"P" + std::to_string(i) + "\"\n{\n    custom int v = " +
            std::to_string(i) + "\n    def Scope \"C\" { custom int cc = 1 }\n}\n";
  }

  std::string err;

  AssetResolutionResolver resolver_st;
  pcp::CacheOptions st_opts;
  st_opts.num_threads = 1;
  pcp::Cache st_cache;
  TEST_CHECK(open_cache(usda.c_str(), resolver_st, st_cache, st_opts, &err));
  if (!err.empty()) { TEST_MSG("st open err: %s", err.c_str()); return; }
  Stage st_stage;
  std::string warn;
  TEST_CHECK(st_cache.BuildStage(&st_stage, &warn, &err));

  AssetResolutionResolver resolver_mt;
  pcp::CacheOptions mt_opts;
  mt_opts.num_threads = -1;  // hardware_concurrency (or 1 if threads disabled)
  pcp::Cache mt_cache;
  TEST_CHECK(open_cache(usda.c_str(), resolver_mt, mt_cache, mt_opts, &err));
  if (!err.empty()) { TEST_MSG("mt open err: %s", err.c_str()); return; }
  Stage mt_stage;
  TEST_CHECK(mt_cache.BuildStage(&mt_stage, &warn, &err));

  // Same number of computed indices and root prims.
  TEST_CHECK_(st_cache.ComputedPrimIndexCount() ==
                  mt_cache.ComputedPrimIndexCount(),
              "st=%zu mt=%zu", st_cache.ComputedPrimIndexCount(),
              mt_cache.ComputedPrimIndexCount());
  TEST_CHECK(st_stage.root_prims().size() == mt_stage.root_prims().size());

  // Per-prim node counts and strength-order sizes must match exactly.
  std::string w, e;
  for (int i = 0; i < kNumPrims; i++) {
    std::string path = "/P" + std::to_string(i);
    const auto *st = st_cache.ComputePrimIndex(Path(path, ""), &w, &e);
    const auto *mt = mt_cache.ComputePrimIndex(Path(path, ""), &w, &e);
    TEST_CHECK(st != nullptr && mt != nullptr);
    if (!st || !mt) continue;
    TEST_CHECK_(st->GetNodeCount() == mt->GetNodeCount(),
                "%s node counts differ: st=%u mt=%u", path.c_str(),
                st->GetNodeCount(), mt->GetNodeCount());
    TEST_CHECK(st->GetStrengthOrder().size() == mt->GetStrengthOrder().size());
  }
}

// ---------------------------------------------------------------------------
// pcp_mt_shared_reference_test
// Many prims referencing the SAME external asset, built multi-threaded. This
// exercises the concurrency paths the basic MT test does not: contended
// LayerRegistry loads and concurrent find_primspec_at() on a registry-shared
// referenced Layer. Run under ThreadSanitizer to confirm no data race.
// ---------------------------------------------------------------------------

void pcp_mt_shared_reference_test(void) {
  const int kNumPrims = 16;  // >= min_paths_for_parallel so the MT path engages
  std::string root_usda = "#usda 1.0\n";
  for (int i = 0; i < kNumPrims; i++) {
    root_usda += "def Xform \"P" + std::to_string(i) +
                 "\" (\n    prepend references = @ref.usda@</Ref>\n)\n{\n"
                 "    custom int local = " + std::to_string(i) + "\n}\n";
  }

  MemAsset mem;
  mem.content =
      "#usda 1.0\n"
      "def Xform \"Ref\"\n"
      "{\n"
      "    custom int refVal = 7\n"
      "}\n";

  AssetResolutionResolver resolver;
  install_mem_handler(&resolver, &mem);

  pcp::CacheOptions opts;
  opts.num_threads = -1;  // hardware_concurrency (or 1 if threads disabled)

  pcp::Cache cache;
  std::string err;
  TEST_CHECK(open_cache(root_usda.c_str(), resolver, cache, opts, &err));
  if (!err.empty()) { TEST_MSG("open err: %s", err.c_str()); return; }

  // Build all prims (parallel under threads); BuildStage prewarms everything.
  Stage stage;
  std::string warn;
  TEST_CHECK(cache.BuildStage(&stage, &warn, &err));

  // The shared external asset must be parsed exactly once even under MT.
  TEST_CHECK_(cache.layer_registry().parse_count() == 1,
              "expected 1 parse, got %zu", cache.layer_registry().parse_count());
  TEST_CHECK(cache.ComputedPrimIndexCount() == static_cast<size_t>(kNumPrims));

  // Every prim should have resolved its Reference arc consistently.
  std::string w, e;
  for (int i = 0; i < kNumPrims; i++) {
    const auto *idx =
        cache.ComputePrimIndex(Path("/P" + std::to_string(i), ""), &w, &e);
    TEST_CHECK(idx != nullptr);
    if (!idx) continue;
    bool has_ref = false;
    for (uint16_t n = 0; n < idx->GetNodeCount(); n++) {
      if (idx->GetNode(n).arc_type == composition_graph::ArcType::Reference)
        has_ref = true;
    }
    TEST_CHECK(has_ref);
  }
}

// ---------------------------------------------------------------------------
// pcp_external_payload_load_unload_test
// Deferred<->loaded toggle for an EXTERNAL payload. Unlike the internal-payload
// test, this drives LoadPayload's external branch: GetOrLoad (parse-on-demand
// through the registry), ValidateAndNormalizeAssetPath, and AddLayerStack.
// ---------------------------------------------------------------------------

void pcp_external_payload_load_unload_test(void) {
  const char *root_usda = R"(#usda 1.0
def Xform "Consumer" (
    payload = @payload.usda@</PPrim>
)
{
    custom int localAttr = 10
}
)";

  MemAsset mem;
  mem.content =
      "#usda 1.0\n"
      "def Xform \"PPrim\"\n"
      "{\n"
      "    custom int payloadAttr = 77\n"
      "}\n";

  AssetResolutionResolver resolver;
  install_mem_handler(&resolver, &mem);

  pcp::CacheOptions opts;
  opts.composition.payload_policy = [](const Path &, const Payload &) {
    return false;  // defer all payloads at compose
  };

  pcp::Cache cache;
  std::string err;
  TEST_CHECK(open_cache(root_usda, resolver, cache, opts, &err));
  if (!err.empty()) { TEST_MSG("open err: %s", err.c_str()); return; }

  std::string w, e;
  const auto *idx = cache.ComputePrimIndex(Path("/Consumer", ""), &w, &e);
  TEST_CHECK(idx != nullptr);
  if (!idx) return;

  // Deferred: the external file must NOT be parsed yet.
  TEST_CHECK(cache.HasDeferredPayload(Path("/Consumer", "")));
  TEST_CHECK_(cache.layer_registry().parse_count() == 0,
              "external payload must not parse while deferred, got %zu",
              cache.layer_registry().parse_count());

  // Load -> registry parses payload.usda on demand; a payload-loaded node appears.
  auto loaded = cache.LoadPayload(Path("/Consumer", ""), &w, &e);
  TEST_CHECK_(loaded.has_value(), "LoadPayload failed: %s",
              loaded ? "" : loaded.error().c_str());
  TEST_CHECK(!cache.HasDeferredPayload(Path("/Consumer", "")));
  TEST_CHECK_(cache.layer_registry().parse_count() == 1,
              "expected external payload parsed once, got %zu",
              cache.layer_registry().parse_count());

  bool any_loaded = false;
  for (uint16_t i = 0; i < idx->GetNodeCount(); i++) {
    if (idx->GetNode(i).is_payload_loaded()) any_loaded = true;
  }
  TEST_CHECK_(any_loaded, "expected a payload-loaded node after LoadPayload");

  // Unload -> back to deferred (the parsed layer stays cached in the registry).
  auto unloaded = cache.UnloadPayload(Path("/Consumer", ""));
  TEST_CHECK(unloaded.has_value());
  TEST_CHECK(cache.HasDeferredPayload(Path("/Consumer", "")));
  TEST_CHECK(cache.layer_registry().parse_count() == 1);
}

// ---------------------------------------------------------------------------
// pcp_buildstage_reference_grandchildren_test
// A reference targets a prim that has child/grandchild prims with NO local
// opinion in the root layer. The composition_graph engine (both the eager
// CompositionGraph and pcp::Cache) composes the reference's opinions onto the
// referencing prim but does not currently expand the referenced child subtree
// into separate prim paths. BuildStage::GatherAllPrimPaths walks only LOCAL
// primspec paths, so this guards that pcp's BuildStage stays in PARITY with the
// eager CompositionGraph (same has/!has decision for every candidate path) and
// that the reference arc is actually processed -- catching any future
// divergence where pcp drops or gains a prim relative to the eager engine.
// ---------------------------------------------------------------------------

void pcp_buildstage_reference_grandchildren_test(void) {
  const char *root_usda = R"(#usda 1.0
def Xform "Inst" (
    prepend references = @ref.usda@</Base>
)
{
    custom int localTop = 1
}
)";

  MemAsset mem;
  mem.content =
      "#usda 1.0\n"
      "def Xform \"Base\"\n"
      "{\n"
      "    custom int baseAttr = 5\n"
      "    def Scope \"Sub\"\n"
      "    {\n"
      "        custom int subAttr = 6\n"
      "        def Scope \"Leaf\" { custom int leafAttr = 7 }\n"
      "    }\n"
      "}\n";

  std::string warn, err;

  // pcp::Cache path.
  AssetResolutionResolver resolver_a;
  install_mem_handler(&resolver_a, &mem);
  pcp::Cache cache;
  TEST_CHECK(open_cache(root_usda, resolver_a, cache, {}, &err));
  if (!err.empty()) { TEST_MSG("open err: %s", err.c_str()); return; }
  Stage pcp_stage;
  TEST_CHECK(cache.BuildStage(&pcp_stage, &warn, &err));
  if (!err.empty()) { TEST_MSG("pcp BuildStage err: %s", err.c_str()); return; }

  // eager CompositionGraph oracle.
  Layer layer;
  TEST_CHECK(tinyusdz_test::parse_usda_to_layer(root_usda, &layer, &warn, &err));
  AssetResolutionResolver resolver_b;
  install_mem_handler(&resolver_b, &mem);
  auto cg = composition_graph::CompositionGraph::Compose(
      resolver_b, layer, composition_graph::CompositionGraphOptions());
  TEST_CHECK(cg.has_value());
  if (!cg) { TEST_MSG("cg err: %s", cg.error().c_str()); return; }
  Stage cg_stage;
  TEST_CHECK(cg->BuildStage(&cg_stage, &warn, &err));

  // pcp and the eager graph must agree on every candidate path (present or
  // absent) -- a divergence means pcp's BuildStage dropped or gained a prim.
  const char *paths[] = {"/Inst", "/Inst/Sub", "/Inst/Sub/Leaf"};
  for (const char *p : paths) {
    auto a = pcp_stage.GetPrimAtPath(Path(p, ""));
    auto b = cg_stage.GetPrimAtPath(Path(p, ""));
    TEST_CHECK_(a.has_value() == b.has_value(),
                "path %s parity broken: pcp=%d cg=%d", p, int(a.has_value()),
                int(b.has_value()));
  }
  // Non-vacuous: the referencing prim itself is present in both.
  TEST_CHECK(pcp_stage.GetPrimAtPath(Path("/Inst", "")).has_value());
  TEST_CHECK(cg_stage.GetPrimAtPath(Path("/Inst", "")).has_value());
  TEST_CHECK_(pcp_stage.root_prims().size() == cg_stage.root_prims().size(),
              "pcp roots=%zu cg roots=%zu", pcp_stage.root_prims().size(),
              cg_stage.root_prims().size());

  // The reference must actually be processed by pcp (an arc node exists), so the
  // parity above is not vacuously comparing two unreferenced prims.
  std::string w, e;
  const auto *inst = cache.ComputePrimIndex(Path("/Inst", ""), &w, &e);
  TEST_CHECK(inst != nullptr);
  bool has_ref = false;
  if (inst) {
    for (uint16_t i = 0; i < inst->GetNodeCount(); i++) {
      if (inst->GetNode(i).arc_type == composition_graph::ArcType::Reference)
        has_ref = true;
    }
  }
  TEST_CHECK_(has_ref, "expected a Reference arc node on /Inst");
}
