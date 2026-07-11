// SPDX-License-Identifier: Apache 2.0
// Unit tests for UsdRender placeholders and UsdVol prim types. These are
// recognized as distinct prim *types* (not the generic Model fallback) and
// retain authored properties through USDA + USDC round-trips.

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-render-vol.h"
#include "tinyusdz.hh"
#include "usdc-writer.hh"
#include "prim-types.hh"
#include "core/model-scope.hh"
#include "usdGeom.hh"

#include <string>
#include <vector>

using namespace tinyusdz;

static const char *kRenderVolUSDA =
    "#usda 1.0\n"
    "\n"
    "def RenderSettings \"settings\"\n"
    "{\n"
    "    int2 resolution = (1920, 1080)\n"
    "}\n"
    "\n"
    "def RenderProduct \"product\"\n"
    "{\n"
    "    token productType = \"raster\"\n"
    "}\n"
    "\n"
    "def RenderVar \"var\"\n"
    "{\n"
    "    token dataType = \"color3f\"\n"
    "}\n"
    "\n"
    "def Volume \"vol\"\n"
    "{\n"
    "}\n"
    "\n"
    "def OpenVDBAsset \"density\"\n"
    "{\n"
    "    asset filePath = @density.vdb@\n"
    "}\n"
    "\n"
    "def Field3DAsset \"f3d\"\n"
    "{\n"
    "}\n"
    "\n"
    "def GenerativeProcedural \"proc\"\n"
    "{\n"
    "    token primvars:displayColor = \"foo\"\n"
    "}\n";

static bool LoadUSDA(const std::string &usda, Stage *stage) {
  std::string warn, err;
  bool ok = tinyusdz::LoadUSDFromMemory(
      reinterpret_cast<const uint8_t *>(usda.data()), usda.size(), "mem.usda",
      stage, &warn, &err);
  if (!ok) TEST_MSG("USDA parse failed: %s", err.c_str());
  return ok;
}

// Find a root prim by element name (root-prim order is not guaranteed to match
// authored order after a USDC round-trip).
static const Prim *FindRoot(const Stage &stage, const std::string &name) {
  for (const auto &p : stage.root_prims()) {
    if (std::string(p.element_name()) == name) return &p;
  }
  return nullptr;
}

// The 6 root prims are recognized as their concrete placeholder types (NOT the
// generic Model fallback), and an authored property is retained.
static void CheckTypes(const Stage &stage) {
  TEST_CHECK_(stage.root_prims().size() == 7, "expected 7 root prims, got %zu",
              stage.root_prims().size());

  const Prim *settings = FindRoot(stage, "settings");
  const Prim *proc = FindRoot(stage, "proc");
  TEST_CHECK(proc && proc->as<GenerativeProcedural>() != nullptr);
  const Prim *product = FindRoot(stage, "product");
  const Prim *var = FindRoot(stage, "var");
  const Prim *vol = FindRoot(stage, "vol");
  const Prim *density = FindRoot(stage, "density");
  const Prim *f3d = FindRoot(stage, "f3d");

  TEST_CHECK(settings && settings->as<RenderSettings>() != nullptr);
  TEST_CHECK(product && product->as<RenderProduct>() != nullptr);
  TEST_CHECK(var && var->as<RenderVar>() != nullptr);
  TEST_CHECK(vol && vol->as<Volume>() != nullptr);
  TEST_CHECK(density && density->as<OpenVDBAsset>() != nullptr);
  TEST_CHECK(f3d && f3d->as<Field3DAsset>() != nullptr);

  // Must NOT be misclassified as the generic Model fallback.
  TEST_CHECK(settings && settings->as<Model>() == nullptr);

  // Authored property retained generically.
  if (product) {
    if (const RenderProduct *p = product->as<RenderProduct>()) {
      TEST_CHECK(p->props.find("productType") != p->props.end());
    }
  }
}

void render_vol_usda_roundtrip_test(void) {
  Stage stage;
  TEST_CHECK(LoadUSDA(kRenderVolUSDA, &stage));
  CheckTypes(stage);

  std::string exported = stage.ExportToString();
  TEST_CHECK(exported.find("def RenderSettings \"settings\"") != std::string::npos);
  TEST_CHECK(exported.find("def RenderProduct \"product\"") != std::string::npos);
  TEST_CHECK(exported.find("def RenderVar \"var\"") != std::string::npos);
  TEST_CHECK(exported.find("def Volume \"vol\"") != std::string::npos);
  TEST_CHECK(exported.find("def OpenVDBAsset \"density\"") != std::string::npos);
  TEST_CHECK(exported.find("def Field3DAsset \"f3d\"") != std::string::npos);
  TEST_CHECK(exported.find("def GenerativeProcedural \"proc\"") != std::string::npos);

  Stage stage2;
  TEST_CHECK(LoadUSDA(exported, &stage2));
  CheckTypes(stage2);
}

void render_vol_crate_roundtrip_test(void) {
  Stage stage;
  TEST_CHECK(LoadUSDA(kRenderVolUSDA, &stage));

  std::vector<uint8_t> usdc;
  std::string w, e;
  bool sret = tinyusdz::usdc::SaveAsUSDCToMemory(stage, &usdc, &w, &e);
  TEST_CHECK_(sret, "SaveAsUSDCToMemory failed: %s", e.c_str());
  if (!sret) return;
  TEST_CHECK(!usdc.empty());

  Stage stage2;
  std::string w2, e2;
  bool lret = tinyusdz::LoadUSDCFromMemory(usdc.data(), usdc.size(), "mem.usdc",
                                           &stage2, &w2, &e2);
  TEST_CHECK_(lret, "LoadUSDCFromMemory failed: %s", e2.c_str());
  if (!lret) return;
  CheckTypes(stage2);
}
