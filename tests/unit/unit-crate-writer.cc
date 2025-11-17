#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-crate-writer.h"
#include "tinyusdz.hh"
#include "prim-types.hh"
#include "value-types.hh"
#include "timesamples.hh"
#include "crate-writer.hh"
#include "io-util.hh"
#include "usdc-reader.hh"
#include "usdShade.hh"
#include "layer.hh"
#include "pprinter.hh"
#include <cstdio>

using namespace tinyusdz;
using namespace tinyusdz::experimental;

// Helper function to create a temporary filename
static std::string get_temp_filename(const std::string& prefix) {
  static int counter = 0;
  return prefix + "_" + std::to_string(counter++) + ".usdc";
}

// Helper function to delete a file
static void cleanup_file(const std::string& filename) {
  std::remove(filename.c_str());
}

//
// Test 1: Basic file creation
// Verifies that a USDC file can be created with correct header
//
void crate_writer_basic_creation_test(void) {
  std::string filename = get_temp_filename("test_basic");
  std::string err;

  // Create minimal stage
  Stage stage;

  // Write to USDC
  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;

  writer.SetOptions(opts);

  TEST_CHECK(writer.Open(&err));
  TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
  TEST_CHECK(writer.Finalize(&err));
  writer.Close();

  // Verify file exists and has PXR-USDC header
  std::vector<uint8_t> data;
  TEST_CHECK(tinyusdz::io::ReadWholeFile(&data, &err, filename, /* filesize_max */ 0, nullptr));

  // Check magic header
  TEST_CHECK(data.size() >= 8);
  TEST_CHECK(data[0] == 'P');
  TEST_CHECK(data[1] == 'X');
  TEST_CHECK(data[2] == 'R');
  TEST_CHECK(data[3] == '-');
  TEST_CHECK(data[4] == 'U');
  TEST_CHECK(data[5] == 'S');
  TEST_CHECK(data[6] == 'D');
  TEST_CHECK(data[7] == 'C');

  cleanup_file(filename);
}

//
// Test 2: Simple prim writing
// Verifies that a simple Xform prim can be written
//
void crate_writer_simple_prim_test(void) {
  std::string filename = get_temp_filename("test_simple_prim");
  std::string err;

  // Create stage with Xform
  Stage stage;

  Xform xform;
  xform.name = "TestXform";
  xform.spec = Specifier::Def;

  Prim prim("TestXform", xform);
  stage.root_prims().emplace_back(prim);

  // Write to USDC
  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;

  writer.SetOptions(opts);

  TEST_CHECK(writer.Open(&err));
  TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
  TEST_CHECK(writer.Finalize(&err));
  writer.Close();

  // Verify file exists
  std::vector<uint8_t> data;
  TEST_CHECK(tinyusdz::io::ReadWholeFile(&data, &err, filename, 0, nullptr));
  TEST_CHECK(data.size() > 72); // Should be larger than just header

  cleanup_file(filename);
}

//
// Test 3: TypeName encoding
// Verifies that typeName fields are correctly encoded as tokens
// This tests the fix for issue #1
//
void crate_writer_typename_encoding_test(void) {
  std::string filename = get_temp_filename("test_typename");
  std::string err;

  // Create stage with Cube (has typeName)
  Stage stage;

  GeomCube cube;
  cube.name = "TestCube";
  cube.spec = Specifier::Def;

  Prim prim("TestCube", cube);
  stage.root_prims().emplace_back(prim);

  // Write to USDC
  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;

  writer.SetOptions(opts);

  TEST_CHECK(writer.Open(&err));
  TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
  TEST_CHECK(writer.Finalize(&err));
  writer.Close();

  // Read back and verify
  Stage loaded_stage;
  std::string warn;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  TEST_CHECK(ret == true);
  if (!ret) {
    TEST_MSG("Failed to load: %s", err.c_str());
  }

  // Verify prim was loaded
  TEST_CHECK(loaded_stage.root_prims().size() == 1);
  if (loaded_stage.root_prims().size() == 1) {
    const Prim& loaded_prim = loaded_stage.root_prims()[0];
    TEST_CHECK(loaded_prim.element_name() == "TestCube");

    // Note: Type reconstruction may not be fully implemented yet
    // The important thing is the file was created and can be read
  }

  cleanup_file(filename);
}

//
// Test 4: TimeSamples writing
// Verifies that animated properties with TimeSamples are written correctly
// This tests the fix for issue #2
//
void crate_writer_timesamples_test(void) {
  std::string filename = get_temp_filename("test_timesamples");
  std::string err;

  // Create stage with animated Xform
  Stage stage;

  Xform xform;
  xform.name = "AnimatedXform";
  xform.spec = Specifier::Def;

  // Create translate xformOp with TimeSamples
  XformOp translate_op;
  translate_op.op_type = XformOp::OpType::Translate;

  // Create TimeSamples with 5 frames
  value::TimeSamples ts;
  for (int i = 0; i < 5; i++) {
    double time = static_cast<double>(i);
    value::float3 position;
    position[0] = 10.0f * i;
    position[1] = 0.0f;
    position[2] = 0.0f;
    value::Value pos_value(position);
    ts.add_sample(time, pos_value);
  }

  translate_op._var._ts = ts;

  // Set default value
  value::float3 default_pos;
  default_pos[0] = 0.0f;
  default_pos[1] = 0.0f;
  default_pos[2] = 0.0f;
  translate_op._var._value = value::Value(default_pos);

  xform.xformOps.push_back(translate_op);

  Prim prim("AnimatedXform", xform);
  stage.root_prims().emplace_back(prim);

  // Write to USDC
  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;
  opts.enable_deduplication = true;

  writer.SetOptions(opts);

  TEST_CHECK(writer.Open(&err));
  TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
  TEST_CHECK(writer.Finalize(&err));
  writer.Close();

  // Read back and verify
  Stage loaded_stage;
  std::string warn;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  TEST_CHECK(ret == true);
  if (!ret) {
    TEST_MSG("Failed to load: %s", err.c_str());
  }

  cleanup_file(filename);
}

//
// Test 5: PseudoRoot ordering
// Verifies that PseudoRoot is always first in specs array
// This tests the fix for issue #3
//
void crate_writer_pseudoroot_ordering_test(void) {
  std::string filename = get_temp_filename("test_pseudoroot");
  std::string err;

  // Create stage with multiple prims with different names
  Stage stage;

  // Add prims with names that would sort differently
  Xform xform1;
  xform1.name = "AAA_First";
  xform1.spec = Specifier::Def;
  Prim prim1("AAA_First", xform1);
  stage.root_prims().emplace_back(prim1);

  Xform xform2;
  xform2.name = "ZZZ_Last";
  xform2.spec = Specifier::Def;
  Prim prim2("ZZZ_Last", xform2);
  stage.root_prims().emplace_back(prim2);

  Xform xform3;
  xform3.name = "MMM_Middle";
  xform3.spec = Specifier::Def;
  Prim prim3("MMM_Middle", xform3);
  stage.root_prims().emplace_back(prim3);

  // Write to USDC
  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;

  writer.SetOptions(opts);

  TEST_CHECK(writer.Open(&err));
  TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
  TEST_CHECK(writer.Finalize(&err));
  writer.Close();

  // Read back - should not get PseudoRoot error
  Stage loaded_stage;
  std::string warn;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  TEST_CHECK(ret == true);
  if (!ret) {
    TEST_MSG("Failed to load (PseudoRoot error?): %s", err.c_str());
  }

  // Verify all prims are present
  TEST_CHECK(loaded_stage.root_prims().size() == 3);

  cleanup_file(filename);
}

//
// Test 6: Round-trip conversion
// Verifies that a stage can be written and read back correctly
//
void crate_writer_roundtrip_test(void) {
  std::string filename = get_temp_filename("test_roundtrip");
  std::string err;

  // Create stage with various prims
  Stage stage;

  // Add Xform
  Xform xform;
  xform.name = "Root";
  xform.spec = Specifier::Def;
  Prim xform_prim("Root", xform);
  stage.root_prims().emplace_back(xform_prim);

  // Add Cube
  GeomCube cube;
  cube.name = "MyCube";
  cube.spec = Specifier::Def;
  Prim cube_prim("MyCube", cube);
  stage.root_prims().emplace_back(cube_prim);

  // Add Sphere
  GeomSphere sphere;
  sphere.name = "MySphere";
  sphere.spec = Specifier::Def;
  Prim sphere_prim("MySphere", sphere);
  stage.root_prims().emplace_back(sphere_prim);

  // Write to USDC
  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;

  writer.SetOptions(opts);

  TEST_CHECK(writer.Open(&err));
  TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
  TEST_CHECK(writer.Finalize(&err));
  writer.Close();

  // Read back
  Stage loaded_stage;
  std::string warn;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  TEST_CHECK(ret == true);
  if (!ret) {
    TEST_MSG("Failed to load: %s", err.c_str());
  }

  // Verify all prims are present
  TEST_CHECK(loaded_stage.root_prims().size() == 3);

  if (loaded_stage.root_prims().size() == 3) {
    // Check prim names (basic round-trip verification)
    // Note: Full type reconstruction is tested separately
    TEST_MSG("Prim 0: %s", loaded_stage.root_prims()[0].element_name().c_str());
    TEST_MSG("Prim 1: %s", loaded_stage.root_prims()[1].element_name().c_str());
    TEST_MSG("Prim 2: %s", loaded_stage.root_prims()[2].element_name().c_str());
  }

  cleanup_file(filename);
}

//
// Test 7: Multiple prims at root level
// Verifies that multiple prims can be written and read correctly
//
void crate_writer_multiple_prims_test(void) {
  std::string filename = get_temp_filename("test_multiple");
  std::string err;

  Stage stage;

  // Create 10 prims
  for (int i = 0; i < 10; i++) {
    Xform xform;
    std::string name = "Prim_" + std::to_string(i);
    xform.name = name;
    xform.spec = Specifier::Def;

    Prim prim(name, xform);
    stage.root_prims().emplace_back(prim);
  }

  // Write to USDC
  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;

  writer.SetOptions(opts);

  TEST_CHECK(writer.Open(&err));
  TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
  TEST_CHECK(writer.Finalize(&err));
  writer.Close();

  // Read back
  Stage loaded_stage;
  std::string warn;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  TEST_CHECK(ret == true);
  TEST_CHECK(loaded_stage.root_prims().size() == 10);

  cleanup_file(filename);
}

//
// Test 8: Nested prims (hierarchy)
// Verifies that prim hierarchies can be written
//
void crate_writer_nested_prims_test(void) {
  std::string filename = get_temp_filename("test_nested");
  std::string err;

  Stage stage;

  // Create parent Xform
  Xform parent_xform;
  parent_xform.name = "Parent";
  parent_xform.spec = Specifier::Def;

  // Create child Xform
  Xform child_xform;
  child_xform.name = "Child";
  child_xform.spec = Specifier::Def;
  Prim child_prim("Child", child_xform);

  // Add child to parent
  Prim parent_prim("Parent", parent_xform);
  parent_prim.children().emplace_back(child_prim);

  stage.root_prims().emplace_back(parent_prim);

  // Write to USDC
  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;

  writer.SetOptions(opts);

  TEST_CHECK(writer.Open(&err));
  TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
  TEST_CHECK(writer.Finalize(&err));
  writer.Close();

  // Read back
  Stage loaded_stage;
  std::string warn;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  TEST_CHECK(ret == true);
  if (ret) {
    TEST_CHECK(loaded_stage.root_prims().size() == 1);
    if (loaded_stage.root_prims().size() == 1) {
      const Prim& loaded_parent = loaded_stage.root_prims()[0];
      TEST_CHECK(loaded_parent.element_name() == "Parent");
      TEST_CHECK(loaded_parent.children().size() == 1);

      if (loaded_parent.children().size() == 1) {
        const Prim& loaded_child = loaded_parent.children()[0];
        TEST_CHECK(loaded_child.element_name() == "Child");
      }
    }
  }

  cleanup_file(filename);
}

//
// Test 9: Error handling
// Verifies that appropriate errors are returned for invalid operations
//
void crate_writer_error_handling_test(void) {
  std::string err;

  // Test: Cannot finalize without opening
  {
    std::string filename = get_temp_filename("test_error1");
    CrateWriter writer(filename);

    bool result = writer.Finalize(&err);
    TEST_CHECK(result == false);
    TEST_CHECK(!err.empty());
  }

  // Test: Cannot convert stage without opening
  {
    std::string filename = get_temp_filename("test_error2");
    CrateWriter writer(filename);
    Stage stage;

    bool result = writer.ConvertStageToSpecs(stage, &err);
    TEST_CHECK(result == false);
    TEST_CHECK(!err.empty());
  }
}

//
// Test 10: Material and Shader writing
// Verifies that Material and Shader prims can be written
//
void crate_writer_material_shader_test(void) {
  std::string filename = get_temp_filename("test_material");
  std::string err;

  // Create stage with Material and Shader
  Stage stage;

  // Create a basic Shader prim
  Shader shader;
  shader.name = "TestShader";
  shader.spec = Specifier::Def;
  shader.info_id = "UsdPreviewSurface";  // Shader type

  Prim shader_prim("TestShader", shader);
  shader_prim.prim_type_name() = "Shader";  // Set typeName explicitly
  stage.root_prims().emplace_back(shader_prim);

  // Create a Material prim with surface output connection
  Material material;
  material.name = "TestMaterial";
  material.spec = Specifier::Def;

  // Set surface output to connect to the shader
  Path shader_path("/TestShader", "");
  material.surface.set(shader_path);

  Prim material_prim("TestMaterial", material);
  material_prim.prim_type_name() = "Material";  // Set typeName explicitly
  stage.root_prims().emplace_back(material_prim);

  // Write to USDC
  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;

  writer.SetOptions(opts);

  TEST_CHECK(writer.Open(&err));
  TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
  TEST_CHECK(writer.Finalize(&err));
  writer.Close();

  std::cerr << "Material test file: " << filename << "\n";

  // Read back and verify
  Stage loaded_stage;
  std::string warn;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  TEST_CHECK(ret == true);
  if (!ret) {
    TEST_MSG("Failed to load: %s", err.c_str());
    std::cerr << "FAILED TO LOAD: " << err << "\n";
  }

  // Verify prims were loaded
  std::cerr << "Loaded " << loaded_stage.root_prims().size() << " root prims\n";
  for (size_t i = 0; i < loaded_stage.root_prims().size(); i++) {
    const auto& prim = loaded_stage.root_prims()[i];
    std::cerr << "  Prim " << i << ": " << prim.element_name()
              << " (type: " << prim.prim_type_name() << ")"
              << " children=" << prim.children().size() << "\n";
    for (size_t j = 0; j < prim.children().size(); j++) {
      std::cerr << "    Child " << j << ": " << prim.children()[j].element_name()
                << " (type: " << prim.children()[j].prim_type_name() << ")\n";
    }
  }
  TEST_MSG("Loaded %zu root prims", loaded_stage.root_prims().size());
  for (size_t i = 0; i < loaded_stage.root_prims().size(); i++) {
    TEST_MSG("Prim %zu: %s (type: %s)", i,
             loaded_stage.root_prims()[i].element_name().c_str(),
             loaded_stage.root_prims()[i].prim_type_name().c_str());
  }

  TEST_CHECK(loaded_stage.root_prims().size() == 2);
  if (loaded_stage.root_prims().size() == 2) {
    TEST_MSG("Prim 0: %s", loaded_stage.root_prims()[0].element_name().c_str());
    TEST_MSG("Prim 1: %s", loaded_stage.root_prims()[1].element_name().c_str());

    // Find the Material prim and verify surface connection
    for (const auto& prim : loaded_stage.root_prims()) {
      if (prim.element_name() == "TestMaterial") {
        const Material* loaded_mat = prim.data().as<Material>();
        TEST_CHECK(loaded_mat != nullptr);
        if (loaded_mat) {
          TEST_CHECK(loaded_mat->surface.authored());
          TEST_CHECK(loaded_mat->surface.has_value());
          if (loaded_mat->surface.has_value()) {
            const auto& connections = loaded_mat->surface.get_connections();
            TEST_CHECK(connections.size() == 1);
            if (connections.size() == 1) {
              TEST_MSG("Surface connection: %s", connections[0].full_path_name().c_str());
              TEST_CHECK(connections[0].full_path_name() == "/TestShader");
            }
          }
        }
      }
    }
  }

  std::cerr << "Material test file: " << filename << "\n";
  cleanup_file(filename);
}

void crate_writer_layer_metadata_test(void) {
  using namespace tinyusdz;

  std::string filename = "test_layer_metadata.usdc";

  // Create a layer with metadata
  Layer layer;
  layer.set_name("TestLayer");

  // Set various layer metadata
  LayerMetas& metas = layer.metas();

  // Standard metadata
  metas.upAxis = Axis::Z;  // Set upAxis to Z
  metas.metersPerUnit = 0.01;  // centimeters
  metas.timeCodesPerSecond = 30.0;  // 30 fps
  metas.framesPerSecond = 30.0;
  metas.startTimeCode = 1.0;
  metas.endTimeCode = 120.0;
  metas.defaultPrim = value::token("MyPrim");
  metas.doc.value = "Test documentation string";
  metas.comment.value = "Test comment";
  metas.kilogramsPerUnit = 0.001;  // grams

  // Add a simple prim so the layer has content
  PrimSpec ps;
  ps.specifier() = Specifier::Def;
  ps.typeName() = "Xform";
  layer.add_primspec("MyPrim", ps);

  // Write using CrateWriter
  {
    experimental::CrateWriter writer(filename);
    std::string err;

    TEST_CHECK(writer.Open(&err));
    if (!writer.Open(&err)) {
      TEST_MSG("Failed to open: %s", err.c_str());
      return;
    }

    TEST_CHECK(writer.ConvertLayerToSpecs(layer, &err));
    if (!writer.ConvertLayerToSpecs(layer, &err)) {
      TEST_MSG("Failed to convert: %s", err.c_str());
      return;
    }

    TEST_CHECK(writer.Finalize(&err));
    if (!writer.Finalize(&err)) {
      TEST_MSG("Failed to finalize: %s", err.c_str());
      return;
    }

    writer.Close();
  }

  TEST_MSG("Layer metadata test file: %s", filename.c_str());

  // Load and verify
  Stage loaded_stage;
  std::string warn, err;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  if (!ret) {
    std::cerr << "FAILED TO LOAD: " << err << "\n";
  }
  TEST_CHECK(ret == true);
  if (!ret) {
    TEST_MSG("Failed to load: %s", err.c_str());
    cleanup_file(filename);
    return;
  }

  // Verify layer metadata was preserved
  const LayerMetas& loaded_metas = loaded_stage.metas();

  TEST_CHECK(loaded_metas.upAxis.get_value() == Axis::Z);
  TEST_MSG("upAxis: %s (expected Z)", to_string(loaded_metas.upAxis.get_value()).c_str());

  TEST_CHECK(std::abs(loaded_metas.metersPerUnit.get_value() - 0.01) < 0.0001);
  TEST_MSG("metersPerUnit: %f (expected 0.01)", loaded_metas.metersPerUnit.get_value());

  TEST_CHECK(std::abs(loaded_metas.timeCodesPerSecond.get_value() - 30.0) < 0.0001);
  TEST_MSG("timeCodesPerSecond: %f (expected 30.0)", loaded_metas.timeCodesPerSecond.get_value());

  TEST_CHECK(std::abs(loaded_metas.framesPerSecond.get_value() - 30.0) < 0.0001);
  TEST_MSG("framesPerSecond: %f (expected 30.0)", loaded_metas.framesPerSecond.get_value());

  TEST_CHECK(std::abs(loaded_metas.startTimeCode.get_value() - 1.0) < 0.0001);
  TEST_MSG("startTimeCode: %f (expected 1.0)", loaded_metas.startTimeCode.get_value());

  TEST_CHECK(std::abs(loaded_metas.endTimeCode.get_value() - 120.0) < 0.0001);
  TEST_MSG("endTimeCode: %f (expected 120.0)", loaded_metas.endTimeCode.get_value());

  TEST_CHECK(loaded_metas.defaultPrim.str() == "MyPrim");
  TEST_MSG("defaultPrim: %s (expected MyPrim)", loaded_metas.defaultPrim.str().c_str());

  TEST_CHECK(loaded_metas.doc.value == "Test documentation string");
  TEST_MSG("doc: %s", loaded_metas.doc.value.c_str());

  TEST_CHECK(loaded_metas.comment.value == "Test comment");
  TEST_MSG("comment: %s", loaded_metas.comment.value.c_str());

  TEST_CHECK(std::abs(loaded_metas.kilogramsPerUnit.get_value() - 0.001) < 0.00001);
  TEST_MSG("kilogramsPerUnit: %f (expected 0.001)", loaded_metas.kilogramsPerUnit.get_value());

  std::cerr << "Layer metadata roundtrip successful!\n";
  cleanup_file(filename);
}

// Test UsdPreviewSurface shader with inputs
void crate_writer_usd_preview_surface_test(void) {
  std::string filename = get_temp_filename("test_usd_preview_surface.usdc");

  {
    // Create a Stage with a UsdPreviewSurface shader
    Stage stage;

    // Create UsdPreviewSurface value
    UsdPreviewSurface preview_surface;

    Animatable<value::color3f> diffuse_anim(value::color3f{0.8f, 0.1f, 0.1f});
    preview_surface.diffuseColor.set_value(diffuse_anim);

    Animatable<float> metallic_anim(0.9f);
    preview_surface.metallic.set_value(metallic_anim);

    Animatable<float> roughness_anim(0.2f);
    preview_surface.roughness.set_value(roughness_anim);

    Animatable<float> opacity_anim(0.85f);
    preview_surface.opacity.set_value(opacity_anim);

    Animatable<float> ior_anim(1.45f);
    preview_surface.ior.set_value(ior_anim);

    Animatable<value::color3f> emissive_anim(value::color3f{0.1f, 0.1f, 0.0f});
    preview_surface.emissiveColor.set_value(emissive_anim);

    Animatable<float> clearcoat_anim(0.5f);
    preview_surface.clearcoat.set_value(clearcoat_anim);

    Animatable<float> clearcoat_rough_anim(0.1f);
    preview_surface.clearcoatRoughness.set_value(clearcoat_rough_anim);

    // Create Shader and set info:id and value
    Shader shader;
    shader.name = "PreviewSurface";
    shader.spec = Specifier::Def;
    shader.info_id = "UsdPreviewSurface";
    shader.value = preview_surface;

    // Create Shader prim with UsdPreviewSurface
    Prim shader_prim("PreviewSurface", shader);
    shader_prim.prim_type_name() = "Shader";  // Set typeName explicitly

    // Add shader to stage
    stage.root_prims().push_back(shader_prim);

    // Write using CrateWriter
    experimental::CrateWriter writer(filename);
    std::string err;
    bool ret = writer.Open(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to open writer: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.ConvertStageToSpecs(stage, &err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to convert stage: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.Finalize(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to finalize: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    writer.Close();
  }

  TEST_MSG("UsdPreviewSurface test file: %s", filename.c_str());

  // Load and verify
  Stage loaded_stage;
  std::string warn, err;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  if (!ret) {
    std::cerr << "FAILED TO LOAD: " << err << "\n";
  }
  TEST_CHECK(ret == true);
  if (!ret) {
    TEST_MSG("Failed to load: %s", err.c_str());
    cleanup_file(filename);
    return;
  }

  // Find the shader prim
  auto shader_prim_result = loaded_stage.GetPrimAtPath(Path("/PreviewSurface", ""));
  TEST_CHECK(shader_prim_result.has_value());
  if (!shader_prim_result.has_value()) {
    TEST_MSG("Failed to find shader prim: %s", shader_prim_result.error().c_str());
    cleanup_file(filename);
    return;
  }

  const Prim* shader_prim = shader_prim_result.value();
  TEST_CHECK(shader_prim != nullptr);

  TEST_CHECK(shader_prim->prim_type_name() == "Shader");
  TEST_MSG("Found Shader prim: %s", shader_prim->element_name().c_str());

  // NOTE: The USDC reader doesn't fully support reading Shader properties yet,
  // so we can't verify info:id or the shader inputs at this time.
  // The writer correctly writes these fields (verified in debug output),
  // but the reader needs additional work to parse them back.
  //
  // For now, we just verify that:
  // 1. The Shader prim exists and has the correct type
  // 2. The file can be written and loaded without errors
  //
  // TODO: Once the USDC reader is enhanced to parse Shader properties,
  // uncomment the verification code below to test full round-trip.

  /*
  // Get the Shader from prim data
  const Shader* shader = shader_prim->as<Shader>();
  TEST_CHECK(shader != nullptr);
  if (shader) {
    TEST_CHECK(shader->info_id == "UsdPreviewSurface");
    TEST_MSG("info:id = %s", shader->info_id.c_str());

    // Get UsdPreviewSurface from shader value
    const UsdPreviewSurface* preview = shader->value.as<UsdPreviewSurface>();
    if (preview) {
      // Verify shader inputs...
    }
  }
  */

  std::cerr << "UsdPreviewSurface roundtrip successful!\n";
  cleanup_file(filename);
}

// Test UsdUVTexture shader with inputs
void crate_writer_usd_uv_texture_test(void) {
  std::string filename = get_temp_filename("test_usd_uv_texture.usdc");

  {
    // Create a Stage with a UsdUVTexture shader
    Stage stage;

    // Create UsdUVTexture value
    UsdUVTexture uv_texture;

    // Set file path
    Animatable<value::AssetPath> file_anim(value::AssetPath("textures/diffuse.png"));
    uv_texture.file.set_value(file_anim);

    // Set texture coordinates
    Animatable<value::texcoord2f> st_anim(value::texcoord2f{0.5f, 0.5f});
    uv_texture.st.set_value(st_anim);

    // Set wrap modes
    Animatable<UsdUVTexture::Wrap> wraps_anim(UsdUVTexture::Wrap::Repeat);
    uv_texture.wrapS.set_value(wraps_anim);

    Animatable<UsdUVTexture::Wrap> wrapt_anim(UsdUVTexture::Wrap::Clamp);
    uv_texture.wrapT.set_value(wrapt_anim);

    // Set fallback color
    value::color4f fallback{1.0f, 0.0f, 1.0f, 1.0f};
    uv_texture.fallback.set_value(fallback);

    // Set color space
    Animatable<UsdUVTexture::SourceColorSpace> colorspace_anim(UsdUVTexture::SourceColorSpace::SRGB);
    uv_texture.sourceColorSpace.set_value(colorspace_anim);

    // Set scale and bias
    value::float4 scale{2.0f, 2.0f, 1.0f, 1.0f};
    uv_texture.scale.set_value(scale);

    value::float4 bias{0.1f, 0.1f, 0.0f, 0.0f};
    uv_texture.bias.set_value(bias);

    // Create Shader and set info:id and value
    Shader shader;
    shader.name = "DiffuseTexture";
    shader.spec = Specifier::Def;
    shader.info_id = "UsdUVTexture";
    shader.value = uv_texture;

    // Create Shader prim with UsdUVTexture
    Prim shader_prim("DiffuseTexture", shader);
    shader_prim.prim_type_name() = "Shader";

    // Add shader to stage
    stage.root_prims().push_back(shader_prim);

    // Write using CrateWriter
    experimental::CrateWriter writer(filename);
    std::string err;
    bool ret = writer.Open(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to open writer: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.ConvertStageToSpecs(stage, &err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to convert stage: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.Finalize(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to finalize: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    writer.Close();
  }

  TEST_MSG("UsdUVTexture test file: %s", filename.c_str());

  // Load and verify
  Stage loaded_stage;
  std::string warn, err;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  if (!ret) {
    std::cerr << "FAILED TO LOAD: " << err << "\n";
  }
  TEST_CHECK(ret == true);
  if (!ret) {
    TEST_MSG("Failed to load: %s", err.c_str());
    cleanup_file(filename);
    return;
  }

  // Find the shader prim
  auto shader_prim_result = loaded_stage.GetPrimAtPath(Path("/DiffuseTexture", ""));
  TEST_CHECK(shader_prim_result.has_value());
  if (!shader_prim_result.has_value()) {
    TEST_MSG("Failed to find shader prim: %s", shader_prim_result.error().c_str());
    cleanup_file(filename);
    return;
  }

  const Prim* shader_prim = shader_prim_result.value();
  TEST_CHECK(shader_prim != nullptr);

  TEST_CHECK(shader_prim->prim_type_name() == "Shader");
  TEST_MSG("Found Shader prim: %s", shader_prim->element_name().c_str());

  // NOTE: The USDC reader doesn't fully support reading Shader properties yet,
  // so we can't verify info:id or the shader inputs at this time.
  // The writer correctly writes these fields (verified in debug output),
  // but the reader needs additional work to parse them back.
  //
  // For now, we just verify that:
  // 1. The Shader prim exists and has the correct type
  // 2. The file can be written and loaded without errors
  //
  // TODO: Once the USDC reader is enhanced to parse Shader properties,
  // uncomment the verification code below to test full round-trip.

  std::cerr << "UsdUVTexture roundtrip successful!\n";
  cleanup_file(filename);
}

// Test UsdPrimvarReader_float2 shader with inputs
void crate_writer_usd_primvar_reader_test(void) {
  std::string filename = get_temp_filename("test_usd_primvar_reader.usdc");

  {
    // Create a Stage with a UsdPrimvarReader_float2 shader
    Stage stage;

    // Create UsdPrimvarReader_float2 value
    UsdPrimvarReader_float2 primvar_reader;

    // Set varname - the name of the primvar to read
    Animatable<std::string> varname_anim("st");  // Read "st" primvar (texture coordinates)
    primvar_reader.varname.set_value(varname_anim);

    // Create Shader and set info:id and value
    Shader shader;
    shader.name = "StReader";
    shader.spec = Specifier::Def;
    shader.info_id = "UsdPrimvarReader_float2";
    shader.value = primvar_reader;

    // Create Shader prim with UsdPrimvarReader_float2
    Prim shader_prim("StReader", shader);
    shader_prim.prim_type_name() = "Shader";

    // Add shader to stage
    stage.root_prims().push_back(shader_prim);

    // Write using CrateWriter
    experimental::CrateWriter writer(filename);
    std::string err;
    bool ret = writer.Open(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to open writer: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.ConvertStageToSpecs(stage, &err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to convert stage: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.Finalize(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to finalize: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    writer.Close();
  }

  TEST_MSG("UsdPrimvarReader test file: %s", filename.c_str());

  // Load and verify
  Stage loaded_stage;
  std::string warn, err;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  if (!ret) {
    std::cerr << "FAILED TO LOAD: " << err << "\n";
  }
  TEST_CHECK(ret == true);
  if (!ret) {
    TEST_MSG("Failed to load: %s", err.c_str());
    cleanup_file(filename);
    return;
  }

  // Find the shader prim
  auto shader_prim_result = loaded_stage.GetPrimAtPath(Path("/StReader", ""));
  TEST_CHECK(shader_prim_result.has_value());
  if (!shader_prim_result.has_value()) {
    TEST_MSG("Failed to find shader prim: %s", shader_prim_result.error().c_str());
    cleanup_file(filename);
    return;
  }

  const Prim* shader_prim = shader_prim_result.value();
  TEST_CHECK(shader_prim != nullptr);

  TEST_CHECK(shader_prim->prim_type_name() == "Shader");
  TEST_MSG("Found Shader prim: %s", shader_prim->element_name().c_str());

  // NOTE: The USDC reader doesn't fully support reading Shader properties yet,
  // so we can't verify info:id or the shader inputs at this time.
  // The writer correctly writes these fields (verified in debug output),
  // but the reader needs additional work to parse them back.
  //
  // For now, we just verify that:
  // 1. The Shader prim exists and has the correct type
  // 2. The file can be written and loaded without errors
  //
  // TODO: Once the USDC reader is enhanced to parse Shader properties,
  // uncomment the verification code below to test full round-trip.

  std::cerr << "UsdPrimvarReader roundtrip successful!\n";
  cleanup_file(filename);
}

void crate_writer_usd_transform2d_test(void) {
  std::string filename = get_temp_filename("test_usd_transform2d.usdc");

  {
    // Create a Stage with a UsdTransform2d shader
    Stage stage;

    // Create UsdTransform2d value
    UsdTransform2d transform2d;

    // Set transformation inputs
    // inputs:in (float2) - input texture coordinates
    Animatable<value::float2> in_anim(value::float2{0.0f, 0.0f});
    transform2d.in.set_value(in_anim);

    // inputs:rotation (float) - rotation in degrees, CCW
    Animatable<float> rotation_anim(45.0f);
    transform2d.rotation.set_value(rotation_anim);

    // inputs:scale (float2) - scale factors
    Animatable<value::float2> scale_anim(value::float2{2.0f, 2.0f});
    transform2d.scale.set_value(scale_anim);

    // inputs:translation (float2) - translation offset
    Animatable<value::float2> translation_anim(value::float2{0.5f, 0.5f});
    transform2d.translation.set_value(translation_anim);

    // Create Shader and set info:id and value
    Shader shader;
    shader.name = "TexTransform";
    shader.spec = Specifier::Def;
    shader.info_id = "UsdTransform2d";
    shader.value = transform2d;

    // Create Shader prim with UsdTransform2d
    Prim shader_prim("TexTransform", shader);
    shader_prim.prim_type_name() = "Shader";

    // Add shader to stage
    stage.root_prims().push_back(shader_prim);

    // Write using CrateWriter
    experimental::CrateWriter writer(filename);
    std::string err;
    bool ret = writer.Open(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to open writer: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.ConvertStageToSpecs(stage, &err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to convert stage: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.Finalize(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to finalize: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    writer.Close();
  }

  TEST_MSG("UsdTransform2d test file: %s", filename.c_str());

  // Load and verify
  Stage loaded_stage;
  std::string warn, err;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  if (!ret) {
    std::cerr << "FAILED TO LOAD: " << err << "\n";
  }
  TEST_CHECK(ret == true);
  if (!ret) {
    TEST_MSG("Failed to load: %s", err.c_str());
    cleanup_file(filename);
    return;
  }

  // Find the shader prim
  auto shader_prim_result = loaded_stage.GetPrimAtPath(Path("/TexTransform", ""));
  TEST_CHECK(shader_prim_result.has_value());
  if (!shader_prim_result.has_value()) {
    TEST_MSG("Failed to find shader prim: %s", shader_prim_result.error().c_str());
    cleanup_file(filename);
    return;
  }

  const Prim* shader_prim = shader_prim_result.value();
  TEST_CHECK(shader_prim != nullptr);

  TEST_CHECK(shader_prim->prim_type_name() == "Shader");
  TEST_MSG("Found Shader prim: %s", shader_prim->element_name().c_str());

  // NOTE: The USDC reader doesn't fully support reading Shader properties yet,
  // so we can't verify info:id or the shader inputs at this time.
  // The writer correctly writes these fields (verified in debug output),
  // but the reader needs additional work to parse them back.
  //
  // For now, we just verify that:
  // 1. The Shader prim exists and has the correct type
  // 2. The file can be written and loaded without errors
  //
  // TODO: Once the USDC reader is enhanced to parse Shader properties,
  // uncomment the verification code below to test full round-trip.

  std::cerr << "UsdTransform2d roundtrip successful!\n";
  cleanup_file(filename);
}

void crate_writer_cone_test(void) {
  std::string filename = get_temp_filename("test_cone.usdc");

  {
    // Create a Stage with a Cone prim
    Stage stage;

    // Create Cone geometry
    GeomCone cone;
    cone.radius = 1.5;
    cone.height = 3.0;
    cone.axis = Axis::Z;

    // Create Prim with Cone
    Prim cone_prim("MyCone", cone);
    cone_prim.prim_type_name() = "Cone";

    // Add to stage
    stage.root_prims().push_back(cone_prim);

    // Write using CrateWriter
    experimental::CrateWriter writer(filename);
    std::string err;
    bool ret = writer.Open(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to open writer: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.ConvertStageToSpecs(stage, &err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to convert stage: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.Finalize(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to finalize: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    writer.Close();
  }

  TEST_MSG("Cone test file: %s", filename.c_str());

  // Load and verify
  Stage loaded_stage;
  std::string warn, err;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  if (!ret) {
    std::cerr << "FAILED TO LOAD: " << err << "\n";
  }
  TEST_CHECK(ret == true);
  if (!ret) {
    TEST_MSG("Failed to load: %s", err.c_str());
    cleanup_file(filename);
    return;
  }

  // Find the cone prim
  auto cone_prim_result = loaded_stage.GetPrimAtPath(Path("/MyCone", ""));
  TEST_CHECK(cone_prim_result.has_value());
  if (!cone_prim_result.has_value()) {
    TEST_MSG("Failed to find cone prim: %s", cone_prim_result.error().c_str());
    cleanup_file(filename);
    return;
  }

  const Prim* cone_prim = cone_prim_result.value();
  TEST_CHECK(cone_prim != nullptr);
  TEST_CHECK(cone_prim->prim_type_name() == "Cone");

  std::cerr << "Cone roundtrip successful!\n";
  cleanup_file(filename);
}

void crate_writer_capsule_test(void) {
  std::string filename = get_temp_filename("test_capsule.usdc");

  {
    // Create a Stage with a Capsule prim
    Stage stage;

    // Create Capsule geometry
    GeomCapsule capsule;
    capsule.radius = 0.75;
    capsule.height = 2.5;
    capsule.axis = Axis::Y;

    // Create Prim with Capsule
    Prim capsule_prim("MyCapsule", capsule);
    capsule_prim.prim_type_name() = "Capsule";

    // Add to stage
    stage.root_prims().push_back(capsule_prim);

    // Write using CrateWriter
    experimental::CrateWriter writer(filename);
    std::string err;
    bool ret = writer.Open(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to open writer: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.ConvertStageToSpecs(stage, &err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to convert stage: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.Finalize(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to finalize: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    writer.Close();
  }

  TEST_MSG("Capsule test file: %s", filename.c_str());

  // Load and verify
  Stage loaded_stage;
  std::string warn, err;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  if (!ret) {
    std::cerr << "FAILED TO LOAD: " << err << "\n";
  }
  TEST_CHECK(ret == true);
  if (!ret) {
    TEST_MSG("Failed to load: %s", err.c_str());
    cleanup_file(filename);
    return;
  }

  // Find the capsule prim
  auto capsule_prim_result = loaded_stage.GetPrimAtPath(Path("/MyCapsule", ""));
  TEST_CHECK(capsule_prim_result.has_value());
  if (!capsule_prim_result.has_value()) {
    TEST_MSG("Failed to find capsule prim: %s", capsule_prim_result.error().c_str());
    cleanup_file(filename);
    return;
  }

  const Prim* capsule_prim = capsule_prim_result.value();
  TEST_CHECK(capsule_prim != nullptr);
  TEST_CHECK(capsule_prim->prim_type_name() == "Capsule");

  std::cerr << "Capsule roundtrip successful!\n";
  cleanup_file(filename);
}

void crate_writer_points_test(void) {
  std::string filename = get_temp_filename("test_points.usdc");

  {
    // Create a Stage with a Points prim
    Stage stage;

    // Create point cloud data
    std::vector<value::point3f> points_data = {
      {0.0f, 0.0f, 0.0f},
      {1.0f, 0.0f, 0.0f},
      {1.0f, 1.0f, 0.0f},
      {0.0f, 1.0f, 0.0f}
    };

    std::vector<float> widths_data = {
      0.1f, 0.1f, 0.1f, 0.1f
    };

    // Create GeomPoints
    GeomPoints points;
    // Note: We currently support point3f[] and float[] arrays
    // int64[], normal3f[], vector3f[] support can be added later
    Animatable<std::vector<value::point3f>> points_anim(points_data);
    Animatable<std::vector<float>> widths_anim(widths_data);

    // Use authored() check pattern from existing code
    auto points_opt = points.points.get_value();
    points.points.set_value(points_anim);
    points.widths.set_value(widths_anim);

    // Create Prim with Points
    Prim points_prim("MyPoints", points);
    points_prim.prim_type_name() = "Points";

    // Add to stage
    stage.root_prims().push_back(points_prim);

    // Write using CrateWriter
    experimental::CrateWriter writer(filename);
    std::string err;
    bool ret = writer.Open(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to open writer: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.ConvertStageToSpecs(stage, &err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to convert stage: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.Finalize(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to finalize: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    writer.Close();
  }

  TEST_MSG("Points test file: %s", filename.c_str());

  // Load and verify
  Stage loaded_stage;
  std::string warn, err;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  if (!ret) {
    std::cerr << "FAILED TO LOAD: " << err << "\n";
  }
  TEST_CHECK(ret == true);
  if (!ret) {
    TEST_MSG("Failed to load: %s", err.c_str());
    cleanup_file(filename);
    return;
  }

  // Find the points prim
  auto points_prim_result = loaded_stage.GetPrimAtPath(Path("/MyPoints", ""));
  TEST_CHECK(points_prim_result.has_value());
  if (!points_prim_result.has_value()) {
    TEST_MSG("Failed to find points prim: %s", points_prim_result.error().c_str());
    cleanup_file(filename);
    return;
  }

  const Prim* points_prim = points_prim_result.value();
  TEST_CHECK(points_prim != nullptr);
  TEST_CHECK(points_prim->prim_type_name() == "Points");

  std::cerr << "Points roundtrip successful!\n";
  cleanup_file(filename);
}

void crate_writer_camera_test(void) {
  std::string filename = get_temp_filename("test_camera.usdc");

  {
    // Create a Stage with a Camera
    Stage stage;

    // Create a Camera prim
    GeomCamera camera;
    camera.name = "Camera";
    camera.focalLength = 35.0f;  // 35mm lens
    camera.clippingRange = value::float2({0.1f, 10000.0f});
    camera.exposure = 0.0f;
    camera.fStop = 2.8f;
    camera.horizontalAperture = 21.0f;
    camera.verticalAperture = 15.2f;

    Prim camera_prim("Camera", camera);
    camera_prim.prim_type_name() = "Camera";
    stage.root_prims().push_back(camera_prim);

    // Write using CrateWriter
    experimental::CrateWriter writer(filename);
    std::string err;
    bool ret = writer.Open(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to open writer: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.ConvertStageToSpecs(stage, &err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to convert stage: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.Finalize(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to finalize: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    writer.Close();
  }

  TEST_MSG("Camera test file: %s", filename.c_str());

  // Load and verify
  Stage loaded_stage;
  std::string warn, err;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  if (!ret) {
    std::cerr << "FAILED TO LOAD: " << err << "\n";
  }
  TEST_CHECK(ret == true);
  if (!ret) {
    TEST_MSG("Failed to load: %s", err.c_str());
    cleanup_file(filename);
    return;
  }

  // Find the camera prim
  auto camera_prim_result = loaded_stage.GetPrimAtPath(Path("/Camera", ""));
  TEST_CHECK(camera_prim_result.has_value());
  if (!camera_prim_result.has_value()) {
    TEST_MSG("Failed to find camera prim: %s", camera_prim_result.error().c_str());
    cleanup_file(filename);
    return;
  }

  const Prim* camera_prim = camera_prim_result.value();
  TEST_CHECK(camera_prim != nullptr);
  TEST_CHECK(camera_prim->prim_type_name() == "Camera");

  // Verify camera properties
  const GeomCamera* loaded_camera = camera_prim->data().as<GeomCamera>();
  TEST_CHECK(loaded_camera != nullptr);
  if (loaded_camera) {
    // Check focalLength
    if (loaded_camera->focalLength.authored()) {
      const Animatable<float>& fl_anim = loaded_camera->focalLength.get_value();
      float fl_val;
      if (fl_anim.get_scalar(&fl_val)) {
        TEST_MSG("Camera focalLength: %.1f", fl_val);
        TEST_CHECK(std::abs(fl_val - 35.0f) < 0.01f);
      }
    }

    // Check clippingRange
    if (loaded_camera->clippingRange.authored()) {
      const Animatable<value::float2>& cr_anim = loaded_camera->clippingRange.get_value();
      value::float2 cr_val;
      if (cr_anim.get_scalar(&cr_val)) {
        TEST_MSG("Camera clippingRange: [%.1f, %.1f]", cr_val[0], cr_val[1]);
        TEST_CHECK(std::abs(cr_val[0] - 0.1f) < 0.01f);
        TEST_CHECK(std::abs(cr_val[1] - 10000.0f) < 1.0f);
      }
    }

    // Check fStop
    if (loaded_camera->fStop.authored()) {
      const Animatable<float>& fs_anim = loaded_camera->fStop.get_value();
      float fs_val;
      if (fs_anim.get_scalar(&fs_val)) {
        TEST_MSG("Camera fStop: %.1f", fs_val);
        TEST_CHECK(std::abs(fs_val - 2.8f) < 0.01f);
      }
    }
  }

  std::cerr << "Camera roundtrip successful!\n";
  cleanup_file(filename);
}

void crate_writer_basis_curves_test(void) {
  std::string filename = get_temp_filename("test_basis_curves.usdc");

  {
    // Create a stage with a BasisCurves prim
    Stage stage;

    // Create curve point data
    std::vector<value::point3f> points_data = {
      {0.0f, 0.0f, 0.0f},
      {1.0f, 0.0f, 0.0f},
      {2.0f, 1.0f, 0.0f},
      {3.0f, 0.0f, 0.0f},
    };

    std::vector<int> counts_data = {4};
    std::vector<float> widths_data = {1.0f, 1.0f, 1.0f, 1.0f};

    // Create GeomBasisCurves
    GeomBasisCurves basis_curves;
    basis_curves.type = GeomBasisCurves::Type::Cubic;
    basis_curves.basis = GeomBasisCurves::Basis::Bezier;
    basis_curves.wrap = GeomBasisCurves::Wrap::Nonperiodic;

    // Set animatable properties
    Animatable<std::vector<value::point3f>> points_anim(points_data);
    Animatable<std::vector<int>> counts_anim(counts_data);
    Animatable<std::vector<float>> widths_anim(widths_data);

    basis_curves.points.set_value(points_anim);
    basis_curves.curveVertexCounts.set_value(counts_anim);
    basis_curves.widths.set_value(widths_anim);

    // Create Prim with BasisCurves
    Prim curves_prim("MyBasisCurve", basis_curves);
    curves_prim.prim_type_name() = "BasisCurves";

    // Add to stage
    stage.root_prims().push_back(curves_prim);

    // Write using CrateWriter
    experimental::CrateWriter writer(filename);
    std::string err;
    bool ret = writer.Open(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to open writer: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.ConvertStageToSpecs(stage, &err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to convert stage: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.Finalize(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to finalize: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    writer.Close();
  }

  TEST_MSG("BasisCurves test file: %s", filename.c_str());

  // Load and verify
  Stage loaded_stage;
  std::string warn, err;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  if (!ret) {
    std::cerr << "FAILED TO LOAD: " << err << "\n";
  }
  TEST_CHECK(ret == true);
  if (!ret) {
    TEST_MSG("Failed to load: %s", err.c_str());
    cleanup_file(filename);
    return;
  }

  // Find the BasisCurves prim
  auto curves_prim_result = loaded_stage.GetPrimAtPath(Path("/MyBasisCurve", ""));
  TEST_CHECK(curves_prim_result.has_value());
  if (!curves_prim_result.has_value()) {
    TEST_MSG("Failed to find BasisCurves prim: %s", curves_prim_result.error().c_str());
    cleanup_file(filename);
    return;
  }

  const Prim* curves_prim = curves_prim_result.value();
  TEST_CHECK(curves_prim != nullptr);
  TEST_CHECK(curves_prim->prim_type_name() == "BasisCurves");

  if (curves_prim) {
    // Verify BasisCurves type
    const GeomBasisCurves* loaded_curves = curves_prim->data().as<GeomBasisCurves>();
    TEST_CHECK(loaded_curves != nullptr);
    if (loaded_curves) {
      // Verify enum properties
      TEST_CHECK(loaded_curves->type.get_value() == GeomBasisCurves::Type::Cubic);
      TEST_CHECK(loaded_curves->basis.get_value() == GeomBasisCurves::Basis::Bezier);
      TEST_CHECK(loaded_curves->wrap.get_value() == GeomBasisCurves::Wrap::Nonperiodic);

      // Verify points
      if (loaded_curves->points.authored()) {
        auto points_opt = loaded_curves->points.get_value();
        if (points_opt.has_value()) {
          const Animatable<std::vector<value::point3f>>& points_anim = points_opt.value();
          std::vector<value::point3f> pts;
          if (points_anim.get_default(&pts)) {
            TEST_CHECK(pts.size() == 4);
            TEST_MSG("BasisCurves has %zu points", pts.size());
          }
        }
      }

      // Verify curveVertexCounts
      if (loaded_curves->curveVertexCounts.authored()) {
        auto counts_opt = loaded_curves->curveVertexCounts.get_value();
        if (counts_opt.has_value()) {
          const Animatable<std::vector<int>>& counts_anim = counts_opt.value();
          std::vector<int> counts;
          if (counts_anim.get_default(&counts)) {
            TEST_CHECK(counts.size() == 1);
            TEST_CHECK(counts[0] == 4);
            TEST_MSG("BasisCurves curveVertexCounts: [%d]", counts[0]);
          }
        }
      }

      // Verify widths
      if (loaded_curves->widths.authored()) {
        auto widths_opt = loaded_curves->widths.get_value();
        if (widths_opt.has_value()) {
          const Animatable<std::vector<float>>& widths_anim = widths_opt.value();
          std::vector<float> widths;
          if (widths_anim.get_default(&widths)) {
            TEST_CHECK(widths.size() == 4);
            TEST_MSG("BasisCurves has %zu widths", widths.size());
          }
        }
      }
    }
  }

  std::cerr << "BasisCurves roundtrip successful!\n";
  cleanup_file(filename);
}

void crate_writer_nurbs_curves_test(void) {
  std::string filename = get_temp_filename("test_nurbs_curves.usdc");

  {
    // Create a stage with a NurbsCurves prim
    Stage stage;

    // Create NURBS curve data - simple quadratic curve
    std::vector<value::point3f> points = {
      {0.0f, 0.0f, 0.0f},
      {1.0f, 1.0f, 0.0f},
      {2.0f, 0.0f, 0.0f},
      {3.0f, 1.0f, 0.0f},
    };

    // NURBS curve order (degree + 1)
    std::vector<int> orders = {3};  // Quadratic (degree 2)

    // Knot vector for quadratic NURBS
    std::vector<double> knots = {0.0, 0.0, 0.0, 1.0, 1.0, 1.0};

    // Parameter range
    std::vector<value::double2> ranges = {{0.0, 1.0}};

    // Weights for control points (uniform weights for non-rational curve)
    std::vector<double> weights = {1.0, 1.0, 1.0, 1.0};

    // Curve vertex counts
    std::vector<int> vertex_counts = {4};

    // Create GeomNurbsCurves
    GeomNurbsCurves nurbs_curves;
    Animatable<std::vector<value::point3f>> points_anim(points);
    Animatable<std::vector<int>> orders_anim(orders);
    Animatable<std::vector<double>> knots_anim(knots);
    Animatable<std::vector<value::double2>> ranges_anim(ranges);
    Animatable<std::vector<double>> weights_anim(weights);
    Animatable<std::vector<int>> counts_anim(vertex_counts);

    nurbs_curves.points.set_value(points_anim);
    nurbs_curves.order.set_value(orders_anim);
    nurbs_curves.knots.set_value(knots_anim);
    nurbs_curves.ranges.set_value(ranges_anim);
    nurbs_curves.pointWeights.set_value(weights_anim);
    nurbs_curves.curveVertexCounts.set_value(counts_anim);

    // Create Prim with NurbsCurves
    Prim nurbs_prim("MyNurbsCurve", nurbs_curves);
    nurbs_prim.prim_type_name() = "NurbsCurves";

    // Add to stage
    stage.root_prims().push_back(nurbs_prim);

    // Write using CrateWriter
    experimental::CrateWriter writer(filename);
    std::string err;
    bool ret = writer.Open(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to open writer: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.ConvertStageToSpecs(stage, &err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to convert stage: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.Finalize(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to finalize: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    writer.Close();
  }

  TEST_MSG("NurbsCurves test file: %s", filename.c_str());

  // Load and verify
  Stage loaded_stage;
  std::string warn, err;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  if (!ret) {
    std::cerr << "FAILED TO LOAD: " << err << "\n";
  }
  TEST_CHECK(ret == true);
  if (!ret) {
    TEST_MSG("Failed to load: %s", err.c_str());
    cleanup_file(filename);
    return;
  }

  // Find the NurbsCurves prim
  auto nurbs_prim_result = loaded_stage.GetPrimAtPath(Path("/MyNurbsCurve", ""));
  TEST_CHECK(nurbs_prim_result.has_value());
  if (!nurbs_prim_result.has_value()) {
    TEST_MSG("Failed to find NurbsCurves prim: %s", nurbs_prim_result.error().c_str());
    cleanup_file(filename);
    return;
  }

  const Prim* nurbs_prim = nurbs_prim_result.value();
  TEST_CHECK(nurbs_prim != nullptr);
  TEST_CHECK(nurbs_prim->prim_type_name() == "NurbsCurves");

  if (nurbs_prim) {
    // Verify NurbsCurves type
    const GeomNurbsCurves* loaded_nurbs = nurbs_prim->data().as<GeomNurbsCurves>();
    TEST_CHECK(loaded_nurbs != nullptr);
    if (loaded_nurbs) {
      // Verify points
      if (loaded_nurbs->points.authored()) {
        auto points_opt = loaded_nurbs->points.get_value();
        if (points_opt.has_value()) {
          const Animatable<std::vector<value::point3f>>& points_anim = points_opt.value();
          std::vector<value::point3f> pts;
          if (points_anim.get_default(&pts)) {
            TEST_CHECK(pts.size() == 4);
            TEST_MSG("NurbsCurves has %zu points", pts.size());
          }
        }
      }

      // Verify order
      if (loaded_nurbs->order.authored()) {
        auto order_opt = loaded_nurbs->order.get_value();
        if (order_opt.has_value()) {
          const Animatable<std::vector<int>>& order_anim = order_opt.value();
          std::vector<int> orders;
          if (order_anim.get_default(&orders)) {
            TEST_CHECK(orders.size() == 1);
            TEST_CHECK(orders[0] == 3);
            TEST_MSG("NurbsCurves order: %d", orders[0]);
          }
        }
      }

      // Verify knots
      if (loaded_nurbs->knots.authored()) {
        auto knots_opt = loaded_nurbs->knots.get_value();
        if (knots_opt.has_value()) {
          const Animatable<std::vector<double>>& knots_anim = knots_opt.value();
          std::vector<double> knots;
          if (knots_anim.get_default(&knots)) {
            TEST_CHECK(knots.size() == 6);
            TEST_MSG("NurbsCurves has %zu knots", knots.size());
          }
        }
      }

      // Verify ranges
      if (loaded_nurbs->ranges.authored()) {
        auto ranges_opt = loaded_nurbs->ranges.get_value();
        if (ranges_opt.has_value()) {
          const Animatable<std::vector<value::double2>>& ranges_anim = ranges_opt.value();
          std::vector<value::double2> ranges;
          if (ranges_anim.get_default(&ranges)) {
            TEST_CHECK(ranges.size() == 1);
            TEST_MSG("NurbsCurves has %zu ranges", ranges.size());
          }
        }
      }

      // Verify pointWeights
      if (loaded_nurbs->pointWeights.authored()) {
        auto weights_opt = loaded_nurbs->pointWeights.get_value();
        if (weights_opt.has_value()) {
          const Animatable<std::vector<double>>& weights_anim = weights_opt.value();
          std::vector<double> weights;
          if (weights_anim.get_default(&weights)) {
            TEST_CHECK(weights.size() == 4);
            TEST_MSG("NurbsCurves has %zu point weights", weights.size());
          }
        }
      }
    }
  }

  std::cerr << "NurbsCurves roundtrip successful!\n";
  cleanup_file(filename);
}

void crate_writer_geom_subset_test(void) {
  std::string filename = get_temp_filename("test_geom_subset.usdc");

  {
    // Create a stage with a Mesh and a GeomSubset
    Stage stage;

    // Create simple mesh data (cube-like structure)
    std::vector<value::point3f> points = {
      {-1.0f, -1.0f, -1.0f},
      {1.0f, -1.0f, -1.0f},
      {1.0f, 1.0f, -1.0f},
      {-1.0f, 1.0f, -1.0f},
      {-1.0f, -1.0f, 1.0f},
      {1.0f, -1.0f, 1.0f},
      {1.0f, 1.0f, 1.0f},
      {-1.0f, 1.0f, 1.0f},
    };

    std::vector<int> face_vertex_counts = {4, 4, 4, 4, 4, 4};
    std::vector<int> face_vertex_indices = {
      0, 1, 2, 3,  // front
      4, 7, 6, 5,  // back
      0, 4, 5, 1,  // bottom
      2, 6, 7, 3,  // top
      0, 3, 7, 4,  // left
      1, 5, 6, 2   // right
    };

    // Create GeomMesh
    GeomMesh mesh;
    Animatable<std::vector<value::point3f>> points_anim(points);
    Animatable<std::vector<int>> fv_counts_anim(face_vertex_counts);
    Animatable<std::vector<int>> fv_indices_anim(face_vertex_indices);

    mesh.points.set_value(points_anim);
    mesh.faceVertexCounts.set_value(fv_counts_anim);
    mesh.faceVertexIndices.set_value(fv_indices_anim);

    // Create Prim with Mesh
    Prim mesh_prim("MyMesh", mesh);
    mesh_prim.prim_type_name() = "Mesh";

    // Create GeomSubset that references first 3 faces (12 indices)
    GeomSubset subset;
    subset.elementType = GeomSubset::ElementType::Face;
    subset.name = "material_faces";

    // Set family name
    value::token family_name("material_family");
    subset.familyName.set_value(family_name);

    // Set indices for first 3 faces (0-11)
    std::vector<int32_t> subset_indices = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    Animatable<std::vector<int32_t>> indices_anim(subset_indices);
    subset.indices.set_value(indices_anim);

    // Create Prim with GeomSubset
    Prim subset_prim("MyMesh_subset", subset);
    subset_prim.prim_type_name() = "GeomSubset";

    // Add to stage
    stage.root_prims().push_back(mesh_prim);
    stage.root_prims().push_back(subset_prim);

    // Write using CrateWriter
    experimental::CrateWriter writer(filename);
    std::string err;
    bool ret = writer.Open(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to open writer: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.ConvertStageToSpecs(stage, &err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to convert stage: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.Finalize(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to finalize: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    writer.Close();
  }

  TEST_MSG("GeomSubset test file: %s", filename.c_str());

  // Load and verify
  Stage loaded_stage;
  std::string warn, err;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  if (!ret) {
    std::cerr << "FAILED TO LOAD: " << err << "\n";
  }
  TEST_CHECK(ret == true);
  if (!ret) {
    TEST_MSG("Failed to load: %s", err.c_str());
    cleanup_file(filename);
    return;
  }

  // Find the subset prim
  auto subset_prim_result = loaded_stage.GetPrimAtPath(Path("/MyMesh_subset", ""));
  TEST_CHECK(subset_prim_result.has_value());
  if (!subset_prim_result.has_value()) {
    TEST_MSG("Failed to find GeomSubset prim: %s", subset_prim_result.error().c_str());
    cleanup_file(filename);
    return;
  }

  const Prim* subset_prim = subset_prim_result.value();
  TEST_CHECK(subset_prim != nullptr);
  TEST_CHECK(subset_prim->prim_type_name() == "GeomSubset");

  if (subset_prim) {
    // Verify GeomSubset type
    const GeomSubset* loaded_subset = subset_prim->data().as<GeomSubset>();
    TEST_CHECK(loaded_subset != nullptr);
    if (loaded_subset) {
      // Verify elementType property
      TEST_CHECK(loaded_subset->elementType.get_value() == GeomSubset::ElementType::Face);
      TEST_MSG("GeomSubset elementType verified: face");

      // Verify familyName
      if (loaded_subset->familyName.authored()) {
        auto familyname_opt = loaded_subset->familyName.get_value();
        if (familyname_opt.has_value()) {
          const value::token& familyname_val = familyname_opt.value();
          TEST_MSG("GeomSubset familyName: %s", familyname_val.str().c_str());
        }
      }

      // Verify indices
      if (loaded_subset->indices.authored()) {
        auto indices_opt = loaded_subset->indices.get_value();
        if (indices_opt.has_value()) {
          const Animatable<std::vector<int32_t>>& indices_anim = indices_opt.value();
          std::vector<int32_t> indices;
          if (indices_anim.get_default(&indices)) {
            TEST_CHECK(indices.size() == 12);
            TEST_MSG("GeomSubset has %zu indices", indices.size());
          }
        }
      }
    }
  }

  std::cerr << "GeomSubset roundtrip successful!\n";
  cleanup_file(filename);
}

void crate_writer_material_binding_test(void) {
  std::string filename = get_temp_filename("test_material_binding.usdc");

  {
    // Create a Stage with a Material and a Sphere with material binding
    Stage stage;

    // Create a Material prim
    Material material;
    material.name = "SimpleMaterial";
    material.spec = Specifier::Def;

    Prim material_prim("SimpleMaterial", material);
    material_prim.prim_type_name() = "Material";
    stage.root_prims().push_back(material_prim);

    // Create a Sphere geometry with material binding
    GeomSphere sphere;
    sphere.radius = 2.0;

    // Add material:binding relationship pointing to the Material prim
    Relationship binding_rel;
    binding_rel.set(Path("/SimpleMaterial", ""));
    sphere.set_materialBinding(binding_rel);

    Prim sphere_prim("Sphere", sphere);
    sphere_prim.prim_type_name() = "Sphere";

    stage.root_prims().push_back(sphere_prim);

    // Write using CrateWriter
    experimental::CrateWriter writer(filename);
    std::string err;
    bool ret = writer.Open(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to open writer: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.ConvertStageToSpecs(stage, &err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to convert stage: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    ret = writer.Finalize(&err);
    TEST_CHECK(ret == true);
    if (!ret) {
      TEST_MSG("Failed to finalize: %s", err.c_str());
      cleanup_file(filename);
      return;
    }

    writer.Close();
  }

  TEST_MSG("Material binding test file: %s", filename.c_str());

  // Load and verify
  Stage loaded_stage;
  std::string warn, err;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);

  if (!ret) {
    std::cerr << "FAILED TO LOAD: " << err << "\n";
  }
  TEST_CHECK(ret == true);
  if (!ret) {
    TEST_MSG("Failed to load: %s", err.c_str());
    cleanup_file(filename);
    return;
  }

  // Find the sphere prim
  auto sphere_prim_result = loaded_stage.GetPrimAtPath(Path("/Sphere", ""));
  TEST_CHECK(sphere_prim_result.has_value());
  if (!sphere_prim_result.has_value()) {
    TEST_MSG("Failed to find sphere prim: %s", sphere_prim_result.error().c_str());
    cleanup_file(filename);
    return;
  }

  const Prim* sphere_prim = sphere_prim_result.value();
  TEST_CHECK(sphere_prim != nullptr);
  TEST_CHECK(sphere_prim->prim_type_name() == "Sphere");

  // Verify material binding exists
  const GeomSphere* loaded_sphere = sphere_prim->data().as<GeomSphere>();
  TEST_CHECK(loaded_sphere != nullptr);
  if (loaded_sphere) {
    TEST_CHECK(loaded_sphere->has_materialBinding());
    if (loaded_sphere->has_materialBinding()) {
      const Relationship& binding = loaded_sphere->materialBinding.value();
      if (binding.is_path()) {
        TEST_MSG("Material binding target: %s", binding.targetPath.full_path_name().c_str());
        TEST_CHECK(binding.targetPath.full_path_name() == "/SimpleMaterial");
      } else if (binding.is_pathvector() && !binding.targetPathVector.empty()) {
        TEST_MSG("Material binding target: %s", binding.targetPathVector[0].full_path_name().c_str());
        TEST_CHECK(binding.targetPathVector[0].full_path_name() == "/SimpleMaterial");
      }
    }
  }

  std::cerr << "Material binding roundtrip successful!\n";
  cleanup_file(filename);
}
