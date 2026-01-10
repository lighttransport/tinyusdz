//
// Validation program to compare TinyUSDZ path sorting with OpenUSD SdfPath sorting
// SPDX-License-Identifier: Apache 2.0
//
#include "path-sort-api.hh"

// OpenUSD includes
#include "pxr/usd/sdf/path.h"

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>

using namespace tinyusdz;

// Test case structure
struct TestCase {
  std::string prim_part;
  std::string prop_part;
  std::string description;

  TestCase(const std::string& prim, const std::string& prop, const std::string& desc)
    : prim_part(prim), prop_part(prop), description(desc) {}
};

// Create test paths
std::vector<TestCase> GetTestCases() {
  std::vector<TestCase> tests;

  // Basic absolute paths
  tests.push_back(TestCase("/", "", "Root path"));
  tests.push_back(TestCase("/foo", "", "Single prim"));
  tests.push_back(TestCase("/foo/bar", "", "Two level prim"));
  tests.push_back(TestCase("/foo/bar/baz", "", "Three level prim"));

  // Property paths
  tests.push_back(TestCase("/foo", "prop", "Prim with property"));
  tests.push_back(TestCase("/foo/bar", "prop", "Nested prim with property"));
  tests.push_back(TestCase("/foo", "aaa", "Property aaa"));
  tests.push_back(TestCase("/foo", "zzz", "Property zzz"));

  // Alphabetic ordering tests
  tests.push_back(TestCase("/aaa", "", "Path aaa"));
  tests.push_back(TestCase("/bbb", "", "Path bbb"));
  tests.push_back(TestCase("/zzz", "", "Path zzz"));
  tests.push_back(TestCase("/aaa/bbb", "", "Path aaa/bbb"));
  tests.push_back(TestCase("/aaa/ccc", "", "Path aaa/ccc"));

  // Depth tests
  tests.push_back(TestCase("/a", "", "Shallow path a"));
  tests.push_back(TestCase("/a/b", "", "Path a/b"));
  tests.push_back(TestCase("/a/b/c", "", "Path a/b/c"));
  tests.push_back(TestCase("/a/b/c/d", "", "Deep path a/b/c/d"));

  // Mixed tests
  tests.push_back(TestCase("/World", "", "World prim"));
  tests.push_back(TestCase("/World/Geom", "", "World/Geom"));
  tests.push_back(TestCase("/World/Geom/mesh", "", "World/Geom/mesh"));
  tests.push_back(TestCase("/World/Geom", "xformOp:transform", "World/Geom with xform"));
  tests.push_back(TestCase("/World/Geom/mesh", "points", "mesh with points"));
  tests.push_back(TestCase("/World/Geom/mesh", "normals", "mesh with normals"));

  // Edge cases
  tests.push_back(TestCase("/x", "", "Single char x"));
  tests.push_back(TestCase("/x/y", "", "Single char x/y"));
  tests.push_back(TestCase("/x/y/z", "", "Single char x/y/z"));

  return tests;
}

bool ValidateSort() {
  std::vector<TestCase> test_cases = GetTestCases();

  std::cout << "Creating " << test_cases.size() << " test paths...\n" << std::endl;

  // Create TinyUSDZ paths
  std::vector<SimplePath> tiny_paths;
  for (const auto& tc : test_cases) {
    tiny_paths.push_back(SimplePath(tc.prim_part, tc.prop_part));
  }

  // Create OpenUSD paths
  std::vector<pxr::SdfPath> usd_paths;
  for (const auto& tc : test_cases) {
    std::string path_str = tc.prim_part;
    if (!tc.prop_part.empty()) {
      path_str += "." + tc.prop_part;
    }
    usd_paths.push_back(pxr::SdfPath(path_str));
  }

  // Sort using TinyUSDZ implementation
  std::vector<SimplePath> tiny_sorted = tiny_paths;
  pathsort::SortSimplePaths(tiny_sorted);

  // Sort using OpenUSD SdfPath
  std::vector<pxr::SdfPath> usd_sorted = usd_paths;
  std::sort(usd_sorted.begin(), usd_sorted.end());

  // Compare results
  std::cout << "Comparing sorted results...\n" << std::endl;

  bool all_match = true;
  for (size_t i = 0; i < tiny_sorted.size(); i++) {
    std::string tiny_str = tiny_sorted[i].full_path_name();
    std::string usd_str = usd_sorted[i].GetString();

    bool match = (tiny_str == usd_str);
    if (!match) {
      all_match = false;
    }

    std::cout << "[" << i << "] "
              << (match ? "✓" : "✗")
              << " TinyUSDZ: " << tiny_str
              << " | OpenUSD: " << usd_str
              << std::endl;
  }

  std::cout << "\n" << std::string(60, '=') << std::endl;
  if (all_match) {
    std::cout << "SUCCESS: All paths sorted identically!" << std::endl;
  } else {
    std::cout << "FAILURE: Path sorting differs between implementations!" << std::endl;
  }
  std::cout << std::string(60, '=') << std::endl;

  return all_match;
}

bool ValidatePairwiseComparison() {
  std::cout << "\n\nPairwise Comparison Validation\n" << std::endl;
  std::cout << std::string(60, '=') << std::endl;

  std::vector<TestCase> test_cases = GetTestCases();

  // Create paths
  std::vector<SimplePath> tiny_paths;
  std::vector<pxr::SdfPath> usd_paths;

  for (const auto& tc : test_cases) {
    tiny_paths.push_back(SimplePath(tc.prim_part, tc.prop_part));

    std::string path_str = tc.prim_part;
    if (!tc.prop_part.empty()) {
      path_str += "." + tc.prop_part;
    }
    usd_paths.push_back(pxr::SdfPath(path_str));
  }

  int mismatches = 0;
  int total_comparisons = 0;

  // Compare every pair
  for (size_t i = 0; i < tiny_paths.size(); i++) {
    for (size_t j = 0; j < tiny_paths.size(); j++) {
      if (i == j) continue;

      total_comparisons++;

      // TinyUSDZ comparison
      int tiny_cmp = pathsort::CompareSimplePaths(tiny_paths[i], tiny_paths[j]);
      bool tiny_less = tiny_cmp < 0;

      // OpenUSD comparison
      bool usd_less = usd_paths[i] < usd_paths[j];

      if (tiny_less != usd_less) {
        mismatches++;
        std::cout << "MISMATCH: "
                  << tiny_paths[i].full_path_name() << " vs "
                  << tiny_paths[j].full_path_name()
                  << " | TinyUSDZ: " << (tiny_less ? "less" : "not-less")
                  << " | OpenUSD: " << (usd_less ? "less" : "not-less")
                  << std::endl;
      }
    }
  }

  std::cout << "\nTotal comparisons: " << total_comparisons << std::endl;
  std::cout << "Mismatches: " << mismatches << std::endl;

  if (mismatches == 0) {
    std::cout << "SUCCESS: All pairwise comparisons match!" << std::endl;
  } else {
    std::cout << "FAILURE: " << mismatches << " comparison mismatches found!" << std::endl;
  }

  return mismatches == 0;
}

int main() {
  std::cout << "=" << std::string(60, '=') << std::endl;
  std::cout << "TinyUSDZ Path Sorting Validation" << std::endl;
  std::cout << "Comparing against OpenUSD SdfPath" << std::endl;
  std::cout << "=" << std::string(60, '=') << "\n" << std::endl;

  bool sort_valid = ValidateSort();
  bool pairwise_valid = ValidatePairwiseComparison();

  std::cout << "\n\n" << std::string(60, '=') << std::endl;
  std::cout << "FINAL RESULT" << std::endl;
  std::cout << std::string(60, '=') << std::endl;
  std::cout << "Sort validation:     " << (sort_valid ? "PASS" : "FAIL") << std::endl;
  std::cout << "Pairwise validation: " << (pairwise_valid ? "PASS" : "FAIL") << std::endl;
  std::cout << "Overall:             " << (sort_valid && pairwise_valid ? "PASS" : "FAIL") << std::endl;
  std::cout << std::string(60, '=') << std::endl;

  return (sort_valid && pairwise_valid) ? 0 : 1;
}
