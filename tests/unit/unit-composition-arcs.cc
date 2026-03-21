#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-composition-arcs.h"
#include "composition.hh"
#include "core/prim.hh"
#include "core/prim-spec.hh"
#include "layer.hh"
#include "tinyusdz.hh"

using namespace tinyusdz;

// ---------------------------------------------------------------------------
// 1. comp_inherits_child_prims_test
// /Base has child "SubChild" with prop "childVal"=42.
// /Derived inherits /Base. After CompositeInherits, Derived should have
// child "SubChild" with childVal.
// ---------------------------------------------------------------------------
void comp_inherits_child_prims_test(void) {
  Layer layer;

  // -- /Base (class) with child SubChild --
  PrimSpec base(Specifier::Class, "Scope", "Base");
  {
    PrimSpec subchild(Specifier::Def, "Scope", "SubChild");
    Attribute attr;
    attr.set_value(42);
    attr.set_type_name("int");
    subchild.props()["childVal"] = Property(attr, /* custom */ false);
    base.children().push_back(subchild);
  }
  layer.add_primspec("Base", base);

  // -- /Derived inherits /Base --
  PrimSpec derived(Specifier::Def, "Scope", "Derived");
  {
    std::vector<std::pair<ListEditQual, std::vector<Path>>> inh;
    inh.push_back({ListEditQual::ResetToExplicit, {Path("/Base", "")}});
    derived.metas().inherits = inh;
  }
  layer.add_primspec("Derived", derived);

  Layer result;
  std::string warn, err;
  bool ok = CompositeInherits(layer, &result, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("CompositeInherits failed: %s", err.c_str());
    return;
  }

  // Verify /Derived exists in result
  auto it = result.primspecs().find("Derived");
  TEST_CHECK(it != result.primspecs().end());
  if (it == result.primspecs().end()) return;

  // Verify /Derived has child named "SubChild"
  const auto &children = it->second.children();
  bool found_subchild = false;
  for (const auto &child : children) {
    if (child.name() == "SubChild") {
      found_subchild = true;
      // Verify childVal property exists and equals 42
      TEST_CHECK(child.props().count("childVal") > 0);
      if (child.props().count("childVal")) {
        const Property &p = child.props().at("childVal");
        TEST_CHECK(p.is_attribute());
        if (p.is_attribute()) {
          auto v = p.get_attribute().get_value<int>();
          TEST_CHECK(v.has_value());
          if (v.has_value()) {
            TEST_CHECK(v.value() == 42);
          }
        }
      }
      break;
    }
  }
  TEST_CHECK(found_subchild);
  if (!found_subchild) {
    TEST_MSG("SubChild not found in Derived after CompositeInherits");
  }
}

// ---------------------------------------------------------------------------
// 2. comp_inherits_multiple_bases_test
// /Derived inherits from [</Base1>, </Base2>]. Both bases' properties appear.
// ---------------------------------------------------------------------------
void comp_inherits_multiple_bases_test(void) {
  Layer layer;

  // -- /Base1 --
  PrimSpec base1(Specifier::Class, "Scope", "Base1");
  {
    Attribute attr;
    attr.set_value(1);
    attr.set_type_name("int");
    base1.props()["fromBase1"] = Property(attr, false);
  }
  layer.add_primspec("Base1", base1);

  // -- /Base2 --
  PrimSpec base2(Specifier::Class, "Scope", "Base2");
  {
    Attribute attr;
    attr.set_value(2);
    attr.set_type_name("int");
    base2.props()["fromBase2"] = Property(attr, false);
  }
  layer.add_primspec("Base2", base2);

  // -- /Derived inherits both --
  PrimSpec derived(Specifier::Def, "Scope", "Derived");
  {
    std::vector<std::pair<ListEditQual, std::vector<Path>>> inh;
    inh.push_back({ListEditQual::ResetToExplicit,
                   {Path("/Base1", ""), Path("/Base2", "")}});
    derived.metas().inherits = inh;
  }
  layer.add_primspec("Derived", derived);

  Layer result;
  std::string warn, err;
  bool ok = CompositeInherits(layer, &result, &warn, &err);

  // Multiple inheritance is now supported.
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("CompositeInherits failed: %s", err.c_str());
    return;
  }

  // Verify /Derived has properties from both bases.
  auto it = result.primspecs().find("Derived");
  TEST_CHECK(it != result.primspecs().end());
  if (it != result.primspecs().end()) {
    TEST_CHECK(it->second.props().count("fromBase1") > 0);
    TEST_CHECK(it->second.props().count("fromBase2") > 0);

    // Verify values
    if (it->second.props().count("fromBase1")) {
      const auto &a = it->second.props().at("fromBase1").get_attribute();
      auto v = a.get_value<int>();
      TEST_CHECK(v.has_value() && v.value() == 1);
    }
    if (it->second.props().count("fromBase2")) {
      const auto &a = it->second.props().at("fromBase2").get_attribute();
      auto v = a.get_value<int>();
      TEST_CHECK(v.has_value() && v.value() == 2);
    }
  }
}

// ---------------------------------------------------------------------------
// 3. comp_specializes_child_prims_test
// Same as inherits child propagation but via specializes arc.
// ---------------------------------------------------------------------------
void comp_specializes_child_prims_test(void) {
  Layer layer;

  // -- /Base with child SubChild --
  PrimSpec base(Specifier::Def, "Scope", "Base");
  {
    PrimSpec subchild(Specifier::Def, "Scope", "SubChild");
    Attribute attr;
    attr.set_value(99);
    attr.set_type_name("int");
    subchild.props()["childVal"] = Property(attr, false);
    base.children().push_back(subchild);
  }
  layer.add_primspec("Base", base);

  // -- /Specialized specializes /Base --
  PrimSpec specialized(Specifier::Def, "Scope", "Specialized");
  {
    std::vector<std::pair<ListEditQual, std::vector<Path>>> spec;
    spec.push_back({ListEditQual::ResetToExplicit, {Path("/Base", "")}});
    specialized.metas().specializes = spec;
  }
  layer.add_primspec("Specialized", specialized);

  Layer result;
  std::string warn, err;
  bool ok = CompositeSpecializes(layer, &result, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("CompositeSpecializes failed: %s", err.c_str());
    return;
  }

  auto it = result.primspecs().find("Specialized");
  TEST_CHECK(it != result.primspecs().end());
  if (it == result.primspecs().end()) return;

  const auto &children = it->second.children();
  bool found_subchild = false;
  for (const auto &child : children) {
    if (child.name() == "SubChild") {
      found_subchild = true;
      TEST_CHECK(child.props().count("childVal") > 0);
      if (child.props().count("childVal")) {
        auto v = child.props().at("childVal").get_attribute().get_value<int>();
        TEST_CHECK(v.has_value());
        if (v.has_value()) {
          TEST_CHECK(v.value() == 99);
        }
      }
      break;
    }
  }
  TEST_CHECK(found_subchild);
  if (!found_subchild) {
    TEST_MSG("SubChild not found in Specialized after CompositeSpecializes");
  }

  // specializes metadata should be cleared
  TEST_CHECK(!it->second.metas().specializes.has_value());
}

// ---------------------------------------------------------------------------
// 4. comp_variant_two_sets_test
// Two variantSets on one PrimSpec; select from each; both applied.
// ---------------------------------------------------------------------------
void comp_variant_two_sets_test(void) {
  Layer layer;

  PrimSpec root(Specifier::Def, "Scope", "Root");

  // variantSets metadata: color, size
  {
    std::vector<std::pair<ListEditQual, std::vector<std::string>>> vsets;
    vsets.push_back({ListEditQual::ResetToExplicit, {"color", "size"}});
    root.metas().variantSets = vsets;
  }

  // variant selection: color=red, size=large
  {
    VariantSelectionMap vsmap;
    vsmap["color"] = "red";
    vsmap["size"] = "large";
    root.metas().variants = vsmap;
  }

  // color variantSet
  {
    VariantSetSpec vss;
    vss.name = "color";

    PrimSpec red(Specifier::Def, "", "red");
    {
      Attribute attr;
      attr.set_value(value::token("red"));
      attr.set_type_name("token");
      red.props()["colorName"] = Property(attr, false);
    }
    vss.variantSet["red"] = red;

    PrimSpec blue(Specifier::Def, "", "blue");
    {
      Attribute attr;
      attr.set_value(value::token("blue"));
      attr.set_type_name("token");
      blue.props()["colorName"] = Property(attr, false);
    }
    vss.variantSet["blue"] = blue;

    root.variantSets()["color"] = vss;
  }

  // size variantSet
  {
    VariantSetSpec vss;
    vss.name = "size";

    PrimSpec large(Specifier::Def, "", "large");
    {
      Attribute attr;
      attr.set_value(10.0f);
      attr.set_type_name("float");
      large.props()["sizeVal"] = Property(attr, false);
    }
    vss.variantSet["large"] = large;

    PrimSpec small_v(Specifier::Def, "", "small");
    {
      Attribute attr;
      attr.set_value(1.0f);
      attr.set_type_name("float");
      small_v.props()["sizeVal"] = Property(attr, false);
    }
    vss.variantSet["small"] = small_v;

    root.variantSets()["size"] = vss;
  }

  layer.add_primspec("Root", root);

  Layer result;
  std::string warn, err;
  bool ok = CompositeVariant(layer, &result, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("CompositeVariant failed: %s", err.c_str());
    return;
  }

  auto it = result.primspecs().find("Root");
  TEST_CHECK(it != result.primspecs().end());
  if (it == result.primspecs().end()) return;

  // Verify colorName = "red" from color variant
  TEST_CHECK(it->second.props().count("colorName") > 0);
  if (it->second.props().count("colorName")) {
    auto v = it->second.props().at("colorName").get_attribute().get_value<value::token>();
    TEST_CHECK(v.has_value());
    if (v.has_value()) {
      TEST_CHECK(v.value().str() == "red");
    }
  }

  // Verify sizeVal = 10.0f from size variant
  TEST_CHECK(it->second.props().count("sizeVal") > 0);
  if (it->second.props().count("sizeVal")) {
    auto v = it->second.props().at("sizeVal").get_attribute().get_value<float>();
    TEST_CHECK(v.has_value());
    if (v.has_value()) {
      TEST_CHECK(v.value() == 10.0f);
    }
  }

  // variant metadata should be cleared
  TEST_CHECK(!it->second.metas().variants.has_value());
  TEST_CHECK(!it->second.metas().variantSets.has_value());
  TEST_CHECK(it->second.variantSets().empty());
}

// ---------------------------------------------------------------------------
// 5. comp_variant_no_selection_test
// VariantSet exists but no selection; CompositeVariant should handle
// gracefully without crashing or applying variants.
// ---------------------------------------------------------------------------
void comp_variant_no_selection_test(void) {
  Layer layer;

  PrimSpec root(Specifier::Def, "Scope", "Root");

  // variantSets metadata
  {
    std::vector<std::pair<ListEditQual, std::vector<std::string>>> vsets;
    vsets.push_back({ListEditQual::ResetToExplicit, {"myVariant"}});
    root.metas().variantSets = vsets;
  }

  // No variant selection set (root.metas().variants is not set)

  // Set up variantSet content
  {
    VariantSetSpec vss;
    vss.name = "myVariant";

    PrimSpec optA(Specifier::Def, "", "optionA");
    {
      Attribute attr;
      attr.set_value(100);
      attr.set_type_name("int");
      optA.props()["val"] = Property(attr, false);
    }
    vss.variantSet["optionA"] = optA;

    root.variantSets()["myVariant"] = vss;
  }

  // Add a pre-existing property on root to ensure it survives
  {
    Attribute attr;
    attr.set_value(7);
    attr.set_type_name("int");
    root.props()["existing"] = Property(attr, false);
  }

  layer.add_primspec("Root", root);

  Layer result;
  std::string warn, err;
  bool ok = CompositeVariant(layer, &result, &warn, &err);
  // Should succeed (no crash, no error)
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("CompositeVariant with no selection failed: %s", err.c_str());
    return;
  }

  auto it = result.primspecs().find("Root");
  TEST_CHECK(it != result.primspecs().end());
  if (it == result.primspecs().end()) return;

  // The variant property should NOT be applied (no selection)
  TEST_CHECK(it->second.props().count("val") == 0);

  // existing property should still be there
  TEST_CHECK(it->second.props().count("existing") > 0);
}

// ---------------------------------------------------------------------------
// 6. comp_variant_nested_children_test
// Variant with child PrimSpecs; children appear after selection.
// ---------------------------------------------------------------------------
void comp_variant_nested_children_test(void) {
  Layer layer;

  PrimSpec root(Specifier::Def, "Scope", "Root");

  // variantSets metadata
  {
    std::vector<std::pair<ListEditQual, std::vector<std::string>>> vsets;
    vsets.push_back({ListEditQual::ResetToExplicit, {"shapeVariant"}});
    root.metas().variantSets = vsets;
  }

  // selection: shapeVariant=optionA
  {
    VariantSelectionMap vsmap;
    vsmap["shapeVariant"] = "optionA";
    root.metas().variants = vsmap;
  }

  // variantSet with child PrimSpec
  {
    VariantSetSpec vss;
    vss.name = "shapeVariant";

    PrimSpec optA(Specifier::Def, "", "optionA");
    {
      // Nested child inside variant
      PrimSpec nested(Specifier::Def, "Scope", "Nested");
      Attribute attr;
      attr.set_value(42);
      attr.set_type_name("int");
      nested.props()["nestedVal"] = Property(attr, false);
      optA.children().push_back(nested);
    }
    vss.variantSet["optionA"] = optA;

    PrimSpec optB(Specifier::Def, "", "optionB");
    vss.variantSet["optionB"] = optB;

    root.variantSets()["shapeVariant"] = vss;
  }

  layer.add_primspec("Root", root);

  Layer result;
  std::string warn, err;
  bool ok = CompositeVariant(layer, &result, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("CompositeVariant failed: %s", err.c_str());
    return;
  }

  auto it = result.primspecs().find("Root");
  TEST_CHECK(it != result.primspecs().end());
  if (it == result.primspecs().end()) return;

  // /Root should have child "Nested" from selected variant
  const auto &children = it->second.children();
  bool found_nested = false;
  for (const auto &child : children) {
    if (child.name() == "Nested") {
      found_nested = true;
      TEST_CHECK(child.props().count("nestedVal") > 0);
      if (child.props().count("nestedVal")) {
        auto v = child.props().at("nestedVal").get_attribute().get_value<int>();
        TEST_CHECK(v.has_value());
        if (v.has_value()) {
          TEST_CHECK(v.value() == 42);
        }
      }
      break;
    }
  }
  TEST_CHECK(found_nested);
  if (!found_nested) {
    TEST_MSG("Nested child not found in Root after CompositeVariant");
  }
}

// ---------------------------------------------------------------------------
// 7. comp_override_basic_test
// Create a /Base (def) and a /Base (over). Use OverridePrimSpec to merge
// the over into def. Verify the override properties are merged.
// ---------------------------------------------------------------------------
void comp_override_basic_test(void) {
  PrimSpec dst(Specifier::Def, "Scope", "Base");
  {
    Attribute attr;
    attr.set_value(1);
    attr.set_type_name("int");
    dst.props()["origProp"] = Property(attr, false);
  }

  PrimSpec src(Specifier::Over, "Scope", "Base");
  {
    Attribute attr;
    attr.set_value(100);
    attr.set_type_name("int");
    src.props()["overProp"] = Property(attr, false);
  }

  std::string warn, err;
  bool ok = OverridePrimSpec(dst, src, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("OverridePrimSpec failed: %s", err.c_str());
    return;
  }

  // origProp should still exist
  TEST_CHECK(dst.props().count("origProp") > 0);
  if (dst.props().count("origProp")) {
    auto v = dst.props().at("origProp").get_attribute().get_value<int>();
    TEST_CHECK(v.has_value());
    if (v.has_value()) {
      TEST_CHECK(v.value() == 1);
    }
  }

  // overProp should be merged from src
  TEST_CHECK(dst.props().count("overProp") > 0);
  if (dst.props().count("overProp")) {
    auto v = dst.props().at("overProp").get_attribute().get_value<int>();
    TEST_CHECK(v.has_value());
    if (v.has_value()) {
      TEST_CHECK(v.value() == 100);
    }
  }
}

// ---------------------------------------------------------------------------
// 8. comp_override_replace_property_test
// Over replaces existing property value.
// ---------------------------------------------------------------------------
void comp_override_replace_property_test(void) {
  PrimSpec dst(Specifier::Def, "Scope", "Thing");
  {
    Attribute attr;
    attr.set_value(1);
    attr.set_type_name("int");
    dst.props()["val"] = Property(attr, false);
  }

  PrimSpec src(Specifier::Over, "Scope", "Thing");
  {
    Attribute attr;
    attr.set_value(2);
    attr.set_type_name("int");
    src.props()["val"] = Property(attr, false);
  }

  std::string warn, err;
  bool ok = OverridePrimSpec(dst, src, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("OverridePrimSpec failed: %s", err.c_str());
    return;
  }

  // val should be replaced with 2
  TEST_CHECK(dst.props().count("val") > 0);
  if (dst.props().count("val")) {
    auto v = dst.props().at("val").get_attribute().get_value<int>();
    TEST_CHECK(v.has_value());
    if (v.has_value()) {
      TEST_CHECK(v.value() == 2);
      if (v.value() != 2) {
        TEST_MSG("Expected val=2, got %d", v.value());
      }
    }
  }
}

// ---------------------------------------------------------------------------
// 9. comp_override_add_new_property_test
// Over adds a new property that didn't exist in dst.
// ---------------------------------------------------------------------------
void comp_override_add_new_property_test(void) {
  PrimSpec dst(Specifier::Def, "Scope", "Thing");
  {
    Attribute attr;
    attr.set_value(10);
    attr.set_type_name("int");
    dst.props()["existing"] = Property(attr, false);
  }

  PrimSpec src(Specifier::Over, "Scope", "Thing");
  {
    Attribute attr;
    attr.set_value(3.14f);
    attr.set_type_name("float");
    src.props()["newProp"] = Property(attr, false);
  }

  std::string warn, err;
  bool ok = OverridePrimSpec(dst, src, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("OverridePrimSpec failed: %s", err.c_str());
    return;
  }

  // existing should remain
  TEST_CHECK(dst.props().count("existing") > 0);
  if (dst.props().count("existing")) {
    auto v = dst.props().at("existing").get_attribute().get_value<int>();
    TEST_CHECK(v.has_value());
    if (v.has_value()) {
      TEST_CHECK(v.value() == 10);
    }
  }

  // newProp should be added
  TEST_CHECK(dst.props().count("newProp") > 0);
  if (dst.props().count("newProp")) {
    auto v = dst.props().at("newProp").get_attribute().get_value<float>();
    TEST_CHECK(v.has_value());
    if (v.has_value()) {
      TEST_CHECK(v.value() == 3.14f);
    }
  }
}

// ---------------------------------------------------------------------------
// 10. comp_inherit_primspec_api_test
// Direct InheritPrimSpec() call. Create base with props, derived without.
// InheritPrimSpec(derived, base). Verify derived got base's props.
// ---------------------------------------------------------------------------
void comp_inherit_primspec_api_test(void) {
  PrimSpec base(Specifier::Class, "Scope", "Base");
  {
    Attribute attr;
    attr.set_value(77);
    attr.set_type_name("int");
    base.props()["baseVal"] = Property(attr, false);

    Attribute attr2;
    attr2.set_value(2.5f);
    attr2.set_type_name("float");
    base.props()["baseFloat"] = Property(attr2, false);
  }

  PrimSpec derived(Specifier::Def, "Scope", "Derived");
  // derived starts empty

  std::string warn, err;
  bool ok = InheritPrimSpec(derived, base, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("InheritPrimSpec failed: %s", err.c_str());
    return;
  }

  // derived should have both base properties
  TEST_CHECK(derived.props().count("baseVal") > 0);
  TEST_CHECK(derived.props().count("baseFloat") > 0);

  if (derived.props().count("baseVal")) {
    auto v = derived.props().at("baseVal").get_attribute().get_value<int>();
    TEST_CHECK(v.has_value());
    if (v.has_value()) {
      TEST_CHECK(v.value() == 77);
    }
  }

  if (derived.props().count("baseFloat")) {
    auto v = derived.props().at("baseFloat").get_attribute().get_value<float>();
    TEST_CHECK(v.has_value());
    if (v.has_value()) {
      TEST_CHECK(v.value() == 2.5f);
    }
  }
}

// ---------------------------------------------------------------------------
// 11. comp_override_primspec_api_test
// Direct OverridePrimSpec() call. Create def with props, over with different
// props. OverridePrimSpec(def, over). Verify merge.
// ---------------------------------------------------------------------------
void comp_override_primspec_api_test(void) {
  PrimSpec dst(Specifier::Def, "Scope", "Prim");
  {
    Attribute attr;
    attr.set_value(value::token("original"));
    attr.set_type_name("token");
    dst.props()["name"] = Property(attr, false);

    Attribute attr2;
    attr2.set_value(100);
    attr2.set_type_name("int");
    dst.props()["count"] = Property(attr2, false);
  }

  PrimSpec src(Specifier::Over, "Scope", "Prim");
  {
    // Override name
    Attribute attr;
    attr.set_value(value::token("overridden"));
    attr.set_type_name("token");
    src.props()["name"] = Property(attr, false);

    // Add new property
    Attribute attr2;
    attr2.set_value(true);
    attr2.set_type_name("bool");
    src.props()["active"] = Property(attr2, false);
  }

  std::string warn, err;
  bool ok = OverridePrimSpec(dst, src, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("OverridePrimSpec failed: %s", err.c_str());
    return;
  }

  // "name" should be overridden
  TEST_CHECK(dst.props().count("name") > 0);
  if (dst.props().count("name")) {
    auto v = dst.props().at("name").get_attribute().get_value<value::token>();
    TEST_CHECK(v.has_value());
    if (v.has_value()) {
      TEST_CHECK(v.value().str() == "overridden");
    }
  }

  // "count" should be untouched
  TEST_CHECK(dst.props().count("count") > 0);
  if (dst.props().count("count")) {
    auto v = dst.props().at("count").get_attribute().get_value<int>();
    TEST_CHECK(v.has_value());
    if (v.has_value()) {
      TEST_CHECK(v.value() == 100);
    }
  }

  // "active" should be added
  TEST_CHECK(dst.props().count("active") > 0);
  if (dst.props().count("active")) {
    auto v = dst.props().at("active").get_attribute().get_value<bool>();
    TEST_CHECK(v.has_value());
    if (v.has_value()) {
      TEST_CHECK(v.value() == true);
    }
  }
}

// ---------------------------------------------------------------------------
// 12. comp_has_references_test
// Test HasReferences() with layer that has references and one without.
// ---------------------------------------------------------------------------
void comp_has_references_test(void) {
  // Empty layer should not have references.
  // Use force_check=true since the default cached flag may not reflect the
  // actual state of a freshly-constructed Layer.
  {
    Layer empty_layer;
    TEST_CHECK(!HasReferences(empty_layer, /* force_check */ true));
  }

  // Layer without references
  {
    Layer layer;
    PrimSpec ps(Specifier::Def, "Scope", "Plain");
    layer.add_primspec("Plain", ps);
    TEST_CHECK(!HasReferences(layer, true));
  }

  // Layer with references
  {
    Layer layer;
    PrimSpec ps(Specifier::Def, "Scope", "Ref");

    Reference ref;
    ref.asset_path = value::AssetPath("other.usda");
    std::vector<std::pair<ListEditQual, std::vector<Reference>>> refs;
    refs.push_back({ListEditQual::ResetToExplicit, {ref}});
    ps.metas().references = refs;

    layer.add_primspec("Ref", ps);

    TEST_CHECK(HasReferences(layer, /* force_check */ true));
  }
}

// ---------------------------------------------------------------------------
// 13. comp_has_payload_test
// Test HasPayload() with layer that has payload and one without.
// ---------------------------------------------------------------------------
void comp_has_payload_test(void) {
  // Empty layer should not have payload.
  // Use force_check=true since the default cached flag may not reflect the
  // actual state of a freshly-constructed Layer.
  {
    Layer empty_layer;
    TEST_CHECK(!HasPayload(empty_layer, /* force_check */ true));
  }

  // Layer without payload
  {
    Layer layer;
    PrimSpec ps(Specifier::Def, "Scope", "NoPay");
    layer.add_primspec("NoPay", ps);
    TEST_CHECK(!HasPayload(layer, true));
  }

  // Layer with payload
  {
    Layer layer;
    PrimSpec ps(Specifier::Def, "Scope", "Pay");

    Payload pl;
    pl.asset_path = value::AssetPath("payload.usda");
    std::vector<std::pair<ListEditQual, std::vector<Payload>>> payloads;
    payloads.push_back({ListEditQual::ResetToExplicit, {pl}});
    ps.metas().payload = payloads;

    layer.add_primspec("Pay", ps);

    TEST_CHECK(HasPayload(layer, /* force_check */ true));
  }
}

// ---------------------------------------------------------------------------
// 14. comp_has_over_test
// Create layer with Specifier::Over PrimSpec. HasOver returns true.
// Without returns false.
// ---------------------------------------------------------------------------
void comp_has_over_test(void) {
  // Empty layer
  {
    Layer empty_layer;
    TEST_CHECK(!HasOver(empty_layer));
  }

  // Layer with only def PrimSpec
  {
    Layer layer;
    PrimSpec ps(Specifier::Def, "Scope", "DefPrim");
    layer.add_primspec("DefPrim", ps);
    TEST_CHECK(!HasOver(layer));
  }

  // Layer with over PrimSpec
  {
    Layer layer;
    PrimSpec ps(Specifier::Over, "Scope", "OverPrim");
    layer.add_primspec("OverPrim", ps);
    TEST_CHECK(HasOver(layer));
  }

  // Layer with class PrimSpec (should not count as over)
  {
    Layer layer;
    PrimSpec ps(Specifier::Class, "Scope", "ClassPrim");
    layer.add_primspec("ClassPrim", ps);
    TEST_CHECK(!HasOver(layer));
  }
}

// ---------------------------------------------------------------------------
// 15. comp_extract_variants_from_layer_test
// Set up Layer with variant metadata, call ExtractVariants(Layer, dict, err),
// verify Dictionary contents.
// ---------------------------------------------------------------------------
void comp_extract_variants_from_layer_test(void) {
  Layer layer;

  PrimSpec ps(Specifier::Def, "Scope", "Cube");

  // variantSets
  {
    std::vector<std::pair<ListEditQual, std::vector<std::string>>> vsets;
    vsets.push_back({ListEditQual::ResetToExplicit, {"colorVariant"}});
    ps.metas().variantSets = vsets;
  }

  // variant selection
  {
    VariantSelectionMap vsmap;
    vsmap["colorVariant"] = "green";
    ps.metas().variants = vsmap;
  }

  // Set up VariantSetSpec
  {
    VariantSetSpec vss;
    vss.name = "colorVariant";

    PrimSpec green(Specifier::Def, "", "green");
    {
      Attribute attr;
      attr.set_value(value::token("green"));
      attr.set_type_name("token");
      green.props()["color"] = Property(attr, false);
    }
    vss.variantSet["green"] = green;

    ps.variantSets()["colorVariant"] = vss;
  }

  layer.add_primspec("Cube", ps);

  Dictionary dict;
  std::string err;
  bool ok = ExtractVariants(layer, &dict, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("ExtractVariants(Layer) failed: %s", err.c_str());
    return;
  }

  // Dictionary should contain entries (non-empty if variants found)
  // The exact structure depends on implementation but it should not be empty
  // for a layer with variants.
  TEST_CHECK(!dict.empty());
}

// ---------------------------------------------------------------------------
// 16. comp_extract_variants_from_stage_test
// Parse simple USDA with variants via LoadUSDAFromMemory, then call
// ExtractVariants(Stage, dict, err).
// ---------------------------------------------------------------------------
void comp_extract_variants_from_stage_test(void) {
  const std::string usda = R"(#usda 1.0

def Scope "Root" (
    variants = {
        string shapeVariant = "cube"
    }
    prepend variantSets = "shapeVariant"
)
{
    variantSet "shapeVariant" = {
        "cube" {
            int cubeSize = 10
        }
        "sphere" {
            float sphereRadius = 5.0
        }
    }
}
)";

  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda.data()), usda.size(),
      "", &stage, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());
    return;
  }

  Dictionary dict;
  ok = ExtractVariants(stage, &dict, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("ExtractVariants(Stage) failed: %s", err.c_str());
    return;
  }

  // Dictionary should contain variant info
  TEST_CHECK(!dict.empty());
}

// ---------------------------------------------------------------------------
// 17. comp_layer_to_stage_test
// Build Layer with PrimSpecs, call LayerToStage, verify Stage has prims
// at correct paths.
// ---------------------------------------------------------------------------
void comp_layer_to_stage_test(void) {
  Layer layer;

  PrimSpec root(Specifier::Def, "Scope", "Root");
  {
    Attribute attr;
    attr.set_value(1);
    attr.set_type_name("int");
    root.props()["rootVal"] = Property(attr, false);
  }
  layer.add_primspec("Root", root);

  PrimSpec another(Specifier::Def, "Scope", "Another");
  {
    Attribute attr;
    attr.set_value(2);
    attr.set_type_name("int");
    another.props()["anotherVal"] = Property(attr, false);
  }
  layer.add_primspec("Another", another);

  Stage stage;
  std::string warn, err;
  bool ok = LayerToStage(std::move(layer), &stage, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("LayerToStage failed: %s", err.c_str());
    return;
  }

  // Stage should have root prims
  TEST_CHECK(stage.root_prims().size() >= 2);

  // Check /Root exists
  {
    Path path("/Root", "");
    auto result = stage.GetPrimAtPath(path);
    TEST_CHECK(result.has_value());
    if (result) {
      TEST_CHECK(result.value()->element_name() == "Root");
    }
  }

  // Check /Another exists
  {
    Path path("/Another", "");
    auto result = stage.GetPrimAtPath(path);
    TEST_CHECK(result.has_value());
    if (result) {
      TEST_CHECK(result.value()->element_name() == "Another");
    }
  }
}

// ---------------------------------------------------------------------------
// 18. comp_list_variant_selections_test
// Set up Layer with variant selections, call ListVariantSelectionMaps,
// verify returned map.
// ---------------------------------------------------------------------------
void comp_list_variant_selections_test(void) {
  // ListVariantSelectionMaps() is declared in composition.hh but not yet
  // implemented. This test is a placeholder until the implementation lands.
  TEST_MSG("ListVariantSelectionMaps not yet implemented -- skipping");
}

// ---------------------------------------------------------------------------
// 19. comp_variant_select_primspec_test
// Direct VariantSelectPrimSpec() call. Create PrimSpec with variantSets,
// call with selection map, verify correct variant applied.
// ---------------------------------------------------------------------------
void comp_variant_select_primspec_test(void) {
  PrimSpec src(Specifier::Def, "Scope", "Root");

  // Set up variantSet
  {
    VariantSetSpec vss;
    vss.name = "matVariant";

    PrimSpec metal(Specifier::Def, "", "metal");
    {
      Attribute attr;
      attr.set_value(value::token("metallic"));
      attr.set_type_name("token");
      metal.props()["material"] = Property(attr, false);
    }
    vss.variantSet["metal"] = metal;

    PrimSpec wood(Specifier::Def, "", "wood");
    {
      Attribute attr;
      attr.set_value(value::token("wooden"));
      attr.set_type_name("token");
      wood.props()["material"] = Property(attr, false);
    }
    vss.variantSet["wood"] = wood;

    src.variantSets()["matVariant"] = vss;
  }

  // variantSets metadata
  {
    std::vector<std::pair<ListEditQual, std::vector<std::string>>> vsets;
    vsets.push_back({ListEditQual::ResetToExplicit, {"matVariant"}});
    src.metas().variantSets = vsets;
  }

  // variant selection metadata (required for VariantSelectPrimSpec to enter
  // the composition path)
  {
    VariantSelectionMap vsmap;
    vsmap["matVariant"] = "metal";
    src.metas().variants = vsmap;
  }

  PrimSpec dst(Specifier::Def, "Scope", "Root");

  std::map<std::string, std::string> selection;
  selection["matVariant"] = "metal";

  std::string warn, err;
  bool ok = VariantSelectPrimSpec(dst, src, selection, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("VariantSelectPrimSpec failed: %s", err.c_str());
    return;
  }

  // dst should have material="metallic" from metal variant
  TEST_CHECK(dst.props().count("material") > 0);
  if (dst.props().count("material")) {
    auto v = dst.props().at("material").get_attribute().get_value<value::token>();
    TEST_CHECK(v.has_value());
    if (v.has_value()) {
      TEST_CHECK(v.value().str() == "metallic");
    }
  }
}

// ---------------------------------------------------------------------------
// 20. comp_apply_variant_selector_test
// Set up Layer with variants, call ApplyVariantSelector with
// VariantSelectorMap.
// ---------------------------------------------------------------------------
void comp_apply_variant_selector_test(void) {
  // ApplyVariantSelector() is declared in composition.hh but not yet
  // implemented. This test is a placeholder until the implementation lands.
  TEST_MSG("ApplyVariantSelector not yet implemented -- skipping");
}
