// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Composition arc-expansion tests for src/next: references (internal +
// external, with subtree grafting), payloads, inherits, specializes, local
// override strength, arc clearing, and cycle safety.

#include "next/composition/composition.hh"
#include "next/layer/layer.hh"
#include "next/layer/prim-spec.hh"
#include "next/types/value.hh"
#include "next/crate/crate-writer.hh"
#include "next/crate/crate-reader.hh"

#include <iostream>
#include <memory>
#include <string>

using namespace tinyusdz::next;

static int g_fail = 0;
#define CHECK(cond, msg)                                                    \
  do {                                                                      \
    if (!(cond)) { std::cerr << "  FAIL: " << msg << "\n"; ++g_fail; }      \
    else { std::cout << "  ok: " << msg << "\n"; }                          \
  } while (0)

static PrimSpec MakePrim(const std::string& path, const std::string& type) {
  size_t s = path.rfind('/');
  std::string name = (s == std::string::npos) ? path : path.substr(s + 1);
  PrimSpec p(name, type);
  p.set_path(Path(path));
  return p;
}

static const Value* PropOf(const Layer& l, const std::string& prim_path,
                           const std::string& prop) {
  const PrimSpec* p = l.prim_at_path(prim_path);
  if (!p) return nullptr;
  return p->property_value(prop);
}

// Inherits copy class opinions; local opinion wins; arc cleared after flatten.
static void test_inherits() {
  std::cout << "[inherits]\n";
  Layer layer;
  {
    PrimSpec cls = MakePrim("/_class_Sphere", "Sphere");
    cls.add_property("radius", Value(2.0));
    cls.add_property("displayColor", Value(std::string("red")));
    layer.add_prim(std::move(cls));
  }
  {
    PrimSpec ball = MakePrim("/Ball", "Sphere");
    ball.add_property("radius", Value(5.0));  // local override (strongest)
    ball.meta().inherits.push_back("/_class_Sphere");
    layer.add_prim(std::move(ball));
  }
  layer.finalize();

  Compositor comp;
  auto out = comp.Compose(layer);
  CHECK(out != nullptr, "compose succeeds");
  const Value* r = PropOf(*out, "/Ball", "radius");
  CHECK(r && r->as_double() && *r->as_double() == 5.0,
        "local radius wins over inherited (5.0)");
  CHECK(PropOf(*out, "/Ball", "displayColor") != nullptr,
        "displayColor inherited from class");
  const PrimSpec* ball = out->prim_at_path("/Ball");
  CHECK(ball && ball->meta().inherits.empty(),
        "inherits arc cleared after flatten");
}

// Internal reference copies opinions AND grafts the referenced subtree.
static void test_internal_reference() {
  std::cout << "[internal reference]\n";
  Layer layer;
  {
    PrimSpec lib = MakePrim("/Lib", "Xform");
    lib.add_property("v", Value(int32_t(1)));
    layer.add_prim(std::move(lib));
  }
  {
    PrimSpec child = MakePrim("/Lib/Mesh", "Mesh");
    child.add_property("points", Value(int32_t(7)));
    layer.add_prim(std::move(child));
  }
  {
    PrimSpec inst = MakePrim("/Inst", "Xform");
    inst.meta().references.push_back("</Lib>");  // internal ref
    layer.add_prim(std::move(inst));
  }
  layer.finalize();

  Compositor comp;
  auto out = comp.Compose(layer);
  CHECK(out != nullptr, "compose succeeds");
  CHECK(PropOf(*out, "/Inst", "v") != nullptr,
        "referenced opinion copied onto /Inst");
  CHECK(out->prim_at_path("/Inst/Mesh") != nullptr,
        "referenced subtree grafted under /Inst");
  CHECK(PropOf(*out, "/Inst/Mesh", "points") != nullptr,
        "grafted child carries its properties");
  const PrimSpec* inst = out->prim_at_path("/Inst");
  CHECK(inst && inst->meta().references.empty(),
        "reference arc cleared after flatten");
  // Original library prim still exists independently.
  CHECK(out->prim_at_path("/Lib") != nullptr, "/Lib still present");
}

// External reference resolved via a layer loader, with subtree graft.
static void test_external_reference() {
  std::cout << "[external reference]\n";
  Layer root;
  {
    PrimSpec r = MakePrim("/Root", "Xform");
    r.meta().references.push_back("@base.usd@</Base>");
    root.add_prim(std::move(r));
  }
  root.finalize();

  Compositor comp;
  comp.SetLayerLoader(
      [](const std::string& path, std::string* err) -> std::unique_ptr<Layer> {
        (void)path;
        (void)err;
        auto l = std::make_unique<Layer>();
        PrimSpec base = MakePrim("/Base", "Xform");
        base.add_property("imported", Value(int32_t(42)));
        l->add_prim(std::move(base));
        PrimSpec geo = MakePrim("/Base/Geo", "Mesh");
        geo.add_property("n", Value(int32_t(3)));
        l->add_prim(std::move(geo));
        l->finalize();  // external layers must be finalized for prim_at_path
        return l;
      });

  auto out = comp.Compose(root);
  CHECK(out != nullptr, "compose succeeds");
  CHECK(PropOf(*out, "/Root", "imported") != nullptr,
        "external opinion copied onto /Root");
  CHECK(out->prim_at_path("/Root/Geo") != nullptr,
        "external subtree grafted under /Root");
  CHECK(PropOf(*out, "/Root/Geo", "n") != nullptr,
        "grafted external child carries properties");
}

// Specializes is the weakest arc: only fills what is still missing.
static void test_specializes() {
  std::cout << "[specializes]\n";
  Layer layer;
  {
    PrimSpec base = MakePrim("/_base", "Material");
    base.add_property("roughness", Value(0.5));
    base.add_property("metallic", Value(0.0));
    layer.add_prim(std::move(base));
  }
  {
    PrimSpec mat = MakePrim("/Mat", "Material");
    mat.add_property("roughness", Value(0.9));  // local
    mat.meta().specializes.push_back("/_base");
    layer.add_prim(std::move(mat));
  }
  layer.finalize();

  Compositor comp;
  auto out = comp.Compose(layer);
  const Value* rough = PropOf(*out, "/Mat", "roughness");
  CHECK(rough && rough->as_double() && *rough->as_double() == 0.9,
        "local roughness wins over specialized (0.9)");
  CHECK(PropOf(*out, "/Mat", "metallic") != nullptr,
        "metallic filled from specialize");
}

// Cyclic internal references must terminate.
static void test_cycle_safety() {
  std::cout << "[cycle safety]\n";
  Layer layer;
  {
    PrimSpec a = MakePrim("/A", "Xform");
    a.add_property("a", Value(int32_t(1)));
    a.meta().references.push_back("</B>");
    layer.add_prim(std::move(a));
  }
  {
    PrimSpec b = MakePrim("/B", "Xform");
    b.add_property("b", Value(int32_t(2)));
    b.meta().references.push_back("</A>");
    layer.add_prim(std::move(b));
  }
  layer.finalize();

  Compositor comp;
  auto out = comp.Compose(layer);  // must not infinite-loop
  CHECK(out != nullptr, "compose terminates on cyclic references");
  const PrimSpec* a = out->prim_at_path("/A");
  CHECK(a && a->meta().references.empty(), "cycle: arcs cleared");
}

// Transitive internal reference: P -> Q -> R; P must end up with R's opinion.
static void test_transitive_reference() {
  std::cout << "[transitive reference]\n";
  Layer layer;
  {
    PrimSpec r = MakePrim("/R", "Xform");
    r.add_property("deep", Value(int32_t(99)));
    layer.add_prim(std::move(r));
  }
  {
    PrimSpec q = MakePrim("/Q", "Xform");
    q.meta().references.push_back("</R>");
    layer.add_prim(std::move(q));
  }
  {
    PrimSpec p = MakePrim("/P", "Xform");
    p.meta().references.push_back("</Q>");
    layer.add_prim(std::move(p));
  }
  layer.finalize();

  Compositor comp;
  auto out = comp.Compose(layer);
  CHECK(PropOf(*out, "/P", "deep") != nullptr,
        "transitive opinion R->Q->P reached /P");
}

// Variant selection applies the chosen variant's opinions; a property authored
// locally (outside the variant) stays strongest; variant arcs are cleared.
static void test_variants() {
  std::cout << "[variants]\n";
  Layer layer;
  {
    PrimSpec shape = MakePrim("/Shape", "Sphere");
    shape.add_property("radius", Value(1.0));  // local opinion (strongest)

    VariantSetData vsd;
    vsd.name = "shapeVariant";
    VariantData big;
    big.name = "big";
    big.properties.emplace_back("radius", Value(10.0));            // weaker than local
    big.properties.emplace_back("color", Value(std::string("red")));  // variant-only
    vsd.variants.push_back(std::move(big));
    VariantData small;
    small.name = "small";
    small.properties.emplace_back("radius", Value(0.5));
    vsd.variants.push_back(std::move(small));
    shape.meta().variantSets().push_back(std::move(vsd));
    shape.meta().variantSelection = "shapeVariant=big";
    layer.add_prim(std::move(shape));
  }
  layer.finalize();

  Compositor comp;
  auto out = comp.Compose(layer);
  const Value* r = PropOf(*out, "/Shape", "radius");
  CHECK(r && r->as_double() && *r->as_double() == 1.0,
        "local radius (1.0) wins over variant radius (10.0)");
  CHECK(PropOf(*out, "/Shape", "color") != nullptr,
        "variant-only 'color' applied from selected variant 'big'");
  const PrimSpec* shape = out->prim_at_path("/Shape");
  CHECK(shape && shape->meta().variantSets().empty() &&
            shape->meta().variantSelection.empty(),
        "variant set/selection cleared after flatten");
}

// Multiple variant sets selected on one prim, each applied (via vs.selected).
static void test_variants_multiset() {
  std::cout << "[variants: multiple sets]\n";
  Layer layer;
  {
    PrimSpec p = MakePrim("/M", "Mesh");
    VariantSetData lod;
    lod.name = "lod"; lod.selected = "high";
    { VariantData v; v.name = "high"; v.properties.emplace_back("subdiv", Value(int32_t(2))); lod.variants.push_back(std::move(v)); }
    { VariantData v; v.name = "low";  v.properties.emplace_back("subdiv", Value(int32_t(0))); lod.variants.push_back(std::move(v)); }
    p.meta().variantSets().push_back(std::move(lod));
    VariantSetData shade;
    shade.name = "shading"; shade.selected = "red";
    { VariantData v; v.name = "red"; v.properties.emplace_back("tint", Value(std::string("r"))); shade.variants.push_back(std::move(v)); }
    p.meta().variantSets().push_back(std::move(shade));
    layer.add_prim(std::move(p));
  }
  layer.finalize();

  Compositor comp;
  auto out = comp.Compose(layer);
  const Value* sd = PropOf(*out, "/M", "subdiv");
  CHECK(sd && sd->as_int() && *sd->as_int() == 2,
        "set 'lod'=high applied (subdiv=2)");
  CHECK(PropOf(*out, "/M", "tint") != nullptr,
        "set 'shading'=red applied (tint) — both sets selected");
}

// A variant that adds CHILD PRIMS: the selected variant's sub-prims are grafted
// under the owning prim; the unselected variant's sub-prims are not.
static void test_variant_subprim() {
  std::cout << "[variants: sub-prim graft]\n";
  Layer layer;
  {
    PrimSpec root = MakePrim("/Root", "Xform");
    VariantSetData vsd;
    vsd.name = "geo";
    vsd.selected = "hi";  // no property variants — only child prims
    root.meta().variantSets().push_back(std::move(vsd));
    layer.add_prim(std::move(root));
  }
  {  // selected variant's child (+ a grandchild) live under the holder path
    PrimSpec body = MakePrim("/Root/{geo=hi}/Body", "Scope");
    body.add_property("val", Value(int32_t(4)));
    layer.add_prim(std::move(body));
  }
  {
    PrimSpec sub = MakePrim("/Root/{geo=hi}/Body/Sub", "Scope");
    layer.add_prim(std::move(sub));
  }
  {  // unselected variant's child
    PrimSpec other = MakePrim("/Root/{geo=lo}/Other", "Scope");
    layer.add_prim(std::move(other));
  }
  layer.finalize();

  Compositor comp;
  auto out = comp.Compose(layer);
  CHECK(out->prim_at_path("/Root/Body") != nullptr,
        "selected variant's child prim grafted under owner");
  CHECK(PropOf(*out, "/Root/Body", "val") != nullptr,
        "grafted child carries its property");
  CHECK(out->prim_at_path("/Root/Body/Sub") != nullptr,
        "grafted child's grandchild grafted too (full subtree)");
  CHECK(out->prim_at_path("/Root/Other") == nullptr,
        "unselected variant's child prim NOT grafted");
}

// Unflattened variant round-trip: write a layer with variant holders/children
// WITHOUT composing, read it back, and confirm the variant content survives
// (and still composes correctly).
static void test_variant_roundtrip() {
  std::cout << "[variants: unflattened round-trip]\n";
  Layer layer;
  {
    PrimSpec root = MakePrim("/Root", "Xform");
    VariantSetData vsd;
    vsd.name = "geo";
    vsd.selected = "hi";
    root.meta().variantSets().push_back(std::move(vsd));
    layer.add_prim(std::move(root));
  }
  {  // variant holder + child prim (a normal layer prim at the bracketed path)
    PrimSpec holder = MakePrim("/Root/{geo=hi}", "Xform");
    layer.add_prim(std::move(holder));
  }
  {
    PrimSpec body = MakePrim("/Root/{geo=hi}/Body", "Scope");
    body.add_property("val", Value(int32_t(4)));
    layer.add_prim(std::move(body));
  }
  layer.finalize();

  // Write WITHOUT composing, then read back.
  CrateWriter writer;
  std::vector<uint8_t> buf;
  CHECK(writer.WriteLayerToMemory(buf, layer).success,
        "unflattened write succeeds");
  CrateReadOptions ro;
  ro.lazy_arrays = true;
  CrateReader reader(ro);
  CrateReadResult rr = reader.Read(buf.data(), buf.size());
  CHECK(rr.success, "re-read succeeds");
  const Layer* l2 = rr.stage.GetRootLayer();
  const PrimSpec* root2 = l2 ? l2->prim_at_path("/Root") : nullptr;
  CHECK(root2 && !root2->meta().variantSets().empty(),
        "variant set + selection survived the unflattened write");
  CHECK(l2 && l2->prim_at_path("/Root/{geo=hi}/Body") != nullptr,
        "variant child prim survived in the layer");

  // Composing the re-read layer must still bake the selected variant.
  Compositor comp;
  auto composed = comp.Compose(*l2);
  CHECK(composed->prim_at_path("/Root/Body") != nullptr,
        "re-read variant composes: child grafted under owner");
  CHECK(PropOf(*composed, "/Root/Body", "val") != nullptr,
        "grafted child retains its property after round-trip");
}

// A modelingVariant set with two options ("ChairA"/"ChairB"); each option
// carries a distinguishing "which" property only when `populated`.
static VariantSetData MakeModelingVariantSet(bool populated) {
  VariantSetData vsd;
  vsd.name = "modelingVariant";
  VariantData a; a.name = "ChairA";
  VariantData b; b.name = "ChairB";
  if (populated) {
    a.properties.emplace_back("which", Value(std::string("I_am_ChairA")));
    b.properties.emplace_back("which", Value(std::string("I_am_ChairB")));
  }
  vsd.variants.push_back(std::move(a));
  vsd.variants.push_back(std::move(b));
  return vsd;
}

// Regression (Pixar Kitchen_set Chair.usd): a variant selection authored on the
// REFERENCING prim must win over the referenced asset's OWN default selection.
//   /C1 references @geom@</Chair>, selects non-default "ChairB"
//   geom/Chair: populated variantSet + its own (weaker) default "ChairA"
// The main backend (examples/tusdcat) regressed on the larger Kitchen_set chain;
// the next backend composes all arcs per-prim before ApplyVariants, so the local
// selection is still present when the variant is baked. Confirms that here.
static void test_variant_selection_over_reference() {
  std::cout << "[variants: local selection wins over referenced default]\n";
  Layer root;
  {
    PrimSpec c1 = MakePrim("/C1", "Xform");
    c1.meta().references.push_back("@geom@</Chair>");
    c1.meta().variantSelection = "modelingVariant=ChairB";  // strong, non-default
    root.add_prim(std::move(c1));
  }
  root.finalize();

  Compositor comp;
  comp.SetLayerLoader(
      [](const std::string&, std::string*) -> std::unique_ptr<Layer> {
        // Referenced asset: populated variantSet + its own default "ChairA".
        auto l = std::make_unique<Layer>();
        PrimSpec chair = MakePrim("/Chair", "Xform");
        chair.meta().variantSelection = "modelingVariant=ChairA";
        chair.meta().variantSets().push_back(
            MakeModelingVariantSet(/*populated=*/true));
        l->add_prim(std::move(chair));
        l->finalize();
        return l;
      });

  auto out = comp.Compose(root);
  CHECK(out != nullptr, "compose succeeds");
  const Value* which = out ? PropOf(*out, "/C1", "which") : nullptr;
  CHECK(which != nullptr,
        "/C1.which present (selected variant's opinion applied to host)");
  if (which) {
    const std::string* s = which->as_string();
    CHECK(s && *s == "I_am_ChairB",
          std::string("local 'ChairB' wins over referenced default 'ChairA' "
                      "(got '") +
              (s ? *s : std::string("<non-string>")) + "')");
  }
}

// Regression: the next backend must resolve the arcs authored on an
// EXTERNALLY-referenced target (its payload / nested references), not just the
// arcs on the prim itself. The exact Pixar Kitchen_set Chair.usd shape:
//   /C1 references @asset@</Chair>, selects non-default "ChairB"
//   asset/Chair: EMPTY variant blocks + payload -> @payload@ + own default "ChairA"
//   payload/Chair: references @geom@</Chair>
//   geom/Chair: POPULATED variantSet + own (weakest) default "ChairA"
// GetComposedExternalLayer() composes each referenced layer's own arcs (variants
// deferred) before its opinions are merged into the host, and CopyLocalOpinions
// merges variantSet CONTENT per-variant so the deep populated blocks fill the
// asset's empty ones. The host then bakes its own "ChairB" selection.
static void test_variant_ref_payload_chain() {
  std::cout << "[variants: selection across ref->payload->ref chain]\n";
  Layer root;
  {
    PrimSpec c1 = MakePrim("/C1", "Xform");
    c1.meta().references.push_back("@asset@</Chair>");
    c1.meta().variantSelection = "modelingVariant=ChairB";
    root.add_prim(std::move(c1));
  }
  root.finalize();

  Compositor comp;
  comp.SetLayerLoader(
      [](const std::string& path, std::string*) -> std::unique_ptr<Layer> {
        auto l = std::make_unique<Layer>();
        if (path.find("asset") != std::string::npos) {
          PrimSpec chair = MakePrim("/Chair", "Xform");
          chair.meta().payloads.push_back("@payload@</Chair>");
          chair.meta().variantSelection = "modelingVariant=ChairA";
          chair.meta().variantSets().push_back(
              MakeModelingVariantSet(/*populated=*/false));
          l->add_prim(std::move(chair));
        } else if (path.find("payload") != std::string::npos) {
          PrimSpec chair = MakePrim("/Chair", "Xform");
          chair.meta().references.push_back("@geom@</Chair>");
          l->add_prim(std::move(chair));
        } else {
          PrimSpec chair = MakePrim("/Chair", "Xform");
          chair.meta().variantSelection = "modelingVariant=ChairA";
          chair.meta().variantSets().push_back(
              MakeModelingVariantSet(/*populated=*/true));
          l->add_prim(std::move(chair));
        }
        l->finalize();
        return l;
      });

  auto out = comp.Compose(root);
  CHECK(out != nullptr, "compose succeeds");
  const Value* which = out ? PropOf(*out, "/C1", "which") : nullptr;
  CHECK(which != nullptr,
        "/C1.which present (deep populated variant content reached the host "
        "through the ref->payload->ref chain)");
  if (which) {
    const std::string* s = which->as_string();
    CHECK(s && *s == "I_am_ChairB",
          std::string("local 'ChairB' wins across the full ref+payload chain "
                      "(got '") +
              (s ? *s : std::string("<non-string>")) + "', want 'I_am_ChairB')");
  }
}

// Subtree-scoped external composition: a SELF-CONTAINED arc-bearing prim of a
// multi-prim library composes alone (only its subtree is materialized; the
// library's other prims are not needed) and yields the same result as composing
// it inside the whole layer. AssetA's only arc is an EXTERNAL reference, so it
// is self-contained; the library also holds an unrelated /Base and a
// NON-self-contained /AssetC to ensure they are neither needed nor leaked.
static void test_extref_self_contained_subtree() {
  std::cout << "[extref: self-contained subtree composed alone]\n";
  Layer root;
  {
    PrimSpec r = MakePrim("/R", "Xform");
    r.meta().references.push_back("@lib@</AssetA>");
    root.add_prim(std::move(r));
  }
  root.finalize();

  Compositor comp;
  comp.SetLayerLoader(
      [](const std::string& path, std::string*) -> std::unique_ptr<Layer> {
        auto l = std::make_unique<Layer>();
        if (path.find("shader") != std::string::npos) {
          PrimSpec s = MakePrim("/S", "Shader");
          s.add_property("mat", Value(std::string("red")));
          l->add_prim(std::move(s));
        } else {
          PrimSpec a = MakePrim("/AssetA", "Xform");
          a.meta().references.push_back("@shader@</S>");  // external → self-contained
          l->add_prim(std::move(a));
          PrimSpec ag = MakePrim("/AssetA/Geom", "Mesh");  // a descendant to graft
          ag.add_property("n", Value(int32_t(5)));
          l->add_prim(std::move(ag));
          PrimSpec base = MakePrim("/Base", "Xform");
          base.add_property("base", Value(int32_t(7)));
          l->add_prim(std::move(base));
          PrimSpec c = MakePrim("/AssetC", "Xform");
          c.meta().references.push_back("</Base>");  // internal → NOT self-contained
          l->add_prim(std::move(c));
        }
        l->finalize();
        return l;
      });

  auto out = comp.Compose(root);
  CHECK(out != nullptr, "compose succeeds");
  const Value* mat = out ? PropOf(*out, "/R", "mat") : nullptr;
  CHECK(mat && mat->as_string() && *mat->as_string() == "red",
        "self-contained AssetA's external ref resolved via subtree compose");
  CHECK(out && out->prim_at_path("/R/Geom") != nullptr,
        "AssetA's descendant grafted from the extracted subtree");
  CHECK(PropOf(*out, "/R/Geom", "n") != nullptr,
        "grafted descendant carries its property");
  CHECK(out && out->prim_at_path("/R/Base") == nullptr,
        "unrelated library sibling not leaked into the host");
}

// Fallback: a referenced prim whose INTERNAL reference escapes its subtree is
// NOT self-contained and must compose against the whole layer so the sibling
// target resolves.
static void test_extref_non_self_contained_fallback() {
  std::cout << "[extref: non-self-contained subtree falls back to whole layer]\n";
  Layer root;
  {
    PrimSpec r = MakePrim("/R", "Xform");
    r.meta().references.push_back("@lib@</AssetC>");
    root.add_prim(std::move(r));
  }
  root.finalize();

  Compositor comp;
  comp.SetLayerLoader(
      [](const std::string&, std::string*) -> std::unique_ptr<Layer> {
        auto l = std::make_unique<Layer>();
        PrimSpec base = MakePrim("/Base", "Xform");
        base.add_property("base", Value(int32_t(7)));
        l->add_prim(std::move(base));
        PrimSpec c = MakePrim("/AssetC", "Xform");
        c.meta().references.push_back("</Base>");  // internal ref to a sibling
        l->add_prim(std::move(c));
        l->finalize();
        return l;
      });

  auto out = comp.Compose(root);
  CHECK(out != nullptr, "compose succeeds");
  const Value* base = out ? PropOf(*out, "/R", "base") : nullptr;
  CHECK(base && base->as_int() && *base->as_int() == 7,
        "AssetC's internal ref to sibling /Base resolved via whole-layer compose");
}

// The crate (USDC) reader records a variant selection as BOTH a per-set
// VariantSetData.selected AND the legacy variantSelection string. A host that
// overrides a referenced asset's variant must win regardless of which form it
// carries; the asset's (weaker) per-set `selected` must not ride along the
// CopyLocalOpinions variantSet merge and defeat the override in ApplyVariants
// (which prefers vs.selected over the legacy string).
static void test_variant_selected_field_over_reference() {
  std::cout << "[variants: host selection wins over referenced vs.selected]\n";

  // A modelingVariant set carrying its own per-set `selected` (reader form).
  auto makeSet = [](const std::string& sel) {
    VariantSetData vsd;
    vsd.name = "modelingVariant";
    vsd.selected = sel;
    VariantData a; a.name = "ChairA";
    a.properties.emplace_back("which", Value(std::string("I_am_ChairA")));
    VariantData b; b.name = "ChairB";
    b.properties.emplace_back("which", Value(std::string("I_am_ChairB")));
    vsd.variants.push_back(std::move(a));
    vsd.variants.push_back(std::move(b));
    return vsd;
  };
  // Referenced asset always selects its OWN default "ChairA" via vs.selected.
  auto loader = [&](const std::string&, std::string*) -> std::unique_ptr<Layer> {
    auto l = std::make_unique<Layer>();
    PrimSpec ch = MakePrim("/Chair", "Xform");
    ch.meta().variantSets().push_back(makeSet("ChairA"));
    ch.meta().variantSelection = "modelingVariant=ChairA";
    l->add_prim(std::move(ch));
    l->finalize();
    return l;
  };
  auto whichOf = [](const Layer* out, const std::string& prim) {
    const Value* w = out ? PropOf(*out, prim, "which") : nullptr;
    return (w && w->as_string()) ? *w->as_string() : std::string("<none>");
  };

  // (1) reader-consistent host: BOTH per-set selected AND the string = "ChairB".
  {
    Layer root;
    PrimSpec c = MakePrim("/H1", "Xform");
    c.meta().references.push_back("@asset@</Chair>");
    c.meta().variantSets().push_back(makeSet("ChairB"));
    c.meta().variantSelection = "modelingVariant=ChairB";
    root.add_prim(std::move(c));
    root.finalize();
    Compositor comp; comp.SetLayerLoader(loader);
    CHECK(whichOf(comp.Compose(root).get(), "/H1") == "I_am_ChairB",
          "host vs.selected 'ChairB' wins over referenced vs.selected 'ChairA'");
  }

  // (2) string-only host: the asset's per-set `selected` must NOT defeat the
  // host's legacy variantSelection (the hardened merge suppresses it).
  {
    Layer root;
    PrimSpec c = MakePrim("/H2", "Xform");
    c.meta().references.push_back("@asset@</Chair>");
    c.meta().variantSelection = "modelingVariant=ChairB";  // string only
    root.add_prim(std::move(c));
    root.finalize();
    Compositor comp; comp.SetLayerLoader(loader);
    CHECK(whichOf(comp.Compose(root).get(), "/H2") == "I_am_ChairB",
          "host variantSelection string 'ChairB' wins over referenced vs.selected");
  }

  // (3) control: host does NOT override → the asset's own default ChairA stands
  // (the hardening must not suppress a referenced selection the host didn't override).
  {
    Layer root;
    PrimSpec c = MakePrim("/H3", "Xform");
    c.meta().references.push_back("@asset@</Chair>");
    root.add_prim(std::move(c));
    root.finalize();
    Compositor comp; comp.SetLayerLoader(loader);
    CHECK(whichOf(comp.Compose(root).get(), "/H3") == "I_am_ChairA",
          "no host override → referenced default 'ChairA' applies");
  }
}

int main() {
  test_inherits();
  test_internal_reference();
  test_external_reference();
  test_specializes();
  test_cycle_safety();
  test_transitive_reference();
  test_variants();
  test_variants_multiset();
  test_variant_subprim();
  test_variant_roundtrip();
  test_variant_selection_over_reference();
  test_variant_ref_payload_chain();
  test_extref_self_contained_subtree();
  test_extref_non_self_contained_fallback();
  test_variant_selected_field_over_reference();

  if (g_fail) {
    std::cerr << "\n" << g_fail << " composition check(s) FAILED\n";
    return 1;
  }
  std::cout << "\nAll composition checks passed.\n";
  return 0;
}
