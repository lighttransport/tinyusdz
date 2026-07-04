// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Writer Test

#include <iostream>
#include <fstream>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "next/stage/stage.hh"
#include "next/layer/layer.hh"
#include "next/layer/property-index.hh"
#include "next/types/value.hh"
#include "next/crate/crate-writer.hh"
#include "next/crate/crate-format.hh"
#include "next/writer/usdc-writer.hh"
#include "next/writer/usda-writer.hh"
#include "next/reader/usdc-reader.hh"

using namespace tinyusdz::next;

bool contains(const std::string& str, const std::string& substr) {
  return str.find(substr) != std::string::npos;
}

std::vector<uint8_t> read_file_bytes(const std::string& path) {
  std::ifstream ifs(path, std::ios::binary | std::ios::ate);
  assert(ifs.is_open());
  std::vector<uint8_t> bytes(static_cast<size_t>(ifs.tellg()));
  ifs.seekg(0);
  if (!bytes.empty()) {
    ifs.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
    assert(static_cast<size_t>(ifs.gcount()) == bytes.size());
  }
  return bytes;
}

std::string read_file_text(const std::string& path) {
  std::ifstream ifs(path, std::ios::binary);
  assert(ifs.is_open());
  return std::string((std::istreambuf_iterator<char>(ifs)),
                     std::istreambuf_iterator<char>());
}

void test_crate_writer_basic() {
  std::cout << "Testing crate-writer basic...\n";

  // Create a simple stage
  StageBuilder stage_builder;
  stage_builder.SetDefaultPrim("Root");
  stage_builder.SetUpAxis("Y");

  LayerBuilder& layer = stage_builder.GetLayerBuilder();

  // Create root prim
  layer.begin_prim("Root", "Xform");
  layer.end_prim();

  // Create a mesh
  layer.begin_prim("Cube", "Mesh");

  std::vector<float> points = {
    -1, -1, -1,
     1, -1, -1,
     1,  1, -1,
    -1,  1, -1,
    -1, -1,  1,
     1, -1,  1,
     1,  1,  1,
    -1,  1,  1
  };
  layer.add_property("points", Value::MakeFloat3Array(points));

  std::vector<int> faceVertexCounts = {4, 4, 4, 4, 4, 4};
  layer.add_property("faceVertexCounts", Value::MakeIntArray(faceVertexCounts));

  layer.end_prim();
  layer.finalize();

  Stage stage = stage_builder.Build();

  // Write to memory buffer
  std::vector<uint8_t> buffer;
  CrateWriter writer;
  CrateWriteResult result = writer.WriteToMemory(buffer, stage);

  std::cout << "  Write result: " << (result.success ? "success" : "failed") << "\n";
  if (!result.success) {
    std::cout << "  Error: " << result.error << "\n";
  }
  assert(result.success);

  std::cout << "  Bytes written: " << result.bytes_written << "\n";
  std::cout << "  Token count: " << result.token_count << "\n";
  std::cout << "  Path count: " << result.path_count << "\n";
  std::cout << "  Spec count: " << result.spec_count << "\n";
  std::cout << "  Field count: " << result.field_count << "\n";

  // Verify magic number
  assert(buffer.size() >= 8);
  assert(std::memcmp(buffer.data(), kCrateMagic, 8) == 0);
  std::cout << "  Magic number verified\n";

  // Verify version
  uint8_t version_major = buffer[8];
  uint8_t version_minor = buffer[9];
  uint8_t version_patch = buffer[10];
  std::cout << "  Version: " << (int)version_major << "."
            << (int)version_minor << "." << (int)version_patch << "\n";

  std::cout << "  crate-writer basic test passed!\n\n";
}

void test_usdc_writer_file() {
  std::cout << "Testing usdc-writer file output...\n";

  // Create a stage with materials
  StageBuilder stage_builder;
  stage_builder.SetDefaultPrim("World");
  stage_builder.SetUpAxis("Z");
  stage_builder.SetMetersPerUnit(1.0);

  LayerBuilder& layer = stage_builder.GetLayerBuilder();

  // Create hierarchy
  layer.begin_prim("World", "Xform");
  layer.end_prim();

  layer.begin_prim("Geometry", "Scope");
  layer.end_prim();

  layer.begin_prim("Sphere", "Sphere");
  layer.add_property("radius", Value(2.0));
  layer.end_prim();

  layer.begin_prim("Materials", "Scope");
  layer.end_prim();

  layer.begin_prim("GoldMaterial", "Material");
  layer.add_property("inputs:roughness", Value(0.3f));
  layer.add_property("inputs:metallic", Value(1.0f));
  layer.end_prim();

  layer.finalize();

  Stage stage = stage_builder.Build();

  // Write to file
  const char* test_file = "/tmp/test_output.usdc";
  USDCWriteOptions opts;
  opts.write_usda_debug = true;  // Also write USDA for inspection

  USDCWriteResult result = WriteUSDCToFile(test_file, stage, opts);

  std::cout << "  Write result: " << (result.success ? "success" : "failed") << "\n";
  if (!result.success) {
    std::cout << "  Error: " << result.error << "\n";
  }
  assert(result.success);

  std::cout << "  Wrote " << result.bytes_written << " bytes to " << test_file << "\n";
  std::cout << "  Tokens: " << result.token_count << "\n";
  std::cout << "  Paths: " << result.path_count << "\n";
  std::cout << "  Specs: " << result.spec_count << "\n";

  // Verify file exists and has content
  std::ifstream check(test_file, std::ios::binary | std::ios::ate);
  assert(check.is_open());
  size_t file_size = check.tellg();
  assert(file_size == result.bytes_written);
  std::cout << "  File size verified: " << file_size << " bytes\n";
  check.close();
  std::string debug_usda = read_file_text("/tmp/test_output.usda");
  assert(debug_usda == WriteUSDAToString(stage));
  std::remove(test_file);
  std::remove("/tmp/test_output.usda");

  std::cout << "  usdc-writer file test passed!\n\n";
}

void test_usdc_roundtrip() {
  std::cout << "Testing USDC roundtrip (write -> read -> USDA text)...\n";

  // Create a stage
  StageBuilder stage_builder;
  stage_builder.SetDefaultPrim("TestPrim");

  LayerBuilder& layer = stage_builder.GetLayerBuilder();

  layer.begin_prim("TestPrim", "Xform");
  layer.end_prim();

  layer.begin_prim("Cube", "Mesh");
  std::vector<float> points = {0, 0, 0, 1, 0, 0, 1, 1, 0};
  layer.add_property("points", Value::MakeFloat3Array(points));
  layer.end_prim();

  layer.finalize();

  Stage stage = stage_builder.Build();

  // Write to memory and read back through the high-level USDC reader.
  std::vector<uint8_t> buffer;
  USDCWriteResult write_result = WriteUSDCToMemory(buffer, stage);
  assert(write_result.success);
  std::cout << "  Written " << write_result.bytes_written << " bytes\n";

  assert(buffer.size() == write_result.bytes_written);
  assert(buffer.size() >= 64);
  assert(std::memcmp(buffer.data(), kCrateMagic, 8) == 0);

  USDCLoadResult read_result = LoadUSDCFromMemory(buffer.data(), buffer.size());
  assert(read_result.success);
  std::string actual_usda = WriteUSDAToString(read_result.stage);
  assert(read_result.stage.GetPrimCount() == stage.GetPrimCount());
  assert(read_result.stage.GetMeta().defaultPrim == "TestPrim");
  UsdPrim root = read_result.stage.GetPrimAtPath("/TestPrim");
  assert(root.IsValid());
  assert(root.GetTypeName() == "Xform");
  UsdPrim cube = read_result.stage.GetPrimAtPath("/Cube");
  assert(cube.IsValid());
  assert(cube.GetTypeName() == "Mesh");
  const Value* read_points = cube.GetPropertyValue("points");
  assert(read_points != nullptr);
  assert(read_points->array_size() == 3);
  const std::vector<float>* read_points_array = read_points->as_float_array();
  assert(read_points_array != nullptr);
  assert(*read_points_array == points);
  assert(contains(actual_usda, "def Xform \"TestPrim\""));
  assert(contains(actual_usda, "def Mesh \"Cube\""));
  assert(contains(actual_usda, "float3[] points"));

  std::cout << "  USDC roundtrip test passed!\n\n";
}

void test_usdc_stage_backend_parity() {
  std::cout << "Testing USDC Stage memory/file backend parity...\n";

  StageBuilder stage_builder;
  stage_builder.SetDefaultPrim("Root");
  stage_builder.SetUpAxis("Z");
  LayerBuilder& layer = stage_builder.GetLayerBuilder();

  layer.begin_prim("Root", "Xform");
  layer.add_property("visibility", Value::MakeToken("inherited"));
  layer.end_prim();

  layer.begin_prim("Mesh", "Mesh");
  layer.add_property("faceVertexCounts", Value::MakeIntArray({3}));
  layer.add_property("faceVertexIndices", Value::MakeIntArray({0, 1, 2}));
  layer.add_property("points", Value::MakeFloat3Array({
      0.0f, 0.0f, 0.0f,
      1.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f}));
  layer.end_prim();
  layer.finalize();

  Stage stage = stage_builder.Build();
  std::vector<uint8_t> memory;
  USDCWriteResult memory_result = WriteUSDCToMemory(memory, stage);
  assert(memory_result.success);
  assert(memory_result.bytes_written == memory.size());

  const std::string path = "/tmp/tinyusdz_next_usdc_stage_backend_parity.usdc";
  USDCWriteResult file_result = WriteUSDCToFile(path, stage);
  assert(file_result.success);
  assert(file_result.bytes_written == memory_result.bytes_written);
  std::vector<uint8_t> file_bytes = read_file_bytes(path);
  assert(file_bytes == memory);
  std::remove(path.c_str());

  std::cout << "  USDC Stage backend parity test passed!\n\n";
}

void test_layer_to_usdc() {
  std::cout << "Testing Layer to USDC...\n";

  // Create a layer directly
  Layer layer;
  LayerBuilder builder(layer);

  layer.meta().defaultPrim = "Asset";
  layer.meta().upAxis = "Y";

  builder.begin_prim("Asset", "Xform");
  builder.end_prim();

  builder.begin_prim("Model", "Mesh");
  builder.add_property("subdivisionScheme", Value::MakeToken("catmullClark"));
  builder.end_prim();

  builder.finalize();

  // Write layer directly
  std::vector<uint8_t> buffer;
  USDCWriteResult result = WriteLayerToUSDCMemory(buffer, layer);

  assert(result.success);
  std::cout << "  Layer written: " << result.bytes_written << " bytes\n";
  std::cout << "  Specs: " << result.spec_count << "\n";

  // Verify magic
  assert(buffer.size() >= 8);
  assert(std::memcmp(buffer.data(), kCrateMagic, 8) == 0);

  std::cout << "  Layer to USDC test passed!\n\n";
}

void test_usdc_layer_backend_parity() {
  std::cout << "Testing USDC Layer memory/file backend parity...\n";

  Layer layer;
  layer.meta().defaultPrim = "Asset";
  layer.meta().upAxis = "Y";
  LayerBuilder builder(layer);
  builder.begin_prim("Asset", "Xform");
  builder.add_property("purpose", Value::MakeToken("render"));
  builder.end_prim();
  builder.begin_prim("Geom", "Mesh");
  builder.add_property("points", Value::MakeFloat3Array({
      -1.0f, 0.0f, 0.0f,
       1.0f, 0.0f, 0.0f,
       0.0f, 1.0f, 0.0f}));
  builder.end_prim();
  builder.finalize();

  std::vector<uint8_t> memory;
  USDCWriteResult memory_result = WriteLayerToUSDCMemory(memory, layer);
  assert(memory_result.success);
  assert(memory_result.bytes_written == memory.size());

  const std::string path = "/tmp/tinyusdz_next_usdc_layer_backend_parity.usdc";
  USDCWriteResult file_result = WriteLayerToUSDCFile(path, layer);
  assert(file_result.success);
  assert(file_result.bytes_written == memory_result.bytes_written);
  std::vector<uint8_t> file_bytes = read_file_bytes(path);
  assert(file_bytes == memory);
  std::remove(path.c_str());

  std::cout << "  USDC Layer backend parity test passed!\n\n";
}

void test_usdc_path_check() {
  std::cout << "Testing IsUSDCPath...\n";

  assert(IsUSDCPath("model.usdc") == true);
  assert(IsUSDCPath("model.usd") == true);
  assert(IsUSDCPath("model.USDC") == true);
  assert(IsUSDCPath("model.usda") == false);
  assert(IsUSDCPath("model.txt") == false);
  assert(IsUSDCPath("/path/to/file.usdc") == true);

  std::cout << "  IsUSDCPath test passed!\n\n";
}

void test_usdc_api_error_paths() {
  std::cout << "Testing USDC API error paths...\n";

  StageBuilder stage_builder;
  LayerBuilder& layer = stage_builder.GetLayerBuilder();
  layer.begin_prim("Root", "Xform");
  layer.end_prim();
  layer.finalize();
  Stage stage = stage_builder.Build();

  USDCWriteResult null_write = WriteUSDCToFile(static_cast<const char*>(nullptr), stage);
  assert(!null_write.success);
  assert(!null_write.error.empty());

  USDCLoadResult null_read = LoadUSDCFromFile(static_cast<const char*>(nullptr));
  assert(!null_read.success);
  assert(!null_read.error_summary.empty());

  const uint8_t not_usdc[] = {'n', 'o', 't', 'u', 's', 'd', 'c'};
  assert(!IsUSDCData(not_usdc, sizeof(not_usdc)));
  USDCLoadResult load_result = LoadUSDCFromMemory(not_usdc, sizeof(not_usdc));
  assert(!load_result.success);
  assert(!load_result.error_summary.empty() || !load_result.errors.empty());

  const std::string bad_path = "/tmp/tinyusdz_next_not_usdc.txt";
  std::remove(bad_path.c_str());
  {
    std::ofstream ofs(bad_path, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(not_usdc),
              static_cast<std::streamsize>(sizeof(not_usdc)));
    assert(ofs.good());
  }
  assert(!IsUSDCFile(bad_path.c_str()));
  USDCLoadResult bad_file_result = LoadUSDCFromFile(bad_path);
  assert(!bad_file_result.success);
  assert(!bad_file_result.error_summary.empty() ||
         !bad_file_result.errors.empty());
  std::remove(bad_path.c_str());

  std::vector<uint8_t> buffer;
  USDCWriteResult write_result = WriteUSDCToMemory(buffer, stage);
  assert(write_result.success);
  const std::string good_path = "/tmp/tinyusdz_next_is_usdc_file.usdc";
  std::remove(good_path.c_str());
  {
    std::ofstream ofs(good_path, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(buffer.data()),
              static_cast<std::streamsize>(buffer.size()));
    assert(ofs.good());
  }
  assert(IsUSDCFile(good_path.c_str()));
  USDCLoadResult good_file_result = LoadUSDCFromFile(good_path);
  assert(good_file_result.success);
  std::remove(good_path.c_str());

  std::cout << "  USDC API error path test passed!\n\n";
}

void test_usdc_bool_array_roundtrip() {
  std::cout << "Testing USDC bool array roundtrip...\n";

  StageBuilder stage_builder;
  stage_builder.SetDefaultPrim("Root");
  LayerBuilder& layer = stage_builder.GetLayerBuilder();
  layer.begin_prim("Root", "Xform");
  layer.add_property("boolArray", Value::MakeBoolArray({
      true, false, true, true, false, false, true, false, true}));
  layer.add_property("singleBool", Value(true));
  layer.end_prim();
  layer.finalize();
  Stage stage = stage_builder.Build();

  std::vector<uint8_t> buffer;
  USDCWriteResult write_result = WriteUSDCToMemory(buffer, stage);
  assert(write_result.success);
  assert(!buffer.empty());

  USDCLoadResult read_result = LoadUSDCFromMemory(buffer.data(), buffer.size());
  if (!read_result.success) {
    std::cout << "  Error: " << read_result.error_summary << "\n";
  }
  assert(read_result.success);
  UsdPrim root = read_result.stage.GetPrimAtPath("/Root");
  assert(root.IsValid());

  const Value* bool_array_value = root.GetPropertyValue("boolArray");
  assert(bool_array_value);
  const std::vector<uint8_t>* bool_array = bool_array_value->as_bool_array();
  assert(bool_array);
  assert((*bool_array) == std::vector<uint8_t>({1, 0, 1, 1, 0, 0, 1, 0, 1}));

  const Value* single_bool_value = root.GetPropertyValue("singleBool");
  assert(single_bool_value);
  const bool* single_bool = single_bool_value->as_bool();
  assert(single_bool && *single_bool);

  const std::string usda = WriteUSDAToString(read_result.stage);
  assert(contains(usda, "bool[] boolArray"));
  assert(contains(usda, "bool singleBool = true"));

  std::cout << "  USDC bool array roundtrip test passed!\n\n";
}

void test_usdc_relationship_connection_roundtrip() {
  std::cout << "Testing USDC relationship/connection roundtrip...\n";

  StageBuilder stage_builder;
  stage_builder.SetDefaultPrim("World");
  LayerBuilder& layer = stage_builder.GetLayerBuilder();

  layer.begin_prim("World", "Xform");
  layer.end_prim();

  layer.begin_prim("Mat", "Material");
  layer.add_property("outputs:surface", Value::MakeToken("surface"));
  layer.end_prim();

  layer.begin_prim("Shader", "Shader");
  layer.add_property("outputs:surface", Value::MakeToken("UsdPreviewSurface"));
  layer.end_prim();

  layer.begin_prim("Mesh", "Mesh");
  layer.add_relationship("material:binding", Path("/Mat"));
  PropNameId color_id = GetPropNameTable().intern("inputs:diffuseColor");
  layer.current()->add_property_slot(
      color_id, TypeId::Float3, PropSlot::kFlagConnection);
  layer.current()->set_property_type_name("inputs:diffuseColor", "color3f");
  layer.current()->add_connection("inputs:diffuseColor",
                                  Path("/Shader.outputs:surface"));
  layer.end_prim();
  layer.finalize();
  Stage stage = stage_builder.Build();

  std::vector<uint8_t> buffer;
  USDCWriteResult write_result = WriteUSDCToMemory(buffer, stage);
  assert(write_result.success);
  assert(!buffer.empty());

  USDCLoadResult read_result = LoadUSDCFromMemory(buffer.data(), buffer.size());
  if (!read_result.success) {
    std::cout << "  Error: " << read_result.error_summary << "\n";
  }
  assert(read_result.success);

  UsdPrim mesh = read_result.stage.GetPrimAtPath("/Mesh");
  assert(mesh.IsValid());
  const std::vector<Path>* binding = mesh.GetRelationship("material:binding");
  assert(binding && binding->size() == 1);
  assert((*binding)[0].str() == "/Mat");

  const PrimSpec* spec = mesh.GetPrimSpec();
  assert(spec);
  const std::vector<Path>* conns = spec->connection("inputs:diffuseColor");
  assert(conns && conns->size() == 1);
  assert((*conns)[0].str() == "/Shader.outputs:surface");
  const PropSlot* slot = spec->property("inputs:diffuseColor");
  assert(slot && slot->is_connection());
  assert(spec->property_value("inputs:diffuseColor") == nullptr);

  const std::string usda = WriteUSDAToString(read_result.stage);
  assert(contains(usda, "rel material:binding"));
  assert(contains(usda, "</Mat>"));
  assert(contains(usda, "inputs:diffuseColor.connect"));
  assert(contains(usda, "</Shader.outputs:surface>"));

  std::cout << "  USDC relationship/connection roundtrip test passed!\n\n";
}

void test_usdc_encode_value_fallback_roundtrip() {
  std::cout << "Testing USDC EncodeValue fallback roundtrip...\n";

  StageBuilder stage_builder;
  stage_builder.SetDefaultPrim("Root");
  LayerBuilder& layer = stage_builder.GetLayerBuilder();

  float matrix4f[16] = {
      1.0f, 2.0f, 3.0f, 4.0f,
      5.0f, 6.0f, 7.0f, 8.0f,
      9.0f, 10.0f, 11.0f, 12.0f,
      13.0f, 14.0f, 15.0f, 16.0f};
  Dict nested;
  nested.set("label", Value("inner"));
  nested.set("weight", Value(2.5));
  Dict dict;
  dict.set("name", Value("fallback"));
  dict.set("asset", Value::MakeAssetPath("tex/albedo.png"));
  dict.set("nested", Value::MakeDictionary(std::move(nested)));

  layer.begin_prim("Root", "Xform");
  layer.add_property("i64", Value(int64_t(-1234567890123LL)));
  layer.add_property("u64", Value(uint64_t(1234567890123ULL)));
  layer.add_property("f2", Value::MakeFloat2(1.25f, -2.5f));
  layer.add_property("f4", Value::MakeFloat4(1.0f, 2.0f, 3.0f, 4.0f));
  layer.add_property("d2", Value::MakeDouble2(10.0, -20.0));
  layer.add_property("d3", Value::MakeDouble3(1.0, 2.0, 3.0));
  layer.add_property("m4f", Value::MakeMatrix4f(matrix4f));
  layer.add_property("str", Value("hello"));
  layer.add_property("tok", Value::MakeToken("render"));
  layer.add_property("asset", Value::MakeAssetPath("model.usda"));
  layer.add_property("dict", Value::MakeDictionary(std::move(dict)));
  layer.end_prim();
  layer.finalize();
  Stage stage = stage_builder.Build();

  std::vector<uint8_t> buffer;
  USDCWriteResult write_result = WriteUSDCToMemory(buffer, stage);
  assert(write_result.success);
  assert(!buffer.empty());

  USDCLoadResult read_result = LoadUSDCFromMemory(buffer.data(), buffer.size());
  if (!read_result.success) {
    std::cout << "  Error: " << read_result.error_summary << "\n";
  }
  assert(read_result.success);

  UsdPrim root = read_result.stage.GetPrimAtPath("/Root");
  assert(root.IsValid());
  auto prop = [&](const char* name) -> const Value* {
    const Value* v = root.GetPropertyValue(name);
    assert(v && "expected property missing");
    return v;
  };

  assert(prop("i64")->as_int64() && *prop("i64")->as_int64() == -1234567890123LL);
  assert(prop("u64")->as_uint64() && *prop("u64")->as_uint64() == 1234567890123ULL);
  assert(prop("f2")->as_float2() && prop("f2")->as_float2()[0] == 1.25f &&
         prop("f2")->as_float2()[1] == -2.5f);
  assert(prop("f4")->as_float4() && prop("f4")->as_float4()[3] == 4.0f);
  assert(prop("d2")->as_double2() && prop("d2")->as_double2()[1] == -20.0);
  assert(prop("d3")->as_double3() && prop("d3")->as_double3()[2] == 3.0);
  // Matrix4f is encoded in crate as Matrix4d, matching OpenUSD's crate type.
  assert(prop("m4f")->as_matrix4d());
  for (int i = 0; i < 16; ++i) {
    assert(prop("m4f")->as_matrix4d()[i] == static_cast<double>(matrix4f[i]));
  }
  assert(prop("str")->as_string() && *prop("str")->as_string() == "hello");
  assert(prop("tok")->as_token() && *prop("tok")->as_token() == "render");
  assert(prop("asset")->as_asset_path() &&
         *prop("asset")->as_asset_path() == "model.usda");

  const Dict* rd = prop("dict")->as_dictionary();
  assert(rd);
  const Value* name = rd->find("name");
  assert(name && name->as_string() && *name->as_string() == "fallback");
  const Value* asset = rd->find("asset");
  assert(asset && asset->as_asset_path() &&
         *asset->as_asset_path() == "tex/albedo.png");
  const Value* nested_value = rd->find("nested");
  assert(nested_value && nested_value->as_dictionary());
  const Value* label = nested_value->as_dictionary()->find("label");
  assert(label && label->as_string() && *label->as_string() == "inner");
  const Value* weight = nested_value->as_dictionary()->find("weight");
  assert(weight && weight->as_double() && *weight->as_double() == 2.5);

  std::cout << "  USDC EncodeValue fallback roundtrip test passed!\n\n";
}

void test_usdc_value_block_roundtrip() {
  std::cout << "Testing USDC ValueBlock roundtrip...\n";

  StageBuilder stage_builder;
  stage_builder.SetDefaultPrim("Root");
  LayerBuilder& layer = stage_builder.GetLayerBuilder();

  layer.begin_prim("Mesh", "Mesh");
  layer.current()->set_property_type_name("primvars:displayColor:indices", "int[]");
  layer.add_property("primvars:displayColor:indices", Value::MakeBlock());
  layer.end_prim();
  layer.finalize();

  Stage stage = stage_builder.Build();
  std::vector<uint8_t> buffer;
  USDCWriteResult write_result = WriteUSDCToMemory(buffer, stage);
  assert(write_result.success);
  USDCLoadResult read_result = LoadUSDCFromMemory(buffer.data(), buffer.size());
  assert(read_result.success);

  UsdPrim mesh = read_result.stage.GetPrimAtPath("/Mesh");
  assert(mesh.IsValid());
  const Value* prop = mesh.GetPropertyValue("primvars:displayColor:indices");
  assert(prop && prop->is_block());

  std::cout << "  USDC ValueBlock roundtrip test passed!\n\n";
}

void test_usdc_thread_count_parity() {
  std::cout << "Testing USDC writer thread-count parity...\n";

  StageBuilder stage_builder;
  stage_builder.SetDefaultPrim("P0");
  LayerBuilder& layer = stage_builder.GetLayerBuilder();

  // Large enough to exercise the threaded structural-section preparation path,
  // while keeping the unit test cheap. The older per-prim map-reduce writer path
  // is intentionally disabled; this test locks byte identity for the remaining
  // supported threaded writer work.
  constexpr int kPrimCount = 3000;
  for (int i = 0; i < kPrimCount; ++i) {
    const std::string name = "P" + std::to_string(i);
    layer.begin_prim(name, "Xform");
    layer.add_property("id", Value(i));
    layer.add_property("purpose", Value::MakeToken((i % 2) ? "proxy" : "render"));
    layer.end_prim();
  }
  layer.finalize();
  Stage stage = stage_builder.Build();

  USDCWriteOptions serial_opts;
  serial_opts.crate_options.num_threads = 1;
  std::vector<uint8_t> serial;
  USDCWriteResult serial_result = WriteUSDCToMemory(serial, stage, serial_opts);
  assert(serial_result.success);
  assert(serial_result.bytes_written == serial.size());

  USDCWriteOptions threaded_opts;
  threaded_opts.crate_options.num_threads = 8;
  std::vector<uint8_t> threaded;
  USDCWriteResult threaded_result = WriteUSDCToMemory(threaded, stage, threaded_opts);
  assert(threaded_result.success);
  assert(threaded_result.bytes_written == threaded.size());

  assert(threaded == serial);

  USDCLoadResult read_result =
      LoadUSDCFromMemoryBorrowed(threaded.data(), threaded.size());
  assert(read_result.success);
  assert(read_result.stage.GetPrimCount() == static_cast<size_t>(kPrimCount));
  UsdPrim last = read_result.stage.GetPrimAtPath("/P2999");
  assert(last.IsValid());
  const Value* id = last.GetPropertyValue("id");
  assert(id && id->as_int() && *id->as_int() == 2999);

  std::cout << "  USDC writer thread-count parity test passed!\n\n";
}

// Regression: flatten-output nondeterminism class 1 (fixed by sorting
// PropIndex slots by NAME). PropNameId assignment order is run-unstable
// (names are interned concurrently while layers load in parallel), and the
// crate writer emits the `properties` field, property specs and value blocks
// in slots() order — so an id-derived slot order leaked run-to-run
// nondeterminism into the crate bytes. Simulate an id order that DISAGREES
// with name order (intern the lexicographically-later name first) and assert
// (a) slots() come out name-sorted, (b) the crate bytes are independent of
// property ADD order.
void test_usdc_slot_order_interning_independent() {
  std::cout << "Testing slot-order independence from PropNameId order...\n";

  PropNameTable& table = GetPropNameTable();
  // Unique names for this test; interning "zz" BEFORE "aa" gives it the
  // SMALLER PropNameId, so id order contradicts name order.
  const char* kLate = "zz_slot_order_regression";
  const char* kEarly = "aa_slot_order_regression";
  PropNameId zz = table.intern(kLate);
  PropNameId aa = table.intern(kEarly);
  (void)zz;
  (void)aa;

  auto build = [&](bool reversed) -> Layer {
    Layer layer;
    PrimSpec prim("Shape", "Xform");
    prim.set_path(Path("/Shape"));
    const std::vector<double> darr = {0.5600000023841858, -1.25, 42.0};
    if (!reversed) {
      prim.add_property(kLate, Value::MakeDoubleArray(darr));
      prim.add_property(kEarly, Value(int32_t(7)));
    } else {
      prim.add_property(kEarly, Value(int32_t(7)));
      prim.add_property(kLate, Value::MakeDoubleArray(darr));
    }
    layer.add_prim(std::move(prim));
    layer.finalize();
    return layer;
  };

  Layer l1 = build(false);
  const PrimSpec* p = l1.prim_at_path("/Shape");
  assert(p && p->properties().slots().size() == 2);
  // Canonical order is NAME order ("aa..." first), NOT intern-id order
  // ("zz..." was interned first and has the smaller id).
  assert(table.get(p->properties().slots()[0].name_id) == kEarly &&
         "slots must be name-sorted, not PropNameId-sorted");
  assert(table.get(p->properties().slots()[1].name_id) == kLate);
  // find() by id must still resolve both post-sort.
  assert(p->property(kEarly) && p->property(kLate));

  Layer l2 = build(true);
  std::vector<uint8_t> b1, b2;
  CrateWriter w1, w2;
  assert(w1.WriteLayerToMemory(b1, l1).success);
  assert(w2.WriteLayerToMemory(b2, l2).success);
  assert(b1 == b2 && "crate bytes must be independent of property add order");

  std::cout << "  slot-order interning-independence test passed!\n\n";
}

// Shared fixture for the consume/determinism tests: a prim with a double[]
// default (the value class behind the Island 2-byte nondeterminism), a
// non-inlinable double scalar, an AssetPath default and time samples.
static Layer BuildConsumeFixtureLayer() {
  Layer layer;
  PrimSpec prim("Geom", "Mesh");
  prim.set_path(Path("/Geom"));
  prim.add_property("param",
                    Value::MakeDoubleArray({0.5600000023841858, 1.5, -2.25}));
  prim.add_property("weight", Value(3.14159265358979));  // Double block
  prim.add_property("file", Value::MakeAssetPath("textures/albedo.png"));
  PropNameId anim = GetPropNameTable().intern("anim_value");
  prim.add_time_sample(anim, 0.0, Value(1.0));
  prim.add_time_sample(anim, 10.0, Value(2.0));
  layer.add_prim(std::move(prim));
  layer.finalize();
  return layer;
}

// Regression: CrateWriteOptions::consume_values must be byte-neutral, and a
// consuming write must keep the layer structurally valid with AssetPath
// defaults intact (post-write asset collection depends on them) while other
// value payloads are released.
void test_usdc_consume_values_byte_identity() {
  std::cout << "Testing consume_values byte identity + AssetPath retention...\n";

  Layer keep = BuildConsumeFixtureLayer();
  Layer consumed = BuildConsumeFixtureLayer();

  CrateWriteOptions keep_opts;  // consume_values = false
  std::vector<uint8_t> b1;
  CrateWriter w1(keep_opts);
  assert(w1.WriteLayerToMemory(b1, keep).success);

  CrateWriteOptions consume_opts;
  consume_opts.consume_values = true;
  std::vector<uint8_t> b2;
  CrateWriter w2(consume_opts);
  assert(w2.WriteLayerToMemory(b2, consumed).success);

  assert(b1 == b2 && "consume_values must not change the output bytes");

  const PrimSpec* p = consumed.prim_at_path("/Geom");
  assert(p);
  // Non-asset values are released (empty but present: slots stay valid).
  const Value* darr = p->property_value("param");
  assert(darr && darr->is_empty() && "double[] payload should be released");
  const Value* dsc = p->property_value("weight");
  assert(dsc && dsc->is_empty() && "double scalar payload should be released");
  // AssetPath defaults survive for post-write asset collection.
  const Value* ap = p->property_value("file");
  assert(ap && ap->as_asset_path() &&
         *ap->as_asset_path() == "textures/albedo.png" &&
         "AssetPath default must survive a consuming write");
  // The non-consuming layer keeps everything.
  const PrimSpec* pk = keep.prim_at_path("/Geom");
  assert(pk && pk->property_value("param") &&
         !pk->property_value("param")->is_empty());

  std::cout << "  consume_values byte identity test passed!\n\n";
}

// Regression: flatten-output nondeterminism class 2 (uninitialized bytes in
// a value block — the double[]-as-scalar encode read raw_data() of an array).
// Cheap in-process guard: writing the same content twice from independently
// built layers must produce identical bytes. (Full-scene determinism is
// checked by scripts/run-next-checks.sh RUN_DETERMINISM.)
void test_usdc_double_write_determinism() {
  std::cout << "Testing double-write byte determinism...\n";
  Layer a = BuildConsumeFixtureLayer();
  Layer b = BuildConsumeFixtureLayer();
  std::vector<uint8_t> ba, bb;
  CrateWriter wa, wb;
  assert(wa.WriteLayerToMemory(ba, a).success);
  assert(wb.WriteLayerToMemory(bb, b).success);
  assert(ba == bb && "two writes of identical content must be byte-identical");
  std::cout << "  double-write determinism test passed!\n\n";
}

int main() {
  std::cout << "=== TinyUSDZ Next USDC Writer Tests ===\n\n";

  try {
    test_crate_writer_basic();
    test_usdc_writer_file();
    test_usdc_roundtrip();
    test_usdc_stage_backend_parity();
    test_layer_to_usdc();
    test_usdc_layer_backend_parity();
    test_usdc_path_check();
    test_usdc_api_error_paths();
    test_usdc_bool_array_roundtrip();
    test_usdc_relationship_connection_roundtrip();
    test_usdc_encode_value_fallback_roundtrip();
    test_usdc_value_block_roundtrip();
    test_usdc_thread_count_parity();
    test_usdc_slot_order_interning_independent();
    test_usdc_consume_values_byte_identity();
    test_usdc_double_write_determinism();

    std::cout << "=== All USDC writer tests passed! ===\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << "\n";
    return 1;
  }
}
