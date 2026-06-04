// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Test for the native PCP-style lazy composition cache (next/pcp).
// Phase 1: internal references + lazy ComputePrimIndex + BuildStage.

#include <cassert>
#include <iostream>
#include <memory>

#include "next/layer/layer.hh"
#include "next/pcp/cache.hh"
#include "next/resolver/asset-resolver.hh"
#include "next/stage/stage.hh"

using namespace tinyusdz::next;

// Build an in-memory root layer:
//   /World (Xform)
//     /A (Xform)  -> references </Lib/Model>
//   /Lib (Scope)
//     /Model (Mesh, size=float3) { /Inner (Sphere) }
static std::shared_ptr<Layer> BuildRootLayer() {
  Layer layer;
  LayerBuilder lb(layer);

  lb.begin_prim("World", "Xform");
  // Typeless so the referenced prim's type ("Mesh") composes through (a local
  // typeName would correctly win over the reference).
  lb.begin_prim("A", "");
  // Internal reference to /Lib/Model in the same layer.
  lb.current()->meta().references.push_back("</Lib/Model>");
  lb.end_prim();  // A
  // P carries a PAYLOAD (deferrable) to the same Model.
  lb.begin_prim("P", "");
  lb.current()->meta().payloads.push_back("</Lib/Model>");
  lb.end_prim();  // P
  lb.end_prim();  // World

  lb.begin_prim("Lib", "Scope");
  lb.begin_prim("Model", "Mesh");
  lb.add_property("size", Value::MakeFloat3(1.0f, 2.0f, 3.0f));
  lb.begin_prim("Inner", "Sphere");
  lb.end_prim();  // Inner
  lb.end_prim();  // Model
  lb.end_prim();  // Lib

  lb.finalize();
  return std::make_shared<Layer>(std::move(layer));
}

static void test_compute_prim_index() {
  std::cout << "test_compute_prim_index..." << std::endl;
  AssetResolver resolver;
  auto root = BuildRootLayer();

  auto opened = pcp::Cache::Open(resolver, root);
  assert(opened && "Cache::Open failed");
  pcp::Cache cache = std::move(*opened);

  std::string warn, err;
  const pcp::PrimIndex *idx = cache.ComputePrimIndex(Path("/World/A"), &warn, &err);
  assert(idx != nullptr && "no PrimIndex for /World/A");
  // Root node + one reference node (to /Lib/Model).
  assert(idx->GetNodeCount() >= 2);
  assert(idx->HasAnySpecs());
  std::cout << idx->DumpToString();

  // Caching: second query returns the same pointer.
  const pcp::PrimIndex *idx2 = cache.ComputePrimIndex(Path("/World/A"), &warn, &err);
  assert(idx == idx2);
  assert(cache.HasComputedPrimIndex(Path("/World/A")));

  // A prim with no local spec returns nullptr (phase-1 behavior).
  assert(cache.ComputePrimIndex(Path("/Nope"), &warn, &err) == nullptr);

  std::cout << "  OK" << std::endl;
}

static void test_build_stage() {
  std::cout << "test_build_stage..." << std::endl;
  AssetResolver resolver;
  auto root = BuildRootLayer();
  auto opened = pcp::Cache::Open(resolver, root);
  assert(opened);
  pcp::Cache cache = std::move(*opened);

  Stage stage;
  std::string warn, err;
  bool ok = cache.BuildStage(&stage, &warn, &err);
  assert(ok && "BuildStage failed");

  // /World/A composed the reference: it should now be typed "Mesh", carry the
  // referenced "size" property, and have the referenced child "Inner".
  UsdPrim a = stage.GetPrimAtPath("/World/A");
  assert(a.IsValid() && "/World/A missing");
  assert(a.GetTypeName() == "Mesh" && "reference did not compose type");
  assert(a.GetPropertyValue("size") != nullptr && "referenced property missing");

  UsdPrim inner = stage.GetPrimAtPath("/World/A/Inner");
  assert(inner.IsValid() && "referenced child /World/A/Inner missing");
  assert(inner.GetTypeName() == "Sphere");

  // The source prim is still present too.
  UsdPrim model = stage.GetPrimAtPath("/Lib/Model");
  assert(model.IsValid() && "/Lib/Model missing");

  std::cout << "  OK" << std::endl;
}

static void test_invalidate() {
  std::cout << "test_invalidate..." << std::endl;
  AssetResolver resolver;
  auto root = BuildRootLayer();
  auto opened = pcp::Cache::Open(resolver, root);
  assert(opened);
  pcp::Cache cache = std::move(*opened);

  std::string warn, err;
  cache.ComputePrimIndex(Path("/World/A"), &warn, &err);
  assert(cache.HasComputedPrimIndex(Path("/World/A")));
  assert(cache.ComputedPrimIndexCount() == 1);

  cache.Invalidate(Path("/World/A"));
  assert(!cache.HasComputedPrimIndex(Path("/World/A")));
  assert(cache.ComputedPrimIndexCount() == 0);

  // Recompute works after invalidation.
  assert(cache.ComputePrimIndex(Path("/World/A"), &warn, &err) != nullptr);
  std::cout << "  OK" << std::endl;
}

// Phase 2: ancestral composition - a descendant of a referenced prim can be
// computed lazily even though it has no local spec.
static void test_ancestral_compute() {
  std::cout << "test_ancestral_compute..." << std::endl;
  AssetResolver resolver;
  auto root = BuildRootLayer();
  auto opened = pcp::Cache::Open(resolver, root);
  assert(opened);
  pcp::Cache cache = std::move(*opened);

  std::string warn, err;
  // /World/A/Inner exists only because /World/A references /Lib/Model whose
  // child is Inner. No local spec at /World/A/Inner.
  const pcp::PrimIndex *inner =
      cache.ComputePrimIndex(Path("/World/A/Inner"), &warn, &err);
  assert(inner != nullptr && "ancestral composition failed for /World/A/Inner");
  assert(inner->HasAnySpecs());
  std::cout << "  OK" << std::endl;
}

// Phase 2: deferred payloads.
static void test_deferred_payload() {
  std::cout << "test_deferred_payload..." << std::endl;
  AssetResolver resolver;
  auto root = BuildRootLayer();

  pcp::CompositionOptions opts;
  opts.payload_policy = [](const Path &, const std::string &) { return false; };
  auto opened = pcp::Cache::Open(resolver, root, "", opts);
  assert(opened);
  pcp::Cache cache = std::move(*opened);

  Stage stage;
  std::string warn, err;
  assert(cache.BuildStage(&stage, &warn, &err));

  // Payload deferred: /World/P composed but WITHOUT the payload's content.
  assert(cache.HasDeferredPayload(Path("/World/P")) && "payload not deferred");
  UsdPrim p = stage.GetPrimAtPath("/World/P");
  assert(p.IsValid());
  assert(p.GetPropertyValue("size") == nullptr && "payload content leaked while deferred");
  assert(!stage.GetPrimAtPath("/World/P/Inner").IsValid());

  // Load the payload and recompose: content now present.
  assert(cache.LoadPayload(Path("/World/P"), &warn, &err));
  assert(!cache.HasDeferredPayload(Path("/World/P")));
  Stage stage2;
  assert(cache.BuildStage(&stage2, &warn, &err));
  UsdPrim p2 = stage2.GetPrimAtPath("/World/P");
  assert(p2.GetPropertyValue("size") != nullptr && "payload did not load");
  assert(stage2.GetPrimAtPath("/World/P/Inner").IsValid());
  std::cout << "  OK" << std::endl;
}

int main() {
  test_compute_prim_index();
  test_build_stage();
  test_invalidate();
  test_ancestral_compute();
  test_deferred_payload();
  std::cout << "All next/pcp tests passed." << std::endl;
  return 0;
}
