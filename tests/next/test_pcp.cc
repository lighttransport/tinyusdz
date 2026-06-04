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
  std::cout << "All next/pcp tests passed." << std::endl;
  return 0;
}
