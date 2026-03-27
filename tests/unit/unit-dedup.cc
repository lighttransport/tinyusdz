#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-dedup.h"
#include "../../src/crate-writer.hh"
#include "../../src/tinyusdz.hh"
#include "../../src/core/prim.hh"
#include "../../src/core/prim-spec.hh"
#include "../../src/value-types.hh"
#include "../../src/timesamples.hh"
#include "../../src/io-util.hh"
#include <cmath>
#include <fstream>

using namespace tinyusdz;
using namespace tinyusdz::experimental;

// Helper function to get file size
static size_t GetFileSize(const std::string& filename) {
  std::ifstream file(filename, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return 0;
  }
  return static_cast<size_t>(file.tellg());
}

// Test 1: Basic deduplication - repeated float arrays
void dedup_float_array_test(void) {
  std::cout << "\n=== Test: Float Array Deduplication ===" << std::endl;

  // Create a stage with animated attribute containing repeated array values
  Stage stage;

  // Create Xform prim
  Xform xform;
  xform.name = "DedupTest";
  xform.spec = Specifier::Def;

  // Create a custom attribute with float[] TimeSamples
  Attribute attr;
  attr.set_type_name("float[]");
  attr.set_var(Variability::Varying);

  // Create TimeSamples with 100 frames
  // First 50 frames: same array [1.0, 2.0, 3.0, 4.0, 5.0]
  // Next 50 frames: same array [6.0, 7.0, 8.0, 9.0, 10.0]
  value::TimeSamples ts;

  std::vector<float> array1 = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
  std::vector<float> array2 = {6.0f, 7.0f, 8.0f, 9.0f, 10.0f};

  for (int frame = 1; frame <= 100; frame++) {
    double time = static_cast<double>(frame);
    if (frame <= 50) {
      ts.add_sample(time, value::Value(array1));
    } else {
      ts.add_sample(time, value::Value(array2));
    }
  }

  // Set TimeSamples to attribute
  PrimAttrib prim_attr;
  prim_attr._var._ts = ts;
  prim_attr._var._value = value::Value(array1); // default value

  attr.set_var(prim_attr);

  // Add attribute to xform properties
  xform.props()["testArray"] = Property(attr, /* custom */ false);

  // Add Xform to stage
  Prim prim(xform);
  prim.element_name() = "DedupTest";
  prim.spec() = Specifier::Def;
  stage.root_prims().emplace_back(prim);

  // Test 1a: Write with deduplication ENABLED
  std::string filename_dedup = "/tmp/test_dedup_enabled.usdc";
  {
    std::string err;
    CrateWriter writer(filename_dedup);
    CrateWriter::Options opts;
    opts.enable_deduplication = true;  // ENABLE
    writer.SetOptions(opts);

    TEST_CHECK(writer.Open(&err));
    TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
    TEST_CHECK(writer.Finalize(&err));
    writer.Close();

    std::cout << "  Written with dedup: " << filename_dedup << std::endl;
  }

  // Test 1b: Write with deduplication DISABLED
  std::string filename_no_dedup = "/tmp/test_dedup_disabled.usdc";
  {
    std::string err;
    CrateWriter writer(filename_no_dedup);
    CrateWriter::Options opts;
    opts.enable_deduplication = false;  // DISABLE
    writer.SetOptions(opts);

    TEST_CHECK(writer.Open(&err));
    TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
    TEST_CHECK(writer.Finalize(&err));
    writer.Close();

    std::cout << "  Written without dedup: " << filename_no_dedup << std::endl;
  }

  // Test 1c: Compare file sizes
  size_t size_dedup = GetFileSize(filename_dedup);
  size_t size_no_dedup = GetFileSize(filename_no_dedup);

  std::cout << "  File size with dedup:    " << size_dedup << " bytes" << std::endl;
  std::cout << "  File size without dedup: " << size_no_dedup << " bytes" << std::endl;

  // Dedup file should be significantly smaller
  // We have 100 samples, but only 2 unique arrays
  // Without dedup: 100 arrays written
  // With dedup: only 2 arrays written
  // So dedup file should be much smaller
  TEST_CHECK(size_dedup < size_no_dedup);

  // Calculate savings percentage
  if (size_no_dedup > 0) {
    double savings = 100.0 * (1.0 - static_cast<double>(size_dedup) / static_cast<double>(size_no_dedup));
    std::cout << "  Space savings: " << savings << "%" << std::endl;

    // We expect at least 30% savings for this test case
    TEST_CHECK(savings > 30.0);
  }

  // Test 1d: Read back and verify correctness
  {
    Stage stage_read;
    std::string warn, err;
    bool ret = tinyusdz::LoadUSDFromFile(filename_dedup, &stage_read, &warn, &err);
    TEST_CHECK(ret == true);

    if (ret) {
      std::cout << "  Successfully read back deduplicated file" << std::endl;

      // Verify we can access the prim and attribute
      TEST_CHECK(stage_read.root_prims().size() == 1);

      // Note: Full verification of TimeSamples values would require
      // accessing the attribute data which involves more complex API
      // For now, just verify the file reads successfully
    }
  }

  std::cout << "  Test PASSED" << std::endl;
}

// Test 2: Double array deduplication
void dedup_double_array_test(void) {
  std::cout << "\n=== Test: Double Array Deduplication ===" << std::endl;

  Stage stage;
  Xform xform;
  xform.name = "DedupDoubleTest";
  xform.spec = Specifier::Def;

  Attribute attr;
  attr.set_type_name("double[]");
  attr.set_var(Variability::Varying);

  value::TimeSamples ts;
  std::vector<double> constant_array = {1.5, 2.5, 3.5, 4.5};

  // All 50 frames have the SAME array - perfect deduplication case
  for (int frame = 1; frame <= 50; frame++) {
    double time = static_cast<double>(frame);
    ts.add_sample(time, value::Value(constant_array));
  }

  PrimAttrib prim_attr;
  prim_attr._var._ts = ts;
  prim_attr._var._value = value::Value(constant_array);
  attr.set_var(prim_attr);

  xform.props()["doubleArray"] = Property(attr, false);

  Prim prim(xform);
  prim.element_name() = "DedupDoubleTest";
  prim.spec() = Specifier::Def;
  stage.root_prims().emplace_back(prim);

  // Write with dedup
  std::string filename = "/tmp/test_dedup_double.usdc";
  std::string err;
  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.enable_deduplication = true;
  writer.SetOptions(opts);

  TEST_CHECK(writer.Open(&err));
  TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
  TEST_CHECK(writer.Finalize(&err));
  writer.Close();

  std::cout << "  File written: " << filename << std::endl;

  // Read back
  Stage stage_read;
  std::string warn;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &stage_read, &warn, &err);
  TEST_CHECK(ret == true);

  std::cout << "  Test PASSED" << std::endl;
}

// Test 3: Int array deduplication
void dedup_int_array_test(void) {
  std::cout << "\n=== Test: Int Array Deduplication ===" << std::endl;

  Stage stage;
  Xform xform;
  xform.name = "DedupIntTest";
  xform.spec = Specifier::Def;

  Attribute attr;
  attr.set_type_name("int[]");
  attr.set_var(Variability::Varying);

  value::TimeSamples ts;
  std::vector<int32_t> array1 = {10, 20, 30};
  std::vector<int32_t> array2 = {40, 50, 60};
  std::vector<int32_t> array3 = {70, 80, 90};

  // Create pattern: array1, array1, array2, array2, array3, array3, ...
  for (int frame = 1; frame <= 30; frame++) {
    double time = static_cast<double>(frame);
    int pattern = ((frame - 1) / 2) % 3;
    if (pattern == 0) {
      ts.add_sample(time, value::Value(array1));
    } else if (pattern == 1) {
      ts.add_sample(time, value::Value(array2));
    } else {
      ts.add_sample(time, value::Value(array3));
    }
  }

  PrimAttrib prim_attr;
  prim_attr._var._ts = ts;
  prim_attr._var._value = value::Value(array1);
  attr.set_var(prim_attr);

  xform.props()["intArray"] = Property(attr, false);

  Prim prim(xform);
  prim.element_name() = "DedupIntTest";
  prim.spec() = Specifier::Def;
  stage.root_prims().emplace_back(prim);

  std::string filename_dedup = "/tmp/test_dedup_int_enabled.usdc";
  std::string filename_no_dedup = "/tmp/test_dedup_int_disabled.usdc";

  // With dedup
  {
    std::string err;
    CrateWriter writer(filename_dedup);
    CrateWriter::Options opts;
    opts.enable_deduplication = true;
    writer.SetOptions(opts);

    TEST_CHECK(writer.Open(&err));
    TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
    TEST_CHECK(writer.Finalize(&err));
    writer.Close();
  }

  // Without dedup
  {
    std::string err;
    CrateWriter writer(filename_no_dedup);
    CrateWriter::Options opts;
    opts.enable_deduplication = false;
    writer.SetOptions(opts);

    TEST_CHECK(writer.Open(&err));
    TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
    TEST_CHECK(writer.Finalize(&err));
    writer.Close();
  }

  size_t size_dedup = GetFileSize(filename_dedup);
  size_t size_no_dedup = GetFileSize(filename_no_dedup);

  std::cout << "  Size with dedup:    " << size_dedup << " bytes" << std::endl;
  std::cout << "  Size without dedup: " << size_no_dedup << " bytes" << std::endl;

  TEST_CHECK(size_dedup < size_no_dedup);

  std::cout << "  Test PASSED" << std::endl;
}

// Test 4: No deduplication opportunity (all unique arrays)
void dedup_unique_arrays_test(void) {
  std::cout << "\n=== Test: Unique Arrays (No Dedup Opportunity) ===" << std::endl;

  Stage stage;
  Xform xform;
  xform.name = "UniqueArraysTest";
  xform.spec = Specifier::Def;

  Attribute attr;
  attr.set_type_name("float[]");
  attr.set_var(Variability::Varying);

  value::TimeSamples ts;

  // Each frame has a UNIQUE array
  for (int frame = 1; frame <= 20; frame++) {
    double time = static_cast<double>(frame);
    std::vector<float> unique_array;
    for (int i = 0; i < 5; i++) {
      unique_array.push_back(static_cast<float>(frame * 10 + i));
    }
    ts.add_sample(time, value::Value(unique_array));
  }

  PrimAttrib prim_attr;
  prim_attr._var._ts = ts;
  std::vector<float> default_arr = {0.0f};
  prim_attr._var._value = value::Value(default_arr);
  attr.set_var(prim_attr);

  xform.props()["uniqueArray"] = Property(attr, false);

  Prim prim(xform);
  prim.element_name() = "UniqueArraysTest";
  prim.spec() = Specifier::Def;
  stage.root_prims().emplace_back(prim);

  std::string filename_dedup = "/tmp/test_unique_dedup.usdc";
  std::string filename_no_dedup = "/tmp/test_unique_no_dedup.usdc";

  // With dedup
  {
    std::string err;
    CrateWriter writer(filename_dedup);
    CrateWriter::Options opts;
    opts.enable_deduplication = true;
    writer.SetOptions(opts);

    TEST_CHECK(writer.Open(&err));
    TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
    TEST_CHECK(writer.Finalize(&err));
    writer.Close();
  }

  // Without dedup
  {
    std::string err;
    CrateWriter writer(filename_no_dedup);
    CrateWriter::Options opts;
    opts.enable_deduplication = false;
    writer.SetOptions(opts);

    TEST_CHECK(writer.Open(&err));
    TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
    TEST_CHECK(writer.Finalize(&err));
    writer.Close();
  }

  size_t size_dedup = GetFileSize(filename_dedup);
  size_t size_no_dedup = GetFileSize(filename_no_dedup);

  std::cout << "  Size with dedup:    " << size_dedup << " bytes" << std::endl;
  std::cout << "  Size without dedup: " << size_no_dedup << " bytes" << std::endl;

  // Since all arrays are unique, file sizes should be very similar
  // Allow for small overhead differences (< 5%)
  double ratio = static_cast<double>(size_dedup) / static_cast<double>(size_no_dedup);
  TEST_CHECK(ratio > 0.95 && ratio < 1.05);

  std::cout << "  File sizes similar (no dedup opportunity): " << (ratio * 100.0) << "%" << std::endl;
  std::cout << "  Test PASSED" << std::endl;
}

// Test 5: String array deduplication
void dedup_string_array_test(void) {
  std::cout << "\n=== Test: String Array Deduplication ===" << std::endl;

  Stage stage;
  Xform xform;
  xform.name = "StringArrayTest";
  xform.spec = Specifier::Def;

  Attribute attr;
  attr.set_type_name("string[]");
  attr.set_var(Variability::Varying);

  value::TimeSamples ts;
  std::vector<std::string> repeated_array = {"hello", "world", "usd"};
  std::vector<std::string> different_array = {"foo", "bar", "baz"};

  // Pattern: repeated_array appears in 30 frames, different_array in 20 frames
  for (int frame = 1; frame <= 50; frame++) {
    double time = static_cast<double>(frame);
    if (frame <= 30) {
      ts.add_sample(time, value::Value(repeated_array));
    } else {
      ts.add_sample(time, value::Value(different_array));
    }
  }

  PrimAttrib prim_attr;
  prim_attr._var._ts = ts;
  prim_attr._var._value = value::Value(repeated_array);
  attr.set_var(prim_attr);

  xform.props()["stringArray"] = Property(attr, false);

  Prim prim(xform);
  prim.element_name() = "StringArrayTest";
  prim.spec() = Specifier::Def;
  stage.root_prims().emplace_back(prim);

  std::string filename_dedup = "/tmp/test_dedup_string_enabled.usdc";
  std::string filename_no_dedup = "/tmp/test_dedup_string_disabled.usdc";

  // With dedup
  {
    std::string err;
    CrateWriter writer(filename_dedup);
    CrateWriter::Options opts;
    opts.enable_deduplication = true;
    writer.SetOptions(opts);

    TEST_CHECK(writer.Open(&err));
    TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
    TEST_CHECK(writer.Finalize(&err));
    writer.Close();
  }

  // Without dedup
  {
    std::string err;
    CrateWriter writer(filename_no_dedup);
    CrateWriter::Options opts;
    opts.enable_deduplication = false;
    writer.SetOptions(opts);

    TEST_CHECK(writer.Open(&err));
    TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
    TEST_CHECK(writer.Finalize(&err));
    writer.Close();
  }

  size_t size_dedup = GetFileSize(filename_dedup);
  size_t size_no_dedup = GetFileSize(filename_no_dedup);

  std::cout << "  Size with dedup:    " << size_dedup << " bytes" << std::endl;
  std::cout << "  Size without dedup: " << size_no_dedup << " bytes" << std::endl;

  // Should see space savings
  TEST_CHECK(size_dedup < size_no_dedup);

  if (size_no_dedup > 0) {
    double savings = 100.0 * (1.0 - static_cast<double>(size_dedup) / static_cast<double>(size_no_dedup));
    std::cout << "  Space savings: " << savings << "%" << std::endl;
  }

  std::cout << "  Test PASSED" << std::endl;
}

// Test 6: Matrix4d scalar deduplication (transform animations)
void dedup_matrix4d_test(void) {
  std::cout << "\n=== Test: Matrix4d Scalar Deduplication ===" << std::endl;

  Stage stage;
  Xform xform;
  xform.name = "MatrixTest";
  xform.spec = Specifier::Def;

  Attribute attr;
  attr.set_type_name("matrix4d");
  attr.set_var(Variability::Varying);

  value::TimeSamples ts;

  // Create two distinct matrices
  value::matrix4d identity_matrix;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      identity_matrix.m[i][j] = (i == j) ? 1.0 : 0.0;
    }
  }

  value::matrix4d transform_matrix;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      transform_matrix.m[i][j] = (i == j) ? 2.0 : 0.5;
    }
  }

  // Repeat identity matrix for 40 frames, transform for 10 frames
  for (int frame = 1; frame <= 50; frame++) {
    double time = static_cast<double>(frame);
    if (frame <= 40) {
      ts.add_sample(time, value::Value(identity_matrix));
    } else {
      ts.add_sample(time, value::Value(transform_matrix));
    }
  }

  PrimAttrib prim_attr;
  prim_attr._var._ts = ts;
  prim_attr._var._value = value::Value(identity_matrix);
  attr.set_var(prim_attr);

  xform.props()["xformMatrix"] = Property(attr, false);

  Prim prim(xform);
  prim.element_name() = "MatrixTest";
  prim.spec() = Specifier::Def;
  stage.root_prims().emplace_back(prim);

  std::string filename_dedup = "/tmp/test_dedup_matrix_enabled.usdc";
  std::string filename_no_dedup = "/tmp/test_dedup_matrix_disabled.usdc";

  // With dedup
  {
    std::string err;
    CrateWriter writer(filename_dedup);
    CrateWriter::Options opts;
    opts.enable_deduplication = true;
    writer.SetOptions(opts);

    TEST_CHECK(writer.Open(&err));
    TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
    TEST_CHECK(writer.Finalize(&err));
    writer.Close();
  }

  // Without dedup
  {
    std::string err;
    CrateWriter writer(filename_no_dedup);
    CrateWriter::Options opts;
    opts.enable_deduplication = false;
    writer.SetOptions(opts);

    TEST_CHECK(writer.Open(&err));
    TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
    TEST_CHECK(writer.Finalize(&err));
    writer.Close();
  }

  size_t size_dedup = GetFileSize(filename_dedup);
  size_t size_no_dedup = GetFileSize(filename_no_dedup);

  std::cout << "  Size with dedup:    " << size_dedup << " bytes" << std::endl;
  std::cout << "  Size without dedup: " << size_no_dedup << " bytes" << std::endl;

  // Should see significant space savings (matrices are 128 bytes each)
  TEST_CHECK(size_dedup < size_no_dedup);

  if (size_no_dedup > 0) {
    double savings = 100.0 * (1.0 - static_cast<double>(size_dedup) / static_cast<double>(size_no_dedup));
    std::cout << "  Space savings: " << savings << "%" << std::endl;
    // Expect at least 20% savings
    TEST_CHECK(savings > 20.0);
  }

  std::cout << "  Test PASSED" << std::endl;
}
