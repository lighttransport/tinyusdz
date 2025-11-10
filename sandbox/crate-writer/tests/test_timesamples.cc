// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Test TimeSamples (animated attributes) in USD Crate Writer

#include <iostream>
#include <string>
#include <cmath>

#include "../../../src/tinyusdz.hh"
#include "../../../src/prim-types.hh"
#include "../../../src/value-types.hh"
#include "../../../src/timesamples.hh"
#include "../src/crate-writer.hh"

using namespace tinyusdz;
using namespace tinyusdz::experimental;

// Test 1: Simple TimeSamples with scalar values (animating transform)
bool TestSimpleTransformAnimation(const std::string& filename) {
  std::cout << "\n=== Test 1: Simple Transform Animation ===" << std::endl;
  std::cout << "Creating: " << filename << std::endl;

  // Create a Stage with an animated Xform
  Stage stage;

  // Create Xform prim
  Xform xform;
  xform.name = "AnimatedCube";
  xform.spec = Specifier::Def;

  // Create translate xformOp with TimeSamples
  XformOp translate_op;
  translate_op.op_type = XformOp::OpType::Translate;

  // Create TimeSamples for the translate operation (float3)
  value::TimeSamples ts;

  // Add samples for frames 1-5
  for (int frame = 1; frame <= 5; frame++) {
    double time = static_cast<double>(frame);
    value::float3 position(static_cast<float>(frame), 0.0f, 0.0f);
    value::Value pos_value(position);

    ts.add_sample(time, pos_value);
    std::cout << "  Frame " << frame << ": translate = (" << position[0] << ", "
              << position[1] << ", " << position[2] << ")" << std::endl;
  }

  // Set the TimeSamples to the xformOp's PrimVar
  translate_op._var._ts = ts;

  // Also set default value for frame 0
  value::float3 default_pos(0.0f, 0.0f, 0.0f);
  translate_op._var._value = value::Value(default_pos);

  // Add xformOp to the Xform
  xform.xformOps.push_back(translate_op);

  // Build xformOpOrder
  xform.xformOps[0].suffix = "";
  value::token op_token("xformOp:translate");
  xform.set_xformOpOrder({op_token});

  // Add Xform to stage
  Prim prim(xform);
  prim.element_name() = "AnimatedCube";
  prim.spec() = Specifier::Def;

  // Write to USD Crate
  std::string err;
  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;

  if (!writer.Open(opts, &err)) {
    std::cerr << "Failed to open writer: " << err << std::endl;
    return false;
  }

  // Convert Stage to Crate
  if (!writer.Convert StageToSpecs(stage, &err)) {
    std::cerr << "Failed to convert stage: " << err << std::endl;
    return false;
  }

  writer.Close();

  std::cout << "✓ Created: " << filename << std::endl;
  return true;
}

// Test 2: TimeSamples with float3 array (animating mesh points)
bool TestMeshPointsAnimation(const std::string& filename) {
  std::cout << "\n=== Test 2: Mesh Points Animation ===" << std::endl;
  std::cout << "Creating: " << filename << std::endl;

  // Create a Stage with an animated Mesh
  Stage stage;

  // Create Mesh prim
  GeomMesh mesh;
  mesh.name = "AnimatedMesh";

  // Create base mesh geometry (a simple quad)
  std::vector<int32_t> faceVertexCounts = {4};  // 1 quad
  std::vector<int32_t> faceVertexIndices = {0, 1, 2, 3};

  // Set default (static) geometry
  mesh.faceVertexCounts = Animatable<std::vector<int32_t>>(faceVertexCounts);
  mesh.faceVertexIndices = Animatable<std::vector<int32_t>>(faceVertexIndices);

  // Create TimeSamples for points (animated vertices)
  value::TimeSamples points_ts;

  // Add samples for frames 0-4 (simple wave animation)
  for (int frame = 0; frame < 5; frame++) {
    double time = static_cast<double>(frame);
    float offset = std::sin(frame * 0.5f) * 0.5f;  // Wave offset

    std::vector<value::point3f> points = {
      value::point3f(-1.0f, offset, -1.0f),
      value::point3f( 1.0f, offset,  -1.0f),
      value::point3f( 1.0f, offset, 1.0f),
      value::point3f(-1.0f, offset, 1.0f)
    };

    value::Value points_value(points);
    points_ts.add_sample(time, points_value);

    std::cout << "  Frame " << frame << ": y-offset = " << offset << std::endl;
  }

  // Set TimeSamples to the points attribute
  // Note: This requires creating a Property with TimeSamples
  Attribute points_attr;
  points_attr.set_type_name("point3f[]");
  points_attr.set_var(primvar::PrimVar());  // Initialize PrimVar
  points_attr.get_var()._ts = points_ts;

  // Set default value for frame 0
  std::vector<value::point3f> default_points = {
    value::point3f(-1.0f, 0.0f, -1.0f),
    value::point3f( 1.0f, 0.0f, -1.0f),
    value::point3f( 1.0f, 0.0f, 1.0f),
    value::point3f(-1.0f, 0.0f, 1.0f)
  };
  points_attr.get_var()._value = value::Value(default_points);

  // Add points attribute to mesh
  Property points_prop;
  points_prop.set_property(points_attr);
  mesh.props["points"] = points_prop;

  // Add Mesh to stage
  Prim prim(mesh);
  prim.element_name() = "AnimatedMesh";
  prim.spec() = Specifier::Def;

  // Write to USD Crate
  std::string err;
  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;

  if (!writer.Open(opts, &err)) {
    std::cerr << "Failed to open writer: " << err << std::endl;
    return false;
  }

  // Convert Stage to Crate
  if (!writer.ConvertStageToSpecs(stage, &err)) {
    std::cerr << "Failed to convert stage: " << err << std::endl;
    return false;
  }

  writer.Close();

  std::cout << "✓ Created: " << filename << std::endl;
  return true;
}

// Test 3: Multiple animated attributes on single prim
bool TestMultipleAnimatedAttributes(const std::string& filename) {
  std::cout << "\n=== Test 3: Multiple Animated Attributes ===" << std::endl;
  std::cout << "Creating: " << filename << std::endl;

  // Create a Stage with multiple animated properties
  Stage stage;

  // Create Xform with both translate and rotateZ animated
  Xform xform;
  xform.name = "ComplexAnim";
  xform.spec = Specifier::Def;

  // Animated translate
  {
    XformOp translate_op;
    translate_op.op_type = XformOp::OpType::Translate;

    value::TimeSamples ts;
    for (int frame = 0; frame < 10; frame++) {
      double time = static_cast<double>(frame);
      float x = std::cos(frame * 0.3f) * 2.0f;
      float z = std::sin(frame * 0.3f) * 2.0f;
      value::float3 position(x, 0.0f, z);
      ts.add_sample(time, value::Value(position));
    }

    translate_op._var._ts = ts;
    translate_op._var._value = value::Value(value::float3(0.0f, 0.0f, 0.0f));
    xform.xformOps.push_back(translate_op);
  }

  // Animated rotateZ
  {
    XformOp rotate_op;
    rotate_op.op_type = XformOp::OpType::RotateZ;

    value::TimeSamples ts;
    for (int frame = 0; frame < 10; frame++) {
      double time = static_cast<double>(frame);
      float angle = frame * 36.0f;  // 36 degrees per frame (full rotation in 10 frames)
      ts.add_sample(time, value::Value(angle));
    }

    rotate_op._var._ts = ts;
    rotate_op._var._value = value::Value(0.0f);
    xform.xformOps.push_back(rotate_op);
  }

  // Set xformOpOrder
  xform.set_xformOpOrder({
    value::token("xformOp:translate"),
    value::token("xformOp:rotateZ")
  });

  // Add to stage
  Prim prim(xform);
  prim.element_name() = "ComplexAnim";
  prim.spec() = Specifier::Def;

  // Write to USD Crate
  std::string err;
  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;

  if (!writer.Open(opts, &err)) {
    std::cerr << "Failed to open writer: " << err << std::endl;
    return false;
  }

  if (!writer.ConvertStageToSpecs(stage, &err)) {
    std::cerr << "Failed to convert stage: " << err << std::endl;
    return false;
  }

  writer.Close();

  std::cout << "✓ Created: " << filename << " (circular motion with rotation)" << std::endl;
  return true;
}

int main(int argc, char** argv) {
  std::cout << "====================================" << std::endl;
  std::cout << "USD Crate Writer - TimeSamples Tests" << std::endl;
  std::cout << "====================================" << std::endl;

  bool all_pass = true;

  // Test 1: Simple transform animation
  if (!TestSimpleTransformAnimation("/tmp/test_anim_transform.usdc")) {
    std::cerr << "✗ Test 1 FAILED: Simple Transform Animation" << std::endl;
    all_pass = false;
  }

  // Test 2: Mesh points animation
  if (!TestMeshPointsAnimation("/tmp/test_anim_mesh.usdc")) {
    std::cerr << "✗ Test 2 FAILED: Mesh Points Animation" << std::endl;
    all_pass = false;
  }

  // Test 3: Multiple animated attributes
  if (!TestMultipleAnimatedAttributes("/tmp/test_anim_complex.usdc")) {
    std::cerr << "✗ Test 3 FAILED: Multiple Animated Attributes" << std::endl;
    all_pass = false;
  }

  std::cout << "\n====================================" << std::endl;
  if (all_pass) {
    std::cout << "✓ All TimeSamples tests PASSED" << std::endl;
    std::cout << "\nVerify with:" << std::endl;
    std::cout << "  usdcat /tmp/test_anim_transform.usdc" << std::endl;
    std::cout << "  usdcat /tmp/test_anim_mesh.usdc" << std::endl;
    std::cout << "  usdcat /tmp/test_anim_complex.usdc" << std::endl;
    return 0;
  } else {
    std::cout << "✗ Some TimeSamples tests FAILED" << std::endl;
    return 1;
  }
}
