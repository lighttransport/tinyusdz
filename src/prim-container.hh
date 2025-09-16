// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 Light Transport Entertainment Inc.
//
// Container utilities and helper functions for prim operations
// This file contains utility functions that operate on prims and paths

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <algorithm>

#include "prim-forward-decl.hh"
#include "path.hh"
#include "nonstd/expected.hpp"

namespace tinyusdz {

///
/// Connection path representation for attribute connections
///
struct ConnectionPath {
  Path path;
  std::string token;  // Optional token for output specification
  
  ConnectionPath() = default;
  ConnectionPath(const Path& p) : path(p) {}
  ConnectionPath(const Path& p, const std::string& t) : path(p), token(t) {}
  
  ///
  /// Get full connection path string
  ///
  std::string GetFullPath() const {
    if (token.empty()) {
      return path.full_path_name();
    }
    return path.full_path_name() + "." + token;
  }
  
  bool operator==(const ConnectionPath& rhs) const {
    return path == rhs.path && token == rhs.token;
  }
  
  bool operator!=(const ConnectionPath& rhs) const {
    return !(*this == rhs);
  }
};

///
/// Typed connection for strongly-typed attribute connections
///
template<typename T>
class TypedConnection {
 public:
  TypedConnection() = default;
  TypedConnection(const ConnectionPath& path) : _path(path) {}
  
  const ConnectionPath& GetPath() const { return _path; }
  void SetPath(const ConnectionPath& path) { _path = path; }
  
  bool IsConnected() const { return !_path.path.is_empty(); }
  void Clear() { _path = ConnectionPath(); }
  
 private:
  ConnectionPath _path;
};

///
/// Prim traversal utilities
///

///
/// Visit all prims in a hierarchy with a visitor function
///
template<typename Visitor>
void TraversePrims(const Prim& root, Visitor&& visitor) {
  visitor(root);
  for (const auto& child : root.children()) {
    TraversePrims(child, std::forward<Visitor>(visitor));
  }
}

///
/// Visit all prims with early termination support
/// Visitor should return true to continue, false to stop
///
template<typename Visitor>
bool TraversePrimsWithEarlyTermination(const Prim& root, Visitor&& visitor) {
  if (!visitor(root)) {
    return false;
  }
  for (const auto& child : root.children()) {
    if (!TraversePrimsWithEarlyTermination(child, std::forward<Visitor>(visitor))) {
      return false;
    }
  }
  return true;
}

///
/// Find a prim by path in a hierarchy
///
const Prim* FindPrimByPath(const Prim& root, const Path& path);
Prim* FindPrimByPath(Prim& root, const Path& path);

///
/// Find all prims matching a predicate
///
template<typename Predicate>
std::vector<const Prim*> FindPrims(const Prim& root, Predicate&& pred) {
  std::vector<const Prim*> result;
  TraversePrims(root, [&](const Prim& prim) {
    if (pred(prim)) {
      result.push_back(&prim);
    }
  });
  return result;
}

///
/// Find all prims of a specific type
///
template<typename T>
std::vector<const T*> FindPrimsOfType(const Prim& root) {
  std::vector<const T*> result;
  TraversePrims(root, [&](const Prim& prim) {
    if (const T* typed = prim.as<T>()) {
      result.push_back(typed);
    }
  });
  return result;
}

///
/// Path utilities
///

///
/// Validate a prim element name
/// Returns false if the name contains invalid characters
///
bool ValidatePrimElementName(const std::string& name);

///
/// Validate a full prim path
///
bool ValidatePrimPath(const Path& path);

///
/// Get the parent path of a given path
///
Path GetParentPath(const Path& path);

///
/// Get the element name from a path (last component)
///
std::string GetElementName(const Path& path);

///
/// Check if one path is an ancestor of another
///
bool IsAncestor(const Path& ancestor, const Path& descendant);

///
/// Check if one path is a descendant of another
///
bool IsDescendant(const Path& descendant, const Path& ancestor);

///
/// Compute relative path from one path to another
///
nonstd::expected<Path, std::string> ComputeRelativePath(
    const Path& from, const Path& to);

///
/// Prim name utilities
///

///
/// Generate a unique name for a child prim
/// Uses Maya-like naming convention: plane -> plane1 -> plane2
///
std::string GenerateUniquePrimName(const Prim& parent, 
                                   const std::string& baseName);

///
/// Check if a prim name exists among children
///
bool HasChildNamed(const Prim& parent, const std::string& name);

///
/// Get all child prim names
///
std::vector<std::string> GetChildNames(const Prim& prim);

///
/// Property utilities
///

///
/// Get all property names from a prim
///
std::vector<std::string> GetPropertyNames(const Prim& prim);

///
/// Get all attribute names from a prim
///
std::vector<std::string> GetAttributeNames(const Prim& prim);

///
/// Get all relationship names from a prim
///
std::vector<std::string> GetRelationshipNames(const Prim& prim);

///
/// Check if a property exists on a prim
///
bool HasProperty(const Prim& prim, const std::string& name);

///
/// Check if an attribute exists on a prim
///
bool HasAttribute(const Prim& prim, const std::string& name);

///
/// Check if a relationship exists on a prim
///
bool HasRelationship(const Prim& prim, const std::string& name);

///
/// Hierarchy utilities
///

///
/// Get the depth of a prim in the hierarchy (root = 0)
///
size_t GetPrimDepth(const Prim& prim, const Prim& root);

///
/// Get all ancestors of a prim up to the root
///
std::vector<const Prim*> GetAncestors(const Prim& prim, const Prim& root);

///
/// Get all descendants of a prim
///
std::vector<const Prim*> GetDescendants(const Prim& prim);

///
/// Count total number of prims in a hierarchy
///
size_t CountPrims(const Prim& root);

///
/// Count prims matching a predicate
///
template<typename Predicate>
size_t CountPrimsIf(const Prim& root, Predicate&& pred) {
  size_t count = 0;
  TraversePrims(root, [&](const Prim& prim) {
    if (pred(prim)) {
      ++count;
    }
  });
  return count;
}

///
/// Prim filtering utilities
///

///
/// Filter prims by visibility
///
std::vector<const Prim*> FilterByVisibility(
    const std::vector<const Prim*>& prims,
    Visibility visibility);

///
/// Filter prims by purpose
///
std::vector<const Prim*> FilterByPurpose(
    const std::vector<const Prim*>& prims,
    Purpose purpose);

///
/// Filter prims by kind
///
std::vector<const Prim*> FilterByKind(
    const std::vector<const Prim*>& prims,
    Kind kind);

///
/// Filter active prims only
///
std::vector<const Prim*> FilterActive(
    const std::vector<const Prim*>& prims);

///
/// Filter instanceable prims
///
std::vector<const Prim*> FilterInstanceable(
    const std::vector<const Prim*>& prims);

} // namespace tinyusdz