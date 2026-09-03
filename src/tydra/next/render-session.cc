// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.

#include "render-session.hh"

#include <algorithm>
#include <map>
#include <limits>
#include <set>
#include <utility>

namespace lightusd {
namespace tydra {
namespace next {
namespace {

using IdMap = std::unordered_map<std::string, RenderId>;

bool PathRelated(const std::string& key, const std::string& changed) {
  if (changed.empty() || changed == "/") return true;
  auto under = [](const std::string& value, const std::string& root) {
    return value == root ||
           (value.size() > root.size() &&
            value.compare(0, root.size(), root) == 0 &&
            value[root.size()] == '/');
  };
  return under(key, changed) || under(changed, key);
}

std::string KeyOrIndex(const std::string& key, size_t index,
                       const char* prefix) {
  if (!key.empty()) return key;
  return std::string(prefix) + std::to_string(index);
}

bool KindAffected(RenderResourceKind kind,
                  ::lightusd::next::StageChangeFlag flags) {
  using Flag = ::lightusd::next::StageChangeFlag;
  if (::lightusd::next::HasStageChange(flags, Flag::Resync)) return true;
  switch (kind) {
    case RenderResourceKind::Node:
      return ::lightusd::next::HasStageChange(flags, Flag::Transform) ||
             ::lightusd::next::HasStageChange(flags, Flag::Visibility) ||
             ::lightusd::next::HasStageChange(flags, Flag::Metadata);
    case RenderResourceKind::Mesh:
    case RenderResourceKind::Points:
    case RenderResourceKind::Curves:
    case RenderResourceKind::PointInstancer:
      return ::lightusd::next::HasStageChange(flags, Flag::Topology) ||
             ::lightusd::next::HasStageChange(flags, Flag::Primvar) ||
             ::lightusd::next::HasStageChange(flags, Flag::Transform) ||
             ::lightusd::next::HasStageChange(flags, Flag::Animation) ||
             ::lightusd::next::HasStageChange(flags, Flag::Visibility);
    case RenderResourceKind::Material:
      return ::lightusd::next::HasStageChange(flags, Flag::Material);
    case RenderResourceKind::Texture:
    case RenderResourceKind::Image:
      return ::lightusd::next::HasStageChange(flags, Flag::Texture) ||
             ::lightusd::next::HasStageChange(flags, Flag::Material);
    case RenderResourceKind::Light:
      return ::lightusd::next::HasStageChange(flags, Flag::Light) ||
             ::lightusd::next::HasStageChange(flags, Flag::Transform) ||
             ::lightusd::next::HasStageChange(flags, Flag::Animation);
    case RenderResourceKind::Camera:
      return ::lightusd::next::HasStageChange(flags, Flag::Camera) ||
             ::lightusd::next::HasStageChange(flags, Flag::Transform) ||
             ::lightusd::next::HasStageChange(flags, Flag::Animation);
    case RenderResourceKind::Animation:
    case RenderResourceKind::Skeleton:
      return ::lightusd::next::HasStageChange(flags, Flag::Animation) ||
             ::lightusd::next::HasStageChange(flags, Flag::Topology);
  }
  return true;
}

}  // namespace

struct RenderSession::Impl {
  explicit Impl(const ConverterConfig& config) : converter(config) {}

  RenderSceneConverter converter;
  RenderScene scene;
  uint64_t revision = 0;
  RenderId next_id = 1;
  bool id_exhausted = false;
  std::map<RenderResourceKind, IdMap> ids;

  bool IsAffected(RenderResourceKind kind, const std::string& key,
                  const ::lightusd::next::StageChangeSet& changes) const {
    if (revision == 0 || changes.full_resync ||
        changes.base_revision != revision) {
      return true;
    }
    for (const auto& change : changes.prims) {
      if (KindAffected(kind, change.flags) &&
          PathRelated(key, change.path.str())) {
        return true;
      }
    }
    return changes.stage_metadata_changed && kind == RenderResourceKind::Node;
  }

  bool HasRenderEffect(const ::lightusd::next::StageChangeSet& changes) const {
    if (changes.full_resync) return true;
    if (changes.stage_metadata_changed) return true;
    for (const auto& change : changes.prims) {
      if (::lightusd::next::HasStageChange(
              change.flags, ::lightusd::next::StageChangeFlag::Resync)) {
        return true;
      }
      for (const auto& kind_map : ids) {
        for (const auto& resource : kind_map.second) {
          if (KindAffected(kind_map.first, change.flags) &&
              PathRelated(resource.first, change.path.str())) {
            return true;
          }
        }
      }
    }
    return false;
  }

  RenderUpdateResult ApplyNoOp(const ::lightusd::next::StageSnapshot& snapshot,
                               SceneUpdateSink* sink) {
    RenderUpdateResult out;
    out.revision = revision;
    out.full_resync = false;
    if (!sink->BeginUpdate(revision, snapshot.revision, false) ||
        !sink->UpdateCatalog(scene) || !sink->EndUpdate()) {
      out.error = "RenderSession: sink rejected no-op update";
      sink->AbortUpdate();
      return out;
    }
    revision = snapshot.revision;
    out.revision = revision;
    out.success = true;
    return out;
  }

  RenderId IdFor(RenderResourceKind kind, const std::string& key,
                 IdMap* next_keys) {
    IdMap& current = ids[kind];
    auto found = current.find(key);
    RenderId id = found == current.end() ? kInvalidRenderId : found->second;
    if (found == current.end()) {
      if (next_id == (std::numeric_limits<RenderId>::max)()) {
        id_exhausted = true;
        return kInvalidRenderId;
      }
      id = next_id++;
    }
    (*next_keys)[key] = id;
    return id;
  }

  template <typename T, typename KeyFn, typename EmitFn>
  bool EmitVector(RenderResourceKind kind, const std::vector<T>& values,
                  const ::lightusd::next::StageChangeSet& changes,
                  KeyFn key_fn, EmitFn emit,
                  IdMap* next_keys, size_t* upserts) {
    std::unordered_map<std::string, size_t> occurrences;
    for (size_t i = 0; i < values.size(); ++i) {
      std::string key = key_fn(values[i], i);
      const size_t occurrence = occurrences[key]++;
      if (occurrence != 0) {
        key += "#" + std::to_string(occurrence);
      }
      const RenderId id = IdFor(kind, key, next_keys);
      if (id == kInvalidRenderId) return false;
      if (IsAffected(kind, key, changes)) {
        if (!emit(id, values[i])) return false;
        ++*upserts;
      }
    }
    return true;
  }

  RenderUpdateResult Apply(const ::lightusd::next::StageSnapshot& snapshot,
                           const ::lightusd::next::StageChangeSet& requested,
                           SceneUpdateSink* sink) {
    RenderUpdateResult out;
    out.revision = revision;
    if (!snapshot || !sink) {
      out.error = !snapshot ? "RenderSession: invalid stage snapshot"
                            : "RenderSession: null update sink";
      return out;
    }
    if (revision != 0 && snapshot.revision <= revision) {
      out.error = "RenderSession: snapshot revision is not newer";
      return out;
    }

    ::lightusd::next::StageChangeSet changes = requested;
    if (revision == 0 || changes.base_revision != revision ||
        changes.new_revision != snapshot.revision) {
      changes.base_revision = revision;
      changes.new_revision = snapshot.revision;
      changes.full_resync = true;
    }
    out.full_resync = changes.full_resync;

    // A transaction may advance the immutable Stage revision without changing
    // any render-affecting data (for example an edit batch that resolves to the
    // already-authored value). Preserve the sink's revision protocol without
    // paying for a complete Stage -> RenderScene conversion.
    if (revision != 0 && (changes.empty() || !HasRenderEffect(changes))) {
      return ApplyNoOp(snapshot, sink);
    }

    ConvertResult converted = converter.Convert(*snapshot.stage);
    out.warnings = converted.warnings;
    if (!converted.success) {
      out.error = converted.error;
      return out;
    }
    RenderScene& next = converted.scene;
    out.converted_resource_count =
        next.images.size() + next.textures.size() + next.materials.size() +
        next.meshes.size() + next.points.size() + next.curves.size() +
        next.point_instancers.size() + next.skeletons.size() +
        next.animations.size() + next.lights.size() + next.cameras.size() +
        next.nodes.size();
    out.converted_scene_bytes = next.memory_usage();
    if (!sink->BeginUpdate(revision, snapshot.revision, changes.full_resync) ||
        !sink->UpdateCatalog(next)) {
      out.error = "RenderSession: sink rejected update start/catalog";
      sink->AbortUpdate();
      return out;
    }

    std::map<RenderResourceKind, IdMap> next_ids;
    bool ok = true;
#define EMIT_VECTOR(KIND, MEMBER, KEY, METHOD)                                  \
    ok = ok && EmitVector(RenderResourceKind::KIND, next.MEMBER, changes,       \
      [](const auto& value, size_t index) {                                     \
        return KeyOrIndex(value.KEY, index, #KIND ":");                        \
      },                                                                        \
      [&](RenderId id, const auto& value) { return sink->METHOD(id, value); },   \
      &next_ids[RenderResourceKind::KIND], &out.upsert_count)

    EMIT_VECTOR(Image, images, resolved_path, UpsertImage);
    EMIT_VECTOR(Texture, textures, prim_path, UpsertTexture);
    EMIT_VECTOR(Material, materials, prim_path, UpsertMaterial);
    EMIT_VECTOR(Mesh, meshes, prim_path, UpsertMesh);
    EMIT_VECTOR(Points, points, prim_path, UpsertPoints);
    EMIT_VECTOR(Curves, curves, prim_path, UpsertCurves);
    EMIT_VECTOR(PointInstancer, point_instancers, prim_path,
                UpsertPointInstancer);
    EMIT_VECTOR(Skeleton, skeletons, prim_path, UpsertSkeleton);
    EMIT_VECTOR(Animation, animations, prim_path, UpsertAnimation);
    EMIT_VECTOR(Light, lights, prim_path, UpsertLight);
    EMIT_VECTOR(Camera, cameras, prim_path, UpsertCamera);
    EMIT_VECTOR(Node, nodes, prim_path, UpsertNode);
#undef EMIT_VECTOR

    if (ok) {
      for (const auto& kind_map : ids) {
        const IdMap& retained = next_ids[kind_map.first];
        for (const auto& old : kind_map.second) {
          if (retained.count(old.first) != 0) continue;
          RemovedRenderResource removed;
          removed.kind = kind_map.first;
          removed.id = old.second;
          removed.key = old.first;
          if (!sink->Remove(removed)) {
            ok = false;
            break;
          }
          ++out.remove_count;
        }
        if (!ok) break;
      }
    }
    if (!ok || !sink->EndUpdate()) {
      out.error = id_exhausted
          ? "RenderSession: stable resource ID space exhausted"
          : "RenderSession: sink rejected resource update";
      sink->AbortUpdate();
      return out;
    }

    scene = std::move(next);
    ids = std::move(next_ids);
    revision = snapshot.revision;
    out.revision = revision;
    out.success = true;
    return out;
  }
};

RenderSession::RenderSession(const ConverterConfig& config)
    : impl_(new Impl(config)) {}
RenderSession::~RenderSession() = default;
RenderSession::RenderSession(RenderSession&&) noexcept = default;
RenderSession& RenderSession::operator=(RenderSession&&) noexcept = default;

RenderUpdateResult RenderSession::Initialize(
    const ::lightusd::next::StageSnapshot& snapshot, SceneUpdateSink* sink) {
  ::lightusd::next::StageChangeSet full;
  full.new_revision = snapshot.revision;
  full.full_resync = true;
  return impl_->Apply(snapshot, full, sink);
}

RenderUpdateResult RenderSession::Apply(
    const ::lightusd::next::StageSnapshot& snapshot,
    const ::lightusd::next::StageChangeSet& changes, SceneUpdateSink* sink) {
  return impl_->Apply(snapshot, changes, sink);
}

uint64_t RenderSession::revision() const { return impl_->revision; }
const RenderScene* RenderSession::scene() const {
  return impl_->revision ? &impl_->scene : nullptr;
}
void RenderSession::Reset() {
  impl_->scene = RenderScene();
  impl_->revision = 0;
  impl_->next_id = 1;
  impl_->id_exhausted = false;
  impl_->ids.clear();
}

}  // namespace next
}  // namespace tydra
}  // namespace lightusd
