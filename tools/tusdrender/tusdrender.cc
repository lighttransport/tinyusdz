// SPDX-License-Identifier: Apache 2.0
// Copyright 2026 - Present, Light Transport Entertainment, Inc.
//
// tusdrender: CPU preview raytrace renderer for USD scenes.
//
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>  // ProcessRSS()/AvailableSystemMemory() use std::ifstream
                    // unconditionally (was only included under TINYUSDZ_WITH_QJS)
#include <iostream>
#include <mutex>
#include <new>
#include <unistd.h>
#include <array>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <malloc.h>  // mallopt (peak-RSS tuning, glibc)
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "asset-resolution.hh"
#include "image-loader.hh"
#include "image-writer.hh"
#include "io-util.hh"
#include "mmap-array-ref.hh"
#include "tinyusdz.hh"
#include "tsd/tinysubdiv.hh"
#include "tydra/attribute-eval.hh"
#include "tydra/render-data.hh"
#include "usdVol.hh"  // OpenVDB (.vdb) loader for the `next` volume path
#include "usdGeom.hh"
#include "value-types.hh"
#include "xform.hh"

// Experimental `next` lazy loader: fast, low-memory USDC parse used as the
// default backend for the RT preview path. tydra_next provides bit-exact
// world transforms (see src/tydra/next/scene-access.cc).
#include "next/layer/prim-spec.hh"
#include "next/pcp/prim-index.hh"
#include "next/prim/path.hh"
#include "next/reader/usdz-reader.hh"
#include "next/schema/geom-mesh.hh"
#include "next/stage/stage.hh"
#include "next/tinyusdz-next.hh"
#include "next/types/value.hh"
#include "tydra/next/scene-access.hh"



extern "C" {
#include "lightrt_c_tri.h"
}

#ifdef TINYUSDZ_WITH_QJS
#include <fstream>
#include <sstream>

#include "external/jsonhpp/nlohmann/json.hpp"
#include "tydra/js-script.hh"
extern "C" {
#include "external/quickjs-ng/quickjs.h"
}
#endif

#include "tusdr_math.hh"
#include "tusdr_types.hh"
#include "tusdr_context.hh"

namespace tusdr {

using tinyusdz::value::color3f;
using tinyusdz::value::float3;
using tinyusdz::value::matrix4d;
using tinyusdz::tydra::Node;
using tinyusdz::tydra::NodeType;
using tinyusdz::tydra::RenderCamera;
using tinyusdz::tydra::RenderLight;
using tinyusdz::tydra::RenderMaterial;
using tinyusdz::tydra::RenderMesh;
using tinyusdz::tydra::RenderScene;

// ===========================================================================
// Memory budget / manager.
//
// A process-wide cap that keeps tusdrender from being OOM-killed on huge scenes
// (e.g. fully instance-expanded Caldera maps). The cap defaults to
// min(32 GiB, 0.5 * system MemAvailable) and is overridable with -maxMem <GiB>.
// It is enforced two ways:
//   1. Phase guards (GuardPhase) check the live process RSS + an estimate of the
//      next phase's allocation and abort cleanly BEFORE the allocation that would
//      bust the cap (covers composition + LightRT BVH, which allocate outside our
//      allocator).
//   2. The pool allocator (PoolAlloc, below) accounts every render-buffer byte
//      into `tracked_` and throws std::bad_alloc when our allocations would push
//      RSS past the cap (covers the triangle stream precisely, mid-flight).
// ===========================================================================

// Decoded RGB(A) texture (8-bit) sampled by the diffuse texture pipeline.
// UsdUVTexture wrap mode (inputs:wrapS / inputs:wrapT).





std::vector<int> FaceMaterialIds(const RenderMesh &mesh) {
  const std::vector<uint32_t> &counts = mesh.faceVertexCounts();
  std::vector<int> ids(counts.size(), mesh.material_id);
  for (const auto &kv : mesh.material_subsetMap) {
    const tinyusdz::tydra::MaterialSubset &subset = kv.second;
    const std::vector<int> &faces = subset.indices();
    for (int f : faces) {
      if (f >= 0 && size_t(f) < ids.size()) {
        ids[size_t(f)] = subset.material_id;
      }
    }
  }
  return ids;
}

void AddMeshTriangles(const RenderScene &scene, const RenderMesh &mesh,
                      const matrix4d &world, std::vector<float> *vertices,
                      std::vector<TriInfo> *tris, Bounds *bounds,
                      LightCache *lights = nullptr) {
  if (!vertices || !tris || !bounds) return;
  const std::vector<uint32_t> &indices = mesh.faceVertexIndices();
  const std::vector<uint32_t> &counts = mesh.faceVertexCounts();
  std::vector<int> material_ids = FaceMaterialIds(mesh);
  float mesh_area = 0.0f;
  if (mesh.is_area_light) {
    size_t area_cursor = 0;
    for (size_t face = 0; face < counts.size(); face++) {
      uint32_t nverts = counts[face];
      if (nverts < 3 || area_cursor + nverts > indices.size()) {
        area_cursor += nverts;
        continue;
      }
      for (uint32_t k = 1; k + 1 < nverts; k++) {
        uint32_t i0 = indices[area_cursor + 0];
        uint32_t i1 = indices[area_cursor + k];
        uint32_t i2 = indices[area_cursor + k + 1];
        if (i0 >= mesh.points.size() || i1 >= mesh.points.size() ||
            i2 >= mesh.points.size()) {
          continue;
        }
        mesh_area += TriangleArea(TransformPoint(world, FromFloat3(mesh.points[i0])),
                                  TransformPoint(world, FromFloat3(mesh.points[i1])),
                                  TransformPoint(world, FromFloat3(mesh.points[i2])));
      }
      area_cursor += nverts;
    }
  }
  size_t cursor = 0;
  for (size_t face = 0; face < counts.size(); face++) {
    uint32_t nverts = counts[face];
    if (nverts < 3 || cursor + nverts > indices.size()) {
      cursor += nverts;
      continue;
    }
    int mat_id = (face < material_ids.size()) ? material_ids[face] : mesh.material_id;
    for (uint32_t k = 1; k + 1 < nverts; k++) {
      uint32_t i0 = indices[cursor + 0];
      uint32_t i1 = indices[cursor + k];
      uint32_t i2 = indices[cursor + k + 1];
      if (i0 >= mesh.points.size() || i1 >= mesh.points.size() ||
          i2 >= mesh.points.size()) {
        continue;
      }
      Vec3 p0 = TransformPoint(world, FromFloat3(mesh.points[i0]));
      Vec3 p1 = TransformPoint(world, FromFloat3(mesh.points[i1]));
      Vec3 p2 = TransformPoint(world, FromFloat3(mesh.points[i2]));
      Vec3 n = Normalize(Cross(Sub(p1, p0), Sub(p2, p0)));
      if (!mesh.is_rightHanded) {
        n = Mul(n, -1.0f);
      }
      TriInfo tri;
      tri.p0 = p0;
      tri.p1 = p1;
      tri.p2 = p2;
      tri.n = n;
      tri.base_color = MaterialColor(scene, mesh, mat_id);
      tri.emission = MaterialEmission(scene, mat_id);
      tri.roughness = MaterialRoughness(scene, mat_id);
      tri.metallic = MaterialMetallic(scene, mat_id);
      if (mesh.is_area_light) {
        tri.emission = MeshLightEmission(scene, mesh, mat_id, mesh_area);
      }
      vertices->push_back(p0.x);
      vertices->push_back(p0.y);
      vertices->push_back(p0.z);
      vertices->push_back(p1.x);
      vertices->push_back(p1.y);
      vertices->push_back(p1.z);
      vertices->push_back(p2.x);
      vertices->push_back(p2.y);
      vertices->push_back(p2.z);
      int tri_id = int(tris->size());
      tris->push_back(tri);
      if (lights && mesh.is_area_light && Luminance(tri.emission) > 1.0e-6f) {
        float area = TriangleArea(p0, p1, p2);
        if (area > 1.0e-10f) {
          PreviewLight ml;
          ml.kind = PreviewLight::Kind::Mesh;
          ml.position = Mul(Add(Add(p0, p1), p2), 1.0f / 3.0f);
          ml.normal = n;
          ml.direction = Mul(n, -1.0f);
          ml.radiance = tri.emission;
          ml.area = area;
          ml.power = std::max(0.0f, Luminance(ml.radiance) * area);
          ml.tri_id = tri_id;
          lights->mesh.push_back(ml);
        }
      }
      Expand(bounds, p0);
      Expand(bounds, p1);
      Expand(bounds, p2);
    }
    cursor += nverts;
  }
}

void CollectGeometry(const RenderScene &scene, const Node &node,
                     std::vector<float> *vertices, std::vector<TriInfo> *tris,
                     Bounds *bounds,
                     const std::unordered_set<std::string> *skip_paths,
                     LightCache *lights) {
  if (node.nodeType == NodeType::Mesh && node.id >= 0 &&
      size_t(node.id) < scene.meshes.size()) {
    const RenderMesh &mesh = scene.meshes[size_t(node.id)];
    if (!skip_paths || !skip_paths->count(mesh.abs_path)) {
      AddMeshTriangles(scene, mesh, node.global_matrix, vertices, tris, bounds,
                       lights);
    }
  }
  for (const Node &child : node.children) {
    CollectGeometry(scene, child, vertices, tris, bounds, skip_paths, lights);
  }
}

void CollectAllGeometry(const RenderScene &scene, std::vector<float> *vertices,
                        std::vector<TriInfo> *tris, Bounds *bounds,
                        const std::unordered_set<std::string> *skip_paths,
                        LightCache *lights) {
  for (const Node &root : scene.nodes) {
    CollectGeometry(scene, root, vertices, tris, bounds, skip_paths, lights);
  }
  for (const tinyusdz::tydra::RenderInstance &inst : scene.instances) {
    if (inst.mesh_id >= 0 && size_t(inst.mesh_id) < scene.meshes.size() &&
        inst.visible) {
      const RenderMesh &mesh = scene.meshes[size_t(inst.mesh_id)];
      AddMeshTriangles(scene, mesh, inst.global_matrix, vertices, tris, bounds,
                       lights);
    }
  }
}


template <typename T>
bool BorrowMMapArray(const tinyusdz::Stage &stage, const std::string &prim_path,
                     const std::string &attr_name, BorrowedArrayView<T> *out) {
  if (!out || !stage.has_mmap_zero_copy()) return false;
  const tinyusdz::MMapArrayRef *ref =
      stage.mmap_table()->find_compatible(prim_path, attr_name);
  if (!ref) return false;
  const tinyusdz::MMapDataSource *source = stage.mmap_source();
  if (!source || !source->is_valid()) return false;
  if (ref->element_size != sizeof(T)) return false;
  if (ref->element_count > (UINT64_MAX / sizeof(T))) return false;
  uint64_t byte_count = ref->element_count * sizeof(T);
  if (ref->byte_offset > source->size()) return false;
  if (byte_count > (source->size() - ref->byte_offset)) return false;
  const uint8_t *bytes = source->addr() + ref->byte_offset;
  const T *ptr = nullptr;
  if (reinterpret_cast<uintptr_t>(bytes) % alignof(T) == 0) {
    ptr = reinterpret_cast<const T *>(bytes);
  }
  if (ref->element_count > uint64_t((std::numeric_limits<size_t>::max)())) {
    return false;
  }
  out->data = ptr;
  out->bytes = bytes;
  out->count = static_cast<size_t>(ref->element_count);
  out->mmap = true;
  out->owned.clear();
  return true;
}

template <typename T>
T ReadBorrowedArrayValue(const BorrowedArrayView<T> &view, size_t index) {
  if (view.data) return view.data[index];
  T value{};
  std::memcpy(&value, view.bytes + index * sizeof(T), sizeof(T));
  return value;
}

// Zero-copy const-ref access to an in-memory array attribute. Returns the
// underlying vector without copying when the attribute holds a static (non
// time-sampled, non-connected, non-blocked) default value. Returns nullptr for
// time-sampled/connected/blocked attributes (the caller should fall back to the
// copying EvalAnim() path) or for deferred mmap arrays (empty vector in
// mmap_zero_copy mode; the caller should try BorrowMMapArray() first).
template <typename T>
const std::vector<T> *BorrowScalarVector(
    const tinyusdz::TypedAttribute<tinyusdz::Animatable<std::vector<T>>> &attr) {
  if (attr.is_blocked() || attr.has_connections()) return nullptr;
  const auto &opt = attr.get_value_ref();
  if (!opt) return nullptr;
  const tinyusdz::Animatable<std::vector<T>> &anim = opt.value();
  if (anim.is_scalar() && anim.has_default()) {
    return &anim.get_scalar_ref();
  }
  return nullptr;
}

// Number of worker threads to use for the embarrassingly-parallel mesh-stream
// and render passes. `requested` is the user's -threads value (<=0 means auto).
unsigned WorkerThreadCount(int requested) {
  if (requested > 0) return unsigned(requested);
  unsigned hw = std::thread::hardware_concurrency();
  return hw > 0 ? hw : 1u;
}

const tinyusdz::Xformable *AsPreviewXformable(const tinyusdz::Prim &prim) {
  if (const tinyusdz::Xform *x = prim.as<tinyusdz::Xform>()) return x;
  if (const tinyusdz::GeomMesh *m = prim.as<tinyusdz::GeomMesh>()) return m;
  if (const tinyusdz::GeomCamera *c = prim.as<tinyusdz::GeomCamera>()) return c;
  return nullptr;
}

const tinyusdz::GPrim *AsPreviewGPrim(const tinyusdz::Prim &prim) {
  if (const tinyusdz::Xform *p = prim.as<tinyusdz::Xform>()) return p;
  if (const tinyusdz::GeomMesh *p = prim.as<tinyusdz::GeomMesh>()) return p;
  if (const tinyusdz::GeomCamera *p = prim.as<tinyusdz::GeomCamera>()) return p;
  if (const tinyusdz::GeomCube *p = prim.as<tinyusdz::GeomCube>()) return p;
  if (const tinyusdz::GeomSphere *p = prim.as<tinyusdz::GeomSphere>()) return p;
  if (const tinyusdz::GeomCone *p = prim.as<tinyusdz::GeomCone>()) return p;
  if (const tinyusdz::GeomCylinder *p = prim.as<tinyusdz::GeomCylinder>()) return p;
  if (const tinyusdz::GeomCapsule *p = prim.as<tinyusdz::GeomCapsule>()) return p;
  if (const tinyusdz::GeomPlane *p = prim.as<tinyusdz::GeomPlane>()) return p;
  if (const tinyusdz::GeomTetMesh *p = prim.as<tinyusdz::GeomTetMesh>()) return p;
  if (const tinyusdz::GeomNurbsPatch *p = prim.as<tinyusdz::GeomNurbsPatch>()) {
    return p;
  }
  if (const tinyusdz::GeomBasisCurves *p = prim.as<tinyusdz::GeomBasisCurves>()) {
    return p;
  }
  if (const tinyusdz::GeomHermiteCurves *p =
          prim.as<tinyusdz::GeomHermiteCurves>()) {
    return p;
  }
  if (const tinyusdz::GeomNurbsCurves *p = prim.as<tinyusdz::GeomNurbsCurves>()) {
    return p;
  }
  if (const tinyusdz::GeomPoints *p = prim.as<tinyusdz::GeomPoints>()) return p;
  if (const tinyusdz::GeomPointInstancer *p =
          prim.as<tinyusdz::GeomPointInstancer>()) {
    return p;
  }
  return nullptr;
}

matrix4d LocalMatrixOrIdentity(const tinyusdz::Xformable *xformable, double time,
                               bool *reset) {
  if (reset) *reset = false;
  if (!xformable) return matrix4d::identity();
  bool local_reset = false;
  auto ret = xformable->GetLocalMatrix(
      time, tinyusdz::value::TimeSampleInterpolationType::Linear, &local_reset);
  if (reset) *reset = local_reset;
  if (!ret) return matrix4d::identity();
  return ret.value();
}

template <typename T>
bool EvalAnim(const tinyusdz::Stage &stage,
              const tinyusdz::TypedAttribute<tinyusdz::Animatable<T>> &attr,
              const std::string &name, double time, T *out) {
  std::string err;
  return tinyusdz::tydra::EvaluateTypedAnimatableAttribute(
      stage, attr, name, out, &err, time);
}

template <typename T>
bool EvalAnimFallback(
    const tinyusdz::Stage &stage,
    const tinyusdz::TypedAttributeWithFallback<tinyusdz::Animatable<T>> &attr,
    const std::string &name, double time, T *out) {
  std::string err;
  return tinyusdz::tydra::EvaluateTypedAnimatableAttribute(
      stage, attr, name, out, &err, time);
}

uint32_t PurposeBit(tinyusdz::Purpose purpose) {
  switch (purpose) {
    case tinyusdz::Purpose::Render:
      return kPurposeRenderBit;
    case tinyusdz::Purpose::Proxy:
      return kPurposeProxyBit;
    case tinyusdz::Purpose::Guide:
      return kPurposeGuideBit;
    case tinyusdz::Purpose::Default:
    default:
      return kPurposeDefaultBit;
  }
}

tinyusdz::Purpose ResolvePurpose(const tinyusdz::Prim &prim,
                                 tinyusdz::Purpose inherited) {
  if (const tinyusdz::GPrim *gprim = AsPreviewGPrim(prim)) {
    tinyusdz::Purpose purpose = gprim->purpose.get_value();
    if (purpose != tinyusdz::Purpose::Default) return purpose;
  }
  return inherited;
}

bool PurposeVisible(uint32_t purpose_bit, uint32_t purpose_mask) {
  return (purpose_bit & purpose_mask) != 0;
}

bool AddRTPreviewMesh(const tinyusdz::Stage &stage, const std::string &prim_path,
                      const tinyusdz::GeomMesh &mesh, const matrix4d &world,
                      double time, tinyusdz::Purpose purpose,
                      uint32_t purpose_mask,
                      std::vector<float> *vertices, std::vector<TriInfo> *tris,
                      Bounds *bounds, RTPreviewStats *stats) {
  if (!vertices || !tris || !bounds || !stats) return false;
  BorrowedArrayView<tinyusdz::value::point3f> points;
  if (BorrowMMapArray(stage, prim_path, "points", &points)) {
    stats->meshes_with_mmap_points++;
  } else if (const std::vector<tinyusdz::value::point3f> *pv =
                 BorrowScalarVector(mesh.points)) {
    // Zero-copy: in-memory (materialized) points vector.
    points.data = pv->data();
    points.bytes = reinterpret_cast<const uint8_t *>(pv->data());
    points.count = pv->size();
    points.mmap = false;
    stats->meshes_with_owned_points++;
  } else {
    // Fallback (time-sampled/connected): copy via attribute evaluation.
    if (!EvalAnim(stage, mesh.points, "points", time, &points.owned)) {
      stats->skipped_meshes++;
      return false;
    }
    points.data = points.owned.data();
    points.bytes = reinterpret_cast<const uint8_t *>(points.owned.data());
    points.count = points.owned.size();
    points.mmap = false;
    stats->meshes_with_owned_points++;
    stats->copied_point_bytes +=
        uint64_t(points.count) * sizeof(tinyusdz::value::point3f);
  }

  // Topology: prefer zero-copy const-ref to the in-memory vectors; fall back to
  // a copy only for time-sampled/connected attributes.
  std::vector<int32_t> counts_owned;
  std::vector<int32_t> indices_owned;
  const std::vector<int32_t> *counts_ptr =
      BorrowScalarVector(mesh.faceVertexCounts);
  if (!counts_ptr) {
    if (!EvalAnim(stage, mesh.faceVertexCounts, "faceVertexCounts", time,
                  &counts_owned)) {
      stats->skipped_meshes++;
      return false;
    }
    counts_ptr = &counts_owned;
    stats->copied_topology_bytes += uint64_t(counts_owned.size()) * sizeof(int32_t);
  }
  const std::vector<int32_t> *indices_ptr =
      BorrowScalarVector(mesh.faceVertexIndices);
  if (!indices_ptr) {
    if (!EvalAnim(stage, mesh.faceVertexIndices, "faceVertexIndices", time,
                  &indices_owned)) {
      stats->skipped_meshes++;
      return false;
    }
    indices_ptr = &indices_owned;
    stats->copied_topology_bytes += uint64_t(indices_owned.size()) * sizeof(int32_t);
  }
  const std::vector<int32_t> &counts = *counts_ptr;
  const std::vector<int32_t> &indices = *indices_ptr;
  if ((!points.data && !points.bytes) || points.count == 0 || counts.empty() ||
      indices.empty()) {
    stats->skipped_meshes++;
    return false;
  }

  // Reserve output buffers up-front from the exact triangle-fan estimate to
  // avoid repeated reallocation while appending.
  size_t tri_estimate = 0;
  for (int32_t c : counts) {
    if (c >= 3) tri_estimate += size_t(c - 2);
  }
  if (tri_estimate) {
    vertices->reserve(vertices->size() + tri_estimate * 9);
    tris->reserve(tris->size() + tri_estimate);
  }

  size_t cursor = 0;
  for (int32_t c : counts) {
    if (c < 3 || cursor + size_t(c) > indices.size()) {
      cursor += size_t(std::max<int32_t>(0, c));
      continue;
    }
    for (int32_t k = 1; k + 1 < c; k++) {
      int32_t i0 = indices[cursor + 0];
      int32_t i1 = indices[cursor + size_t(k)];
      int32_t i2 = indices[cursor + size_t(k + 1)];
      if (i0 < 0 || i1 < 0 || i2 < 0 || size_t(i0) >= points.count ||
          size_t(i1) >= points.count || size_t(i2) >= points.count) {
        continue;
      }
      Vec3 p0 = TransformPoint(
          world, FromPoint3(ReadBorrowedArrayValue(points, size_t(i0))));
      Vec3 p1 = TransformPoint(
          world, FromPoint3(ReadBorrowedArrayValue(points, size_t(i1))));
      Vec3 p2 = TransformPoint(
          world, FromPoint3(ReadBorrowedArrayValue(points, size_t(i2))));
      Vec3 n = Normalize(Cross(Sub(p1, p0), Sub(p2, p0)));
      if (Length(n) < 1.0e-12f) continue;

      TriInfo tri;
      tri.p0 = p0;
      tri.p1 = p1;
      tri.p2 = p2;
      tri.n = n;
      tri.base_color = Vec3{0.55f, 0.55f, 0.55f};
      tri.purpose_bit = PurposeBit(purpose);
      if (tri.purpose_bit == kPurposeRenderBit) {
        stats->purpose_render_triangles++;
      } else if (tri.purpose_bit == kPurposeProxyBit) {
        stats->purpose_proxy_triangles++;
      } else if (tri.purpose_bit == kPurposeGuideBit) {
        stats->purpose_guide_triangles++;
      } else {
        stats->purpose_default_triangles++;
      }
      const bool visible_for_fit = PurposeVisible(tri.purpose_bit, purpose_mask);
      vertices->insert(vertices->end(),
                       {p0.x, p0.y, p0.z, p1.x, p1.y, p1.z, p2.x, p2.y, p2.z});
      tris->push_back(tri);
      stats->triangles++;
      if (visible_for_fit) {
        Expand(bounds, p0);
        Expand(bounds, p1);
        Expand(bounds, p2);
      }
    }
    cursor += size_t(c);
  }
  return true;
}

// A single mesh-extraction work item produced by the (serial) tree walk and
// consumed by the parallel mesh-stream workers.

// Serial: resolve world matrices / purpose (parent-dependent) and flatten the
// renderable GeomMesh prims into `jobs`. The per-triangle work happens later in
// parallel; this walk only does cheap per-prim xform/purpose evaluation.
void CollectRTPreviewMeshes(const tinyusdz::Prim &prim,
                            const matrix4d &parent_world, double time,
                            tinyusdz::Purpose inherited_purpose,
                            std::vector<MeshJob> *jobs) {
  bool reset = false;
  const matrix4d local =
      LocalMatrixOrIdentity(AsPreviewXformable(prim), time, &reset);
  const matrix4d world = reset ? local : (local * parent_world);
  const tinyusdz::Purpose purpose = ResolvePurpose(prim, inherited_purpose);
  if (const tinyusdz::GeomMesh *mesh = prim.as<tinyusdz::GeomMesh>()) {
    MeshJob job;
    job.mesh = mesh;
    job.world = world;
    job.purpose = purpose;
    job.prim_path = prim.absolute_path().full_path_name();
    jobs->push_back(std::move(job));
  }
  for (const tinyusdz::Prim &child : prim.children()) {
    CollectRTPreviewMeshes(child, world, time, purpose, jobs);
  }
}

void MergeStats(RTPreviewStats *dst, const RTPreviewStats &src) {
  dst->meshes_with_mmap_points += src.meshes_with_mmap_points;
  dst->meshes_with_owned_points += src.meshes_with_owned_points;
  dst->skipped_meshes += src.skipped_meshes;
  dst->triangles += src.triangles;
  dst->copied_point_bytes += src.copied_point_bytes;
  dst->copied_topology_bytes += src.copied_topology_bytes;
  dst->purpose_default_triangles += src.purpose_default_triangles;
  dst->purpose_render_triangles += src.purpose_render_triangles;
  dst->purpose_proxy_triangles += src.purpose_proxy_triangles;
  dst->purpose_guide_triangles += src.purpose_guide_triangles;
}

void MergeBounds(Bounds *dst, const Bounds &src) {
  if (!src.valid) return;
  Expand(dst, src.lo);
  Expand(dst, src.hi);
}

bool BuildRTPreviewScene(const tinyusdz::Stage &stage, const Options &opt,
                         std::vector<float> *vertices,
                         std::vector<TriInfo> *tris, Bounds *bounds,
                         RTPreviewStats *stats, std::string *err) {
  if (!vertices || !tris || !bounds || !stats) return false;
  vertices->clear();
  tris->clear();
  *bounds = Bounds();
  *stats = RTPreviewStats();
  if (stage.has_mmap_zero_copy()) {
    stats->mmap_deferred_bytes = stage.mmap_table()->total_deferred_bytes();
  }
  const auto t0 = std::chrono::steady_clock::now();

  // Pass A (serial, cheap): flatten the prim tree into per-mesh jobs.
  std::vector<MeshJob> jobs;
  for (const tinyusdz::Prim &root : stage.root_prims()) {
    CollectRTPreviewMeshes(root, matrix4d::identity(), opt.timecode,
                           tinyusdz::Purpose::Default, &jobs);
  }
  stats->meshes = jobs.size();

  // Pass B (parallel): extract + triangulate each mesh into its own result
  // buffer (disjoint writes, no locking). Work-stealing via an atomic cursor
  // balances the highly non-uniform per-mesh cost.
  struct MeshResult {
    std::vector<float> vertices;
    std::vector<TriInfo> tris;
    Bounds bounds;
    RTPreviewStats stats;
  };
  std::vector<MeshResult> results(jobs.size());
  const unsigned nthreads =
      std::min<unsigned>(WorkerThreadCount(opt.threads),
                         jobs.empty() ? 1u : unsigned(jobs.size()));
  std::atomic<size_t> cursor{0};
  auto worker = [&]() {
    for (;;) {
      const size_t i = cursor.fetch_add(1, std::memory_order_relaxed);
      if (i >= jobs.size()) break;
      const MeshJob &job = jobs[i];
      MeshResult &r = results[i];
      AddRTPreviewMesh(stage, job.prim_path, *job.mesh, job.world, opt.timecode,
                       job.purpose, opt.purpose_mask, &r.vertices, &r.tris,
                       &r.bounds, &r.stats);
    }
  };
  if (nthreads <= 1) {
    worker();
  } else {
    std::vector<std::thread> pool;
    pool.reserve(nthreads);
    for (unsigned t = 0; t < nthreads; ++t) pool.emplace_back(worker);
    for (std::thread &th : pool) th.join();
  }

  // Pass C (serial merge): concatenate in job order for deterministic output,
  // freeing each chunk as we go to bound peak memory.
  size_t total_floats = 0;
  size_t total_tris = 0;
  for (const MeshResult &r : results) {
    total_floats += r.vertices.size();
    total_tris += r.tris.size();
  }
  vertices->reserve(total_floats);
  tris->reserve(total_tris);
  for (MeshResult &r : results) {
    vertices->insert(vertices->end(), r.vertices.begin(), r.vertices.end());
    tris->insert(tris->end(), r.tris.begin(), r.tris.end());
    MergeBounds(bounds, r.bounds);
    MergeStats(stats, r.stats);
    std::vector<float>().swap(r.vertices);
    std::vector<TriInfo>().swap(r.tris);
  }

  const auto t1 = std::chrono::steady_clock::now();
  stats->build_seconds = std::chrono::duration<double>(t1 - t0).count();
  stats->packed_triangle_bytes = uint64_t(vertices->size()) * sizeof(float);
  if (tris->empty()) {
    if (err) *err = "RT preview found no renderable Mesh triangles.";
    return false;
  }
  return true;
}

bool MatchPrimNameOrPath(const tinyusdz::Prim &prim, const std::string &query) {
  if (query.empty()) return true;
  const std::string path = prim.absolute_path().full_path_name();
  return path == query || prim.element_name() == query;
}

bool CameraFrameFromGeomCamera(const tinyusdz::Stage &stage,
                               const tinyusdz::GeomCamera &cam,
                               const matrix4d &world, double time,
                               CameraFrame *frame) {
  if (!frame) return false;
  float focal_length = 50.0f;
  float vertical_aperture = 15.2908f;
  float horizontal_aperture = 20.955f;
  tinyusdz::value::float2 clipping_range{0.1f, 1000000.0f};
  tinyusdz::GeomCamera::Projection projection =
      tinyusdz::GeomCamera::Projection::Perspective;
  EvalAnimFallback(stage, cam.focalLength, "focalLength", time, &focal_length);
  EvalAnimFallback(stage, cam.verticalAperture, "verticalAperture", time,
                   &vertical_aperture);
  EvalAnimFallback(stage, cam.horizontalAperture, "horizontalAperture", time,
                   &horizontal_aperture);
  EvalAnimFallback(stage, cam.clippingRange, "clippingRange", time,
                   &clipping_range);
  cam.projection.get_value().get_scalar(&projection);

  frame->origin =
      Vec3{float(world.m[3][0]), float(world.m[3][1]), float(world.m[3][2])};
  frame->right = Normalize(TransformVector(world, Vec3{1.0f, 0.0f, 0.0f}));
  frame->up = Normalize(TransformVector(world, Vec3{0.0f, 1.0f, 0.0f}));
  frame->forward = Normalize(TransformVector(world, Vec3{0.0f, 0.0f, -1.0f}));
  frame->yfov = 2.0f * std::atan(0.5f * vertical_aperture /
                                 std::max(1.0e-6f, focal_length));
  frame->xmag = horizontal_aperture;
  frame->ymag = vertical_aperture;
  frame->znear = std::max(1.0e-5f, clipping_range[0]);
  frame->zfar = std::max(frame->znear, clipping_range[1]);
  frame->ortho = projection == tinyusdz::GeomCamera::Projection::Orthographic;
  return true;
}

bool FindStageCameraFrameRecursive(const tinyusdz::Stage &stage,
                                   const tinyusdz::Prim &prim,
                                   const std::string &query,
                                   const matrix4d &parent_world, double time,
                                   CameraFrame *frame) {
  bool reset = false;
  const matrix4d local =
      LocalMatrixOrIdentity(AsPreviewXformable(prim), time, &reset);
  const matrix4d world = reset ? local : (local * parent_world);
  if (const tinyusdz::GeomCamera *cam = prim.as<tinyusdz::GeomCamera>()) {
    if (MatchPrimNameOrPath(prim, query)) {
      return CameraFrameFromGeomCamera(stage, *cam, world, time, frame);
    }
  }
  for (const tinyusdz::Prim &child : prim.children()) {
    if (FindStageCameraFrameRecursive(stage, child, query, world, time, frame)) {
      return true;
    }
  }
  return false;
}

bool FindStageCameraFrame(const tinyusdz::Stage &stage, const std::string &query,
                          double time, CameraFrame *frame) {
  for (const tinyusdz::Prim &root : stage.root_prims()) {
    if (FindStageCameraFrameRecursive(stage, root, query, matrix4d::identity(),
                                      time, frame)) {
      return true;
    }
  }
  return false;
}

template <typename T>
bool EvalFallback(const tinyusdz::Stage &stage,
                  const tinyusdz::TypedAttributeWithFallback<T> &attr,
                  const std::string &name, T *out) {
  std::string err;
  return tinyusdz::tydra::EvaluateTypedAttribute(stage, attr, name, out, &err);
}

bool EvalAxis(const tinyusdz::TypedAttributeWithFallback<tinyusdz::Axis> &attr,
              tinyusdz::Axis *out) {
  if (!out) return false;
  *out = attr.get_value();
  return true;
}

std::string PrimPathString(const tinyusdz::Prim &prim) {
  return prim.absolute_path().full_path_name();
}

float ApproxScale(const matrix4d &m) {
  Vec3 sx = TransformVector(m, Vec3{1.0f, 0.0f, 0.0f});
  Vec3 sy = TransformVector(m, Vec3{0.0f, 1.0f, 0.0f});
  Vec3 sz = TransformVector(m, Vec3{0.0f, 0.0f, 1.0f});
  return std::max(1.0e-6f, (Length(sx) + Length(sy) + Length(sz)) / 3.0f);
}

void AddNurbsTriangle(const Vec3 &p0, const Vec3 &p1, const Vec3 &p2,
                      std::vector<float> *vertices, std::vector<TriInfo> *tris,
                      Bounds *bounds) {
  Vec3 n = Normalize(Cross(Sub(p1, p0), Sub(p2, p0)));
  TriInfo tri;
  tri.p0 = p0;
  tri.p1 = p1;
  tri.p2 = p2;
  tri.n = n;
  tri.base_color = Vec3{0.42f, 0.42f, 0.48f};
  vertices->insert(vertices->end(), {p0.x, p0.y, p0.z, p1.x, p1.y, p1.z,
                                     p2.x, p2.y, p2.z});
  tris->push_back(tri);
  Expand(bounds, p0);
  Expand(bounds, p1);
  Expand(bounds, p2);
}

void AddDirectTriangle(const Vec3 &p0, const Vec3 &p1, const Vec3 &p2,
                       const Vec3 &color, std::vector<float> *vertices,
                       std::vector<TriInfo> *tris, Bounds *bounds) {
  Vec3 n = Normalize(Cross(Sub(p1, p0), Sub(p2, p0)));
  TriInfo tri;
  tri.p0 = p0;
  tri.p1 = p1;
  tri.p2 = p2;
  tri.n = n;
  tri.base_color = color;
  vertices->insert(vertices->end(), {p0.x, p0.y, p0.z, p1.x, p1.y, p1.z,
                                     p2.x, p2.y, p2.z});
  tris->push_back(tri);
  Expand(bounds, p0);
  Expand(bounds, p1);
  Expand(bounds, p2);
}

void AddDirectCube(double size, const matrix4d &world, std::vector<float> *vertices,
                   std::vector<TriInfo> *tris, Bounds *bounds) {
  float h = float(size * 0.5);
  Vec3 p[8] = {
      TransformPoint(world, Vec3{-h, -h, -h}),
      TransformPoint(world, Vec3{ h, -h, -h}),
      TransformPoint(world, Vec3{ h,  h, -h}),
      TransformPoint(world, Vec3{-h,  h, -h}),
      TransformPoint(world, Vec3{-h, -h,  h}),
      TransformPoint(world, Vec3{ h, -h,  h}),
      TransformPoint(world, Vec3{ h,  h,  h}),
      TransformPoint(world, Vec3{-h,  h,  h}),
  };
  const int f[12][3] = {
      {0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7},
      {0, 1, 5}, {0, 5, 4}, {1, 2, 6}, {1, 6, 5},
      {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7},
  };
  for (const auto &tri : f) {
    AddDirectTriangle(p[tri[0]], p[tri[1]], p[tri[2]],
                      Vec3{0.46f, 0.50f, 0.56f}, vertices, tris, bounds);
  }
}

void AddDirectPlane(double width, double length, tinyusdz::Axis axis,
                    const matrix4d &world, std::vector<float> *vertices,
                    std::vector<TriInfo> *tris, Bounds *bounds) {
  float hw = float(width * 0.5);
  float hl = float(length * 0.5);
  Vec3 local[4];
  if (axis == tinyusdz::Axis::X) {
    local[0] = Vec3{0.0f, -hw, -hl};
    local[1] = Vec3{0.0f,  hw, -hl};
    local[2] = Vec3{0.0f,  hw,  hl};
    local[3] = Vec3{0.0f, -hw,  hl};
  } else if (axis == tinyusdz::Axis::Y) {
    local[0] = Vec3{-hw, 0.0f, -hl};
    local[1] = Vec3{ hw, 0.0f, -hl};
    local[2] = Vec3{ hw, 0.0f,  hl};
    local[3] = Vec3{-hw, 0.0f,  hl};
  } else {
    local[0] = Vec3{-hw, -hl, 0.0f};
    local[1] = Vec3{ hw, -hl, 0.0f};
    local[2] = Vec3{ hw,  hl, 0.0f};
    local[3] = Vec3{-hw,  hl, 0.0f};
  }
  Vec3 p0 = TransformPoint(world, local[0]);
  Vec3 p1 = TransformPoint(world, local[1]);
  Vec3 p2 = TransformPoint(world, local[2]);
  Vec3 p3 = TransformPoint(world, local[3]);
  AddDirectTriangle(p0, p1, p2, Vec3{0.38f, 0.55f, 0.44f}, vertices, tris, bounds);
  AddDirectTriangle(p0, p2, p3, Vec3{0.38f, 0.55f, 0.44f}, vertices, tris, bounds);
}

double BSplineBasis(int i, int degree, double u, const std::vector<double> &knots) {
  if (degree == 0) {
    const bool last = (i + 1 == int(knots.size()) - 1) && (u == knots.back());
    return ((knots[size_t(i)] <= u && u < knots[size_t(i + 1)]) || last) ? 1.0 : 0.0;
  }
  double left = 0.0;
  double denom_l = knots[size_t(i + degree)] - knots[size_t(i)];
  if (std::abs(denom_l) > 1.0e-14) {
    left = (u - knots[size_t(i)]) / denom_l *
           BSplineBasis(i, degree - 1, u, knots);
  }
  double right = 0.0;
  double denom_r = knots[size_t(i + degree + 1)] - knots[size_t(i + 1)];
  if (std::abs(denom_r) > 1.0e-14) {
    right = (knots[size_t(i + degree + 1)] - u) / denom_r *
            BSplineBasis(i + 1, degree - 1, u, knots);
  }
  return left + right;
}

Vec3 EvalNurbsPatchPoint(const std::vector<tinyusdz::value::point3f> &points,
                         const std::vector<double> &weights, int u_count,
                         int v_count, int u_order, int v_order,
                         const std::vector<double> &u_knots,
                         const std::vector<double> &v_knots, double u, double v) {
  Vec3 sum{0.0f, 0.0f, 0.0f};
  double wsum = 0.0;
  int u_degree = std::max(0, u_order - 1);
  int v_degree = std::max(0, v_order - 1);
  for (int j = 0; j < v_count; j++) {
    double bv = BSplineBasis(j, v_degree, v, v_knots);
    if (bv == 0.0) continue;
    for (int i = 0; i < u_count; i++) {
      double bu = BSplineBasis(i, u_degree, u, u_knots);
      if (bu == 0.0) continue;
      size_t idx = size_t(j) * size_t(u_count) + size_t(i);
      double w = idx < weights.size() ? weights[idx] : 1.0;
      double b = bu * bv * w;
      Vec3 p = FromPoint3(points[idx]);
      sum = Add(sum, Mul(p, float(b)));
      wsum += b;
    }
  }
  if (std::abs(wsum) > 1.0e-20) {
    sum = Mul(sum, float(1.0 / wsum));
  }
  return sum;
}

void AddNurbsPatchTriangles(const tinyusdz::Stage &stage,
                            const tinyusdz::GeomNurbsPatch &patch,
                            const matrix4d &world, double time,
                            std::vector<float> *vertices,
                            std::vector<TriInfo> *tris, Bounds *bounds) {
  std::vector<tinyusdz::value::point3f> points;
  int u_count = 0, v_count = 0, u_order = 0, v_order = 0;
  std::vector<double> u_knots, v_knots, weights;
  if (!EvalAnim(stage, patch.points, "points", time, &points) ||
      !EvalAnim(stage, patch.uVertexCount, "uVertexCount", time, &u_count) ||
      !EvalAnim(stage, patch.vVertexCount, "vVertexCount", time, &v_count) ||
      !EvalAnim(stage, patch.uOrder, "uOrder", time, &u_order) ||
      !EvalAnim(stage, patch.vOrder, "vOrder", time, &v_order) ||
      !EvalAnim(stage, patch.uKnots, "uKnots", time, &u_knots) ||
      !EvalAnim(stage, patch.vKnots, "vKnots", time, &v_knots)) {
    return;
  }
  EvalAnim(stage, patch.pointWeights, "pointWeights", time, &weights);
  if (u_count <= 0 || v_count <= 0 ||
      points.size() < size_t(u_count) * size_t(v_count)) {
    return;
  }
  double u0 = u_knots[size_t(std::max(0, u_order - 1))];
  double u1 = u_knots[u_knots.size() - size_t(std::max(1, u_order))];
  double v0 = v_knots[size_t(std::max(0, v_order - 1))];
  double v1 = v_knots[v_knots.size() - size_t(std::max(1, v_order))];
  tinyusdz::value::double2 range;
  if (EvalAnim(stage, patch.uRange, "uRange", time, &range)) {
    u0 = range[0];
    u1 = range[1];
  }
  if (EvalAnim(stage, patch.vRange, "vRange", time, &range)) {
    v0 = range[0];
    v1 = range[1];
  }
  constexpr int divs = 24;
  std::vector<Vec3> grid(size_t(divs + 1) * size_t(divs + 1));
  for (int y = 0; y <= divs; y++) {
    double v = v0 + (v1 - v0) * double(y) / double(divs);
    for (int x = 0; x <= divs; x++) {
      double u = u0 + (u1 - u0) * double(x) / double(divs);
      grid[size_t(y) * size_t(divs + 1) + size_t(x)] =
          TransformPoint(world, EvalNurbsPatchPoint(points, weights, u_count,
                                                    v_count, u_order, v_order,
                                                    u_knots, v_knots, u, v));
    }
  }
  for (int y = 0; y < divs; y++) {
    for (int x = 0; x < divs; x++) {
      Vec3 p00 = grid[size_t(y) * size_t(divs + 1) + size_t(x)];
      Vec3 p10 = grid[size_t(y) * size_t(divs + 1) + size_t(x + 1)];
      Vec3 p01 = grid[size_t(y + 1) * size_t(divs + 1) + size_t(x)];
      Vec3 p11 = grid[size_t(y + 1) * size_t(divs + 1) + size_t(x + 1)];
      AddNurbsTriangle(p00, p10, p11, vertices, tris, bounds);
      AddNurbsTriangle(p00, p11, p01, vertices, tris, bounds);
    }
  }
}

// Fixed base color for unmaterialed curve geometry (the `next` path doesn't
// resolve curve displayColor yet). One value for every hair segment.

void AppendLinearCurveStrands(const std::vector<tinyusdz::value::point3f> &points,
                              const std::vector<int> &counts,
                              const std::vector<float> &widths,
                              const matrix4d &world,
                              std::vector<float> *curve_points,
                              std::vector<float> *curve_radii,
                              std::vector<uint32_t> *first,
                              std::vector<uint32_t> *count,
                              std::vector<TriInfo> *info,
                              Bounds *bounds) {
  size_t cursor = 0;
  for (int c : counts) {
    if (c < 2 || cursor + size_t(c) > points.size()) {
      cursor += size_t(std::max(0, c));
      continue;
    }
    first->push_back(uint32_t(curve_points->size() / 3));
    count->push_back(uint32_t(c));
    for (int i = 0; i < c; i++) {
      size_t idx = cursor + size_t(i);
      Vec3 p = TransformPoint(world, FromPoint3(points[idx]));
      float radius = 0.5f * ((idx < widths.size()) ? widths[idx] : 0.01f);
      curve_points->insert(curve_points->end(), {p.x, p.y, p.z});
      curve_radii->push_back(std::max(1.0e-5f, radius * ApproxScale(world)));
      Expand(bounds, p);
    }
    // Per-segment TriInfo: only for the DirectScene curve path (info != null).
    // The instanced curve BLAS passes info == null and derives its slim per-
    // segment endpoints straight from curve_points (kCurveColor is the material),
    // skipping this 120 B/segment intermediate entirely.
    if (info) {
      for (int i = 0; i + 1 < c; i++) {
        TriInfo ti;
        size_t point_base = size_t(first->back()) + size_t(i);
        Vec3 p0{(*curve_points)[point_base * 3 + 0],
                (*curve_points)[point_base * 3 + 1],
                (*curve_points)[point_base * 3 + 2]};
        Vec3 p1{(*curve_points)[(point_base + 1) * 3 + 0],
                (*curve_points)[(point_base + 1) * 3 + 1],
                (*curve_points)[(point_base + 1) * 3 + 2]};
        ti.p0 = p0;
        ti.p1 = p1;
        ti.p2 = Add(p0, Vec3{0.0f, 1.0f, 0.0f});
        ti.base_color = kCurveColor;
        info->push_back(ti);
      }
    }
    cursor += size_t(c);
  }
}

void TraverseDirectPrims(const tinyusdz::Stage &stage, const tinyusdz::Prim &prim,
                         const std::unordered_map<std::string, matrix4d> &matrices,
                         double time, DirectScene *direct,
                         std::vector<float> *vertices, std::vector<TriInfo> *tris,
                         Bounds *bounds, std::vector<float> *sphere_data,
                         std::vector<float> *round_points,
                         std::vector<float> *round_radii,
                         std::vector<uint32_t> *round_first,
                         std::vector<uint32_t> *round_count,
                         std::vector<float> *flat_points,
                         std::vector<float> *flat_radii,
                         std::vector<uint32_t> *flat_first,
                         std::vector<uint32_t> *flat_count,
                         std::vector<float> *point_centers,
                         std::vector<float> *point_radii,
                         std::vector<float> *bez_cps,
                         std::vector<float> *tet_aabbs) {
  const std::string path = PrimPathString(prim);
  const matrix4d world = MatrixForPath(matrices, path);
  matrix4d inv_world;
  bool has_inv = tinyusdz::inverse(world, inv_world, 1.0e-12);

  if (const tinyusdz::GeomSphere *sphere = prim.as<tinyusdz::GeomSphere>()) {
    double radius = 2.0;
    EvalAnimFallback(stage, sphere->radius, "radius", time, &radius);
    Vec3 c = TransformPoint(world, Vec3{0.0f, 0.0f, 0.0f});
    float r = float(radius) * ApproxScale(world);
    sphere_data->insert(sphere_data->end(), {c.x, c.y, c.z, r});
    TriInfo ti;
    ti.p0 = c;
    ti.base_color = Vec3{0.35f, 0.48f, 0.80f};
    direct->sphere_info.push_back(ti);
    direct->direct_paths.insert(path);
    Expand(bounds, Add(c, Vec3{r, r, r}));
    Expand(bounds, Sub(c, Vec3{r, r, r}));
  } else if (has_inv) {
    DirectShape shape;
    bool add_shape = false;
    if (const tinyusdz::GeomCylinder *cyl = prim.as<tinyusdz::GeomCylinder>()) {
      shape.type = DirectShape::Type::Cylinder;
      EvalAnimFallback(stage, cyl->radius, "radius", time, &shape.radius);
      EvalAnimFallback(stage, cyl->height, "height", time, &shape.height);
      EvalAxis(cyl->axis, &shape.axis);
      add_shape = true;
    } else if (const tinyusdz::GeomCone *cone = prim.as<tinyusdz::GeomCone>()) {
      shape.type = DirectShape::Type::Cone;
      EvalAnimFallback(stage, cone->radius, "radius", time, &shape.radius);
      EvalAnimFallback(stage, cone->height, "height", time, &shape.height);
      EvalAxis(cone->axis, &shape.axis);
      add_shape = true;
    } else if (const tinyusdz::GeomCapsule *cap = prim.as<tinyusdz::GeomCapsule>()) {
      shape.type = DirectShape::Type::Capsule;
      EvalAnimFallback(stage, cap->radius, "radius", time, &shape.radius);
      EvalAnimFallback(stage, cap->height, "height", time, &shape.height);
      EvalAxis(cap->axis, &shape.axis);
      add_shape = true;
    }
    if (add_shape) {
      shape.world = world;
      shape.inv_world = inv_world;
      direct->shapes.push_back(shape);
      direct->direct_paths.insert(path);
      float e = float(std::max(shape.height * 0.5 + shape.radius, shape.radius)) *
                ApproxScale(world);
      Vec3 c = TransformPoint(world, Vec3{0.0f, 0.0f, 0.0f});
      Expand(bounds, Add(c, Vec3{e, e, e}));
      Expand(bounds, Sub(c, Vec3{e, e, e}));
    }
  }

  if (const tinyusdz::GeomCube *cube = prim.as<tinyusdz::GeomCube>()) {
    double size = 2.0;
    EvalAnimFallback(stage, cube->size, "size", time, &size);
    AddDirectCube(size, world, vertices, tris, bounds);
    direct->direct_paths.insert(path);
  } else if (const tinyusdz::GeomPlane *plane = prim.as<tinyusdz::GeomPlane>()) {
    double width = 2.0;
    double length = 2.0;
    tinyusdz::Axis axis = tinyusdz::Axis::Z;
    EvalAnimFallback(stage, plane->width, "width", time, &width);
    EvalAnimFallback(stage, plane->length, "length", time, &length);
    EvalAxis(plane->axis, &axis);
    AddDirectPlane(width, length, axis, world, vertices, tris, bounds);
    direct->direct_paths.insert(path);
  } else if (const tinyusdz::GeomPoints *pts = prim.as<tinyusdz::GeomPoints>()) {
    std::vector<tinyusdz::value::point3f> points;
    std::vector<float> widths;
    if (EvalAnim(stage, pts->points, "points", time, &points)) {
      EvalAnim(stage, pts->widths, "widths", time, &widths);
      for (size_t i = 0; i < points.size(); i++) {
        Vec3 p = TransformPoint(world, FromPoint3(points[i]));
        float radius = 0.5f * ((i < widths.size()) ? widths[i] : 0.05f) *
                       ApproxScale(world);
        radius = std::max(1.0e-5f, radius);
        point_centers->insert(point_centers->end(), {p.x, p.y, p.z});
        point_radii->push_back(radius);
        TriInfo ti;
        ti.p0 = p;
        ti.base_color = Vec3{0.90f, 0.72f, 0.26f};
        direct->point_info.push_back(ti);
        Expand(bounds, Add(p, Vec3{radius, radius, radius}));
        Expand(bounds, Sub(p, Vec3{radius, radius, radius}));
      }
      direct->direct_paths.insert(path);
    }
  } else if (const tinyusdz::GeomTetMesh *tet = prim.as<tinyusdz::GeomTetMesh>()) {
    std::vector<tinyusdz::value::point3f> points;
    std::vector<tinyusdz::value::int4> indices;
    if (EvalAnim(stage, tet->points, "points", time, &points) &&
        EvalAnim(stage, tet->tetVertexIndices, "tetVertexIndices", time, &indices)) {
      for (const auto &idx : indices) {
        if (idx[0] < 0 || idx[1] < 0 || idx[2] < 0 || idx[3] < 0 ||
            size_t(idx[0]) >= points.size() || size_t(idx[1]) >= points.size() ||
            size_t(idx[2]) >= points.size() || size_t(idx[3]) >= points.size()) {
          continue;
        }
        TetPrim tp;
        tp.p[0] = TransformPoint(world, FromPoint3(points[size_t(idx[0])]));
        tp.p[1] = TransformPoint(world, FromPoint3(points[size_t(idx[1])]));
        tp.p[2] = TransformPoint(world, FromPoint3(points[size_t(idx[2])]));
        tp.p[3] = TransformPoint(world, FromPoint3(points[size_t(idx[3])]));
        Vec3 lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()};
        Vec3 hi{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                -std::numeric_limits<float>::max()};
        for (const Vec3 &p : tp.p) {
          lo.x = std::min(lo.x, p.x);
          lo.y = std::min(lo.y, p.y);
          lo.z = std::min(lo.z, p.z);
          hi.x = std::max(hi.x, p.x);
          hi.y = std::max(hi.y, p.y);
          hi.z = std::max(hi.z, p.z);
          Expand(bounds, p);
        }
        tet_aabbs->insert(tet_aabbs->end(), {lo.x, lo.y, lo.z, hi.x, hi.y, hi.z});
        direct->tet_prims.push_back(tp);
      }
      direct->direct_paths.insert(path);
    }
  }

  if (const tinyusdz::GeomBasisCurves *curves = prim.as<tinyusdz::GeomBasisCurves>()) {
    std::vector<tinyusdz::value::point3f> points;
    std::vector<int> counts;
    std::vector<float> widths;
    if (EvalAnim(stage, curves->points, "points", time, &points) &&
        EvalAnim(stage, curves->curveVertexCounts, "curveVertexCounts", time, &counts)) {
      EvalAnim(stage, curves->widths, "widths", time, &widths);
      if (curves->normals.authored()) {
        AppendLinearCurveStrands(points, counts, widths, world, flat_points,
                                 flat_radii, flat_first, flat_count,
                                 &direct->flat_curve_info, bounds);
      } else {
        AppendLinearCurveStrands(points, counts, widths, world, round_points,
                                 round_radii, round_first, round_count,
                                 &direct->round_curve_info, bounds);
      }
      direct->direct_paths.insert(path);
    }
  } else if (const tinyusdz::GeomNurbsCurves *curves = prim.as<tinyusdz::GeomNurbsCurves>()) {
    std::vector<tinyusdz::value::point3f> points;
    std::vector<int> counts;
    std::vector<float> widths;
    if (EvalAnim(stage, curves->points, "points", time, &points) &&
        EvalAnim(stage, curves->curveVertexCounts, "curveVertexCounts", time, &counts)) {
      EvalAnim(stage, curves->widths, "widths", time, &widths);
      AppendLinearCurveStrands(points, counts, widths, world, round_points,
                               round_radii, round_first, round_count,
                               &direct->round_curve_info, bounds);
      direct->direct_paths.insert(path);
    }
  } else if (const tinyusdz::GeomNurbsPatch *patch = prim.as<tinyusdz::GeomNurbsPatch>()) {
    AddNurbsPatchTriangles(stage, *patch, world, time, vertices, tris, bounds);
    direct->direct_paths.insert(path);
  } else if (const tinyusdz::GeomHermiteCurves *curves = prim.as<tinyusdz::GeomHermiteCurves>()) {
    std::vector<tinyusdz::value::point3f> points;
    std::vector<tinyusdz::value::vector3f> tangents;
    std::vector<int> counts;
    std::vector<float> widths;
    if (EvalAnim(stage, curves->points, "points", time, &points) &&
        EvalAnim(stage, curves->curveVertexCounts, "curveVertexCounts", time, &counts) &&
        EvalAnim(stage, curves->tangents, "tangents", time, &tangents)) {
      EvalAnim(stage, curves->widths, "widths", time, &widths);
      size_t cursor = 0;
      for (int c : counts) {
        if (c < 2 || cursor + size_t(c) > points.size() ||
            cursor + size_t(c) > tangents.size()) {
          cursor += size_t(std::max(0, c));
          continue;
        }
        for (int i = 0; i + 1 < c; i++) {
          size_t i0 = cursor + size_t(i);
          size_t i1 = i0 + 1;
          Vec3 p0 = TransformPoint(world, FromPoint3(points[i0]));
          Vec3 p1 = TransformPoint(world, FromPoint3(points[i1]));
          Vec3 t0 = TransformVector(world, FromVector3(tangents[i0]));
          Vec3 t1 = TransformVector(world, FromVector3(tangents[i1]));
          float r0 = 0.5f * ((i0 < widths.size()) ? widths[i0] : 0.01f) *
                     ApproxScale(world);
          float r1 = 0.5f * ((i1 < widths.size()) ? widths[i1] : 0.01f) *
                     ApproxScale(world);
          r0 = std::max(1.0e-5f, r0);
          r1 = std::max(1.0e-5f, r1);
          Vec3 b0 = p0;
          Vec3 b1 = Add(p0, Mul(t0, 1.0f / 3.0f));
          Vec3 b2 = Sub(p1, Mul(t1, 1.0f / 3.0f));
          Vec3 b3 = p1;
          bez_cps->insert(bez_cps->end(),
                          {b0.x, b0.y, b0.z, r0, b1.x, b1.y, b1.z, r0,
                           b2.x, b2.y, b2.z, r1, b3.x, b3.y, b3.z, r1});
          TriInfo ti;
          ti.p0 = p0;
          ti.p1 = p1;
          ti.p2 = Add(p0, Vec3{0.0f, 1.0f, 0.0f});
          ti.base_color = Vec3{0.72f, 0.45f, 0.28f};
          direct->bez_curve_info.push_back(ti);
          Expand(bounds, p0);
          Expand(bounds, p1);
        }
        cursor += size_t(c);
      }
      direct->direct_paths.insert(path);
    }
  }

  for (const tinyusdz::Prim &child : prim.children()) {
    TraverseDirectPrims(stage, child, matrices, time, direct, vertices, tris,
                        bounds, sphere_data, round_points, round_radii,
                        round_first, round_count, flat_points, flat_radii,
                        flat_first, flat_count, point_centers, point_radii,
                        bez_cps, tet_aabbs);
  }
}

bool BuildDirectScene(const tinyusdz::Stage &stage, const RenderScene &render_scene,
                      const Options &opt, std::vector<float> *vertices,
                      std::vector<TriInfo> *tris, Bounds *bounds,
                      DirectScene *direct, std::string *err) {
  if (!direct || !vertices || !tris || !bounds) return false;
  std::vector<float> sphere_data;
  std::vector<float> round_points, round_radii, flat_points, flat_radii;
  std::vector<float> point_centers, point_radii;
  std::vector<float> bez_cps;
  std::vector<float> tet_aabbs;
  std::vector<uint32_t> round_first, round_count, flat_first, flat_count;
  std::unordered_map<std::string, matrix4d> matrices = BuildNodeMatrixMap(render_scene);
  for (const tinyusdz::Prim &root : stage.root_prims()) {
    TraverseDirectPrims(stage, root, matrices, opt.timecode, direct, vertices,
                        tris, bounds, &sphere_data, &round_points, &round_radii,
                        &round_first, &round_count, &flat_points, &flat_radii,
                        &flat_first, &flat_count, &point_centers, &point_radii,
                        &bez_cps, &tet_aabbs);
  }

  lrt_tri_build_options build_opts;
  std::memset(&build_opts, 0, sizeof(build_opts));
  build_opts.quality = opt.quality;
  build_opts.layout = LRT_TRI_LAYOUT_AUTO;
  build_opts.num_threads = WorkerThreadCount(opt.threads);
  lrt_result lrt_err = LRT_RESULT_OK;
  if (!sphere_data.empty()) {
    direct->spheres.reset(
        lrt_sphere_scene_build(sphere_data.data(), sphere_data.size() / 4,
                               &build_opts, &lrt_err));
    if (!direct->spheres) {
      if (err) *err = "Failed to build LightRT sphere scene.";
      return false;
    }
  }
  if (!round_first.empty()) {
    lrt_hair_strands strands;
    std::memset(&strands, 0, sizeof(strands));
    strands.points = round_points.data();
    strands.radius = round_radii.data();
    strands.strand_first = round_first.data();
    strands.strand_count = round_count.data();
    strands.nstrands = round_first.size();
    strands.npoints = round_radii.size();
    direct->round_curves.reset(
        lrt_roundcurve_scene_build(&strands, &build_opts, &lrt_err));
    if (!direct->round_curves) {
      if (err) *err = "Failed to build LightRT round curve scene.";
      return false;
    }
  }
  if (!flat_first.empty()) {
    lrt_hair_strands strands;
    std::memset(&strands, 0, sizeof(strands));
    strands.points = flat_points.data();
    strands.radius = flat_radii.data();
    strands.strand_first = flat_first.data();
    strands.strand_count = flat_count.data();
    strands.nstrands = flat_first.size();
    strands.npoints = flat_radii.size();
    direct->flat_curves.reset(
        lrt_flatcurve_scene_build(&strands, &build_opts, &lrt_err));
    if (!direct->flat_curves) {
      if (err) *err = "Failed to build LightRT flat curve scene.";
      return false;
    }
  }
  if (!point_centers.empty()) {
    direct->points.reset(
        lrt_points_scene_build(point_centers.data(), point_radii.data(), nullptr,
                               LRT_POINT_SPHERE, point_radii.size(),
                               &build_opts, &lrt_err));
    if (!direct->points) {
      if (err) *err = "Failed to build LightRT points scene.";
      return false;
    }
  }
  if (!bez_cps.empty()) {
    direct->bez_curves.reset(
        lrt_bezcurve_scene_build(bez_cps.data(), bez_cps.size() / 16,
                                 &build_opts, &lrt_err));
    if (!direct->bez_curves) {
      if (err) *err = "Failed to build LightRT Hermite/Bezier curve scene.";
      return false;
    }
  }
  if (!tet_aabbs.empty()) {
    direct->tets.reset(lrt_user_scene_build(
        tet_aabbs.data(), direct->tet_prims.size(), TetUserIntersect,
        TetUserOccluded, &direct->tet_prims, &build_opts, &lrt_err));
    if (!direct->tets) {
      if (err) *err = "Failed to build LightRT TetMesh user scene.";
      return false;
    }
  }
  return true;
}

bool FindCameraNode(const RenderScene &scene, const Node &node,
                    const std::string &query, const Node **node_out) {
  if (node.nodeType == NodeType::Camera && node.id >= 0 &&
      size_t(node.id) < scene.cameras.size()) {
    const RenderCamera &cam = scene.cameras[size_t(node.id)];
    if (query.empty() || node.abs_path == query || cam.abs_path == query ||
        cam.name == query || node.prim_name == query) {
      *node_out = &node;
      return true;
    }
  }
  for (const Node &child : node.children) {
    if (FindCameraNode(scene, child, query, node_out)) return true;
  }
  return false;
}

const Node *FindCameraNode(const RenderScene &scene, const std::string &query) {
  const Node *result = nullptr;
  for (const Node &root : scene.nodes) {
    if (FindCameraNode(scene, root, query, &result)) return result;
  }
  return nullptr;
}

CameraFrame MakeCameraFrame(const RenderScene &scene, const Options &opt,
                            const Bounds &bounds, int height,
                            tinyusdz::Axis up_axis) {
  CameraFrame frame;
  const Node *cam_node = FindCameraNode(scene, opt.camera);
  if (!cam_node && !opt.camera.empty()) {
    std::cerr << "WARN: Camera not found: " << opt.camera
              << ". Using auto-fit camera.\n";
  }
  if (cam_node) {
    const RenderCamera &cam = scene.cameras[size_t(cam_node->id)];
    const matrix4d &m = cam_node->global_matrix;
    frame.origin = Vec3{float(m.m[3][0]), float(m.m[3][1]), float(m.m[3][2])};
    frame.right = Normalize(TransformVector(m, Vec3{1.0f, 0.0f, 0.0f}));
    frame.up = Normalize(TransformVector(m, Vec3{0.0f, 1.0f, 0.0f}));
    frame.forward = Normalize(TransformVector(m, Vec3{0.0f, 0.0f, -1.0f}));
    frame.yfov = 2.0f * std::atan(0.5f * cam.verticalAperture /
                                  std::max(1.0e-6f, cam.focalLength));
    frame.xmag = cam.xmag;
    frame.ymag = cam.ymag;
    frame.znear = std::max(1.0e-5f, cam.znear);
    frame.zfar = cam.zfar;
    frame.ortho = cam.projection == tinyusdz::GeomCamera::Projection::Orthographic;
    return frame;
  }

  Vec3 center{0.0f, 0.0f, 0.0f};
  float radius = 1.0f;
  if (bounds.valid) {
    center = Mul(Add(bounds.lo, bounds.hi), 0.5f);
    radius = std::max(0.001f, Length(Sub(bounds.hi, bounds.lo)) * 0.5f);
  }
  float aspect = (height > 0) ? float(opt.width) / float(height) : 16.0f / 9.0f;
  frame.yfov = 45.0f * 3.14159265358979323846f / 180.0f;
  float distance = radius / std::tan(frame.yfov * 0.5f);
  if (aspect < 1.0f) {
    distance /= aspect;
  }
  Vec3 up_axis_vec{0.0f, 1.0f, 0.0f};
  Vec3 view_dir{0.0f, 0.15f, 1.8f};
  if (up_axis == tinyusdz::Axis::Z) {
    up_axis_vec = Vec3{0.0f, 0.0f, 1.0f};
    view_dir = Normalize(Vec3{-0.95f, -1.15f, 0.62f});
  } else if (up_axis == tinyusdz::Axis::X) {
    up_axis_vec = Vec3{1.0f, 0.0f, 0.0f};
    view_dir = Normalize(Vec3{0.62f, -0.95f, -1.15f});
  }
  if (opt.has_view_dir) {
    view_dir = Normalize(opt.view_dir);
  }
  frame.origin = Add(center, Mul(view_dir, distance * opt.fit_scale));
  frame.forward = Normalize(Sub(center, frame.origin));
  frame.right = Normalize(Cross(frame.forward, up_axis_vec));
  if (Length(frame.right) < 1.0e-6f) {
    frame.right = Vec3{1.0f, 0.0f, 0.0f};
  }
  frame.up = Normalize(Cross(frame.right, frame.forward));
  frame.znear = std::max(1.0e-4f, distance * 0.001f);
  frame.zfar = std::max(1000.0f, distance * 10.0f);
  return frame;
}




// ===========================================================================
// `next` lazy-loader RT preview backend (default for USDC inputs).
//
// Loads the USDC with the experimental `next` reader (fast, low-memory, lazy
// arrays) and streams triangles using tydra_next's bit-exact world transforms.
// Produces the byte-identical triangle stream of the legacy path (validated:
// matching per-purpose triangle counts on large scenes). Falls back to the
// legacy eager loader for non-USDC inputs or when -legacyLoad is given.
// ===========================================================================

matrix4d Mat4FromArray(const double d[16]) {
  matrix4d m;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) m.m[i][j] = d[i * 4 + j];
  return m;
}

// Read an array attribute, decoding lazily-stored arrays into a throwaway temp
// (materialized_copy) so the `next` stage's Value stays lazy. This keeps the
// big geometry arrays (points/indices/normals) from being permanently
// materialized into the stage as we stream the scene, bounding peak memory.
// `time` is NaN for the default value, or a frame time for animated (time-
// sampled) arrays (held to the nearest authored sample). Decoding goes through
// materialized_copy so the stage's Value stays lazy.


}  // namespace tusdr

// The Vulkan backend and main() below live in the global namespace; pull in the
// tusdr names they use (Vec3, Options, RTPreviewStats, qjs::*, ...).
using namespace tusdr;

// ---------------------------------------------------------------------------
// LightRT Vulkan backend: uses the LightRT C API (lightrt_c_vk.h) for GPU
// BVH traversal. Builds the scene with the existing CPU builder, uploads to
// GPU, traces camera rays, then shades hits on the CPU.
// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
#if defined(__GLIBC__)
  // Triangle streaming allocates and frees large transient per-job buffers
  // (megabytes each) while the final geometry buffers stay live. glibc's default
  // dynamic mmap threshold (which grows up to 32 MB) keeps those big frees in the
  // arena interleaved with live data, so they cannot be returned to the OS and
  // inflate peak RSS by ~1.3 GB on Island-scale scenes (isCoral 6.4 -> 5.0 GB).
  // Pinning the mmap threshold at 1 MB routes the large temporaries through
  // mmap/munmap (returned to the OS on free); only sub-MB allocations stay in the
  // arena, so the per-call syscall cost is negligible (~0.2 s on isCoral).
  mallopt(M_MMAP_THRESHOLD, 1 << 20);
  mallopt(M_TRIM_THRESHOLD, 4 << 20);
  // Grow each (sub-MB) arena in 16 MB chunks. The parallel compose allocates
  // ~140K prims' property storage; with the default minimal top-pad, glibc
  // commits a fresh page almost per prim -- ~139K mprotect() calls on isCoral
  // (97% of all syscall time, serialized under the kernel mmap_lock, which caps
  // the parallel fill near ~4 cores). A 16 MB pad commits arena memory in bulk so
  // those calls collapse to a few hundred; isCoral load 3.85 -> 3.48 s. Sized to
  // hold peak RSS under budget (128 MB pad inflated it ~400 MB; 16 MB is +~50 MB).
  mallopt(M_TOP_PAD, 16 << 20);
#endif
  Options opt;
  if (!ParseArgs(argc, argv, &opt)) {
    return EXIT_FAILURE;
  }

  // Configure the process memory budget: -maxMem <GiB>, else auto
  // min(32 GiB, 0.5 * system MemAvailable). Keeps tusdrender from being
  // OOM-killed on huge (instance-expanded) scenes; it aborts with a clear
  // message instead.
  MemBudget::Get().Init(opt.max_mem_gib);
  if (opt.stats) {
    std::cerr << "memory cap: " << MemBudget::GiB(MemBudget::Get().Cap());
    size_t avail = MemBudget::AvailableSystemMemory();
    if (avail) std::cerr << " (system avail: " << MemBudget::GiB(avail) << ")";
    std::cerr << "\n";
  }

  // Interactive / scripted modes (memory-persistent rendering over the next
  // loader): -js runs a JavaScript animation/control script, -mcp runs an MCP
  // stdio control server. Both keep the scene + BVH resident for repeated
  // re-rendering.
  if (opt.mcp || !opt.js_script.empty()) {
#ifdef TINYUSDZ_WITH_QJS
    if (opt.mcp) return RunMCPMode(opt);
    return RunJSScriptMode(opt, opt.js_script);
#else
    std::cerr << "-js/-mcp require building with -DTINYUSDZ_WITH_QJS=ON.\n";
    return EXIT_FAILURE;
#endif
  }

  // RT preview backend: the `next` lazy loader (fast, low-memory compose +
  // mmap USDC; also handles .usdz + .usda). Falls back to the legacy eager
  // loader for other inputs or when -legacyLoad is requested.
  if (opt.rt_preview && !opt.legacy_load) {
    return RunRTPreviewNext(opt);
  }

#ifdef HAVE_VULKAN
  // Vulkan backend: loads the scene through the `next` lazy loader, then
  // renders via Vulkan rasterizer or ray tracer.
  if (opt.vulkan) {
    // Load through next loader.
    tinyusdz::next::Stage stage;
    std::string warn, err;
    tinyusdz::next::pcp::CompositionOptions comp_opts;
    if (!opt.variant_overrides.empty())
      comp_opts.variant_overrides = opt.variant_overrides;
    if (!tinyusdz::next::LoadUSDComposed(opt.input, &stage, &warn, &err,
                                         &comp_opts)) {
      if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
      std::cerr << "Failed to load USD: " << err << "\n";
      return EXIT_FAILURE;
    }
    if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";

    // Collect meshes and build geometry.
    std::vector<tinyusdz::next::UsdPrim> mesh_prims;
    std::vector<matrix4d> worlds;
    std::vector<Vec3> base_colors;
    std::vector<int32_t> tex_ids;
    std::vector<float> roughnesses;
    std::vector<float> metallics;
    std::vector<RTPreviewStats::MeshGeometry> geos;

    {
      // Traverse and collect meshes.
      std::vector<tinyusdz::next::UsdPrim> mesh_stack;
      for (const auto &root : stage.GetRootPrims()) {
        std::vector<tinyusdz::next::UsdPrim> stack;
        stack.push_back(root);
        while (!stack.empty()) {
          auto prim = stack.back();
          stack.pop_back();
          if (prim.GetTypeName() == "Mesh") {
            mesh_stack.push_back(prim);
            Vec3 bc{0.5f, 0.5f, 0.5f};
            // Try to get displayColor.
            const tinyusdz::next::Value *dcv = prim.GetPropertyValue("primvars:displayColor");
            if (dcv) {
              const std::vector<float> *dc = dcv->as_float_array();
              if (dc && dc->size() >= 3) {
                bc = Vec3{(*dc)[0], (*dc)[1], (*dc)[2]};
              }
            }
            base_colors.push_back(bc);
            tex_ids.push_back(-1);
            roughnesses.push_back(0.5f);
            metallics.push_back(0.0f);
          }
          for (const auto &child : prim.GetChildren()) {
            stack.push_back(child);
          }
        }
      }

      // Stream geometry.
      for (auto &prim : mesh_stack) {
        RTPreviewStats::MeshGeometry geo;
        uint32_t nv = 0;
        const tinyusdz::next::Value *val = prim.GetPropertyValue("points");
        if (!val) continue;
        const std::vector<float> *pts = val->as_float_array();
        if (!pts || pts->empty()) continue;
        nv = uint32_t(pts->size() / 3);
        geo.positions = *pts;

        val = prim.GetPropertyValue("normals");
        if (val) {
          const std::vector<float> *nrm = val->as_float_array();
          if (nrm && nrm->size() >= nv * 3)
            geo.normals = *nrm;
        }
        if (geo.normals.empty()) {
          geo.normals.resize(nv * 3, 0);
        }

        val = prim.GetPropertyValue("primvars:st");
        if (val) {
          const std::vector<float> *uv = val->as_float_array();
          if (uv && !uv->empty()) {
            geo.uvs = *uv;
          }
        }
        if (geo.uvs.empty()) geo.uvs.resize(nv * 2, 0);

        val = prim.GetPropertyValue("faceVertexIndices");
        if (val) {
          const std::vector<int> *idx = val->as_int_array();
          if (idx && !idx->empty()) {
            geo.indices.assign(idx->begin(), idx->end());
          }
        }

        geos.push_back(std::move(geo));
      }
    }

    if (geos.empty()) {
      std::cerr << "No renderable geometry found.\n";
      return EXIT_FAILURE;
    }

    // Resolve camera.
    CameraFrame camera;
    double up_axis = 1.0; // Y-up
    {
      std::string up = stage.GetUpAxis();
      if (up == "Z") up_axis = 2.0;
      else if (up == "X") up_axis = 0.0;
    }
    tinyusdz::Axis usdUp = (up_axis == 2.0) ? tinyusdz::Axis::Z
                           : (up_axis == 0.0) ? tinyusdz::Axis::X
                           : tinyusdz::Axis::Y;

    Bounds bounds;
    for (const auto &g : geos) {
      for (size_t j = 0; j < g.positions.size() / 3; ++j) {
        bounds.lo.x = std::min(bounds.lo.x, g.positions[j * 3 + 0]);
        bounds.lo.y = std::min(bounds.lo.y, g.positions[j * 3 + 1]);
        bounds.lo.z = std::min(bounds.lo.z, g.positions[j * 3 + 2]);
        bounds.hi.x = std::max(bounds.hi.x, g.positions[j * 3 + 0]);
        bounds.hi.y = std::max(bounds.hi.y, g.positions[j * 3 + 1]);
        bounds.hi.z = std::max(bounds.hi.z, g.positions[j * 3 + 2]);
      }
    }
    bounds.valid = true;
    int out_height = opt.height > 0 ? opt.height : 540;

    Options auto_opt = opt;
    auto_opt.camera.clear();
    if (opt.autoframe) {
      camera = MakeCameraFrame({}, auto_opt, bounds, out_height, usdUp);
    } else {
      camera.origin = Vec3{0, 0, 5};
      camera.forward = Normalize(Sub(Vec3{0, 0, 0}, camera.origin));
      camera.up = Vec3{0, 1, 0};
      camera.yfov = 45.0f * 3.14159265f / 180.0f;
    }

    if (!RunVulkanLightRT(opt, base_colors, geos, camera, out_height)) {
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }
#endif

  tinyusdz::Stage stage;
  std::string warn;
  std::string err;
  tinyusdz::USDLoadOptions load_options;
  load_options.mmap_zero_copy = opt.rt_preview;
  load_options.max_memory_limit_in_mb = opt.rt_preview ? 65536 : 16384;
  load_options.load_assets = !opt.rt_preview;
  if (opt.progress) {
    load_options.progress_callback = LoadProgress;
  }
  const auto load_t0 = std::chrono::steady_clock::now();
  if (!tinyusdz::LoadUSDFromFile(opt.input, &stage, &warn, &err, load_options)) {
    if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
    std::cerr << "Failed to load USD: " << err << "\n";
    return EXIT_FAILURE;
  }
  const auto load_t1 = std::chrono::steady_clock::now();
  const double load_seconds =
      std::chrono::duration<double>(load_t1 - load_t0).count();
  if (!warn.empty()) {
    std::cerr << "WARN: " << warn << "\n";
  }

  if (opt.rt_preview) {
    if (tinyusdz::IsUSDC(opt.input) && !stage.has_mmap_zero_copy()) {
      std::cerr << "RT preview requires mmap zero-copy metadata for USDC input. "
                << "Write flattened USDC without --compress-float-arrays.\n";
      return EXIT_FAILURE;
    }

    std::vector<float> vertices;
    std::vector<TriInfo> tris;
    Bounds bounds;
    RTPreviewStats rt_stats;
    std::string rt_err;
    if (!BuildRTPreviewScene(stage, opt, &vertices, &tris, &bounds, &rt_stats,
                             &rt_err)) {
      if (opt.stats) {
        std::cerr << "rt preview: 1\n";
        std::cerr << "rt meshes: " << rt_stats.meshes << "\n";
        std::cerr << "rt skipped meshes: " << rt_stats.skipped_meshes << "\n";
        std::cerr << "rt mmap point meshes: "
                  << rt_stats.meshes_with_mmap_points << "\n";
        std::cerr << "rt owned point meshes: "
                  << rt_stats.meshes_with_owned_points << "\n";
        std::cerr << "rt mmap deferred bytes: "
                  << rt_stats.mmap_deferred_bytes << "\n";
        std::cerr << "rt copied point bytes: " << rt_stats.copied_point_bytes
                  << "\n";
        std::cerr << "rt copied topology bytes: "
                  << rt_stats.copied_topology_bytes << "\n";
        std::cerr << "rt purpose default triangles: "
                  << rt_stats.purpose_default_triangles << "\n";
        std::cerr << "rt purpose render triangles: "
                  << rt_stats.purpose_render_triangles << "\n";
        std::cerr << "rt purpose proxy triangles: "
                  << rt_stats.purpose_proxy_triangles << "\n";
        std::cerr << "rt purpose guide triangles: "
                  << rt_stats.purpose_guide_triangles << "\n";
      }
      std::cerr << rt_err << "\n";
      return EXIT_FAILURE;
    }

    lrt_tri_build_options build_opts;
    std::memset(&build_opts, 0, sizeof(build_opts));
    build_opts.quality = opt.quality;
    build_opts.layout = LRT_TRI_LAYOUT_AUTO;
    build_opts.max_leaf_size = 0;
    build_opts.num_threads = WorkerThreadCount(opt.threads);

    const auto bvh_t0 = std::chrono::steady_clock::now();
    lrt_result lrt_err = LRT_RESULT_OK;
    lrt_tri_scene *lrt_scene =
        lrt_tri_scene_build(vertices.data(), tris.size(), &build_opts, &lrt_err);
    const auto bvh_t1 = std::chrono::steady_clock::now();
    if (!lrt_scene) {
      std::cerr << "Failed to build LightRT scene (err=" << int(lrt_err) << ").\n";
      return EXIT_FAILURE;
    }

    int height = opt.height > 0 ? opt.height : 540;
    RenderScene empty_render_scene;
    CameraFrame camera;
    if (!FindStageCameraFrame(stage, opt.camera, opt.timecode, &camera)) {
      if (!opt.camera.empty()) {
        std::cerr << "WARN: Camera not found: " << opt.camera
                  << ". Using auto-fit camera.\n";
      }
      Options auto_opt = opt;
      auto_opt.camera.clear();
      camera = MakeCameraFrame(empty_render_scene, auto_opt, bounds, height,
                               stage.metas().upAxis.get_value());
    }
    DirectScene direct_scene;
    LightCache light_cache;
    IblCache ibl_cache;

    if (opt.stats) {
      lrt_tri_stats st;
      std::memset(&st, 0, sizeof(st));
      lrt_tri_scene_stats(lrt_scene, &st);
      double bvh_seconds =
          std::chrono::duration<double>(bvh_t1 - bvh_t0).count();
      std::cerr << "rt preview: 1\n";
      std::cerr << "rt meshes: " << rt_stats.meshes << "\n";
      std::cerr << "rt skipped meshes: " << rt_stats.skipped_meshes << "\n";
      std::cerr << "rt mmap point meshes: "
                << rt_stats.meshes_with_mmap_points << "\n";
      std::cerr << "rt owned point meshes: "
                << rt_stats.meshes_with_owned_points << "\n";
      std::cerr << "rt mmap deferred bytes: "
                << rt_stats.mmap_deferred_bytes << "\n";
      std::cerr << "rt copied point bytes: " << rt_stats.copied_point_bytes
                << "\n";
      std::cerr << "rt copied topology bytes: "
                << rt_stats.copied_topology_bytes << "\n";
      std::cerr << "rt purpose default triangles: "
                << rt_stats.purpose_default_triangles << "\n";
      std::cerr << "rt purpose render triangles: "
                << rt_stats.purpose_render_triangles << "\n";
      std::cerr << "rt purpose proxy triangles: "
                << rt_stats.purpose_proxy_triangles << "\n";
      std::cerr << "rt purpose guide triangles: "
                << rt_stats.purpose_guide_triangles << "\n";
      std::cerr << "rt packed triangle bytes: "
                << rt_stats.packed_triangle_bytes << "\n";
      std::cerr << "load seconds: " << load_seconds << "\n";
      std::cerr << "rt triangle stream seconds: " << rt_stats.build_seconds
                << "\n";
      std::cerr << "rt bvh build seconds: " << bvh_seconds << "\n";
      std::cerr << "triangles: " << tris.size() << "\n";
      std::cerr << "lightrt: " << lrt_tri_kernel_name(lrt_scene) << "\n";
      std::cerr << "bvh nodes: " << st.node_count << ", leaves: "
                << st.leaf_count << ", memory: " << st.memory_bytes
                << " bytes\n";
    }

    const auto render_t0 = std::chrono::steady_clock::now();
    tinyusdz::Image img =
        RenderImage(lrt_scene, &direct_scene, tris, light_cache, nullptr, camera,
                    opt, height);
    const auto render_t1 = std::chrono::steady_clock::now();
    if (opt.stats) {
      std::cerr << "render seconds: "
                << std::chrono::duration<double>(render_t1 - render_t0).count()
                << "\n";
    }
    lrt_tri_scene_free(lrt_scene);

    tinyusdz::image::WriteOption wopt;
    wopt.format = tinyusdz::image::WriteImageFormat::Autodetect;
    auto ret = tinyusdz::image::WriteImageToFile(opt.output, img, wopt);
    if (!ret) {
      std::cerr << "Failed to write image: " << ret.error() << "\n";
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }

  tinyusdz::tydra::RenderScene render_scene;
  tinyusdz::tydra::RenderSceneConverter converter;
  tinyusdz::tydra::RenderSceneConverterEnv env(stage);
  env.timecode = opt.timecode;
  env.mesh_config.triangulate = !opt.direct_prims;
  env.mesh_config.subdivision_level = opt.subdivision_level;
  env.mesh_config.build_vertex_indices = !opt.direct_prims;
  env.mesh_config.compute_tangents_and_binormals = false;
  env.scene_config.load_texture_assets = true;
  env.set_search_paths({tinyusdz::io::GetBaseDir(opt.input)});
  if (opt.no_assetresolver) {
    SetupNullAssetResolution(&env.asset_resolver);
  }
  if (!converter.ConvertToRenderScene(env, &render_scene)) {
    std::cerr << "Failed to convert USD Stage to RenderScene:\n"
              << converter.GetError() << "\n";
    return EXIT_FAILURE;
  }
  std::string converter_warn = converter.GetWarning();
  if (!converter_warn.empty()) {
    std::cerr << "WARN: " << converter_warn << "\n";
  }

  std::vector<float> vertices;
  std::vector<TriInfo> tris;
  Bounds bounds;
  DirectScene direct_scene;
  LightCache light_cache;
  if (opt.direct_prims) {
    std::string direct_err;
    if (!BuildDirectScene(stage, render_scene, opt, &vertices, &tris, &bounds,
                          &direct_scene, &direct_err)) {
      std::cerr << direct_err << "\n";
      return EXIT_FAILURE;
    }
  }
  CollectAllGeometry(render_scene, &vertices, &tris, &bounds,
                     opt.direct_prims ? &direct_scene.direct_paths : nullptr,
                     &light_cache);
  const bool has_direct = direct_scene.spheres || direct_scene.round_curves ||
                          direct_scene.flat_curves || direct_scene.points ||
                          direct_scene.bez_curves || direct_scene.tets ||
                          !direct_scene.shapes.empty();

  // UsdVol volumes (OpenVDB) -> dense grids for raymarching. Built here so a
  // volume-only scene still renders and contributes to camera-framing bounds.
  std::vector<VolumeData> volumes = BuildVolumes(render_scene);
  ExpandBoundsByVolume(volumes, &bounds);

  if (tris.empty() && !has_direct && volumes.empty()) {
    std::cerr << "No renderable geometry found.\n";
    return EXIT_FAILURE;
  }

  lrt_tri_build_options build_opts;
  std::memset(&build_opts, 0, sizeof(build_opts));
  build_opts.quality = opt.quality;
  build_opts.layout = LRT_TRI_LAYOUT_AUTO;
  build_opts.max_leaf_size = 0;
  build_opts.num_threads = WorkerThreadCount(opt.threads);

  lrt_result lrt_err = LRT_RESULT_OK;
  lrt_tri_scene *lrt_scene = nullptr;
  if (!tris.empty()) {
    lrt_scene = lrt_tri_scene_build(vertices.data(), tris.size(), &build_opts, &lrt_err);
    if (!lrt_scene) {
      std::cerr << "Failed to build LightRT scene (err=" << int(lrt_err) << ").\n";
      return EXIT_FAILURE;
    }
  }

  int height = opt.height;
  if (height <= 0) {
    height = 540;
    const Node *cam_node = FindCameraNode(render_scene, opt.camera);
    if (cam_node) {
      const RenderCamera &cam = render_scene.cameras[size_t(cam_node->id)];
      if (cam.verticalAspectRatio > 0.0f) {
        height = std::max(1, int(std::round(float(opt.width) *
                                           cam.verticalAspectRatio)));
      }
    }
  }

  tinyusdz::Axis up_axis = GetUpAxis(render_scene.meta.upAxis);
  CameraFrame camera = MakeCameraFrame(render_scene, opt, bounds, height,
                                       up_axis);
  CollectLights(render_scene, &light_cache);
  IblCache ibl_cache;
  BuildIblCache(render_scene, light_cache, &ibl_cache);

  if (opt.stats) {
    lrt_tri_stats st;
    std::memset(&st, 0, sizeof(st));
    if (lrt_scene) lrt_tri_scene_stats(lrt_scene, &st);
    std::cerr << "triangles: " << tris.size() << "\n";
    std::cerr << "direct spheres: " << direct_scene.sphere_info.size() << "\n";
    std::cerr << "direct round curve segments: "
              << direct_scene.round_curve_info.size() << "\n";
    std::cerr << "direct flat curve segments: "
              << direct_scene.flat_curve_info.size() << "\n";
    std::cerr << "direct Hermite/Bezier curve segments: "
              << direct_scene.bez_curve_info.size() << "\n";
    std::cerr << "direct points: " << direct_scene.point_info.size() << "\n";
    std::cerr << "direct tetrahedra: " << direct_scene.tet_prims.size()
              << "\n";
    std::cerr << "direct analytic shapes: " << direct_scene.shapes.size()
              << "\n";
    std::cerr << "subdivision level: " << opt.subdivision_level << "\n";
    std::cerr << "lights: " << light_cache.finite.size() << "\n";
    std::cerr << "mesh light triangles: " << light_cache.mesh.size() << "\n";
    std::cerr << "domelight: " << (light_cache.has_dome ? 1 : 0) << "\n";
    std::cerr << "light sampling finite cdf entries: "
              << light_cache.finite_cdf.size() << "\n";
    std::cerr << "light sampling mesh cdf entries: "
              << light_cache.mesh_cdf.size() << "\n";
    std::cerr << "light sampling env cdf entries: "
              << light_cache.env_cdf.size() << "\n";
    std::cerr << "ibl envmap: " << (ibl_cache.valid ? 1 : 0) << "\n";
    std::cerr << "ibl diffuse size: "
              << (ibl_cache.diffuse.width * ibl_cache.diffuse.height) << "\n";
    std::cerr << "ibl prefilter levels: " << ibl_cache.prefiltered.size()
              << "\n";
    std::cerr << "ibl brdf lut size: "
              << (ibl_cache.brdf_size * ibl_cache.brdf_size) << "\n";
    if (lrt_scene) std::cerr << "lightrt: " << lrt_tri_kernel_name(lrt_scene) << "\n";
    std::cerr << "bvh nodes: " << st.node_count << ", leaves: " << st.leaf_count
              << ", memory: " << st.memory_bytes << " bytes\n";
    std::cerr << "load seconds: " << load_seconds << "\n";
  }

  if (opt.stats) {
    std::cerr << "volumes: " << volumes.size() << "\n";
  }

  const auto render_t0 = std::chrono::steady_clock::now();
  tinyusdz::Image img =
      RenderImage(lrt_scene, &direct_scene, tris, light_cache,
                  ibl_cache.valid ? &ibl_cache : nullptr, camera, opt, height,
                  /*textures*/ nullptr, /*tri_uvs*/ nullptr, /*tlas*/ nullptr,
                  /*blas*/ nullptr, /*instances*/ nullptr, /*tri_colors*/ nullptr,
                  /*tri_normals*/ nullptr, &volumes);
  const auto render_t1 = std::chrono::steady_clock::now();
  if (opt.stats) {
    std::cerr << "render seconds: "
              << std::chrono::duration<double>(render_t1 - render_t0).count()
              << "\n";
  }
  if (lrt_scene) lrt_tri_scene_free(lrt_scene);

  tinyusdz::image::WriteOption wopt;
  wopt.format = tinyusdz::image::WriteImageFormat::Autodetect;
  auto ret = tinyusdz::image::WriteImageToFile(opt.output, img, wopt);
  if (!ret) {
    std::cerr << "Failed to write image: " << ret.error() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
