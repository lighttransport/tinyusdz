//
// Crate format PATHS tree encoding implementation
// SPDX-License-Identifier: Apache 2.0
//
#include "tree-encode.hh"
#include <algorithm>
#include <functional>
#include <sstream>
#include <stdexcept>

namespace tinyusdz {
namespace crate {

// ============================================================================
// TokenTable Implementation
// ============================================================================

TokenIndex TokenTable::GetOrCreateToken(const std::string& str, bool is_property) {
  auto it = tokens_.find(str);
  if (it != tokens_.end()) {
    return it->second;
  }

  TokenIndex index = next_index_++;

  // Properties use negative indices (as per OpenUSD convention)
  if (is_property) {
    index = -index - 1;  // -1, -2, -3, ...
  }

  tokens_[str] = index;
  reverse_tokens_[index] = str;

  return index;
}

std::string TokenTable::GetToken(TokenIndex index) const {
  auto it = reverse_tokens_.find(index);
  if (it == reverse_tokens_.end()) {
    return "";
  }
  return it->second;
}

// ============================================================================
// Tree Building
// ============================================================================

std::unique_ptr<PathTreeNode> BuildPathTree(
  const std::vector<SimplePath>& sorted_paths,
  TokenTable& token_table
) {
  if (sorted_paths.empty()) {
    return nullptr;
  }

  // Create root node (represents the root "/" path)
  // Note: In Crate format, root is implicit and starts with empty element
  auto root = std::make_unique<PathTreeNode>("", 0, 0, false);
  root->path_index = 0;  // Root path is always at index 0 if it exists

  // Map from path string to node (for quick lookup)
  std::map<std::string, PathTreeNode*> path_to_node;
  path_to_node["/"] = root.get();

  for (size_t path_idx = 0; path_idx < sorted_paths.size(); ++path_idx) {
    const SimplePath& path = sorted_paths[path_idx];

    // Parse prim part
    std::string prim_part = path.prim_part();
    std::string prop_part = path.prop_part();

    // Skip root path - it's already represented by root node
    if (prim_part == "/" && prop_part.empty()) {
      continue;
    }

    // Handle root with property (e.g., "/.prop")
    if (prim_part == "/" && !prop_part.empty()) {
      TokenIndex token_idx = token_table.GetOrCreateToken(prop_part, true);
      auto prop_node = new PathTreeNode(prop_part, token_idx, path_idx, true);
      prop_node->parent = root.get();

      if (root->first_child == nullptr) {
        root->first_child = prop_node;
      } else {
        PathTreeNode* sibling = root->first_child;
        while (sibling->next_sibling != nullptr) {
          sibling = sibling->next_sibling;
        }
        sibling->next_sibling = prop_node;
      }
      continue;
    }

    // Split prim part into elements
    std::vector<std::string> elements;
    std::string current_path;

    if (!prim_part.empty() && prim_part[0] == '/') {
      current_path = "/";
      size_t start = 1;

      while (start < prim_part.size()) {
        size_t end = prim_part.find('/', start);
        if (end == std::string::npos) {
          end = prim_part.size();
        }

        std::string element = prim_part.substr(start, end - start);
        if (!element.empty()) {
          elements.push_back(element);
        }

        start = end + 1;
      }
    }

    // Build prim hierarchy
    PathTreeNode* parent_node = root.get();
    current_path = "";

    for (size_t i = 0; i < elements.size(); ++i) {
      const std::string& element = elements[i];
      current_path = current_path.empty() ? "/" + element : current_path + "/" + element;

      // Check if node already exists
      auto it = path_to_node.find(current_path);
      if (it != path_to_node.end()) {
        parent_node = it->second;
        continue;
      }

      // Create new node
      TokenIndex token_idx = token_table.GetOrCreateToken(element, false);
      PathIndex node_path_idx = (i == elements.size() - 1 && prop_part.empty()) ? path_idx : 0;

      auto new_node = new PathTreeNode(element, token_idx, node_path_idx, false);
      new_node->parent = parent_node;

      // Add as child to parent
      if (parent_node->first_child == nullptr) {
        parent_node->first_child = new_node;
      } else {
        // Find last sibling and append
        PathTreeNode* sibling = parent_node->first_child;
        while (sibling->next_sibling != nullptr) {
          sibling = sibling->next_sibling;
        }
        sibling->next_sibling = new_node;
      }

      path_to_node[current_path] = new_node;
      parent_node = new_node;
    }

    // Add property if present
    if (!prop_part.empty()) {
      TokenIndex token_idx = token_table.GetOrCreateToken(prop_part, true);
      auto prop_node = new PathTreeNode(prop_part, token_idx, path_idx, true);
      prop_node->parent = parent_node;

      if (parent_node->first_child == nullptr) {
        parent_node->first_child = prop_node;
      } else {
        PathTreeNode* sibling = parent_node->first_child;
        while (sibling->next_sibling != nullptr) {
          sibling = sibling->next_sibling;
        }
        sibling->next_sibling = prop_node;
      }
    }
  }

  return root;
}

// ============================================================================
// Tree Walking and Encoding
// ============================================================================

int32_t CalculateJump(
  const PathTreeNode* node,
  bool has_child,
  bool has_sibling,
  size_t sibling_offset
) {
  if (!has_child && !has_sibling) {
    return -2;  // Leaf node
  }

  if (has_child && !has_sibling) {
    return -1;  // Only child follows
  }

  if (!has_child && has_sibling) {
    return 0;   // Only sibling follows
  }

  // Both child and sibling exist
  // Return offset to sibling (positive value)
  return static_cast<int32_t>(sibling_offset);
}

void WalkTreeDepthFirst(
  PathTreeNode* node,
  std::vector<PathIndex>& path_indexes,
  std::vector<TokenIndex>& element_token_indexes,
  std::vector<int32_t>& jumps,
  std::vector<size_t>& sibling_offsets,
  bool include_node = true  // Whether to include this node in output
) {
  if (node == nullptr) {
    return;
  }

  size_t current_pos = 0;
  bool has_child = (node->first_child != nullptr);
  bool has_sibling = (node->next_sibling != nullptr);

  if (include_node) {
    // Record current position
    current_pos = path_indexes.size();

    // Add this node
    path_indexes.push_back(node->path_index);
    element_token_indexes.push_back(node->element_token_index);

    // Placeholder for jump (will be filled in later if needed)
    jumps.push_back(0);

    // If we have both child and sibling, we need to track sibling offset
    if (has_child && has_sibling) {
      sibling_offsets.push_back(current_pos);  // Mark for later update
    }
  }

  // Process child first (depth-first)
  size_t sibling_pos = 0;
  if (has_child) {
    WalkTreeDepthFirst(node->first_child, path_indexes, element_token_indexes, jumps, sibling_offsets, true);

    // If we also have a sibling, record where it will be
    if (has_sibling && include_node) {
      sibling_pos = path_indexes.size();
    }
  }

  if (include_node) {
    // Calculate and set jump value
    size_t offset_to_sibling = has_sibling ? (sibling_pos - current_pos) : 0;
    jumps[current_pos] = CalculateJump(node, has_child, has_sibling, offset_to_sibling);
  }

  // Process sibling
  if (has_sibling) {
    WalkTreeDepthFirst(node->next_sibling, path_indexes, element_token_indexes, jumps, sibling_offsets, true);
  }
}

CompressedPathTree EncodePathTree(const std::vector<SimplePath>& sorted_paths) {
  CompressedPathTree result;

  if (sorted_paths.empty()) {
    return result;
  }

  // Build tree structure
  auto root = BuildPathTree(sorted_paths, result.token_table);

  if (!root) {
    return result;
  }

  // Walk tree and generate arrays
  std::vector<size_t> sibling_offsets;

  // Start from root's children (root itself is implicit in the structure)
  // But we need to add root as the first node
  result.path_indexes.push_back(root->path_index);
  result.element_token_indexes.push_back(root->element_token_index);
  result.jumps.push_back(-1);  // Root always has children (or is a leaf if no children)

  if (root->first_child) {
    // Process children
    WalkTreeDepthFirst(root->first_child, result.path_indexes, result.element_token_indexes,
                       result.jumps, sibling_offsets, true);

    // Update root's jump value
    if (!root->first_child->next_sibling) {
      result.jumps[0] = -1;  // Only child
    } else {
      result.jumps[0] = -1;  // Child follows (siblings are also children of root)
    }
  } else {
    // No children - root is a leaf
    result.jumps[0] = -2;
  }

  // Clean up tree (delete nodes)
  std::function<void(PathTreeNode*)> delete_tree = [&](PathTreeNode* node) {
    if (!node) return;

    // Delete children
    PathTreeNode* child = node->first_child;
    while (child) {
      PathTreeNode* next = child->next_sibling;
      delete_tree(child);
      delete child;
      child = next;
    }
  };

  delete_tree(root.get());

  return result;
}

// ============================================================================
// Tree Decoding
// ============================================================================

std::vector<SimplePath> DecodePathTree(const CompressedPathTree& compressed) {
  if (compressed.empty()) {
    return {};
  }

  // Create a map from path_index to reconstructed path
  std::map<PathIndex, SimplePath> path_map;

  // Recursive decoder
  std::function<void(size_t, std::string)> decode_recursive;
  decode_recursive = [&](size_t idx, std::string current_prim) {
    if (idx >= compressed.size()) {
      return;
    }

    PathIndex path_idx = compressed.path_indexes[idx];
    TokenIndex token_idx = compressed.element_token_indexes[idx];
    int32_t jump = compressed.jumps[idx];

    // Get element name
    std::string element = compressed.token_table.GetToken(token_idx);
    bool is_property = (token_idx < 0);

    // Build current path
    std::string prim_part = current_prim;
    std::string prop_part;

    if (is_property) {
      // Property path - prim_part stays the same, prop_part is the element
      prop_part = element;
    } else {
      // Prim path - build new prim path
      if (element.empty()) {
        // Root node
        prim_part = "/";
      } else if (current_prim == "/") {
        prim_part = "/" + element;
      } else if (current_prim.empty()) {
        prim_part = "/" + element;
      } else {
        prim_part = current_prim + "/" + element;
      }
    }

    // Store path if this node represents an actual path (not just a tree structure node)
    // Nodes with path_index > 0 or the root (path_idx==0 and element.empty()) are actual paths
    if (path_idx > 0 || (path_idx == 0 && element.empty())) {
      path_map[path_idx] = SimplePath(prim_part, prop_part);
    }

    // Process according to jump value
    if (jump == -2) {
      // Leaf - done
      return;
    } else if (jump == -1) {
      // Only child
      // For prim nodes, child inherits the prim path
      // For property nodes, this shouldn't happen (properties are leaves)
      if (!is_property) {
        decode_recursive(idx + 1, prim_part);
      }
    } else if (jump == 0) {
      // Only sibling
      // Sibling has the same parent, so use current_prim
      decode_recursive(idx + 1, current_prim);
    } else if (jump > 0) {
      // Both child and sibling
      // Child is next
      if (!is_property) {
        decode_recursive(idx + 1, prim_part);
      } else {
        decode_recursive(idx + 1, current_prim);
      }
      // Sibling is at offset (same parent)
      decode_recursive(idx + jump, current_prim);
    }
  };

  // Start decoding from root (index 0)
  // Root starts with empty path
  decode_recursive(0, "");

  // Convert map to vector (sorted by path_index)
  std::vector<SimplePath> result;
  for (const auto& pair : path_map) {
    result.push_back(pair.second);
  }

  return result;
}

} // namespace crate
} // namespace tinyusdz
