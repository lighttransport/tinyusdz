// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Roundtrip Test
// Creates a stage with multiple schema types, writes to USDC,
// and verifies the binary output by parsing TOC/sections.

#include <iostream>
#include <fstream>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <map>

#include "next/stage/stage.hh"
#include "next/layer/layer.hh"
#include "next/types/value.hh"
#include "next/crate/crate-writer.hh"
#include "next/crate/crate-format.hh"
#include "next/crate/crate-reader.hh"
#include "next/writer/usdc-writer.hh"

using namespace tinyusdz::next;

// ============================================================
// Minimal binary USDC section parser (inline, no reader dependency)
// ============================================================

struct SectionInfo {
  char name[16];
  int64_t start;
  int64_t size;
};

struct TocInfo {
  std::vector<SectionInfo> sections;

  const SectionInfo* find(const char* n) const {
    for (const auto& s : sections) {
      if (std::strncmp(s.name, n, 15) == 0) return &s;
    }
    return nullptr;
  }
};

bool ParseUSDCBinary(const uint8_t* data, size_t size, TocInfo& toc) {
  if (size < 24) return false;
  if (std::memcmp(data, kCrateMagic, 8) != 0) return false;

  // Bootstrap: 8 magic + 8 version/pad = up to reserved byte 15;
  // TOC offset at byte 16-23 (u64 LE)
  uint64_t toc_offset = 0;
  for (int i = 0; i < 8; i++) {
    toc_offset |= static_cast<uint64_t>(data[16 + i]) << (i * 8);
  }

  if (toc_offset + 8 > size) return false;

  // Read TOC
  uint64_t num_sections = 0;
  size_t pos = static_cast<size_t>(toc_offset);
  if (pos + 8 > size) return false;
  for (int i = 0; i < 8; i++) {
    num_sections |= static_cast<uint64_t>(data[pos + i]) << (i * 8);
  }
  pos += 8;

  if (num_sections > 100) return false;

  for (uint64_t i = 0; i < num_sections; i++) {
    if (pos + 16 + 8 + 8 > size) return false;
    SectionInfo si;
    std::memcpy(si.name, data + pos, 16);
    pos += 16;

    int64_t start = 0;
    for (int j = 0; j < 8; j++) {
      start |= static_cast<int64_t>(data[pos + j]) << (j * 8);
    }
    pos += 8;
    si.start = start;

    int64_t sec_size = 0;
    for (int j = 0; j < 8; j++) {
      sec_size |= static_cast<int64_t>(data[pos + j]) << (j * 8);
    }
    pos += 8;
    si.size = sec_size;

    if (start < 0 || sec_size < 0 ||
        static_cast<size_t>(start + sec_size) > size)
      return false;

    toc.sections.push_back(si);
  }
  return true;
}

// Read a uint64 LE from a data pointer
uint64_t ReadU64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v |= static_cast<uint64_t>(p[i]) << (i * 8);
  return v;
}

uint32_t ReadU32(const uint8_t* p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) v |= static_cast<uint32_t>(p[i]) << (i * 8);
  return v;
}

// Read a length-prefixed string (u64 length + data)
std::string ReadString(const uint8_t* data, size_t& pos) {
  uint64_t len = ReadU64(data + pos);
  pos += 8;
  std::string s(reinterpret_cast<const char*>(data + pos),
                static_cast<size_t>(len));
  pos += static_cast<size_t>(len);
  return s;
}

// ============================================================
// Test functions
// ============================================================

void test_roundtrip_schema_types() {
  std::cout << "Testing roundtrip with multiple schema types...\n";

  // Create a stage with various schema types
  StageBuilder stage_builder;
  stage_builder.SetDefaultPrim("World");
  stage_builder.SetUpAxis("Y");
  stage_builder.SetMetersPerUnit(0.01);
  stage_builder.SetTimeCodesPerSecond(24.0);
  stage_builder.SetStartTimeCode(0.0);
  stage_builder.SetEndTimeCode(100.0);

  LayerBuilder& layer = stage_builder.GetLayerBuilder();

  // Xform
  layer.begin_prim("World", "Xform");
  layer.add_property("visibility", Value::MakeToken("inherited"));
  layer.end_prim();

  // Mesh
  layer.begin_prim("Cube", "Mesh");
  std::vector<float> points = {
    -1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1,
    -1, -1,  1, 1, -1,  1, 1, 1,  1, -1, 1,  1
  };
  layer.add_property("points", Value::MakeFloat3Array(points));
  std::vector<int> fvc = {4, 4, 4, 4, 4, 4};
  layer.add_property("faceVertexCounts", Value::MakeIntArray(fvc));
  layer.add_property("doubleSided", Value(true));
  layer.add_property("subdivisionScheme", Value::MakeToken("none"));
  layer.end_prim();

  // Camera
  layer.begin_prim("Camera1", "Camera");
  layer.add_property("focalLength", Value(50.0));
  layer.add_property("fStop", Value(2.8f));
  layer.add_property("horizontalAperture", Value(36.0));
  layer.end_prim();

  // Sphere
  layer.begin_prim("Sphere1", "Sphere");
  layer.add_property("radius", Value(1.5));
  layer.end_prim();

  // Capsule
  layer.begin_prim("Capsule1", "Capsule");
  layer.add_property("radius", Value(0.5));
  layer.add_property("height", Value(2.0));
  layer.end_prim();

  // Cylinder
  layer.begin_prim("Cylinder1", "Cylinder");
  layer.add_property("radius", Value(0.75));
  layer.add_property("height", Value(3.0));
  layer.add_property("axis", Value::MakeToken("Z"));
  layer.end_prim();

  // Material with shader
  layer.begin_prim("Material1", "Material");
  layer.add_property("inputs:roughness", Value(0.5f));
  layer.add_property("inputs:metallic", Value(1.0f));
  layer.add_property("inputs:baseColor", Value::MakeFloat3(0.2f, 0.8f, 0.3f));
  layer.end_prim();

  layer.begin_prim("Shader1", "Shader");
  layer.add_property("info:id", Value::MakeToken("UsdPreviewSurface"));
  layer.add_property("inputs:diffuseColor", Value::MakeFloat3(0.5f, 0.5f, 0.5f));
  layer.end_prim();

  // Lights
  layer.begin_prim("DistantLight1", "DistantLight");
  layer.add_property("intensity", Value(5000.0f));
  layer.add_property("angle", Value(0.53f));
  layer.end_prim();

  layer.begin_prim("SphereLight1", "SphereLight");
  layer.add_property("intensity", Value(150.0f));
  layer.add_property("radius", Value(0.5));
  layer.end_prim();

  layer.begin_prim("RectLight1", "RectLight");
  layer.add_property("intensity", Value(300.0f));
  layer.add_property("width", Value(1.0));
  layer.add_property("height", Value(0.5));
  layer.end_prim();

  // Prim with metadata
  layer.begin_prim("Helper", "Scope");
  {
    PrimSpec* prim = layer.current();
    assert(prim);
    prim->meta().doc = "A helper scope";
    prim->meta().hidden = true;
  }
  layer.end_prim();

  layer.finalize();

  Stage stage = stage_builder.Build();

  // Write to USDC via CrateWriter (low-level)
  std::vector<uint8_t> buffer;
  CrateWriter writer;
  CrateWriteResult result = writer.WriteToMemory(buffer, stage);

  assert(result.success);
  std::cout << "  Written " << result.bytes_written << " bytes\n";
  std::cout << "  Tokens: " << result.token_count << "\n";
  std::cout << "  Paths: " << result.path_count << "\n";
  std::cout << "  Specs: " << result.spec_count << "\n";
  std::cout << "  Fields: " << result.field_count << "\n";

  // Verify basic structure
  assert(buffer.size() >= kCrateBootstrapSize);
  assert(std::memcmp(buffer.data(), kCrateMagic, 8) == 0);

  // Parse TOC
  TocInfo toc;
  // NOTE: ParseUSDCBinary() populates `toc` as a side effect, so it must run
  // outside assert() — under NDEBUG `assert(expr)` does not evaluate `expr`,
  // which would leave `toc` empty and null-deref the section lookups below.
  const bool toc_parsed = ParseUSDCBinary(buffer.data(), buffer.size(), toc);
  assert(toc_parsed);
  (void)toc_parsed;

  // Verify required sections exist
  auto* tokens_sec = toc.find("TOKENS");
  auto* strings_sec = toc.find("STRINGS");
  auto* fields_sec = toc.find("FIELDS");
  auto* fieldsets_sec = toc.find("FIELDSETS");
  auto* paths_sec = toc.find("PATHS");
  auto* specs_sec = toc.find("SPECS");

  assert(tokens_sec != nullptr);
  assert(strings_sec != nullptr);
  assert(fields_sec != nullptr);
  assert(fieldsets_sec != nullptr);
  assert(paths_sec != nullptr);
  assert(specs_sec != nullptr);

  // VALUE section is not in TOC (pxrUSD stores it between bootstrap
  // and structural sections, not in the TOC). Verify it exists between
  // bootstrap end (64) and first structural section.
  if (toc.sections.size() > 0) {
    assert(toc.sections[0].start > 64);
  }

  // Verify section order
  assert(std::strncmp(toc.sections[0].name, "TOKENS", 6) == 0);
  assert(std::strncmp(toc.sections[1].name, "STRINGS", 7) == 0);
  assert(std::strncmp(toc.sections[2].name, "FIELDS", 6) == 0);
  assert(std::strncmp(toc.sections[3].name, "FIELDSETS", 9) == 0);
  assert(std::strncmp(toc.sections[4].name, "PATHS", 5) == 0);
  assert(std::strncmp(toc.sections[5].name, "SPECS", 5) == 0);

  // Verify sections are non-empty
  assert(tokens_sec->size > 8);
  assert(paths_sec->size >= 8);
  assert(specs_sec->size >= 8);
  assert(fields_sec->size >= 8);
  assert(fieldsets_sec->size >= 8);

  // Verify no overlap between sections
  for (size_t i = 0; i < toc.sections.size(); i++) {
    for (size_t j = i + 1; j < toc.sections.size(); j++) {
      int64_t a_start = toc.sections[i].start;
      int64_t a_end = a_start + toc.sections[i].size;
      int64_t b_start = toc.sections[j].start;
      int64_t b_end = b_start + toc.sections[j].size;
      assert(!(a_start < b_end && b_start < a_end));
    }
  }

  // Parse TOKENS section (LZ4 compressed: num + uncomp_size + comp_size + data)
  {
    const uint8_t* tdata = buffer.data() + static_cast<size_t>(tokens_sec->start);
    uint64_t num_tokens = ReadU64(tdata);

    uint64_t uncomp_size = ReadU64(tdata + 8);
    uint64_t comp_size = ReadU64(tdata + 16);

    // Decompress (using pxrUSD TfFastCompression format: n_chunks prefix + LZ4)
    std::vector<uint8_t> tok_raw;
    if (comp_size == 0) {
      // Uncompressed fallback
      tok_raw.assign(tdata + 24, tdata + 24 + static_cast<size_t>(uncomp_size));
    } else {
      DecompressResult dr = DecompressCrateBlob(
          tdata + 24, static_cast<size_t>(comp_size),
          static_cast<size_t>(uncomp_size));
      assert(dr.success);
      tok_raw = std::move(dr.data);
    }

    // Parse null-terminated token strings
    std::map<std::string, uint32_t> token_map;
    std::vector<std::string> token_list;
    const char* ptr = reinterpret_cast<const char*>(tok_raw.data());
    const char* end = ptr + tok_raw.size();
    uint32_t idx = 0;
    while (ptr < end && idx < num_tokens) {
      std::string tok(ptr);
      token_list.push_back(tok);
      token_map[tok] = idx++;
      ptr += tok.size() + 1;
    }

    assert(token_list.size() == num_tokens);

    // Verify schema tokens exist
    auto check_token = [&](const std::string& t) {
      if (token_map.find(t) == token_map.end()) {
        std::cerr << "  Missing token: \"" << t << "\"\n";
        std::cerr << "  Available tokens (" << token_list.size() << "):";
        for (size_t ti = 0; ti < token_list.size() && ti < 20; ti++)
          std::cerr << " [" << ti << "]=" << token_list[ti];
        if (token_list.size() > 20)
          std::cerr << " ... (" << (token_list.size() - 20) << " more)";
        std::cerr << "\n";
        assert(false);
      }
    };

    // Prim type names
    check_token("Xform");
    check_token("Mesh");
    check_token("Camera");
    check_token("Sphere");
    check_token("Capsule");
    check_token("Cylinder");
    check_token("Material");
    check_token("Shader");
    check_token("DistantLight");
    check_token("SphereLight");
    check_token("RectLight");
    check_token("Scope");

    // Schema field names
    check_token("points");
    check_token("faceVertexCounts");
    check_token("doubleSided");
    check_token("subdivisionScheme");
    check_token("focalLength");
    check_token("fStop");
    check_token("horizontalAperture");
    check_token("radius");
    check_token("height");
    check_token("axis");
    check_token("intensity");
    check_token("angle");
    check_token("width");

    // Common tokens
    check_token("typeName");
    check_token("specifier");
    check_token("defaultPrim");
    check_token("upAxis");
    check_token("metersPerUnit");
    check_token("timeCodesPerSecond");
    check_token("startTimeCode");
    check_token("endTimeCode");
    check_token("active");
    check_token("hidden");
    check_token("doc");

    std::cout << "  Verified " << token_list.size() << " tokens"
              << " (schema type names + field names all present)\n";
  }

  // Write to file for external inspection
  const char* test_file = "/tmp/test_roundtrip_schema.usdc";
  CrateWriteResult file_result = writer.WriteToFile(test_file, stage);
  assert(file_result.success);
  std::cout << "  Wrote to " << test_file << " ("
            << file_result.bytes_written << " bytes)\n";

  // Verify file matches memory
  std::ifstream ifs(test_file, std::ios::binary | std::ios::ate);
  assert(ifs.is_open());
  size_t file_size = static_cast<size_t>(ifs.tellg());
  ifs.close();
  assert(file_size == buffer.size());

  std::cout << "  roundtrip schema types test passed!\n\n";
}

void test_roundtrip_layer_metadata() {
  std::cout << "Testing roundtrip layer metadata...\n";

  // Create layer directly with metadata
  Layer layer;
  layer.meta().defaultPrim = "Root";
  layer.meta().upAxis = "Z";
  layer.meta().metersPerUnit = 1.0;
  layer.meta().timeCodesPerSecond = 30.0;
  layer.meta().startTimeCode = 1.0;
  layer.meta().endTimeCode = 48.0;
  layer.meta().doc = "Test layer";

  LayerBuilder builder(layer);
  builder.begin_prim("Root", "Xform");
  builder.end_prim();
  builder.finalize();

  // Write via CrateWriter directly
  CrateWriter writer;
  std::vector<uint8_t> buffer;
  CrateWriteResult result = writer.WriteLayerToMemory(buffer, layer);
  assert(result.success);

  // Parse TOC and verify
  TocInfo toc;
  // NOTE: ParseUSDCBinary() populates `toc` as a side effect, so it must run
  // outside assert() — under NDEBUG `assert(expr)` does not evaluate `expr`,
  // which would leave `toc` empty and null-deref the section lookups below.
  const bool toc_parsed = ParseUSDCBinary(buffer.data(), buffer.size(), toc);
  assert(toc_parsed);
  (void)toc_parsed;

  auto* tokens_sec = toc.find("TOKENS");
  assert(tokens_sec != nullptr);

  // Verify version in bootstrap
  uint8_t v_major = buffer[8];
  uint8_t v_minor = buffer[9];
  uint8_t v_patch = buffer[10];
  std::cout << "  Version: " << (int)v_major << "." << (int)v_minor << "." << (int)v_patch << "\n";

  std::cout << "  Bytes: " << result.bytes_written
            << "  Tokens: " << result.token_count
            << "  Paths: " << result.path_count
            << "  Specs: " << result.spec_count
            << "  Fields: " << result.field_count << "\n";

  std::cout << "  roundtrip layer metadata test passed!\n\n";
}

void test_roundtrip_time_samples() {
  std::cout << "Testing roundtrip time samples...\n";

  // Create a stage with time-sampled properties
  // (direct CrateWriter test using layer)
  Layer layer;

  LayerBuilder builder(layer);
  builder.begin_prim("Animated", "Xform");

  // Add time-sampled translate
  builder.add_time_sample("xformOp:translate", 0.0,
                           Value::MakeFloat3(0.0f, 0.0f, 0.0f));
  builder.add_time_sample("xformOp:translate", 50.0,
                           Value::MakeFloat3(10.0f, 5.0f, 0.0f));
  builder.add_time_sample("xformOp:translate", 100.0,
                           Value::MakeFloat3(20.0f, 0.0f, 0.0f));

  // Add animated weight
  builder.add_time_sample("weight", 0.0, Value(0.0f));
  builder.add_time_sample("weight", 100.0, Value(1.0f));

  builder.end_prim();
  builder.finalize();

  // Write via CrateWriter directly
  CrateWriter writer;
  std::vector<uint8_t> buffer;
  CrateWriteResult result = writer.WriteLayerToMemory(buffer, layer);
  assert(result.success);

  std::cout << "  Written " << result.bytes_written << " bytes\n";
  std::cout << "  Tokens: " << result.token_count
            << "  Paths: " << result.path_count
            << "  Specs: " << result.spec_count
            << "  Fields: " << result.field_count << "\n";

  // Verify basic structure
  assert(std::memcmp(buffer.data(), kCrateMagic, 8) == 0);

  TocInfo toc;
  // NOTE: ParseUSDCBinary() populates `toc` as a side effect, so it must run
  // outside assert() — under NDEBUG `assert(expr)` does not evaluate `expr`,
  // which would leave `toc` empty and null-deref the section lookups below.
  const bool toc_parsed = ParseUSDCBinary(buffer.data(), buffer.size(), toc);
  assert(toc_parsed);
  (void)toc_parsed;

  auto* ts = toc.find("TOKENS");
  assert(ts != nullptr);

  std::cout << "  roundtrip time samples test passed!\n\n";
}

void test_write_usdc_from_stage_api() {
  std::cout << "Testing WriteUSDC via high-level API...\n";

  StageBuilder stage_builder;
  stage_builder.SetDefaultPrim("Test");
  stage_builder.SetUpAxis("Y");

  LayerBuilder& layer = stage_builder.GetLayerBuilder();
  layer.begin_prim("Test", "Xform");
  layer.add_property("testValue", Value(42));
  layer.end_prim();
  layer.finalize();

  Stage stage = stage_builder.Build();

  // Test WriteUSDCToFile
  const char* test_file = "/tmp/test_api_output.usdc";
  USDCWriteResult file_result = WriteUSDCToFile(test_file, stage);
  assert(file_result.success);
  std::cout << "  WriteUSDCToFile: " << file_result.bytes_written << " bytes\n";

  // Test WriteLayerToUSDCMemory
  const Layer* root_layer = stage.GetRootLayer();
  assert(root_layer != nullptr);
  std::vector<uint8_t> mem_buffer;
  USDCWriteResult mem_result = WriteLayerToUSDCMemory(mem_buffer, *root_layer);
  assert(mem_result.success);
  std::cout << "  WriteLayerToUSDCMemory: " << mem_result.bytes_written << " bytes\n";

  assert(std::memcmp(mem_buffer.data(), kCrateMagic, 8) == 0);

  std::cout << "  WriteUSDC stage API test passed!\n\n";
}

// Vec4f / Matrix4d / Quatf / Double3 arrays must survive a write -> read cycle
// (Phase 8: these were dropped on read with "Unsupported array type").
void test_roundtrip_vec_matrix_arrays() {
  std::cout << "Testing roundtrip vec/matrix/quat arrays...\n";

  Layer layer;
  LayerBuilder b(layer);
  b.begin_prim("Geo", "Mesh");
  std::vector<float> v4 = {1, 2, 3, 4, 5, 6, 7, 8};           // 2 x Vec4f
  std::vector<float> qf = {0, 0, 0, 1, 0, 1, 0, 0};           // 2 x Quatf
  std::vector<double> m4(32);                                 // 2 x Matrix4d
  for (size_t i = 0; i < m4.size(); ++i) m4[i] = double(i) * 0.5;
  std::vector<float> m4f(32);                                 // 2 x Matrix4f
  std::vector<double> m4f_as_d(32);
  for (size_t i = 0; i < m4f.size(); ++i) {
    m4f[i] = float(i) * 0.25f;
    m4f_as_d[i] = double(m4f[i]);
  }
  std::vector<double> d3 = {1.5, 2.5, 3.5, 4.5, 5.5, 6.5};    // 2 x Double3
  b.add_property("v4", Value::MakeFloatCompArray(std::vector<float>(v4), TypeId::Float4, 4));
  b.add_property("qf", Value::MakeFloatCompArray(std::vector<float>(qf), TypeId::Quatf, 4));
  b.add_property("m4", Value::MakeDoubleCompArray(std::vector<double>(m4), TypeId::Matrix4d, 16));
  b.add_property("m4f", Value::MakeFloatCompArray(std::vector<float>(m4f), TypeId::Matrix4f, 16));
  b.add_property("d3", Value::MakeDoubleCompArray(std::vector<double>(d3), TypeId::Double3, 3));
  b.end_prim();
  b.finalize();

  CrateWriter writer;
  std::vector<uint8_t> buf;
  CrateWriteResult wr = writer.WriteLayerToMemory(buf, layer);
  assert(wr.success);

  CrateReader reader;
  CrateReadResult rr = reader.Read(buf.data(), buf.size());
  assert(rr.success && "re-read of vec/matrix arrays failed");
  const Layer* rl = rr.stage.GetRootLayer();
  const PrimSpec* geo = rl->prim_at_path("/Geo");
  assert(geo);

  auto check_f = [&](const char* name, const std::vector<float>& expect, TypeId t) {
    const Value* val = geo->property_value(name);
    assert(val && val->is_array() && val->type_id() == t);
    const std::vector<float>* arr = val->as_float_array();
    assert(arr && *arr == expect);
  };
  auto check_d = [&](const char* name, const std::vector<double>& expect, TypeId t) {
    const Value* val = geo->property_value(name);
    assert(val && val->is_array() && val->type_id() == t);
    const std::vector<double>* arr = val->as_double_array();
    assert(arr && *arr == expect);
  };
  check_f("v4", v4, TypeId::Float4);
  check_f("qf", qf, TypeId::Quatf);
  check_d("m4", m4, TypeId::Matrix4d);
  check_d("m4f", m4f_as_d, TypeId::Matrix4d);
  check_d("d3", d3, TypeId::Double3);

  std::cout << "  vec/matrix/quat array roundtrip passed!\n\n";
}

int main() {
  std::cout << "=== TinyUSDZ Next USDC Roundtrip Tests ===\n\n";

  try {
    test_roundtrip_schema_types();
    test_roundtrip_layer_metadata();
    test_roundtrip_time_samples();
    test_roundtrip_vec_matrix_arrays();
    test_write_usdc_from_stage_api();

    std::cout << "=== All USDC roundtrip tests passed! ===\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << "\n";
    return 1;
  }
}
