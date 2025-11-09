//
// Adapter for TinyUSDZ Path class to work with crate encoding library
// SPDX-License-Identifier: Apache 2.0
//
#pragma once

#include "crate/path_interface.hh"

// Forward declare TinyUSDZ path if needed
// #include "path/to/tinyusdz/prim-types.hh"

namespace crate {
namespace adapters {

///
/// Adapter for TinyUSDZ Path class
///
/// Usage:
///   #include "adapters/tinyusdz_adapter.hh"
///   #include "tinyusdz_path.hh"  // Your TinyUSDZ path header
///
///   tinyusdz::Path tiny_path("/World/Geom", "points");
///   TinyUSDZPathAdapter adapter(tiny_path);
///
///   // Now use with crate encoding
///   std::vector<TinyUSDZPathAdapter> paths;
///   ...
///   crate::SortPaths(paths);
///   crate::CompressedPathTree tree = crate::EncodePathsGeneric(paths);
///
class TinyUSDZPathAdapter : public IPath {
public:
  /// Construct from TinyUSDZ Path
  /// Replace with actual TinyUSDZ Path type
  explicit TinyUSDZPathAdapter(const std::string& prim, const std::string& prop = "")
    : prim_part_(prim), prop_part_(prop) {}

  // Implement IPath interface
  std::string GetString() const override {
    if (prop_part_.empty()) {
      return prim_part_;
    }
    return prim_part_ + "." + prop_part_;
  }

  std::string GetPrimPart() const override {
    return prim_part_;
  }

  std::string GetPropertyPart() const override {
    return prop_part_;
  }

  bool IsAbsolute() const override {
    return !prim_part_.empty() && prim_part_[0] == '/';
  }

  bool IsPrimPath() const override {
    return !prim_part_.empty() && prop_part_.empty();
  }

  bool IsPropertyPath() const override {
    return !prim_part_.empty() && !prop_part_.empty();
  }

  IPath* Clone() const override {
    return new TinyUSDZPathAdapter(prim_part_, prop_part_);
  }

private:
  std::string prim_part_;
  std::string prop_part_;
};

} // namespace adapters
} // namespace crate
