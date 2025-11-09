//
// Test program for tree encoding/decoding
// SPDX-License-Identifier: Apache 2.0
//
#include "tree-encode.hh"
#include "path-sort-api.hh"
#include <iostream>
#include <iomanip>
#include <vector>

using namespace tinyusdz;
using namespace tinyusdz::crate;

void PrintCompressedTree(const CompressedPathTree& tree) {
  std::cout << "\nCompressed Tree Data:\n";
  std::cout << std::string(60, '-') << "\n";
  std::cout << "Size: " << tree.size() << " nodes\n\n";

  std::cout << std::setw(5) << "Idx" << " | "
            << std::setw(10) << "PathIdx" << " | "
            << std::setw(15) << "TokenIdx" << " | "
            << std::setw(8) << "Jump" << " | "
            << "Element\n";
  std::cout << std::string(60, '-') << "\n";

  for (size_t i = 0; i < tree.size(); ++i) {
    std::string element = tree.token_table.GetToken(tree.element_token_indexes[i]);
    std::string jump_str;

    int32_t jump = tree.jumps[i];
    if (jump == -2) {
      jump_str = "LEAF";
    } else if (jump == -1) {
      jump_str = "CHILD";
    } else if (jump == 0) {
      jump_str = "SIBLING";
    } else {
      jump_str = "BOTH(+" + std::to_string(jump) + ")";
    }

    std::cout << std::setw(5) << i << " | "
              << std::setw(10) << tree.path_indexes[i] << " | "
              << std::setw(15) << tree.element_token_indexes[i] << " | "
              << std::setw(8) << jump_str << " | "
              << element << "\n";
  }
}

bool TestEncodeDecodeRoundTrip() {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "Test: Encode/Decode Round-Trip\n";
  std::cout << std::string(60, '=') << "\n";

  // Create test paths
  std::vector<SimplePath> test_paths = {
    SimplePath("/", ""),
    SimplePath("/World", ""),
    SimplePath("/World/Geom", ""),
    SimplePath("/World/Geom", "xformOp:transform"),
    SimplePath("/World/Geom/mesh", ""),
    SimplePath("/World/Geom/mesh", "points"),
    SimplePath("/World/Geom/mesh", "normals"),
    SimplePath("/World/Lights", ""),
    SimplePath("/World/Lights/key", ""),
    SimplePath("/foo", ""),
    SimplePath("/foo/bar", ""),
    SimplePath("/foo/bar", "prop"),
  };

  std::cout << "\nOriginal paths (" << test_paths.size() << "):\n";
  for (size_t i = 0; i < test_paths.size(); ++i) {
    std::cout << "  [" << i << "] " << test_paths[i].full_path_name() << "\n";
  }

  // Sort paths (required before encoding)
  std::vector<SimplePath> sorted_paths = test_paths;
  pathsort::SortSimplePaths(sorted_paths);

  std::cout << "\nSorted paths:\n";
  for (size_t i = 0; i < sorted_paths.size(); ++i) {
    std::cout << "  [" << i << "] " << sorted_paths[i].full_path_name() << "\n";
  }

  // Encode
  std::cout << "\nEncoding...\n";
  CompressedPathTree encoded = EncodePathTree(sorted_paths);

  PrintCompressedTree(encoded);

  // Decode
  std::cout << "\nDecoding...\n";
  std::vector<SimplePath> decoded = DecodePathTree(encoded);

  std::cout << "\nDecoded paths (" << decoded.size() << "):\n";
  for (size_t i = 0; i < decoded.size(); ++i) {
    std::cout << "  [" << i << "] " << decoded[i].full_path_name() << "\n";
  }

  // Verify
  std::cout << "\n" << std::string(60, '-') << "\n";
  std::cout << "Verification:\n";
  std::cout << std::string(60, '-') << "\n";

  bool success = true;

  if (sorted_paths.size() != decoded.size()) {
    std::cout << "FAIL: Size mismatch - "
              << "original: " << sorted_paths.size()
              << ", decoded: " << decoded.size() << "\n";
    success = false;
  } else {
    size_t mismatches = 0;
    for (size_t i = 0; i < sorted_paths.size(); ++i) {
      std::string orig = sorted_paths[i].full_path_name();
      std::string dec = decoded[i].full_path_name();

      if (orig != dec) {
        std::cout << "  [" << i << "] MISMATCH: "
                  << "original=\"" << orig << "\", "
                  << "decoded=\"" << dec << "\"\n";
        mismatches++;
        success = false;
      }
    }

    if (mismatches == 0) {
      std::cout << "SUCCESS: All " << sorted_paths.size() << " paths match!\n";
    } else {
      std::cout << "FAIL: " << mismatches << " mismatches found!\n";
    }
  }

  return success;
}

bool TestTreeStructure() {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "Test: Tree Structure Validation\n";
  std::cout << std::string(60, '=') << "\n";

  // Simpler test case to verify tree structure
  std::vector<SimplePath> paths = {
    SimplePath("/", ""),
    SimplePath("/a", ""),
    SimplePath("/a/b", ""),
    SimplePath("/a/b", "prop1"),
    SimplePath("/a/c", ""),
    SimplePath("/d", ""),
  };

  pathsort::SortSimplePaths(paths);

  std::cout << "\nTest paths:\n";
  for (size_t i = 0; i < paths.size(); ++i) {
    std::cout << "  [" << i << "] " << paths[i].full_path_name() << "\n";
  }

  CompressedPathTree encoded = EncodePathTree(paths);
  PrintCompressedTree(encoded);

  // Verify tree navigation
  std::cout << "\nTree Navigation Verification:\n";
  std::cout << std::string(60, '-') << "\n";

  bool success = true;

  // Expected structure:
  // [0] / (root) - should have child
  // [1] a - should have child and sibling
  // [2] b - should have child and sibling
  // [3] prop1 - should be leaf
  // [4] c - should be leaf
  // [5] d - should be leaf

  struct Expected {
    size_t idx;
    std::string element;
    int32_t jump;
    std::string description;
  };

  std::vector<Expected> expected = {
    {0, "", -1, "root with child"},
    {1, "a", -1, "a with child (d is sibling, but after descendants)"},
    {2, "b", 2, "b with child prop1 and sibling c (offset +2)"},
    {3, "prop1", -2, "prop1 is leaf"},
    {4, "c", -2, "c is leaf"},
    {5, "d", -2, "d is leaf"},
  };

  for (const auto& exp : expected) {
    if (exp.idx >= encoded.size()) {
      std::cout << "  [" << exp.idx << "] ERROR: Index out of bounds\n";
      success = false;
      continue;
    }

    std::string elem = encoded.token_table.GetToken(encoded.element_token_indexes[exp.idx]);
    int32_t jump = encoded.jumps[exp.idx];

    bool match = (jump == exp.jump);
    std::cout << "  [" << exp.idx << "] " << (match ? "✓" : "✗")
              << " " << exp.description
              << " (expected jump=" << exp.jump << ", got=" << jump << ")\n";

    if (!match) {
      success = false;
    }
  }

  return success;
}

bool TestEmptyPaths() {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "Test: Empty Paths\n";
  std::cout << std::string(60, '=') << "\n";

  std::vector<SimplePath> empty_paths;
  CompressedPathTree encoded = EncodePathTree(empty_paths);

  if (encoded.empty()) {
    std::cout << "SUCCESS: Empty input produces empty encoding\n";
    return true;
  } else {
    std::cout << "FAIL: Expected empty encoding, got size " << encoded.size() << "\n";
    return false;
  }
}

bool TestSinglePath() {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "Test: Single Path\n";
  std::cout << std::string(60, '=') << "\n";

  std::vector<SimplePath> paths = { SimplePath("/foo", "") };

  CompressedPathTree encoded = EncodePathTree(paths);
  PrintCompressedTree(encoded);

  std::vector<SimplePath> decoded = DecodePathTree(encoded);

  if (decoded.size() == 1 && decoded[0].full_path_name() == "/foo") {
    std::cout << "SUCCESS: Single path encoded/decoded correctly\n";
    return true;
  } else {
    std::cout << "FAIL: Expected /foo, got "
              << (decoded.empty() ? "empty" : decoded[0].full_path_name()) << "\n";
    return false;
  }
}

int main() {
  std::cout << std::string(60, '=') << "\n";
  std::cout << "PATHS Tree Encoding/Decoding Tests\n";
  std::cout << "Crate Format v0.4.0+ Compressed Format\n";
  std::cout << std::string(60, '=') << "\n";

  bool all_pass = true;

  all_pass &= TestEmptyPaths();
  all_pass &= TestSinglePath();
  all_pass &= TestTreeStructure();
  all_pass &= TestEncodeDecodeRoundTrip();

  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "FINAL RESULT: " << (all_pass ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << "\n";
  std::cout << std::string(60, '=') << "\n";

  return all_pass ? 0 : 1;
}
