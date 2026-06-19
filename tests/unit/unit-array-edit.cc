// SPDX-License-Identifier: Apache 2.0
// Unit tests for VtArrayEdit (crate ValueRep IsArrayEdit bit, crate >= 0.14.0).
//
// The local OpenUSD build only supports crate <= 0.9.0, so cross-tool fixtures
// for 0.14.0 cannot be generated here; these tests build a value::ArrayEdit
// programmatically and exercise the Crate write + read round-trip.

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-array-edit.h"
#include "tinyusdz.hh"
#include "usdc-writer.hh"
#include "array-edit.hh"
#include "crate-format.hh"
#include "prim-types.hh"
#include "usdGeom.hh"  // Xform

#include <string>
#include <vector>

using namespace tinyusdz;

void array_edit_crate_roundtrip_test(void) {
  // Build: int[] edited = edit [ write 123 to [1]; append 234; erase [0] ]
  value::ArrayEdit ae;
  ae.element_type_id =
      static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_INT);
  ae.literals = value::Value(std::vector<int32_t>{123, 234});
  // write literal[0] (=123) to [1]
  ae.ops.push_back(
      value::ArrayEditPackOpWord(value::ArrayEditOp::WriteLiteral, 1));
  ae.ops.push_back(0);
  ae.ops.push_back(1);
  // append literal[1] (=234)  -- insert at EndIndex
  ae.ops.push_back(
      value::ArrayEditPackOpWord(value::ArrayEditOp::InsertLiteral, 1));
  ae.ops.push_back(1);
  ae.ops.push_back(value::kArrayEditEndIndex);
  // erase [0]
  ae.ops.push_back(value::ArrayEditPackOpWord(value::ArrayEditOp::EraseRef, 1));
  ae.ops.push_back(0);

  Attribute attr;
  attr.set_type_name("int[]");
  attr.set_value(ae);

  Xform xform;
  xform.name = "Foo";
  xform.props["edited"] = Property(attr, /*custom=*/true);

  Stage stage;
  TEST_CHECK(stage.add_root_prim(Prim(xform)));

  std::vector<uint8_t> usdc;
  std::string w, e;
  bool sret = usdc::SaveAsUSDCToMemory(stage, &usdc, &w, &e);
  TEST_CHECK_(sret, "SaveAsUSDCToMemory failed: %s", e.c_str());
  if (!sret) return;

  // A VtArrayEdit value must bump the emitted crate version to >= 0.14.0.
  TEST_CHECK(usdc.size() > 10);
  TEST_CHECK_(usdc[9] >= 14, "crate version minor = %d, expected >= 14",
              usdc[9]);

  Stage stage2;
  std::string w2, e2;
  bool lret = LoadUSDCFromMemory(usdc.data(), usdc.size(), "mem.usdc", &stage2,
                                 &w2, &e2);
  TEST_CHECK_(lret, "LoadUSDCFromMemory failed: %s", e2.c_str());
  if (!lret) return;

  TEST_CHECK(stage2.root_prims().size() == 1);
  if (stage2.root_prims().empty()) return;
  const Xform* xf = stage2.root_prims()[0].as<Xform>();
  TEST_CHECK(xf != nullptr);
  if (!xf) return;

  auto it = xf->props.find("edited");
  TEST_CHECK_(it != xf->props.end(), "`edited` property missing after reload");
  if (it == xf->props.end()) return;

  auto got = it->second.get_attribute().get_value<value::ArrayEdit>();
  TEST_CHECK_(bool(got), "reloaded attr value is not a value::ArrayEdit");
  if (!got) return;

  TEST_CHECK(got.value().element_type_id == ae.element_type_id);
  TEST_CHECK_(got.value().ops == ae.ops, "op stream mismatch after roundtrip");

  auto lits = got.value().literals.as<std::vector<int32_t>>();
  TEST_CHECK_(lits != nullptr, "literals not int[] after roundtrip");
  if (lits) {
    TEST_CHECK(lits->size() == 2);
    if (lits->size() == 2) {
      TEST_CHECK((*lits)[0] == 123 && (*lits)[1] == 234);
    }
  }
}

void array_edit_identity_test(void) {
  // An identity edit (no ops) is inlined: no out-of-line data, payload 0.
  value::ArrayEdit ae;
  ae.element_type_id =
      static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT);
  // ops intentionally empty
  TEST_CHECK(ae.is_identity());

  Attribute attr;
  attr.set_type_name("float[]");
  attr.set_value(ae);

  Xform xform;
  xform.name = "Bar";
  xform.props["e"] = Property(attr, /*custom=*/true);

  Stage stage;
  TEST_CHECK(stage.add_root_prim(Prim(xform)));

  std::vector<uint8_t> usdc;
  std::string w, e;
  TEST_CHECK(usdc::SaveAsUSDCToMemory(stage, &usdc, &w, &e));
  TEST_CHECK(usdc.size() > 10);
  TEST_CHECK_(usdc[9] >= 14, "crate version minor = %d, expected >= 14",
              usdc[9]);

  Stage stage2;
  std::string w2, e2;
  bool lret = LoadUSDCFromMemory(usdc.data(), usdc.size(), "mem.usdc", &stage2,
                                 &w2, &e2);
  TEST_CHECK_(lret, "LoadUSDCFromMemory failed: %s", e2.c_str());
  if (!lret) return;

  if (stage2.root_prims().empty()) {
    TEST_CHECK(false);
    return;
  }
  const Xform* xf = stage2.root_prims()[0].as<Xform>();
  TEST_CHECK(xf != nullptr);
  if (!xf) return;
  auto it = xf->props.find("e");
  TEST_CHECK(it != xf->props.end());
  if (it == xf->props.end()) return;

  auto got = it->second.get_attribute().get_value<value::ArrayEdit>();
  TEST_CHECK_(bool(got), "reloaded identity attr is not a value::ArrayEdit");
  if (!got) return;
  TEST_CHECK(got.value().is_identity());
  TEST_CHECK(got.value().element_type_id == ae.element_type_id);
}
