// SPDX-License-Identifier: Apache 2.0
// Unit tests for UsdRender placeholders and UsdVol prim types. These are
// recognized as distinct prim *types* (not the generic Model fallback) and
// retain authored properties through USDA + USDC round-trips.

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-render-vol.h"
#include "tinyusdz.hh"
#include "usdc-writer.hh"
#include "usdGeom.hh"
#include "usdVol.hh"
#include "usdShade.hh"
#include "core/model-scope.hh"
#include "usdGeom.hh"

#include <cstdlib>
#include <fstream>
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

void usdvol_material_binding_test(void) {
  const std::string usda = R"USD(#usda 1.0
def Volume "Vol" (prepend apiSchemas = ["MaterialBindingAPI"]) {
    rel material:binding = </Mat>
}
def Material "Mat" {
    token outputs:volume.connect = </Mat/Shader.outputs:volume>
    def Shader "Shader" {
        uniform token info:id = "ND_standard_volume_volume"
        float inputs:density = 0.25
        color3f inputs:scattering_color = (0.1, 0.2, 0.3)
        color3f inputs:emission_color = (0.4, 0.5, 0.6)
        float inputs:emission_intensity = 2
        token outputs:volume
    }
}
)USD";
  Stage stage;
  TEST_CHECK(LoadUSDA(usda, &stage));
  const Prim *vol_prim = FindRoot(stage, "Vol");
  const Prim *mat_prim = FindRoot(stage, "Mat");
  TEST_ASSERT(vol_prim && mat_prim);
  const Volume *volume = vol_prim->as<Volume>();
  const Material *material = mat_prim->as<Material>();
  TEST_ASSERT(volume && material);
  Relationship binding;
  TEST_CHECK(volume->get_materialBinding(value::token(""), &binding));
  TEST_CHECK(material->volume.authored());
  TEST_CHECK(material->volume.get_connections().size() == 1);
  const Prim *shader_prim = nullptr;
  TEST_CHECK(stage.find_prim_at_path(Path("/Mat/Shader", ""), shader_prim));
  TEST_ASSERT(shader_prim && shader_prim->as<Shader>());
  const Shader *shader = shader_prim->as<Shader>();
  const ShaderNode *node = shader->value.as<ShaderNode>();
  TEST_ASSERT(node != nullptr);
  TEST_CHECK(node->props.find("inputs:density") != node->props.end());
  TEST_CHECK(node->props.find("inputs:emission_color") != node->props.end());
}

void usdvol_vdb_corpus_test(void) {
  const char *root = std::getenv("TINYVDBIO_DATA_DIR");
  if (!root || !root[0]) {
    TEST_MSG("SKIP: set TINYVDBIO_DATA_DIR to the tinyvdbio data directory");
    return;
  }
  struct Case { const char *file; const char *name; const char *type; };
  const Case cases[] = {
      {"reference/ref_bool.vdb", "bool_test", "bool"},
      {"reference/ref_float.vdb", "float_test", "float"},
      {"reference/ref_double.vdb", "double_test", "double"},
      {"reference/ref_int32.vdb", "int32_test", "int32"},
      {"reference/ref_int64.vdb", "int64_test", "int64"},
      {"reference/ref_vec3s.vdb", "vec3s_test", "vec3f-magnitude"},
  };
  for (const Case &c : cases) {
    std::vector<usdVol::VDBGrid> grids;
    std::string warn, err;
    const std::string path = std::string(root) + "/" + c.file;
    const bool ok = usdVol::ReadVDBFromFile(path, &grids, &warn, &err, 4096);
    TEST_CHECK_(ok, "%s: %s", path.c_str(), err.c_str());
    if (!ok || grids.empty()) continue;
    const usdVol::VDBGrid &grid = grids[0];
    TEST_CHECK_(grid.name == c.name, "%s: grid name '%s'", c.file,
                grid.name.c_str());
    TEST_CHECK_(grid.value_type == c.type, "%s: value type '%s'", c.file,
                grid.value_type.c_str());
    TEST_CHECK(grid.dim[0] == 4 && grid.dim[1] == 4 && grid.dim[2] == 4);
    TEST_CHECK(grid.data.size() == 64);
  }

  // USDZ asset resolution supplies VDB bytes rather than a filesystem path.
  // Exercise the external-buffer ownership path separately.
  {
    const std::string path = std::string(root) + "/reference/ref_float.vdb";
    std::ifstream stream(path.c_str(), std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                               std::istreambuf_iterator<char>());
    std::vector<usdVol::VDBGrid> grids;
    std::string warn, err;
    const bool ok = usdVol::ReadVDBFromMemory(bytes.data(), bytes.size(), path,
                                               &grids, &warn, &err, 4096);
    TEST_CHECK_(ok, "memory VDB: %s", err.c_str());
    TEST_CHECK(grids.size() == 1 && grids[0].data.size() == 64);
  }

  const Case volume_cases[] = {
      {"sphere-div1.vdb", "density", "float"},
      {"smoke.vdb", "density", "float"},
      {"suzanne.vdb", "density", "float"},
      {"fire.vdb", "density", "float"},
      {"explosion.vdb", "density", "float"},
  };
  for (const Case &c : volume_cases) {
    std::vector<usdVol::VDBGrid> grids;
    std::string warn, err;
    const std::string path = std::string(root) + "/" + c.file;
    const bool ok = usdVol::ReadVDBFromFile(path, &grids, &warn, &err,
                                            size_t(16) * 1024 * 1024);
    TEST_CHECK_(ok, "%s: %s %s", path.c_str(), warn.c_str(), err.c_str());
    if (!ok || grids.empty()) continue;
    const usdVol::VDBGrid *grid = nullptr;
    for (const auto &candidate : grids)
      if (candidate.name == c.name) { grid = &candidate; break; }
    TEST_CHECK_(grid != nullptr, "%s: missing grid '%s'", c.file, c.name);
    if (!grid) continue;
    TEST_CHECK(grid->value_type == c.type);
    TEST_CHECK(!grid->data.empty());
    TEST_CHECK(grid->dim[0] > 0 && grid->dim[1] > 0 && grid->dim[2] > 0);
    if (std::string(c.file) == "fire.vdb") {
      const usdVol::VDBGrid *temperature = nullptr;
      for (const auto &candidate : grids) {
        if (candidate.name == "temperature") temperature = &candidate;
      }
      TEST_CHECK_(temperature != nullptr,
                  "fire.vdb: temperature field must be decoded for volume shading");
      if (temperature) {
        TEST_CHECK(!temperature->data.empty());
        // This corpus intentionally exercises independent field lattices; the
        // renderer must sample each field through its own VDB transform.
        TEST_CHECK(temperature->dim[0] != grid->dim[0] ||
                   temperature->dim[1] != grid->dim[1] ||
                   temperature->dim[2] != grid->dim[2]);
      }
    }
  }

  // PointDataGrid is valid VDB but is not a voxel-valued UsdVol field. It must
  // be rejected cleanly instead of treating its auxiliary point payload as a
  // scalar leaf buffer.
  {
    std::vector<usdVol::VDBGrid> grids;
    std::string warn, err;
    const bool ok = usdVol::ReadVDBFromFile(
        std::string(root) + "/sphere_points.vdb", &grids, &warn, &err, 4096);
    TEST_CHECK(!ok);
    TEST_CHECK(grids.empty());
    TEST_CHECK(warn.find("point grid") != std::string::npos);
  }
}
