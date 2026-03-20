// SPDX-License-Identifier: Apache 2.0
// Copyright 2025-Present Light Transport Entertainment, Inc.
//
// USD Layer Diff Tool
//
// Usage:
//   usddiff file1.usd file2.usd
//   usddiff --json file1.usd file2.usd
//   usddiff --help
//

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#include "tinyusdz.hh"
#include "layer.hh"
#include "core/prim.hh"
#include "core/prim-spec.hh"
#include "tydra/diff-and-compare.hh"
#include "io-util.hh"

namespace {

void print_usage() {
  std::cout << "USD Layer Diff Tool\n";
  std::cout << "\n";
  std::cout << "USAGE:\n";
  std::cout << "  usddiff [OPTIONS] <file1> <file2>\n";
  std::cout << "\n";
  std::cout << "OPTIONS:\n";
  std::cout << "  --json      Output diff in JSON format\n";
  std::cout << "  --help      Show this help message\n";
  std::cout << "  -h          Show this help message\n";
  std::cout << "\n";
  std::cout << "EXAMPLES:\n";
  std::cout << "  usddiff old.usd new.usd\n";
  std::cout << "  usddiff --json scene1.usda scene2.usda\n";
  std::cout << "  usddiff model_v1.usdc model_v2.usdc\n";
  std::cout << "\n";
  std::cout << "SUPPORTED FORMATS:\n";
  std::cout << "  .usd, .usda, .usdc, .usdz\n";
}

bool load_usd_file(const std::string &filename, tinyusdz::Layer *layer, std::string *error) {
  if (!layer) {
    if (error) *error = "Invalid layer pointer";
    return false;
  }

  // Check if file exists
  if (!tinyusdz::io::FileExists(filename)) {
    if (error) *error = "File does not exist: " + filename;
    return false;
  }

  // Try to load as USD
  tinyusdz::Stage stage;
  std::string warn, err;
  
  bool ret = tinyusdz::LoadUSDFromFile(filename, &stage, &warn, &err);
  if (!ret) {
    if (error) *error = "Failed to load USD file '" + filename + "': " + err;
    return false;
  }

  if (!warn.empty()) {
    std::cerr << "Warning loading " << filename << ": " << warn << std::endl;
  }

  // Convert Stage to Layer for diffing
  // For now, we'll create a simple layer from the stage's root prims
  layer->set_name(filename);
  
  // Add root prims to layer
  for (const auto &rootPrim : stage.root_prims()) {
    tinyusdz::PrimSpec primSpec(tinyusdz::Specifier::Def, rootPrim.element_name());
    
    // Convert Prim to PrimSpec (simplified)
    // TODO: This could be enhanced to preserve more Prim information
    layer->add_primspec(rootPrim.element_name(), primSpec);
  }
  
  return true;
}

} // namespace

int main(int argc, char **argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    args.push_back(std::string(argv[i]));
  }

  bool json_output = false;
  std::string file1, file2;

  // Parse command line arguments
  for (size_t i = 0; i < args.size(); i++) {
    if (args[i] == "--help" || args[i] == "-h") {
      print_usage();
      return 0;
    } else if (args[i] == "--json") {
      json_output = true;
    } else if (file1.empty()) {
      file1 = args[i];
    } else if (file2.empty()) {
      file2 = args[i];
    } else {
      std::cerr << "Error: Too many arguments\n";
      print_usage();
      return 1;
    }
  }

  if (file1.empty() || file2.empty()) {
    std::cerr << "Error: Please specify two USD files to compare\n";
    print_usage();
    return 1;
  }

  // Load both USD files
  tinyusdz::Layer layer1, layer2;
  std::string error;

  if (!load_usd_file(file1, &layer1, &error)) {
    std::cerr << "Error loading " << file1 << ": " << error << std::endl;
    return 1;
  }

  if (!load_usd_file(file2, &layer2, &error)) {
    std::cerr << "Error loading " << file2 << ": " << error << std::endl;
    return 1;
  }

  // Perform diff
  try {
    if (json_output) {
      std::string jsonDiff = tinyusdz::tydra::DiffToJSON(layer1, layer2, file1, file2);
      std::cout << jsonDiff;
    } else {
      std::string textDiff = tinyusdz::tydra::DiffToText(layer1, layer2, file1, file2);
      std::cout << textDiff;
    }
  } catch (const std::exception &e) {
    std::cerr << "Error computing diff: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
