// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Roundtrip Test
// Creates a stage with multiple schema types, writes to USDC,
// and verifies the binary output by parsing TOC/sections.

#include <iostream>
#include <fstream>
#include "test-check.hh"
#include <cstdio>
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
#include "next/tinyusdz-next.hh"
#include "next/writer/usdc-writer.hh"

using namespace tinyusdz::next;

static const Value* DictFind(const Value& dv, const char* key) {
  if (!dv.is_dictionary() || !dv.as_dictionary()) return nullptr;
  return dv.as_dictionary()->find(key);
}

static const PrimSpec* MustPrim(const Layer* layer, const char* path) {
  NEXT_CHECK(layer);
  const PrimSpec* prim = layer->prim_at_path(path);
  NEXT_CHECK(prim && "expected prim missing");
  return prim;
}

static const Value* MustProp(const PrimSpec* prim, const char* name) {
  NEXT_CHECK(prim);
  const Value* value = prim->property_value(name);
  NEXT_CHECK(value && "expected property missing");
  return value;
}

static void CheckFloatArray(const Value* value, TypeId type,
                            const std::vector<float>& expected) {
  NEXT_CHECK(value && value->is_array() && value->type_id() == type);
  const std::vector<float>* arr = value->as_float_array();
  NEXT_CHECK(arr && *arr == expected);
}

static void CheckIntArray(const Value* value,
                          const std::vector<int32_t>& expected) {
  NEXT_CHECK(value && value->is_array() && value->type_id() == TypeId::Int);
  const std::vector<int32_t>* arr = value->as_int_array();
  NEXT_CHECK(arr && *arr == expected);
}

static void CheckInt64Array(const Value* value,
                            const std::vector<int64_t>& expected) {
  NEXT_CHECK(value && value->is_array() && value->type_id() == TypeId::Int64);
  const std::vector<int64_t>* arr = value->as_int64_array();
  NEXT_CHECK(arr && *arr == expected);
}

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
    NEXT_CHECK(prim);
    prim->meta().doc() = "A helper scope";
    prim->meta().hidden = true;
  }
  layer.end_prim();

  layer.finalize();

  Stage stage = stage_builder.Build();

  // Write to USDC via CrateWriter (low-level)
  std::vector<uint8_t> buffer;
  CrateWriter writer;
  CrateWriteResult result = writer.WriteToMemory(buffer, stage);

  NEXT_CHECK(result.success);
  std::cout << "  Written " << result.bytes_written << " bytes\n";
  std::cout << "  Tokens: " << result.token_count << "\n";
  std::cout << "  Paths: " << result.path_count << "\n";
  std::cout << "  Specs: " << result.spec_count << "\n";
  std::cout << "  Fields: " << result.field_count << "\n";

  // Verify basic structure
  NEXT_CHECK(buffer.size() >= kCrateBootstrapSize);
  NEXT_CHECK(std::memcmp(buffer.data(), kCrateMagic, 8) == 0);

  // Parse TOC
  TocInfo toc;
  // NOTE: ParseUSDCBinary() populates `toc` as a side effect, so it must run
  // outside NEXT_CHECK() — under NDEBUG `NEXT_CHECK(expr)` does not evaluate `expr`,
  // which would leave `toc` empty and null-deref the section lookups below.
  const bool toc_parsed = ParseUSDCBinary(buffer.data(), buffer.size(), toc);
  NEXT_CHECK(toc_parsed);
  (void)toc_parsed;

  // Verify required sections exist
  auto* tokens_sec = toc.find("TOKENS");
  auto* strings_sec = toc.find("STRINGS");
  auto* fields_sec = toc.find("FIELDS");
  auto* fieldsets_sec = toc.find("FIELDSETS");
  auto* paths_sec = toc.find("PATHS");
  auto* specs_sec = toc.find("SPECS");

  NEXT_CHECK(tokens_sec != nullptr);
  NEXT_CHECK(strings_sec != nullptr);
  NEXT_CHECK(fields_sec != nullptr);
  NEXT_CHECK(fieldsets_sec != nullptr);
  NEXT_CHECK(paths_sec != nullptr);
  NEXT_CHECK(specs_sec != nullptr);

  // VALUE section is not in TOC (pxrUSD stores it between bootstrap
  // and structural sections, not in the TOC). Verify it exists between
  // bootstrap end (64) and first structural section.
  if (toc.sections.size() > 0) {
    NEXT_CHECK(toc.sections[0].start > 64);
  }

  // Verify section order
  NEXT_CHECK(std::strncmp(toc.sections[0].name, "TOKENS", 6) == 0);
  NEXT_CHECK(std::strncmp(toc.sections[1].name, "STRINGS", 7) == 0);
  NEXT_CHECK(std::strncmp(toc.sections[2].name, "FIELDS", 6) == 0);
  NEXT_CHECK(std::strncmp(toc.sections[3].name, "FIELDSETS", 9) == 0);
  NEXT_CHECK(std::strncmp(toc.sections[4].name, "PATHS", 5) == 0);
  NEXT_CHECK(std::strncmp(toc.sections[5].name, "SPECS", 5) == 0);

  // Verify sections are non-empty
  NEXT_CHECK(tokens_sec->size > 8);
  NEXT_CHECK(paths_sec->size >= 8);
  NEXT_CHECK(specs_sec->size >= 8);
  NEXT_CHECK(fields_sec->size >= 8);
  NEXT_CHECK(fieldsets_sec->size >= 8);

  // Verify no overlap between sections
  for (size_t i = 0; i < toc.sections.size(); i++) {
    for (size_t j = i + 1; j < toc.sections.size(); j++) {
      int64_t a_start = toc.sections[i].start;
      int64_t a_end = a_start + toc.sections[i].size;
      int64_t b_start = toc.sections[j].start;
      int64_t b_end = b_start + toc.sections[j].size;
      NEXT_CHECK(!(a_start < b_end && b_start < a_end));
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
      NEXT_CHECK(dr.success);
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

    NEXT_CHECK(token_list.size() == num_tokens);

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
        NEXT_CHECK(false);
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
  CrateWriter file_writer;
  CrateWriteResult file_result = file_writer.WriteToFile(test_file, stage);
  NEXT_CHECK(file_result.success);
  std::cout << "  Wrote to " << test_file << " ("
            << file_result.bytes_written << " bytes)\n";

  // Verify file matches memory
  std::ifstream ifs(test_file, std::ios::binary | std::ios::ate);
  NEXT_CHECK(ifs.is_open());
  size_t file_size = static_cast<size_t>(ifs.tellg());
  ifs.close();
  if (file_size != buffer.size()) {
    std::cerr << "  file_size=" << file_size << " buffer_size=" << buffer.size()
              << "\n";
    std::cerr.flush();
    NEXT_CHECK(file_size == buffer.size());
  }

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
  NEXT_CHECK(result.success);

  // Parse TOC and verify
  TocInfo toc;
  // NOTE: ParseUSDCBinary() populates `toc` as a side effect, so it must run
  // outside NEXT_CHECK() — under NDEBUG `NEXT_CHECK(expr)` does not evaluate `expr`,
  // which would leave `toc` empty and null-deref the section lookups below.
  const bool toc_parsed = ParseUSDCBinary(buffer.data(), buffer.size(), toc);
  NEXT_CHECK(toc_parsed);
  (void)toc_parsed;

  auto* tokens_sec = toc.find("TOKENS");
  NEXT_CHECK(tokens_sec != nullptr);

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
  NEXT_CHECK(result.success);

  std::cout << "  Written " << result.bytes_written << " bytes\n";
  std::cout << "  Tokens: " << result.token_count
            << "  Paths: " << result.path_count
            << "  Specs: " << result.spec_count
            << "  Fields: " << result.field_count << "\n";

  // Verify basic structure
  NEXT_CHECK(std::memcmp(buffer.data(), kCrateMagic, 8) == 0);

  TocInfo toc;
  // NOTE: ParseUSDCBinary() populates `toc` as a side effect, so it must run
  // outside NEXT_CHECK() — under NDEBUG `NEXT_CHECK(expr)` does not evaluate `expr`,
  // which would leave `toc` empty and null-deref the section lookups below.
  const bool toc_parsed = ParseUSDCBinary(buffer.data(), buffer.size(), toc);
  NEXT_CHECK(toc_parsed);
  (void)toc_parsed;

  auto* ts = toc.find("TOKENS");
  NEXT_CHECK(ts != nullptr);

  // Read back and verify the time samples decode per-property (Phase: crate
  // TimeSamples decoding — previously skipped on read).
  CrateReader reader;
  CrateReadResult rr = reader.Read(buffer.data(), buffer.size());
  NEXT_CHECK(rr.success && "re-read of time-sampled layer failed");
  const PrimSpec* anim = rr.stage.GetRootLayer()->prim_at_path("/Animated");
  NEXT_CHECK(anim);

  PropNameId tr = GetPropNameTable().find("xformOp:translate");
  NEXT_CHECK(tr.is_valid() && anim->has_time_samples(tr));
  const auto* tr_samples = anim->time_samples(tr);
  NEXT_CHECK(tr_samples && tr_samples->size() == 3 && "translate: 3 samples expected");
  // Sample at t=50 should be (10,5,0).
  bool found_50 = false;
  for (const auto& kv : *tr_samples) {
    if (std::abs(kv.first - 50.0) < 1e-9) {
      Value vs;
      const Value* v = anim->time_sample_value(kv.second, &vs);
      NEXT_CHECK(v && v->is_array() == false);
      const float* f3 = v->as_float3();
      NEXT_CHECK(f3 && f3[0] == 10.0f && f3[1] == 5.0f && f3[2] == 0.0f);
      found_50 = true;
    }
  }
  NEXT_CHECK(found_50 && "translate sample at t=50 missing/wrong");

  PropNameId wt = GetPropNameTable().find("weight");
  NEXT_CHECK(wt.is_valid() && anim->has_time_samples(wt));
  const auto* wt_samples = anim->time_samples(wt);
  NEXT_CHECK(wt_samples && wt_samples->size() == 2 && "weight: 2 samples expected");

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
  NEXT_CHECK(file_result.success);
  std::cout << "  WriteUSDCToFile: " << file_result.bytes_written << " bytes\n";

  // Test WriteLayerToUSDCMemory
  const Layer* root_layer = stage.GetRootLayer();
  NEXT_CHECK(root_layer != nullptr);
  std::vector<uint8_t> mem_buffer;
  USDCWriteResult mem_result = WriteLayerToUSDCMemory(mem_buffer, *root_layer);
  NEXT_CHECK(mem_result.success);
  std::cout << "  WriteLayerToUSDCMemory: " << mem_result.bytes_written << " bytes\n";

  NEXT_CHECK(std::memcmp(mem_buffer.data(), kCrateMagic, 8) == 0);

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
  // Plain double[] regression: EncodeValue's scalar Double branch used to
  // hijack arrays (no is_array guard) and emit 8 bytes of the ArrayBox header
  // as a scalar double (garbage, nondeterministic).
  std::vector<double> dd = {0.5600000023841858, -1.25, 42.0, 1e-9};
  b.add_property("v4", Value::MakeFloatCompArray(std::vector<float>(v4), TypeId::Float4, 4));
  b.add_property("qf", Value::MakeFloatCompArray(std::vector<float>(qf), TypeId::Quatf, 4));
  b.add_property("m4", Value::MakeDoubleCompArray(std::vector<double>(m4), TypeId::Matrix4d, 16));
  b.add_property("m4f", Value::MakeFloatCompArray(std::vector<float>(m4f), TypeId::Matrix4f, 16));
  b.add_property("d3", Value::MakeDoubleCompArray(std::vector<double>(d3), TypeId::Double3, 3));
  b.add_property("dd", Value::MakeDoubleArray(std::vector<double>(dd)));
  b.end_prim();
  b.finalize();

  CrateWriter writer;
  std::vector<uint8_t> buf;
  CrateWriteResult wr = writer.WriteLayerToMemory(buf, layer);
  NEXT_CHECK(wr.success);

  {
    CrateReadOptions limited;
    limited.max_memory = 1;
    CrateReader limited_reader(limited);
    CrateReadResult limited_result = limited_reader.Read(buf.data(), buf.size());
    NEXT_CHECK(!limited_result.success && !limited_result.errors.empty() &&
           "max_memory must reject oversized in-memory crate input");
  }

  CrateReader reader;
  CrateReadResult rr = reader.Read(buf.data(), buf.size());
  NEXT_CHECK(rr.success && "re-read of vec/matrix arrays failed");
  const Layer* rl = rr.stage.GetRootLayer();
  const PrimSpec* geo = rl->prim_at_path("/Geo");
  NEXT_CHECK(geo);

  auto check_f = [&](const char* name, const std::vector<float>& expect, TypeId t) {
    const Value* val = geo->property_value(name);
    NEXT_CHECK(val && val->is_array() && val->type_id() == t);
    const std::vector<float>* arr = val->as_float_array();
    NEXT_CHECK(arr && *arr == expect);
  };
  auto check_d = [&](const char* name, const std::vector<double>& expect, TypeId t) {
    const Value* val = geo->property_value(name);
    NEXT_CHECK(val && val->is_array() && val->type_id() == t);
    const std::vector<double>* arr = val->as_double_array();
    NEXT_CHECK(arr && *arr == expect);
  };
  check_f("v4", v4, TypeId::Float4);
  check_f("qf", qf, TypeId::Quatf);
  check_d("m4", m4, TypeId::Matrix4d);
  check_d("m4f", m4f_as_d, TypeId::Matrix4d);
  check_d("d3", d3, TypeId::Double3);
  check_d("dd", dd, TypeId::Double);

  std::cout << "  vec/matrix/quat array roundtrip passed!\n\n";
}

void test_high_level_memory_caps() {
  std::cout << "Testing high-level memory caps...\n";

  Layer layer;
  LayerBuilder b(layer);
  b.begin_prim("P", "Xform");
  b.add_property("v", Value(1.0));
  b.end_prim();
  b.finalize();

  CrateWriter writer;
  std::vector<uint8_t> buf;
  CrateWriteResult wr = writer.WriteLayerToMemory(buf, layer);
  NEXT_CHECK(wr.success);

  const char* usdc_file = "/tmp/next_memcap_highlevel.usdc";
  {
    std::ofstream f(usdc_file, std::ios::binary);
    f.write(reinterpret_cast<const char*>(buf.data()),
            static_cast<std::streamsize>(buf.size()));
  }

  LoadUSDOptions load_opts;
  load_opts.max_memory = 1;
  Stage limited_stage;
  std::string warn, err;
  bool ok = LoadUSD(usdc_file, &limited_stage, load_opts, &warn, &err);
  NEXT_CHECK(!ok && "LoadUSD max_memory must reject oversized USDC input");

  USDCLoadOptions usdc_opts;
  usdc_opts.crate_options.max_memory = 1;
  warn.clear();
  err.clear();
  ok = LoadUSDC(usdc_file, &limited_stage, usdc_opts, &warn, &err);
  NEXT_CHECK(!ok && "LoadUSDC options must reject oversized USDC input");

  const char* usda_file = "/tmp/next_memcap_highlevel.usda";
  {
    std::ofstream f(usda_file, std::ios::binary);
    f << "#usda 1.0\n\ndef Xform \"World\" {}\n";
  }
  warn.clear();
  err.clear();
  ok = LoadUSD(usda_file, &limited_stage, load_opts, &warn, &err);
  NEXT_CHECK(!ok && "LoadUSD max_memory must reject oversized USDA input");

  LoadOptions usda_opts;
  usda_opts.parse_options.max_file_size = 1;
  warn.clear();
  err.clear();
  ok = LoadUSDA(usda_file, &limited_stage, usda_opts, &warn, &err);
  NEXT_CHECK(!ok && "LoadUSDA options must reject oversized USDA input");

  const char* asset_file = "/tmp/next_memcap_ext_asset.usda";
  const char* root_file = "/tmp/next_memcap_ext_root.usda";
  std::string root_text =
      "#usda 1.0\n(\n  subLayers = [@./next_memcap_ext_asset.usda@]\n)\n"
      "def Xform \"World\" {}\n";
  std::string asset_text =
      "#usda 1.0\n\ndef Xform \"Asset\" {\n  string note = \"";
  asset_text.append(256, 'x');
  asset_text += "\"\n}\n";
  {
    std::ofstream f(root_file, std::ios::binary);
    f << root_text;
  }
  {
    std::ofstream f(asset_file, std::ios::binary);
    f << asset_text;
  }

  LoadUSDOptions composed_opts;
  composed_opts.max_memory = root_text.size() + 8;
  NEXT_CHECK(root_text.size() <= composed_opts.max_memory);
  NEXT_CHECK(asset_text.size() > composed_opts.max_memory);
  warn.clear();
  err.clear();
  pcp::CompositionOptions comp_opts;
  comp_opts.error_when_asset_not_found = true;
  ok = LoadUSDComposed(root_file, &limited_stage, composed_opts, &warn, &err,
                       &comp_opts);
  NEXT_CHECK(!ok && "LoadUSDComposed must cap external composition layers");

  std::remove(usdc_file);
  std::remove(usda_file);
  std::remove(asset_file);
  std::remove(root_file);
  std::cout << "  high-level memory caps passed!\n\n";
}

// HalfToFloat/FloatToHalf: every finite half bit pattern must survive
// half -> float -> half byte-exact (NaN payloads excluded).
void test_half_conversion() {
  std::cout << "Testing half<->float conversion...\n";
  size_t checked = 0;
  for (uint32_t bits = 0; bits < 0x10000u; ++bits) {
    uint16_t h = static_cast<uint16_t>(bits);
    uint32_t exp = (h >> 10) & 0x1Fu;
    uint32_t mant = h & 0x3FFu;
    if (exp == 0x1Fu && mant != 0) continue;  // skip NaN (payload not preserved)
    float f = HalfToFloat(h);
    uint16_t h2 = FloatToHalf(f);
    NEXT_CHECK(h2 == h && "half->float->half not exact");
    ++checked;
  }
  NEXT_CHECK(HalfToFloat(0x3C00) == 1.0f && "half 1.0");
  NEXT_CHECK(HalfToFloat(0xC000) == -2.0f && "half -2.0");
  std::cout << "  " << checked << " half patterns round-tripped exactly\n\n";
}

// Half / Vec3h / Quath arrays must survive write -> read (values chosen exactly
// representable in half so the comparison is exact).
void test_roundtrip_half_arrays() {
  std::cout << "Testing roundtrip half arrays...\n";

  Layer layer;
  LayerBuilder b(layer);
  b.begin_prim("Geo", "Mesh");
  std::vector<float> h1 = {1.0f, -2.0f, 0.5f, 0.25f};            // 4 x Half
  std::vector<float> h3 = {1.0f, 2.0f, 3.0f, -1.0f, 0.5f, 4.0f}; // 2 x Half3
  std::vector<float> qh = {0.0f, 0.0f, 0.0f, 1.0f};             // 1 x Quath
  b.add_property("h1", Value::MakeFloatCompArray(std::vector<float>(h1), TypeId::Half, 1));
  b.add_property("h3", Value::MakeFloatCompArray(std::vector<float>(h3), TypeId::Half3, 3));
  b.add_property("qh", Value::MakeFloatCompArray(std::vector<float>(qh), TypeId::Quath, 4));
  b.end_prim();
  b.finalize();

  CrateWriter writer;
  std::vector<uint8_t> buf;
  CrateWriteResult wr = writer.WriteLayerToMemory(buf, layer);
  NEXT_CHECK(wr.success);

  CrateReader reader;
  CrateReadResult rr = reader.Read(buf.data(), buf.size());
  NEXT_CHECK(rr.success && "re-read of half arrays failed");
  const PrimSpec* geo = rr.stage.GetRootLayer()->prim_at_path("/Geo");
  NEXT_CHECK(geo);

  auto check = [&](const char* name, const std::vector<float>& expect, TypeId t) {
    const Value* v = geo->property_value(name);
    NEXT_CHECK(v && v->is_array() && v->type_id() == t);
    const std::vector<float>* arr = v->as_float_array();  // materializes half->float
    NEXT_CHECK(arr && *arr == expect);
  };
  check("h1", h1, TypeId::Half);
  check("h3", h3, TypeId::Half3);
  check("qh", qh, TypeId::Quath);

  std::cout << "  half array roundtrip passed!\n\n";
}

// Phase 7 S5: arc list-op qualifiers survive a USDC write -> read cycle via the
// companion `<arc>_listOp` token[] fields. The within-spec effective list (the
// `references` token[]) and the ArcEdit (prepend/append/delete) both round-trip.
void test_roundtrip_arc_listops() {
  std::cout << "Testing arc list-op crate roundtrip...\n";
  Layer layer;
  LayerBuilder b(layer);
  b.begin_prim("P", "Scope");
  // Within-spec effective list (= prepend B), plus the raw edit: prepend </B>,
  // delete </A>.
  b.current()->meta().references.push_back("</B>");
  {
    ArcEdit& e = b.current()->meta().ensure_arc_edits().references;
    e.authored = true;
    e.is_explicit = false;
    e.prepended.push_back("</B>");
    e.deleted.push_back("</A>");
  }
  // A bare (explicit) inherits list must NOT gain a companion field.
  b.current()->meta().inherits.push_back("</_class_X>");
  b.end_prim();
  b.finalize();

  CrateWriter writer;
  std::vector<uint8_t> buf;
  CrateWriteResult wr = writer.WriteLayerToMemory(buf, layer);
  NEXT_CHECK(wr.success);

  CrateReader reader;
  CrateReadResult rr = reader.Read(buf.data(), buf.size());
  NEXT_CHECK(rr.success && "re-read of arc list-ops failed");
  const PrimSpec* p = rr.stage.GetRootLayer()->prim_at_path("/P");
  NEXT_CHECK(p);

  // The within-spec effective list round-trips.
  NEXT_CHECK(p->meta().references.size() == 1 && p->meta().references[0] == "</B>");
  // The non-explicit edit round-trips.
  const ArcListOpEdits* ed = p->meta().arc_edits();
  NEXT_CHECK(ed && "references edit must survive the crate roundtrip");
  NEXT_CHECK(ed->references.authored);
  NEXT_CHECK(!ed->references.is_explicit);
  NEXT_CHECK(ed->references.prepended.size() == 1 &&
         ed->references.prepended[0] == "</B>");
  NEXT_CHECK(ed->references.deleted.size() == 1 &&
         ed->references.deleted[0] == "</A>");
  // Bare inherits: no companion field, so its edit stays explicit (default).
  NEXT_CHECK(!ed->inherits.authored && ed->inherits.is_explicit &&
         ed->inherits.prepended.empty());
  std::cout << "  arc list-op crate roundtrip passed!\n\n";
}

void test_roundtrip_arc_metadata_dicts() {
  std::cout << "Testing arc metadata dictionary crate roundtrip...\n";

  Layer layer;
  LayerBuilder b(layer);
  b.begin_prim("AssetRef", "Xform");
  b.current()->meta().references.push_back("@asset.usda@</Model>");
  b.current()->meta().payloads.push_back("@payload.usda@</Payload>");
  {
    Dict cd;
    cd.set("source", Value("reference-fixture"));
    cd.set("enabled", Value(true));
    cd.set("revision", Value(int32_t(7)));
    b.current()->meta().customData() = Value::MakeDictionary(std::move(cd));
  }
  {
    Dict ai;
    ai.set("identifier", Value::MakeAssetPath("asset.usda"));
    ai.set("kind", Value::MakeToken("component"));
    b.current()->meta().assetInfo() = Value::MakeDictionary(std::move(ai));
  }
  b.end_prim();
  b.finalize();

  CrateWriter writer;
  std::vector<uint8_t> buf;
  CrateWriteResult wr = writer.WriteLayerToMemory(buf, layer);
  NEXT_CHECK(wr.success);

  CrateReader reader;
  CrateReadResult rr = reader.Read(buf.data(), buf.size());
  NEXT_CHECK(rr.success && "re-read of arc metadata dictionaries failed");
  const PrimSpec* p = rr.stage.GetRootLayer()->prim_at_path("/AssetRef");
  NEXT_CHECK(p);

  NEXT_CHECK(p->meta().references.size() == 1);
  if (p->meta().references[0] != "@asset.usda@</Model>") {
    std::cout << "  ref=" << p->meta().references[0] << "\n";
    NEXT_CHECK(p->meta().references[0] == "@asset.usda@</Model>");
  }
  NEXT_CHECK(p->meta().payloads.size() == 1);
  if (p->meta().payloads[0] != "@payload.usda@</Payload>") {
    std::cerr << "  payload=" << p->meta().payloads[0] << "\n";
    std::cerr.flush();
    NEXT_CHECK(p->meta().payloads[0] == "@payload.usda@</Payload>");
  }

  const Value& cd = p->meta().customData();
  NEXT_CHECK(cd.is_dictionary());
  const Value* source = DictFind(cd, "source");
  NEXT_CHECK(source && source->as_string() && *source->as_string() == "reference-fixture");
  const Value* enabled = DictFind(cd, "enabled");
  NEXT_CHECK(enabled && enabled->as_bool() && *enabled->as_bool());
  const Value* revision = DictFind(cd, "revision");
  NEXT_CHECK(revision && revision->as_int() && *revision->as_int() == 7);

  const Value& ai = p->meta().assetInfo();
  NEXT_CHECK(ai.is_dictionary());
  const Value* identifier = DictFind(ai, "identifier");
  NEXT_CHECK(identifier && identifier->as_asset_path() &&
         *identifier->as_asset_path() == "asset.usda");
  const Value* kind = DictFind(ai, "kind");
  NEXT_CHECK(kind && kind->as_token() && *kind->as_token() == "component");

  std::cout << "  arc metadata dictionary crate roundtrip passed!\n\n";
}

void test_roundtrip_custom_qualifier() {
  std::cout << "Testing custom-qualifier crate roundtrip...\n";
  Layer layer;
  LayerBuilder b(layer);
  b.begin_prim("P", "Xform");
  b.add_property("isAsset", Value(true), PropSlot::kFlagCustom);
  b.add_property("plain", Value(1.0));  // non-custom control
  b.end_prim();
  b.finalize();

  CrateWriter writer;
  std::vector<uint8_t> buf;
  CrateWriteResult wr = writer.WriteLayerToMemory(buf, layer);
  NEXT_CHECK(wr.success);

  CrateReader reader;
  CrateReadResult rr = reader.Read(buf.data(), buf.size());
  NEXT_CHECK(rr.success && "re-read of custom-qualified attr failed");
  const PrimSpec* p = rr.stage.GetRootLayer()->prim_at_path("/P");
  NEXT_CHECK(p);

  const PropSlot* a = p->property("isAsset");
  NEXT_CHECK(a && a->is_custom() && "custom flag lost across the crate roundtrip");
  const PropSlot* pl = p->property("plain");
  NEXT_CHECK(pl && !pl->is_custom() && "non-custom attr gained a custom flag");
  std::cout << "  custom-qualifier crate roundtrip passed!\n\n";
}

void test_roundtrip_api_schemas() {
  std::cout << "Testing apiSchemas crate roundtrip...\n";
  Layer layer;
  LayerBuilder b(layer);
  b.begin_prim("P", "Mesh");
  b.current()->meta().apiSchemas().emplace_back("MaterialBindingAPI");
  b.current()->meta().apiSchemas().emplace_back("PhysicsCollisionAPI");
  b.end_prim();
  b.finalize();

  CrateWriter writer;
  std::vector<uint8_t> buf;
  CrateWriteResult wr = writer.WriteLayerToMemory(buf, layer);
  NEXT_CHECK(wr.success);

  CrateReader reader;
  CrateReadResult rr = reader.Read(buf.data(), buf.size());
  NEXT_CHECK(rr.success && "re-read of apiSchemas failed");
  const PrimSpec* p = rr.stage.GetRootLayer()->prim_at_path("/P");
  NEXT_CHECK(p);

  // apiSchemas must land in PrimSpecMeta (pxr writes it in the metadata block),
  // NOT leak back as a phantom `token[] apiSchemas` body property.
  NEXT_CHECK(p->meta().apiSchemas().size() == 2 &&
         "apiSchemas did not round-trip into PrimSpecMeta");
  NEXT_CHECK(p->meta().apiSchemas()[0].str() == "MaterialBindingAPI");
  NEXT_CHECK(p->meta().apiSchemas()[1].str() == "PhysicsCollisionAPI");
  NEXT_CHECK(p->property("apiSchemas") == nullptr &&
         "apiSchemas leaked as a phantom body property");
  std::cout << "  apiSchemas crate roundtrip passed!\n\n";
}

void test_comprehensive_usdc_fixture() {
  std::cout << "Testing comprehensive USDC fixture roundtrip...\n";

  Layer layer;
  layer.meta().defaultPrim = "World";
  layer.meta().upAxis = "Z";
  layer.meta().metersPerUnit = 1.0;
  layer.meta().timeCodesPerSecond = 24.0;
  layer.meta().framesPerSecond = 24.0;
  layer.meta().framesPerSecond_set = true;
  layer.meta().startTimeCode = 1.0;
  layer.meta().endTimeCode = 48.0;
  layer.meta().doc = "Dense generated USDC fixture";
  {
    Dict cld;
    cld.set("fixture", Value("comprehensive-usdc"));
    cld.set("revision", Value(int32_t(3)));
    cld.set("enabled", Value(true));
    layer.meta().customLayerData = Value::MakeDictionary(std::move(cld));
  }

  LayerBuilder b(layer);
  b.begin_prim("World", "Xform");
  {
    PrimSpec* world = b.current();
    NEXT_CHECK(world);
    world->meta().doc() = "fixture root";
    world->meta().apiSchemas().emplace_back("MaterialBindingAPI");
    world->meta().references.push_back("@asset.usda@</Asset>");
    world->meta().payloads.push_back("@payload.usda@</Payload>");
    world->meta().inherits.push_back("</_class_Model>");
    world->meta().specializes.push_back("</_class_ModelSpecialization>");
    Dict cd;
    cd.set("role", Value::MakeToken("root"));
    cd.set("lod", Value(int32_t(1)));
    world->meta().customData() = Value::MakeDictionary(std::move(cd));
    Dict ai;
    ai.set("identifier", Value::MakeAssetPath("asset.usda"));
    ai.set("kind", Value::MakeToken("component"));
    world->meta().assetInfo() = Value::MakeDictionary(std::move(ai));
  }
  b.add_time_sample("xformOp:translate", 1.0,
                    Value::MakeFloat3(0.0f, 0.0f, 0.0f));
  b.add_time_sample("xformOp:translate", 24.0,
                    Value::MakeFloat3(1.0f, 2.0f, 3.0f));
  b.end_prim();

  const std::vector<float> points = {
      0.0f, 0.0f, 0.0f,
      1.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 1.0f};
  const std::vector<int32_t> fvc = {3, 3};
  const std::vector<int32_t> fvi = {0, 1, 2, 0, 2, 3};
  const std::vector<float> display_color = {
      1.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 1.0f,
      1.0f, 1.0f, 1.0f};
  b.begin_prim("Mesh", "Mesh");
  b.add_property("points", Value::MakeFloat3Array(points));
  b.add_property("faceVertexCounts", Value::MakeIntArray(fvc));
  b.add_property("faceVertexIndices", Value::MakeIntArray(fvi));
  b.add_property("primvars:displayColor",
                 Value::MakeFloat3Array(display_color));
  b.add_relationship("material:binding", Path("/Material"));
  {
    PropMeta& pm = b.current()->ensure_property_meta("primvars:displayColor");
    pm.interpolation = "vertex";
    pm.authored |= PropMeta::kInterpolation;
    pm.colorSpace = "sRGB";
    pm.authored |= PropMeta::kColorSpace;
    pm.elementSize = 1;
    pm.authored |= PropMeta::kElementSize;
    Dict pcd;
    pcd.set("role", Value::MakeToken("albedo"));
    pm.customData = Value::MakeDictionary(std::move(pcd));
    pm.authored |= PropMeta::kCustomData;
  }
  b.end_prim();

  b.begin_prim("Material", "Material");
  {
    PropNameId surface_id = GetPropNameTable().intern("outputs:surface");
    b.current()->add_property_slot(surface_id, TypeId::Token,
                                   PropSlot::kFlagConnection);
    b.current()->set_property_type_name("outputs:surface", "token");
  }
  b.current()->add_connection("outputs:surface",
                              Path("/Shader.outputs:surface"));
  b.end_prim();

  b.begin_prim("Shader", "Shader");
  b.add_property("info:id", Value::MakeToken("UsdPreviewSurface"));
  b.add_property("inputs:diffuseColor",
                 Value::MakeFloat3(0.25f, 0.5f, 0.75f),
                 PropSlot::kFlagConnection);
  b.current()->set_property_type_name("inputs:diffuseColor", "color3f");
  b.current()->add_connection("inputs:diffuseColor",
                              Path("/Texture.outputs:rgb"));
  b.end_prim();

  b.begin_prim("Texture", "Shader");
  b.add_property("info:id", Value::MakeToken("UsdUVTexture"));
  b.add_property("inputs:file", Value::MakeAssetPath("albedo.png"));
  b.end_prim();

  const std::vector<int32_t> proto_indices = {0, 0, 0};
  const std::vector<float> positions = {
      0.0f, 0.0f, 0.0f,
      1.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f};
  const std::vector<float> positions_t2 = {
      0.0f, 0.0f, 0.0f,
      2.0f, 0.0f, 0.0f,
      0.0f, 2.0f, 0.0f};
  const std::vector<float> orientations = {
      1.0f, 0.0f, 0.0f, 0.0f,
      1.0f, 0.0f, 0.0f, 0.0f,
      1.0f, 0.0f, 0.0f, 0.0f};
  const std::vector<float> scales = {
      1.0f, 1.0f, 1.0f,
      2.0f, 2.0f, 2.0f,
      0.5f, 0.5f, 0.5f};
  const std::vector<int64_t> ids = {10, 11, 12};
  const std::vector<int64_t> invisible_ids = {11};
  b.begin_prim("Instancer", "PointInstancer");
  b.add_relationship("prototypes", Path("/Mesh"));
  b.add_property("protoIndices", Value::MakeIntArray(proto_indices));
  b.add_property("positions", Value::MakeFloat3Array(positions));
  b.add_property("orientations",
                 Value::MakeFloatCompArray(std::vector<float>(orientations),
                                           TypeId::Quatf, 4));
  b.add_property("scales", Value::MakeFloat3Array(scales));
  b.add_property("ids", Value::MakeInt64Array(ids));
  b.add_property("invisibleIds", Value::MakeInt64Array(invisible_ids));
  b.add_time_sample("positions", 1.0, Value::MakeFloat3Array(positions));
  b.add_time_sample("positions", 2.0, Value::MakeFloat3Array(positions_t2));
  b.end_prim();

  b.finalize();

  CrateWriter writer;
  std::vector<uint8_t> buf;
  CrateWriteResult wr = writer.WriteLayerToMemory(buf, layer);
  NEXT_CHECK(wr.success && "failed to write comprehensive fixture");

  CrateReader reader;
  CrateReadResult rr = reader.Read(buf.data(), buf.size());
  NEXT_CHECK(rr.success && "failed to read comprehensive fixture");
  const Layer* rl = rr.stage.GetRootLayer();
  NEXT_CHECK(rl);

  NEXT_CHECK(rl->meta().defaultPrim == "World");
  NEXT_CHECK(rl->meta().upAxis == "Z");
  NEXT_CHECK(rl->meta().framesPerSecond_set);
  NEXT_CHECK(rl->meta().framesPerSecond == 24.0);
  NEXT_CHECK(rl->meta().customLayerData.is_dictionary());
  const Value* fixture = DictFind(rl->meta().customLayerData, "fixture");
  NEXT_CHECK(fixture && fixture->as_string() &&
         *fixture->as_string() == "comprehensive-usdc");

  const PrimSpec* world = MustPrim(rl, "/World");
  NEXT_CHECK(world->type_name() == "Xform");
  NEXT_CHECK(world->meta().apiSchemas().size() == 1);
  NEXT_CHECK(world->meta().apiSchemas()[0].str() == "MaterialBindingAPI");
  NEXT_CHECK(world->meta().references.size() == 1);
  NEXT_CHECK(world->meta().payloads.size() == 1);
  NEXT_CHECK(world->meta().inherits.size() == 1);
  NEXT_CHECK(world->meta().specializes.size() == 1);
  NEXT_CHECK(world->meta().customData().is_dictionary());
  NEXT_CHECK(world->meta().assetInfo().is_dictionary());
  PropNameId xform_op = GetPropNameTable().find("xformOp:translate");
  NEXT_CHECK(xform_op.is_valid() && world->has_time_samples(xform_op));
  const auto* xform_samples = world->time_samples(xform_op);
  if (!xform_samples) {
    std::cerr << "xformOp:translate has no time samples\n";
    std::abort();
  }
  if (xform_samples->size() != 2) {
    std::cerr << "xformOp:translate sample count = " << xform_samples->size()
              << "\n";
    for (const auto& s : *xform_samples) {
      std::cerr << "  t=" << s.first << " value_index=" << s.second << "\n";
    }
    std::abort();
  }

  const PrimSpec* mesh = MustPrim(rl, "/Mesh");
  CheckFloatArray(MustProp(mesh, "points"), TypeId::Float3, points);
  CheckIntArray(MustProp(mesh, "faceVertexCounts"), fvc);
  CheckIntArray(MustProp(mesh, "faceVertexIndices"), fvi);
  CheckFloatArray(MustProp(mesh, "primvars:displayColor"), TypeId::Float3,
                  display_color);
  const PropMeta* color_meta = mesh->property_meta("primvars:displayColor");
  NEXT_CHECK(color_meta);
  NEXT_CHECK((color_meta->authored & PropMeta::kInterpolation) &&
         color_meta->interpolation == "vertex");
  NEXT_CHECK((color_meta->authored & PropMeta::kColorSpace) &&
         color_meta->colorSpace == "sRGB");
  NEXT_CHECK((color_meta->authored & PropMeta::kElementSize) &&
         color_meta->elementSize == 1);
  NEXT_CHECK((color_meta->authored & PropMeta::kCustomData) &&
         color_meta->customData.is_dictionary());
  const std::vector<Path>* mat_targets = mesh->relationship("material:binding");
  NEXT_CHECK(mat_targets && mat_targets->size() == 1 &&
         (*mat_targets)[0].str() == "/Material");

  const PrimSpec* material = MustPrim(rl, "/Material");
  const PropSlot* surface_slot = material->property("outputs:surface");
  NEXT_CHECK(surface_slot && surface_slot->is_connection());
  const std::vector<Path>* surface =
      material->connection("outputs:surface");
  NEXT_CHECK(surface && surface->size() == 1 &&
         (*surface)[0].str() == "/Shader.outputs:surface");

  const PrimSpec* shader = MustPrim(rl, "/Shader");
  const Value* shader_id = MustProp(shader, "info:id");
  NEXT_CHECK(shader_id->as_token() &&
         *shader_id->as_token() == "UsdPreviewSurface");
  const PropSlot* diffuse_slot = shader->property("inputs:diffuseColor");
  NEXT_CHECK(diffuse_slot && diffuse_slot->is_connection());
  const std::vector<Path>* diffuse_conn =
      shader->connection("inputs:diffuseColor");
  NEXT_CHECK(diffuse_conn && diffuse_conn->size() == 1 &&
         (*diffuse_conn)[0].str() == "/Texture.outputs:rgb");

  const PrimSpec* texture = MustPrim(rl, "/Texture");
  const Value* texture_file = MustProp(texture, "inputs:file");
  NEXT_CHECK(texture_file->as_asset_path() &&
         *texture_file->as_asset_path() == "albedo.png");

  const PrimSpec* instancer = MustPrim(rl, "/Instancer");
  NEXT_CHECK(instancer->type_name() == "PointInstancer");
  const std::vector<Path>* prototypes =
      instancer->relationship("prototypes");
  NEXT_CHECK(prototypes && prototypes->size() == 1 &&
         (*prototypes)[0].str() == "/Mesh");
  CheckIntArray(MustProp(instancer, "protoIndices"), proto_indices);
  CheckFloatArray(MustProp(instancer, "positions"), TypeId::Float3,
                  positions);
  CheckFloatArray(MustProp(instancer, "orientations"), TypeId::Quatf,
                  orientations);
  CheckFloatArray(MustProp(instancer, "scales"), TypeId::Float3, scales);
  CheckInt64Array(MustProp(instancer, "ids"), ids);
  CheckInt64Array(MustProp(instancer, "invisibleIds"), invisible_ids);
  PropNameId positions_id = GetPropNameTable().find("positions");
  NEXT_CHECK(positions_id.is_valid() && instancer->has_time_samples(positions_id));
  const auto* position_samples = instancer->time_samples(positions_id);
  NEXT_CHECK(position_samples && position_samples->size() == 2);
  bool found_t2 = false;
  for (const auto& sample : *position_samples) {
    if (std::abs(sample.first - 2.0) < 1e-9) {
      Value vscratch;
      const Value* value = instancer->time_sample_value(sample.second, &vscratch);
      CheckFloatArray(value, TypeId::Float3, positions_t2);
      found_t2 = true;
    }
  }
  NEXT_CHECK(found_t2 && "PointInstancer positions t=2 sample missing");

  std::cout << "  comprehensive USDC fixture roundtrip passed! ("
            << wr.bytes_written << " bytes)\n\n";
}

int main() {
  std::cout << "=== TinyUSDZ Next USDC Roundtrip Tests ===\n\n";

  try {
    test_half_conversion();
    test_roundtrip_arc_listops();
    test_roundtrip_arc_metadata_dicts();
    test_roundtrip_custom_qualifier();
    test_roundtrip_api_schemas();
    test_comprehensive_usdc_fixture();
    test_roundtrip_schema_types();
    test_roundtrip_layer_metadata();
    test_roundtrip_time_samples();
    test_roundtrip_vec_matrix_arrays();
    test_high_level_memory_caps();
    test_roundtrip_half_arrays();
    test_write_usdc_from_stage_api();

    std::cout << "=== All USDC roundtrip tests passed! ===\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << "\n";
    return 1;
  }
}
