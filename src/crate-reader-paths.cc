// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-2022 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Path decompression and scene graph hierarchy building for CrateReader
//

#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "crate-reader.hh"

#ifdef __wasi__
#else
#include <thread>
#endif

#include <algorithm>
#include <unordered_set>
#include <stack>

#include "crate-format.hh"
#include "parser-timing.hh"
#include "crate-pprint.hh"
#include "integerCoding.h"
#include "lz4-compression.hh"
#include "memory-budget.hh"
#include "path-util.hh"
#include "pprint-meta.hh"
#include "core/prim-spec.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "value-pprint.hh"
#include "value-types.hh"
#include "tiny-format.hh"
#include "str-util.hh"

//
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "nonstd/expected.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

//

#include "common-macros.inc"

namespace tinyusdz {
namespace crate {

#define kTag "[Crate]"

#define CHECK_MEMORY_USAGE(__nbytes) \
  MEMORY_BUDGET_CHECK((*memory_manager_), (__nbytes), kTag)

#if defined(TINYUSDZ_CRATE_USE_FOR_BASED_PATH_INDEX_DECODER)
bool CrateReader::BuildDecompressedPathsImpl(
    BuildDecompressedPathsArg *arg) {

  if (!arg) {
    return false;
  }

  Path parentPath = arg->parentPath;
  if (!arg->pathIndexes) {
    return false;
  }
  if (!arg->elementTokenIndexes) {
    return false;
  }
  if (!arg->jumps) {
    return false;
  }
  if (!arg->visit_table) {
    return false;
  }
  auto &pathIndexes = *arg->pathIndexes;
  auto &elementTokenIndexes = *arg->elementTokenIndexes;
  auto &jumps = *arg->jumps;
  auto &visit_table = *arg->visit_table;

  auto rootPath = Path::make_root_path();

  const size_t maxIter = _config.maxPathIndicesDecodeIteration;

  std::stack<size_t> startIndexStack;
  std::stack<size_t> endIndexStack;
  std::stack<Path> parentPathStack;

  size_t nIter = 0;

  size_t startIndex = arg->startIndex;
  size_t endIndex = arg->endIndex;

  while (nIter < maxIter) {

    DCOUT("startIndex = " << startIndex << ", endIdx = " << endIndex);

    for (size_t thisIndex = startIndex; thisIndex < (endIndex + 1); thisIndex++) {
      //auto thisIndex = curIndex++;
      DCOUT("thisIndex = " << thisIndex << ", pathIndexes.size = " << pathIndexes.size());
      if (parentPath.is_empty()) {
        // root node.
        // Assume single root node in the scene.
        parentPath = rootPath;

        if (thisIndex >= pathIndexes.size()) {
          PUSH_ERROR("Index exceeds pathIndexes.size()");
          return false;
        }

        size_t idx = pathIndexes[thisIndex];
        DCOUT("paths[" << idx << "] is parent. name = " << parentPath.full_path_name());
        if (idx >= _paths.size()) {
          PUSH_ERROR("Index is out-of-range");
          return false;
        }

        if (idx < visit_table.size()) {
          if (visit_table[idx]) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Circular referencing of Path index {}(thisIndex {}) detected. Invalid Paths data.", idx, thisIndex));
          }
        }

        _paths[idx] = parentPath;
        visit_table[idx] = true;
      } else {
        if (thisIndex >= elementTokenIndexes.size()) {
          PUSH_ERROR("Index exceeds elementTokenIndexes.size()");
          return false;
        }
        int32_t _tokenIndex = elementTokenIndexes[thisIndex];
        DCOUT("elementTokenIndex = " << _tokenIndex);
        bool isPrimPropertyPath = _tokenIndex < 0;
        // Guard against INT32_MIN: -INT32_MIN is UB (signed overflow).
        if (isPrimPropertyPath && _tokenIndex == (std::numeric_limits<int32_t>::min)()) {
          PUSH_ERROR("Invalid tokenIndex (INT32_MIN) in BuildDecompressedPathsImpl.");
          return false;
        }
        // ~0 returns -2147483648, so cast to uint32
        uint32_t tokenIndex = uint32_t(isPrimPropertyPath ? -_tokenIndex : _tokenIndex);

        DCOUT("tokenIndex = " << tokenIndex << ", _tokens.size = " << _tokens.size());
        if (tokenIndex >= _tokens.size()) {
          PUSH_ERROR("Invalid tokenIndex in BuildDecompressedPathsImpl.");
          return false;
        }
        auto const &elemToken = _tokens[size_t(tokenIndex)];
        DCOUT("elemToken = " << elemToken);

        if (thisIndex >= pathIndexes.size()) {
          PUSH_ERROR("thisIndex exceeds pathIndexes.size()");
          return false;
        }

        size_t idx = pathIndexes[thisIndex];
        DCOUT("[" << idx << "].append = " << elemToken);

        if (idx >= _paths.size()) {
          PUSH_ERROR("Index is out-of-range");
          return false;
        }

        if (idx >= _elemPaths.size()) {
          PUSH_ERROR("Index is out-of-range");
          return false;
        }

        if (idx < visit_table.size()) {
          if (visit_table[idx]) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Circular referencing of Path index {}(thisIndex {}) detected. Invalid Paths data.", idx, thisIndex));
          }
        }

        // Reconstruct full path
        _paths[idx] =
            isPrimPropertyPath ? parentPath.AppendProperty(elemToken.str())
                               : parentPath.AppendElement(elemToken.str()); // prim, variantSelection, etc.

        // also set leaf path for 'primChildren' check
        _elemPaths[idx] = Path(elemToken.str(), "");
        //_paths[pathIndexes[thisIndex]].SetLocalPart(elemToken.str());

        visit_table[idx] = true;
      }

      // If we have either a child or a sibling but not both, then just
      // continue to the neighbor.  If we have both then spawn a task for the
      // sibling and do the child ourself.  We think that our path trees tend
      // to be broader more often than deep.

      if (thisIndex >= jumps.size()) {
        PUSH_ERROR("Index is out-of-range");
        return false;
      }

      bool hasChild = (jumps[thisIndex] > 0) || (jumps[thisIndex] == -1);
      bool hasSibling = (jumps[thisIndex] >= 0);
      DCOUT("hasChild = " << hasChild << ", hasSibling = " << hasSibling);

      if (hasChild) {
        if (hasSibling) {
          // NOTE(syoyo): This recursive call can be parallelized
          auto siblingIndex = thisIndex + size_t(jumps[thisIndex]);

          if (siblingIndex >= jumps.size()) {
            PUSH_ERROR_AND_RETURN("jump index corrupted.");
          }

          // Find subtree end.
          size_t subtreeStartIdx = siblingIndex;
          size_t subtreeIdx = subtreeStartIdx;

          for (; subtreeIdx < jumps.size(); subtreeIdx++) {

            bool has_child = (jumps[subtreeIdx] > 0) || (jumps[subtreeIdx] == -1);
            bool has_sibling = (jumps[subtreeIdx] >= 0);

            if (has_child || has_sibling) {
              continue;
            }
            break;
          }

          size_t subtreeEndIdx = subtreeIdx;
          if (subtreeEndIdx >= jumps.size()) {
            // Guess corrupted.
            PUSH_ERROR_AND_RETURN("jump indices seems corrupted.");
          }

          DCOUT("subtree startIdx " << subtreeStartIdx << ", subtree endIndex " << subtreeEndIdx);

          if (subtreeEndIdx >= subtreeStartIdx) {

            // index range after traversing subtree
            if (jumps[thisIndex] > 1) {

                // Setup stacks to resume loop from [Cont.]
                startIndexStack.push(thisIndex+1);
                // jumps should be always positive, so no siblingIndex < thisIndex
                endIndexStack.push(siblingIndex-1); // endIndex is inclusive so subtract 1.

                {
                  if (thisIndex >= pathIndexes.size()) {
                    PUSH_ERROR("thisIndex exceeds pathIndexes.size()");
                    return false;
                  }
                  size_t idx = pathIndexes[thisIndex];
                  if (idx >= _paths.size()) {
                    PUSH_ERROR("Index is out-of-range");
                    return false;
                  }

                  parentPathStack.push(_paths[idx]);
                }
            }

            startIndexStack.push(subtreeStartIdx);
            endIndexStack.push(subtreeEndIdx);

            parentPathStack.push(parentPath);
            DCOUT("stack size: " << startIndexStack.size());

            nIter++;

            break; // goto `(A)`
          }

        }

        // [Cont.]
        if (thisIndex >= pathIndexes.size()) {
          PUSH_ERROR("thisIndex exceeds pathIndexes.size()");
          return false;
        }
        size_t idx = pathIndexes[thisIndex];
        if (idx >= _paths.size()) {
          PUSH_ERROR("Index is out-of-range");
          return false;
        }

        parentPath = _paths[idx];

      }
    }

    // (A)

    if (startIndexStack.empty()) {
      break; // end traversal
    }

    startIndex = startIndexStack.top();
    startIndexStack.pop();

    endIndex = endIndexStack.top();
    endIndexStack.pop();

    parentPath = parentPathStack.top();
    parentPathStack.pop();

    nIter++;
  }

  if (nIter >= maxIter) {
    PUSH_ERROR_AND_RETURN("PathIndex tree Too deep.");
  }

  return true;
}
#else
bool CrateReader::BuildDecompressedPathsImpl(
    std::vector<uint32_t> const &pathIndexes,
    std::vector<int32_t> const &elementTokenIndexes,
    std::vector<int32_t> const &jumps,
    std::vector<bool> &visit_table,
    size_t curIndex, const Path &_parentPath) {

  Path parentPath = _parentPath;

  bool hasChild = false, hasSibling = false;
  do {
    auto thisIndex = curIndex++;
    DCOUT("thisIndex = " << thisIndex << ", pathIndexes.size = " << pathIndexes.size());
    if (parentPath.is_empty()) {
      // root node.
      // Assume single root node in the scene.
      parentPath = Path::make_root_path();

      if (thisIndex >= pathIndexes.size()) {
        PUSH_ERROR("Index exceeds pathIndexes.size()");
        return false;
      }

      size_t idx = pathIndexes[thisIndex];
      DCOUT("paths[" << idx << "] is parent. name = " << parentPath.full_path_name());
      if (idx >= _paths.size()) {
        PUSH_ERROR("Index is out-of-range");
        return false;
      }

      if (idx < visit_table.size()) {
        if (visit_table[idx]) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Circular referencing of Path index tree detected. Invalid Paths data.");
        }
      }

      _paths[idx] = parentPath;
      visit_table[idx] = true;
    } else {
      if (thisIndex >= elementTokenIndexes.size()) {
        PUSH_ERROR("Index exceeds elementTokenIndexes.size()");
        return false;
      }
      int32_t _tokenIndex = elementTokenIndexes[thisIndex];
      DCOUT("elementTokenIndex = " << _tokenIndex);
      bool isPrimPropertyPath = _tokenIndex < 0;
      // Guard against INT32_MIN: -INT32_MIN is UB (signed overflow).
      if (isPrimPropertyPath && _tokenIndex == (std::numeric_limits<int32_t>::min)()) {
        PUSH_ERROR("Invalid tokenIndex (INT32_MIN) in BuildDecompressedPathsImpl.");
        return false;
      }
      // ~0 returns -2147483648, so cast to uint32
      uint32_t tokenIndex = uint32_t(isPrimPropertyPath ? -_tokenIndex : _tokenIndex);

      DCOUT("tokenIndex = " << tokenIndex << ", _tokens.size = " << _tokens.size());
      if (tokenIndex >= _tokens.size()) {
        PUSH_ERROR("Invalid tokenIndex in BuildDecompressedPathsImpl.");
        return false;
      }
      auto const &elemToken = _tokens[size_t(tokenIndex)];
      DCOUT("elemToken = " << elemToken);

      if (thisIndex >= pathIndexes.size()) {
        PUSH_ERROR("thisIndex exceeds pathIndexes.size()");
        return false;
      }

      size_t idx = pathIndexes[thisIndex];
      DCOUT("[" << idx << "].append = " << elemToken);

      if (idx >= _paths.size()) {
        PUSH_ERROR("Index is out-of-range");
        return false;
      }

      if (idx >= _elemPaths.size()) {
        PUSH_ERROR("Index is out-of-range");
        return false;
      }

      if (idx < visit_table.size()) {
        if (visit_table[idx]) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Circular referencing of Path index tree detected. Invalid Paths data.");
        }
      }

      // Reconstruct full path
      _paths[idx] =
          isPrimPropertyPath ? parentPath.AppendProperty(elemToken.str())
                             : parentPath.AppendElement(elemToken.str()); // prim, variantSelection, etc.

      // also set leaf path for 'primChildren' check
      _elemPaths[idx] = Path(elemToken.str(), "");
      //_paths[pathIndexes[thisIndex]].SetLocalPart(elemToken.str());

      visit_table[idx] = true;
    }

    // If we have either a child or a sibling but not both, then just
    // continue to the neighbor.  If we have both then spawn a task for the
    // sibling and do the child ourself.  We think that our path trees tend
    // to be broader more often than deep.

    if (thisIndex >= jumps.size()) {
      PUSH_ERROR("Index is out-of-range");
      return false;
    }

    hasChild = (jumps[thisIndex] > 0) || (jumps[thisIndex] == -1);
    hasSibling = (jumps[thisIndex] >= 0);
    DCOUT("hasChild = " << hasChild << ", hasSibling = " << hasSibling);

    DCOUT(fmt::format("hasChild {}, hasSibling {}", hasChild, hasSibling));

    if (hasChild) {
      if (hasSibling) {
        // NOTE(syoyo): This recursive call can be parallelized
        auto siblingIndex = thisIndex + size_t(jumps[thisIndex]);
        if (!BuildDecompressedPathsImpl(pathIndexes, elementTokenIndexes, jumps, visit_table,
                                        siblingIndex, parentPath)) {
          return false;
        }
      }

      if (thisIndex >= pathIndexes.size()) {
        PUSH_ERROR("thisIndex exceeds pathIndexes.size()");
        return false;
      }
      size_t idx = pathIndexes[thisIndex];
      if (idx >= _paths.size()) {
        PUSH_ERROR("Index is out-of-range");
        return false;
      }

      // Have a child (may have also had a sibling). Reset parent path.
      parentPath = _paths[idx];
    }
    // If we had only a sibling, we just continue since the parent path is
    // unchanged and the next thing in the reader stream is the sibling's
    // header.
  } while (hasChild || hasSibling);

  return true;
}
#endif

#if defined(TINYUSDZ_CRATE_USE_FOR_BASED_PATH_INDEX_DECODER)
bool CrateReader::BuildNodeHierarchy(
    std::vector<uint32_t> const &pathIndexes,
    std::vector<int32_t> const &elementTokenIndexes,
    std::vector<int32_t> const &jumps,
    std::vector<bool> &visit_table, /* inout */
    size_t _curIndex,
    int64_t _parentNodeIndex) {

  (void)elementTokenIndexes;

  std::stack<int64_t> parentNodeIndexStack;
  std::stack<size_t> startIndexStack;
  std::stack<size_t> endIndexStack;

  size_t nIter = 0;
  const size_t maxIter = _config.maxPathIndicesDecodeIteration;

  size_t startIndex = _curIndex;
  size_t endIndex = pathIndexes.size() - 1;
  int64_t parentNodeIndex = _parentNodeIndex;

  // NOTE: Need to indirectly lookup index through pathIndexes[] when accessing
  // `_nodes`
  while (nIter < maxIter) {

    for (size_t thisIndex = startIndex; thisIndex < (endIndex + 1); thisIndex++) {
      if (parentNodeIndex == -1) {
        // root node.
        // Assume single root node in the scene.
        //assert(thisIndex == 0);
        if (thisIndex != 0) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "TODO: Multiple root nodes.");
        }

        if (thisIndex >= pathIndexes.size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Index out-of-range.");
        }

        size_t pathIdx = pathIndexes[thisIndex];
        if (pathIdx >= _paths.size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
        }

        if (pathIdx >= _nodes.size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
        }

        if (pathIdx >= visit_table.size()) {
          // This should not be happan though
          PUSH_ERROR_AND_RETURN_TAG(kTag, "[InternalError] out-of-range.");
        }

        if (visit_table[pathIdx]) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Circular referencing detected. Invalid Prim tree representation.");
        }

        _nodes[pathIdx] = Node(parentNodeIndex, _paths[pathIdx]);
        visit_table[pathIdx] = true;

        parentNodeIndex = int64_t(thisIndex);

      } else {
        //if (parentNodeIndex >= int64_t(_nodes.size())) {
        //  PUSH_ERROR_AND_RETURN_TAG(kTag, "Parent Index out-of-range.");
        //}

        if (parentNodeIndex >= int64_t(pathIndexes.size())) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Parent Index out-of-range.");
        }

        if (thisIndex >= pathIndexes.size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Index out-of-range.");
        }

        DCOUT("Hierarchy. parent[" << pathIndexes[size_t(parentNodeIndex)]
                                   << "].add_child = " << pathIndexes[thisIndex]);

        size_t pathIdx = pathIndexes[thisIndex];
        if (pathIdx >= _paths.size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
        }

        if (pathIdx >= _nodes.size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
        }

        if (pathIdx >= visit_table.size()) {
          // This should not be happan though
          PUSH_ERROR_AND_RETURN_TAG(kTag, "[InternalError] out-of-range.");
        }

        if (visit_table[pathIdx]) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Circular referencing detected. Invalid Prim tree representation.");
        }


        // Ensure parent is not set yet.
        if (_nodes[pathIdx].GetParent() != -2) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "???: Maybe corrupted path hierarchy?.");
        }

        Node node(parentNodeIndex, _paths[pathIdx]);
        _nodes[pathIdx] = node;

        visit_table[pathIdx] = true;

        if (pathIdx >= _elemPaths.size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
        }

        //std::string name = _paths[pathIndexes[thisIndex]].local_path_name();
        std::string name = _elemPaths[pathIdx].full_path_name();
        DCOUT("childName = " << name);

        size_t parentNodeIdx = size_t(parentNodeIndex);
        if (parentNodeIdx >= pathIndexes.size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "ParentNodeIdx out-of-range.");
        }

        size_t parentPathIdx = pathIndexes[parentNodeIdx];
        if (parentPathIdx >= _nodes.size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
        }

        if (!_nodes[parentPathIdx].AddChildren(
            name, pathIdx)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid path index.");
        }
      }

      if (thisIndex >= jumps.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Index is out-of-range");
      }

      bool hasChild = (jumps[thisIndex] > 0) || (jumps[thisIndex] == -1);
      bool hasSibling = (jumps[thisIndex] >= 0);

      if (hasChild) {
        if (hasSibling) {
          auto siblingIndex = thisIndex + size_t(jumps[thisIndex]);

          if (siblingIndex >= jumps.size()) {
            PUSH_ERROR_AND_RETURN("jump index corrupted.");
          }

          // Find subtree end.
          size_t subtreeStartIdx = siblingIndex;
          size_t subtreeIdx = subtreeStartIdx;

          for (; subtreeIdx < jumps.size(); subtreeIdx++) {

            bool has_child = (jumps[subtreeIdx] > 0) || (jumps[subtreeIdx] == -1);
            bool has_sibling = (jumps[subtreeIdx] >= 0);

            if (has_child || has_sibling) {
              continue;
            }
            break;
          }

          size_t subtreeEndIdx = subtreeIdx;
          if (subtreeEndIdx >= jumps.size()) {
            // Guess corrupted.
            PUSH_ERROR_AND_RETURN("jump indices seems corrupted.");
          }

          DCOUT("subtree startIdx " << subtreeStartIdx << ", subtree endIndex " << subtreeEndIdx);

          if (subtreeEndIdx >= subtreeStartIdx) {

            // index range after traversing subtree
            if (jumps[thisIndex] > 1) {
                startIndexStack.push(thisIndex+1);
                // jumps should be always positive, so no siblingIndex < thisIndex
                endIndexStack.push(siblingIndex-1); // endIndex is inclusive so subtract 1.
                parentNodeIndexStack.push(int64_t(thisIndex));
            }

            startIndexStack.push(subtreeStartIdx);
            endIndexStack.push(subtreeEndIdx);
            parentNodeIndexStack.push(parentNodeIndex);

            DCOUT("stack size: " << startIndexStack.size());

            nIter++;

            break; // goto `(A)`
          }

        }
        // Have a child (may have also had a sibling). Reset parent node index
        parentNodeIndex = int64_t(thisIndex);
        DCOUT("parentNodeIndex = " << parentNodeIndex);
      }
    }

    // (A)

    if (startIndexStack.empty()) {
      break; // end traversal
    }

    startIndex = startIndexStack.top();
    startIndexStack.pop();

    endIndex = endIndexStack.top();
    endIndexStack.pop();

    parentNodeIndex = parentNodeIndexStack.top();
    parentNodeIndexStack.pop();

    nIter++;
  }

  if (nIter >= maxIter) {
    PUSH_ERROR_AND_RETURN("PathIndex tree Too deep.");
  }

  return true;
}
#else
// TODO(syoyo): Refactor. Code is mostly identical to BuildDecompressedPathsImpl
bool CrateReader::BuildNodeHierarchy(
    std::vector<uint32_t> const &pathIndexes,
    std::vector<int32_t> const &elementTokenIndexes,
    std::vector<int32_t> const &jumps,
    std::vector<bool> &visit_table, /* inout */
    size_t curIndex,
    int64_t parentNodeIndex) {
  bool hasChild = false, hasSibling = false;

  // NOTE: Need to indirectly lookup index through pathIndexes[] when accessing
  // `_nodes`
  do {
    auto thisIndex = curIndex++;
    DCOUT("thisIndex = " << thisIndex << ", curIndex = " << curIndex);
    if (parentNodeIndex == -1) {
      // root node.
      // Assume single root node in the scene.
      //assert(thisIndex == 0);
      if (thisIndex != 0) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "TODO: Multiple root nodes.");
      }

      if (thisIndex >= pathIndexes.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Index out-of-range.");
      }

      size_t pathIdx = pathIndexes[thisIndex];
      if (pathIdx >= _paths.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
      }

      if (pathIdx >= _nodes.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
      }

      if (pathIdx >= visit_table.size()) {
        // This should not be happan though
        PUSH_ERROR_AND_RETURN_TAG(kTag, "[InternalError] out-of-range.");
      }

      if (visit_table[pathIdx]) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Circular referencing detected. Invalid Prim tree representation.");
      }

      Node root(parentNodeIndex, _paths[pathIdx]);

      _nodes[pathIdx] = root;
      visit_table[pathIdx] = true;

      parentNodeIndex = int64_t(thisIndex);

    } else {
      //if (parentNodeIndex >= int64_t(_nodes.size())) {
      //  PUSH_ERROR_AND_RETURN_TAG(kTag, "Parent Index out-of-range.");
      //}

      if (parentNodeIndex >= int64_t(pathIndexes.size())) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Parent Index out-of-range.");
      }

      if (thisIndex >= pathIndexes.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Index out-of-range.");
      }

      DCOUT("Hierarchy. parent[" << pathIndexes[size_t(parentNodeIndex)]
                                 << "].add_child = " << pathIndexes[thisIndex]);

      size_t pathIdx = pathIndexes[thisIndex];
      if (pathIdx >= _paths.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
      }

      if (pathIdx >= _nodes.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
      }

      if (pathIdx >= visit_table.size()) {
        // This should not be happan though
        PUSH_ERROR_AND_RETURN_TAG(kTag, "[InternalError] out-of-range.");
      }

      if (visit_table[pathIdx]) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Circular referencing detected. Invalid Prim tree representation.");
      }

      Node node(parentNodeIndex, _paths[pathIdx]);

      // Ensure parent is not set yet.
      if (_nodes[pathIdx].GetParent() != -2) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "???: Maybe corrupted path hierarchy?.");
      }

      _nodes[pathIdx] = node;
      visit_table[pathIdx] = true;

      if (pathIdx >= _elemPaths.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
      }

      //std::string name = _paths[pathIndexes[thisIndex]].local_path_name();
      std::string name = _elemPaths[pathIdx].full_path_name();
      DCOUT("childName = " << name);

      size_t parentNodeIdx = size_t(parentNodeIndex);
      if (parentNodeIdx >= pathIndexes.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "ParentNodeIdx out-of-range.");
      }

      size_t parentPathIdx = pathIndexes[parentNodeIdx];
      if (parentPathIdx >= _nodes.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
      }

      if (!_nodes[parentPathIdx].AddChildren(
          name, pathIdx)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid path index.");
      }
    }

    if (thisIndex >= jumps.size()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Index is out-of-range");
    }

    hasChild = (jumps[thisIndex] > 0) || (jumps[thisIndex] == -1);
    hasSibling = (jumps[thisIndex] >= 0);

    if (hasChild) {
      if (hasSibling) {
        auto siblingIndex = thisIndex + size_t(jumps[thisIndex]);
        if (!BuildNodeHierarchy(pathIndexes, elementTokenIndexes, jumps, visit_table,
                                siblingIndex, parentNodeIndex)) {
          return false;
        }
      }
      // Have a child (may have also had a sibling). Reset parent node index
      parentNodeIndex = int64_t(thisIndex);
      DCOUT("parentNodeIndex = " << parentNodeIndex);
    }
    // If we had only a sibling, we just continue since the parent path is
    // unchanged and the next thing in the reader stream is the sibling's
    // header.
  } while (hasChild || hasSibling);

  return true;
}
#endif

bool CrateReader::ReadCompressedPaths(const uint64_t maxNumPaths) {
  std::vector<uint32_t> pathIndexes;
  std::vector<int32_t> elementTokenIndexes;
  std::vector<int32_t> jumps;

  // Read number of encoded paths.
  uint64_t numEncodedPaths;
  if (!_sr->read8(&numEncodedPaths)) {
    _err += "Failed to read the number of encoded paths.\n";
    return false;
  }

  DCOUT("maxNumPaths : " << maxNumPaths);
  DCOUT("numEncodedPaths : " << numEncodedPaths);

  // Number of compressed paths could be less than maxNumPaths,
  // but should not be greater.
  if (maxNumPaths < numEncodedPaths) {
    _err += "Size mismatch of numEncodedPaths at `PATHS` section.\n";
    return false;
  }


  // 3 = pathIndex, elementTokenIndex, jump
  CHECK_MEMORY_USAGE(size_t(numEncodedPaths) * sizeof(int32_t) * 3);

  pathIndexes.resize(static_cast<size_t>(numEncodedPaths));
  elementTokenIndexes.resize(static_cast<size_t>(numEncodedPaths));
  jumps.resize(static_cast<size_t>(numEncodedPaths));

  size_t compBufferSize = Usd_IntegerCompression::GetCompressedBufferSize(static_cast<size_t>(numEncodedPaths));
  size_t workspaceBufferSize = Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(static_cast<size_t>(numEncodedPaths));
  CHECK_MEMORY_USAGE(compBufferSize);
  CHECK_MEMORY_USAGE(workspaceBufferSize);

  // Optimized implementation: reuse buffers across calls
  if (_decomp_comp_buffer.size() < compBufferSize) {
    _decomp_comp_buffer.resize(compBufferSize);
  }
  if (_decomp_working_buffer.size() < workspaceBufferSize) {
    _decomp_working_buffer.resize(workspaceBufferSize);
  }
  // Create references for compatibility with existing code
  std::vector<char> &compBuffer = _decomp_comp_buffer;
  std::vector<char> &workingSpace = _decomp_working_buffer;

  // pathIndexes.
  {
    uint64_t compPathIndexesSize;
    if (!_sr->read8(&compPathIndexesSize)) {
      _err += "Failed to read pathIndexesSize.\n";
      return false;
    }

    if (compPathIndexesSize > compBufferSize) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Compressed PathIndexes size.");
    }

    CHECK_MEMORY_USAGE(size_t(compPathIndexesSize));

    if (compPathIndexesSize !=
        _sr->read(size_t(compPathIndexesSize), size_t(compPathIndexesSize),
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      _err += "Failed to read compressed pathIndexes data.\n";
      return false;
    }

    DCOUT("comBuffer.size = " << compBuffer.size());
    DCOUT("compPathIndexesSize = " << compPathIndexesSize);

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), size_t(compPathIndexesSize), pathIndexes.data(),
        size_t(numEncodedPaths), &err, workingSpace.data());
    if (!err.empty()) {
      _err += "Failed to decode pathIndexes\n" + err;
      return false;
    }
  }

  // elementTokenIndexes.
  {
    uint64_t compElementTokenIndexesSize;
    if (!_sr->read8(&compElementTokenIndexesSize)) {
      _err += "Failed to read elementTokenIndexesSize.\n";
      return false;
    }

    if (compElementTokenIndexesSize > compBufferSize) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Compressed elementTokenIndexes size.");
    }

    CHECK_MEMORY_USAGE(size_t(compElementTokenIndexesSize));

    if (compElementTokenIndexesSize !=
        _sr->read(size_t(compElementTokenIndexesSize),
                  size_t(compElementTokenIndexesSize),
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      PUSH_ERROR("Failed to read elementTokenIndexes data.");
      return false;
    }

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), size_t(compElementTokenIndexesSize),
        elementTokenIndexes.data(), size_t(numEncodedPaths), &err,
        workingSpace.data());

    if (!err.empty()) {
      PUSH_ERROR("Failed to decode elementTokenIndexes.");
      return false;
    }
  }

  // jumps.
  {
    uint64_t compJumpsSize;
    if (!_sr->read8(&compJumpsSize)) {
      PUSH_ERROR("Failed to read compressed jumpsSize.");
      return false;
    }

    if (compJumpsSize > compBufferSize) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Compressed elementTokenIndexes size.");
    }

    CHECK_MEMORY_USAGE(size_t(compJumpsSize));

    if (compJumpsSize !=
        _sr->read(size_t(compJumpsSize), size_t(compJumpsSize),
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      PUSH_ERROR("Failed to read compressed jumps data.");
      return false;
    }

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), size_t(compJumpsSize), jumps.data(), size_t(numEncodedPaths),
        &err, workingSpace.data());

    if (!err.empty()) {
      PUSH_ERROR("Failed to decode jumps.");
      return false;
    }
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  for (size_t i = 0; i < pathIndexes.size(); i++) {
    DCOUT("pathIndexes[" << i << "] = " << pathIndexes[i]);
  }

  for (size_t i = 0; i < elementTokenIndexes.size(); i++) {
    std::stringstream ss;
    ss << "elementTokenIndexes[" << i << "] = " << elementTokenIndexes[i];
    int32_t tokIdx = elementTokenIndexes[i];
    if (tokIdx < 0) {
      // Property Path. Need to negate it.
      tokIdx = -tokIdx;
    }
    if (auto tokv = GetToken(crate::Index(uint32_t(tokIdx)))) {
      ss << "(" << tokv.value() << ")";
    }
    ss << "\n";
    DCOUT(ss.str());
  }

  for (size_t i = 0; i < jumps.size(); i++) {
    DCOUT(fmt::format("jumps[{}] = {}", i, jumps[i]));
  }
#endif

  // For circular tree check
  std::vector<bool> visit_table;
  CHECK_MEMORY_USAGE(_paths.size()); // TODO: divide by 8?

  // `_paths` is already initialized just before calling this ReadCompressedPaths
  visit_table.resize(_paths.size());
  for (size_t i = 0; i < visit_table.size(); i++) {
    visit_table[i] = false;
  }

  // Now build the paths.
#if defined(TINYUSDZ_CRATE_USE_FOR_BASED_PATH_INDEX_DECODER)
  BuildDecompressedPathsArg arg;
  arg.pathIndexes = &pathIndexes;
  arg.elementTokenIndexes = &elementTokenIndexes;
  arg.jumps = &jumps;
  arg.visit_table = &visit_table;
  if (pathIndexes.empty()) {
    PUSH_ERROR("pathIndexes is empty.");
    return false;
  }
  arg.startIndex = 0;
  arg.endIndex = pathIndexes.size() - 1; // or numEncodedPaths - 1
  arg.parentPath = Path();
  if (!BuildDecompressedPathsImpl(&arg)) {
    return false;
  }

#else
  if (!BuildDecompressedPathsImpl(pathIndexes, elementTokenIndexes, jumps, visit_table,
                                  /* curIndex */ 0, Path())) {
    return false;
  }
#endif

  //
  // Ensure decoded numEncodedPaths.
  //
  size_t sumDecodedPaths = 0;
  for (size_t i = 0; i < visit_table.size(); i++) {
    if (visit_table[i]) {
      sumDecodedPaths++;
    }
  }
  if (sumDecodedPaths != numEncodedPaths) {
    PUSH_ERROR_AND_RETURN(fmt::format("Decoded {} paths but numEncodedPaths in Crate is {}. Possible corruption of Crate data.",
      sumDecodedPaths, numEncodedPaths));
  }

  // Now build node hierarchy.

  // Circular referencing check should be done in BuildDecompressedPathsImpl,
  // but do check it again just in case.
  for (size_t i = 0; i < visit_table.size(); i++) {
    visit_table[i] = false;
  }
  if (!BuildNodeHierarchy(pathIndexes, elementTokenIndexes, jumps, visit_table,
                          /* curIndex */ 0, /* parent node index */ -1)) {
    return false;
  }

  sumDecodedPaths = 0;
  for (size_t i = 0; i < visit_table.size(); i++) {
    if (visit_table[i]) {
      sumDecodedPaths++;
    }
  }
  if (sumDecodedPaths != numEncodedPaths) {
    PUSH_ERROR_AND_RETURN(fmt::format("Decoded {} paths but numEncodedPaths in BuildNodeHierarchy is {}. Possible corruption of Crate data.",
      sumDecodedPaths, numEncodedPaths));
  }

  return true;
}

} // namespace crate
} // namespace tinyusdz
