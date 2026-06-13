// SPDX-License-Identifier: Apache 2.0
// Copyright 2025-Present Light Transport Entertainment, Inc.
//
// tusddiff — USD Layer Diff Tool
//
// Usage:
//   tusddiff file1.usd file2.usd
//   tusddiff --json file1.usd file2.usd
//   tusddiff --help
//
// Exit codes:
//   0 = no differences found
//   1 = differences found
//   2 = error (file not found, parse failure, etc.)
//

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#include "tinyusdz.hh"
#include "layer.hh"
#include "tydra/diff-and-compare.hh"

namespace {

void print_usage() {
  std::cout << "tusddiff — USD Layer Diff Tool\n";
  std::cout << "\n";
  std::cout << "USAGE:\n";
  std::cout << "  tusddiff [OPTIONS] <file1> <file2>\n";
  std::cout << "\n";
  std::cout << "OPTIONS:\n";
  std::cout << "  --json      Output diff in JSON format\n";
  std::cout << "  --quiet     Suppress diff output, exit code only\n";
  std::cout << "  --ulps N    Floating-point tolerance in ULPs (default 1).\n";
  std::cout << "              Absorbs ~1 ULP rounding (e.g. pxr quaternion/xform).\n";
  std::cout << "              Use 0 for bitwise-exact comparison.\n";
  std::cout << "  --eps F     Absolute floating-point tolerance (overrides/ORs ULP).\n";
  std::cout << "  --no-meta   Do not compare metadata (attr/prim/layer).\n";
  std::cout << "  --help      Show this help message\n";
  std::cout << "  -h          Show this help message\n";
  std::cout << "\n";
  std::cout << "EXIT CODES:\n";
  std::cout << "  0  No differences found\n";
  std::cout << "  1  Differences found\n";
  std::cout << "  2  Error (file not found, parse failure, etc.)\n";
  std::cout << "\n";
  std::cout << "EXAMPLES:\n";
  std::cout << "  tusddiff old.usd new.usd\n";
  std::cout << "  tusddiff --json scene1.usda scene2.usda\n";
  std::cout << "  tusddiff --quiet model.usda model.usdc\n";
  std::cout << "\n";
  std::cout << "SUPPORTED FORMATS:\n";
  std::cout << "  .usd, .usda, .usdc, .usdz\n";
}

} // namespace

int main(int argc, char **argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    args.push_back(std::string(argv[i]));
  }

  bool json_output = false;
  bool quiet = false;
  std::string file1, file2;
  tinyusdz::tydra::DiffOptions diff_opts;

  // Parse command line arguments
  for (size_t i = 0; i < args.size(); i++) {
    if (args[i] == "--help" || args[i] == "-h") {
      print_usage();
      return 0;
    } else if (args[i] == "--json") {
      json_output = true;
    } else if (args[i] == "--quiet") {
      quiet = true;
    } else if (args[i] == "--no-meta") {
      diff_opts.compareMetadata = false;
    } else if (args[i] == "--ulps") {
      if (i + 1 >= args.size()) {
        std::cerr << "Error: --ulps requires a value\n";
        return 2;
      }
      try {
        unsigned long u = std::stoul(args[++i]);
        diff_opts.floatUlps = static_cast<uint32_t>(u);
        diff_opts.doubleUlps = static_cast<uint64_t>(u);
      } catch (...) {
        std::cerr << "Error: --ulps value must be a non-negative integer\n";
        return 2;
      }
    } else if (args[i] == "--eps") {
      if (i + 1 >= args.size()) {
        std::cerr << "Error: --eps requires a value\n";
        return 2;
      }
      try {
        diff_opts.absEps = std::stod(args[++i]);
      } catch (...) {
        std::cerr << "Error: --eps value must be a number\n";
        return 2;
      }
    } else if (file1.empty()) {
      file1 = args[i];
    } else if (file2.empty()) {
      file2 = args[i];
    } else {
      std::cerr << "Error: Too many arguments\n";
      print_usage();
      return 2;
    }
  }

  if (file1.empty() || file2.empty()) {
    std::cerr << "Error: Please specify two USD files to compare\n";
    print_usage();
    return 2;
  }

  // Load both USD files as Layers (preserves full PrimSpec tree)
  tinyusdz::Layer layer1, layer2;
  std::string warn, err;

  if (!tinyusdz::LoadLayerFromFile(file1, &layer1, &warn, &err)) {
    std::cerr << "Error loading " << file1 << ": " << err << std::endl;
    return 2;
  }
  if (!warn.empty()) {
    std::cerr << "Warning loading " << file1 << ": " << warn << std::endl;
  }

  warn.clear();
  err.clear();

  if (!tinyusdz::LoadLayerFromFile(file2, &layer2, &warn, &err)) {
    std::cerr << "Error loading " << file2 << ": " << err << std::endl;
    return 2;
  }
  if (!warn.empty()) {
    std::cerr << "Warning loading " << file2 << ": " << warn << std::endl;
  }

  // Perform diff
  tinyusdz::HashMap<std::string, tinyusdz::tydra::PrimSpecDiff> psDiffs;
  tinyusdz::HashMap<std::string, tinyusdz::tydra::PropDiff> propDiffs;
  tinyusdz::tydra::LayerMetaDiff layerMetaDiff;
  tinyusdz::tydra::Diff(layer1, layer2, psDiffs, propDiffs, diff_opts,
                        &layerMetaDiff);

  bool has_diffs =
      !psDiffs.empty() || !propDiffs.empty() || layerMetaDiff.changed();

  if (!quiet) {
    if (json_output) {
      std::string jsonDiff =
          tinyusdz::tydra::DiffToJSON(layer1, layer2, file1, file2, diff_opts);
      std::cout << jsonDiff;
    } else {
      if (has_diffs) {
        std::string textDiff =
            tinyusdz::tydra::DiffToText(layer1, layer2, file1, file2, diff_opts);
        std::cout << textDiff;
      } else {
        std::cout << "No differences found." << std::endl;
      }
    }
  }

  return has_diffs ? 1 : 0;
}
