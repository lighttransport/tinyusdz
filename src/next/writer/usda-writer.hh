// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDA Writer
// Export Stage/Layer to USDA ASCII format

#pragma once

#include "../stage/stage.hh"
#include <string>
#include <ostream>

namespace tinyusdz {
namespace next {

/// Options for USDA output
struct USDAWriteOptions {
  /// Indent string (default 4 spaces)
  std::string indent = "    ";

  /// Float precision (digits after decimal)
  int float_precision = 6;

  /// Double precision (digits after decimal)
  int double_precision = 15;

  /// Maximum array elements per line (0 = unlimited)
  size_t max_elements_per_line = 16;

  /// Write compact format (less whitespace)
  bool compact = false;

  /// Include comments (doc metadata)
  bool include_comments = true;

  /// Sort properties alphabetically
  bool sort_properties = false;

  /// Export time samples as separate lines
  bool expand_time_samples = true;
};

/// Result of write operation
struct USDAWriteResult {
  bool success = false;
  std::string error;
  size_t bytes_written = 0;
};

/// Write Stage to USDA string
std::string WriteUSDAToString(const Stage& stage,
                               const USDAWriteOptions& options = {});

/// Write Stage to stream
USDAWriteResult WriteUSDA(std::ostream& os, const Stage& stage,
                           const USDAWriteOptions& options = {});

/// Write Stage to file
USDAWriteResult WriteUSDAToFile(const std::string& filename, const Stage& stage,
                                 const USDAWriteOptions& options = {});
USDAWriteResult WriteUSDAToFile(const char* filename, const Stage& stage,
                                 const USDAWriteOptions& options = {});

/// Write Layer to USDA string (for individual layers)
std::string WriteLayerToString(const Layer& layer,
                                const USDAWriteOptions& options = {});

/// Write Layer to stream
USDAWriteResult WriteLayer(std::ostream& os, const Layer& layer,
                            const USDAWriteOptions& options = {});

/// Write Layer to file
USDAWriteResult WriteLayerToFile(const std::string& filename, const Layer& layer,
                                  const USDAWriteOptions& options = {});

}  // namespace next
}  // namespace tinyusdz
