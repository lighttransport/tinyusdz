// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Batch USDA to USDC Conversion Tool
//
// This tool converts multiple USDA files to USDC format using the crate-writer.
// It's designed to test the crate-writer against a large corpus of USD files
// to validate correctness across various USD features.
//

#include "crate-writer.hh"
#include "tinyusdz.hh"
#include "usda-reader.hh"
#include "stage.hh"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <chrono>

using namespace tinyusdz;
using namespace tinyusdz::experimental;

namespace fs = std::filesystem;
namespace tcrate = tinyusdz::crate;

// Conversion statistics
struct ConversionStats {
  size_t total_files = 0;
  size_t successful = 0;
  size_t failed_read = 0;
  size_t failed_write = 0;
  std::vector<std::string> errors;

  void print() const {
    std::cout << "\n=== Conversion Statistics ===\n";
    std::cout << "Total files:     " << total_files << "\n";
    std::cout << "Successful:      " << successful << "\n";
    std::cout << "Failed to read:  " << failed_read << "\n";
    std::cout << "Failed to write: " << failed_write << "\n";
    std::cout << "Success rate:    "
              << (total_files > 0 ? (100.0 * successful / total_files) : 0.0)
              << "%\n";

    if (!errors.empty()) {
      std::cout << "\n=== Errors (showing first 20) ===\n";
      size_t count = 0;
      for (const auto& err : errors) {
        if (count++ >= 20) break;
        std::cout << err << "\n";
      }
    }
  }
};

// Convert a single USDA file to USDC
bool ConvertFile(const fs::path& input_path, const fs::path& output_path,
                 std::string* err_out, bool verbose = false) {
  if (verbose) {
    std::cout << "Converting: " << input_path.filename().string() << " ... ";
    std::cout.flush();
  }

  // Load USDA file
  tinyusdz::Stage stage;
  std::string warn, err;

  bool ret = tinyusdz::LoadUSDFromFile(input_path.string(), &stage, &warn, &err);

  if (!ret) {
    if (err_out) {
      *err_out = "Failed to load USDA: " + err;
    }
    if (verbose) std::cout << "FAILED (read)\n";
    return false;
  }

  if (!warn.empty() && verbose) {
    std::cerr << "  Warning: " << warn << "\n";
  }

  // Create writer
  CrateWriter writer(output_path.string());

  // Configure for USD v0.8.0
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;
  opts.enable_deduplication = true;
  writer.SetOptions(opts);

  // Open file
  if (!writer.Open(&err)) {
    if (err_out) {
      *err_out = "Failed to open output: " + err;
    }
    if (verbose) std::cout << "FAILED (open)\n";
    return false;
  }

  // Convert stage to specs
  if (!writer.ConvertStageToSpecs(stage, &err)) {
    if (err_out) {
      *err_out = "Failed to convert stage: " + err;
    }
    if (verbose) std::cout << "FAILED (convert)\n";
    writer.Close();
    return false;
  }

  // Finalize and write
  if (!writer.Finalize(&err)) {
    if (err_out) {
      *err_out = "Failed to finalize: " + err;
    }
    if (verbose) std::cout << "FAILED (finalize)\n";
    writer.Close();
    return false;
  }

  writer.Close();

  if (verbose) std::cout << "OK\n";
  return true;
}

// Recursively find all .usda files
std::vector<fs::path> FindUsdaFiles(const fs::path& root_dir, bool skip_fail_case = true) {
  std::vector<fs::path> usda_files;

  try {
    for (const auto& entry : fs::recursive_directory_iterator(root_dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".usda") {
        // Skip fail-case directory if requested
        if (skip_fail_case && entry.path().string().find("/fail-case/") != std::string::npos) {
          continue;
        }
        usda_files.push_back(entry.path());
      }
    }
  } catch (const fs::filesystem_error& e) {
    std::cerr << "Error scanning directory: " << e.what() << "\n";
  }

  // Sort for consistent ordering
  std::sort(usda_files.begin(), usda_files.end());

  return usda_files;
}

void PrintUsage(const char* prog_name) {
  std::cout << "Usage: " << prog_name << " [options] <input_dir> <output_dir>\n";
  std::cout << "\nOptions:\n";
  std::cout << "  -v, --verbose       Verbose output (show each conversion)\n";
  std::cout << "  -i, --include-fail  Include fail-case test files\n";
  std::cout << "  -l, --limit N       Limit to first N files\n";
  std::cout << "  -h, --help          Show this help\n";
  std::cout << "\nExample:\n";
  std::cout << "  " << prog_name << " ../../tests/usda ./output\n";
}

int main(int argc, char** argv) {
  // Parse command line
  bool verbose = false;
  bool include_fail = false;
  int limit = -1;
  std::vector<std::string> positional_args;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-v" || arg == "--verbose") {
      verbose = true;
    } else if (arg == "-i" || arg == "--include-fail") {
      include_fail = true;
    } else if (arg == "-l" || arg == "--limit") {
      if (i + 1 < argc) {
        limit = std::atoi(argv[++i]);
      } else {
        std::cerr << "Error: --limit requires a number\n";
        return 1;
      }
    } else if (arg == "-h" || arg == "--help") {
      PrintUsage(argv[0]);
      return 0;
    } else if (arg[0] == '-') {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      PrintUsage(argv[0]);
      return 1;
    } else {
      positional_args.push_back(arg);
    }
  }

  if (positional_args.size() != 2) {
    std::cerr << "Error: Expected 2 arguments (input_dir and output_dir)\n";
    PrintUsage(argv[0]);
    return 1;
  }

  fs::path input_dir = positional_args[0];
  fs::path output_dir = positional_args[1];

  // Validate input directory
  if (!fs::exists(input_dir) || !fs::is_directory(input_dir)) {
    std::cerr << "Error: Input directory does not exist: " << input_dir << "\n";
    return 1;
  }

  // Create output directory if needed
  if (!fs::exists(output_dir)) {
    fs::create_directories(output_dir);
  }

  std::cout << "Batch USDA to USDC Conversion\n";
  std::cout << "==============================\n";
  std::cout << "Input:  " << fs::absolute(input_dir) << "\n";
  std::cout << "Output: " << fs::absolute(output_dir) << "\n";
  std::cout << "Skip fail-case: " << (!include_fail ? "yes" : "no") << "\n";
  if (limit > 0) {
    std::cout << "Limit:  " << limit << " files\n";
  }
  std::cout << "\n";

  // Find all USDA files
  std::cout << "Scanning for USDA files...\n";
  auto usda_files = FindUsdaFiles(input_dir, !include_fail);

  if (limit > 0 && static_cast<size_t>(limit) < usda_files.size()) {
    usda_files.resize(limit);
  }

  std::cout << "Found " << usda_files.size() << " USDA files\n\n";

  if (usda_files.empty()) {
    std::cout << "No files to convert.\n";
    return 0;
  }

  // Convert all files
  ConversionStats stats;
  stats.total_files = usda_files.size();

  auto start_time = std::chrono::steady_clock::now();

  for (const auto& input_path : usda_files) {
    // Generate output path (maintain relative structure)
    auto relative_path = fs::relative(input_path, input_dir);
    auto output_path = output_dir / relative_path;
    output_path.replace_extension(".usdc");

    // Create output subdirectories if needed
    fs::create_directories(output_path.parent_path());

    // Convert
    std::string err;
    bool success = ConvertFile(input_path, output_path, &err, verbose);

    if (success) {
      stats.successful++;
    } else {
      if (err.find("Failed to load") != std::string::npos) {
        stats.failed_read++;
      } else {
        stats.failed_write++;
      }

      std::string error_msg = input_path.filename().string() + ": " + err;
      stats.errors.push_back(error_msg);

      if (!verbose) {
        std::cerr << "FAILED: " << error_msg << "\n";
      }
    }
  }

  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

  // Print statistics
  stats.print();

  std::cout << "\nTime elapsed: " << (duration.count() / 1000.0) << " seconds\n";
  std::cout << "Average:      "
            << (stats.total_files > 0 ? (duration.count() / static_cast<double>(stats.total_files)) : 0.0)
            << " ms/file\n";

  return (stats.successful == stats.total_files) ? 0 : 1;
}
