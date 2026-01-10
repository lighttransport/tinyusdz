//
// Crate format PATHS tree encoding (v0.4.0+ compressed format)
// SPDX-License-Identifier: Apache 2.0
//
#pragma once

#include "simple-path.hh"
#include <vector>
#include <string>
#include <cstdint>
#include <map>
#include <memory>

namespace tinyusdz {
namespace crate {

///
/// Token index type
/// In real implementation, this would map to the token table
///
using TokenIndex = int32_t;

///
/// Path index into the original paths vector
///
using PathIndex = uint64_t;

///
/// Tree node representing a path element in the hierarchy
///
struct PathTreeNode {
  std::string element_name;        // Name of this element (e.g., "foo", "bar", "points")
  TokenIndex element_token_index;  // Token index for element name (negative for properties)
  PathIndex path_index;            // Index into original paths vector
  bool is_property;                // Is this a property element?

  PathTreeNode* parent = nullptr;
  PathTreeNode* first_child = nullptr;
  PathTreeNode* next_sibling = nullptr;

  PathTreeNode(const std::string& name, TokenIndex token_idx, PathIndex path_idx, bool is_prop)
    : element_name(name), element_token_index(token_idx), path_index(path_idx), is_property(is_prop) {}
};

///
/// Token table for mapping strings to token indices
///
class TokenTable {
public:
  TokenTable() : next_index_(0) {}

  /// Get or create token index for a string
  /// Properties use negative indices
  TokenIndex GetOrCreateToken(const std::string& str, bool is_property);

  /// Get token string from index
  std::string GetToken(TokenIndex index) const;

  /// Get all tokens
  const std::map<std::string, TokenIndex>& GetTokens() const { return tokens_; }

  /// Get reverse mapping
  const std::map<TokenIndex, std::string>& GetReverseTokens() const { return reverse_tokens_; }

private:
  std::map<std::string, TokenIndex> tokens_;
  std::map<TokenIndex, std::string> reverse_tokens_;
  TokenIndex next_index_;
};

///
/// Compressed path tree encoding result
///
struct CompressedPathTree {
  std::vector<PathIndex> path_indexes;           // Index into _paths vector
  std::vector<TokenIndex> element_token_indexes; // Token for element (negative = property)
  std::vector<int32_t> jumps;                    // Navigation: -2=leaf, -1=child, 0=sibling, >0=both

  TokenTable token_table;                        // Token table used for encoding

  size_t size() const { return path_indexes.size(); }
  bool empty() const { return path_indexes.empty(); }
};

///
/// Build a hierarchical tree from sorted paths
///
/// Example:
///   ["/", "/World", "/World/Geom", "/World/Geom.points"]
///
/// Becomes tree:
///   / (root)
///     └─ World
///          └─ Geom
///               └─ .points (property)
///
std::unique_ptr<PathTreeNode> BuildPathTree(
  const std::vector<SimplePath>& sorted_paths,
  TokenTable& token_table
);

///
/// Encode path tree into compressed format (three parallel arrays)
///
/// Walks the tree in depth-first order and generates:
/// - pathIndexes[i]: index into original paths vector
/// - elementTokenIndexes[i]: token index for this element
/// - jumps[i]: navigation information
///
CompressedPathTree EncodePathTree(const std::vector<SimplePath>& sorted_paths);

///
/// Decode compressed path tree back to paths
///
/// Reconstructs paths from the three arrays by following jump instructions
///
std::vector<SimplePath> DecodePathTree(const CompressedPathTree& compressed);

///
/// Internal: Walk tree in depth-first order and populate arrays
///
void WalkTreeDepthFirst(
  PathTreeNode* node,
  std::vector<PathIndex>& path_indexes,
  std::vector<TokenIndex>& element_token_indexes,
  std::vector<int32_t>& jumps,
  std::vector<size_t>& sibling_offsets  // Positions that need sibling offset filled in
);

///
/// Internal: Calculate jump value for a node
///
int32_t CalculateJump(
  const PathTreeNode* node,
  bool has_child,
  bool has_sibling,
  size_t sibling_offset
);

} // namespace crate
} // namespace tinyusdz
