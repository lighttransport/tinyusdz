// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Mmap and Zero-copy Tests

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fstream>

#include "next/tinyusdz-next.hh"

using namespace tinyusdz::next;

// ============================================================
// Test: TypedArrayView basic operations
// ============================================================

bool test_typed_array_view() {
  printf("Testing TypedArrayView...\n");

  // Create test data
  std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};

  // Create view
  FloatArrayView view(data.data(), data.size());

  if (view.size() != 5) {
    printf("  FAILED: View size mismatch\n");
    return false;
  }

  if (view.empty()) {
    printf("  FAILED: View should not be empty\n");
    return false;
  }

  if (view[0] != 1.0f || view[4] != 5.0f) {
    printf("  FAILED: View element access failed\n");
    return false;
  }

  if (view.front() != 1.0f || view.back() != 5.0f) {
    printf("  FAILED: View front/back failed\n");
    return false;
  }

  // Test iteration
  float sum = 0;
  for (float f : view) {
    sum += f;
  }
  if (sum != 15.0f) {
    printf("  FAILED: View iteration failed\n");
    return false;
  }

  // Test subview
  auto sub = view.subview(1, 3);
  if (sub.size() != 3 || sub[0] != 2.0f || sub[2] != 4.0f) {
    printf("  FAILED: Subview failed\n");
    return false;
  }

  // Test first/last
  auto first2 = view.first(2);
  if (first2.size() != 2 || first2[0] != 1.0f) {
    printf("  FAILED: first() failed\n");
    return false;
  }

  auto last2 = view.last(2);
  if (last2.size() != 2 || last2[0] != 4.0f) {
    printf("  FAILED: last() failed\n");
    return false;
  }

  printf("  TypedArrayView: PASSED\n");
  return true;
}

// ============================================================
// Test: Value array views
// ============================================================

bool test_value_array_view() {
  printf("Testing Value array views...\n");

  // Create test data
  std::vector<float> float_data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  std::vector<int32_t> int_data = {10, 20, 30, 40};

  // Create float array view
  Value float_view = Value::MakeFloatArrayView(float_data.data(), float_data.size());

  if (!float_view.is_array()) {
    printf("  FAILED: Float view should be array\n");
    return false;
  }

  if (!float_view.is_view()) {
    printf("  FAILED: Float view should be a view\n");
    return false;
  }

  if (float_view.array_size() != 6) {
    printf("  FAILED: Float view size mismatch\n");
    return false;
  }

  // Get typed view
  auto fv = float_view.float_array_view();
  if (fv.size() != 6 || fv[0] != 1.0f || fv[5] != 6.0f) {
    printf("  FAILED: Float array view accessor failed\n");
    return false;
  }

  // Create int array view
  Value int_view = Value::MakeIntArrayView(int_data.data(), int_data.size());

  if (!int_view.is_view() || int_view.array_size() != 4) {
    printf("  FAILED: Int view creation failed\n");
    return false;
  }

  auto iv = int_view.int_array_view();
  if (iv.size() != 4 || iv[0] != 10 || iv[3] != 40) {
    printf("  FAILED: Int array view accessor failed\n");
    return false;
  }

  // Create float3 array view
  Value float3_view = Value::MakeFloat3ArrayView(float_data.data(), 2);  // 2 float3s

  if (!float3_view.is_view() || float3_view.array_size() != 2) {
    printf("  FAILED: Float3 view creation failed\n");
    return false;
  }

  auto f3v = float3_view.float_array_view();
  if (f3v.size() != 6) {  // 2 float3s = 6 floats
    printf("  FAILED: Float3 array view size mismatch: got %zu, expected 6\n", f3v.size());
    return false;
  }

  printf("  Value array views: PASSED\n");
  return true;
}

// ============================================================
// Test: View to owned conversion
// ============================================================

bool test_make_owned() {
  printf("Testing make_owned()...\n");

  // Create test data
  std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f};

  // Create view
  Value view = Value::MakeFloatArrayView(data.data(), data.size());

  if (!view.is_view()) {
    printf("  FAILED: Should be a view initially\n");
    return false;
  }

  // as_float_array should return nullptr for views
  if (view.as_float_array() != nullptr) {
    printf("  FAILED: as_float_array should return nullptr for views\n");
    return false;
  }

  // Convert to owned
  bool converted = view.make_owned();

  if (!converted) {
    printf("  FAILED: make_owned should return true\n");
    return false;
  }

  if (view.is_view()) {
    printf("  FAILED: Should not be a view after make_owned\n");
    return false;
  }

  // as_float_array should now work
  const auto* arr = view.as_float_array();
  if (!arr) {
    printf("  FAILED: as_float_array should work after make_owned\n");
    return false;
  }

  if (arr->size() != 4 || (*arr)[0] != 1.0f || (*arr)[3] != 4.0f) {
    printf("  FAILED: Owned data mismatch\n");
    return false;
  }

  // Modify original data - view should be independent now
  data[0] = 999.0f;
  if ((*arr)[0] != 1.0f) {
    printf("  FAILED: Owned copy should be independent of original\n");
    return false;
  }

  printf("  make_owned: PASSED\n");
  return true;
}

// ============================================================
// Test: MmapFile
// ============================================================

bool test_mmap_file() {
  printf("Testing MmapFile...\n");

  // Create a temporary test file
  const char* test_filename = "/tmp/test_mmap.bin";

  // Write test data
  {
    std::ofstream ofs(test_filename, std::ios::binary);
    if (!ofs) {
      printf("  SKIPPED: Cannot create test file\n");
      return true;
    }

    // Write some test data
    std::vector<float> data(1000);
    for (size_t i = 0; i < data.size(); ++i) {
      data[i] = static_cast<float>(i);
    }
    ofs.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(float));
  }

  // Open with mmap
  MmapFile mmap;
  std::string err;
  if (!mmap.Open(test_filename, &err)) {
    printf("  FAILED: Cannot mmap file: %s\n", err.c_str());
    return false;
  }

  if (!mmap.IsOpen()) {
    printf("  FAILED: MmapFile should be open\n");
    return false;
  }

  if (mmap.size() != 1000 * sizeof(float)) {
    printf("  FAILED: Size mismatch: got %zu, expected %zu\n",
           mmap.size(), 1000 * sizeof(float));
    return false;
  }

  // Access data
  const float* float_data = reinterpret_cast<const float*>(mmap.data());
  if (float_data[0] != 0.0f || float_data[999] != 999.0f) {
    printf("  FAILED: Data mismatch\n");
    return false;
  }

  // Test at() bounds check
  if (mmap.at(mmap.size()) != nullptr) {
    printf("  FAILED: at() should return nullptr for out of bounds\n");
    return false;
  }

  // Close and verify
  mmap.Close();
  if (mmap.IsOpen()) {
    printf("  FAILED: MmapFile should be closed\n");
    return false;
  }

  // Clean up
  std::remove(test_filename);

  printf("  MmapFile: PASSED\n");
  return true;
}

// ============================================================
// Test: MmapFileRef sharing
// ============================================================

bool test_mmap_file_ref() {
  printf("Testing MmapFileRef...\n");

  // Create a temporary test file
  const char* test_filename = "/tmp/test_mmap_ref.bin";

  {
    std::ofstream ofs(test_filename, std::ios::binary);
    if (!ofs) {
      printf("  SKIPPED: Cannot create test file\n");
      return true;
    }
    uint32_t data = 0x12345678;
    ofs.write(reinterpret_cast<const char*>(&data), sizeof(data));
  }

  // Create shared reference
  std::string err;
  MmapFileRef ref = MakeMmapFile(test_filename, &err);

  if (!ref.IsValid()) {
    printf("  FAILED: MakeMmapFile failed: %s\n", err.c_str());
    return false;
  }

  if (ref.use_count() != 1) {
    printf("  FAILED: Initial use count should be 1\n");
    return false;
  }

  // Copy the reference
  MmapFileRef ref2 = ref;

  if (ref.use_count() != 2 || ref2.use_count() != 2) {
    printf("  FAILED: Use count should be 2 after copy\n");
    return false;
  }

  // Access through both references
  if (ref->size() != ref2->size()) {
    printf("  FAILED: Both refs should point to same data\n");
    return false;
  }

  // Clean up
  ref = MmapFileRef();  // Release first ref
  if (!ref2.IsValid()) {
    printf("  FAILED: ref2 should still be valid\n");
    return false;
  }

  if (ref2.use_count() != 1) {
    printf("  FAILED: Use count should be 1 after releasing ref\n");
    return false;
  }

  ref2 = MmapFileRef();  // Release second ref
  std::remove(test_filename);

  printf("  MmapFileRef: PASSED\n");
  return true;
}

// ============================================================
// Test: Memory budget with mmap tracking
// ============================================================

bool test_memory_budget_mmap() {
  printf("Testing MemoryBudget mmap tracking...\n");

  MemoryBudget budget = MemoryBudget::FromMB(10);

  // Track some heap memory
  if (!budget.TryReserve(5 * 1024 * 1024)) {
    printf("  FAILED: Should allow 5MB heap allocation\n");
    return false;
  }

  // Track mmap memory (doesn't count against budget)
  budget.TrackMmapMemory(100 * 1024 * 1024);  // 100MB mmap

  // Check totals
  if (budget.GetCurrentUsage() != 5 * 1024 * 1024) {
    printf("  FAILED: Heap usage incorrect\n");
    return false;
  }

  if (budget.GetMmapUsage() != 100 * 1024 * 1024) {
    printf("  FAILED: Mmap usage incorrect\n");
    return false;
  }

  if (budget.GetTotalMemory() != 105 * 1024 * 1024) {
    printf("  FAILED: Total memory incorrect\n");
    return false;
  }

  // Heap budget should still be respected
  if (budget.TryReserve(6 * 1024 * 1024)) {  // Would exceed 10MB heap limit
    printf("  FAILED: Should reject heap allocation exceeding budget\n");
    return false;
  }

  // Test ShouldUseZeroCopy
  budget.Reset();
  budget.SetMaxBudgetMB(1);  // 1MB budget

  // Small data - should not use zero copy
  if (budget.ShouldUseZeroCopy(100)) {
    printf("  FAILED: Small data should not use zero copy\n");
    return false;
  }

  // Large data that would exceed budget - should use zero copy
  if (!budget.ShouldUseZeroCopy(2 * 1024 * 1024)) {
    printf("  FAILED: Large data exceeding budget should use zero copy\n");
    return false;
  }

  printf("  MemoryBudget mmap: PASSED\n");
  return true;
}

// ============================================================
// Test: CrateReadOptions zero-copy settings
// ============================================================

bool test_crate_read_options() {
  printf("Testing CrateReadOptions zero-copy settings...\n");

  CrateReadOptions options;

  // Check defaults
  // use_mmap should be true on 64-bit systems
  if (sizeof(void*) >= 8 && !options.use_mmap) {
    printf("  FAILED: use_mmap should default to true on 64-bit\n");
    return false;
  }

  if (!options.zero_copy_arrays) {
    printf("  FAILED: zero_copy_arrays should default to true\n");
    return false;
  }

  if (options.force_copy) {
    printf("  FAILED: force_copy should default to false\n");
    return false;
  }

  // Test explicit settings
  options.use_mmap = false;
  options.zero_copy_arrays = false;
  options.force_copy = true;

  if (options.use_mmap || options.zero_copy_arrays || !options.force_copy) {
    printf("  FAILED: Option settings not applied\n");
    return false;
  }

  printf("  CrateReadOptions: PASSED\n");
  return true;
}

// ============================================================
// Test: View copy behavior
// ============================================================

bool test_view_copy() {
  printf("Testing view copy behavior...\n");

  std::vector<float> data = {1.0f, 2.0f, 3.0f};

  // Create view
  Value v1 = Value::MakeFloatArrayView(data.data(), data.size());

  // Copy the view
  Value v2 = v1;

  if (!v2.is_view()) {
    printf("  FAILED: Copied view should still be a view\n");
    return false;
  }

  // Both should point to same data
  auto fv1 = v1.float_array_view();
  auto fv2 = v2.float_array_view();

  if (fv1.data() != fv2.data()) {
    printf("  FAILED: Views should share same underlying data\n");
    return false;
  }

  // Modify original data
  data[0] = 999.0f;

  // Both views should see the change
  if (fv1[0] != 999.0f || fv2[0] != 999.0f) {
    printf("  FAILED: Views should reflect changes in original data\n");
    return false;
  }

  printf("  View copy: PASSED\n");
  return true;
}

// ============================================================
// Main
// ============================================================

int main() {
  printf("=== Mmap and Zero-copy Tests ===\n\n");

  bool all_passed = true;

  all_passed &= test_typed_array_view();
  all_passed &= test_value_array_view();
  all_passed &= test_make_owned();
  all_passed &= test_mmap_file();
  all_passed &= test_mmap_file_ref();
  all_passed &= test_memory_budget_mmap();
  all_passed &= test_crate_read_options();
  all_passed &= test_view_copy();

  printf("\n");
  if (all_passed) {
    printf("=== All Mmap/Zero-copy tests PASSED ===\n");
    return 0;
  } else {
    printf("=== Some tests FAILED ===\n");
    return 1;
  }
}
