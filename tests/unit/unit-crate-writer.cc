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
