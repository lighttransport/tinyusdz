// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Persistent, revisioned next::Stage -> Tydra render update conversion.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "render-converter.hh"
#include "next/stage/change-set.hh"

namespace tinyusdz {
namespace tydra {
namespace next {

using RenderId = uint64_t;
constexpr RenderId kInvalidRenderId = 0;

enum class RenderResourceKind : uint8_t {
  Node = 0,
  Mesh,
  Points,
  Curves,
  PointInstancer,
  Material,
  Texture,
  Image,
  Light,
  Camera,
  Animation,
  Skeleton,
};

struct RemovedRenderResource {
  RenderResourceKind kind = RenderResourceKind::Node;
  RenderId id = kInvalidRenderId;
  std::string key;
};

/// Transactional destination for incremental render resources. A failed call
/// aborts the update; the sink must keep its previously committed revision.
class SceneUpdateSink {
 public:
  virtual ~SceneUpdateSink() = default;
  virtual bool BeginUpdate(uint64_t base_revision, uint64_t new_revision,
                           bool full_resync) = 0;
  virtual bool UpdateCatalog(const RenderScene&) { return true; }
  virtual bool Remove(const RemovedRenderResource&) { return true; }
  virtual bool UpsertNode(RenderId, const SceneNode&) { return true; }
  virtual bool UpsertMesh(RenderId, const RenderMesh&) { return true; }
  virtual bool UpsertPoints(RenderId, const RenderPoints&) { return true; }
  virtual bool UpsertCurves(RenderId, const RenderCurves&) { return true; }
  virtual bool UpsertPointInstancer(RenderId, const RenderPointInstancer&) {
    return true;
  }
  virtual bool UpsertMaterial(RenderId, const RenderMaterial&) { return true; }
  virtual bool UpsertTexture(RenderId, const RenderTexture&) { return true; }
  virtual bool UpsertImage(RenderId, const TextureImage&) { return true; }
  virtual bool UpsertLight(RenderId, const RenderLight&) { return true; }
  virtual bool UpsertCamera(RenderId, const RenderCamera&) { return true; }
  virtual bool UpsertAnimation(RenderId, const AnimationClip&) { return true; }
  virtual bool UpsertSkeleton(RenderId, const Skeleton&) { return true; }
  virtual bool EndUpdate() = 0;
  virtual void AbortUpdate() {}
};

struct RenderUpdateResult {
  bool success = false;
  bool cancelled = false;
  uint64_t revision = 0;
  size_t upsert_count = 0;
  size_t remove_count = 0;
  std::string error;
  std::vector<std::string> warnings;

  explicit operator bool() const { return success; }
};

/// Owns stable resource IDs and the last committed retained RenderScene.
class RenderSession {
 public:
  explicit RenderSession(const ConverterConfig& config = {});
  ~RenderSession();
  RenderSession(RenderSession&&) noexcept;
  RenderSession& operator=(RenderSession&&) noexcept;
  RenderSession(const RenderSession&) = delete;
  RenderSession& operator=(const RenderSession&) = delete;

  RenderUpdateResult Initialize(
      const ::tinyusdz::next::StageSnapshot& snapshot,
      SceneUpdateSink* sink);
  RenderUpdateResult Apply(const ::tinyusdz::next::StageSnapshot& snapshot,
                           const ::tinyusdz::next::StageChangeSet& changes,
                           SceneUpdateSink* sink);

  uint64_t revision() const;
  const RenderScene* scene() const;
  void Reset();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
