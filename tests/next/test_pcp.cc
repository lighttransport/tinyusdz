// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Test for the native PCP-style lazy composition cache (next/pcp).
// Phase 1: internal references + lazy ComputePrimIndex + BuildStage.

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

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
  // I inherits the class (provides type "Scope").
  lb.begin_prim("I", "");
  lb.current()->meta().inherits.push_back("</_class_Base>");
  lb.end_prim();  // I
  // IR inherits the class AND references RefModel (Mesh): inherit (Scope) is
  // STRONGER than the reference (Mesh), so composed type == "Scope".
  lb.begin_prim("IR", "");
  lb.current()->meta().inherits.push_back("</_class_Base>");
  lb.current()->meta().references.push_back("</Lib/RefModel>");
  lb.end_prim();  // IR
  // SP references RefModel (Mesh) AND specializes the class (Scope):
  // specialize is globally WEAKEST, so the reference (Mesh) wins.
  lb.begin_prim("SP", "");
  lb.current()->meta().references.push_back("</Lib/RefModel>");
  lb.current()->meta().specializes.push_back("</_class_Base>");
  lb.end_prim();  // SP
  // V has a variant set "vset" with selection "high".
  lb.begin_prim("V", "");
  {
    VariantSetData vss;
    vss.name = "vset";
    VariantData hi;
    hi.name = "high";
    hi.properties.push_back({"variantHigh", Value::MakeFloat3(9, 9, 9)});
    VariantData lo;
    lo.name = "low";
    lo.properties.push_back({"variantLow", Value::MakeFloat3(1, 1, 1)});
    vss.variants.push_back(std::move(hi));
    vss.variants.push_back(std::move(lo));
    lb.current()->meta().variantSets.push_back(std::move(vss));
    lb.current()->meta().variantSelection = "vset=high";
  }
  lb.end_prim();  // V
  // VC has a variant whose CONTENT subtree adds a host property + a child prim.
  {
    auto content = std::make_shared<Layer>();
    LayerBuilder cb(*content);
    cb.begin_prim("__self__", "");
    cb.add_property("hostProp", Value::MakeFloat3(7, 7, 7));
    cb.begin_prim("Geom", "Mesh");
    cb.end_prim();  // Geom
    cb.end_prim();  // __self__
    cb.finalize();

    lb.begin_prim("VC", "");
    VariantSetData vss;
    vss.name = "geo";
    VariantData full;
    full.name = "full";
    full.content = content;
    vss.variants.push_back(std::move(full));
    lb.current()->meta().variantSets.push_back(std::move(vss));
    lb.current()->meta().variantSelection = "geo=full";
    lb.end_prim();  // VC
  }
  // MV selects TWO variant sets at once via variantSelections.
  {
    lb.begin_prim("MV", "");
    VariantSetData s1;
    s1.name = "s1";
    VariantData s1on;
    s1on.name = "on";
    s1on.properties.push_back({"p1", Value::MakeFloat3(1, 1, 1)});
    s1.variants.push_back(std::move(s1on));
    VariantSetData s2;
    s2.name = "s2";
    VariantData s2on;
    s2on.name = "on";
    s2on.properties.push_back({"p2", Value::MakeFloat3(2, 2, 2)});
    s2.variants.push_back(std::move(s2on));
    lb.current()->meta().variantSets.push_back(std::move(s1));
    lb.current()->meta().variantSets.push_back(std::move(s2));
    lb.current()->meta().variantSelections.push_back({"s1", "on"});
    lb.current()->meta().variantSelections.push_back({"s2", "on"});
    lb.end_prim();  // MV
  }
  // Three instanceable prims: Inst1/Inst2 reference the same asset (one
  // prototype, shared); Inst3 references a different asset (separate prototype).
  lb.begin_prim("Inst1", "");
  lb.current()->meta().references.push_back("</Lib/Model>");
  lb.current()->meta().instanceable = true;
  lb.end_prim();
  lb.begin_prim("Inst2", "");
  lb.current()->meta().references.push_back("</Lib/Model>");
  lb.current()->meta().instanceable = true;
  lb.end_prim();
  lb.begin_prim("Inst3", "");
  lb.current()->meta().references.push_back("</Lib/RefModel>");
  lb.current()->meta().instanceable = true;
  lb.end_prim();
  // Relocate /World/Old -> /World/New (authored on World).
  lb.current()->meta().relocates.push_back({"/World/Old", "/World/New"});
  lb.begin_prim("Old", "Scope");
  lb.end_prim();  // Old (relocated to New)
  lb.end_prim();  // World

  lb.begin_prim("Lib", "Scope");
  lb.begin_prim("Model", "Mesh");
  lb.add_property("size", Value::MakeFloat3(1.0f, 2.0f, 3.0f));
  lb.begin_prim("Inner", "Sphere");
  lb.end_prim();  // Inner
  lb.end_prim();  // Model
  lb.begin_prim("RefModel", "Mesh");
  lb.end_prim();  // RefModel
  lb.end_prim();  // Lib

  // Abstract class used by inherits/specializes (type "Scope").
  lb.begin_prim("_class_Base", "Scope", PrimSpecifier::Class);
  lb.end_prim();  // _class_Base

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

// Phase 3: inherits + specializes, with LIVRPS strength
// (Local > Inherit > Reference > Payload > Specialize).
static void test_inherits_specializes() {
  std::cout << "test_inherits_specializes..." << std::endl;
  AssetResolver resolver;
  auto root = BuildRootLayer();
  auto opened = pcp::Cache::Open(resolver, root);
  assert(opened);
  pcp::Cache cache = std::move(*opened);

  Stage stage;
  std::string warn, err;
  assert(cache.BuildStage(&stage, &warn, &err));

  // Inherit composes the class type.
  UsdPrim i = stage.GetPrimAtPath("/World/I");
  assert(i.IsValid());
  assert(i.GetTypeName() == "Scope" && "inherit did not compose class type");

  // Inherit (Scope) is STRONGER than the reference (Mesh).
  UsdPrim ir = stage.GetPrimAtPath("/World/IR");
  assert(ir.IsValid());
  assert(ir.GetTypeName() == "Scope" && "inherit should outrank reference");

  // Specialize (Scope) is WEAKER than the reference (Mesh).
  UsdPrim sp = stage.GetPrimAtPath("/World/SP");
  assert(sp.IsValid());
  assert(sp.GetTypeName() == "Mesh" && "reference should outrank specialize");

  std::cout << "  OK" << std::endl;
}

// Phase 4: variant selection grafts the chosen variant's opinions only.
static void test_variants() {
  std::cout << "test_variants..." << std::endl;
  AssetResolver resolver;
  auto root = BuildRootLayer();
  auto opened = pcp::Cache::Open(resolver, root);
  assert(opened);
  pcp::Cache cache = std::move(*opened);

  Stage stage;
  std::string warn, err;
  assert(cache.BuildStage(&stage, &warn, &err));

  UsdPrim v = stage.GetPrimAtPath("/World/V");
  assert(v.IsValid());
  assert(v.GetPropertyValue("variantHigh") != nullptr &&
         "selected variant 'high' not grafted");
  assert(v.GetPropertyValue("variantLow") == nullptr &&
         "non-selected variant 'low' leaked");
  std::cout << "  OK" << std::endl;
}

// FU2: variant subtree (adds geometry) + multi-selection.
static void test_variants_v2() {
  std::cout << "test_variants_v2..." << std::endl;
  AssetResolver resolver;
  auto root = BuildRootLayer();
  auto opened = pcp::Cache::Open(resolver, root);
  assert(opened);
  pcp::Cache cache = std::move(*opened);

  Stage stage;
  std::string warn, err;
  assert(cache.BuildStage(&stage, &warn, &err));

  // Content variant grafts a host property AND a child prim.
  UsdPrim vc = stage.GetPrimAtPath("/World/VC");
  assert(vc.IsValid());
  assert(vc.GetPropertyValue("hostProp") != nullptr && "variant host opinion missing");
  UsdPrim geom = stage.GetPrimAtPath("/World/VC/Geom");
  assert(geom.IsValid() && "variant child prim not grafted");
  assert(geom.GetTypeName() == "Mesh");

  // Multi-selection: both selected variant sets contribute.
  UsdPrim mv = stage.GetPrimAtPath("/World/MV");
  assert(mv.IsValid());
  assert(mv.GetPropertyValue("p1") != nullptr && "variant set s1 not applied");
  assert(mv.GetPropertyValue("p2") != nullptr && "variant set s2 not applied");
  std::cout << "  OK" << std::endl;
}

// Phase 5: instancing - structural prototype detection + grouping.
static void test_instancing() {
  std::cout << "test_instancing..." << std::endl;
  AssetResolver resolver;
  auto root = BuildRootLayer();
  auto opened = pcp::Cache::Open(resolver, root);
  assert(opened);
  pcp::Cache cache = std::move(*opened);

  std::string warn, err;
  std::vector<Path> paths{Path("/World/Inst1"), Path("/World/Inst2"),
                          Path("/World/Inst3")};
  assert(cache.PrewarmPrimIndices(paths, &warn, &err));

  // Two distinct prototypes: {Inst1,Inst2} share, Inst3 is its own.
  assert(cache.PrototypeCount() == 2);

  std::string k1 = cache.ComputeInstanceKey(Path("/World/Inst1"), &warn, &err);
  std::string k2 = cache.ComputeInstanceKey(Path("/World/Inst2"), &warn, &err);
  std::string k3 = cache.ComputeInstanceKey(Path("/World/Inst3"), &warn, &err);
  assert(k1 == k2 && "same-asset instances must share a key");
  assert(k1 != k3 && "different-asset instances must differ");

  Path p1 = cache.GetPrototype(Path("/World/Inst1"));
  Path p2 = cache.GetPrototype(Path("/World/Inst2"));
  assert(p1 == p2 && "Inst1/Inst2 must share a prototype");
  assert(cache.GetInstancesForPrototype(p1).size() == 2);

  // Exactly one of the two is the prototype (IsInstance == false), the other an
  // instance (IsInstance == true).
  bool i1 = cache.IsInstance(Path("/World/Inst1"));
  bool i2 = cache.IsInstance(Path("/World/Inst2"));
  assert(i1 != i2);
  std::cout << "  OK" << std::endl;
}

// FU1: relocates rename a prim in the composed namespace.
static void test_relocates() {
  std::cout << "test_relocates..." << std::endl;
  AssetResolver resolver;
  auto root = BuildRootLayer();
  auto opened = pcp::Cache::Open(resolver, root);
  assert(opened);
  pcp::Cache cache = std::move(*opened);

  Stage stage;
  std::string warn, err;
  assert(cache.BuildStage(&stage, &warn, &err));

  assert(stage.GetPrimAtPath("/World/New").IsValid() && "relocate target missing");
  assert(!stage.GetPrimAtPath("/World/Old").IsValid() && "relocate source leaked");
  assert(stage.GetPrimAtPath("/World/New").GetTypeName() == "Scope");
  std::cout << "  OK" << std::endl;
}

// FU3: implied inherit propagation. A referenced asset inherits a class; a
// root-authored override on the same class path composes (implied into root).
static void test_implied_inherit() {
  std::cout << "test_implied_inherit..." << std::endl;

  // In-memory referenced asset: a class with libClassProp + an Asset prim that
  // inherits it.
  auto asset = std::make_shared<Layer>();
  {
    LayerBuilder ab(*asset);
    ab.begin_prim("_class_Over", "Scope", PrimSpecifier::Class);
    ab.add_property("libClassProp", Value::MakeFloat3(1, 1, 1));
    ab.end_prim();
    ab.begin_prim("Asset", "Mesh");
    ab.current()->meta().inherits.push_back("</_class_Over>");
    ab.end_prim();
    ab.finalize();
  }

  // Root layer: its OWN _class_Over (rootClassProp) + Q references the asset.
  auto rootL = std::make_shared<Layer>();
  {
    LayerBuilder rb(*rootL);
    rb.begin_prim("_class_Over", "Scope", PrimSpecifier::Class);
    rb.add_property("rootClassProp", Value::MakeFloat3(2, 2, 2));
    rb.end_prim();
    rb.begin_prim("World", "Xform");
    rb.begin_prim("Q", "");
    rb.current()->meta().references.push_back("@mem_asset@</Asset>");
    rb.end_prim();
    rb.end_prim();
    rb.finalize();
  }

  AssetResolver resolver;
  resolver.SetCustomResolver(
      [](const std::string &a, const std::string &) { return a; });
  auto opened = pcp::Cache::Open(resolver, rootL);
  assert(opened);
  pcp::Cache cache = std::move(*opened);
  cache.PreloadLayer("mem_asset", asset);

  Stage stage;
  std::string warn, err;
  assert(cache.BuildStage(&stage, &warn, &err));

  UsdPrim q = stage.GetPrimAtPath("/World/Q");
  assert(q.IsValid());
  assert(q.GetTypeName() == "Mesh" && "cross-file reference type missing");
  assert(q.GetPropertyValue("libClassProp") != nullptr &&
         "direct (referenced-stack) inherit missing");
  assert(q.GetPropertyValue("rootClassProp") != nullptr &&
         "IMPLIED root-stack inherit override missing");
  std::cout << "  OK" << std::endl;
}

// FU5: BuildStage materializes instances as proxies that share the prototype's
// subtree (no duplication), with transparent child access.
static void test_instance_proxy() {
  std::cout << "test_instance_proxy..." << std::endl;
  AssetResolver resolver;
  auto root = BuildRootLayer();
  auto opened = pcp::Cache::Open(resolver, root);
  assert(opened);
  pcp::Cache cache = std::move(*opened);

  Stage stage;
  std::string warn, err;
  assert(cache.BuildStage(&stage, &warn, &err));

  UsdPrim i1 = stage.GetPrimAtPath("/World/Inst1");
  UsdPrim i2 = stage.GetPrimAtPath("/World/Inst2");
  assert(i1.IsValid() && i2.IsValid());

  // Inst1 is the prototype (subtree materialized); Inst2 links to it.
  assert(i1.GetMeta().instance_prototype.empty());
  assert(i2.GetMeta().instance_prototype == "/World/Inst1");

  // The prototype owns the referenced child; the instance's subtree is NOT
  // duplicated in the layer.
  assert(stage.GetPrimAtPath("/World/Inst1/Inner").IsValid());
  assert(!stage.GetPrimAtPath("/World/Inst2/Inner").IsValid() &&
         "instance subtree was duplicated");

  // ...but child access through the instance is transparent (proxy).
  assert(i2.GetChildCount() == i1.GetChildCount());
  assert(i2.GetChild("Inner").IsValid() && "instance proxy child access failed");
  std::cout << "  OK" << std::endl;
}

// FU6: PrewarmPrimIndices with num_threads>1 parallelizes layer prefetch via the
// thread-safe registry. Uses preloaded in-memory assets so the parallel path is
// exercised safely; the composed result must match a sequential build.
static void test_parallel_prewarm() {
  std::cout << "test_parallel_prewarm..." << std::endl;
  const int N = 8;

  // N distinct in-memory assets, each a Mesh "A".
  std::vector<std::shared_ptr<Layer>> assets;
  for (int k = 0; k < N; ++k) {
    auto a = std::make_shared<Layer>();
    LayerBuilder ab(*a);
    ab.begin_prim("A", "Mesh");
    ab.add_property("tag", Value::MakeFloat3(float(k), 0, 0));
    ab.end_prim();
    ab.finalize();
    assets.push_back(a);
  }

  // Root: /World/R{k} references @asset_{k}@</A>.
  auto rootL = std::make_shared<Layer>();
  {
    LayerBuilder rb(*rootL);
    rb.begin_prim("World", "Xform");
    for (int k = 0; k < N; ++k) {
      rb.begin_prim("R" + std::to_string(k), "");
      rb.current()->meta().references.push_back("@asset_" + std::to_string(k) +
                                                "@</A>");
      rb.end_prim();
    }
    rb.end_prim();
    rb.finalize();
  }

  AssetResolver resolver;
  resolver.SetCustomResolver(
      [](const std::string &a, const std::string &) { return a; });

  pcp::CompositionOptions opts;
  opts.num_threads = 4;
  auto opened = pcp::Cache::Open(resolver, rootL, "", opts);
  assert(opened);
  pcp::Cache cache = std::move(*opened);
  for (int k = 0; k < N; ++k) {
    cache.PreloadLayer("asset_" + std::to_string(k), assets[k]);
  }

  std::vector<Path> paths;
  for (int k = 0; k < N; ++k) paths.push_back(Path("/World/R" + std::to_string(k)));

  std::string warn, err;
  assert(cache.PrewarmPrimIndices(paths, &warn, &err));

  // Every prim's index is built and composed the referenced Mesh.
  for (int k = 0; k < N; ++k) {
    Path p("/World/R" + std::to_string(k));
    assert(cache.HasComputedPrimIndex(p));
    const pcp::PrimIndex *idx = cache.ComputePrimIndex(p, &warn, &err);
    assert(idx && idx->GetNodeCount() >= 2);
  }
  std::cout << "  OK" << std::endl;
}

// FU7: cross-source variant selection - the variantSet is defined on a
// referenced asset, but the SELECTION is authored on the (stronger) local prim.
static void test_cross_source_variant() {
  std::cout << "test_cross_source_variant..." << std::endl;

  // Asset defines variantSet "vs" with variant "hi" (grafts xvar), no selection.
  auto asset = std::make_shared<Layer>();
  {
    LayerBuilder ab(*asset);
    ab.begin_prim("A", "");
    VariantSetData vss;
    vss.name = "vs";
    VariantData hi;
    hi.name = "hi";
    hi.properties.push_back({"xvar", Value::MakeFloat3(5, 5, 5)});
    vss.variants.push_back(std::move(hi));
    ab.current()->meta().variantSets.push_back(std::move(vss));
    ab.end_prim();
    ab.finalize();
  }

  // Root: CS references the asset AND authors the selection vs=hi.
  auto rootL = std::make_shared<Layer>();
  {
    LayerBuilder rb(*rootL);
    rb.begin_prim("World", "Xform");
    rb.begin_prim("CS", "");
    rb.current()->meta().references.push_back("@cs_asset@</A>");
    rb.current()->meta().variantSelection = "vs=hi";
    rb.end_prim();
    rb.end_prim();
    rb.finalize();
  }

  AssetResolver resolver;
  resolver.SetCustomResolver(
      [](const std::string &a, const std::string &) { return a; });
  auto opened = pcp::Cache::Open(resolver, rootL);
  assert(opened);
  pcp::Cache cache = std::move(*opened);
  cache.PreloadLayer("cs_asset", asset);

  Stage stage;
  std::string warn, err;
  assert(cache.BuildStage(&stage, &warn, &err));

  UsdPrim cs = stage.GetPrimAtPath("/World/CS");
  assert(cs.IsValid());
  assert(cs.GetPropertyValue("xvar") != nullptr &&
         "cross-source variant selection failed");
  std::cout << "  OK" << std::endl;
}

// FU8: implied class arcs propagate into INTERMEDIATE stacks of a multi-level
// reference chain (root -> A -> B), not just root. B inherits a class; each of
// B's, A's, and root's class opinions composes onto the final prim.
static void test_implied_intermediate() {
  std::cout << "test_implied_intermediate..." << std::endl;

  auto B = std::make_shared<Layer>();
  {
    LayerBuilder bb(*B);
    bb.begin_prim("_class_Foo", "Scope", PrimSpecifier::Class);
    bb.add_property("fooB", Value::MakeFloat3(1, 0, 0));
    bb.end_prim();
    bb.begin_prim("B", "Mesh");
    bb.current()->meta().inherits.push_back("</_class_Foo>");
    bb.end_prim();
    bb.finalize();
  }
  auto A = std::make_shared<Layer>();
  {
    LayerBuilder ab(*A);
    ab.begin_prim("_class_Foo", "Scope", PrimSpecifier::Class);
    ab.add_property("fooA", Value::MakeFloat3(0, 1, 0));
    ab.end_prim();
    ab.begin_prim("A", "");
    ab.current()->meta().references.push_back("@assetB@</B>");
    ab.end_prim();
    ab.finalize();
  }
  auto rootL = std::make_shared<Layer>();
  {
    LayerBuilder rb(*rootL);
    rb.begin_prim("_class_Foo", "Scope", PrimSpecifier::Class);
    rb.add_property("fooRoot", Value::MakeFloat3(0, 0, 1));
    rb.end_prim();
    rb.begin_prim("World", "Xform");
    rb.begin_prim("T", "");
    rb.current()->meta().references.push_back("@assetA@</A>");
    rb.end_prim();
    rb.end_prim();
    rb.finalize();
  }

  AssetResolver resolver;
  resolver.SetCustomResolver(
      [](const std::string &a, const std::string &) { return a; });
  auto opened = pcp::Cache::Open(resolver, rootL);
  assert(opened);
  pcp::Cache cache = std::move(*opened);
  cache.PreloadLayer("assetA", A);
  cache.PreloadLayer("assetB", B);

  Stage stage;
  std::string warn, err;
  assert(cache.BuildStage(&stage, &warn, &err));

  UsdPrim t = stage.GetPrimAtPath("/World/T");
  assert(t.IsValid());
  assert(t.GetTypeName() == "Mesh");
  assert(t.GetPropertyValue("fooB") != nullptr && "direct (B stack) class missing");
  assert(t.GetPropertyValue("fooA") != nullptr &&
         "implied INTERMEDIATE (A stack) class missing");
  assert(t.GetPropertyValue("fooRoot") != nullptr &&
         "implied root class missing");
  std::cout << "  OK" << std::endl;
}

// FU10/FU12: end-to-end one-call composition from real USDA files, with a
// cross-file reference (exercises reader arc-metadata parsing + registry +
// namespace mapping).
static void test_compose_from_file() {
  std::cout << "test_compose_from_file..." << std::endl;

  const std::string ref = "/tmp/next_pcp_ref.usda";
  const std::string root = "/tmp/next_pcp_root.usda";
  { std::ofstream f(ref); f << "#usda 1.0\ndef Mesh \"Geom\"\n{\n}\n"; }
  {
    std::ofstream f(root);
    f << "#usda 1.0\n"
         "def Xform \"World\"\n{\n"
         "    def \"Q\" (\n"
         "        prepend references = [@next_pcp_ref.usda@</Geom>]\n"
         "    )\n    {\n    }\n}\n";
  }

  AssetResolver resolver;
  resolver.SetWorkingDirectory("/tmp");

  Stage stage;
  std::string warn, err;
  bool ok = pcp::ComposeStageFromFile(root, resolver, &stage, {}, &warn, &err);
  if (!ok) std::cout << "  [diag] err=" << err << std::endl;
  assert(ok && "ComposeStageFromFile failed");

  UsdPrim q = stage.GetPrimAtPath("/World/Q");
  assert(q.IsValid() && "/World/Q missing");
  assert(q.GetTypeName() == "Mesh" &&
         "cross-file reference did not compose through the loader");

  std::remove(ref.c_str());
  std::remove(root.c_str());
  std::cout << "  OK" << std::endl;
}

// Sublayers: weaker layer-stack opinions fill stronger root opinions; roots,
// child prims, and arcs authored only in a sublayer must still compose.
static void test_sublayer_stack_composition() {
  std::cout << "test_sublayer_stack_composition..." << std::endl;

  const std::string sub = "/tmp/next_pcp_sublayer_sub.usda";
  {
    std::ofstream f(sub);
    f << "#usda 1.0\n"
         "def Xform \"World\"\n"
         "{\n"
         "    custom int weakVal = 5\n"
         "    def Scope \"WeakChild\"\n"
         "    {\n"
         "    }\n"
         "    def \"SubRef\" (\n"
         "        prepend references = [</Lib/Model>]\n"
         "    )\n"
         "    {\n"
         "    }\n"
         "    def \"SubPayload\" (\n"
         "        payload = </Lib/Model>\n"
         "    )\n"
         "    {\n"
         "    }\n"
         "}\n"
         "def Scope \"OnlyInSub\"\n"
         "{\n"
         "    custom int subOnly = 9\n"
         "}\n"
         "def Scope \"Lib\"\n"
         "{\n"
         "    def Mesh \"Model\"\n"
         "    {\n"
         "        custom int modelVal = 7\n"
         "    }\n"
         "}\n";
  }

  auto root_layer = std::make_shared<Layer>();
  {
    root_layer->meta().subLayers.push_back("next_pcp_sublayer_sub.usda");
    LayerBuilder rb(*root_layer);
    rb.begin_prim("World", "Xform");
    rb.begin_prim("StrongChild", "Scope");
    rb.end_prim();
    rb.end_prim();
    rb.finalize();
  }

  AssetResolver resolver;
  resolver.SetWorkingDirectory("/tmp");
  std::string warn, err;
  auto opened = pcp::Cache::Open(resolver, root_layer,
                                 "/tmp/next_pcp_sublayer_root.usda");
  assert(opened && "sublayer Cache::Open failed");
  pcp::Cache cache = std::move(*opened);
  Stage stage;
  bool ok = cache.BuildStage(&stage, &warn, &err);
  if (!ok) std::cout << "  [diag] err=" << err << std::endl;
  assert(ok && "sublayer BuildStage failed");

  UsdPrim world = stage.GetPrimAtPath("/World");
  assert(world.IsValid());
  assert(world.GetPropertyValue("weakVal") != nullptr &&
         "weaker sublayer property did not fill");
  assert(stage.GetPrimAtPath("/World/StrongChild").IsValid());
  assert(stage.GetPrimAtPath("/World/WeakChild").IsValid() &&
         "weaker sublayer child missing");
  assert(stage.GetPrimAtPath("/OnlyInSub").IsValid() &&
         "sublayer-only root missing");

  UsdPrim sub_ref = stage.GetPrimAtPath("/World/SubRef");
  assert(sub_ref.IsValid());
  assert(sub_ref.GetTypeName() == "Mesh" &&
         "sublayer-authored reference did not compose");
  assert(sub_ref.GetPropertyValue("modelVal") != nullptr);

  UsdPrim sub_payload = stage.GetPrimAtPath("/World/SubPayload");
  assert(sub_payload.IsValid());
  assert(sub_payload.GetTypeName() == "Mesh" &&
         "sublayer-authored payload did not compose");
  assert(sub_payload.GetPropertyValue("modelVal") != nullptr);

  std::remove(sub.c_str());
  std::cout << "  OK" << std::endl;
}

static void test_sublayer_cycle_and_depth() {
  std::cout << "test_sublayer_cycle_and_depth..." << std::endl;

  const std::string cycle_root = "/tmp/next_pcp_cycle_root.usda";
  {
    std::ofstream f(cycle_root);
    f << "#usda 1.0\n"
         "def Xform \"Root\"\n"
         "{\n"
         "}\n";
  }
  auto cycle_layer = std::make_shared<Layer>();
  {
    cycle_layer->meta().subLayers.push_back("next_pcp_cycle_root.usda");
    LayerBuilder lb(*cycle_layer);
    lb.begin_prim("Root", "Xform");
    lb.end_prim();
    lb.finalize();
  }

  AssetResolver resolver;
  resolver.SetWorkingDirectory("/tmp");
  std::string warn, err;
  auto cycle_opened = pcp::Cache::Open(resolver, cycle_layer, cycle_root);
  assert(!cycle_opened && "sublayer cycle should fail Cache::Open");
  assert(cycle_opened.error().find("Sublayer cycle") != std::string::npos);

  const std::string depth_sub = "/tmp/next_pcp_depth_sub.usda";
  {
    std::ofstream f(depth_sub);
    f << "#usda 1.0\n"
         "def Scope \"Sub\"\n"
         "{\n"
         "}\n";
  }

  auto root_layer = std::make_shared<Layer>();
  {
    root_layer->meta().subLayers.push_back("next_pcp_depth_sub.usda");
    LayerBuilder lb(*root_layer);
    lb.begin_prim("Root", "Xform");
    lb.end_prim();
    lb.finalize();
  }
  pcp::CompositionOptions opts;
  opts.max_depth = 0;
  auto opened = pcp::Cache::Open(resolver, root_layer,
                                 "/tmp/next_pcp_depth_root.usda", opts);
  assert(!opened && "sublayer depth overflow should fail Cache::Open");

  std::remove(cycle_root.c_str());
  std::remove(depth_sub.c_str());
  std::cout << "  OK" << std::endl;
}

static void test_node_overflow_fails_cleanly() {
  std::cout << "test_node_overflow_fails_cleanly..." << std::endl;

  auto root = std::make_shared<Layer>();
  {
    LayerBuilder lb(*root);
    lb.begin_prim("Boom", "");
    for (size_t i = 0; i < pcp::PrimIndex::kMaxNodeCount; ++i) {
      lb.current()->meta().references.push_back("</Lib/A>");
    }
    lb.end_prim();
    lb.begin_prim("Lib", "Scope");
    lb.begin_prim("A", "Mesh");
    lb.end_prim();
    lb.end_prim();
    lb.finalize();
  }

  AssetResolver resolver;
  auto opened = pcp::Cache::Open(resolver, root);
  assert(opened);
  pcp::Cache cache = std::move(*opened);
  std::string warn, err;
  const pcp::PrimIndex *idx =
      cache.ComputePrimIndex(Path("/Boom"), &warn, &err);
  assert(idx == nullptr && "overflowing PrimIndex should not be published");
  assert(err.find("uint16 capacity") != std::string::npos);
  assert(!cache.HasComputedPrimIndex(Path("/Boom")));
  std::cout << "  OK" << std::endl;
}

// FU9: the Cache is thread-safe (built with TINYUSDZ_ENABLE_THREAD) -- multiple
// threads may query/compose a shared cache concurrently.
static void test_concurrent_queries() {
  std::cout << "test_concurrent_queries..." << std::endl;
#if defined(TINYUSDZ_ENABLE_THREAD)
  AssetResolver resolver;
  auto root = BuildRootLayer();
  auto opened = pcp::Cache::Open(resolver, root);
  assert(opened);
  pcp::Cache cache = std::move(*opened);

  const std::vector<std::string> existing = {
      "/World", "/World/A", "/World/A/Inner", "/World/I",
      "/World/IR", "/World/SP", "/World/V", "/Lib/Model"};

  const int kThreads = 8;
  const int kIters = 1000;
  std::atomic<bool> ok{true};
  auto worker = [&]() {
    std::string w, e;
    for (int it = 0; it < kIters; ++it) {
      for (const std::string &p : existing) {
        if (cache.ComputePrimIndex(Path(p), &w, &e) == nullptr) ok.store(false);
      }
      (void)cache.ComputedPrimIndexCount();
      (void)cache.HasComputedPrimIndex(Path("/World/A"));
    }
  };
  std::vector<std::thread> ts;
  for (int i = 0; i < kThreads; ++i) ts.emplace_back(worker);
  for (auto &t : ts) t.join();
  assert(ok.load());
#else
  std::cout << "  (skipped: build with -DTINYUSDZ_NEXT_ENABLE_THREAD=ON)\n";
#endif
  std::cout << "  OK" << std::endl;
}

// FU9 (parallel build): a canonical, index-order-independent serialization of a
// PrimIndex -- strength-ordered (arc, site-string, has-specs) tuples + count.
// Captures everything observable about composition while ignoring the internal
// interning integers (which may differ between a serial and a parallel build).
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

// Canonical (sorted) dump of the instance/prototype groupings of a cache.
static std::string CanonInstances(const pcp::Cache &cache) {
  std::vector<Path> protos = cache.GetPrototypePaths();
  std::vector<std::string> rows;
  for (const Path &p : protos) {
    std::vector<Path> inst = cache.GetInstancesForPrototype(p);
    std::vector<std::string> names;
    for (const Path &i : inst) names.push_back(i.str());
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

// FU9 (parallel build): building a batch with num_threads>1 must produce indices
// structurally identical to a serial (num_threads=1) build, and the same
// instance/prototype groupings. Exercises the per-worker lock-free build + the
// deterministic input-order merge (incl. ordered prototype assignment).
static void test_parallel_build_matches_serial() {
  std::cout << "test_parallel_build_matches_serial..." << std::endl;
#if defined(TINYUSDZ_ENABLE_THREAD)
  const std::vector<std::string> path_strs = {
      "/World",          "/World/A",     "/World/A/Inner", "/World/I",
      "/World/IR",       "/World/SP",    "/World/V",       "/World/VC",
      "/World/VC/Geom",  "/World/MV",    "/World/Inst1",   "/World/Inst2",
      "/World/Inst3",    "/Lib/Model",   "/Lib/Model/Inner", "/Lib/RefModel"};
  std::vector<Path> paths;
  for (const std::string &p : path_strs) paths.push_back(Path(p));

  auto run = [&](int num_threads) -> std::pair<std::vector<std::string>, std::string> {
    AssetResolver resolver;
    auto root = BuildRootLayer();
    pcp::CompositionOptions opts;
    opts.num_threads = num_threads;
    auto opened = pcp::Cache::Open(resolver, root, "", opts);
    assert(opened);
    pcp::Cache cache = std::move(*opened);
    std::string warn, err;
    assert(cache.PrewarmPrimIndices(paths, &warn, &err));
    std::vector<std::string> dumps;
    for (const Path &p : paths) {
      dumps.push_back(CanonIndex(cache.ComputePrimIndex(p, &warn, &err)));
    }
    return {dumps, CanonInstances(cache)};
  };

  auto serial = run(1);
  auto parallel = run(4);

  for (size_t i = 0; i < paths.size(); ++i) {
    if (serial.first[i] != parallel.first[i]) {
      std::cout << "  MISMATCH at " << path_strs[i] << "\n   serial:   "
                << serial.first[i] << "\n   parallel: " << parallel.first[i]
                << std::endl;
    }
    assert(serial.first[i] == parallel.first[i] &&
           "parallel-built index differs from serial");
  }
  assert(serial.second == parallel.second &&
         "parallel instance groupings differ from serial");
  // Sanity: the instanceable prims actually grouped (Inst1+Inst2 share a
  // prototype; Inst3 is its own), so the ordered-merge path was exercised.
  assert(!parallel.second.empty() && "expected instance groupings");
#else
  std::cout << "  (skipped: build with -DTINYUSDZ_NEXT_ENABLE_THREAD=ON)\n";
#endif
  std::cout << "  OK" << std::endl;
}

// --- security: cycle / recursion hardening ----------------------------------
// Adversarial composition graphs must surface errors, never crash or exhaust
// the C++ stack (the module builds with -fno-exceptions).

// `def "A" { def "B" (references = </A>) {} }`: the referenced subtree contains
// the referencing prim, so the composed namespace would grow forever. Caught by
// the same-stack ancestor-arc check in ProcessArc.
static void test_ancestor_reference_cycle() {
  std::cout << "test_ancestor_reference_cycle..." << std::endl;
  auto root = std::make_shared<Layer>();
  {
    LayerBuilder lb(*root);
    lb.begin_prim("A", "Xform");
    lb.add_property("rootProp", Value::MakeFloat3(1, 1, 1));
    lb.begin_prim("B", "");
    lb.current()->meta().references.push_back("</A>");
    lb.end_prim();
    lb.end_prim();
    lb.finalize();
  }

  AssetResolver resolver;
  auto opened = pcp::Cache::Open(resolver, root);
  assert(opened);
  pcp::Cache cache = std::move(*opened);

  Stage stage;
  std::string warn, err;
  assert(cache.BuildStage(&stage, &warn, &err));
  assert(!err.empty() && "ancestor-reference cycle must be reported");
  // The cycle arc is dropped: /A/B exists, but no unbounded /A/B/B/... chain.
  assert(stage.GetPrimAtPath("/A/B").IsValid());
  assert(!stage.GetPrimAtPath("/A/B/B/B/B/B/B/B/B").IsValid());
  std::cout << "  OK" << std::endl;
}

// Mutual reference cycle across two in-memory "files": A -> @b@</B> -> @a@</A>.
// Caught by the expansion frame chain (site revisited within one expansion).
static void test_mutual_reference_cycle() {
  std::cout << "test_mutual_reference_cycle..." << std::endl;
  auto a = std::make_shared<Layer>();
  {
    LayerBuilder ab(*a);
    ab.begin_prim("A", "Mesh");
    ab.add_property("fromA", Value::MakeFloat3(1, 0, 0));
    ab.current()->meta().references.push_back("@asset_b@</B>");
    ab.end_prim();
    ab.finalize();
  }
  auto b = std::make_shared<Layer>();
  {
    LayerBuilder bb(*b);
    bb.begin_prim("B", "");
    bb.add_property("fromB", Value::MakeFloat3(0, 1, 0));
    bb.current()->meta().references.push_back("@asset_a@</A>");
    bb.end_prim();
    bb.finalize();
  }
  auto rootL = std::make_shared<Layer>();
  {
    LayerBuilder rb(*rootL);
    rb.begin_prim("X", "");
    rb.current()->meta().references.push_back("@asset_a@</A>");
    rb.end_prim();
    rb.finalize();
  }

  AssetResolver resolver;
  resolver.SetCustomResolver(
      [](const std::string &p, const std::string &) { return p; });
  auto opened = pcp::Cache::Open(resolver, rootL);
  assert(opened);
  pcp::Cache cache = std::move(*opened);
  cache.PreloadLayer("asset_a", a);
  cache.PreloadLayer("asset_b", b);

  Stage stage;
  std::string warn, err;
  assert(cache.BuildStage(&stage, &warn, &err));
  assert(!err.empty() && "mutual reference cycle must be reported");
  // Opinions up to the cycle point still compose.
  UsdPrim x = stage.GetPrimAtPath("/X");
  assert(x.IsValid());
  assert(x.GetPropertyValue("fromA") != nullptr);
  assert(x.GetPropertyValue("fromB") != nullptr);
  std::cout << "  OK" << std::endl;
}

// An (internal) payload arc targeting the prim's own site.
static void test_self_payload_cycle() {
  std::cout << "test_self_payload_cycle..." << std::endl;
  auto root = std::make_shared<Layer>();
  {
    LayerBuilder lb(*root);
    lb.begin_prim("P", "Xform");
    lb.current()->meta().payloads.push_back("</P>");
    lb.end_prim();
    lb.finalize();
  }
  AssetResolver resolver;
  auto opened = pcp::Cache::Open(resolver, root);
  assert(opened);
  pcp::Cache cache = std::move(*opened);
  Stage stage;
  std::string warn, err;
  assert(cache.BuildStage(&stage, &warn, &err));
  assert(!err.empty() && "self-payload cycle must be reported");
  assert(stage.GetPrimAtPath("/P").IsValid());
  std::cout << "  OK" << std::endl;
}

// A selected variant whose content subtree references itself.
static void test_variant_content_cycle() {
  std::cout << "test_variant_content_cycle..." << std::endl;
  auto content = std::make_shared<Layer>();
  {
    LayerBuilder cb(*content);
    cb.begin_prim("__self__", "");
    cb.current()->meta().references.push_back("</__self__>");
    cb.add_property("vProp", Value::MakeFloat3(3, 3, 3));
    cb.end_prim();
    cb.finalize();
  }
  auto root = std::make_shared<Layer>();
  {
    LayerBuilder lb(*root);
    lb.begin_prim("VC", "");
    VariantSetData vss;
    vss.name = "geo";
    VariantData full;
    full.name = "full";
    full.content = content;
    vss.variants.push_back(std::move(full));
    lb.current()->meta().variantSets.push_back(std::move(vss));
    lb.current()->meta().variantSelection = "geo=full";
    lb.end_prim();
    lb.finalize();
  }
  AssetResolver resolver;
  auto opened = pcp::Cache::Open(resolver, root);
  assert(opened);
  pcp::Cache cache = std::move(*opened);
  Stage stage;
  std::string warn, err;
  assert(cache.BuildStage(&stage, &warn, &err));
  assert(!err.empty() && "variant-content self-reference must be reported");
  UsdPrim vc = stage.GetPrimAtPath("/VC");
  assert(vc.IsValid());
  assert(vc.GetPropertyValue("vProp") != nullptr);
  std::cout << "  OK" << std::endl;
}

// A deep (but legitimate) authored hierarchy composes; one beyond the
// namespace-depth backstop errors instead of exhausting the stack.
static void test_deep_hierarchy() {
  std::cout << "test_deep_hierarchy..." << std::endl;
  auto build_deep = [](size_t depth) {
    auto l = std::make_shared<Layer>();
    LayerBuilder lb(*l);
    for (size_t i = 0; i < depth; ++i) lb.begin_prim("P" + std::to_string(i), "Xform");
    for (size_t i = 0; i < depth; ++i) lb.end_prim();
    lb.finalize();
    return l;
  };

  {
    AssetResolver resolver;
    auto opened = pcp::Cache::Open(resolver, build_deep(1000));
    assert(opened);
    pcp::Cache cache = std::move(*opened);
    Stage stage;
    std::string warn, err;
    assert(cache.BuildStage(&stage, &warn, &err));
    assert(err.empty() && "1000-deep authored hierarchy must compose");
  }
  {
    AssetResolver resolver;
    pcp::CompositionOptions opts;
    opts.max_namespace_depth = 64;
    auto opened = pcp::Cache::Open(resolver, build_deep(100), "", opts);
    assert(opened);
    pcp::Cache cache = std::move(*opened);
    Stage stage;
    std::string warn, err;
    assert(cache.BuildStage(&stage, &warn, &err));
    assert(!err.empty() && "beyond-backstop depth must be reported");
  }
  std::cout << "  OK" << std::endl;
}

// A reference chain longer than max_depth errors cleanly; a shorter one
// composes the deepest opinion through.
static void test_reference_chain_at_max_depth() {
  std::cout << "test_reference_chain_at_max_depth..." << std::endl;
  auto run_chain = [](size_t D, uint32_t max_depth, std::string *err_out) {
    std::vector<std::shared_ptr<Layer>> chain;
    for (size_t k = 0; k < D; ++k) {
      auto l = std::make_shared<Layer>();
      LayerBuilder lb(*l);
      lb.begin_prim("A", k + 1 == D ? "Mesh" : "");
      if (k + 1 < D) {
        lb.current()->meta().references.push_back("@chain_" +
                                                  std::to_string(k + 1) + "@</A>");
      } else {
        lb.add_property("deepest", Value::MakeFloat3(9, 9, 9));
      }
      lb.end_prim();
      lb.finalize();
      chain.push_back(l);
    }
    auto rootL = std::make_shared<Layer>();
    {
      LayerBuilder rb(*rootL);
      rb.begin_prim("Top", "");
      rb.current()->meta().references.push_back("@chain_0@</A>");
      rb.end_prim();
      rb.finalize();
    }
    AssetResolver resolver;
    resolver.SetCustomResolver(
        [](const std::string &p, const std::string &) { return p; });
    pcp::CompositionOptions opts;
    opts.max_depth = max_depth;
    auto opened = pcp::Cache::Open(resolver, rootL, "", opts);
    assert(opened);
    pcp::Cache cache = std::move(*opened);
    for (size_t k = 0; k < D; ++k)
      cache.PreloadLayer("chain_" + std::to_string(k), chain[k]);
    Stage stage;
    std::string warn;
    assert(cache.BuildStage(&stage, &warn, err_out));
    return stage.GetPrimAtPath("/Top").GetPropertyValue("deepest") != nullptr;
  };

  std::string err;
  assert(run_chain(100, 256, &err) && err.empty() &&
         "short chain must compose fully");
  err.clear();
  assert(!run_chain(300, 256, &err) && !err.empty() &&
         "over-max_depth chain must error (and not crash)");
  std::cout << "  OK" << std::endl;
}

int main() {
  test_compute_prim_index();
  test_build_stage();
  test_invalidate();
  test_ancestral_compute();
  test_deferred_payload();
  test_inherits_specializes();
  test_variants();
  test_variants_v2();
  test_instancing();
  test_relocates();
  test_implied_inherit();
  test_instance_proxy();
  test_parallel_prewarm();
  test_cross_source_variant();
  test_implied_intermediate();
  test_compose_from_file();
  test_sublayer_stack_composition();
  test_sublayer_cycle_and_depth();
  test_node_overflow_fails_cleanly();
  test_concurrent_queries();
  test_parallel_build_matches_serial();
  test_ancestor_reference_cycle();
  test_mutual_reference_cycle();
  test_self_payload_cycle();
  test_variant_content_cycle();
  test_deep_hierarchy();
  test_reference_chain_at_max_depth();
  std::cout << "All next/pcp tests passed." << std::endl;
  return 0;
}
