// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Reader Unit Tests

#include <iostream>
#include <cassert>
#include <cstring>
#include <fstream>
#include <vector>

#include "next/crate/crate-format.hh"
#include "next/crate/crate-reader.hh"
#include "next/crate/stream-reader.hh"
#include "next/reader/usdc-reader.hh"

using namespace tinyusdz::next;

// ============================================================
// Crate format tests
// ============================================================

void test_value_rep() {
  std::cout << "Testing ValueRep..." << std::endl;

  // Test basic ValueRep construction
  ValueRep rep1(0);
  assert(rep1.raw() == 0);
  assert(!rep1.is_array());
  assert(!rep1.is_inlined());
  assert(!rep1.is_compressed());
  assert(rep1.type_id() == CrateTypeId::Invalid);
  assert(rep1.payload() == 0);

  // Test with flags set
  ValueRep rep2 = ValueRep::Make(CrateTypeId::Float, 12345, false, true, false);
  assert(rep2.type_id() == CrateTypeId::Float);
  assert(rep2.payload() == 12345);
  assert(rep2.is_inlined());
  assert(!rep2.is_array());
  assert(!rep2.is_compressed());

  // Test array flag
  ValueRep rep3 = ValueRep::Make(CrateTypeId::Vec3f, 1000, true, false, false);
  assert(rep3.is_array());
  assert(!rep3.is_inlined());
  assert(rep3.type_id() == CrateTypeId::Vec3f);

  std::cout << "  ValueRep tests passed!" << std::endl;
}

void test_stream_reader() {
  std::cout << "Testing StreamReader..." << std::endl;

  // Create test data
  std::vector<uint8_t> data = {
    0x01, 0x02, 0x03, 0x04,  // 4 bytes
    0x05, 0x06, 0x07, 0x08,  // 4 bytes
    'H', 'e', 'l', 'l', 'o', '\0',  // null-terminated string
    0x00, 0x00, 0x80, 0x3F,  // float 1.0
  };

  StreamReader reader(data.data(), data.size());

  // Test reading
  assert(reader.size() == data.size());
  assert(reader.position() == 0);
  assert(!reader.at_end());

  uint32_t v1;
  assert(reader.read_u32(v1));
  assert(v1 == 0x04030201);  // Little-endian

  uint32_t v2;
  assert(reader.read_u32(v2));
  assert(v2 == 0x08070605);

  std::string s;
  assert(reader.read_cstring(s));
  assert(s == "Hello");

  float f;
  assert(reader.read_f32(f));
  assert(f == 1.0f);

  assert(reader.at_end());

  // Test seeking
  assert(reader.seek(0));
  assert(reader.position() == 0);

  std::cout << "  StreamReader tests passed!" << std::endl;
}

void test_crate_type_names() {
  std::cout << "Testing CrateTypeId names..." << std::endl;

  assert(std::strcmp(CrateTypeIdName(CrateTypeId::Bool), "Bool") == 0);
  assert(std::strcmp(CrateTypeIdName(CrateTypeId::Float), "Float") == 0);
  assert(std::strcmp(CrateTypeIdName(CrateTypeId::Vec3f), "Vec3f") == 0);
  assert(std::strcmp(CrateTypeIdName(CrateTypeId::Matrix4d), "Matrix4d") == 0);
  assert(std::strcmp(CrateTypeIdName(CrateTypeId::Token), "Token") == 0);

  // Test sizes
  assert(CrateTypeIdSize(CrateTypeId::Float) == 4);
  assert(CrateTypeIdSize(CrateTypeId::Double) == 8);
  assert(CrateTypeIdSize(CrateTypeId::Vec3f) == 12);
  assert(CrateTypeIdSize(CrateTypeId::Matrix4d) == 128);

  std::cout << "  CrateTypeId tests passed!" << std::endl;
}

void test_is_usdc_data() {
  std::cout << "Testing IsUSDCData..." << std::endl;

  // Valid USDC magic
  uint8_t valid_magic[] = {'P', 'X', 'R', '-', 'U', 'S', 'D', 'C', 0, 0, 0, 0};
  assert(IsUSDCData(valid_magic, sizeof(valid_magic)));

  // Invalid magic
  uint8_t invalid_magic[] = {'P', 'X', 'R', '-', 'U', 'S', 'D', 'A', 0, 0, 0, 0};
  assert(!IsUSDCData(invalid_magic, sizeof(invalid_magic)));

  // Too short
  uint8_t short_data[] = {'P', 'X', 'R'};
  assert(!IsUSDCData(short_data, sizeof(short_data)));

  // Null data
  assert(!IsUSDCData(nullptr, 0));

  std::cout << "  IsUSDCData tests passed!" << std::endl;
}

void test_lz4_decompression() {
  std::cout << "Testing LZ4 decompression..." << std::endl;

  // Test with empty input
  DecompressResult r1 = DecompressLZ4(nullptr, 0, 0);
  assert(!r1.success);

  // Test with zero uncompressed size
  uint8_t dummy[] = {0};
  DecompressResult r2 = DecompressLZ4(dummy, 1, 0);
  assert(r2.success);
  assert(r2.data.empty());

  std::cout << "  LZ4 decompression tests passed!" << std::endl;
}

void test_crate_reader_invalid() {
  std::cout << "Testing CrateReader with invalid data..." << std::endl;

  CrateReader reader;

  // Empty data
  CrateReadResult r1 = reader.Read(nullptr, 0);
  assert(!r1.success);
  assert(!r1.errors.empty());

  // Too short
  uint8_t short_data[] = {1, 2, 3, 4};
  CrateReadResult r2 = reader.Read(short_data, sizeof(short_data));
  assert(!r2.success);

  // Invalid magic
  uint8_t invalid_magic[100] = {0};
  std::memcpy(invalid_magic, "INVALID!", 8);
  CrateReadResult r3 = reader.Read(invalid_magic, sizeof(invalid_magic));
  assert(!r3.success);

  std::cout << "  CrateReader invalid data tests passed!" << std::endl;
}

// ============================================================
// Integration test with real USDC file (if available)
// ============================================================

void test_read_usdc_file() {
  std::cout << "Testing USDC file reading..." << std::endl;

  const char* required_files[] = {
    "../../../tests/usdc/simple.usdc",
    "../../tests/usdc/simple.usdc",
    "../tests/usdc/simple.usdc",
    "./tests/usdc/simple.usdc",
  };

  const char* required_file = nullptr;
  for (const char* path : required_files) {
    std::ifstream f(path);
    if (f.good()) {
      required_file = path;
      break;
    }
  }

  if (required_file) {
    USDCLoadResult result = LoadUSDCFromFile(required_file);
    if (!result.success) {
      std::cout << "  Required fixture parse failed: " << required_file << std::endl;
      if (!result.errors.empty()) {
        std::cout << "  Error: " << result.errors[0].message << std::endl;
      }
    }
    assert(result.success);
    assert(!result.stage.GetRootPrims().empty());
  } else {
    std::cout << "  Skipping required fixture assertion (tests/usdc/simple.usdc not found from cwd)" << std::endl;
  }

  // Try to find an optional larger model for diagnostic coverage.
  const char* test_files[] = {
    "../../../models/suzanne.usdc",
    "../../models/suzanne.usdc",
    "../models/suzanne.usdc",
    "./models/suzanne.usdc",
  };

  const char* found_file = nullptr;
  for (const char* path : test_files) {
    std::ifstream f(path);
    if (f.good()) {
      found_file = path;
      break;
    }
  }

  if (!found_file) {
    std::cout << "  Skipping (no test USDC file found)" << std::endl;
    return;
  }

  std::cout << "  Found test file: " << found_file << std::endl;

  USDCLoadResult result = LoadUSDCFromFile(found_file);

  if (!result.success) {
    std::cout << "  Parse result: " << (result.success ? "success" : "failed") << std::endl;
    if (!result.errors.empty()) {
      std::cout << "  Error: " << result.errors[0].message << std::endl;
    }
  } else {
    std::cout << "  Version: " << result.version.to_string() << std::endl;
    auto roots = result.stage.GetRootPrims();
    std::cout << "  Root prims: " << roots.size() << std::endl;

    for (const auto& prim : roots) {
      std::cout << "    - " << prim.GetName() << " (" << prim.GetTypeName() << ")" << std::endl;
    }
  }

  std::cout << "  USDC file reading test completed!" << std::endl;
}

void test_openusd_compressed_value_reps_equal_raw_size() {
  std::cout << "Testing OpenUSD compressed ValueRep table edge case..." << std::endl;

  const char* paths[] = {
    "../../../tests/usdc/composition/references-001.usdc",
    "../../tests/usdc/composition/references-001.usdc",
    "../tests/usdc/composition/references-001.usdc",
    "./tests/usdc/composition/references-001.usdc",
  };

  const char* found = nullptr;
  for (const char* path : paths) {
    std::ifstream f(path, std::ios::binary);
    if (f.good()) {
      found = path;
      break;
    }
  }

  if (!found) {
    std::cout << "  Skipping (references-001.usdc fixture not found)" << std::endl;
    return;
  }

  std::ifstream ifs(found, std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(ifs)),
                    std::istreambuf_iterator<char>());
  assert(!bytes.empty());

  CrateReader reader;
  CrateReadResult result = reader.ReadOwned(std::move(bytes));
  if (!result.success) {
    std::cout << "  Error: "
              << (result.errors.empty() ? std::string("(none)")
                                        : result.errors[0].message)
              << std::endl;
  }
  assert(result.success);

  const Layer* layer = result.stage.GetRootLayer();
  assert(layer);
  const PrimSpec* sphere = layer->prim_at_path("/sphere1");
  assert(sphere);
  assert(sphere->meta().references.size() == 1);
  assert(sphere->meta().references[0].find("scene-001.usdc") != std::string::npos);

  std::cout << "  OpenUSD compressed ValueRep edge case passed!" << std::endl;
}

// ============================================================
// Regression: pxr inlines integer-valued Vec3f/Vec4f as packed int8s in the
// value rep. The reader must reconstruct them as Float3/Float4 — they were
// unpacked as Half3/Half4, which no as_float3()/as_float4() consumer accepts
// (xformOp:scale = (1,1,1) evaluated as identity). Fixture authored via pxr
// usdcat from tests/usda/inlined-intvec-001.usda.
// ============================================================

void test_inlined_int_vec_reconstruction() {
  std::cout << "Testing inlined integer-valued Vec3f/Vec4f reconstruction..." << std::endl;

  const char* candidates[] = {
    "../../../tests/usdc/inlined-intvec-001.usdc",
    "../../tests/usdc/inlined-intvec-001.usdc",
    "../tests/usdc/inlined-intvec-001.usdc",
    "./tests/usdc/inlined-intvec-001.usdc",
  };
  const char* fixture = nullptr;
  for (const char* path : candidates) {
    std::ifstream f(path);
    if (f.good()) { fixture = path; break; }
  }
  if (!fixture) {
    std::cout << "  Skipping (tests/usdc/inlined-intvec-001.usdc not found from cwd)" << std::endl;
    return;
  }

  USDCLoadResult result = LoadUSDCFromFile(fixture);
  assert(result.success);

  UsdPrim root = result.stage.GetPrimAtPath("/root");
  assert(root.IsValid());

  // Inlined (integer-valued) float3s must come back as Float3.
  const Value* rot = root.GetPropertyValue("xformOp:rotateXYZ");
  assert(rot && rot->type_id() == TypeId::Float3);
  const float* rf = rot->as_float3();
  assert(rf && rf[0] == 0.0f && rf[1] == 0.0f && rf[2] == -64.0f);

  const Value* scale = root.GetPropertyValue("xformOp:scale");
  assert(scale && scale->type_id() == TypeId::Float3);
  const float* sf = scale->as_float3();
  assert(sf && sf[0] == 1.0f && sf[1] == 1.0f && sf[2] == 1.0f);

  // Non-integer double3 takes the regular payload path.
  const Value* tr = root.GetPropertyValue("xformOp:translate");
  assert(tr);
  const double* td = tr->as_double3();
  assert(td && td[0] == 22.5 && td[1] == 19.25 && td[2] == 0.0);

  UsdPrim quad = result.stage.GetPrimAtPath("/root/quad");
  assert(quad.IsValid());

  // Inlined (integer-valued) float4 must come back as Float4.
  const Value* v4 = quad.GetPropertyValue("primvars:testInlinedVec4");
  assert(v4 && v4->type_id() == TypeId::Float4);
  const float* v4f = v4->as_float4();
  assert(v4f && v4f[0] == 1.0f && v4f[1] == -2.0f && v4f[2] == 3.0f && v4f[3] == 4.0f);

  // Non-integer float3/float4 controls (non-inlined payload path).
  const Value* c3 = quad.GetPropertyValue("primvars:testControlVec3");
  assert(c3);
  const float* c3f = c3->as_float3();
  assert(c3f && c3f[0] == 0.5f && c3f[1] == 1.5f && c3f[2] == -2.5f);

  const Value* c4 = quad.GetPropertyValue("primvars:testControlVec4");
  assert(c4);
  const float* c4f = c4->as_float4();
  assert(c4f && c4f[0] == 0.25f && c4f[1] == 1.75f && c4f[2] == -3.5f && c4f[3] == 8.125f);

  std::cout << "  Inlined integer-valued vec reconstruction passed!" << std::endl;
}

// ============================================================
// Main
// ============================================================

int main() {
  std::cout << "=== TinyUSDZ Next USDC Reader Tests ===" << std::endl;
  std::cout << std::endl;

  try {
    test_value_rep();
    test_stream_reader();
    test_crate_type_names();
    test_is_usdc_data();
    test_lz4_decompression();
    test_crate_reader_invalid();
    test_read_usdc_file();
    test_openusd_compressed_value_reps_equal_raw_size();
    test_inlined_int_vec_reconstruction();

    std::cout << std::endl;
    std::cout << "All USDC tests passed!" << std::endl;
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }
}
