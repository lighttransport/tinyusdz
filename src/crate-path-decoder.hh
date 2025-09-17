// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Path decompression and node hierarchy building for Crate reader
#pragma once

#include <vector>
#include <string>
#include <unordered_set>
#include "crate-format.hh"
#include "prim-types.hh"
#include "nonstd/optional.hpp"
#include "memory-budget.hh"

namespace tinyusdz {
namespace crate {

// Forward declaration
class CrateReader;

// Node class for scene graph hierarchy
class Node {
 public:
  Node() : _parent(-2) {}
  Node(int64_t parent, Path& path) : _parent(parent), _path(path) {}

  int64_t GetParent() const { return _parent; }
  const std::vector<size_t>& GetChildren() const { return _children; }
  
  bool AddChildren(const std::string& child_name, size_t node_index) {
    if (_primChildren.count(child_name)) {
      return false;
    }
    _primChildren.emplace(child_name);
    _children.push_back(node_index);
    return true;
  }

  std::string GetLocalPath() const { return _path.full_path_name(); }
  void SetElementPath(Path& path) { _elemPath = path; }
  
  nonstd::optional<std::string> GetElementName() const {
    if (_elemPath.is_relative_path()) {
      return _elemPath.full_path_name();
    } else {
      return nonstd::nullopt;
    }
  }

  const Path& GetElementPath() const { return _elemPath; }
  const Path& GetPath() const { return _path; }
  const std::unordered_set<std::string>& GetPrimChildren() const {
    return _primChildren;
  }

 private:
  int64_t _parent;  // -1 = root node, -2 = invalid or leaf node
  std::vector<size_t> _children;
  std::unordered_set<std::string> _primChildren;
  Path _path;
  Path _elemPath;
};

class CratePathDecoder {
 public:
  CratePathDecoder(CrateReader* reader, MemoryBudgetManager& memory_manager)
      : _reader(reader), memory_manager_(memory_manager) {}

  // Path decompression
  bool ReadCompressedPaths(const uint64_t maxNumPaths);
  
  // Build decompressed paths (two implementations for different Crate versions)
  bool BuildDecompressedPathsImpl(
      const std::vector<uint32_t>& pathIndexes,
      const std::vector<int32_t>& elementTokenIndexes,
      const std::vector<int32_t>& jumps,
      size_t parentIndex,
      bool isRecursive);
  
  // Alternative implementation for newer format
  bool BuildDecompressedPathsImplIterative(
      const std::vector<uint32_t>& pathIndexes,
      const std::vector<int32_t>& elementTokenIndexes,
      const std::vector<int32_t>& jumps);

  // Build node hierarchy
  bool BuildNodeHierarchy(
      const std::vector<uint32_t>& pathIndexes,
      const std::vector<int32_t>& elementTokenIndexes,
      const std::vector<int32_t>& jumps,
      uint32_t pathIndex,
      int64_t parentNodeIndex,
      int depth);
  
  // Alternative implementation for node hierarchy
  bool BuildNodeHierarchyIterative(
      const std::vector<uint32_t>& pathIndexes,
      const std::vector<int32_t>& elementTokenIndexes,
      const std::vector<int32_t>& jumps);

  // Getters
  const std::vector<Path>& GetPaths() const { return _paths; }
  const std::vector<Node>& GetNodes() const { return _nodes; }
  
  // Error handling
  void PushError(const std::string& msg) { _err += msg + "\n"; }
  std::string GetError() const { return _err; }

 private:
  CrateReader* _reader;
  MemoryBudgetManager& memory_manager_;
  std::vector<Path> _paths;
  std::vector<Node> _nodes;
  std::string _err;
};

}  // namespace crate
}  // namespace tinyusdz