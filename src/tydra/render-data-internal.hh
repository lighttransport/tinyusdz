// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Internal header shared across render-data split files.
// NOT part of the public API.
//
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "tydra/render-data.hh"
#include "usdSkel.hh"
#include "value-types.hh"
#include "common-macros.inc"

namespace tinyusdz {

// Forward declarations
class Stage;
class Prim;
class Path;
struct Skeleton;
struct SkelRoot;
struct SkelAnimation;
struct AssetInfo;

namespace tydra {

// -----------------------------------------------------------------------
// SkelRootSkeletonResolver
// Resolves Skeleton prims from SkelRoot ancestor hierarchy.
// Used by both ConvertMesh (render-data-mesh.cc) and
// ConvertToRenderScene (render-data.cc).
// -----------------------------------------------------------------------
class SkelRootSkeletonResolver {
 public:
  using SkelRootToSkeletonMap =
      std::unordered_map<std::string, std::pair<Path, const Skeleton *>,
                         FNV1StringHash>;

  static void BuildMap(const PathPrimMap<Skeleton> &allSkeletons,
                       const PathPrimMap<SkelRoot> &allSkelRoots,
                       SkelRootToSkeletonMap *out_map) {
    if (!out_map) {
      return;
    }

    out_map->clear();
    out_map->reserve(allSkelRoots.size());

    for (const auto &kv : allSkeletons) {
      const std::string &skel_path_str = kv.first;
      const Skeleton *skel_ptr = kv.second;
      Path current_path(skel_path_str, "");

      size_t iter = 0;
      while (current_path.is_valid() && !current_path.is_root_path()
             && !current_path.is_root_prim()) {
        if (iter++ >= kMaxDefaultTraversalLimit) break;
        Path parent_path = current_path.get_parent_prim_path();
        const std::string parent_path_str = parent_path.prim_part();

        if (allSkelRoots.find(parent_path_str) != allSkelRoots.end()) {
          // Deterministic selection: if multiple Skeletons exist under one
          // SkelRoot, keep lexicographically smallest absolute skeleton path.
          auto it = out_map->find(parent_path_str);
          if (it == out_map->end()) {
            out_map->emplace(parent_path_str,
                             std::make_pair(Path(skel_path_str, ""), skel_ptr));
          } else if (skel_path_str < it->second.first.prim_part()) {
            it->second = std::make_pair(Path(skel_path_str, ""), skel_ptr);
          }
          break;
        }

        current_path = parent_path;
      }
    }
  }

  // Find skeleton for mesh by walking up ancestor hierarchy.
  // Returns true if skeleton found, with path and skeleton pointer stored.
  static bool FindByAncestor(const Path &meshPath,
                             const PathPrimMap<Skeleton> &allSkeletons,
                             const PathPrimMap<SkelRoot> &allSkelRoots,
                             const SkelRootToSkeletonMap *skelRootToSkeleton,
                             Path *outSkelPath, const Skeleton **outSkelPtr) {
    if (allSkeletons.empty()) {
      return false;
    }

    // Walk up ancestor chain
    size_t iter = 0;
    Path currentPath = meshPath;
    while (currentPath.is_valid() && !currentPath.is_root_path()
           && !currentPath.is_root_prim()) {
      if (iter++ >= kMaxDefaultTraversalLimit) break;
      Path parentPath = currentPath.get_parent_prim_path();
      std::string parentPathStr = parentPath.prim_part();

      DCOUT("FindSkeletonByAncestor: checking parent " << parentPathStr);

      // Check if parent is a SkelRoot
      auto skelRootIt = allSkelRoots.find(parentPathStr);
      if (skelRootIt != allSkelRoots.end()) {
        DCOUT("Found SkelRoot ancestor: " << parentPathStr);

        if (skelRootToSkeleton) {
          auto mapped = skelRootToSkeleton->find(parentPathStr);
          if (mapped != skelRootToSkeleton->end()) {
            *outSkelPath = mapped->second.first;
            if (outSkelPtr) *outSkelPtr = mapped->second.second;
            DCOUT("Found skeleton under SkelRoot from cache: "
                  << mapped->second.first.prim_part());
            return true;
          }
        }

        // Found SkelRoot ancestor - search its children for Skeleton
        std::string bestSkelPath;
        const Skeleton *bestSkelPtr{nullptr};
        for (const auto &kv : allSkeletons) {
          const std::string &skelPath = kv.first;
          const Skeleton *skelPtr = kv.second;

          if (IsStrictDescendantPath(skelPath, parentPathStr)) {
            if (!bestSkelPtr || skelPath < bestSkelPath) {
              bestSkelPath = skelPath;
              bestSkelPtr = skelPtr;
            }
          }
        }

        if (bestSkelPtr) {
          *outSkelPath = Path(bestSkelPath, "");
          if (outSkelPtr) *outSkelPtr = bestSkelPtr;
          DCOUT("Found skeleton under SkelRoot: " << bestSkelPath);
          return true;
        }
      }

      // Also check if parent itself is a Skeleton
      auto skelIt = allSkeletons.find(parentPathStr);
      if (skelIt != allSkeletons.end()) {
        *outSkelPath = Path(parentPathStr, "");
        if (outSkelPtr) *outSkelPtr = skelIt->second;
        DCOUT("Found skeleton as ancestor: " << parentPathStr);
        return true;
      }

      currentPath = parentPath;
    }

    // Fallback: if only one skeleton in scene, use it
    if (allSkeletons.size() == 1) {
      *outSkelPath = Path(allSkeletons.begin()->first, "");
      if (outSkelPtr) *outSkelPtr = allSkeletons.begin()->second;
      DCOUT("Fallback: using only skeleton in scene: "
            << allSkeletons.begin()->first);
      return true;
    }

    return false;
  }

 private:
  static bool IsStrictDescendantPath(const std::string &descendantPath,
                                     const std::string &ancestorPath) {
    if (ancestorPath.empty() || descendantPath.empty()) {
      return false;
    }

    // All absolute prim paths are expected to start with '/'.
    if (descendantPath[0] != '/' || ancestorPath[0] != '/') {
      return false;
    }

    if (descendantPath.size() <= ancestorPath.size()) {
      return false;
    }

    if (descendantPath.compare(0, ancestorPath.size(), ancestorPath) != 0) {
      return false;
    }

    // Require a path-segment boundary:
    // "/A/SkelRoot/Skel" is a descendant of "/A/SkelRoot", but
    // "/A/SkelRootExtra/Skel" is not.
    return descendantPath[ancestorPath.size()] == '/';
  }
};

// -----------------------------------------------------------------------
// MeshVisitorEnv / MeshVisitor
// Scene traversal visitor for mesh and material conversion.
// Defined in render-data-material.cc, used in render-data.cc.
// -----------------------------------------------------------------------
struct MeshVisitorEnv {
  RenderSceneConverter *converter{nullptr};
  const RenderSceneConverterEnv *env{nullptr};

  // Progress tracking for detailed progress reporting
  size_t meshes_processed{0};
  size_t meshes_total{0};
  size_t materials_processed{0};
  size_t materials_total{0};

  // Pre-discovered skeleton/animation prims for ancestor-based discovery
  const PathPrimMap<Skeleton> *allSkeletons{nullptr};
  const PathPrimMap<SkelRoot> *allSkelRoots{nullptr};
  const PathPrimMap<SkelAnimation> *allAnimations{nullptr};
};

bool MeshVisitor(const tinyusdz::Path &abs_path, const tinyusdz::Prim &prim,
                 const int32_t level, void *userdata, std::string *err);

// -----------------------------------------------------------------------
// ConnectionResolveCache reset
// Defined in render-data-material.cc, called from render-data.cc.
// -----------------------------------------------------------------------
void ResetConnectionResolveCache(const Stage &stage);

// -----------------------------------------------------------------------
// RawAssetRead
// Shared utility for reading raw assets through the asset resolver.
// Used by render-data-material.cc (ConvertUVTexture) and
// render-data-anim.cc (ConvertDomeLight).
// -----------------------------------------------------------------------
bool RawAssetRead(
    const value::AssetPath &assetPath, const AssetInfo &assetInfo,
    const AssetResolutionResolver &assetResolver,
    Asset *assetOut,
    std::string &resolvedPathOut,
    void *userdata, std::string *warn,
    std::string *err);

}  // namespace tydra
}  // namespace tinyusdz
