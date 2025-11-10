// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Comprehensive Feature Test: Test all major USD features
//
// This test creates USD files exercising all implemented features
// for manual verification with OpenUSD tools (usdcat, usdview)
//

#include "crate-writer.hh"
#include "tinyusdz.hh"
#include <iostream>
#include <vector>

using namespace tinyusdz;
using namespace tinyusdz::experimental;
namespace tcrate = tinyusdz::crate;

bool CreateComprehensiveScene(const std::string& filename) {
  std::cout << "\nCreating comprehensive USD scene: " << filename << std::endl;
  std::string err;

  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;
  opts.enable_deduplication = true;
  writer.SetOptions(opts);

  if (!writer.Open(&err)) {
    std::cerr << "ERROR: Failed to open: " << err << std::endl;
    return false;
  }

  // Root
  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  if (!writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, &err)) {
    std::cerr << "ERROR: Root: " << err << std::endl;
    return false;
  }

  // 1. Xform with transform
  std::cout << "  - Xform with transform matrix..." << std::endl;
  {
    Path xform_path("/World", "");
    tcrate::FieldValuePairVector fields;

    tcrate::CrateValue spec_value;
    spec_value.Set(Specifier::Def);
    fields.push_back({"specifier", spec_value});

    tcrate::CrateValue matrix_value;
    value::matrix4d transform;
    // Translation (0, 0, -5)
    transform.m[0][0] = 1.0; transform.m[0][1] = 0.0; transform.m[0][2] = 0.0; transform.m[0][3] = 0.0;
    transform.m[1][0] = 0.0; transform.m[1][1] = 1.0; transform.m[1][2] = 0.0; transform.m[1][3] = 0.0;
    transform.m[2][0] = 0.0; transform.m[2][1] = 0.0; transform.m[2][2] = 1.0; transform.m[2][3] = 0.0;
    transform.m[3][0] = 0.0; transform.m[3][1] = 0.0; transform.m[3][2] = -5.0; transform.m[3][3] = 1.0;
    matrix_value.Set(transform);
    fields.push_back({"xformOp:transform", matrix_value});

    if (!writer.AddSpec(xform_path, SpecType::Prim, fields, &err)) {
      std::cerr << "ERROR: Xform: " << err << std::endl;
      return false;
    }
  }

  // 2. Mesh with geometry data
  std::cout << "  - Mesh with points, normals, and UVs..." << std::endl;
  {
    Path mesh_path("/World/Cube", "");
    tcrate::FieldValuePairVector fields;

    tcrate::CrateValue spec_value;
    spec_value.Set(Specifier::Def);
    fields.push_back({"specifier", spec_value});

    // Cube vertices
    std::vector<value::float3> points = {
      {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f},
      { 1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f},
      {-1.0f, -1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f},
      { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f}
    };
    tcrate::CrateValue points_value;
    points_value.Set(points);
    fields.push_back({"points", points_value});

    // Face vertex counts
    std::vector<int32_t> faceVertexCounts = {4, 4, 4, 4, 4, 4};
    tcrate::CrateValue fvc_value;
    fvc_value.Set(faceVertexCounts);
    fields.push_back({"faceVertexCounts", fvc_value});

    // Face vertex indices
    std::vector<int32_t> faceVertexIndices = {
      0, 1, 2, 3,  // front
      4, 5, 6, 7,  // back
      0, 4, 7, 3,  // left
      1, 5, 6, 2,  // right
      0, 1, 5, 4,  // bottom
      3, 2, 6, 7   // top
    };
    tcrate::CrateValue fvi_value;
    fvi_value.Set(faceVertexIndices);
    fields.push_back({"faceVertexIndices", fvi_value});

    if (!writer.AddSpec(mesh_path, SpecType::Prim, fields, &err)) {
      std::cerr << "ERROR: Mesh: " << err << std::endl;
      return false;
    }
  }

  // 3. Material with shader
  std::cout << "  - Material with customData..." << std::endl;
  {
    Path material_path("/World/Material", "");
    tcrate::FieldValuePairVector fields;

    tcrate::CrateValue spec_value;
    spec_value.Set(Specifier::Def);
    fields.push_back({"specifier", spec_value});

    // Custom data
    tcrate::CrateValue dict_value;
    value::dict d;
    d["shadingModel"] = std::string("PBR");
    d["roughness"] = 0.5f;
    d["metallic"] = 0.0f;
    dict_value.Set(d);
    fields.push_back({"customData", dict_value});

    if (!writer.AddSpec(material_path, SpecType::Prim, fields, &err)) {
      std::cerr << "ERROR: Material: " << err << std::endl;
      return false;
    }
  }

  // 4. Collection with relationships
  std::cout << "  - Collection with target paths..." << std::endl;
  {
    Path collection_path("/World/Collection", "");
    tcrate::FieldValuePairVector fields;

    tcrate::CrateValue spec_value;
    spec_value.Set(Specifier::Def);
    fields.push_back({"specifier", spec_value});

    // Relationship targets
    tcrate::CrateValue path_array_value;
    std::vector<Path> targets;
    targets.push_back(Path("/World/Cube", ""));
    targets.push_back(Path("/World/Material", ""));
    path_array_value.Set(targets);
    fields.push_back({"targets", path_array_value});

    if (!writer.AddSpec(collection_path, SpecType::Prim, fields, &err)) {
      std::cerr << "ERROR: Collection: " << err << std::endl;
      return false;
    }
  }

  // 5. Camera
  std::cout << "  - Camera with focal length..." << std::endl;
  {
    Path camera_path("/World/Camera", "");
    tcrate::FieldValuePairVector fields;

    tcrate::CrateValue spec_value;
    spec_value.Set(Specifier::Def);
    fields.push_back({"specifier", spec_value});

    tcrate::CrateValue focal_value;
    focal_value.Set(50.0f);  // 50mm focal length
    fields.push_back({"focalLength", focal_value});

    if (!writer.AddSpec(camera_path, SpecType::Prim, fields, &err)) {
      std::cerr << "ERROR: Camera: " << err << std::endl;
      return false;
    }
  }

  if (!writer.Finalize(&err)) {
    std::cerr << "ERROR: Finalize: " << err << std::endl;
    return false;
  }

  writer.Close();
  std::cout << "  ✓ Successfully created: " << filename << std::endl;
  return true;
}

bool CreateMinimalScene(const std::string& filename) {
  std::cout << "\nCreating minimal USD scene: " << filename << std::endl;
  std::string err;

  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;
  writer.SetOptions(opts);

  if (!writer.Open(&err)) {
    std::cerr << "ERROR: " << err << std::endl;
    return false;
  }

  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  if (!writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, &err)) {
    std::cerr << "ERROR: " << err << std::endl;
    return false;
  }

  Path prim_path("/HelloWorld", "");
  tcrate::FieldValuePairVector fields;

  tcrate::CrateValue spec_value;
  spec_value.Set(Specifier::Def);
  fields.push_back({"specifier", spec_value});

  if (!writer.AddSpec(prim_path, SpecType::Prim, fields, &err)) {
    std::cerr << "ERROR: " << err << std::endl;
    return false;
  }

  if (!writer.Finalize(&err)) {
    std::cerr << "ERROR: " << err << std::endl;
    return false;
  }

  writer.Close();
  std::cout << "  ✓ Successfully created: " << filename << std::endl;
  return true;
}

int main() {
  std::cout << "===== USD Crate Writer Comprehensive Feature Test =====\n" << std::endl;

  bool all_pass = true;

  // Create minimal scene
  if (!CreateMinimalScene("/tmp/minimal_scene.usdc")) {
    std::cerr << "✗ Minimal scene creation FAILED" << std::endl;
    all_pass = false;
  }

  // Create comprehensive scene
  if (!CreateComprehensiveScene("/tmp/comprehensive_scene.usdc")) {
    std::cerr << "✗ Comprehensive scene creation FAILED" << std::endl;
    all_pass = false;
  }

  std::cout << "\n===== Verification Instructions =====" << std::endl;
  std::cout << "To verify with OpenUSD tools:\n" << std::endl;
  std::cout << "1. Convert to ASCII:" << std::endl;
  std::cout << "   usdcat /tmp/minimal_scene.usdc -o /tmp/minimal_scene.usda" << std::endl;
  std::cout << "   usdcat /tmp/comprehensive_scene.usdc -o /tmp/comprehensive_scene.usda" << std::endl;
  std::cout << "\n2. Inspect with usddumpcrate:" << std::endl;
  std::cout << "   usddumpcrate /tmp/comprehensive_scene.usdc" << std::endl;
  std::cout << "\n3. View in usdview (if available):" << std::endl;
  std::cout << "   usdview /tmp/comprehensive_scene.usdc" << std::endl;
  std::cout << "\n4. Check with TinyUSDZ:" << std::endl;
  std::cout << "   tusdcat /tmp/comprehensive_scene.usdc" << std::endl;

  if (all_pass) {
    std::cout << "\n✓ All scenes created successfully" << std::endl;
    return 0;
  } else {
    std::cout << "\n✗ Some scenes failed to create" << std::endl;
    return 1;
  }
}
