// SPDX-License-Identifier: Apache 2.0
// USDA roundtrip test: parse USDA, export to string, re-parse, compare via JSON

#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-usda-roundtrip.h"
#include "tinyusdz.hh"
#include "usd-to-json.hh"

#include <iostream>
#include <sstream>

using namespace tinyusdz;

namespace {

// Helper function: parse USDA string and return Stage
static bool parseUSDString(const std::string &usd_content, Stage *stage,
                           std::string *warn, std::string *err) {
  return LoadUSDFromMemory(
      reinterpret_cast<const uint8_t *>(usd_content.data()),
      usd_content.size(),
      "memory.usda",
      stage,
      warn,
      err);
}

// Helper function: perform roundtrip test
// Returns true if roundtrip succeeds and JSON comparison matches
static bool doRoundtripTest(const std::string &usd_content,
                            std::string *err_msg = nullptr) {
  // Step 1: Parse original USDA
  Stage stage1;
  std::string warn1, err1;
  bool ret1 = parseUSDString(usd_content, &stage1, &warn1, &err1);
  if (!ret1) {
    if (err_msg) *err_msg = "Failed to parse original USDA: " + err1;
    return false;
  }

  // Step 2: Export to string
  std::string exported = stage1.ExportToString();
  if (exported.empty()) {
    if (err_msg) *err_msg = "ExportToString returned empty string";
    return false;
  }

  // Step 3: Re-parse the exported string
  Stage stage2;
  std::string warn2, err2;
  bool ret2 = parseUSDString(exported, &stage2, &warn2, &err2);
  if (!ret2) {
    if (err_msg) {
      *err_msg = "Failed to re-parse exported USDA: " + err2 + "\n";
      *err_msg += "Exported content:\n" + exported;
    }
    return false;
  }

  // Step 4: Convert both stages to JSON and compare
#if defined(TINYUSDZ_WITH_JSON)
  USDToJSONOptions options;

  auto json1_result = ToJSON(stage1, options);
  if (!json1_result) {
    if (err_msg) *err_msg = "Failed to convert stage1 to JSON: " + json1_result.error();
    return false;
  }

  auto json2_result = ToJSON(stage2, options);
  if (!json2_result) {
    if (err_msg) *err_msg = "Failed to convert stage2 to JSON: " + json2_result.error();
    return false;
  }

  std::string json1 = json1_result.value();
  std::string json2 = json2_result.value();

  if (json1 != json2) {
    if (err_msg) {
      *err_msg = "JSON mismatch after roundtrip.\n";
      *err_msg += "Original JSON:\n" + json1 + "\n";
      *err_msg += "Roundtrip JSON:\n" + json2 + "\n";
    }
    return false;
  }
#else
  // Without JSON support, just verify that re-parsing succeeds
  // and both stages have the same number of root prims
  if (stage1.root_prims().size() != stage2.root_prims().size()) {
    if (err_msg) {
      std::ostringstream oss;
      oss << "Root prim count mismatch: " << stage1.root_prims().size()
          << " vs " << stage2.root_prims().size();
      *err_msg = oss.str();
    }
    return false;
  }
#endif

  return true;
}

} // anonymous namespace

//
// Test cases
//

void usda_roundtrip_basic_test(void) {
  std::string err;

  // Test 1: Empty stage with just header
  {
    std::string usd = R"(#usda 1.0
)";
    bool ret = doRoundtripTest(usd, &err);
    if (!ret) {
      TEST_MSG("Empty stage test failed: %s", err.c_str());
    }
    TEST_CHECK(ret == true);
  }

  // Test 2: Simple def Xform
  {
    std::string usd = R"(#usda 1.0

def Xform "Root"
{
}
)";
    bool ret = doRoundtripTest(usd, &err);
    if (!ret) {
      TEST_MSG("Simple Xform test failed: %s", err.c_str());
    }
    TEST_CHECK(ret == true);
  }

  // Test 3: Nested hierarchy
  {
    std::string usd = R"(#usda 1.0

def Xform "Root"
{
    def Xform "Child1"
    {
        def Xform "GrandChild"
        {
        }
    }

    def Xform "Child2"
    {
    }
}
)";
    bool ret = doRoundtripTest(usd, &err);
    if (!ret) {
      TEST_MSG("Nested hierarchy test failed: %s", err.c_str());
    }
    TEST_CHECK(ret == true);
  }
}

void usda_roundtrip_xform_test(void) {
  std::string err;

  // Test with xformOps
  {
    std::string usd = R"(#usda 1.0

def Xform "Root"
{
    double3 xformOp:translate = (1.0, 2.0, 3.0)
    double3 xformOp:scale = (1.5, 1.5, 1.5)
    float3 xformOp:rotateXYZ = (0.0, 45.0, 0.0)
    uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ", "xformOp:scale"]
}
)";
    bool ret = doRoundtripTest(usd, &err);
    if (!ret) {
      TEST_MSG("XformOps test failed: %s", err.c_str());
    }
    TEST_CHECK(ret == true);
  }

  // Test with matrix transform
  {
    std::string usd = R"(#usda 1.0

def Xform "MatrixXform"
{
    matrix4d xformOp:transform = ( (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (10, 20, 30, 1) )
    uniform token[] xformOpOrder = ["xformOp:transform"]
}
)";
    bool ret = doRoundtripTest(usd, &err);
    if (!ret) {
      TEST_MSG("Matrix transform test failed: %s", err.c_str());
    }
    TEST_CHECK(ret == true);
  }
}

void usda_roundtrip_mesh_test(void) {
  std::string err;

  // Simple triangle mesh
  {
    std::string usd = R"(#usda 1.0

def Mesh "Triangle"
{
    point3f[] points = [(0, 0, 0), (1, 0, 0), (0.5, 1, 0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 2]
}
)";
    bool ret = doRoundtripTest(usd, &err);
    if (!ret) {
      TEST_MSG("Triangle mesh test failed: %s", err.c_str());
    }
    TEST_CHECK(ret == true);
  }

  // Cube mesh with normals
  {
    std::string usd = R"(#usda 1.0

def Mesh "Cube"
{
    point3f[] points = [
        (-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1),
        (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)
    ]
    int[] faceVertexCounts = [4, 4, 4, 4, 4, 4]
    int[] faceVertexIndices = [
        0, 1, 2, 3,
        4, 7, 6, 5,
        0, 4, 5, 1,
        1, 5, 6, 2,
        2, 6, 7, 3,
        3, 7, 4, 0
    ]
    normal3f[] normals = [
        (0, 0, -1), (0, 0, 1), (0, -1, 0),
        (1, 0, 0), (0, 1, 0), (-1, 0, 0)
    ]
    uniform token subdivisionScheme = "none"
}
)";
    bool ret = doRoundtripTest(usd, &err);
    if (!ret) {
      TEST_MSG("Cube mesh test failed: %s", err.c_str());
    }
    TEST_CHECK(ret == true);
  }
}

void usda_roundtrip_material_test(void) {
  std::string err;

  // UsdPreviewSurface material
  {
    std::string usd = R"(#usda 1.0

def Material "SimpleMaterial"
{
    token outputs:surface.connect = </SimpleMaterial/PreviewSurface.outputs:surface>

    def Shader "PreviewSurface"
    {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor = (0.8, 0.2, 0.1)
        float inputs:metallic = 0.0
        float inputs:roughness = 0.5
        token outputs:surface
    }
}
)";
    bool ret = doRoundtripTest(usd, &err);
    if (!ret) {
      TEST_MSG("Material test failed: %s", err.c_str());
    }
    TEST_CHECK(ret == true);
  }
}

void usda_roundtrip_timesamples_test(void) {
  std::string err;

  // Simple time-sampled animation
  {
    std::string usd = R"(#usda 1.0
(
    startTimeCode = 1
    endTimeCode = 10
)

def Xform "AnimatedXform"
{
    double3 xformOp:translate.timeSamples = {
        1: (0, 0, 0),
        5: (5, 0, 0),
        10: (10, 0, 0),
    }
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
)";
    bool ret = doRoundtripTest(usd, &err);
    if (!ret) {
      TEST_MSG("Timesamples test failed: %s", err.c_str());
    }
    TEST_CHECK(ret == true);
  }

  // Animated visibility
  {
    std::string usd = R"(#usda 1.0

def Xform "VisibilityAnim"
{
    token visibility.timeSamples = {
        1: "inherited",
        5: "invisible",
        10: "inherited",
    }
}
)";
    bool ret = doRoundtripTest(usd, &err);
    if (!ret) {
      TEST_MSG("Visibility animation test failed: %s", err.c_str());
    }
    TEST_CHECK(ret == true);
  }
}
