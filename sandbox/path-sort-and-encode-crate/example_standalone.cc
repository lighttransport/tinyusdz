//
// Standalone example of using the crate encoding library
// No TinyUSDZ or OpenUSD dependencies required
//
// Compile:
//   g++ -std=c++17 -I include example_standalone.cc src/*.cc -o example
//
// Run:
//   ./example
//

#include "crate/path_interface.hh"
#include "crate/path_sort.hh"
#include "crate/tree_encode.hh"
#include <iostream>
#include <iomanip>

using namespace crate;

void PrintTree(const CompressedPathTree& tree) {
  std::cout << "\nCompressed Tree (" << tree.size() << " nodes):\n";
  std::cout << std::string(70, '-') << "\n";
  std::cout << std::setw(4) << "Idx" << " | "
            << std::setw(8) << "PathIdx" << " | "
            << std::setw(10) << "TokenIdx" << " | "
            << std::setw(8) << "Jump" << " | "
            << "Element\n";
  std::cout << std::string(70, '-') << "\n";

  for (size_t i = 0; i < tree.size(); ++i) {
    std::string element = tree.token_table.GetToken(tree.element_token_indexes[i]);

    std::string jump_str;
    int32_t jump = tree.jumps[i];
    if (jump == -2) jump_str = "LEAF";
    else if (jump == -1) jump_str = "CHILD";
    else if (jump == 0) jump_str = "SIBLING";
    else jump_str = "BOTH(+" + std::to_string(jump) + ")";

    std::cout << std::setw(4) << i << " | "
              << std::setw(8) << tree.path_indexes[i] << " | "
              << std::setw(10) << tree.element_token_indexes[i] << " | "
              << std::setw(8) << jump_str << " | "
              << element << "\n";
  }
  std::cout << std::string(70, '-') << "\n";
}

int main() {
  std::cout << "==================================\n";
  std::cout << "Crate Path Encoding - Standalone Example\n";
  std::cout << "==================================\n";

  // Step 1: Create paths using built-in SimplePath
  std::cout << "\n1. Creating paths...\n";
  std::vector<SimplePath> paths = {
    SimplePath("/", ""),
    SimplePath("/World", ""),
    SimplePath("/World/Geom", ""),
    SimplePath("/World/Geom/mesh", ""),
    SimplePath("/World/Geom/mesh", "points"),
    SimplePath("/World/Geom/mesh", "normals"),
    SimplePath("/World/Lights", ""),
    SimplePath("/World/Lights/key", ""),
  };

  std::cout << "Created " << paths.size() << " paths:\n";
  for (size_t i = 0; i < paths.size(); ++i) {
    std::cout << "  [" << i << "] " << paths[i].GetString() << "\n";
  }

  // Step 2: Sort paths (REQUIRED before encoding)
  std::cout << "\n2. Sorting paths...\n";
  SortSimplePaths(paths);

  std::cout << "Sorted order:\n";
  for (size_t i = 0; i < paths.size(); ++i) {
    std::cout << "  [" << i << "] " << paths[i].GetString() << "\n";
  }

  // Step 3: Encode to compressed tree format
  std::cout << "\n3. Encoding to compressed format...\n";
  CompressedPathTree tree = EncodePaths(paths);

  std::cout << "Encoded successfully!\n";
  std::cout << "  - path_indexes: " << tree.path_indexes.size() << " elements\n";
  std::cout << "  - element_token_indexes: " << tree.element_token_indexes.size() << " elements\n";
  std::cout << "  - jumps: " << tree.jumps.size() << " elements\n";
  std::cout << "  - tokens: " << tree.token_table.GetTokens().size() << " unique tokens\n";

  PrintTree(tree);

  // Step 4: Decode back to paths
  std::cout << "\n4. Decoding back to paths...\n";
  std::vector<SimplePath> decoded = DecodePaths(tree);

  std::cout << "Decoded " << decoded.size() << " paths:\n";
  for (size_t i = 0; i < decoded.size(); ++i) {
    std::cout << "  [" << i << "] " << decoded[i].GetString() << "\n";
  }

  // Step 5: Validate round-trip
  std::cout << "\n5. Validating round-trip...\n";
  std::vector<std::string> errors;
  bool valid = ValidateRoundTrip(paths, tree, &errors);

  if (valid) {
    std::cout << "✓ SUCCESS: Round-trip validation passed!\n";
    std::cout << "  All paths encoded and decoded correctly.\n";
  } else {
    std::cout << "✗ FAILURE: Round-trip validation failed!\n";
    for (const auto& err : errors) {
      std::cout << "  - " << err << "\n";
    }
  }

  // Step 6: Show token table
  std::cout << "\n6. Token table contents:\n";
  for (const auto& pair : tree.token_table.GetReverseTokens()) {
    std::string type = (pair.first < 0) ? "property" : "prim";
    std::cout << "  Token " << std::setw(3) << pair.first
              << " (" << std::setw(8) << type << "): "
              << pair.second << "\n";
  }

  std::cout << "\n==================================\n";
  std::cout << "Example completed successfully!\n";
  std::cout << "==================================\n";

  return valid ? 0 : 1;
}
