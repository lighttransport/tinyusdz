// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Complex USDA parsing test

#include <iostream>
#include <cassert>

#include "next/reader/usda-reader.hh"

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
  assert(result.stage.default_prim == "World");
  assert(result.stage.up_axis == "Y");
  assert(result.stage.meters_per_unit == 0.01);
  assert(result.stage.time_codes_per_second == 24);
  assert(result.stage.start_time_code == 0);
  assert(result.stage.end_time_code == 100);

  // Verify structure
  assert(result.stage.root_prims.size() == 1);
  const Prim& world = result.stage.root_prims[0];
  assert(world.name() == "World");
  assert(world.type_name() == "Xform");
  assert(world.child_count() == 2);  // Cube and Materials

  // Find Cube
  const Prim* cube = world.find_child("Cube");
  assert(cube != nullptr);
  assert(cube->type_name() == "Mesh");
  assert(cube->has_attribute("points"));
  assert(cube->has_attribute("faceVertexCounts"));
  assert(cube->has_attribute("faceVertexIndices"));
  assert(cube->has_attribute("normals"));

  // Check relationship
  const std::vector<Path>* binding = cube->get_relationship("material:binding");
  assert(binding != nullptr);
  assert(binding->size() == 1);
  assert((*binding)[0].str() == "/World/Materials/Red");

  // Find Materials
  const Prim* materials = world.find_child("Materials");
  assert(materials != nullptr);
  assert(materials->type_name() == "Scope");

  // Find Red material
  const Prim* red = materials->find_child("Red");
  assert(red != nullptr);
  assert(red->type_name() == "Material");

  // Find Surface shader
  const Prim* surface = red->find_child("Surface");
  assert(surface != nullptr);
  assert(surface->type_name() == "Shader");
  assert(surface->has_attribute("inputs:diffuseColor"));
  assert(surface->has_attribute("inputs:roughness"));

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

  assert(result.stage.root_prims.size() == 1);
  const Prim& cube = result.stage.root_prims[0];
  assert(cube.name() == "Cube");

  const Attribute* translate = cube.get_attribute("xformOp:translate");
  assert(translate != nullptr);
  assert(translate->has_time_samples());

  std::vector<double> times = translate->sample_times();
  assert(times.size() == 5);
  assert(times[0] == 0);
  assert(times[1] == 24);
  assert(times[4] == 96);

  std::cout << "  Animation test passed!" << std::endl;
}

int main() {
  std::cout << "=== TinyUSDZ Next Complex USDA Tests ===" << std::endl;
  std::cout << std::endl;

  try {
    test_cube_with_material();
    test_animation();

    std::cout << std::endl;
    std::cout << "All complex USDA tests passed!" << std::endl;
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }
}
