// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Memory Budget Tests

#include <cstdio>
#include <cstdlib>
#include <string>

#include "next/tinyusdz-next.hh"

using namespace tinyusdz::next;

// ============================================================
// Test: MemoryBudget basic operations
// ============================================================

bool test_memory_budget_basic() {
  printf("Testing MemoryBudget basic operations...\n");

  // Test unlimited budget
  {
    MemoryBudget budget;
    if (!budget.IsUnlimited()) {
      printf("  FAILED: Default budget should be unlimited\n");
      return false;
    }
    if (!budget.TryReserve(1024 * 1024 * 1024)) {  // 1GB
      printf("  FAILED: Unlimited budget should allow large allocations\n");
      return false;
    }
    budget.Release(1024 * 1024 * 1024);
  }

  // Test limited budget
  {
    MemoryBudget budget = MemoryBudget::FromMB(10);  // 10MB limit

    if (budget.GetMaxBudget() != 10ULL * 1024 * 1024) {
      printf("  FAILED: Budget not set correctly\n");
      return false;
    }

    // Should succeed
    if (!budget.TryReserve(5 * 1024 * 1024)) {  // 5MB
      printf("  FAILED: Should allow 5MB allocation\n");
      return false;
    }

    if (budget.GetCurrentUsage() != 5 * 1024 * 1024) {
      printf("  FAILED: Usage not tracked correctly\n");
      return false;
    }

    // Should fail (would exceed 10MB)
    if (budget.TryReserve(6 * 1024 * 1024)) {  // 6MB more = 11MB total
      printf("  FAILED: Should reject allocation exceeding budget\n");
      return false;
    }

    // Should succeed (5MB + 5MB = 10MB exactly)
    if (!budget.TryReserve(5 * 1024 * 1024)) {
      printf("  FAILED: Should allow allocation up to limit\n");
      return false;
    }

    // Release some memory
    budget.Release(3 * 1024 * 1024);

    if (budget.GetCurrentUsage() != 7 * 1024 * 1024) {
      printf("  FAILED: Release not tracked correctly\n");
      return false;
    }

    // Should now allow 3MB more
    if (!budget.TryReserve(3 * 1024 * 1024)) {
      printf("  FAILED: Should allow allocation after release\n");
      return false;
    }

    // Reset should clear usage
    budget.Reset();
    if (budget.GetCurrentUsage() != 0) {
      printf("  FAILED: Reset should clear usage\n");
      return false;
    }
  }

  printf("  MemoryBudget basic: PASSED\n");
  return true;
}

// ============================================================
// Test: MemoryBudget scoped reservation
// ============================================================

bool test_memory_budget_scoped() {
  printf("Testing MemoryBudget scoped reservation...\n");

  MemoryBudget budget = MemoryBudget::FromMB(10);

  // Test successful scoped reservation
  {
    auto reservation = budget.ReserveScoped(5 * 1024 * 1024);
    if (!reservation.IsReserved()) {
      printf("  FAILED: Scoped reservation should succeed\n");
      return false;
    }
    if (budget.GetCurrentUsage() != 5 * 1024 * 1024) {
      printf("  FAILED: Scoped reservation not tracked\n");
      return false;
    }
  }
  // After scope, should be released
  if (budget.GetCurrentUsage() != 0) {
    printf("  FAILED: Scoped reservation should auto-release\n");
    return false;
  }

  // Test failed scoped reservation
  {
    budget.TryReserve(9 * 1024 * 1024);  // Reserve 9MB first

    auto reservation = budget.ReserveScoped(2 * 1024 * 1024);  // Try 2MB more
    if (reservation.IsReserved()) {
      printf("  FAILED: Scoped reservation should fail when exceeding budget\n");
      return false;
    }
    if (budget.GetCurrentUsage() != 9 * 1024 * 1024) {
      printf("  FAILED: Failed reservation should not affect usage\n");
      return false;
    }
  }

  // Test commit (transfer ownership)
  budget.Reset();
  {
    auto reservation = budget.ReserveScoped(5 * 1024 * 1024);
    reservation.Commit();  // Transfer ownership
  }
  // After commit, memory should NOT be released
  if (budget.GetCurrentUsage() != 5 * 1024 * 1024) {
    printf("  FAILED: Committed reservation should not auto-release\n");
    return false;
  }

  printf("  MemoryBudget scoped: PASSED\n");
  return true;
}

// ============================================================
// Test: ParseOptions memory limits
// ============================================================

bool test_parse_options() {
  printf("Testing ParseOptions memory limits...\n");

  ParseOptions options;

  // Check defaults
  if (options.max_memory_limit_in_mb != 16384) {
    printf("  FAILED: Default memory limit should be 16384 MB\n");
    return false;
  }

  if (options.max_prim_count != 0) {
    printf("  FAILED: Default prim count limit should be 0 (unlimited)\n");
    return false;
  }

  // Test setting options
  options.max_memory_limit_in_mb = 100;
  options.max_prim_count = 1000;
  options.max_properties_per_prim = 100;
  options.max_array_elements = 1000000;

  if (options.max_memory_limit_in_mb != 100) {
    printf("  FAILED: Memory limit not set correctly\n");
    return false;
  }

  printf("  ParseOptions: PASSED\n");
  return true;
}

// ============================================================
// Test: LoadOptions convenience methods
// ============================================================

bool test_load_options() {
  printf("Testing LoadOptions convenience methods...\n");

  LoadOptions options;

  options.SetMaxMemoryMB(256)
         .SetMaxPrimCount(10000)
         .SetMaxPropertiesPerPrim(500)
         .SetMaxArrayElements(10000000);

  if (options.parse_options.max_memory_limit_in_mb != 256) {
    printf("  FAILED: SetMaxMemoryMB not working\n");
    return false;
  }

  if (options.parse_options.max_prim_count != 10000) {
    printf("  FAILED: SetMaxPrimCount not working\n");
    return false;
  }

  if (options.parse_options.max_properties_per_prim != 500) {
    printf("  FAILED: SetMaxPropertiesPerPrim not working\n");
    return false;
  }

  if (options.parse_options.max_array_elements != 10000000) {
    printf("  FAILED: SetMaxArrayElements not working\n");
    return false;
  }

  printf("  LoadOptions: PASSED\n");
  return true;
}

// ============================================================
// Test: Memory limit during parsing
// ============================================================

bool test_parse_memory_limit() {
  printf("Testing memory limit during parsing...\n");

  // Generate a large USDA string
  std::string usda = "#usda 1.0\n(\n    defaultPrim = \"Root\"\n)\n\n";
  for (int i = 0; i < 1000; ++i) {
    usda += "def Mesh \"Mesh_" + std::to_string(i) + "\"\n{\n";
    usda += "    float3[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]\n";
    usda += "}\n\n";
  }

  // Parse with generous limit - should succeed
  {
    LoadOptions options;
    options.SetMaxMemoryMB(100);  // 100MB should be plenty

    LoadResult result = LoadUSDAFromString(usda, options);
    if (!result.success) {
      printf("  FAILED: Parse with 100MB limit should succeed\n");
      printf("  Error: %s\n", result.error_summary.c_str());
      return false;
    }
  }

  // Parse with tight limit - should fail
  {
    LoadOptions options;
    options.parse_options.max_memory_limit_in_mb = 0;  // Will use very small limit
    options.parse_options.max_prim_count = 10;  // Only allow 10 prims

    LoadResult result = LoadUSDAFromString(usda, options);
    if (result.success) {
      printf("  FAILED: Parse with 10 prim limit should fail for 1000 prims\n");
      return false;
    }
    // Check that error message mentions the limit
    if (result.error_summary.find("prim count") == std::string::npos &&
        result.error_summary.find("Maximum") == std::string::npos) {
      printf("  Warning: Error message doesn't clearly indicate limit exceeded\n");
      printf("  Error: %s\n", result.error_summary.c_str());
    }
  }

  printf("  Parse memory limit: PASSED\n");
  return true;
}

// ============================================================
// Test: USDCLoadOptions
// ============================================================

bool test_usdc_load_options() {
  printf("Testing USDCLoadOptions...\n");

  USDCLoadOptions options;

  options.SetMaxMemoryMB(512)
         .SetMaxTokens(100000)
         .SetMaxSpecs(500000);

  if (options.crate_options.max_memory != 512ULL * 1024 * 1024) {
    printf("  FAILED: SetMaxMemoryMB not working\n");
    return false;
  }

  if (options.crate_options.max_tokens != 100000) {
    printf("  FAILED: SetMaxTokens not working\n");
    return false;
  }

  if (options.crate_options.max_specs != 500000) {
    printf("  FAILED: SetMaxSpecs not working\n");
    return false;
  }

  printf("  USDCLoadOptions: PASSED\n");
  return true;
}

// ============================================================
// Main
// ============================================================

int main() {
  printf("=== Memory Budget Tests ===\n\n");

  bool all_passed = true;

  all_passed &= test_memory_budget_basic();
  all_passed &= test_memory_budget_scoped();
  all_passed &= test_parse_options();
  all_passed &= test_load_options();
  all_passed &= test_parse_memory_limit();
  all_passed &= test_usdc_load_options();

  printf("\n");
  if (all_passed) {
    printf("=== All Memory Budget tests PASSED ===\n");
    return 0;
  } else {
    printf("=== Some tests FAILED ===\n");
    return 1;
  }
}
