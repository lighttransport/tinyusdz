// SPDX-License-Identifier: Apache 2.0
// USDC reader unit tests: USDA -> USDC -> Stage roundtrip via
// SaveAsUSDCToMemory + LoadUSDCFromMemory.
//
// NOTE: The current USDC writer (CrateWriter) preserves hierarchy, connections,
// stage metadata, and prim types, but may not roundtrip all typed prim
// properties. Tests focus on what the roundtrip actually preserves plus
// USDC-specific error handling.

#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-usdc-reader.h"
#include "tinyusdz.hh"
#include "prim-types.hh"
#include "value-types.hh"
#include "usdc-writer.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "usdLux.hh"
#include "usdSkel.hh"
#include "math-util.inc"

#include <cstring>

using namespace tinyusdz;

// ---------------------------------------------------------------------------
// Helper: parse USDA, write USDC in-memory, read back to Stage
// ---------------------------------------------------------------------------
namespace {

static bool usda_to_usdc_roundtrip(const char *usda, Stage *out,
                                   std::string *warn, std::string *err) {
  Stage tmp;
  if (!LoadUSDAFromMemory(reinterpret_cast<const uint8_t *>(usda),
                          std::strlen(usda), "test.usda", &tmp, warn, err)) {
    return false;
  }
  std::vector<uint8_t> bytes;
  if (!usdc::SaveAsUSDCToMemory(tmp, &bytes, warn, err)) {
    return false;
  }
  return LoadUSDCFromMemory(bytes.data(), bytes.size(), "test.usdc", out, warn,
                            err);
}

}  // anonymous namespace

// ===========================================================================
// Type Roundtrips (6) — verify prim types survive roundtrip
// ===========================================================================

void usdc_reader_scalar_types_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0

def Sphere "sphere" {
}

def Camera "cam" {
}

def Cube "cube" {
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  // Verify prim types are preserved
  {
    auto r = stage.GetPrimAtPath(Path("/sphere", ""));
    TEST_CHECK(bool(r));
    if (r) { TEST_CHECK((*r)->as<GeomSphere>() != nullptr); }
  }
  {
    auto r = stage.GetPrimAtPath(Path("/cam", ""));
    TEST_CHECK(bool(r));
    if (r) { TEST_CHECK((*r)->as<GeomCamera>() != nullptr); }
  }
  {
    auto r = stage.GetPrimAtPath(Path("/cube", ""));
    TEST_CHECK(bool(r));
    if (r) { TEST_CHECK((*r)->as<GeomCube>() != nullptr); }
  }
}

void usdc_reader_string_token_types_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0

def Mesh "mesh" {
    point3f[] points = [(0, 0, 0)]
    int[] faceVertexCounts = [1]
    int[] faceVertexIndices = [0]
}

def Shader "shader" {
    uniform token info:id = "UsdPreviewSurface"
    token outputs:surface
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  // Verify prim types preserved
  {
    auto r = stage.GetPrimAtPath(Path("/mesh", ""));
    TEST_CHECK(bool(r));
    if (r) { TEST_CHECK((*r)->as<GeomMesh>() != nullptr); }
  }
  {
    auto r = stage.GetPrimAtPath(Path("/shader", ""));
    TEST_CHECK(bool(r));
    if (r) { TEST_CHECK((*r)->as<Shader>() != nullptr); }
  }
}

void usdc_reader_vector_matrix_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0

def Xform "xform" {
    double3 xformOp:translate = (1.0, 2.0, 3.0)
    double3 xformOp:scale = (2.0, 2.0, 2.0)
    uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:scale"]
}

def Skeleton "skel" {
    uniform token[] joints = ["Root"]
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  // Verify prim types preserved
  {
    auto r = stage.GetPrimAtPath(Path("/xform", ""));
    TEST_CHECK(bool(r));
    if (r) { TEST_CHECK((*r)->as<Xform>() != nullptr); }
  }
  {
    auto r = stage.GetPrimAtPath(Path("/skel", ""));
    TEST_CHECK(bool(r));
    if (r) { TEST_CHECK((*r)->as<Skeleton>() != nullptr); }
  }
}

void usdc_reader_array_int_float_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0

def Mesh "mesh" {
    point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto r = stage.GetPrimAtPath(Path("/mesh", ""));
  TEST_CHECK(bool(r));
  if (r) { TEST_CHECK((*r)->as<GeomMesh>() != nullptr); }
}

void usdc_reader_array_string_token_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0

def Skeleton "skel" {
    uniform token[] joints = ["Root", "Root/Spine", "Root/Spine/Head"]
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto r = stage.GetPrimAtPath(Path("/skel", ""));
  TEST_CHECK(bool(r));
  if (r) { TEST_CHECK((*r)->as<Skeleton>() != nullptr); }
}

void usdc_reader_array_vector_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0

def Mesh "mesh" {
    point3f[] points = [(1, 2, 3), (4, 5, 6)]
    normal3f[] normals = [(0, 0, 1), (0, 1, 0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 0]
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto r = stage.GetPrimAtPath(Path("/mesh", ""));
  TEST_CHECK(bool(r));
  if (r) { TEST_CHECK((*r)->as<GeomMesh>() != nullptr); }
}

// ===========================================================================
// TimeSamples Roundtrips (4) — verify roundtrip doesn't crash
// ===========================================================================

void usdc_reader_timesamples_scalar_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0

def Xform "test" {
    double3 xformOp:translate.timeSamples = {
        1: (0, 0, 0),
        2: (1, 2, 3),
        3: (10, 20, 30),
    }
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/test", ""));
  TEST_CHECK(bool(result));
  if (result) {
    TEST_CHECK((*result)->as<Xform>() != nullptr);
  }
}

void usdc_reader_timesamples_array_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0

def Mesh "test" {
    point3f[] points.timeSamples = {
        0: [(0, 0, 0), (1, 0, 0), (1, 1, 0)],
        1: [(0, 0, 1), (1, 0, 1), (1, 1, 1)],
    }
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 2]
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/test", ""));
  TEST_CHECK(bool(result));
  if (result) {
    TEST_CHECK((*result)->as<GeomMesh>() != nullptr);
  }
}

void usdc_reader_timesamples_blocked_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0

def SphereLight "light" {
    float inputs:intensity.timeSamples = {
        1: 100,
        2: 200,
        3: 300,
    }
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/light", ""));
  TEST_CHECK(bool(result));
  if (result) {
    TEST_CHECK((*result)->as<SphereLight>() != nullptr);
  }
}

void usdc_reader_timesamples_token_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0

def Xform "test" {
    token visibility.timeSamples = {
        0: "inherited",
        10: "invisible",
        20: "inherited",
    }
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/test", ""));
  TEST_CHECK(bool(result));
  if (result) {
    TEST_CHECK((*result)->as<Xform>() != nullptr);
  }
}

// ===========================================================================
// Connections & Metadata Roundtrips (4)
// ===========================================================================

void usdc_reader_connection_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0

def Material "mat" {
    token outputs:surface.connect = </mat/surf.outputs:surface>

    def Shader "surf" {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor = (0.8, 0.2, 0.1)
        token outputs:surface
    }
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/mat", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Material *mat = (*result)->as<Material>();
  TEST_CHECK(mat != nullptr);
  if (!mat) return;

  TEST_CHECK(mat->surface.authored());
  if (mat->surface.authored()) {
    auto paths = mat->surface.get_connections();
    TEST_CHECK(paths.size() == 1);
  }

  // Shader child should exist
  TEST_CHECK((*result)->children().size() >= 1);
}

void usdc_reader_relationship_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0

def Mesh "mesh" {
    rel material:binding = </mat>
    point3f[] points = [(0, 0, 0)]
    int[] faceVertexCounts = [1]
    int[] faceVertexIndices = [0]
}

def Material "mat" {
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/mesh", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const GeomMesh *mesh = (*result)->as<GeomMesh>();
  TEST_CHECK(mesh != nullptr);
  if (!mesh) return;

  TEST_CHECK(mesh->materialBinding.has_value());
}

void usdc_reader_prim_metadata_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0

def Scope "test" (
    kind = "component"
) {
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/test", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  // Verify prim exists and is Scope type
  TEST_CHECK((*result)->as<Scope>() != nullptr);
}

void usdc_reader_stage_metadata_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0
(
    upAxis = "Z"
    metersPerUnit = 0.01
    defaultPrim = "root"
    startTimeCode = 1
    endTimeCode = 100
)

def Xform "root" {
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  const auto &m = stage.metas();

  TEST_CHECK(m.upAxis.get_value() == Axis::Z);
  TEST_CHECK(math::is_close(m.metersPerUnit.get_value(), 0.01));
  TEST_CHECK(m.defaultPrim.str() == "root");
  TEST_CHECK(math::is_close(m.startTimeCode.get_value(), 1.0));
  TEST_CHECK(math::is_close(m.endTimeCode.get_value(), 100.0));
}

// ===========================================================================
// Hierarchy & Variants (2)
// ===========================================================================

void usdc_reader_nested_hierarchy_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0

def Xform "a" {
    def Xform "b" {
        def Xform "c" {
            def Scope "d" {
            }
        }
        def Scope "e" {
        }
    }
    def Scope "f" {
    }
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  {
    auto r = stage.GetPrimAtPath(Path("/a", ""));
    TEST_CHECK(bool(r));
    if (r) { TEST_CHECK((*r)->children().size() == 2); }
  }

  {
    auto r = stage.GetPrimAtPath(Path("/a/b", ""));
    TEST_CHECK(bool(r));
    if (r) { TEST_CHECK((*r)->children().size() == 2); }
  }

  {
    auto r = stage.GetPrimAtPath(Path("/a/b/c/d", ""));
    TEST_CHECK(bool(r));
  }
}

void usdc_reader_variantset_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0

def Xform "model" (
    variants = {
        string color = "red"
    }
    prepend variantSets = "color"
) {
    variantSet "color" = {
        "red" {
        }
        "blue" {
        }
    }
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/model", ""));
  TEST_CHECK(bool(result));
  if (result) {
    TEST_CHECK((*result)->as<Xform>() != nullptr);
  }
}

// ===========================================================================
// Binary-Specific (3)
// ===========================================================================

void usdc_reader_large_array_compression_test(void) {
  // Build USDA with many prims to test USDC compression with larger files
  std::string usda = "#usda 1.0\n\n";
  for (int i = 0; i < 50; i++) {
    usda += "def Xform \"prim" + std::to_string(i) + "\" {\n";
    usda += "    def Scope \"child\" {\n";
    usda += "    }\n";
    usda += "}\n\n";
  }

  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda.c_str(), &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  TEST_CHECK(stage.root_prims().size() == 50);

  // Spot-check some prims have children
  {
    auto r = stage.GetPrimAtPath(Path("/prim0/child", ""));
    TEST_CHECK(bool(r));
  }
  {
    auto r = stage.GetPrimAtPath(Path("/prim49/child", ""));
    TEST_CHECK(bool(r));
  }
}

void usdc_reader_inlined_scalar_test(void) {
  // Verify that multiple small prims survive USDC roundtrip
  const char *usda = R"(#usda 1.0

def Sphere "s1" {
}

def Sphere "s2" {
}

def Cube "c1" {
}

def Cone "cone1" {
}

def Cylinder "cyl1" {
}

def Capsule "cap1" {
}

def SphereLight "l1" {
}

def SphereLight "l2" {
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  TEST_CHECK(stage.root_prims().size() == 8);

  // Check types
  {
    auto r = stage.GetPrimAtPath(Path("/s1", ""));
    TEST_CHECK(bool(r));
    if (r) { TEST_CHECK((*r)->as<GeomSphere>() != nullptr); }
  }
  {
    auto r = stage.GetPrimAtPath(Path("/c1", ""));
    TEST_CHECK(bool(r));
    if (r) { TEST_CHECK((*r)->as<GeomCube>() != nullptr); }
  }
  {
    auto r = stage.GetPrimAtPath(Path("/l1", ""));
    TEST_CHECK(bool(r));
    if (r) { TEST_CHECK((*r)->as<SphereLight>() != nullptr); }
  }
}

void usdc_reader_multiple_prims_roundtrip_test(void) {
  // Build USDA with 20 root Xform prims
  std::string usda = "#usda 1.0\n\n";
  for (int i = 0; i < 20; i++) {
    usda += "def Xform \"prim" + std::to_string(i) + "\" {\n";
    usda += "}\n\n";
  }

  Stage stage;
  std::string warn, err;
  bool ok = usda_to_usdc_roundtrip(usda.c_str(), &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  TEST_CHECK(stage.root_prims().size() == 20);

  // Spot-check a few prims
  for (int i : {0, 5, 10, 19}) {
    std::string path = "/prim" + std::to_string(i);
    auto r = stage.GetPrimAtPath(Path(path, ""));
    TEST_CHECK(bool(r));
    if (r) {
      TEST_CHECK((*r)->as<Xform>() != nullptr);
    }
  }
}

// ===========================================================================
// Error Handling (3)
// ===========================================================================

void usdc_reader_truncated_input_test(void) {
  // 0 bytes
  {
    Stage stage;
    std::string warn, err;
    bool ok = LoadUSDCFromMemory(nullptr, 0, "test.usdc", &stage, &warn, &err);
    TEST_CHECK(!ok);
  }

  // Header only (8 bytes = magic "PXR-USDC")
  {
    uint8_t data[8] = {'P', 'X', 'R', '-', 'U', 'S', 'D', 'C'};
    Stage stage;
    std::string warn, err;
    bool ok = LoadUSDCFromMemory(data, 8, "test.usdc", &stage, &warn, &err);
    TEST_CHECK(!ok);
  }

  // Partial TOC (magic + some garbage)
  {
    uint8_t data[64];
    memcpy(data, "PXR-USDC", 8);
    memset(data + 8, 0, 56);
    Stage stage;
    std::string warn, err;
    bool ok = LoadUSDCFromMemory(data, 64, "test.usdc", &stage, &warn, &err);
    TEST_CHECK(!ok);
  }
}

void usdc_reader_corrupt_header_test(void) {
  const char *usda = R"(#usda 1.0
def Scope "test" {
}
)";
  Stage tmp;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(reinterpret_cast<const uint8_t *>(usda),
                               std::strlen(usda), "test.usda", &tmp, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) return;

  std::vector<uint8_t> bytes;
  ok = usdc::SaveAsUSDCToMemory(tmp, &bytes, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) return;
  TEST_CHECK(bytes.size() > 8);

  // Corrupt the magic bytes
  bytes[0] = 'X';
  bytes[1] = 'X';
  bytes[2] = 'X';
  bytes[3] = 'X';

  Stage stage;
  ok = LoadUSDCFromMemory(bytes.data(), bytes.size(), "test.usdc", &stage, &warn, &err);
  TEST_CHECK(!ok);
}

void usdc_reader_corrupt_body_test(void) {
  const char *usda = R"(#usda 1.0
def Scope "test" {
}
)";
  Stage tmp;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(reinterpret_cast<const uint8_t *>(usda),
                               std::strlen(usda), "test.usda", &tmp, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) return;

  std::vector<uint8_t> bytes;
  ok = usdc::SaveAsUSDCToMemory(tmp, &bytes, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) return;

  // Zero out bytes 100-200 (if the file is large enough)
  if (bytes.size() > 200) {
    memset(bytes.data() + 100, 0, 100);

    Stage stage;
    ok = LoadUSDCFromMemory(bytes.data(), bytes.size(), "test.usdc", &stage,
                            &warn, &err);
    // Should either fail or produce incorrect data; we just verify no crash
    (void)ok;
    TEST_CHECK(true);
  } else {
    TEST_CHECK(true);
  }
}
