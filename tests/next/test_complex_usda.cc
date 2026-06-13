// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Complex USDA parsing test

#include <iostream>
#include <cassert>
#include <string>

#include "next/reader/usda-reader.hh"
#include "next/writer/usda-writer.hh"

using namespace tinyusdz::next;

void test_cube_with_material() {
  std::cout << "Testing complex cube with material..." << std::endl;

  const char* input = R"(#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
    metersPerUnit = 0.01
    timeCodesPerSecond = 24
    startTimeCode = 0
    endTimeCode = 100
)

def Xform "World" (
    kind = "group"
)
{
    def Mesh "Cube" (
        active = true
    )
    {
        uniform bool doubleSided = false
        float3[] extent = [(-0.5, -0.5, -0.5), (0.5, 0.5, 0.5)]
        int[] faceVertexCounts = [4, 4, 4, 4, 4, 4]
        int[] faceVertexIndices = [0, 1, 3, 2, 2, 3, 5, 4, 4, 5, 7, 6, 6, 7, 1, 0, 1, 7, 5, 3, 6, 0, 2, 4]
        normal3f[] normals = [(0, 0, 1), (0, 0, 1), (0, 0, 1), (0, 0, 1), (0, 1, 0), (0, 1, 0), (0, 1, 0), (0, 1, 0), (0, 0, -1), (0, 0, -1), (0, 0, -1), (0, 0, -1), (0, -1, 0), (0, -1, 0), (0, -1, 0), (0, -1, 0), (1, 0, 0), (1, 0, 0), (1, 0, 0), (1, 0, 0), (-1, 0, 0), (-1, 0, 0), (-1, 0, 0), (-1, 0, 0)]
        point3f[] points = [(-0.5, -0.5, 0.5), (0.5, -0.5, 0.5), (-0.5, 0.5, 0.5), (0.5, 0.5, 0.5), (-0.5, 0.5, -0.5), (0.5, 0.5, -0.5), (-0.5, -0.5, -0.5), (0.5, -0.5, -0.5)]
        texCoord2f[] primvars:st = [(0, 0), (1, 0), (1, 1), (0, 1), (0, 0), (1, 0), (1, 1), (0, 1), (0, 0), (1, 0), (1, 1), (0, 1), (0, 0), (1, 0), (1, 1), (0, 1), (0, 0), (1, 0), (1, 1), (0, 1), (0, 0), (1, 0), (1, 1), (0, 1)]

        rel material:binding = </World/Materials/Red>
    }

    def Scope "Materials" {
        def Material "Red" {
            token outputs:surface.connect = </World/Materials/Red/Surface.outputs:surface>

            def Shader "Surface" {
                uniform token info:id = "UsdPreviewSurface"
                color3f inputs:diffuseColor = (1, 0, 0)
                float inputs:roughness = 0.5
                float inputs:metallic = 0
                token outputs:surface
            }
        }
    }
}
)";

  LoadResult result = LoadUSDAFromString(input);

  if (!result.success) {
    std::cerr << "Parse failed: " << result.error_summary << std::endl;
    for (const auto& err : result.errors) {
      std::cerr << "  Line " << err.line << ": " << err.message << std::endl;
    }
    assert(false);
  }

  // Verify stage metadata
  assert(result.stage.GetMeta().defaultPrim == "World");
  assert(result.stage.GetMeta().upAxis == "Y");
  assert(result.stage.GetMeta().metersPerUnit == 0.01);
  assert(result.stage.GetMeta().timeCodesPerSecond == 24);
  assert(result.stage.GetMeta().startTimeCode == 0);
  assert(result.stage.GetMeta().endTimeCode == 100);

  // Verify structure
  std::vector<UsdPrim> roots = result.stage.GetRootPrims();
  assert(roots.size() == 1);
  const UsdPrim& world = roots[0];
  assert(world.GetName() == "World");
  assert(world.GetTypeName() == "Xform");
  assert(world.GetChildCount() == 2);  // Cube and Materials

  // Find Cube
  UsdPrim cube = world.GetChild("Cube");
  assert(cube);
  assert(cube.GetTypeName() == "Mesh");
  assert(cube.HasProperty("points"));
  assert(cube.HasProperty("faceVertexCounts"));
  assert(cube.HasProperty("faceVertexIndices"));
  assert(cube.HasProperty("normals"));

  // Check relationship
  const std::vector<Path>* binding = cube.GetRelationship("material:binding");
  assert(binding != nullptr);
  assert(binding->size() == 1);
  assert((*binding)[0].str() == "/World/Materials/Red");

  // Find Materials
  UsdPrim materials = world.GetChild("Materials");
  assert(materials);
  assert(materials.GetTypeName() == "Scope");

  // Find Red material
  UsdPrim red = materials.GetChild("Red");
  assert(red);
  assert(red.GetTypeName() == "Material");

  // Find Surface shader
  UsdPrim surface = red.GetChild("Surface");
  assert(surface);
  assert(surface.GetTypeName() == "Shader");
  assert(surface.HasProperty("inputs:diffuseColor"));
  assert(surface.HasProperty("inputs:roughness"));

  std::cout << "  Complex USDA test passed!" << std::endl;
}

void test_animation() {
  std::cout << "Testing animation with timeSamples..." << std::endl;

  const char* input = R"(#usda 1.0

def Xform "Cube" {
    double3 xformOp:translate.timeSamples = {
        0: (0, 0, 0),
        24: (10, 0, 0),
        48: (10, 10, 0),
        72: (0, 10, 0),
        96: (0, 0, 0),
    }
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
)";

  LoadResult result = LoadUSDAFromString(input);

  if (!result.success) {
    std::cerr << "Parse failed: " << result.error_summary << std::endl;
    assert(false);
  }

  std::vector<UsdPrim> roots = result.stage.GetRootPrims();
  assert(roots.size() == 1);
  const UsdPrim& cube = roots[0];
  assert(cube.GetName() == "Cube");

  assert(cube.HasTimeSamples("xformOp:translate"));

  std::vector<double> times = cube.GetTimeSampleTimes("xformOp:translate");
  assert(times.size() == 5);
  assert(times[0] == 0);
  assert(times[1] == 24);
  assert(times[4] == 96);

  std::cout << "  Animation test passed!" << std::endl;
}

void test_large_scene_ascii_grammar_regressions() {
  std::cout << "Testing large-scene ASCII grammar regressions..." << std::endl;

  const char* input = R"USD(#usda 1.0

def Xform "World" {
    def Mesh "Mesh" (
        customData = {
            bool AutoCreated = 1
        }
    )
    {
        point3f[] points = [(0, 0, 0), (1, 0, 0)] (
            interpolation = "vertex"
        )
        int[] primvars:height:indices = None
        texCoord3f[] primvars:uv = [(12.5, -8.0, 960.5)] (
            interpolation = "constant"
        )
        quath[] orientations = [(0.64209, -0.0053215, -0.766602, 0.000286341)]
        custom bool shadow:enable = 0
        bool[] shadow:flags = [true, false, 1, 0]
        token[] primvars:tags = [leaf, "branch"]
        string[] sourceSnippets = ['''line one
line two
''']
        half scalarHalf = 0.5
        point3h halfPoint = (1, 2, 3)
        quath halfOrientation = (0, 0, 0, 1)
        matrix2f uvMatrix = ((1, 0), (0, 1))
        matrix4f localMatrix = ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1))
        matrix2d uvMatrixD = ((1, 0), (0, 1))
        uint3 objectIds = (1, 2, 3)
        float3[] extent.timeSamples = {
            1: [(-1, -1, -1), (1, 1, 1)],
        }
    }

    def Shader "Shader"
    {
        uniform string info:sourceCode = '''#ifndef VOP_SHADING
#define VOP_SHADING
#endif
'''
    }
}
)USD";

  LoadResult result = LoadUSDAFromString(input);
  if (!result.success) {
    std::cerr << "Parse failed: " << result.error_summary << std::endl;
    for (const auto& err : result.errors) {
      std::cerr << "  Line " << err.line << ": " << err.message << std::endl;
    }
    assert(false);
  }

  std::vector<UsdPrim> roots = result.stage.GetRootPrims();
  assert(roots.size() == 1);
  const UsdPrim& world = roots[0];
  UsdPrim mesh = world.GetChild("Mesh");
  assert(mesh);
  assert(mesh.HasProperty("points"));
  assert(mesh.HasProperty("primvars:height:indices"));
  assert(mesh.HasProperty("primvars:uv"));
  assert(mesh.HasProperty("orientations"));
  assert(mesh.HasProperty("shadow:enable"));
  assert(mesh.HasProperty("shadow:flags"));
  assert(mesh.HasProperty("primvars:tags"));
  assert(mesh.HasProperty("sourceSnippets"));
  assert(mesh.HasProperty("scalarHalf"));
  assert(mesh.HasProperty("halfPoint"));
  assert(mesh.HasProperty("halfOrientation"));
  assert(mesh.HasProperty("uvMatrix"));
  assert(mesh.HasProperty("localMatrix"));
  assert(mesh.HasProperty("uvMatrixD"));
  assert(mesh.HasProperty("objectIds"));

  assert(mesh.HasTimeSamples("extent"));
  assert(mesh.GetTimeSampleTimes("extent").size() == 1);

  UsdPrim shader = world.GetChild("Shader");
  assert(shader);
  assert(shader.HasProperty("info:sourceCode"));

  std::cout << "  Large-scene ASCII grammar regression test passed!" << std::endl;
}

// Parser+writer parity: the new metadata / dictionary / property-metadata / value
// features must parse, survive a USDA write, and re-parse to the same facts.
void test_metadata_dict_propmeta_roundtrip() {
  std::cout << "Testing metadata/dict/property-metadata round-trip..." << std::endl;

  const char* input = R"USD(#usda 1.0
(
    defaultPrim = "W"
    framesPerSecond = 30
    customLayerData = {
        string author = "syoyo"
        dictionary nested = {
            int version = 2
        }
    }
)

def Xform "W" (
    kind = "component"
    displayName = "My World"
    instanceable = true
    customData = {
        string note = "hello"
        bool flag = true
    }
)
{
    token[] tags = ["alpha", "beta", "gamma"]
    string[] names = ["x", "y"]
    float farPlane = inf
    float nearPlane = -inf
    color3f[] primvars:displayColor = [(1, 0, 0)] (
        interpolation = "constant"
        elementSize = 1
    )
}
)USD";

  auto check = [](const Stage& stage) {
    std::vector<UsdPrim> roots = stage.GetRootPrims();
    assert(roots.size() == 1);
    const UsdPrim& w = roots[0];
    // Prim metadata
    assert(w.GetMeta().kind() == "component");
    assert(w.GetMeta().displayName() == "My World");
    assert(w.GetMeta().instanceable == true);
    // customData dictionary
    assert(w.GetMeta().customData().is_dictionary());
    const Dict* cd = w.GetMeta().customData().as_dictionary();
    assert(cd && cd->find("note") && cd->find("note")->as_string());
    assert(*cd->find("note")->as_string() == "hello");
    // String/token arrays carry real values (regression for the data-loss bug)
    const Value* tags = w.GetPropertyValue("tags");
    assert(tags && tags->as_token_array());
    assert(tags->as_token_array()->size() == 3);
    assert((*tags->as_token_array())[1] == "beta");
    const Value* names = w.GetPropertyValue("names");
    assert(names && names->as_token_array() &&
           names->as_token_array()->size() == 2);
  };

  LoadResult r1 = LoadUSDAFromString(input);
  if (!r1.success) {
    std::cerr << "Parse failed: " << r1.error_summary << std::endl;
    assert(false);
  }
  check(r1.stage);

  // Write it back out and confirm the new opinions are emitted.
  std::string out = WriteUSDAToString(r1.stage);
  assert(out.find("kind = \"component\"") != std::string::npos);
  assert(out.find("displayName = \"My World\"") != std::string::npos);
  assert(out.find("instanceable = true") != std::string::npos);
  assert(out.find("framesPerSecond = 30") != std::string::npos);
  assert(out.find("customData = {") != std::string::npos);
  assert(out.find("customLayerData = {") != std::string::npos);
  assert(out.find("\"alpha\", \"beta\", \"gamma\"") != std::string::npos);
  assert(out.find("interpolation = \"constant\"") != std::string::npos);
  assert(out.find("inf") != std::string::npos);
  assert(out.find("-inf") != std::string::npos);

  // Re-parse the written USDA and confirm the same facts survive a round-trip.
  LoadResult r2 = LoadUSDAFromString(out.c_str());
  if (!r2.success) {
    std::cerr << "Re-parse failed: " << r2.error_summary << std::endl;
    std::cerr << out << std::endl;
    assert(false);
  }
  check(r2.stage);

  std::cout << "  Metadata/dict/property-metadata round-trip passed!" << std::endl;
}

int main() {
  std::cout << "=== TinyUSDZ Next Complex USDA Tests ===" << std::endl;
  std::cout << std::endl;

  try {
    test_cube_with_material();
    test_animation();
    test_large_scene_ascii_grammar_regressions();
    test_metadata_dict_propmeta_roundtrip();

    std::cout << std::endl;
    std::cout << "All complex USDA tests passed!" << std::endl;
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }
}
