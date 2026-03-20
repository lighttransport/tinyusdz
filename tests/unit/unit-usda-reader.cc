// SPDX-License-Identifier: Apache 2.0
// USDA reader unit tests: parse USDA via LoadUSDAFromMemory, then verify
// that the Stage contains correctly-typed values.

#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-usda-reader.h"
#include "tinyusdz.hh"
#include "core/prim.hh"
#include "value-types.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "usdLux.hh"
#include "core/model-scope.hh"
#include "math-util.inc"

#include <cstring>
#include <sstream>

using namespace tinyusdz;

// ---------------------------------------------------------------------------
// Helper: parse a USDA string into a Stage
// ---------------------------------------------------------------------------
namespace {

static bool parse_usda(const char *usda, Stage *stage, std::string *warn,
                       std::string *err) {
  return LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda), "test.usda",
      stage, warn, err);
}

}  // anonymous namespace

// ===========================================================================
// Basic Types (3)
// ===========================================================================

void usda_reader_scalar_int_float_double_test(void) {
  const char *usda = R"(#usda 1.0

def Scope "test" {
    custom int myInt = 42
    custom float myFloat = 3.14
    custom double myDouble = 2.718281828
    custom bool myBool = true
    custom half myHalf = 1.5
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/test", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Scope *scope = (*result)->as<Scope>();
  TEST_CHECK(scope != nullptr);
  if (!scope) return;
  const auto &props = scope->props;

  // int
  {
    auto it = props.find("myInt");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      TEST_CHECK(it->second.is_attribute());
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<int>();
      TEST_CHECK(v.has_value());
      if (v) { TEST_CHECK(v.value() == 42); }
    }
  }

  // float
  {
    auto it = props.find("myFloat");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<float>();
      TEST_CHECK(v.has_value());
      if (v) { TEST_CHECK(math::is_close(v.value(), 3.14f)); }
    }
  }

  // double
  {
    auto it = props.find("myDouble");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<double>();
      TEST_CHECK(v.has_value());
      if (v) { TEST_CHECK(math::is_close(v.value(), 2.718281828)); }
    }
  }

  // bool
  {
    auto it = props.find("myBool");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<bool>();
      TEST_CHECK(v.has_value());
      if (v) { TEST_CHECK(v.value() == true); }
    }
  }

  // half
  {
    auto it = props.find("myHalf");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<value::half>();
      TEST_CHECK(v.has_value());
    }
  }
}

void usda_reader_scalar_string_token_path_test(void) {
  const char *usda = R"(#usda 1.0

def Scope "test" {
    custom string myString = "hello world"
    custom token myToken = "myTokenValue"
    custom asset myAsset = @./texture.png@
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/test", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Scope *scope = (*result)->as<Scope>();
  TEST_CHECK(scope != nullptr);
  if (!scope) return;
  const auto &props = scope->props;

  // string
  {
    auto it = props.find("myString");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<std::string>();
      TEST_CHECK(v.has_value());
      if (v) { TEST_CHECK(v.value() == "hello world"); }
    }
  }

  // token
  {
    auto it = props.find("myToken");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<value::token>();
      TEST_CHECK(v.has_value());
      if (v) { TEST_CHECK(v.value().str() == "myTokenValue"); }
    }
  }

  // asset
  {
    auto it = props.find("myAsset");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<value::AssetPath>();
      TEST_CHECK(v.has_value());
      if (v) { TEST_CHECK(v.value().GetAssetPath() == "./texture.png"); }
    }
  }
}

void usda_reader_vector_matrix_types_test(void) {
  const char *usda = R"(#usda 1.0

def Scope "test" {
    custom float2 myFloat2 = (1.0, 2.0)
    custom float3 myFloat3 = (1.0, 2.0, 3.0)
    custom double3 myDouble3 = (10.0, 20.0, 30.0)
    custom color3f myColor = (0.5, 0.6, 0.7)
    custom quatf myQuat = (1.0, 0.0, 0.0, 0.0)
    custom matrix4d myMatrix = ( (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1) )
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/test", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Scope *scope = (*result)->as<Scope>();
  TEST_CHECK(scope != nullptr);
  if (!scope) return;
  const auto &props = scope->props;

  // float2
  {
    auto it = props.find("myFloat2");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<value::float2>();
      TEST_CHECK(v.has_value());
      if (v) {
        TEST_CHECK(math::is_close(v.value()[0], 1.0f));
        TEST_CHECK(math::is_close(v.value()[1], 2.0f));
      }
    }
  }

  // float3
  {
    auto it = props.find("myFloat3");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<value::float3>();
      TEST_CHECK(v.has_value());
      if (v) {
        TEST_CHECK(math::is_close(v.value()[0], 1.0f));
        TEST_CHECK(math::is_close(v.value()[1], 2.0f));
        TEST_CHECK(math::is_close(v.value()[2], 3.0f));
      }
    }
  }

  // double3
  {
    auto it = props.find("myDouble3");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<value::double3>();
      TEST_CHECK(v.has_value());
      if (v) {
        TEST_CHECK(math::is_close(v.value()[0], 10.0));
        TEST_CHECK(math::is_close(v.value()[1], 20.0));
        TEST_CHECK(math::is_close(v.value()[2], 30.0));
      }
    }
  }

  // color3f
  {
    auto it = props.find("myColor");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<value::color3f>();
      TEST_CHECK(v.has_value());
      if (v) {
        TEST_CHECK(math::is_close(v.value()[0], 0.5f));
        TEST_CHECK(math::is_close(v.value()[1], 0.6f));
        TEST_CHECK(math::is_close(v.value()[2], 0.7f));
      }
    }
  }

  // quatf
  {
    auto it = props.find("myQuat");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<value::quatf>();
      TEST_CHECK(v.has_value());
    }
  }

  // matrix4d
  {
    auto it = props.find("myMatrix");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<value::matrix4d>();
      TEST_CHECK(v.has_value());
      if (v) {
        // Identity matrix: m[0][0] = 1, m[1][1] = 1, etc.
        TEST_CHECK(math::is_close(v.value().m[0][0], 1.0));
        TEST_CHECK(math::is_close(v.value().m[1][1], 1.0));
        TEST_CHECK(math::is_close(v.value().m[2][2], 1.0));
        TEST_CHECK(math::is_close(v.value().m[3][3], 1.0));
        TEST_CHECK(math::is_close(v.value().m[0][1], 0.0));
      }
    }
  }
}

// ===========================================================================
// Arrays (3)
// ===========================================================================

void usda_reader_array_int_float_test(void) {
  const char *usda = R"(#usda 1.0

def Scope "test" {
    custom int[] myInts = [1, 2, 3, 4, 5]
    custom float[] myFloats = [1.1, 2.2, 3.3]
    custom double[] myDoubles = [10.5, 20.5]
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/test", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Scope *scope = (*result)->as<Scope>();
  TEST_CHECK(scope != nullptr);
  if (!scope) return;
  const auto &props = scope->props;

  // int[]
  {
    auto it = props.find("myInts");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<std::vector<int>>();
      TEST_CHECK(v.has_value());
      if (v) {
        TEST_CHECK(v.value().size() == 5);
        TEST_CHECK(v.value()[0] == 1);
        TEST_CHECK(v.value()[4] == 5);
      }
    }
  }

  // float[]
  {
    auto it = props.find("myFloats");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<std::vector<float>>();
      TEST_CHECK(v.has_value());
      if (v) {
        TEST_CHECK(v.value().size() == 3);
        TEST_CHECK(math::is_close(v.value()[0], 1.1f));
      }
    }
  }

  // double[]
  {
    auto it = props.find("myDoubles");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<std::vector<double>>();
      TEST_CHECK(v.has_value());
      if (v) {
        TEST_CHECK(v.value().size() == 2);
        TEST_CHECK(math::is_close(v.value()[0], 10.5));
      }
    }
  }
}

void usda_reader_array_string_token_test(void) {
  const char *usda = R"(#usda 1.0

def Scope "test" {
    custom string[] myStrings = ["hello", "world"]
    custom token[] myTokens = ["tok1", "tok2", "tok3"]
    custom int[] emptyArray = []
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/test", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Scope *scope = (*result)->as<Scope>();
  TEST_CHECK(scope != nullptr);
  if (!scope) return;
  const auto &props = scope->props;

  // string[]
  {
    auto it = props.find("myStrings");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<std::vector<std::string>>();
      TEST_CHECK(v.has_value());
      if (v) {
        TEST_CHECK(v.value().size() == 2);
        TEST_CHECK(v.value()[0] == "hello");
        TEST_CHECK(v.value()[1] == "world");
      }
    }
  }

  // token[]
  {
    auto it = props.find("myTokens");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<std::vector<value::token>>();
      TEST_CHECK(v.has_value());
      if (v) {
        TEST_CHECK(v.value().size() == 3);
        TEST_CHECK(v.value()[0].str() == "tok1");
      }
    }
  }

  // empty array
  {
    auto it = props.find("emptyArray");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<std::vector<int>>();
      TEST_CHECK(v.has_value());
      if (v) {
        TEST_CHECK(v.value().size() == 0);
      }
    }
  }
}

void usda_reader_array_vector_types_test(void) {
  const char *usda = R"(#usda 1.0

def Mesh "test" {
    point3f[] points = [(1, 2, 3), (4, 5, 6)]
    normal3f[] normals = [(0, 0, 1), (0, 1, 0)]
    color3f[] primvars:displayColor = [(1, 0, 0), (0, 1, 0), (0, 0, 1)]
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/test", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const GeomMesh *mesh = (*result)->as<GeomMesh>();
  TEST_CHECK(mesh != nullptr);
  if (!mesh) return;

  // point3f[]
  auto pts = mesh->get_points();
  TEST_CHECK(pts.size() == 2);
  if (pts.size() == 2) {
    TEST_CHECK(math::is_close(pts[0].x, 1.0f));
    TEST_CHECK(math::is_close(pts[1].x, 4.0f));
  }

  // normal3f[]
  auto norms = mesh->get_normals();
  TEST_CHECK(norms.size() == 2);
  if (norms.size() == 2) {
    TEST_CHECK(math::is_close(norms[0].z, 1.0f));
    TEST_CHECK(math::is_close(norms[1].y, 1.0f));
  }
}

// ===========================================================================
// TimeSamples (4)
// ===========================================================================

void usda_reader_timesamples_scalar_test(void) {
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
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/test", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Xform *xform = (*result)->as<Xform>();
  TEST_CHECK(xform != nullptr);
  if (!xform) return;

  TEST_CHECK(xform->xformOps.size() == 1);
  if (xform->xformOps.empty()) return;

  // The xformOp should have timeSamples
  const auto &op = xform->xformOps[0];
  TEST_CHECK(op.is_timesamples());
}

void usda_reader_timesamples_array_test(void) {
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
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/test", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const GeomMesh *mesh = (*result)->as<GeomMesh>();
  TEST_CHECK(mesh != nullptr);
  if (!mesh) return;

  // points should have timeSamples, not a default value
  auto pts_val = mesh->points.get_value();
  TEST_CHECK(pts_val.has_value());
  if (pts_val) { TEST_CHECK(pts_val.value().is_timesamples()); }
}

void usda_reader_timesamples_blocked_test(void) {
  const char *usda = R"(#usda 1.0

def Scope "test" {
    custom float myVal.timeSamples = {
        1: 10.0,
        2: None,
        3: 30.0,
    }
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/test", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Scope *scope = (*result)->as<Scope>();
  TEST_CHECK(scope != nullptr);
  if (!scope) return;
  const auto &props = scope->props;
  auto it = props.find("myVal");
  TEST_CHECK(it != props.end());
  if (it != props.end()) {
    const auto &attr = it->second.get_attribute();
    TEST_CHECK(attr.has_timesamples());
  }
}

void usda_reader_timesamples_token_enum_test(void) {
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
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/test", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Xform *xform = (*result)->as<Xform>();
  TEST_CHECK(xform != nullptr);
  if (!xform) return;

  TEST_CHECK(xform->visibility.get_value().is_timesamples());
}

// ===========================================================================
// Connections & Relationships (2)
// ===========================================================================

void usda_reader_attribute_connection_test(void) {
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
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/mat", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Material *mat = (*result)->as<Material>();
  TEST_CHECK(mat != nullptr);
  if (!mat) return;

  // Material should have surface output connection
  TEST_CHECK(mat->surface.authored());
  if (mat->surface.authored()) {
    auto paths = mat->surface.get_connections();
    TEST_CHECK(paths.size() == 1);
  }

  // Check shader child
  TEST_CHECK((*result)->children().size() >= 1);
  if ((*result)->children().empty()) return;

  const Shader *shader = (*result)->children()[0].as<Shader>();
  TEST_CHECK(shader != nullptr);
  if (!shader) return;
  TEST_CHECK(shader->info_id == "UsdPreviewSurface");
}

void usda_reader_relationship_test(void) {
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
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/mesh", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const GeomMesh *mesh = (*result)->as<GeomMesh>();
  TEST_CHECK(mesh != nullptr);
  if (!mesh) return;

  // Check material:binding relationship
  TEST_CHECK(mesh->materialBinding.has_value());
}

// ===========================================================================
// Metadata (2)
// ===========================================================================

void usda_reader_prim_metadata_test(void) {
  const char *usda = R"(#usda 1.0

def Scope "test" (
    doc = "A test scope"
    kind = "component"
    customData = {
        string author = "tinyusdz"
        int version = 1
    }
) {
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/test", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Prim *prim = *result;

  // doc
  TEST_CHECK(prim->metas().has_doc());
  if (prim->metas().has_doc()) {
    TEST_CHECK(prim->metas().get_doc().value == "A test scope");
  }

  // kind
  TEST_CHECK(prim->metas().has_kind());
  if (prim->metas().has_kind()) {
    TEST_CHECK(prim->metas().get_kind_enum() == Kind::Component);
  }

  // customData
  TEST_CHECK(prim->metas().has_customData());
  if (prim->metas().has_customData()) {
    Dictionary cd = prim->metas().get_customData();
    TEST_CHECK(cd.size() >= 2);
  }
}

void usda_reader_stage_metadata_test(void) {
  const char *usda = R"(#usda 1.0
(
    upAxis = "Z"
    metersPerUnit = 0.01
    defaultPrim = "root"
    startTimeCode = 1
    endTimeCode = 100
    doc = "stage doc"
)

def Xform "root" {
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  const auto &m = stage.metas();

  // upAxis
  TEST_CHECK(m.upAxis.get_value() == Axis::Z);

  // metersPerUnit
  TEST_CHECK(math::is_close(m.metersPerUnit.get_value(), 0.01));

  // defaultPrim
  TEST_CHECK(m.defaultPrim.str() == "root");

  // timeCode range
  TEST_CHECK(math::is_close(m.startTimeCode.get_value(), 1.0));
  TEST_CHECK(math::is_close(m.endTimeCode.get_value(), 100.0));

  // doc
  TEST_CHECK(m.doc.value == "stage doc");
}

// ===========================================================================
// Variants & Composition (4)
// ===========================================================================

void usda_reader_variantset_basic_test(void) {
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
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/model", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Prim *prim = *result;

  // Check variant selection
  TEST_CHECK(prim->metas().variants.has_value());
  if (prim->metas().variants.has_value()) {
    const auto &vs = prim->metas().variants.value();
    auto it = vs.find("color");
    TEST_CHECK(it != vs.end());
    if (it != vs.end()) {
      TEST_CHECK(it->second == "red");
    }
  }
}

void usda_reader_variantset_with_properties_test(void) {
  const char *usda = R"(#usda 1.0

def Xform "model" (
    variants = {
        string size = "small"
    }
    prepend variantSets = "size"
) {
    variantSet "size" = {
        "small" {
            custom double scale = 1.0
        }
        "large" {
            custom double scale = 10.0
        }
    }
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/model", ""));
  TEST_CHECK(bool(result));
}

void usda_reader_class_inherits_test(void) {
  const char *usda = R"(#usda 1.0

class Scope "_class_base" {
    custom string label = "base"
}

def Scope "child" (
    prepend inherits = </_class_base>
) {
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  // Check class prim
  {
    auto result = stage.GetPrimAtPath(Path("/_class_base", ""));
    TEST_CHECK(bool(result));
    if (result) {
      const Scope *s = (*result)->as<Scope>();
      TEST_CHECK(s != nullptr);
      if (s) { TEST_CHECK(s->spec == Specifier::Class); }
    }
  }

  // Check child with inherits
  {
    auto result = stage.GetPrimAtPath(Path("/child", ""));
    TEST_CHECK(bool(result));
    if (result) {
      TEST_CHECK((*result)->metas().inherits.has_value());
    }
  }
}

void usda_reader_internal_reference_test(void) {
  const char *usda = R"(#usda 1.0

def Scope "source" {
    custom string label = "original"
}

def Scope "target" (
    prepend references = </source>
) {
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/target", ""));
  TEST_CHECK(bool(result));
  if (result) {
    TEST_CHECK((*result)->metas().references.has_value());
  }
}

// ===========================================================================
// Hierarchy & Specifiers (2)
// ===========================================================================

void usda_reader_nested_hierarchy_test(void) {
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
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  // Root: a
  {
    auto r = stage.GetPrimAtPath(Path("/a", ""));
    TEST_CHECK(bool(r));
    if (r) {
      // a has 2 children: b, f
      TEST_CHECK((*r)->children().size() == 2);
    }
  }

  // 2nd level: a/b
  {
    auto r = stage.GetPrimAtPath(Path("/a/b", ""));
    TEST_CHECK(bool(r));
    if (r) {
      // b has 2 children: c, e
      TEST_CHECK((*r)->children().size() == 2);
    }
  }

  // 3rd level: a/b/c
  {
    auto r = stage.GetPrimAtPath(Path("/a/b/c", ""));
    TEST_CHECK(bool(r));
    if (r) {
      TEST_CHECK((*r)->children().size() == 1);
    }
  }

  // 4th level: a/b/c/d
  {
    auto r = stage.GetPrimAtPath(Path("/a/b/c/d", ""));
    TEST_CHECK(bool(r));
  }

  // Siblings: a/b/e
  {
    auto r = stage.GetPrimAtPath(Path("/a/b/e", ""));
    TEST_CHECK(bool(r));
  }

  // Sibling: a/f
  {
    auto r = stage.GetPrimAtPath(Path("/a/f", ""));
    TEST_CHECK(bool(r));
  }
}

void usda_reader_specifiers_def_over_class_test(void) {
  const char *usda = R"(#usda 1.0

def Scope "defPrim" {
}

over Scope "overPrim" {
}

class Scope "classPrim" {
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  // def
  {
    auto r = stage.GetPrimAtPath(Path("/defPrim", ""));
    TEST_CHECK(bool(r));
    if (r) {
      const Scope *s = (*r)->as<Scope>();
      TEST_CHECK(s != nullptr);
      if (s) { TEST_CHECK(s->spec == Specifier::Def); }
    }
  }

  // over
  {
    auto r = stage.GetPrimAtPath(Path("/overPrim", ""));
    TEST_CHECK(bool(r));
    if (r) {
      const Scope *s = (*r)->as<Scope>();
      TEST_CHECK(s != nullptr);
      if (s) { TEST_CHECK(s->spec == Specifier::Over); }
    }
  }

  // class
  {
    auto r = stage.GetPrimAtPath(Path("/classPrim", ""));
    TEST_CHECK(bool(r));
    if (r) {
      const Scope *s = (*r)->as<Scope>();
      TEST_CHECK(s != nullptr);
      if (s) { TEST_CHECK(s->spec == Specifier::Class); }
    }
  }
}

// ===========================================================================
// Edge Cases (3)
// ===========================================================================

void usda_reader_trailing_comma_test(void) {
  const char *usda = R"(#usda 1.0

def Scope "test" {
    custom int[] vals = [1, 2, 3,]
    # comment inside code
    custom float[] fvals = [
        1.0, # trailing comma and comment
        2.0,
        3.0, # last trailing comma
    ]
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/test", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Scope *scope = (*result)->as<Scope>();
  TEST_CHECK(scope != nullptr);
  if (!scope) return;
  const auto &props = scope->props;

  {
    auto it = props.find("vals");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<std::vector<int>>();
      TEST_CHECK(v.has_value());
      if (v) { TEST_CHECK(v.value().size() == 3); }
    }
  }

  {
    auto it = props.find("fvals");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<std::vector<float>>();
      TEST_CHECK(v.has_value());
      if (v) { TEST_CHECK(v.value().size() == 3); }
    }
  }
}

void usda_reader_empty_prim_test(void) {
  const char *usda = R"(#usda 1.0

def Scope "empty1" {
}

def Xform "empty2" {
}

def Scope "parent" {
    def Scope "emptyChild" {
    }
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  TEST_CHECK(stage.root_prims().size() == 3);

  {
    auto r = stage.GetPrimAtPath(Path("/empty1", ""));
    TEST_CHECK(bool(r));
    if (r) {
      const Scope *s = (*r)->as<Scope>();
      TEST_CHECK(s != nullptr);
      if (s) { TEST_CHECK(s->props.empty()); }
      TEST_CHECK((*r)->children().empty());
    }
  }

  {
    auto r = stage.GetPrimAtPath(Path("/empty2", ""));
    TEST_CHECK(bool(r));
  }

  {
    auto r = stage.GetPrimAtPath(Path("/parent/emptyChild", ""));
    TEST_CHECK(bool(r));
  }
}

void usda_reader_unicode_and_special_strings_test(void) {
  const char *usda = R"(#usda 1.0

def Scope "test" {
    custom string escaped = "line1\nline2\ttab"
    custom string withQuotes = "say \"hello\""
    custom string empty = ""
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/test", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Scope *scope = (*result)->as<Scope>();
  TEST_CHECK(scope != nullptr);
  if (!scope) return;
  const auto &props = scope->props;

  // escaped string
  {
    auto it = props.find("escaped");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<std::string>();
      TEST_CHECK(v.has_value());
      if (v) {
        TEST_CHECK(v.value().find('\n') != std::string::npos);
        TEST_CHECK(v.value().find('\t') != std::string::npos);
      }
    }
  }

  // quoted string
  {
    auto it = props.find("withQuotes");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<std::string>();
      TEST_CHECK(v.has_value());
      if (v) {
        TEST_CHECK(v.value().find('"') != std::string::npos);
      }
    }
  }

  // empty string
  {
    auto it = props.find("empty");
    TEST_CHECK(it != props.end());
    if (it != props.end()) {
      const auto &attr = it->second.get_attribute();
      auto v = attr.get_value<std::string>();
      TEST_CHECK(v.has_value());
      if (v) {
        TEST_CHECK(v.value().empty());
      }
    }
  }
}

// ===========================================================================
// Error Handling (2)
// ===========================================================================

void usda_reader_malformed_input_test(void) {
  // Missing header
  {
    const char *usda = "def Scope \"test\" { }";
    Stage stage;
    std::string warn, err;
    bool ok = parse_usda(usda, &stage, &warn, &err);
    TEST_CHECK(!ok);
  }

  // Invalid type name
  {
    const char *usda = R"(#usda 1.0
def InvalidTypeName123 "test" {
}
)";
    Stage stage;
    std::string warn, err;
    bool ok = parse_usda(usda, &stage, &warn, &err);
    // Parser may accept unknown types as Model prim, so just verify no crash
    (void)ok;
    TEST_CHECK(true);
  }

  // Empty input
  {
    const char *usda = "";
    Stage stage;
    std::string warn, err;
    bool ok = parse_usda(usda, &stage, &warn, &err);
    TEST_CHECK(!ok);
  }

  // Null bytes (just a single null)
  {
    Stage stage;
    std::string warn, err;
    uint8_t data[1] = {0};
    bool ok = LoadUSDAFromMemory(data, 1, "test.usda", &stage, &warn, &err);
    TEST_CHECK(!ok);
  }
}

void usda_reader_large_nesting_depth_test(void) {
  // Build a 50-level deep hierarchy
  std::string usda = "#usda 1.0\n\n";
  for (int i = 0; i < 50; i++) {
    for (int j = 0; j < i; j++) usda += "    ";
    usda += "def Scope \"level" + std::to_string(i) + "\" {\n";
  }
  for (int i = 49; i >= 0; i--) {
    for (int j = 0; j < i; j++) usda += "    ";
    usda += "}\n";
  }

  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda.c_str(), &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  // Verify we can reach the deepest level
  std::string path = "";
  for (int i = 0; i < 50; i++) {
    path += "/level" + std::to_string(i);
  }
  auto result = stage.GetPrimAtPath(Path(path, ""));
  TEST_CHECK(bool(result));
}
