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
static const Vec3 kCurveColor{0.62f, 0.50f, 0.34f};

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
std::vector<float> ReadFloatArrayLazy(const tinyusdz::next::UsdPrim &prim,
                                      const char *name, double time) {
  const tinyusdz::next::Value *v = std::isnan(time)
                                       ? prim.GetPropertyValue(name)
                                       : prim.GetValueAtTime(name, time);
  if (!v) return {};
  if (v->is_lazy()) {
    tinyusdz::next::Value tmp = v->materialized_copy();
    if (const std::vector<float> *a = tmp.as_float_array()) return *a;
    return {};
  }
  if (const std::vector<float> *a = v->as_float_array()) return *a;
  return {};
}

std::vector<int32_t> ReadIntArrayLazy(const tinyusdz::next::UsdPrim &prim,
                                      const char *name, double time) {
  const tinyusdz::next::Value *v = std::isnan(time)
                                       ? prim.GetPropertyValue(name)
                                       : prim.GetValueAtTime(name, time);
  if (!v) return {};
  if (v->is_lazy()) {
    tinyusdz::next::Value tmp = v->materialized_copy();
    if (const std::vector<int32_t> *a = tmp.as_int_array()) return *a;
    return {};
  }
  if (const std::vector<int32_t> *a = v->as_int_array()) return *a;
  return {};
}

std::vector<int64_t> ReadInt64ArrayLazy(const tinyusdz::next::UsdPrim &prim,
                                        const char *name, double time) {
  const tinyusdz::next::Value *v = std::isnan(time)
                                       ? prim.GetPropertyValue(name)
                                       : prim.GetValueAtTime(name, time);
  if (!v) return {};
  if (v->is_lazy()) {
    tinyusdz::next::Value tmp = v->materialized_copy();
    if (const std::vector<int64_t> *a = tmp.as_int64_array()) return *a;
    return {};
  }
  if (const std::vector<int64_t> *a = v->as_int64_array()) return *a;
  return {};
}

// Templated on the output buffer type so the flat path can stream into plain
// std::vector (ctx.tris) while the instanced path streams into the budget-tracked
// Blas FloatVec/TriVec (so the big instanced geometry is capped/pooled).
template <class FVec, class TVec>
void AddRTPreviewMeshNext(const tinyusdz::next::UsdPrim &prim,
                          const matrix4d &world, tinyusdz::Purpose purpose,
                          uint32_t purpose_mask, double time,
                          const Vec3 &base_color, int32_t tex_id,
                          int32_t normal_tex_id, float roughness, float metallic,
                          const ScalarTex &rough_tex, const ScalarTex &metal_tex,
                          const Vec3 &emission, int32_t emission_tex_id,
                          float occlusion, const ScalarTex &occ_tex,
                          const UvXform &uv_xform, bool want_uvs,
                          FVec *vertices, TVec *tris, FVec *tri_uvs,
                          Bounds *bounds, RTPreviewStats *stats,
                          bool purpose_cull = false,
                          TriMat *out_job_mat = nullptr, float opacity = 1.0f,
                          bool want_colors = false, ByteVec *tri_colors = nullptr,
                          bool want_normals = false, FVec *tri_normals = nullptr,
                          // Indexed geometry (Phase 2b): when out_uverts != null,
                          // emit 1x unique verts + 3 vertex indices/tri (offset by
                          // *io_vbase) instead of writing the de-indexed soup.
                          FVec *out_uverts = nullptr, IdxVec *out_indices = nullptr,
                          uint32_t *io_vbase = nullptr) {
  // When the output is the slim TriStore (instanced BLAS), the per-mesh material
  // is emitted once into out_job_mat and each triangle stores only its mat_id.
  if (out_job_mat) {
    out_job_mat->base_color = base_color;
    out_job_mat->emission = emission;
    out_job_mat->roughness = roughness;
    out_job_mat->metallic = metallic;
    out_job_mat->tex_id = tex_id;
    out_job_mat->normal_tex_id = normal_tex_id;
    out_job_mat->rough_tex_id = rough_tex.id;
    out_job_mat->metal_tex_id = metal_tex.id;
    out_job_mat->emission_tex_id = emission_tex_id;
    out_job_mat->occ_tex_id = occ_tex.id;
    out_job_mat->occlusion = occlusion;
    out_job_mat->opacity = opacity;
    out_job_mat->rough_ch = rough_tex.ch;
    out_job_mat->metal_ch = metal_tex.ch;
    out_job_mat->occ_ch = occ_tex.ch;
  }
  // Read geometry without permanently materializing it into the stage.
  const std::vector<float> points = ReadFloatArrayLazy(prim, "points", time);
  const std::vector<int32_t> counts =
      ReadIntArrayLazy(prim, "faceVertexCounts", time);
  const std::vector<int32_t> indices =
      ReadIntArrayLazy(prim, "faceVertexIndices", time);
  if (points.empty() || counts.empty() || indices.empty()) {
    stats->skipped_meshes++;
    return;
  }
  const size_t npts = points.size() / 3;
  // Transform each unique vertex ONCE up front. The triangle-fan loop below would
  // otherwise re-transform a shared/fan-center vertex for every incident corner
  // (~6x on a typical triangle mesh) via the double-precision matrix. These are
  // the same world-space floats the loop produced before, just deduplicated, so
  // the emitted soup / normals are byte-identical. (Phase 2b hands this array
  // straight to the indexed build instead of re-expanding it into a soup.)
  std::vector<Vec3> wpts(npts);
  for (size_t i = 0; i < npts; i++) {
    wpts[i] = TransformPoint(
        world, Vec3{points[3 * i], points[3 * i + 1], points[3 * i + 2]});
  }

  // UV (primvars:st) for the diffuse texture. Stored flat [u0,v0,u1,v1,...].
  // Interpolation is inferred from sizes: per-point ("vertex"/"varying") when
  // the UV count matches the point count, otherwise per-face-vertex
  // ("faceVarying"). primvars:st:indices indirection is honored when present.
  std::vector<float> st;
  std::vector<int32_t> st_indices;
  bool st_facevarying = false;
  bool have_st = false;
  if (want_uvs && (tex_id >= 0 || normal_tex_id >= 0 || rough_tex.id >= 0 ||
                   metal_tex.id >= 0 || emission_tex_id >= 0 ||
                   occ_tex.id >= 0)) {
    st = ReadFloatArrayLazy(prim, "primvars:st", time);
    if (st.empty()) st = ReadFloatArrayLazy(prim, "primvars:UVMap", time);
    if (!st.empty()) {
      st_indices = ReadIntArrayLazy(prim, "primvars:st:indices", time);
      const size_t uv_count =
          st_indices.empty() ? (st.size() / 2) : st_indices.size();
      // Per-point if it matches the points; per-face-vertex if it matches the
      // face-vertex stream (== faceVertexIndices length).
      if (uv_count == npts) {
        st_facevarying = false;
        have_st = true;
      } else if (uv_count == indices.size()) {
        st_facevarying = true;
        have_st = true;
      }
    }
  }
  // Fetch the UV for face-vertex slot `fv` (position in the face-vertex stream)
  // whose underlying point index is `pi`. Returns {u,v}.
  auto uv_at = [&](size_t fv, int32_t pi) -> std::pair<float, float> {
    if (!have_st) return {0.0f, 0.0f};
    size_t s = st_facevarying ? fv : size_t(pi);
    if (!st_indices.empty()) {
      if (s >= st_indices.size()) return {0.0f, 0.0f};
      int32_t idx = st_indices[s];
      if (idx < 0) return {0.0f, 0.0f};
      s = size_t(idx);
    }
    if (s * 2 + 1 >= st.size()) return {0.0f, 0.0f};
    return {st[s * 2 + 0], st[s * 2 + 1]};
  };

  // Per-corner displayColor/displayOpacity (RGBA), when the scene has any
  // non-constant display primvar. Interpolation is inferred from array size:
  // vertex (== npoints), faceVarying (== face-vertex count), uniform (== nfaces);
  // a 1-element array (or absent) falls back to the constant base_color/opacity.
  std::vector<float> dcol, dopac;
  int dc_mode = 0, do_mode = 0;  // 0=const, 1=vertex, 2=faceVarying, 3=uniform
  if (want_colors) {
    dcol = ReadFloatArrayLazy(prim, "primvars:displayColor", time);
    const size_t nc = dcol.size() / 3;
    if (nc == npts) dc_mode = 1;
    else if (nc == indices.size()) dc_mode = 2;
    else if (nc == counts.size()) dc_mode = 3;
    dopac = ReadFloatArrayLazy(prim, "primvars:displayOpacity", time);
    const size_t no = dopac.size();
    if (no == npts) do_mode = 1;
    else if (no == indices.size()) do_mode = 2;
    else if (no == counts.size()) do_mode = 3;
  }
  // RGBA for face-vertex slot `fv` (point index `pi`, face `face`).
  auto col_at = [&](size_t fv, int32_t pi, size_t face, float out[4]) {
    out[0] = base_color.x; out[1] = base_color.y; out[2] = base_color.z;
    out[3] = opacity;
    if (dc_mode) {
      size_t ci = dc_mode == 1 ? size_t(pi) : dc_mode == 2 ? fv : face;
      if (ci * 3 + 2 < dcol.size()) {
        out[0] = dcol[ci * 3 + 0]; out[1] = dcol[ci * 3 + 1];
        out[2] = dcol[ci * 3 + 2];
      }
    }
    if (do_mode) {
      size_t oi = do_mode == 1 ? size_t(pi) : do_mode == 2 ? fv : face;
      if (oi < dopac.size()) out[3] = std::min(1.0f, std::max(0.0f, dopac[oi]));
    }
  };

  // Authored normals for smooth shading (`-smooth`). Stored per corner in the
  // job's frame (world for flat, prototype-local for instanced — `world` is
  // identity there), transformed at hit. Falls back to the geometric normal when
  // absent. Interpolation inferred from size (vertex / faceVarying).
  std::vector<float> nrm;
  int nrm_mode = 0;  // 0=none, 1=vertex, 2=faceVarying
  if (want_normals) {
    nrm = ReadFloatArrayLazy(prim, "normals", time);
    if (nrm.empty()) nrm = ReadFloatArrayLazy(prim, "primvars:normals", time);
    const size_t nn = nrm.size() / 3;
    if (nn == npts) nrm_mode = 1;
    else if (nn == indices.size()) nrm_mode = 2;
  }
  // World-space normal for face-vertex slot `fv` (point index `pi`); `geom` is the
  // face's geometric normal (already in the job frame) used as the fallback.
  auto norm_at = [&](size_t fv, int32_t pi, const Vec3 &geom) -> Vec3 {
    if (!nrm_mode) return geom;
    size_t ni = nrm_mode == 1 ? size_t(pi) : fv;
    if (ni * 3 + 2 >= nrm.size()) return geom;
    Vec3 ln{nrm[ni * 3 + 0], nrm[ni * 3 + 1], nrm[ni * 3 + 2]};
    Vec3 wn = TransformVector(world, ln);  // job-frame (world for flat path)
    float len = Length(wn);
    return len > 1.0e-12f ? Mul(wn, 1.0f / len) : geom;
  };

  // Reserve from the exact triangle-fan estimate. StreamMeshJobs gives each mesh
  // its OWN thread-local buffers (one mesh's worth), so a single up-front reserve
  // replaces the per-triangle geometric reallocations (the TriInfo realloc churn
  // perf flagged). (This is safe ONLY because the buffers are per-job now; the
  // old shared-buffer design would have reallocated multi-GB on every mesh.)
  size_t tri_estimate = 0;
  for (int32_t c : counts) {
    if (c >= 3) tri_estimate += size_t(c - 2);
  }
  if (tri_estimate) {
    if (out_uverts) {
      out_indices->reserve(out_indices->size() + tri_estimate * 3);
    } else {
      vertices->reserve(vertices->size() + tri_estimate * 9);
    }
    tris->reserve(tris->size() + tri_estimate);
    if (want_uvs) tri_uvs->reserve(tri_uvs->size() + tri_estimate * 6);
    if (want_colors) tri_colors->reserve(tri_colors->size() + tri_estimate * 12);
    if (want_normals) tri_normals->reserve(tri_normals->size() + tri_estimate * 9);
  }
  // Indexed path: append this mesh's unique world-space vertices once; triangle
  // indices below are offset by the BLAS-local base. The soup path leaves these
  // untouched. All npts are appended (index space == point ids) even if some are
  // unreferenced -- simpler base arithmetic, negligible waste.
  const uint32_t vbase = (out_uverts && io_vbase) ? *io_vbase : 0u;
  if (out_uverts) {
    out_uverts->reserve(out_uverts->size() + npts * 3);
    for (size_t i = 0; i < npts; i++) {
      out_uverts->push_back(wpts[i].x);
      out_uverts->push_back(wpts[i].y);
      out_uverts->push_back(wpts[i].z);
    }
    if (io_vbase) *io_vbase += uint32_t(npts);
  }

  // Material + purpose are constant across a mesh's triangles, so resolve them
  // ONCE here instead of rebuilding a full TriInfo per triangle. The slim TLAS
  // path (TriStore) stores the material once in mat_table and needs none of these
  // per-tri; the flat path copies this template and overwrites only the positions.
  const uint32_t purpose_bit = PurposeBit(purpose);
  const bool visible_for_fit = PurposeVisible(purpose_bit, purpose_mask);
  TriInfo tmpl;
  tmpl.base_color = base_color;
  tmpl.tex_id = tex_id;
  tmpl.normal_tex_id = normal_tex_id;
  tmpl.roughness = roughness;
  tmpl.metallic = metallic;
  tmpl.rough_tex_id = rough_tex.id;
  tmpl.rough_ch = rough_tex.ch;
  tmpl.metal_tex_id = metal_tex.id;
  tmpl.metal_ch = metal_tex.ch;
  tmpl.emission = emission;
  tmpl.emission_tex_id = emission_tex_id;
  tmpl.occlusion = occlusion;
  tmpl.occ_tex_id = occ_tex.id;
  tmpl.occ_ch = occ_tex.ch;
  tmpl.opacity = opacity;
  tmpl.purpose_bit = purpose_bit;

  size_t cursor = 0;
  size_t face_idx = 0;
  for (int32_t c : counts) {
    const size_t face = face_idx++;
    if (c < 3 || cursor + size_t(c) > indices.size()) {
      cursor += size_t(std::max<int32_t>(0, c));
      continue;
    }
    for (int32_t k = 1; k + 1 < c; k++) {
      int32_t i0 = indices[cursor + 0];
      int32_t i1 = indices[cursor + size_t(k)];
      int32_t i2 = indices[cursor + size_t(k + 1)];
      if (i0 < 0 || i1 < 0 || i2 < 0 || size_t(i0) >= npts ||
          size_t(i1) >= npts || size_t(i2) >= npts) {
        continue;
      }
      const Vec3 &p0 = wpts[size_t(i0)];
      const Vec3 &p1 = wpts[size_t(i1)];
      const Vec3 &p2 = wpts[size_t(i2)];
      Vec3 n = Normalize(Cross(Sub(p1, p0), Sub(p2, p0)));
      if (Length(n) < 1.0e-12f) continue;

      if (purpose_bit == kPurposeRenderBit) {
        stats->purpose_render_triangles++;
      } else if (purpose_bit == kPurposeProxyBit) {
        stats->purpose_proxy_triangles++;
      } else if (purpose_bit == kPurposeGuideBit) {
        stats->purpose_guide_triangles++;
      } else {
        stats->purpose_default_triangles++;
      }
      // TLAS mode culls purpose-invisible triangles at build time (closest-hit
      // can't filter per-prim like the flat multi-hit path does).
      if (purpose_cull && !visible_for_fit) continue;
      if (out_indices) {
        out_indices->push_back(vbase + uint32_t(i0));
        out_indices->push_back(vbase + uint32_t(i1));
        out_indices->push_back(vbase + uint32_t(i2));
      } else {
        vertices->insert(vertices->end(),
                         {p0.x, p0.y, p0.z, p1.x, p1.y, p1.z, p2.x, p2.y, p2.z});
      }
      if constexpr (std::is_same<typename TVec::value_type, TriStore>::value) {
        // Slim store: only mat_id (positions are in `vertices` above; the global
        // mat_id is assigned by StreamMeshJobs when it concatenates jobs).
        TriStore ts;
        ts.mat_id = 0;
        tris->push_back(ts);
      } else {
        TriInfo tri = tmpl;  // per-mesh material; per-tri geometry below
        tri.p0 = p0;
        tri.p1 = p1;
        tri.p2 = p2;
        tri.n = n;
        tris->push_back(tri);
      }
      if (want_uvs) {
        // Keep tri_uvs parallel to tris (6 floats/tri). uv0=vert0, uv1=vert(k),
        // uv2=vert(k+1) in fan order, matching i0/i1/i2 above.
        auto uv0 = uv_at(cursor + 0, i0);
        auto uv1 = uv_at(cursor + size_t(k), i1);
        auto uv2 = uv_at(cursor + size_t(k + 1), i2);
        // Bake the UsdTransform2d (if any) into the stored UVs.
        uv_xform.apply(&uv0.first, &uv0.second);
        uv_xform.apply(&uv1.first, &uv1.second);
        uv_xform.apply(&uv2.first, &uv2.second);
        tri_uvs->insert(tri_uvs->end(), {uv0.first, uv0.second, uv1.first,
                                         uv1.second, uv2.first, uv2.second});
      }
      if (want_colors) {
        // Per-corner RGBA8 parallel to tris (12 bytes/tri), fan order matching
        // i0/i1/i2. Constant meshes replicate base_color/opacity at all corners.
        float c0[4], c1[4], c2[4];
        col_at(cursor + 0, i0, face, c0);
        col_at(cursor + size_t(k), i1, face, c1);
        col_at(cursor + size_t(k + 1), i2, face, c2);
        auto q = [](float x) -> uint8_t {
          x = x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
          return uint8_t(int(x * 255.0f + 0.5f));
        };
        tri_colors->insert(tri_colors->end(),
                           {q(c0[0]), q(c0[1]), q(c0[2]), q(c0[3]), q(c1[0]),
                            q(c1[1]), q(c1[2]), q(c1[3]), q(c2[0]), q(c2[1]),
                            q(c2[2]), q(c2[3])});
      }
      if (want_normals) {
        // Per-corner normals (9 floats/tri), fan order matching i0/i1/i2.
        Vec3 n0 = norm_at(cursor + 0, i0, n);
        Vec3 n1 = norm_at(cursor + size_t(k), i1, n);
        Vec3 n2 = norm_at(cursor + size_t(k + 1), i2, n);
        tri_normals->insert(tri_normals->end(),
                            {n0.x, n0.y, n0.z, n1.x, n1.y, n1.z, n2.x, n2.y,
                             n2.z});
      }
      stats->triangles++;
      if (visible_for_fit) {
        Expand(bounds, p0);
        Expand(bounds, p1);
        Expand(bounds, p2);
      }
    }
    cursor += size_t(c);
  }
}


// ---------------------------------------------------------------------------
// Material/texture resolution for the `next` render path.
//
// Resolves a Mesh's bound material (material:binding -> Material ->
// outputs:surface -> UsdPreviewSurface, including MaterialX's
// ND_UsdPreviewSurface_surfaceshader) into a flat diffuse base color and/or a
// diffuse (base color) texture sampled through inputs:diffuseColor ->
// UsdUVTexture(inputs:file). Done serially before the parallel triangle stream.
// ---------------------------------------------------------------------------

// Directory portion of a path (without trailing slash), or "" if none.
std::string DirName(const std::string &path) {
  size_t slash = path.find_last_of("/\\");
  if (slash == std::string::npos) return "";
  return path.substr(0, slash);
}

// Loaded RGB(A) textures + a key->index cache so each (file, wrap, colorspace)
// loads once. `usdz`, when set, is searched first so textures packed inside a
// .usdz archive resolve without touching the filesystem.

// True if a usdz entry name refers to the same asset as `asset_path` (which may
// be relative, "./tex.png", or "tex.png"). Matches on full path or basename.
bool UsdzEntryMatches(const std::string &entry, const std::string &asset) {
  std::string a = asset;
  if (a.rfind("./", 0) == 0) a = a.substr(2);
  if (entry == a) return true;
  if (entry.size() > a.size() &&
      entry.compare(entry.size() - a.size(), a.size(), a) == 0 &&
      entry[entry.size() - a.size() - 1] == '/') {
    return true;
  }
  auto base = [](const std::string &s) {
    size_t p = s.find_last_of('/');
    return p == std::string::npos ? s : s.substr(p + 1);
  };
  return base(entry) == base(a);
}

int32_t LoadTextureCached(TextureCache &tc, const std::string &asset_path,
                          WrapMode ws, WrapMode wt, bool srgb,
                          const Vec3 &scale = Vec3{1.0f, 1.0f, 1.0f},
                          const Vec3 &bias = Vec3{0.0f, 0.0f, 0.0f}) {
  const std::string key = asset_path + "|" + std::to_string(int(ws)) + "," +
                          std::to_string(int(wt)) + (srgb ? "|s" : "|r") + "|" +
                          std::to_string(scale.x) + "," + std::to_string(bias.x);
  auto it = tc.by_key.find(key);
  if (it != tc.by_key.end()) return it->second;

  auto adopt = [&](const tinyusdz::Image &img) -> int32_t {
    if (img.width <= 0 || img.height <= 0 || img.bpp != 8 || img.data.empty()) {
      return -1;
    }
    Texture t;
    t.width = img.width;
    t.height = img.height;
    t.channels = img.channels;
    t.pixels = img.data;
    t.wrap_s = ws;
    t.wrap_t = wt;
    t.srgb = srgb;
    t.scale = scale;
    t.bias = bias;
    t.build_mips();
    int32_t id = int32_t(tc.textures->size());
    tc.textures->push_back(std::move(t));
    return id;
  };

  int32_t id = -1;
  // 1. usdz-embedded.
  if (tc.usdz) {
    for (size_t i = 0; i < tc.usdz->NumEntries(); ++i) {
      if (!UsdzEntryMatches(tc.usdz->EntryName(i), asset_path)) continue;
      auto res = tinyusdz::image::LoadImageFromMemory(
          tc.usdz->EntryData(i), tc.usdz->EntrySize(i), tc.usdz->EntryName(i));
      if (res) id = adopt(res.value().image);
      break;
    }
  }
  // 2. filesystem (anchored to the input dir).
  if (id < 0 && (!asset_path.empty())) {
    std::string path = asset_path;
    if (path[0] != '/' && !tc.base_dir.empty()) path = tc.base_dir + "/" + path;
    auto res = tinyusdz::image::LoadImageFromFile(path);
    if (res) id = adopt(res.value().image);
  }
  if (id < 0) std::cerr << "WARN: failed to load texture: " << asset_path << "\n";
  tc.by_key[key] = id;
  return id;
}

// Follow a connection on `prim` (e.g. "outputs:surface",
// "inputs:diffuseColor") to its target prim, or an invalid prim if unconnected.
tinyusdz::next::UsdPrim ConnectedPrimNext(const tinyusdz::next::Stage &stage,
                                          const tinyusdz::next::UsdPrim &prim,
                                          const std::string &prop) {
  const tinyusdz::next::PrimSpec *spec = prim.GetPrimSpec();
  if (!spec) return tinyusdz::next::UsdPrim();
  const std::vector<tinyusdz::next::Path> *c = spec->connection(prop);
  if (!c || c->empty()) return tinyusdz::next::UsdPrim();
  return stage.GetPrimAtPath((*c)[0].prim_path());
}

WrapMode ParseWrapMode(const std::string &s) {
  if (s == "clamp") return WrapMode::Clamp;
  if (s == "mirror") return WrapMode::Mirror;
  if (s == "black") return WrapMode::Black;
  return WrapMode::Repeat;  // "repeat"/"useMetadata"/default
}

// Resolve a scalar PBR input (inputs:roughness / inputs:metallic) that connects
// to a UsdUVTexture outputs:{r,g,b,a} (e.g. ORM packing). Loads the raw texture
// and records the source channel.
void ResolveScalarTextureNext(const tinyusdz::next::Stage &stage,
                              const tinyusdz::next::UsdPrim &surf,
                              const std::string &input, TextureCache &tc,
                              ScalarTex *out) {
  const tinyusdz::next::PrimSpec *spec = surf.GetPrimSpec();
  if (!spec) return;
  const std::vector<tinyusdz::next::Path> *c = spec->connection(input);
  if (!c || c->empty()) return;
  const tinyusdz::next::Path &target = (*c)[0];
  tinyusdz::next::UsdPrim tex = stage.GetPrimAtPath(target.prim_path());
  if (!tex.IsValid()) return;
  const tinyusdz::next::Value *fv = tex.GetPropertyValue("inputs:file");
  if (!fv) return;
  const std::string *ap = fv->as_asset_path();
  if (!ap) ap = fv->as_string();
  if (!ap || ap->empty()) return;
  WrapMode ws = WrapMode::Repeat, wt = WrapMode::Repeat;
  if (const tinyusdz::next::Value *v = tex.GetPropertyValue("inputs:wrapS"))
    if (const std::string *t = v->as_token()) ws = ParseWrapMode(*t);
  if (const tinyusdz::next::Value *v = tex.GetPropertyValue("inputs:wrapT"))
    if (const std::string *t = v->as_token()) wt = ParseWrapMode(*t);
  int32_t id = LoadTextureCached(tc, *ap, ws, wt, /*srgb=*/false);
  if (id < 0) return;
  out->id = id;
  const std::string prop = target.property_name();  // e.g. "outputs:g"
  if (!prop.empty()) {
    switch (prop.back()) {
      case 'g': out->ch = 1; break;
      case 'b': out->ch = 2; break;
      case 'a': out->ch = 3; break;
      default: out->ch = 0; break;  // r / rgb
    }
  }
}

// If a UsdUVTexture's inputs:st chain runs through a UsdTransform2d, read its
// rotation (deg, CCW) / scale / translation. Otherwise returns identity.
UvXform ResolveUvXform(const tinyusdz::next::Stage &stage,
                       const tinyusdz::next::UsdPrim &uvtex) {
  UvXform x;
  tinyusdz::next::UsdPrim st = ConnectedPrimNext(stage, uvtex, "inputs:st");
  if (!st.IsValid()) return x;
  const tinyusdz::next::Value *idv = st.GetPropertyValue("info:id");
  const std::string *id = idv ? idv->as_token() : nullptr;
  if (!id || *id != "UsdTransform2d") return x;
  float rot = 0.0f, sx = 1.0f, sy = 1.0f, tx = 0.0f, ty = 0.0f;
  if (const tinyusdz::next::Value *v = st.GetPropertyValue("inputs:rotation"))
    if (const float *f = v->as_float()) rot = *f;
  if (const tinyusdz::next::Value *v = st.GetPropertyValue("inputs:scale"))
    if (const float *f = v->as_float2()) { sx = f[0]; sy = f[1]; }
  if (const tinyusdz::next::Value *v = st.GetPropertyValue("inputs:translation"))
    if (const float *f = v->as_float2()) { tx = f[0]; ty = f[1]; }
  if (rot == 0.0f && sx == 1.0f && sy == 1.0f && tx == 0.0f && ty == 0.0f) {
    return x;  // identity
  }
  float rad = rot * 3.14159265358979f / 180.0f;
  x.rc = std::cos(rad);
  x.rs = std::sin(rad);
  x.sx = sx;
  x.sy = sy;
  x.tx = tx;
  x.ty = ty;
  x.identity = false;
  return x;
}

void ResolveMeshMaterialNext(const tinyusdz::next::Stage &stage,
                             const tinyusdz::next::UsdPrim &mesh,
                             TextureCache &tc, Vec3 *base_color, int32_t *tex_id,
                             float *roughness, float *metallic,
                             int32_t *normal_tex_id, UvXform *uv_xform,
                             ScalarTex *rough_tex, ScalarTex *metal_tex,
                             Vec3 *emission, int32_t *emission_tex_id,
                             float *occlusion, ScalarTex *occ_tex,
                             float *opacity = nullptr,
                             bool *vertex_color = nullptr) {
  // Geometry display primvars: the unmaterialed base color/opacity (e.g. ALab
  // geom-only meshes). The first value seeds the constant base; a >1-element
  // array is per-vertex/faceVarying/uniform and sets *vertex_color so the stream
  // stores per-corner colors (see AddRTPreviewMeshNext). A bound material below
  // overrides the constant color.
  if (const tinyusdz::next::Value *dcv =
          mesh.GetPropertyValue("primvars:displayColor")) {
    if (const std::vector<float> *dc = dcv->as_float_array()) {
      if (dc->size() >= 3) *base_color = Vec3{(*dc)[0], (*dc)[1], (*dc)[2]};
      if (vertex_color && dc->size() > 3) *vertex_color = true;
    }
  }
  if (opacity) {
    if (const tinyusdz::next::Value *dov =
            mesh.GetPropertyValue("primvars:displayOpacity")) {
      if (const std::vector<float> *od = dov->as_float_array()) {
        if (!od->empty()) *opacity = std::min(1.0f, std::max(0.0f, (*od)[0]));
        if (vertex_color && od->size() > 1) *vertex_color = true;
      }
    }
  }
  const std::vector<tinyusdz::next::Path> *bind =
      mesh.GetRelationship("material:binding");
  if (!bind || bind->empty()) return;
  tinyusdz::next::UsdPrim mat = stage.GetPrimAtPath((*bind)[0]);
  if (!mat.IsValid()) return;
  tinyusdz::next::UsdPrim surf = ConnectedPrimNext(stage, mat, "outputs:surface");
  if (!surf.IsValid()) {
    surf = ConnectedPrimNext(stage, mat, "outputs:mtlx:surface");
  }
  if (!surf.IsValid()) return;

  // Scalar PBR params (UsdPreviewSurface inputs:roughness / inputs:metallic).
  if (const tinyusdz::next::Value *r = surf.GetPropertyValue("inputs:roughness")) {
    if (const float *f = r->as_float()) *roughness = std::min(1.0f, std::max(0.0f, *f));
  }
  if (const tinyusdz::next::Value *m = surf.GetPropertyValue("inputs:metallic")) {
    if (const float *f = m->as_float()) *metallic = std::min(1.0f, std::max(0.0f, *f));
  }
  // Roughness/metallic textures (channel-aware; ORM packing).
  if (rough_tex)
    ResolveScalarTextureNext(stage, surf, "inputs:roughness", tc, rough_tex);
  if (metal_tex)
    ResolveScalarTextureNext(stage, surf, "inputs:metallic", tc, metal_tex);

  // Occlusion (AO) scalar + optional texture.
  if (const tinyusdz::next::Value *o = surf.GetPropertyValue("inputs:occlusion"))
    if (const float *f = o->as_float())
      if (occlusion) *occlusion = std::min(1.0f, std::max(0.0f, *f));
  if (occ_tex)
    ResolveScalarTextureNext(stage, surf, "inputs:occlusion", tc, occ_tex);

  // Emissive color: constant + optional UsdUVTexture (sRGB color).
  if (const tinyusdz::next::Value *e =
          surf.GetPropertyValue("inputs:emissiveColor"))
    if (const float *f = e->as_float3())
      if (emission) *emission = Vec3{f[0], f[1], f[2]};
  if (emission_tex_id) {
    tinyusdz::next::UsdPrim etex =
        ConnectedPrimNext(stage, surf, "inputs:emissiveColor");
    if (etex.IsValid()) {
      if (const tinyusdz::next::Value *fv =
              etex.GetPropertyValue("inputs:file")) {
        const std::string *ap = fv->as_asset_path();
        if (!ap) ap = fv->as_string();
        if (ap && !ap->empty()) {
          WrapMode ws = WrapMode::Repeat, wt = WrapMode::Repeat;
          if (const tinyusdz::next::Value *v = etex.GetPropertyValue("inputs:wrapS"))
            if (const std::string *t = v->as_token()) ws = ParseWrapMode(*t);
          if (const tinyusdz::next::Value *v = etex.GetPropertyValue("inputs:wrapT"))
            if (const std::string *t = v->as_token()) wt = ParseWrapMode(*t);
          int32_t id = LoadTextureCached(tc, *ap, ws, wt, /*srgb=*/true);
          if (id >= 0) {
            *emission_tex_id = id;
            if (emission) *emission = Vec3{1.0f, 1.0f, 1.0f};  // texture is the tint
          }
        }
      }
    }
  }

  // Constant diffuse color (also the texture's fallback tint).
  if (const tinyusdz::next::Value *dc =
          surf.GetPropertyValue("inputs:diffuseColor")) {
    if (const float *f = dc->as_float3()) {
      *base_color = Vec3{f[0], f[1], f[2]};
    }
  }
  // Diffuse texture: inputs:diffuseColor -> UsdUVTexture(inputs:file), honoring
  // its wrapS/wrapT and sourceColorSpace (sRGB by default for color).
  tinyusdz::next::UsdPrim tex =
      ConnectedPrimNext(stage, surf, "inputs:diffuseColor");
  if (tex.IsValid()) {
    if (const tinyusdz::next::Value *fv = tex.GetPropertyValue("inputs:file")) {
      const std::string *ap = fv->as_asset_path();
      if (!ap) ap = fv->as_string();
      if (ap && !ap->empty()) {
        WrapMode ws = WrapMode::Repeat, wt = WrapMode::Repeat;
        if (const tinyusdz::next::Value *v = tex.GetPropertyValue("inputs:wrapS"))
          if (const std::string *t = v->as_token()) ws = ParseWrapMode(*t);
        if (const tinyusdz::next::Value *v = tex.GetPropertyValue("inputs:wrapT"))
          if (const std::string *t = v->as_token()) wt = ParseWrapMode(*t);
        bool srgb = true;
        if (const tinyusdz::next::Value *v =
                tex.GetPropertyValue("inputs:sourceColorSpace")) {
          if (const std::string *t = v->as_token()) srgb = (*t != "raw");
        }
        // inputs:scale/bias tint the sampled color (default identity).
        Vec3 sc{1.0f, 1.0f, 1.0f}, bi{0.0f, 0.0f, 0.0f};
        if (const tinyusdz::next::Value *v = tex.GetPropertyValue("inputs:scale"))
          if (const float *f = v->as_float3()) sc = Vec3{f[0], f[1], f[2]};
        if (const tinyusdz::next::Value *v = tex.GetPropertyValue("inputs:bias"))
          if (const float *f = v->as_float3()) bi = Vec3{f[0], f[1], f[2]};
        int32_t id = LoadTextureCached(tc, *ap, ws, wt, srgb, sc, bi);
        if (id >= 0) {
          *tex_id = id;
          // A textured surface tints by the texture, not the (often unauthored)
          // diffuseColor constant. Use white so sampling shows true texels.
          *base_color = Vec3{1.0f, 1.0f, 1.0f};
          if (uv_xform) *uv_xform = ResolveUvXform(stage, tex);
        }
      }
    }
  }

  // Tangent-space normal map: inputs:normal -> UsdUVTexture(inputs:file). Always
  // raw (non-sRGB); scale/bias default to the UsdPreviewSurface convention
  // (2,-1) that unpacks a [0,1] texel to a [-1,1] tangent-space normal.
  tinyusdz::next::UsdPrim ntex = ConnectedPrimNext(stage, surf, "inputs:normal");
  if (ntex.IsValid()) {
    if (const tinyusdz::next::Value *fv = ntex.GetPropertyValue("inputs:file")) {
      const std::string *ap = fv->as_asset_path();
      if (!ap) ap = fv->as_string();
      if (ap && !ap->empty()) {
        WrapMode ws = WrapMode::Repeat, wt = WrapMode::Repeat;
        if (const tinyusdz::next::Value *v = ntex.GetPropertyValue("inputs:wrapS"))
          if (const std::string *t = v->as_token()) ws = ParseWrapMode(*t);
        if (const tinyusdz::next::Value *v = ntex.GetPropertyValue("inputs:wrapT"))
          if (const std::string *t = v->as_token()) wt = ParseWrapMode(*t);
        Vec3 scale{2.0f, 2.0f, 2.0f}, bias{-1.0f, -1.0f, -1.0f};
        if (const tinyusdz::next::Value *v = ntex.GetPropertyValue("inputs:scale"))
          if (const float *f = v->as_float3()) scale = Vec3{f[0], f[1], f[2]};
        if (const tinyusdz::next::Value *v = ntex.GetPropertyValue("inputs:bias"))
          if (const float *f = v->as_float3()) bias = Vec3{f[0], f[1], f[2]};
        int32_t id =
            LoadTextureCached(tc, *ap, ws, wt, /*srgb=*/false, scale, bias);
        if (id >= 0) {
          *normal_tex_id = id;
          if (uv_xform && uv_xform->identity)
            *uv_xform = ResolveUvXform(stage, ntex);
        }
      }
    }
  }
}

// The full resolved-material result for one bound material (everything
// ResolveMeshMaterialNext writes). Memoized by bound-material path so a scene
// with many meshes sharing few materials (e.g. Island's 605k coral meshes over a
// handful of coral materials) resolves each material — and its shader-graph walk
// + texture lookups — exactly once instead of per mesh.

// Resolve a mesh job's material with per-material memoization. The result is a
// pure function of the bound material (+ shared texture cache), so a cache hit
// skips the shader-graph walk entirely. Unbound meshes keep MeshJobNext defaults.
void ResolveMeshMaterialCached(
    const tinyusdz::next::Stage &stage, const tinyusdz::next::UsdPrim &mesh,
    TextureCache &tc, std::unordered_map<std::string, ResolvedMat> &cache,
    MeshJobNext *job) {
  const std::vector<tinyusdz::next::Path> *bind =
      mesh.GetRelationship("material:binding");
  const std::string key =
      (bind && !bind->empty()) ? (*bind)[0].str() : std::string();
  if (!key.empty()) {
    auto it = cache.find(key);
    if (it != cache.end()) {
      const ResolvedMat &r = it->second;
      job->base_color = r.base_color;
      job->tex_id = r.tex_id;
      job->roughness = r.roughness;
      job->metallic = r.metallic;
      job->normal_tex_id = r.normal_tex_id;
      job->uv_xform = r.uv_xform;
      job->rough_tex = r.rough_tex;
      job->metal_tex = r.metal_tex;
      job->emission = r.emission;
      job->emission_tex_id = r.emission_tex_id;
      job->occlusion = r.occlusion;
      job->occ_tex = r.occ_tex;
      job->opacity = r.opacity;
      job->vertex_color = r.vertex_color;
      return;
    }
  }
  ResolveMeshMaterialNext(stage, mesh, tc, &job->base_color, &job->tex_id,
                          &job->roughness, &job->metallic, &job->normal_tex_id,
                          &job->uv_xform, &job->rough_tex, &job->metal_tex,
                          &job->emission, &job->emission_tex_id, &job->occlusion,
                          &job->occ_tex, &job->opacity, &job->vertex_color);
  if (!key.empty()) {
    ResolvedMat r;
    r.base_color = job->base_color;
    r.tex_id = job->tex_id;
    r.roughness = job->roughness;
    r.metallic = job->metallic;
    r.normal_tex_id = job->normal_tex_id;
    r.uv_xform = job->uv_xform;
    r.rough_tex = job->rough_tex;
    r.metal_tex = job->metal_tex;
    r.emission = job->emission;
    r.emission_tex_id = job->emission_tex_id;
    r.occlusion = job->occlusion;
    r.occ_tex = job->occ_tex;
    r.opacity = job->opacity;
    r.vertex_color = job->vertex_color;
    cache.emplace(key, r);
  }
}

// True when `path` is one of the mask paths or a descendant of one. An empty
// mask matches everything.
bool PathMatchesMask(const std::string &path,
                     const std::vector<std::string> &mask) {
  if (mask.empty()) return true;
  for (const std::string &m : mask) {
    if (path == m) return true;
    if (path.size() > m.size() && path.compare(0, m.size(), m) == 0 &&
        path[m.size()] == '/') {
      return true;
    }
  }
  return false;
}

// True if any of the prim's authored xform ops are time-sampled (so its local
// transform varies with time).
bool PrimHasAnimatedXform(const tinyusdz::next::UsdPrim &prim) {
  const tinyusdz::next::Value *orderv = prim.GetPropertyValue("xformOpOrder");
  const std::vector<std::string> *order =
      orderv ? orderv->as_token_array() : nullptr;
  if (!order) return false;
  for (const std::string &raw : *order) {
    std::string op = raw;
    if (op.rfind("!invert!", 0) == 0) op = op.substr(8);
    if (op == "!resetXformStack!") continue;
    if (prim.HasTimeSamples(op)) return true;
  }
  return false;
}

// True if the mesh's own geometry (points/topology) is time-sampled.
bool MeshHasAnimatedGeom(const tinyusdz::next::UsdPrim &prim) {
  return prim.HasTimeSamples("points") ||
         prim.HasTimeSamples("faceVertexIndices") ||
         prim.HasTimeSamples("faceVertexCounts");
}

// True if the subtree contains a rendered (masked) Mesh whose world-space
// geometry varies with time: either the mesh's own points/topology are
// time-sampled, or some xform op on the path (this prim or an ancestor) is.
// Cameras and non-rendered prims are ignored, so camera-only animation does not
// flag the geometry as dynamic (the BVH can then be reused across frames).
bool SubtreeGeometryAnimated(const tinyusdz::next::UsdPrim &prim,
                             const std::vector<std::string> &mask,
                             bool ancestor_xform_animated) {
  const bool xform_anim =
      ancestor_xform_animated || PrimHasAnimatedXform(prim);
  if (prim.GetTypeName() == "Mesh" &&
      PathMatchesMask(prim.GetPath().str(), mask)) {
    if (xform_anim || MeshHasAnimatedGeom(prim)) return true;
  }
  for (const tinyusdz::next::UsdPrim &child : prim.GetChildren()) {
    if (SubtreeGeometryAnimated(child, mask, xform_anim)) return true;
  }
  return false;
}

bool SceneGeometryAnimated(const tinyusdz::next::Stage &stage,
                           const std::vector<std::string> &mask) {
  for (const tinyusdz::next::UsdPrim &root : stage.GetRootPrims()) {
    if (SubtreeGeometryAnimated(root, mask, false)) return true;
  }
  return false;
}

// Serial walk: resolve parent-dependent world matrices (at `time`) + purpose and
// flatten Mesh prims into jobs. `mask` (if non-empty) restricts emission to
// meshes at/under those prim paths (usdrecord --mask); the full tree is still
// walked so transform chains remain correct.
void CollectRTPreviewMeshesNext(const tinyusdz::next::Stage &stage,
                                const tinyusdz::next::UsdPrim &prim,
                                const matrix4d &parent_world,
                                tinyusdz::Purpose inherited_purpose, double time,
                                const std::vector<std::string> &mask,
                                std::vector<MeshJobNext> *jobs) {
  if (!prim.IsActive()) return;  // inactive prim + its subtree are pruned
  double dmat[16];
  tinyusdz::tydra::next::ComputeLocalTransform(prim, dmat, time);
  const matrix4d local = Mat4FromArray(dmat);
  const bool reset = tinyusdz::tydra::next::HasResetXformStack(prim);
  const matrix4d world = reset ? local : (local * parent_world);

  tinyusdz::Purpose purpose = inherited_purpose;
  if (const tinyusdz::next::Value *pv = prim.GetPropertyValue("purpose")) {
    if (const std::string *t = pv->as_token()) {
      if (*t == "render") {
        purpose = tinyusdz::Purpose::Render;
      } else if (*t == "proxy") {
        purpose = tinyusdz::Purpose::Proxy;
      } else if (*t == "guide") {
        purpose = tinyusdz::Purpose::Guide;
      }
      // "default"/unknown: keep the inherited purpose.
    }
  }

  // Nested instancing: a prototype subtree may itself contain a PointInstancer or a
  // scenegraph (instanceable) instance. That geometry is NOT baked into this
  // prototype's base BLAS -- CollectProtoMeshNesting records those placements
  // separately and the TLAS flattens them (one level, composed per outer
  // placement). So do not descend here (mirror CollectSceneSplit). No-op for the
  // common leaf prototype (plain meshes), so non-nested scenes are byte-identical.
  if (prim.GetTypeName() == "PointInstancer") return;
  {
    const tinyusdz::next::PrimSpec *ispec = prim.GetPrimSpec();
    if (ispec && !ispec->meta().instance_prototype().empty()) return;
  }
  if (prim.GetTypeName() == "Mesh" &&
      PathMatchesMask(prim.GetPath().str(), mask)) {
    MeshJobNext job;
    job.prim = prim;
    job.world = world;
    job.purpose = purpose;
    jobs->push_back(std::move(job));
  }
  for (const tinyusdz::next::UsdPrim &child : prim.GetChildren()) {
    CollectRTPreviewMeshesNext(stage, child, world, purpose, time, mask, jobs);
  }
}

// `next`-path UsdVol volumes: serial walk resolving world matrices; for each
// Volume prim, follow `field:*` -> field-asset prim -> filePath, load the .vdb
// (relative to `baseDir`), and build a VolumeData for raymarching.
void CollectVolumesNext(const tinyusdz::next::Stage &stage,
                        const tinyusdz::next::UsdPrim &prim,
                        const matrix4d &parent_world, double time,
                        const std::string &baseDir,
                        std::vector<VolumeData> *out) {
  if (!prim.IsActive()) return;
  double dmat[16];
  tinyusdz::tydra::next::ComputeLocalTransform(prim, dmat, time);
  const matrix4d local = Mat4FromArray(dmat);
  const bool reset = tinyusdz::tydra::next::HasResetXformStack(prim);
  const matrix4d world = reset ? local : (local * parent_world);

  if (prim.GetTypeName() == "Volume") {
    for (const std::string &relName : prim.GetRelationshipNames()) {
      if (relName.rfind("field:", 0) != 0) continue;
      const std::vector<tinyusdz::next::Path> *targets =
          prim.GetRelationship(relName);
      if (!targets || targets->empty()) continue;
      tinyusdz::next::UsdPrim field = stage.GetPrimAtPath((*targets)[0]);
      if (!field) continue;

      const tinyusdz::next::Value *fp = field.GetPropertyValue("filePath");
      const std::string *ap = fp ? fp->as_asset_path() : nullptr;
      if (!ap || ap->empty()) continue;
      std::string fieldName = relName.substr(std::strlen("field:"));
      if (const tinyusdz::next::Value *fn = field.GetPropertyValue("fieldName")) {
        if (const std::string *tk = fn->as_token()) fieldName = *tk;
      }
      std::string vpath = *ap;
      if (!vpath.empty() && vpath[0] != '/' && !baseDir.empty()) {
        vpath = baseDir + "/" + vpath;
      }
      std::vector<tinyusdz::usdVol::VDBGrid> grids;
      std::string vw, ve;
      if (!tinyusdz::usdVol::ReadVDBFromFile(vpath, &grids, &vw, &ve) ||
          grids.empty()) {
        continue;
      }
      const tinyusdz::usdVol::VDBGrid *g = nullptr;
      for (const auto &gg : grids)
        if (gg.name == fieldName) { g = &gg; break; }
      if (!g) g = &grids[0];
      if (g->data.empty() || g->dim[0] <= 0 || g->dim[1] <= 0 || g->dim[2] <= 0)
        continue;

      VolumeData vd;
      vd.dim[0] = g->dim[0];
      vd.dim[1] = g->dim[1];
      vd.dim[2] = g->dim[2];
      vd.density = g->data;
      float lo[3], hi[3];
      for (int a = 0; a < 3; a++) {
        lo[a] = float(g->origin[a]) * float(g->voxel_size[a]) +
                float(g->world_translation[a]);
        hi[a] = float(g->origin[a] + g->dim[a]) * float(g->voxel_size[a]) +
                float(g->world_translation[a]);
      }
      vd.bmin = Vec3{lo[0], lo[1], lo[2]};
      vd.bmax = Vec3{hi[0], hi[1], hi[2]};
      matrix4d invw;
      if (!tinyusdz::inverse(world, invw, 1.0e-12)) invw = matrix4d::identity();
      vd.inv_world = invw;
      vd.background = g->background;
      out->push_back(std::move(vd));
    }
  }
  for (const tinyusdz::next::UsdPrim &child : prim.GetChildren()) {
    CollectVolumesNext(stage, child, world, time, baseDir, out);
  }
}

// Read a UsdGeomCamera attribute (float, with double fallback).
float ReadCamFloatNext(const tinyusdz::next::UsdPrim &prim, const char *name,
                       float fallback) {
  if (const tinyusdz::next::Value *v = prim.GetPropertyValue(name)) {
    if (const float *f = v->as_float()) return *f;
    if (const double *d = v->as_double()) return float(*d);
  }
  return fallback;
}

// Find a (named) UsdGeomCamera in the next stage and build a CameraFrame plus
// its aperture aspect (horizontal/vertical). An empty query matches the first
// camera. Mirrors CameraFrameFromGeomCamera but on the next stage with
// bit-exact world transforms.
bool FindNextCameraFrameRecursive(const tinyusdz::next::Stage &stage,
                                  const tinyusdz::next::UsdPrim &prim,
                                  const matrix4d &parent_world,
                                  const std::string &query, double time,
                                  CameraFrame *frame, float *aspect) {
  double dmat[16];
  tinyusdz::tydra::next::ComputeLocalTransform(prim, dmat, time);
  const matrix4d local = Mat4FromArray(dmat);
  const bool reset = tinyusdz::tydra::next::HasResetXformStack(prim);
  const matrix4d world = reset ? local : (local * parent_world);

  if (prim.GetTypeName() == "Camera") {
    const std::string path = prim.GetPath().str();
    const std::string name = prim.GetName();
    const bool match =
        query.empty() || name == query || path == query ||
        (path.size() > query.size() &&
         path.compare(path.size() - query.size(), query.size(), query) == 0 &&
         path[path.size() - query.size() - 1] == '/');
    if (match) {
      const float focal = ReadCamFloatNext(prim, "focalLength", 50.0f);
      const float vap = ReadCamFloatNext(prim, "verticalAperture", 15.2908f);
      const float hap = ReadCamFloatNext(prim, "horizontalAperture", 20.955f);
      float znear = 0.1f, zfar = 1.0e6f;
      if (const tinyusdz::next::Value *v = prim.GetPropertyValue("clippingRange")) {
        if (const float *f = v->as_float2()) { znear = f[0]; zfar = f[1]; }
      }
      std::string proj = "perspective";
      if (const tinyusdz::next::Value *v = prim.GetPropertyValue("projection")) {
        if (const std::string *t = v->as_token()) proj = *t;
      }
      frame->origin = Vec3{float(world.m[3][0]), float(world.m[3][1]),
                           float(world.m[3][2])};
      frame->right = Normalize(TransformVector(world, Vec3{1.0f, 0.0f, 0.0f}));
      frame->up = Normalize(TransformVector(world, Vec3{0.0f, 1.0f, 0.0f}));
      frame->forward = Normalize(TransformVector(world, Vec3{0.0f, 0.0f, -1.0f}));
      frame->yfov = 2.0f * std::atan(0.5f * vap / std::max(1.0e-6f, focal));
      frame->xmag = hap;
      frame->ymag = vap;
      frame->znear = std::max(1.0e-5f, znear);
      frame->zfar = std::max(frame->znear, zfar);
      frame->ortho = (proj == "orthographic");
      if (aspect) *aspect = hap / std::max(1.0e-6f, vap);
      return true;
    }
  }
  for (const tinyusdz::next::UsdPrim &child : prim.GetChildren()) {
    if (FindNextCameraFrameRecursive(stage, child, world, query, time, frame,
                                     aspect)) {
      return true;
    }
  }
  return false;
}

bool FindNextCameraFrame(const tinyusdz::next::Stage &stage,
                         const std::string &query, double time,
                         CameraFrame *frame, float *aspect) {
  for (const tinyusdz::next::UsdPrim &root : stage.GetRootPrims()) {
    if (FindNextCameraFrameRecursive(stage, root, matrix4d::identity(), query,
                                     time, frame, aspect)) {
      return true;
    }
  }
  return false;
}

// OpenUSD usdrecord-style auto framing: replicates
// UsdAppUtilsFrameRecorder's default-GfCamera framing (focal 50mm, aperture
// 20.955 x 15.2908 -> horizontal FOV ~23.66 deg, aspect ~1.37). Positions a
// perspective camera on the depth axis so the bbox fits the horizontal FOV,
// front-on for Y-up and rotated for Z-up. Writes the aperture-derived image
// height (width / aspect) to `out_height`.
CameraFrame MakeUsdRecordCamera(const Bounds &bounds, tinyusdz::Axis up_axis,
                                int width, int *out_height) {
  constexpr float kPi = 3.14159265358979323846f;
  const float focal = 50.0f, hap = 20.955f, vap = 15.2908f;
  const float aspect = hap / vap;  // ~1.370
  const float half_hfov = std::atan(0.5f * hap / focal);
  if (out_height) {
    *out_height = std::max(1, int(std::lround(float(width) / aspect)));
  }

  Vec3 center{0.0f, 0.0f, 0.0f};
  Vec3 dim{2.0f, 2.0f, 2.0f};
  if (bounds.valid) {
    center = Mul(Add(bounds.lo, bounds.hi), 0.5f);
    dim = Sub(bounds.hi, bounds.lo);
  }

  CameraFrame frame;
  frame.yfov = 2.0f * std::atan(0.5f * vap / focal);
  frame.ortho = false;

  // Plane corner (half-extents in the focal plane) and the depth half-extent.
  float plane_x, plane_y, depth;
  Vec3 pos_dir, up_vec, fwd;
  if (up_axis == tinyusdz::Axis::Z) {
    plane_x = dim.x * 0.5f; plane_y = dim.z * 0.5f; depth = dim.y * 0.5f;
    pos_dir = Vec3{0.0f, -1.0f, 0.0f};  // back up along -Y
    fwd = Vec3{0.0f, 1.0f, 0.0f};
    up_vec = Vec3{0.0f, 0.0f, 1.0f};
  } else if (up_axis == tinyusdz::Axis::X) {
    plane_x = dim.y * 0.5f; plane_y = dim.z * 0.5f; depth = dim.x * 0.5f;
    pos_dir = Vec3{-1.0f, 0.0f, 0.0f};
    fwd = Vec3{1.0f, 0.0f, 0.0f};
    up_vec = Vec3{0.0f, 0.0f, 1.0f};
  } else {  // Y-up
    plane_x = dim.x * 0.5f; plane_y = dim.y * 0.5f; depth = dim.z * 0.5f;
    pos_dir = Vec3{0.0f, 0.0f, 1.0f};  // back up along +Z (look down -Z)
    fwd = Vec3{0.0f, 0.0f, -1.0f};
    up_vec = Vec3{0.0f, 1.0f, 0.0f};
  }
  const float plane_radius =
      std::sqrt(plane_x * plane_x + plane_y * plane_y);
  float distance = plane_radius / std::max(1.0e-6f, std::tan(half_hfov)) + depth;
  (void)kPi;

  frame.origin = Add(center, Mul(pos_dir, distance));
  frame.forward = Normalize(fwd);
  frame.right = Normalize(Cross(frame.forward, up_vec));
  if (Length(frame.right) < 1.0e-6f) frame.right = Vec3{1.0f, 0.0f, 0.0f};
  frame.up = Normalize(Cross(frame.right, frame.forward));
  const float diag = Length(dim);
  frame.znear = std::max(1.0e-4f, distance - diag);
  frame.zfar = distance + diag * 2.0f;
  return frame;
}

// Persistent render context: the loaded next stage, extracted geometry, and the
// built BVH are kept alive so the camera/render parameters can be changed and
// the scene re-rendered repeatedly without re-parsing or rebuilding the BVH
// (memory-persistent rendering, e.g. animation with a moving camera).

// Resolve the camera (named / autoframe / auto-fit) into ctx.camera and the
// image height into ctx.height, from the current ctx.opt + ctx.bounds.
void ResolveCameraNext(RenderContext &ctx) {
  const Options &opt = ctx.opt;
  RenderScene empty_render_scene;
  int height = opt.height;
  if (!opt.camera.empty()) {
    float cam_aspect = 16.0f / 9.0f;
    if (FindNextCameraFrame(ctx.stage, opt.camera, ctx.frame_time, &ctx.camera,
                            &cam_aspect)) {
      if (height <= 0) {
        height = std::max(1, int(std::lround(float(ctx.width) / cam_aspect)));
      }
    } else {
      std::cerr << "WARN: camera not found: " << opt.camera
                << ". Using auto-fit.\n";
      if (height <= 0) height = 540;
      Options auto_opt = opt;
      auto_opt.camera.clear();
      auto_opt.width = ctx.width;
      ctx.camera = MakeCameraFrame(empty_render_scene, auto_opt, ctx.bounds,
                                   height, ctx.up_axis);
    }
  } else if (opt.autoframe) {
    ctx.camera = MakeUsdRecordCamera(ctx.bounds, ctx.up_axis, ctx.width, &height);
  } else {
    if (height <= 0) height = 540;
    Options auto_opt = opt;
    auto_opt.camera.clear();
    auto_opt.width = ctx.width;
    ctx.camera = MakeCameraFrame(empty_render_scene, auto_opt, ctx.bounds,
                                 height, ctx.up_axis);
  }
  ctx.height = height;
}

// Prototype BLAS to build: the holder prim's path + the inherited purpose
// context it was instanced under (part of the dedup key, so instances under a
// guide ancestor get a separate, purpose-culled BLAS).

// A curve prim (UsdGeomBasisCurves / NurbsCurves) to ray-trace as hair strands
// in the next path. `world` is the world transform; the linear-strand geometry is
// built into the RenderContext's DirectScene (shared by the flat and TLAS render
// paths) — see BuildNextCurves.

bool IsCurvePrimNext(const tinyusdz::next::UsdPrim &prim) {
  const std::string &t = prim.GetTypeName();
  return t == "BasisCurves" || t == "NurbsCurves";
}

// Read a curve prim's `points` into a point3f vector via the lazy float accessor.
std::vector<tinyusdz::value::point3f> ReadCurvePointsNext(
    const tinyusdz::next::UsdPrim &prim, double time) {
  const std::vector<float> pf = ReadFloatArrayLazy(prim, "points", time);
  std::vector<tinyusdz::value::point3f> pts(pf.size() / 3);
  for (size_t i = 0; i < pts.size(); ++i)
    pts[i] = {pf[3 * i + 0], pf[3 * i + 1], pf[3 * i + 2]};
  return pts;
}

// Build the collected curve jobs into the RenderContext's DirectScene as LightRT
// hair-strand scenes (round by default; flat/ribbon when the prim authors
// `normals`), reusing AppendLinearCurveStrands + the same intersectors the
// legacy direct path uses. Curve hits/occlusion are then traced by RenderImage's
// existing DirectScene path regardless of use_tlas. Returns false only on a
// LightRT build failure.
bool BuildNextCurves(RenderContext &ctx, const std::vector<CurveJobNext> &jobs,
                     double time) {
  if (jobs.empty()) return true;
  std::vector<float> round_points, round_radii, flat_points, flat_radii;
  std::vector<uint32_t> round_first, round_count, flat_first, flat_count;
  for (const CurveJobNext &job : jobs) {
    std::vector<tinyusdz::value::point3f> points =
        ReadCurvePointsNext(job.prim, time);
    std::vector<int32_t> counts32 =
        ReadIntArrayLazy(job.prim, "curveVertexCounts", time);
    if (points.empty() || counts32.empty()) continue;
    std::vector<int> counts(counts32.begin(), counts32.end());
    std::vector<float> widths = ReadFloatArrayLazy(job.prim, "widths", time);
    const bool flat = job.prim.GetPropertyValue("normals") != nullptr;
    if (flat) {
      AppendLinearCurveStrands(points, counts, widths, job.world, &flat_points,
                               &flat_radii, &flat_first, &flat_count,
                               &ctx.direct.flat_curve_info, &ctx.bounds);
    } else {
      AppendLinearCurveStrands(points, counts, widths, job.world, &round_points,
                               &round_radii, &round_first, &round_count,
                               &ctx.direct.round_curve_info, &ctx.bounds);
    }
  }

  lrt_tri_build_options build_opts;
  std::memset(&build_opts, 0, sizeof(build_opts));
  build_opts.quality = ctx.opt.quality;
  build_opts.layout = LRT_TRI_LAYOUT_AUTO;
  build_opts.num_threads = WorkerThreadCount(ctx.opt.threads);
  lrt_result lrt_err = LRT_RESULT_OK;
  auto make_strands = [](const std::vector<float> &pts,
                         const std::vector<float> &radii,
                         const std::vector<uint32_t> &first,
                         const std::vector<uint32_t> &count) {
    lrt_hair_strands s;
    std::memset(&s, 0, sizeof(s));
    s.points = pts.data();
    s.radius = radii.data();
    s.strand_first = first.data();
    s.strand_count = count.data();
    s.nstrands = first.size();
    s.npoints = radii.size();
    return s;
  };
  if (!round_first.empty()) {
    lrt_hair_strands s =
        make_strands(round_points, round_radii, round_first, round_count);
    ctx.direct.round_curves.reset(
        lrt_roundcurve_scene_build(&s, &build_opts, &lrt_err));
    if (!ctx.direct.round_curves) {
      std::cerr << "Failed to build LightRT round curve scene.\n";
      return false;
    }
  }
  if (!flat_first.empty()) {
    lrt_hair_strands s =
        make_strands(flat_points, flat_radii, flat_first, flat_count);
    ctx.direct.flat_curves.reset(
        lrt_flatcurve_scene_build(&s, &build_opts, &lrt_err));
    if (!ctx.direct.flat_curves) {
      std::cerr << "Failed to build LightRT flat curve scene.\n";
      return false;
    }
  }
  return true;
}

// Build a UsdGeomPointInstancer per-instance object->world matrix in the
// row-vector convention (p' = p * M): scale, then orient, then translate, all in
// the instancer's local space (USD's instance transform order). `quat_xyzw` is
// the orientation as stored by the next loader (imaginary x,y,z then real w);
// `scale3`/`pos` are per-axis scale and translation.
matrix4d InstanceTRS(const float *pos, const float *quat_xyzw,
                     const float *scale3) {
  tinyusdz::value::quatf q;
  q.imag[0] = quat_xyzw[0];
  q.imag[1] = quat_xyzw[1];
  q.imag[2] = quat_xyzw[2];
  q.real = quat_xyzw[3];
  // 3x3 rotation in the same convention as the rest of the xform stack.
  tinyusdz::value::matrix3d rot = tinyusdz::to_matrix3x3(q);
  // p * S * R with S diagonal scales row i of R by scale[i].
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) rot.m[i][j] *= double(scale3[i]);
  tinyusdz::value::double3 t{double(pos[0]), double(pos[1]), double(pos[2])};
  return tinyusdz::to_matrix(rot, t);  // translation into row 3
}

// Recursively collect curve prims under `prim`, accumulating world transforms in
// the row-vector convention. Used both at scene level and to gather a
// PointInstancer prototype's curves (relative to the prototype root).
void CollectCurvesNextRec(const tinyusdz::next::UsdPrim &prim,
                          const matrix4d &parent_world,
                          tinyusdz::Purpose inherited_purpose, double time,
                          std::vector<CurveJobNext> *out) {
  if (!prim.IsActive()) return;  // inactive prim + its subtree are pruned
  double dmat[16];
  tinyusdz::tydra::next::ComputeLocalTransform(prim, dmat, time);
  const matrix4d local = Mat4FromArray(dmat);
  const bool reset = tinyusdz::tydra::next::HasResetXformStack(prim);
  const matrix4d world = reset ? local : (local * parent_world);
  tinyusdz::Purpose purpose = inherited_purpose;
  if (const tinyusdz::next::Value *pv = prim.GetPropertyValue("purpose")) {
    if (const std::string *t = pv->as_token()) {
      if (*t == "render") purpose = tinyusdz::Purpose::Render;
      else if (*t == "proxy") purpose = tinyusdz::Purpose::Proxy;
      else if (*t == "guide") purpose = tinyusdz::Purpose::Guide;
    }
  }
  // Nested instancers under a prototype are NOT baked into its curve BLAS -- their
  // (curve) instancing is flattened separately, like the mesh path (else the nested
  // instancer's scatter collapses to a single curve copy). No-op for plain curve
  // prototypes.
  if (prim.GetTypeName() == "PointInstancer") return;
  {
    const tinyusdz::next::PrimSpec *s = prim.GetPrimSpec();
    if (s && !s->meta().instance_prototype().empty()) return;
  }
  if (IsCurvePrimNext(prim)) {
    CurveJobNext cj;
    cj.prim = prim;
    cj.world = world;
    cj.purpose = purpose;
    out->push_back(std::move(cj));
  }
  for (const tinyusdz::next::UsdPrim &child : prim.GetChildren())
    CollectCurvesNextRec(child, world, purpose, time, out);
}

// Collect a PointInstancer prototype's curves with transforms relative to the
// prototype root (root at identity, replaced by the instance transform).
void CollectProtoCurves(const tinyusdz::next::Stage &stage,
                        const std::string &proto_path,
                        tinyusdz::Purpose start_purpose, double time,
                        std::vector<CurveJobNext> *out) {
  tinyusdz::next::UsdPrim proto = stage.GetPrimAtPath(proto_path);
  if (!proto.IsValid()) return;
  if (IsCurvePrimNext(proto)) {
    CurveJobNext cj;
    cj.prim = proto;
    cj.world = matrix4d::identity();
    cj.purpose = start_purpose;
    out->push_back(std::move(cj));
  }
  for (const tinyusdz::next::UsdPrim &child : proto.GetChildren())
    CollectCurvesNextRec(child, matrix4d::identity(), start_purpose, time, out);
}

// Reserve (deduped by path+purpose) a curve BLAS for a prototype's curves, shared
// by PointInstancer and native-instance placements. Returns its index in
// curve_inst->protos, or -1 if curve_inst is null or the prototype has no curves.
int32_t ReserveCurveProto(const tinyusdz::next::Stage &stage,
                          const std::string &proto_path,
                          tinyusdz::Purpose purpose, double time,
                          CurveProtoCollect *curve_inst) {
  if (!curve_inst) return -1;
  const std::string key =
      proto_path + "\x1f" + std::to_string(PurposeBit(purpose));
  auto it = curve_inst->ids.find(key);
  if (it != curve_inst->ids.end()) return int32_t(it->second);
  std::vector<CurveJobNext> probe;
  CollectProtoCurves(stage, proto_path, purpose, time, &probe);
  if (probe.empty()) return -1;
  const uint32_t idx = uint32_t(curve_inst->protos.size());
  curve_inst->ids[key] = idx;
  curve_inst->protos.push_back({proto_path, purpose, 0});
  return int32_t(idx);
}

// Expand a UsdGeomPointInstancer into TLAS placements: each prototype becomes a
// deduped BLAS (shared with the native-instance pool) and every visible instance
// becomes an InstanceRT placing that BLAS at scale*orient*translate composed with
// the instancer's world transform. Prototype paths come from the `prototypes`
// relationship; they are normally descendants of the instancer, so we resolve
// each target by leaf name among the instancer's children first (robust to
// whether composition re-rooted the authored target paths) and fall back to an
// absolute stage lookup. `invisibleIds` are skipped. The instancer's children
// are the prototypes, so the caller must NOT descend into it. Curve prototypes
// are deduped into a curve BLAS and instanced through the same TLAS as meshes.
void CollectPointInstancer(const tinyusdz::next::Stage &stage,
                           const tinyusdz::next::UsdPrim &instancer,
                           const matrix4d &instancer_world,
                           tinyusdz::Purpose purpose, double time,
                           const std::vector<std::string> &mask,
                           std::vector<InstanceRT> *instances,
                           std::unordered_map<std::string, uint32_t> *proto_ids,
                           std::vector<ProtoBuildReq> *protos,
                           CurveProtoCollect *curve_inst, RTPreviewStats *stats,
                           // When set, curve placements go here (in `instancer_world`
                           // space) instead of curve_inst->instances -- used to
                           // capture a NESTED instancer's curve placements per
                           // prototype for later flattening. Curve prototypes are
                           // still deduped into curve_inst.
                           std::vector<CurveInstanceRT> *curve_out = nullptr) {
  if (!PathMatchesMask(instancer.GetPath().str(), mask)) return;
  const std::vector<tinyusdz::next::Path> *targets =
      instancer.GetRelationship("prototypes");
  if (!targets || targets->empty()) return;

  // Resolve each prototype target to a live stage prim and reserve its mesh BLAS
  // id (deduped by path + purpose, matching the native-instance path) and, if the
  // prototype has curves, a curve BLAS id (also deduped) — both stored once and
  // instanced via the TLAS rather than baked per instance.
  std::unordered_map<std::string, tinyusdz::next::UsdPrim> children_by_name;
  for (const tinyusdz::next::UsdPrim &c : instancer.GetChildren())
    children_by_name.emplace(c.GetName(), c);
  std::vector<int32_t> proto_blas(targets->size(), -1);
  std::vector<int32_t> proto_curve(targets->size(), -1);  // CurveProtoCollect idx
  for (size_t pi = 0; pi < targets->size(); ++pi) {
    const tinyusdz::next::Path &tp = (*targets)[pi];
    tinyusdz::next::UsdPrim proto;
    auto cit = children_by_name.find(tp.name());
    if (cit != children_by_name.end()) proto = cit->second;
    else proto = stage.GetPrimAtPath(tp.str());
    if (!proto.IsValid()) continue;
    const std::string proto_path = proto.GetPath().str();
    const std::string key =
        proto_path + "\x1f" + std::to_string(PurposeBit(purpose));
    auto it = proto_ids->find(key);
    if (it == proto_ids->end()) {
      const uint32_t blas_id = uint32_t(protos->size()) + 1;  // blas[0] = base
      (*proto_ids)[key] = blas_id;
      protos->push_back({proto_path, purpose, blas_id});
      proto_blas[pi] = int32_t(blas_id);
    } else {
      proto_blas[pi] = int32_t(it->second);
    }
    proto_curve[pi] =
        ReserveCurveProto(stage, proto_path, purpose, time, curve_inst);
  }

  // Per-instance arrays. `positions` drives the instance count; the rest default
  // (identity orientation, unit scale, proto 0) when absent or shorter.
  const std::vector<float> positions =
      ReadFloatArrayLazy(instancer, "positions", time);
  if (positions.empty()) return;
  const size_t n = positions.size() / 3;
  const std::vector<int32_t> proto_indices =
      ReadIntArrayLazy(instancer, "protoIndices", time);
  const std::vector<float> orientations =
      ReadFloatArrayLazy(instancer, "orientations", time);
  const std::vector<float> scales = ReadFloatArrayLazy(instancer, "scales", time);
  const std::vector<int64_t> invisible =
      ReadInt64ArrayLazy(instancer, "invisibleIds", time);
  const std::unordered_set<int64_t> invisible_set(invisible.begin(),
                                                  invisible.end());

  static const float kIdentQuat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  static const float kUnitScale[3] = {1.0f, 1.0f, 1.0f};
  size_t emitted = 0;
  for (size_t i = 0; i < n; ++i) {
    if (!invisible_set.empty() && invisible_set.count(int64_t(i))) continue;
    const int32_t pidx = (i < proto_indices.size()) ? proto_indices[i] : 0;
    if (pidx < 0 || size_t(pidx) >= proto_blas.size()) continue;
    const int32_t blas_id = proto_blas[size_t(pidx)];
    const int32_t curve_idx = proto_curve[size_t(pidx)];
    if (blas_id < 0 && curve_idx < 0) continue;
    const float *q = (orientations.size() >= (i + 1) * 4) ? &orientations[i * 4]
                                                          : kIdentQuat;
    const float *s =
        (scales.size() >= (i + 1) * 3) ? &scales[i * 3] : kUnitScale;
    const matrix4d inst_world =
        InstanceTRS(&positions[i * 3], q, s) * instancer_world;
    float o2w[12];
    Mat4ToObj2World(inst_world, o2w);
    if (blas_id >= 0) {
      InstanceRT inst;
      inst.blas_id = uint32_t(blas_id);
      std::memcpy(inst.o2w, o2w, sizeof(o2w));
      instances->push_back(inst);
    }
    if (curve_idx >= 0 && curve_inst) {
      CurveInstanceRT ci;
      ci.curve_proto_idx = uint32_t(curve_idx);
      std::memcpy(ci.o2w, o2w, sizeof(o2w));
      (curve_out ? *curve_out : curve_inst->instances).push_back(ci);
    }
    emitted++;
  }
  if (stats) {
    stats->point_instancers++;
    stats->point_instances += emitted;
  }
}

// Walk the composed stage, splitting it into (a) base mesh jobs — geometry not
// under any native instance, emitted in world space — and (b) instance
// placements that reference a per-prototype BLAS. Native instances (prims with
// instance_prototype set) and UsdGeomPointInstancer prims are NOT descended
// into; instead each placement is recorded as an InstanceRT and its prototype
// (deduped by path+purpose) is queued for a BLAS build. This keeps each
// prototype's geometry stored once. Honors `mask`.
void CollectSceneSplit(const tinyusdz::next::Stage &stage,
                       const tinyusdz::next::UsdPrim &prim,
                       const matrix4d &parent_world,
                       tinyusdz::Purpose inherited_purpose, double time,
                       const std::vector<std::string> &mask,
                       std::vector<MeshJobNext> *base_jobs,
                       std::vector<InstanceRT> *instances,
                       std::unordered_map<std::string, uint32_t> *proto_ids,
                       std::vector<ProtoBuildReq> *protos,
                       std::vector<CurveJobNext> *curve_jobs,
                       CurveProtoCollect *curve_inst, RTPreviewStats *stats,
                       const std::unordered_set<std::string> *proto_holders) {
  if (!prim.IsActive()) return;  // inactive prim + its subtree are pruned
  double dmat[16];
  tinyusdz::tydra::next::ComputeLocalTransform(prim, dmat, time);
  const matrix4d local = Mat4FromArray(dmat);
  const bool reset = tinyusdz::tydra::next::HasResetXformStack(prim);
  const matrix4d world = reset ? local : (local * parent_world);

  tinyusdz::Purpose purpose = inherited_purpose;
  if (const tinyusdz::next::Value *pv = prim.GetPropertyValue("purpose")) {
    if (const std::string *t = pv->as_token()) {
      if (*t == "render") purpose = tinyusdz::Purpose::Render;
      else if (*t == "proxy") purpose = tinyusdz::Purpose::Proxy;
      else if (*t == "guide") purpose = tinyusdz::Purpose::Guide;
    }
  }

  const tinyusdz::next::PrimSpec *spec = prim.GetPrimSpec();
  std::string proto_path = spec ? spec->meta().instance_prototype() : std::string();
  // A prototype HOLDER (the target of some instance_prototype) is itself a placed
  // instanceable prim -- Pixar renders every instanceable sibling, including the one
  // composition picked as the prototype source. So emit it as an instance of its OWN
  // geometry (keyed by its own path, the same BLAS its siblings reference) rather
  // than skipping it. Its subtree is the prototype geometry (collected once via
  // CollectProtoJobs), so do not descend. (Without this, one of N instanceable
  // siblings -- e.g. one of isIronwoodA1's two trees -- silently vanished.)
  if (proto_path.empty() && proto_holders &&
      proto_holders->count(prim.GetPath().str())) {
    proto_path = prim.GetPath().str();
  }
  if (!proto_path.empty()) {
    // Native instance (or self-instancing holder): record placement + queue its
    // prototype. Do not descend (the instance proxy's children come from the
    // prototype).
    if (PathMatchesMask(prim.GetPath().str(), mask)) {
      const std::string key =
          proto_path + "\x1f" + std::to_string(PurposeBit(purpose));
      auto it = proto_ids->find(key);
      uint32_t blas_id;
      if (it == proto_ids->end()) {
        blas_id = uint32_t(protos->size()) + 1;  // blas[0] is the base scene
        (*proto_ids)[key] = blas_id;
        protos->push_back({proto_path, purpose, blas_id});
      } else {
        blas_id = it->second;
      }
      float o2w[12];
      Mat4ToObj2World(world, o2w);
      InstanceRT inst;
      inst.blas_id = blas_id;
      std::memcpy(inst.o2w, o2w, sizeof(o2w));
      instances->push_back(inst);
      // Curves under the prototype: place them per native instance via a deduped
      // curve BLAS (the prototype's own copy is collected once as base geometry).
      const int32_t curve_idx =
          ReserveCurveProto(stage, proto_path, purpose, time, curve_inst);
      if (curve_idx >= 0 && curve_inst) {
        CurveInstanceRT ci;
        ci.curve_proto_idx = uint32_t(curve_idx);
        std::memcpy(ci.o2w, o2w, sizeof(o2w));
        curve_inst->instances.push_back(ci);
      }
    }
    return;
  }

  // UsdGeomPointInstancer: expand into TLAS placements (one BLAS per prototype,
  // shared with the native-instance pool) plus instanced curves. Its children are
  // the prototypes, so do not descend (that would emit each prototype once,
  // un-instanced).
  if (prim.GetTypeName() == "PointInstancer") {
    CollectPointInstancer(stage, prim, world, purpose, time, mask, instances,
                          proto_ids, protos, curve_inst, stats);
    return;
  }

  if (prim.GetTypeName() == "Mesh" &&
      PathMatchesMask(prim.GetPath().str(), mask)) {
    MeshJobNext job;
    job.prim = prim;
    job.world = world;
    job.purpose = purpose;
    base_jobs->push_back(std::move(job));
  } else if (curve_jobs && IsCurvePrimNext(prim) &&
             PathMatchesMask(prim.GetPath().str(), mask)) {
    CurveJobNext cj;
    cj.prim = prim;
    cj.world = world;
    cj.purpose = purpose;
    curve_jobs->push_back(std::move(cj));
  }
  for (const tinyusdz::next::UsdPrim &child : prim.GetChildren()) {
    CollectSceneSplit(stage, child, world, purpose, time, mask, base_jobs,
                      instances, proto_ids, protos, curve_jobs, curve_inst,
                      stats, proto_holders);
  }
}

// Recurse a prototype subtree collecting its NESTED instance placements (nested
// PointInstancer expansions + scenegraph-instanceable instances), recorded in
// prototype-LOCAL space. Each placement references a deduped leaf-prototype BLAS
// queued into the shared proto_ids/protos pool, so deeper nesting is collected on
// later iterations of the proto loop and composed by the TLAS expansion. Mesh-only:
// nested instanced curves are routed to a throwaway collector (their geometry still
// renders once via the per-prototype curve BLAS). Mirrors the instance branches of
// CollectSceneSplit; base meshes are left to CollectProtoJobs.
void CollectProtoMeshNestingRec(
    const tinyusdz::next::Stage &stage, const tinyusdz::next::UsdPrim &prim,
    const matrix4d &parent_world, tinyusdz::Purpose inherited_purpose, double time,
    const std::vector<std::string> &mask, std::vector<InstanceRT> *nested,
    std::vector<CurveInstanceRT> *nested_curves,
    std::unordered_map<std::string, uint32_t> *proto_ids,
    std::vector<ProtoBuildReq> *protos, CurveProtoCollect *curve_inst,
    RTPreviewStats *stats,
    const std::unordered_set<std::string> *proto_holders) {
  if (!prim.IsActive()) return;  // inactive prim + its subtree are pruned
  double dmat[16];
  tinyusdz::tydra::next::ComputeLocalTransform(prim, dmat, time);
  const matrix4d local = Mat4FromArray(dmat);
  const bool reset = tinyusdz::tydra::next::HasResetXformStack(prim);
  const matrix4d world = reset ? local : (local * parent_world);

  tinyusdz::Purpose purpose = inherited_purpose;
  if (const tinyusdz::next::Value *pv = prim.GetPropertyValue("purpose")) {
    if (const std::string *t = pv->as_token()) {
      if (*t == "render") purpose = tinyusdz::Purpose::Render;
      else if (*t == "proxy") purpose = tinyusdz::Purpose::Proxy;
      else if (*t == "guide") purpose = tinyusdz::Purpose::Guide;
    }
  }

  // Nested scenegraph instance: record a placement + queue its prototype.
  const tinyusdz::next::PrimSpec *spec = prim.GetPrimSpec();
  const std::string proto_path =
      spec ? spec->meta().instance_prototype() : std::string();
  if (!proto_path.empty()) {
    if (PathMatchesMask(prim.GetPath().str(), mask)) {
      const std::string key =
          proto_path + "\x1f" + std::to_string(PurposeBit(purpose));
      auto it = proto_ids->find(key);
      uint32_t blas_id;
      if (it == proto_ids->end()) {
        blas_id = uint32_t(protos->size()) + 1;
        (*proto_ids)[key] = blas_id;
        protos->push_back({proto_path, purpose, blas_id});
      } else {
        blas_id = it->second;
      }
      InstanceRT inst;
      inst.blas_id = blas_id;
      Mat4ToObj2World(world, inst.o2w);
      nested->push_back(inst);
      // Curves under the nested native instance's prototype, placed (proto-local)
      // via a deduped curve BLAS -- flattened with the outer placements later.
      const int32_t curve_idx =
          ReserveCurveProto(stage, proto_path, purpose, time, curve_inst);
      if (curve_idx >= 0 && nested_curves) {
        CurveInstanceRT ci;
        ci.curve_proto_idx = uint32_t(curve_idx);
        std::memcpy(ci.o2w, inst.o2w, sizeof(inst.o2w));
        nested_curves->push_back(ci);
      }
    }
    return;
  }

  // Nested PointInstancer: reuse the top-level expander, directing its mesh
  // placements into `nested` and curve placements into `nested_curves` (both
  // prototype-local); mesh + curve protos dedup into the shared pools.
  if (prim.GetTypeName() == "PointInstancer") {
    CollectPointInstancer(stage, prim, world, purpose, time, mask, nested,
                          proto_ids, protos, curve_inst, stats, nested_curves);
    return;
  }

  for (const tinyusdz::next::UsdPrim &child : prim.GetChildren()) {
    if (proto_holders && proto_holders->count(child.GetPath().str())) continue;
    CollectProtoMeshNestingRec(stage, child, world, purpose, time, mask, nested,
                               nested_curves, proto_ids, protos, curve_inst, stats,
                               proto_holders);
  }
}

// Entry: root the nested-instance walk at the prototype's CHILDREN with an identity
// world (the prototype root's own transform is replaced by each outer placement,
// matching CollectProtoJobs). Appends prototype-local placements to `nested`.
void CollectProtoMeshNesting(
    const tinyusdz::next::Stage &stage, const std::string &proto_path,
    tinyusdz::Purpose purpose, double time, const std::vector<std::string> &mask,
    std::vector<InstanceRT> *nested, std::vector<CurveInstanceRT> *nested_curves,
    std::unordered_map<std::string, uint32_t> *proto_ids,
    std::vector<ProtoBuildReq> *protos, CurveProtoCollect *curve_inst,
    RTPreviewStats *stats,
    const std::unordered_set<std::string> *proto_holders) {
  tinyusdz::next::UsdPrim proto = stage.GetPrimAtPath(proto_path);
  if (!proto.IsValid()) return;
  for (const tinyusdz::next::UsdPrim &child : proto.GetChildren()) {
    if (proto_holders && proto_holders->count(child.GetPath().str())) continue;
    CollectProtoMeshNestingRec(stage, child, matrix4d::identity(), purpose, time,
                               mask, nested, nested_curves, proto_ids, protos,
                               curve_inst, stats, proto_holders);
  }
}

// Pre-pass: gather the paths of native-instance prototype holders (the targets
// of instance_prototype()). Walks the same prims CollectSceneSplit collects as
// base geometry -- it stops at instance proxies and PointInstancers, so its cost
// is one base-graph traversal (no instance multiplicity), not the expanded set.
void CollectPrototypePaths(const tinyusdz::next::UsdPrim &prim,
                           std::unordered_set<std::string> *out) {
  if (!prim.IsActive()) return;  // inactive prim + its subtree are pruned
  const tinyusdz::next::PrimSpec *spec = prim.GetPrimSpec();
  if (spec && !spec->meta().instance_prototype().empty()) {
    out->insert(spec->meta().instance_prototype());
    return;  // proxy children come from the prototype; don't descend
  }
  if (prim.GetTypeName() == "PointInstancer") return;
  for (const tinyusdz::next::UsdPrim &child : prim.GetChildren())
    CollectPrototypePaths(child, out);
}

// Collect a prototype's mesh jobs in prototype-LOCAL space (the holder prim at
// identity): traverse the holder's children with parent_world = identity, where
// GetChildren() transparently expands any nested instances inline (bounded —
// built once per unique prototype). The instance's world transform is applied
// later by the TLAS.
void CollectProtoJobs(const tinyusdz::next::Stage &stage,
                      const std::string &proto_path,
                      tinyusdz::Purpose start_purpose, double time,
                      std::vector<MeshJobNext> *jobs) {
  tinyusdz::next::UsdPrim proto = stage.GetPrimAtPath(proto_path);
  if (!proto.IsValid()) return;
  static const std::vector<std::string> kNoMask;
  // A prototype root is placed at identity (its own transform is replaced by the
  // instance transform), but if the prototype prim IS a Mesh (a PointInstancer
  // prototype can point straight at a Mesh) it must still be collected.
  if (proto.GetTypeName() == "Mesh") {
    MeshJobNext job;
    job.prim = proto;
    job.world = matrix4d::identity();
    job.purpose = start_purpose;
    jobs->push_back(std::move(job));
  }
  for (const tinyusdz::next::UsdPrim &child : proto.GetChildren()) {
    CollectRTPreviewMeshesNext(stage, child, matrix4d::identity(), start_purpose,
                               time, kNoMask, jobs);
  }
}

// Build one curve prototype into a curve BLAS (round hair) in prototype-LOCAL
// space, with one TriInfo per segment for hit resolution. The instance transform
// is applied later by the TLAS. Instanced curves are treated as round (the common
// XGen case; per-instance flat/ribbon curves are not separated). Returns false
// only on a LightRT build failure.
bool BuildCurveBlas(const tinyusdz::next::Stage &stage,
                    const std::string &proto_path, tinyusdz::Purpose purpose,
                    double time, const lrt_tri_build_options &build_opts,
                    Blas *out, Bounds *local,
                    // Sub-BLAS split: a large curve prototype is split into
                    // several disjoint sub-BLAS so their (serial) LBVH collapses
                    // run CONCURRENTLY. The first sub-BLAS fills `out`/`local`;
                    // any extras are appended here (the caller places each as a
                    // TLAS instance at the prototype's transform). Null => never
                    // split (single BLAS, byte-identical to the old path).
                    std::vector<Blas> *extra_blas = nullptr,
                    std::vector<Bounds> *extra_bounds = nullptr) {
  std::vector<CurveJobNext> curves;
  CollectProtoCurves(stage, proto_path, purpose, time, &curves);
  if (curves.empty()) return true;
  std::vector<float> pts, radii;
  std::vector<uint32_t> first, count;
  // Curve endpoints (curve_seg) are derived directly from the transformed points
  // below, so pass info == null: AppendLinearCurveStrands skips the redundant
  // 120 B/segment TriInfo intermediate (its only payload here is p0/p1 -- already
  // in pts -- and the constant kCurveColor). On a 3 M-segment prototype that
  // removes ~360 MB of build + slim work that was the dominant serial cost.
  // The FULL prototype bounds (over all points). Every sub-BLAS reports this same
  // bounds so the TLAS / autoframe is byte-identical to the unsplit path: the
  // world bounds is the transformed-AABB hull, and a union of partial AABB hulls
  // is tighter than the full AABB's hull under a rotated transform (would reframe
  // the camera). Conservative per sub-BLAS but correct (geometry is within).
  Bounds proto_bounds;
  for (const CurveJobNext &job : curves) {
    std::vector<tinyusdz::value::point3f> p = ReadCurvePointsNext(job.prim, time);
    std::vector<int32_t> c32 =
        ReadIntArrayLazy(job.prim, "curveVertexCounts", time);
    if (p.empty() || c32.empty()) continue;
    std::vector<int> c(c32.begin(), c32.end());
    std::vector<float> w = ReadFloatArrayLazy(job.prim, "widths", time);
    AppendLinearCurveStrands(p, c, w, job.world, &pts, &radii, &first, &count,
                             /*info=*/nullptr, &proto_bounds);
  }
  if (first.empty()) return true;
  const TriMat kCurveMat = ExtractTriMat([] {
    TriInfo t;
    t.base_color = kCurveColor;
    return t;
  }());

  // Partition strands into groups of ~kCurveSplitSegs segments so each group's
  // BLAS build/collapse is a separate, concurrent job. Small prototypes (the
  // common case) stay a single BLAS -> byte-identical to the unsplit path.
  size_t total_segs = 0;
  for (uint32_t c : count) total_segs += size_t(c) - 1u;
  const size_t kCurveSplitSegs = size_t(1) << 20;  // ~1M segments/sub-BLAS
  std::vector<std::array<size_t, 2>> groups;  // {s0, s1}
  if (!extra_blas || total_segs <= kCurveSplitSegs) {
    groups.push_back({0, first.size()});
  } else {
    const size_t nsub = (total_segs + kCurveSplitSegs - 1) / kCurveSplitSegs;
    const size_t per = (total_segs + nsub - 1) / nsub;
    size_t s0 = 0, acc = 0;
    for (size_t s = 0; s < first.size(); s++) {
      acc += size_t(count[s]) - 1u;
      if (acc >= per && s + 1 < first.size()) {
        groups.push_back({s0, s + 1});
        s0 = s + 1;
        acc = 0;
      }
    }
    groups.push_back({s0, first.size()});
  }
  if (extra_blas && groups.size() > 1) {
    extra_blas->resize(groups.size() - 1);
    extra_bounds->resize(groups.size() - 1);
  }

  // Phase A (serial): for each group, derive its per-segment endpoints
  // (curve_seg) straight from the shared transformed points and cut its rebased
  // strand offsets. One material (kCurveMat) covers every segment. Points are NOT
  // copied: each sub-scene reads the shared pts/radii at a base offset.
  struct SubGeom {
    std::vector<uint32_t> sf, sc;  // rebased strand offsets/counts
    uint32_t pbase, npts;
    Blas *dst;
  };
  std::vector<SubGeom> subs(groups.size());
  for (size_t g = 0; g < groups.size(); ++g) {
    const size_t s0 = groups[g][0], s1 = groups[g][1];
    Blas *dst = g == 0 ? out : &(*extra_blas)[g - 1];
    *(g == 0 ? local : &(*extra_bounds)[g - 1]) = proto_bounds;  // full bounds
    SubGeom &sg = subs[g];
    sg.dst = dst;
    sg.pbase = first[s0];
    sg.npts = first[s1 - 1] + count[s1 - 1] - sg.pbase;
    sg.sf.resize(s1 - s0);
    sg.sc.resize(s1 - s0);
    size_t nseg_sub = 0;
    for (size_t s = s0; s < s1; s++) {
      sg.sf[s - s0] = first[s] - sg.pbase;
      sg.sc[s - s0] = count[s];
      nseg_sub += size_t(count[s]) - 1u;
    }
    dst->mat_table.push_back(kCurveMat);  // index 0 for every segment
    dst->curve_seg.reserve(nseg_sub * 6);
    dst->curve_seg_mat.assign(nseg_sub, 0u);
    for (size_t s = s0; s < s1; s++) {
      const size_t pf = first[s];  // global point base (pts is concatenated)
      for (uint32_t i = 0; i + 1u < count[s]; i++) {
        const float *a = &pts[(pf + i) * 3];
        const float *b = &pts[(pf + i + 1) * 3];
        dst->curve_seg.push_back(a[0]);
        dst->curve_seg.push_back(a[1]);
        dst->curve_seg.push_back(a[2]);
        dst->curve_seg.push_back(b[0]);
        dst->curve_seg.push_back(b[1]);
        dst->curve_seg.push_back(b[2]);
      }
    }
  }

  // Phase B (parallel): build each sub-BLAS's round-hair scene (its serial LBVH
  // collapse runs concurrently with the others). Reads the shared pts/radii.
  std::atomic<bool> ok{true};
  std::atomic<size_t> gcur{0};
  auto gw = [&]() {
    for (;;) {
      const size_t g = gcur.fetch_add(1, std::memory_order_relaxed);
      if (g >= subs.size()) break;
      SubGeom &sg = subs[g];
      lrt_hair_strands hs;
      std::memset(&hs, 0, sizeof(hs));
      hs.points = pts.data() + size_t(sg.pbase) * 3;
      hs.radius = radii.data() + sg.pbase;
      hs.strand_first = sg.sf.data();
      hs.strand_count = sg.sc.data();
      hs.nstrands = sg.sf.size();
      hs.npoints = sg.npts;
      lrt_result e = LRT_RESULT_OK;
      sg.dst->scene = lrt_roundcurve_scene_build(&hs, &build_opts, &e);
      sg.dst->is_curve = true;
      if (!sg.dst->scene) ok.store(false, std::memory_order_relaxed);
    }
  };
  const unsigned gt = std::min<unsigned>(
      WorkerThreadCount(int(build_opts.num_threads)), unsigned(subs.size()));
  if (gt <= 1) {
    gw();
  } else {
    std::vector<std::thread> pool;
    pool.reserve(gt);
    for (unsigned t = 0; t < gt; ++t) pool.emplace_back(gw);
    for (std::thread &th : pool) th.join();
  }
  if (!ok.load()) {
    std::cerr << "Failed to build curve BLAS.\n";
    return false;
  }
  return true;
}

// Upper bound on the triangles a mesh job emits: the fan-triangulation count
// (sum of max(0, c-2) over faceVertexCounts). Invalid/degenerate/purpose-culled
// triangles only reduce the actual count, so this never under-reserves -- used to
// reserve the stream output up front so the chunked append never reallocates.
inline size_t EstimateTrisForJob(const tinyusdz::next::UsdPrim &prim,
                                 double time) {
  const std::vector<int32_t> counts =
      ReadIntArrayLazy(prim, "faceVertexCounts", time);
  size_t est = 0;
  for (int32_t c : counts)
    if (c >= 3) est += size_t(c - 2);
  return est;
}

// Stream a list of (material-resolved) mesh jobs into packed triangle buffers +
// a bounds, in parallel, appending in job order (deterministic). Geometry is
// emitted in each job's `world` space. `purpose_cull` drops purpose-invisible
// triangles at build time (TLAS path).
// Returns false if a memory-cap allocation failure (std::bad_alloc from
// PoolAlloc) interrupted streaming — the caller then aborts the render cleanly
// instead of letting the process get OOM-killed.
template <class FVec, class TVec>
bool StreamMeshJobs(const std::vector<MeshJobNext> &jobs, uint32_t purpose_mask,
                    double time, bool want_uvs, bool purpose_cull, int threads,
                    FVec *out_vertices, TVec *out_tris, FVec *out_tri_uvs,
                    Bounds *out_bounds, RTPreviewStats *out_stats,
                    std::vector<TriMat> *out_mat_table = nullptr,
                    bool want_colors = false, ByteVec *out_tri_colors = nullptr,
                    bool want_normals = false, FVec *out_tri_normals = nullptr,
                    // Indexed geometry (Phase 2b): when both non-null, emit unique
                    // verts + 3 indices/tri here instead of the de-indexed soup
                    // in out_vertices.
                    FVec *out_uverts = nullptr, IdxVec *out_indices = nullptr) {
  const bool indexed = (out_uverts && out_indices);
  struct R {
    FVec v;
    TVec t;
    FVec uv;
    ByteVec col;  // per-corner RGBA8 (12 bytes/tri) when want_colors
    FVec nrm;  // per-corner normals (9 floats/tri) when want_normals
    FVec uvv;  // unique verts (3 floats each) when indexed
    IdxVec idx;  // job-local vertex indices (3/tri) when indexed
    Bounds b;
    RTPreviewStats s;
    TriMat mat;  // this job's single material (slim TriStore path only)
  };
  const unsigned nthreads =
      std::min<unsigned>(WorkerThreadCount(threads),
                         jobs.empty() ? 1u : unsigned(jobs.size()));

  // Reserve the outputs from a triangle upper bound (so the appends never
  // reallocate), then stream in CHUNKS: only one chunk's per-job buffers are held
  // at a time and appended (in job order) into the reserved outputs before the
  // next chunk runs. This keeps the FULL per-job set and the concatenated copy
  // from ever coexisting -- the transient that drove the streaming-phase peak RSS
  // on big multi-mesh scenes (isCoral's base). Byte-identical to the old
  // all-jobs-then-concat: same job-order append, same content.
  size_t est_tris = 0;
  for (const MeshJobNext &job : jobs) est_tris += EstimateTrisForJob(job.prim, time);
  try {
    // Test the pointers directly (not the `indexed` bool) so the compiler's
    // -Wnonnull analysis can prove the dereferenced output is non-null -- it does
    // not propagate `indexed == (out_uverts && out_indices)` to these sites.
    if (out_uverts && out_indices)
      out_indices->reserve(out_indices->size() + est_tris * 3);
    else if (out_vertices)
      out_vertices->reserve(out_vertices->size() + est_tris * 9);
    out_tris->reserve(out_tris->size() + est_tris);
    if (want_uvs) out_tri_uvs->reserve(out_tri_uvs->size() + est_tris * 6);
    if (want_colors && out_tri_colors)
      out_tri_colors->reserve(out_tri_colors->size() + est_tris * 12);
    if (want_normals && out_tri_normals)
      out_tri_normals->reserve(out_tri_normals->size() + est_tris * 9);
  } catch (const std::bad_alloc &) {
    return false;
  }

  std::vector<R> results(jobs.size());
  std::atomic<bool> oom{false};
  const size_t njobs = jobs.size();
  const size_t chunk = std::max<size_t>(size_t(nthreads) * 2u, 1u);
  try {
    for (size_t cstart = 0;
         cstart < njobs && !oom.load(std::memory_order_relaxed);
         cstart += chunk) {
      const size_t cend = std::min(cstart + chunk, njobs);
      std::atomic<size_t> cursor{cstart};
      // A bad_alloc must be caught INSIDE each worker thread (an exception
      // escaping a std::thread calls std::terminate); it signals the cap was hit.
      auto worker = [&]() {
        try {
          for (;;) {
            const size_t i = cursor.fetch_add(1, std::memory_order_relaxed);
            if (i >= cend || oom.load(std::memory_order_relaxed)) break;
            const MeshJobNext &job = jobs[i];
            R &r = results[i];
            uint32_t jvb = 0;  // job-local vertex base (indices rebased at concat)
            AddRTPreviewMeshNext(
                job.prim, job.world, job.purpose, purpose_mask, time,
                job.base_color, job.tex_id, job.normal_tex_id, job.roughness,
                job.metallic, job.rough_tex, job.metal_tex, job.emission,
                job.emission_tex_id, job.occlusion, job.occ_tex, job.uv_xform,
                want_uvs, &r.v, &r.t, &r.uv, &r.b, &r.s, purpose_cull, &r.mat,
                job.opacity, want_colors, &r.col, want_normals, &r.nrm,
                indexed ? &r.uvv : nullptr, indexed ? &r.idx : nullptr,
                indexed ? &jvb : nullptr);
          }
        } catch (const std::bad_alloc &) {
          oom.store(true, std::memory_order_relaxed);
        }
      };
      const unsigned cn =
          std::min<unsigned>(nthreads, unsigned(cend - cstart));
      if (cn <= 1) {
        worker();
      } else {
        std::vector<std::thread> pool;
        pool.reserve(cn);
        for (unsigned t = 0; t < cn; ++t) pool.emplace_back(worker);
        for (std::thread &th : pool) th.join();
      }
      if (oom.load(std::memory_order_relaxed)) break;
      // Append this chunk in job order into the (reserved) outputs; free as we go.
      for (size_t i = cstart; i < cend; ++i) {
        R &r = results[i];
        // Direct pointer test (== indexed) so -Wnonnull can prove the appends below
        // dereference non-null outputs.
        if (out_uverts && out_indices) {
          // Rebase this job's local vertex indices by the BLAS-global vertex
          // count, then append its unique verts. Byte-identical triangle set to
          // the soup path (same vertices, same per-tri order).
          const uint32_t base = uint32_t(out_uverts->size() / 3);
          out_uverts->insert(out_uverts->end(), r.uvv.begin(), r.uvv.end());
          for (uint32_t id : r.idx) out_indices->push_back(base + id);
        } else if (out_vertices) {
          out_vertices->insert(out_vertices->end(), r.v.begin(), r.v.end());
        }
        // Slim store: assign each of this job's triangles a global material id and
        // append the job's material to the shared table (one entry per job).
        if constexpr (std::is_same<typename TVec::value_type, TriStore>::value) {
          if (out_mat_table) {
            const uint32_t mid = uint32_t(out_mat_table->size());
            out_mat_table->push_back(r.mat);
            for (auto &ts : r.t) ts.mat_id = mid;
          }
        }
        out_tris->insert(out_tris->end(), r.t.begin(), r.t.end());
        if (want_uvs)
          out_tri_uvs->insert(out_tri_uvs->end(), r.uv.begin(), r.uv.end());
        if (want_colors && out_tri_colors)
          out_tri_colors->insert(out_tri_colors->end(), r.col.begin(),
                                 r.col.end());
        if (want_normals && out_tri_normals)
          out_tri_normals->insert(out_tri_normals->end(), r.nrm.begin(),
                                  r.nrm.end());
        MergeBounds(out_bounds, r.b);
        MergeStats(out_stats, r.s);
        FVec().swap(r.v);
        TVec().swap(r.t);
        FVec().swap(r.uv);
        ByteVec().swap(r.col);
        FVec().swap(r.nrm);
        FVec().swap(r.uvv);
        IdxVec().swap(r.idx);
      }
    }
  } catch (const std::bad_alloc &) {
    return false;
  }
  if (oom.load(std::memory_order_relaxed)) return false;
  return true;
}

// Expand `g` by a local AABB transformed by a 3x4 object->world (8 corners).
void ExpandBoundsByTransformedO2W(Bounds *g, const Bounds &local,
                                  const float o2w[12]) {
  if (!local.valid) return;
  for (int c = 0; c < 8; ++c) {
    Vec3 corner{(c & 1) ? local.hi.x : local.lo.x,
                (c & 2) ? local.hi.y : local.lo.y,
                (c & 4) ? local.hi.z : local.lo.z};
    Expand(g, TransformPointO2W(o2w, corner));
  }
}

// (Re)stream triangles at `time` and (re)build the BVH. Safe to call repeatedly
// (e.g. once per animation frame): frees the previous BVH and clears the
// previous geometry first. Honors ctx.opt.mask. Geometry/transforms are
// evaluated at `time` (NaN = default value). When the composed scene has native
// instances, builds a two-level BVH (per-prototype BLAS + TLAS) so instanced
// geometry is stored once; otherwise builds a single flat scene (byte-identical
// to the historical path).
bool ExtractAndBuildBVH(RenderContext &ctx, double time) {
  const Options &opt = ctx.opt;
  ctx.frame_time = time;
  if (ctx.tlas) {
    lrt_tlas_free(ctx.tlas);
    ctx.tlas = nullptr;
  }
  if (ctx.scene) {
    lrt_tri_scene_free(ctx.scene);
    ctx.scene = nullptr;
  }
  ctx.vertices.clear();
  ctx.tris.clear();
  ctx.textures.clear();
  ctx.tri_uvs.clear();
  ctx.tri_colors.clear();
  ctx.tri_normals.clear();
  ctx.blas.clear();
  ctx.instances.clear();
  ctx.use_tlas = false;
  ctx.bounds = Bounds();
  ctx.stats = RTPreviewStats();

  const auto stream_t0 = std::chrono::steady_clock::now();
  std::vector<MeshJobNext> base_jobs;
  std::vector<InstanceRT> instances;
  std::unordered_map<std::string, uint32_t> proto_ids;
  std::vector<ProtoBuildReq> protos;
  std::vector<CurveJobNext> curve_jobs;
  CurveProtoCollect curve_inst;
  // Gather native-instance prototype holders up front so the base-geometry
  // traversal can skip them (they are rendered via their instance proxies).
  std::unordered_set<std::string> proto_holders;
  for (const tinyusdz::next::UsdPrim &root : ctx.stage.GetRootPrims()) {
    CollectPrototypePaths(root, &proto_holders);
  }
  for (const tinyusdz::next::UsdPrim &root : ctx.stage.GetRootPrims()) {
    CollectSceneSplit(ctx.stage, root, matrix4d::identity(),
                      tinyusdz::Purpose::Default, time, opt.mask, &base_jobs,
                      &instances, &proto_ids, &protos, &curve_jobs, &curve_inst,
                      &ctx.stats, &proto_holders);
  }
  // Curves (BasisCurves/NurbsCurves, plus any baked from curve-prototype
  // instancers) build into ctx.direct as LightRT hair scenes; RenderImage traces
  // them via the DirectScene path in both the flat and TLAS render modes.
  if (!BuildNextCurves(ctx, curve_jobs, time)) return false;
  ctx.stats.curve_strands = curve_jobs.size();

  lrt_tri_build_options build_opts;
  std::memset(&build_opts, 0, sizeof(build_opts));
  build_opts.quality = opt.quality;
  build_opts.layout = LRT_TRI_LAYOUT_AUTO;
  build_opts.max_leaf_size = 0;
  build_opts.num_threads = WorkerThreadCount(opt.threads);

  // Embedded textures: if the input is a .usdz package, open it so material
  // resolution can pull packed textures from the archive (else nullptr -> the
  // texture cache falls back to the filesystem).
  tinyusdz::next::USDZReader usdz_archive;
  const tinyusdz::next::USDZReader *usdz_ptr = nullptr;
  {
    const std::string &in = opt.input;
    if (in.size() >= 5 && in.compare(in.size() - 5, 5, ".usdz") == 0 &&
        usdz_archive.OpenFile(in)) {
      usdz_ptr = &usdz_archive;
    }
  }

  // -------------------------------------------------------------------------
  // Flat path: no native instances and no instanced curve prototypes. Identical
  // to the historical single-scene build (preserves byte-for-byte renders of
  // self-contained scenes).
  // -------------------------------------------------------------------------
  if (instances.empty() && curve_inst.instances.empty()) {
    ctx.stats.meshes = base_jobs.size();
    {
      TextureCache tc;
      tc.textures = &ctx.textures;
      tc.base_dir = DirName(opt.input);
      tc.usdz = usdz_ptr;
      std::unordered_map<std::string, ResolvedMat> mat_cache;
      for (MeshJobNext &job : base_jobs) {
        ResolveMeshMaterialCached(ctx.stage, job.prim, tc, mat_cache, &job);
      }
    }
    const bool want_uvs = !ctx.textures.empty();
    bool want_colors = false;
    for (const MeshJobNext &j : base_jobs)
      if (j.vertex_color) { want_colors = true; break; }
    if (!StreamMeshJobs(base_jobs, opt.purpose_mask, time, want_uvs,
                        /*purpose_cull=*/false, opt.threads, &ctx.vertices,
                        &ctx.tris, &ctx.tri_uvs, &ctx.bounds, &ctx.stats,
                        /*out_mat_table=*/nullptr, want_colors, &ctx.tri_colors,
                        opt.smooth, &ctx.tri_normals)) {
      std::cerr << "Aborting: triangle stream exceeded memory cap "
                << MemBudget::GiB(MemBudget::Get().Cap())
                << ".\n  Raise -maxMem, restrict with -mask, or lower -complexity.\n";
      return false;
    }
    const auto stream_t1 = std::chrono::steady_clock::now();
    ctx.stream_seconds =
        std::chrono::duration<double>(stream_t1 - stream_t0).count();
    ctx.stats.build_seconds = ctx.stream_seconds;
    ctx.stats.packed_triangle_bytes =
        uint64_t(ctx.vertices.size()) * sizeof(float);
    const bool have_curves = ctx.direct.round_curves || ctx.direct.flat_curves ||
                             ctx.direct.bez_curves;
    if (ctx.tris.empty()) {
      if (have_curves) return true;  // curves-only scene: traced via DirectScene
      std::cerr << "RT preview (next) found no renderable Mesh triangles.\n";
      return false;
    }
    // LightRT builds its BVH outside our allocator: it copies the triangle
    // vertices into its own layout (~36 B/tri) plus nodes (~kBvhBytesPerTri/tri).
    // Guard the process RSS against the cap before committing to the build.
    std::string why;
    if (MemBudget::Get().WouldExceed(ctx.tris.size() * kBvhBytesPerTri, &why)) {
      std::cerr << "Aborting BVH build: " << why
                << ".\n  Raise -maxMem, restrict with -mask, or lower -complexity.\n";
      return false;
    }
    const auto bvh_t0 = std::chrono::steady_clock::now();
    lrt_result lrt_err = LRT_RESULT_OK;
    ctx.scene = lrt_tri_scene_build(ctx.vertices.data(), ctx.tris.size(),
                                    &build_opts, &lrt_err);
    const auto bvh_t1 = std::chrono::steady_clock::now();
    if (!ctx.scene) {
      std::cerr << "Failed to build LightRT scene (err=" << int(lrt_err) << ").\n";
      return false;
    }
    ctx.bvh_seconds = std::chrono::duration<double>(bvh_t1 - bvh_t0).count();
    return true;
  }

  // -------------------------------------------------------------------------
  // Two-level (instanced) path: base BLAS + one BLAS per unique prototype,
  // placed by a TLAS. Geometry is stored once per prototype.
  // -------------------------------------------------------------------------
  ctx.use_tlas = true;
  // proto_jobs[i] = prototype i's base meshes; proto_nested[i] = its nested-instance
  // placements (prototype-local, referencing other prototype BLASes). `protos` GROWS
  // here as nested prototypes are queued, so iterate by index and extend the
  // parallel arrays. Copy path/purpose to locals first: collecting nesting can
  // reallocate `protos`, dangling a `protos[i]` reference mid-call.
  std::vector<std::vector<MeshJobNext>> proto_jobs;
  std::vector<std::vector<InstanceRT>> proto_nested;
  std::vector<std::vector<CurveInstanceRT>> proto_nested_curves;
  for (size_t i = 0; i < protos.size(); ++i) {
    const std::string ppath = protos[i].path;
    const tinyusdz::Purpose ppurpose = protos[i].purpose;
    std::vector<MeshJobNext> jobs;
    std::vector<InstanceRT> nested;
    std::vector<CurveInstanceRT> nested_curves;
    CollectProtoJobs(ctx.stage, ppath, ppurpose, time, &jobs);
    CollectProtoMeshNesting(ctx.stage, ppath, ppurpose, time, opt.mask, &nested,
                            &nested_curves, &proto_ids, &protos, &curve_inst,
                            &ctx.stats, &proto_holders);
    if (proto_jobs.size() < protos.size()) proto_jobs.resize(protos.size());
    if (proto_nested.size() < protos.size()) proto_nested.resize(protos.size());
    if (proto_nested_curves.size() < protos.size())
      proto_nested_curves.resize(protos.size());
    proto_jobs[i] = std::move(jobs);
    proto_nested[i] = std::move(nested);
    proto_nested_curves[i] = std::move(nested_curves);
  }
  // Material resolution over base + every prototype's meshes (shared cache).
  {
    TextureCache tc;
    tc.textures = &ctx.textures;
    tc.base_dir = DirName(opt.input);
    tc.usdz = usdz_ptr;
    std::unordered_map<std::string, ResolvedMat> mat_cache;
    for (MeshJobNext &job : base_jobs) {
      ResolveMeshMaterialCached(ctx.stage, job.prim, tc, mat_cache, &job);
    }
    for (std::vector<MeshJobNext> &pj : proto_jobs) {
      for (MeshJobNext &job : pj) {
        ResolveMeshMaterialCached(ctx.stage, job.prim, tc, mat_cache, &job);
      }
    }
  }
  const bool want_uvs = !ctx.textures.empty();
  // displayColor/Opacity is stored per-corner (48 B/tri) only for BLAS that
  // actually carry a *varying* (per-vertex/faceVarying/uniform) primvar. A BLAS
  // whose meshes are all constant-color needs no per-tri storage: the shader
  // falls back to the material base_color (which holds the constant displayColor),
  // so this is byte-identical while skipping the dominant Island footprint
  // (isCoral: ~800 MB of per-corner color across prototypes that don't vary).
  auto jobs_have_color = [](const std::vector<MeshJobNext> &jobs) {
    for (const MeshJobNext &j : jobs)
      if (j.vertex_color) return true;
    return false;
  };

  // Partition the (world-space, non-instanced) base geometry into ~2M-triangle
  // groups, each built as its OWN BLAS and placed as a TLAS instance at identity.
  // One 17M-tri base build allocates a huge BVH-scratch arena (2*ntris bnodes)
  // that dominates peak RSS; per-group builds keep that arena small (freed between
  // groups). Byte-identical: same world-space triangles, same closest hits (the
  // groups partition by mesh, so no triangle's coincidences are split).
  const size_t kBaseGroupTris = size_t(1) << 20;  // ~2M tris/group
  std::vector<std::vector<size_t>> base_group_idx;
  {
    std::vector<size_t> cur;
    size_t cur_tris = 0;
    for (size_t i = 0; i < base_jobs.size(); ++i) {
      const size_t e = EstimateTrisForJob(base_jobs[i].prim, time);
      if (!cur.empty() && cur_tris + e > kBaseGroupTris) {
        base_group_idx.push_back(std::move(cur));
        cur.clear();
        cur_tris = 0;
      }
      cur_tris += e;
      cur.push_back(i);
    }
    if (!cur.empty()) base_group_idx.push_back(std::move(cur));
    if (base_group_idx.empty()) base_group_idx.emplace_back();  // keep blas[0]
  }
  const size_t n_base_groups = base_group_idx.size();

  // blas layout: [0] base group 0, [1..P] mesh protos, [P+1..P+C] curve protos,
  // [P+C+1..] base groups 1..n-1 (appended so proto/curve ids stay stable).
  const size_t curve_base = 1 + protos.size();
  const size_t n_curve_protos = curve_inst.protos.size();
  ctx.blas.clear();
  ctx.blas.resize(curve_base + n_curve_protos + (n_base_groups - 1));
  std::vector<Bounds> local_bounds(ctx.blas.size());
  auto base_blas_id = [&](size_t g) -> uint32_t {
    return g == 0 ? 0u : uint32_t(curve_base + n_curve_protos + (g - 1));
  };

  // Stream base groups CONCURRENTLY, each into its own disjoint BLAS with internal
  // threads=1: isCoral's base is a few huge meshes, so the per-mesh threading
  // inside one StreamMeshJobs leaves most cores cold; running the groups across a
  // pool instead uses them. Byte-identical -- outputs are disjoint and per-group
  // stats are summed afterward (order-independent).
  std::vector<RTPreviewStats> gstats(n_base_groups);
  std::atomic<bool> gstream_ok{true};
  std::atomic<size_t> gcur{0};
  auto gworker = [&]() {
    for (;;) {
      const size_t g = gcur.fetch_add(1, std::memory_order_relaxed);
      if (g >= n_base_groups || !gstream_ok.load(std::memory_order_relaxed)) break;
      const uint32_t b = base_blas_id(g);
      std::vector<MeshJobNext> gjobs;
      gjobs.reserve(base_group_idx[g].size());
      for (size_t ji : base_group_idx[g]) gjobs.push_back(base_jobs[ji]);
      if (!StreamMeshJobs(
              gjobs, opt.purpose_mask, time, want_uvs, /*purpose_cull=*/true,
              /*threads=*/1, &ctx.blas[b].vertices, &ctx.blas[b].tris,
              &ctx.blas[b].tri_uvs, &local_bounds[b], &gstats[g],
              &ctx.blas[b].mat_table, jobs_have_color(gjobs),
              &ctx.blas[b].tri_colors, opt.smooth, &ctx.blas[b].tri_normals,
              /*indexed:*/ &ctx.blas[b].uverts, &ctx.blas[b].indices))
        gstream_ok.store(false, std::memory_order_relaxed);
    }
  };
  const unsigned gthreads = std::min<unsigned>(
      WorkerThreadCount(opt.threads), unsigned(n_base_groups ? n_base_groups : 1));
  if (gthreads <= 1) {
    gworker();
  } else {
    std::vector<std::thread> gpool;
    gpool.reserve(gthreads);
    for (unsigned t = 0; t < gthreads; ++t) gpool.emplace_back(gworker);
    for (std::thread &th : gpool) th.join();
  }
  for (const RTPreviewStats &gs : gstats) MergeStats(&ctx.stats, gs);
  bool stream_ok = gstream_ok.load();
  // Each mesh prototype (local space) -> blas[blas_id].
  for (size_t i = 0; stream_ok && i < protos.size(); ++i) {
    const uint32_t b = protos[i].blas_id;
    RTPreviewStats discard;
    stream_ok = StreamMeshJobs(proto_jobs[i], opt.purpose_mask, time, want_uvs,
                               /*purpose_cull=*/true, opt.threads,
                               &ctx.blas[b].vertices, &ctx.blas[b].tris,
                               &ctx.blas[b].tri_uvs, &local_bounds[b], &discard,
                               &ctx.blas[b].mat_table, jobs_have_color(proto_jobs[i]),
                               &ctx.blas[b].tri_colors, opt.smooth,
                               &ctx.blas[b].tri_normals);
  }
  if (!stream_ok) {
    std::cerr << "Aborting: triangle stream exceeded memory cap "
              << MemBudget::GiB(MemBudget::Get().Cap())
              << ".\n  Raise -maxMem, restrict with -mask, or lower -complexity.\n";
    return false;
  }
  // Each curve prototype -> blas[curve_base + i] (its first sub-BLAS); a large
  // prototype is split into extra sub-BLAS appended at the end of ctx.blas so
  // their LBVH collapses build concurrently. curve_proto_blas[i] lists every
  // sub-BLAS id for prototype i, used below to place one TLAS instance per
  // sub-BLAS at the prototype's transform.
  std::vector<std::vector<uint32_t>> curve_proto_blas(curve_inst.protos.size());
  for (size_t i = 0; i < curve_inst.protos.size(); ++i) {
    const size_t b = curve_base + i;
    std::vector<Blas> extra;
    std::vector<Bounds> extra_b;
    if (!BuildCurveBlas(ctx.stage, curve_inst.protos[i].path,
                        curve_inst.protos[i].purpose, time, build_opts,
                        &ctx.blas[b], &local_bounds[b], &extra, &extra_b)) {
      return false;
    }
    curve_proto_blas[i].push_back(uint32_t(b));
    for (size_t j = 0; j < extra.size(); ++j) {
      curve_proto_blas[i].push_back(uint32_t(ctx.blas.size()));
      ctx.blas.push_back(std::move(extra[j]));
      local_bounds.push_back(extra_b[j]);
    }
  }
  const auto stream_t1 = std::chrono::steady_clock::now();
  ctx.stream_seconds =
      std::chrono::duration<double>(stream_t1 - stream_t0).count();

  // Guard the BLAS builds (LightRT allocates outside our pool): the build adds
  // ~kBvhBytesPerTri per UNIQUE triangle (prototypes stored once).
  {
    size_t unique_tris = 0;
    for (const Blas &b : ctx.blas) unique_tris += b.tris.size();
    std::string why;
    if (MemBudget::Get().WouldExceed(unique_tris * kBvhBytesPerTri, &why)) {
      std::cerr << "Aborting BVH build: " << why
                << ".\n  Raise -maxMem, restrict with -mask, or lower -complexity.\n";
      return false;
    }
  }

  // Build the BLAS with a size-split strategy: LARGE prototypes are built one at
  // a time but each with full intra-build threading (LightRT only parallelizes a
  // single build at >=4096 tris), while the many SMALL prototypes are built in
  // one batch parallelized ACROSS scenes (each single-threaded). Using the batch
  // for everything would force big BLAS single-threaded and regress heavily
  // instanced scenes; the serial loop alone leaves the small-BLAS fleet building
  // one-at-a-time.
  // Drop a BLAS's vertex soup (9 floats/tri) as soon as its BVH is built --
  // ResolveTLASHit recovers a hit triangle's object-space vertices from the BVH
  // leaves (lrt_tri_get_verts, byte-exact). Freeing each soup at build time
  // (rather than all at the end) keeps them from accumulating, so the build-phase
  // peak holds at most one large soup + the built BVHs instead of every soup at
  // once (isCoral ~600 MB off peak). Curve BLAS / unrecoverable leaves keep theirs.
  std::atomic<uint64_t> freed_soup_bytes{0};
  auto drop_soup = [&](size_t b) {
    if (ctx.blas[b].scene && !ctx.blas[b].is_curve &&
        lrt_tri_scene_has_verts(ctx.blas[b].scene)) {
      if (!ctx.blas[b].vertices.empty()) {
        freed_soup_bytes.fetch_add(
            uint64_t(ctx.blas[b].vertices.size()) * sizeof(float),
            std::memory_order_relaxed);
        FloatVec().swap(ctx.blas[b].vertices);
      }
      // Indexed BLAS: the leaf holds the de-indexed verts, so the unique-vertex
      // array + indices are no longer needed (lrt_tri_get_verts recovers hits).
      if (!ctx.blas[b].indices.empty()) {
        freed_soup_bytes.fetch_add(
            uint64_t(ctx.blas[b].uverts.size()) * sizeof(float) +
                uint64_t(ctx.blas[b].indices.size()) * sizeof(uint32_t),
            std::memory_order_relaxed);
        FloatVec().swap(ctx.blas[b].uverts);
        IdxVec().swap(ctx.blas[b].indices);
      }
    }
  };
  // Build a BLAS from whichever geometry form it streamed: indexed (uverts +
  // indices, the base groups) or de-indexed soup (everything else). The leaf is
  // identical either way (lrt_tri_scene_build_indexed gathers through indices).
  auto build_blas = [](Blas &bl, const lrt_tri_build_options *o,
                       lrt_result *e) -> lrt_tri_scene * {
    if (!bl.indices.empty())
      return lrt_tri_scene_build_indexed(bl.uverts.data(), bl.uverts.size() / 3,
                                         bl.indices.data(), bl.tris.size(), o, e);
    return lrt_tri_scene_build(bl.vertices.data(), bl.tris.size(), o, e);
  };
  const auto bvh_t0 = std::chrono::steady_clock::now();
  {
    const size_t nb = ctx.blas.size();
    const size_t kLargeTris = 32768;  // above this, intra-build threading wins
    // Pass 1: large BLAS. Build with BOUNDED concurrency -- kBuildPar builds run
    // at once, each with a few INTRA-build threads (kBuildPar * intra_t cores), so
    // the morton/radix/tree steps (tri_parallel_for) use the cores that a
    // single-threaded build leaves idle (the bvh phase otherwise ran ~3/32 cores).
    // kBuildPar bounds the coexisting build scratch to hold peak under Embree.
    // Byte-identical: each BLAS builds independently from its own geometry.
    std::vector<size_t> large;
    for (size_t b = 0; b < nb; ++b)
      if (ctx.blas[b].tris.size() >= kLargeTris) large.push_back(b);
    lrt_tri_build_options sbuild = build_opts;
    const unsigned kBuildPar = std::min<unsigned>(
        WorkerThreadCount(opt.threads), large.empty() ? 1u : 3u);  // cap for peak
    // Give each concurrent build a few intra-build threads (verified sweet spot
    // ~4: k=3 x 4 = 12 cores, bvh 1.7->1.5s, peak still ~180MB under Embree;
    // higher T tightens peak for little gain). Scales down on smaller machines.
    sbuild.num_threads = std::max(
        1u, std::min(4u, WorkerThreadCount(opt.threads) / kBuildPar));
    std::atomic<size_t> lcur{0};
    std::atomic<bool> lfail{false};
    auto lworker = [&]() {
      for (;;) {
        const size_t i = lcur.fetch_add(1, std::memory_order_relaxed);
        if (i >= large.size() || lfail.load(std::memory_order_relaxed)) break;
        const size_t b = large[i];
        lrt_result e = LRT_RESULT_OK;
        ctx.blas[b].scene = build_blas(ctx.blas[b], &sbuild, &e);
        if (!ctx.blas[b].scene) {
          lfail.store(true, std::memory_order_relaxed);
          break;
        }
        drop_soup(b);
      }
    };
    if (kBuildPar <= 1) {
      lworker();
    } else {
      std::vector<std::thread> lpool;
      lpool.reserve(kBuildPar);
      for (unsigned t = 0; t < kBuildPar; ++t) lpool.emplace_back(lworker);
      for (std::thread &th : lpool) th.join();
    }
    if (lfail.load()) {
      std::cerr << "Failed to build BLAS.\n";
      return false;
    }
    // Pass 2: small BLAS, batched across workers (bntris==0 skips large/empty).
    std::vector<const float *> bverts(nb, nullptr);
    std::vector<size_t> bntris(nb, 0);
    std::vector<lrt_tri_scene *> bscenes(nb, nullptr);
    std::vector<lrt_result> berrs(nb, LRT_RESULT_OK);
    for (size_t b = 0; b < nb; ++b) {
      const size_t nt = ctx.blas[b].tris.size();
      if (nt > 0 && nt < kLargeTris) {
        if (!ctx.blas[b].indices.empty()) {
          // Small indexed BLAS (not expected -- base groups are large -- but kept
          // correct): the soup batch can't consume indexed input, so build it now.
          lrt_result e = LRT_RESULT_OK;
          ctx.blas[b].scene = build_blas(ctx.blas[b], &build_opts, &e);
          if (!ctx.blas[b].scene) {
            std::cerr << "Failed to build BLAS (err=" << int(e) << ").\n";
            return false;
          }
          drop_soup(b);
        } else {
          bverts[b] = ctx.blas[b].vertices.data();
          bntris[b] = nt;
        }
      }
    }
    lrt_tri_scene_build_batch(bverts.data(), bntris.data(), nb, &build_opts,
                              bscenes.data(), berrs.data());
    for (size_t b = 0; b < nb; ++b) {
      if (bntris[b] > 0) {
        ctx.blas[b].scene = bscenes[b];
        if (!bscenes[b]) {
          std::cerr << "Failed to build BLAS (err=" << int(berrs[b]) << ").\n";
          return false;
        }
        drop_soup(b);
      }
    }
  }

  // Phase 5 (opt-in via TUSD_COHCOLOR): reorder each BLAS's per-corner colors
  // from prim_id order into BVH leaf-slot order, so hits within a leaf read
  // adjacent color records instead of scattered ones (the prim_id->slot map is
  // already touched by lrt_tri_get_verts at every hit). Per-BLAS scatter +
  // immediate free of the old array keeps the transient to one BLAS's colors.
  // Off by default: it adds a build-time scatter pass that isn't worth it for the
  // tiny primary-only preview render, but helps render-heavy (hi-res) use.
  // Byte-identical: same 12 bytes, relocated and read back through the same slot.
  if (std::getenv("TUSD_COHCOLOR")) {
    const size_t nb = ctx.blas.size();
    std::atomic<size_t> ccur{0};
    auto cworker = [&]() {
      for (;;) {
        const size_t b = ccur.fetch_add(1, std::memory_order_relaxed);
        if (b >= nb) break;
        Blas &bl = ctx.blas[b];
        if (bl.tri_colors.empty() || !bl.scene) continue;
        const uint32_t ns = lrt_tri_slot_count(bl.scene);
        if (!ns) continue;
        ByteVec cs(size_t(ns) * 12, 0);
        const size_t nt = bl.tris.size();
        for (size_t p = 0; p < nt; p++) {
          const uint32_t slot = lrt_tri_get_slot(bl.scene, uint32_t(p));
          if (slot != LRT_TRI_NO_HIT && size_t(slot) * 12 + 11 < cs.size())
            std::memcpy(&cs[size_t(slot) * 12], &bl.tri_colors[p * 12], 12);
        }
        bl.tri_colors_slot = std::move(cs);
        ByteVec().swap(bl.tri_colors);
      }
    };
    const unsigned cn = std::min<unsigned>(WorkerThreadCount(opt.threads),
                                           nb ? unsigned(nb) : 1u);
    if (cn <= 1) {
      cworker();
    } else {
      std::vector<std::thread> cp;
      cp.reserve(cn);
      for (unsigned t = 0; t < cn; ++t) cp.emplace_back(cworker);
      for (std::thread &th : cp) th.join();
    }
  }

  // Flatten nested instancing into the single level a TLAS expresses. A prototype
  // whose subtree contains instancers contributed nested placements (in that
  // prototype's local space, `proto_nested[blas-1]`). Each top-level placement of
  // such a prototype must ALSO place that prototype's nested geometry, composed with
  // the outer transform -- recursively, to any depth. Geometry stays deduped (each
  // leaf BLAS is stored once); only the 48 B/placement instance list grows. No-op
  // (byte-identical) when nothing nests.
  {
    const size_t nb = ctx.blas.size();
    bool any_nested = false;
    for (const std::vector<InstanceRT> &v : proto_nested)
      if (!v.empty()) { any_nested = true; break; }
    for (const std::vector<CurveInstanceRT> &v : proto_nested_curves)
      if (!v.empty()) { any_nested = true; break; }
    if (any_nested) {
      // Per-blas flattened nested placements (mesh + curve), in blas-local space.
      // flat[b]/flatC[b] = ALL geometry reachable through nested instancing under b.
      std::vector<std::vector<InstanceRT>> flat(nb);
      std::vector<std::vector<CurveInstanceRT>> flatC(nb);
      std::vector<uint8_t> visit(nb, 0);  // 0=unvisited 1=in-progress 2=done
      std::function<void(uint32_t)> build = [&](uint32_t b) {
        if (b >= nb || visit[b]) return;  // done -> cached; in-progress -> cycle
        visit[b] = 1;
        std::vector<InstanceRT> outM;
        std::vector<CurveInstanceRT> outC;
        const size_t pi = size_t(b) - 1;  // proto index for blas b (base=0: none)
        if (b >= 1) {
          if (pi < proto_nested_curves.size())  // curves directly nested in b
            for (const CurveInstanceRT &c : proto_nested_curves[pi]) outC.push_back(c);
          if (pi < proto_nested.size()) {
            for (const InstanceRT &d : proto_nested[pi]) {  // child placed at d.o2w
              const uint32_t cb = d.blas_id;
              if (cb < nb && ctx.blas[cb].scene) outM.push_back(d);  // child's base
              build(cb);
              if (cb < nb) {
                for (const InstanceRT &g : flat[cb]) {  // child's nested meshes
                  InstanceRT e;
                  e.blas_id = g.blas_id;
                  Compose3x4(d.o2w, g.o2w, e.o2w);  // apply g (cb-local) then d
                  outM.push_back(e);
                }
                for (const CurveInstanceRT &g : flatC[cb]) {  // child's nested curves
                  CurveInstanceRT e;
                  e.curve_proto_idx = g.curve_proto_idx;
                  Compose3x4(d.o2w, g.o2w, e.o2w);
                  outC.push_back(e);
                }
              }
            }
          }
        }
        flat[b] = std::move(outM);
        flatC[b] = std::move(outC);
        visit[b] = 2;
      };

      std::vector<InstanceRT> expanded;
      expanded.reserve(instances.size());
      size_t addedM = 0, addedC = 0;
      // Guard the expanded instance arrays against the process memory cap before
      // materializing them (build() is memoized, so this pre-pass is cheap and the
      // expansion loop below reuses the cached flat[]/flatC[]). Nested instancing can
      // multiply the placement count (outer x nested), so a pathological scene could
      // blow the budget on 48 B/placement alone.
      {
        size_t projM = instances.size(), projC = curve_inst.instances.size();
        for (const InstanceRT &it : instances) {
          build(it.blas_id);
          if (it.blas_id < nb) {
            projM += flat[it.blas_id].size();
            projC += flatC[it.blas_id].size();
          }
        }
        std::string why;
        if (MemBudget::Get().WouldExceed(
                projM * sizeof(InstanceRT) + projC * sizeof(CurveInstanceRT),
                &why)) {
          std::cerr << "Aborting nested-instance expansion: " << why
                    << ".\n  Raise -maxMem or restrict with -mask.\n";
          return false;
        }
      }
      for (const InstanceRT &it : instances) {
        expanded.push_back(it);  // the prototype's own base at its outer transform
        build(it.blas_id);
        if (it.blas_id < nb) {
          for (const InstanceRT &g : flat[it.blas_id]) {
            InstanceRT e;
            e.blas_id = g.blas_id;
            Compose3x4(it.o2w, g.o2w, e.o2w);  // leaf-local -> world: apply g then it
            expanded.push_back(e);
            ++addedM;
          }
          for (const CurveInstanceRT &g : flatC[it.blas_id]) {
            CurveInstanceRT e;
            e.curve_proto_idx = g.curve_proto_idx;
            Compose3x4(it.o2w, g.o2w, e.o2w);
            curve_inst.instances.push_back(e);  // nested curve placed in world space
            ++addedC;
          }
        }
      }
      if (addedM) instances = std::move(expanded);
      ctx.stats.nested_instances = addedM;
      ctx.stats.curve_instances = curve_inst.instances.size();
    }
  }

  // LightRT tolerates NULL entries in the BLAS array (empty prototypes — e.g.
  // fully purpose-culled) as long as no instance references them, so the BLAS
  // index used by both the TLAS and shade-time ResolveTLASHit is just the
  // ctx.blas index — no compaction/remap needed.
  std::vector<lrt_tri_scene *> blas_ptrs(ctx.blas.size(), nullptr);
  for (size_t b = 0; b < ctx.blas.size(); ++b) blas_ptrs[b] = ctx.blas[b].scene;

  // Placements: instance 0 is the base scene at identity, then each native
  // instance whose prototype BLAS is non-empty, then each instanced curve
  // prototype placement. instance_id indexes ctx.instances (resolved back to
  // blas_id + transform at shade time).
  //
  // For Island-scale scenes this is tens of millions of instances. The per-
  // instance fill (two 3x4 copies + an 8-corner bounds expand) is independent,
  // so it runs in parallel: a validity mask is filled in parallel, an exclusive
  // scan assigns each kept instance its output slot (preserving the serial
  // base->instances->curves order, hence identical instance_id), then the
  // InstanceRT/lrt_instance arrays are scattered in parallel with per-thread
  // bounds and triangle-count reductions. Output is byte-identical to the serial
  // fill (indexed writes + min/max + integer sums are all order-invariant).
  static const float kIdentO2W[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
  std::vector<lrt_instance> lrt_insts;
  const size_t n_inst_src = instances.size();
  // Expand curve placements: one TLAS instance per sub-BLAS of each curve
  // instance's prototype (split prototypes have several), all at the instance's
  // transform. (curve_inst.instances persists, so o2w pointers stay valid.)
  std::vector<std::pair<uint32_t, const float *>> curve_placements;
  for (const CurveInstanceRT &ci : curve_inst.instances)
    for (uint32_t bid : curve_proto_blas[ci.curve_proto_idx])
      curve_placements.push_back({bid, ci.o2w});
  const size_t n_curve_src = curve_placements.size();
  const size_t n_src = n_base_groups + n_inst_src + n_curve_src;
  // Resolve a source index [0, n_src) to its (blas_id, o2w) without materializing
  // a unified array: [0, G) = base groups (at identity), [G, G+nI) = native
  // instances, the remainder = instanced curve sub-BLAS placements.
  auto src_at = [&](size_t i, uint32_t *blas_id, const float **o2w) {
    if (i < n_base_groups) {
      *blas_id = base_blas_id(i);
      *o2w = kIdentO2W;
      return;
    }
    i -= n_base_groups;
    if (i < n_inst_src) {
      *blas_id = instances[i].blas_id;
      *o2w = instances[i].o2w;
      return;
    }
    i -= n_inst_src;
    *blas_id = curve_placements[i].first;
    *o2w = curve_placements[i].second;
  };
  auto blas_ok = [&](uint32_t blas_id) {
    return blas_id < ctx.blas.size() && ctx.blas[blas_id].scene;
  };
  const unsigned ai_threads = std::min<unsigned>(
      WorkerThreadCount(opt.threads), n_src ? unsigned(n_src) : 1u);
  auto run_range = [&](const std::function<void(unsigned, size_t, size_t)> &fn) {
    if (ai_threads <= 1) {
      fn(0u, 0, n_src);
      return;
    }
    std::vector<std::thread> pool;
    pool.reserve(ai_threads);
    for (unsigned t = 0; t < ai_threads; ++t) {
      const size_t b = (n_src * t) / ai_threads;
      const size_t e = (n_src * (t + 1)) / ai_threads;
      pool.emplace_back([&, t, b, e]() { fn(t, b, e); });
    }
    for (std::thread &th : pool) th.join();
  };

  std::vector<uint8_t> valid(n_src);
  run_range([&](unsigned, size_t b, size_t e) {
    for (size_t i = b; i < e; ++i) {
      uint32_t bid;
      const float *o;
      src_at(i, &bid, &o);
      valid[i] = blas_ok(bid) ? 1u : 0u;
    }
  });
  // Exclusive scan -> output slot per kept instance (cheap, ~tens of ms at 22M).
  std::vector<uint32_t> slot(n_src);
  uint32_t kept = 0;
  for (size_t i = 0; i < n_src; ++i) {
    slot[i] = kept;
    kept += valid[i];
  }
  ctx.instances.resize(kept);
  lrt_insts.resize(kept);

  std::vector<Bounds> tls_bounds(ai_threads ? ai_threads : 1);
  std::vector<uint64_t> tls_tris(ai_threads ? ai_threads : 1, 0);
  run_range([&](unsigned tid, size_t b, size_t e) {
    Bounds lb;
    uint64_t lt = 0;
    for (size_t i = b; i < e; ++i) {
      if (!valid[i]) continue;
      uint32_t bid;
      const float *o;
      src_at(i, &bid, &o);
      const uint32_t id = slot[i];
      InstanceRT &inst = ctx.instances[id];
      inst.blas_id = bid;
      std::memcpy(inst.o2w, o, sizeof(inst.o2w));
      lrt_instance &li = lrt_insts[id];
      std::memset(&li, 0, sizeof(li));
      li.blas_id = bid;
      std::memcpy(li.obj2world, o, sizeof(li.obj2world));
      li.instance_id = id;
      li.mask = 0xffffffffu;
      ExpandBoundsByTransformedO2W(&lb, local_bounds[bid], o);
      lt += uint64_t(ctx.blas[bid].tris.size());
    }
    tls_bounds[tid] = lb;
    tls_tris[tid] = lt;
  });
  for (const Bounds &lb : tls_bounds) {
    if (lb.valid) {
      Expand(&ctx.bounds, lb.lo);
      Expand(&ctx.bounds, lb.hi);
    }
  }
  ctx.stats.triangles = 0;
  for (uint64_t v : tls_tris) ctx.stats.triangles += v;
  std::vector<uint8_t>().swap(valid);
  std::vector<uint32_t>().swap(slot);
  ctx.stats.curve_instances = curve_inst.instances.size();
  // The collection-side instance lists are now fully copied into ctx.instances +
  // lrt_insts; free them before lrt_tlas_build allocates the (peak) TLAS nodes.
  std::vector<InstanceRT>().swap(instances);
  std::vector<CurveInstanceRT>().swap(curve_inst.instances);

  if (lrt_insts.empty()) {
    std::cerr << "RT preview (next) found no renderable Mesh triangles.\n";
    return false;
  }
  ctx.stats.meshes = base_jobs.size() + instances.size();

  lrt_result terr = LRT_RESULT_OK;
  ctx.tlas = lrt_tlas_build(blas_ptrs.data(), blas_ptrs.size(),
                            lrt_insts.data(), lrt_insts.size(), &build_opts,
                            &terr);
  const auto bvh_t1 = std::chrono::steady_clock::now();
  if (!ctx.tlas) {
    std::cerr << "Failed to build LightRT TLAS (err=" << int(terr) << ").\n";
    return false;
  }
  ctx.bvh_seconds = std::chrono::duration<double>(bvh_t1 - bvh_t0).count();
  ctx.stats.build_seconds = ctx.stream_seconds;
  uint64_t blas_bytes = freed_soup_bytes.load();  // soup dropped post-build (above)
  for (const Blas &b : ctx.blas) blas_bytes += uint64_t(b.vertices.size()) * sizeof(float);
  ctx.stats.packed_triangle_bytes = blas_bytes;
  return true;
}

// Collect finite UsdLux lights (Rect/Sphere/Disk/Cylinder/Distant) from the next
// stage into the LightCache, mirroring the legacy CollectLights/AddFiniteLight.
// DomeLights are handled separately as IBL (BuildNextIbl). Radiance is
// color * intensity * 2^exposure; position/direction come from the world xform
// (UsdLux lights emit along local -Z).
void CollectLightsNext(const tinyusdz::next::Stage &stage,
                       const tinyusdz::next::UsdPrim &prim,
                       const matrix4d &parent_world, double time,
                       LightCache *cache) {
  double dmat[16];
  tinyusdz::tydra::next::ComputeLocalTransform(prim, dmat, time);
  const matrix4d local = Mat4FromArray(dmat);
  const bool reset = tinyusdz::tydra::next::HasResetXformStack(prim);
  const matrix4d world = reset ? local : (local * parent_world);

  const std::string &t = prim.GetTypeName();
  PreviewLight::Kind kind;
  bool is_light = true;
  if (t == "RectLight") kind = PreviewLight::Kind::Rect;
  else if (t == "SphereLight") kind = PreviewLight::Kind::Sphere;
  else if (t == "DiskLight") kind = PreviewLight::Kind::Disk;
  else if (t == "CylinderLight") kind = PreviewLight::Kind::Cylinder;
  else if (t == "DistantLight") kind = PreviewLight::Kind::Distant;
  else is_light = false;

  if (is_light) {
    const float intensity = ReadCamFloatNext(prim, "inputs:intensity", 1.0f);
    const float exposure = ReadCamFloatNext(prim, "inputs:exposure", 0.0f);
    Vec3 color{1.0f, 1.0f, 1.0f};
    if (const tinyusdz::next::Value *v = prim.GetPropertyValue("inputs:color"))
      if (const float *f = v->as_float3()) color = Vec3{f[0], f[1], f[2]};
    const float scale = intensity * std::pow(2.0f, exposure);
    PreviewLight dst;
    dst.kind = kind;
    dst.position = Vec3{float(world.m[3][0]), float(world.m[3][1]),
                        float(world.m[3][2])};
    Vec3 dir = TransformVector(world, Vec3{0.0f, 0.0f, -1.0f});  // -Z forward
    dst.direction = Length(dir) > 1.0e-6f ? Normalize(dir) : Vec3{0, -1, 0};
    dst.normal = Mul(dst.direction, -1.0f);
    dst.radiance = Mul(color, scale);
    const float radius = ReadCamFloatNext(prim, "inputs:radius", 0.5f);
    const float width = ReadCamFloatNext(prim, "inputs:width", 1.0f);
    const float height = ReadCamFloatNext(prim, "inputs:height", 1.0f);
    const float length = ReadCamFloatNext(prim, "inputs:length", 1.0f);
    constexpr float kPi = 3.14159265358979323846f;
    dst.radius = radius;
    dst.width = width;
    dst.height = height;
    if (kind == PreviewLight::Kind::Rect) dst.area = width * height;
    else if (kind == PreviewLight::Kind::Sphere) dst.area = 4 * kPi * radius * radius;
    else if (kind == PreviewLight::Kind::Disk) dst.area = kPi * radius * radius;
    else if (kind == PreviewLight::Kind::Cylinder) dst.area = 2 * kPi * radius * length;
    dst.power = std::max(0.0f, Luminance(dst.radiance) * std::max(1.0f, dst.area));
    cache->finite.push_back(std::move(dst));
  }
  for (const tinyusdz::next::UsdPrim &c : prim.GetChildren())
    CollectLightsNext(stage, c, world, time, cache);
}

// Load the scene via next, then stream + build the BVH at the initial time.
// First UsdLuxDomeLight in the composed stage (depth-first), or an invalid prim.
tinyusdz::next::UsdPrim FindDomeLightRec(const tinyusdz::next::UsdPrim &prim) {
  if (prim.GetTypeName() == "DomeLight") return prim;
  for (const tinyusdz::next::UsdPrim &c : prim.GetChildren()) {
    tinyusdz::next::UsdPrim r = FindDomeLightRec(c);
    if (r.IsValid()) return r;
  }
  return tinyusdz::next::UsdPrim();
}

// Build the IBL cache from the --env override or a DomeLight; returns false (and
// leaves ibl invalid) if there is no env, so the renderer falls back to the
// headlight.
bool BuildNextIbl(const tinyusdz::next::Stage &stage, const Options &opt,
                  const std::string &base_dir, IblCache *ibl) {
  std::string env_path = opt.env_file;
  Vec3 scale{1.0f, 1.0f, 1.0f};
  if (env_path.empty()) {
    for (const tinyusdz::next::UsdPrim &root : stage.GetRootPrims()) {
      tinyusdz::next::UsdPrim dome = FindDomeLightRec(root);
      if (!dome.IsValid()) continue;
      if (const tinyusdz::next::Value *v =
              dome.GetPropertyValue("inputs:texture:file")) {
        const std::string *ap = v->as_asset_path();
        if (!ap) ap = v->as_string();
        if (ap) env_path = *ap;
      }
      float intensity = 1.0f;
      if (const tinyusdz::next::Value *v = dome.GetPropertyValue("inputs:intensity"))
        if (const float *f = v->as_float()) intensity = *f;
      Vec3 color{1.0f, 1.0f, 1.0f};
      if (const tinyusdz::next::Value *v = dome.GetPropertyValue("inputs:color"))
        if (const float *f = v->as_float3()) color = Vec3{f[0], f[1], f[2]};
      scale = Vec3{color.x * intensity, color.y * intensity, color.z * intensity};
      break;
    }
  }
  if (env_path.empty()) return false;
  std::string path = env_path;
  if (path[0] != '/' && !base_dir.empty()) path = base_dir + "/" + path;
  EnvImage env;
  if (!LoadEnvImageFromFile(path, scale, &env)) return false;
  return BuildIblFromEnv(std::move(env), ibl);
}

bool BuildRenderContext(const Options &opt, RenderContext &ctx) {
  ctx.opt = opt;
  ctx.width = opt.width > 0 ? opt.width : 960;

  const auto load_t0 = std::chrono::steady_clock::now();
  std::string warn, err;
  // LoadUSDComposed resolves references/payloads/sublayers in place (anchored to
  // the input dir), so tusdrender consumes raw reference-composed scenes (e.g.
  // Caldera prefab stubs) directly — no external usdcat --flatten step. Self-
  // contained / pre-flattened inputs skip composition (identical to LoadUSD).
  tinyusdz::next::pcp::CompositionOptions comp_opts;
  if (!opt.variant_overrides.empty())
    comp_opts.variant_overrides = opt.variant_overrides;
  if (!tinyusdz::next::LoadUSDComposed(opt.input, &ctx.stage, &warn, &err,
                                       &comp_opts)) {
    if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
    std::cerr << "Failed to load USD (next): " << err << "\n";
    return false;
  }
  if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
  const auto load_t1 = std::chrono::steady_clock::now();
  ctx.load_seconds = std::chrono::duration<double>(load_t1 - load_t0).count();

  // The composed stage is the memory baseline; everything our pool allocator
  // tracks (triangle buffers) must fit in cap - base. Abort now if compose alone
  // already blew the cap.
  std::string why;
  if (MemBudget::Get().WouldExceed(0, &why)) {
    std::cerr << "Aborting after load: " << why
              << ".\n  Raise -maxMem, restrict with -mask, or pre-flatten.\n";
    return false;
  }
  MemBudget::Get().SnapshotBase();

  ctx.up_axis = GetUpAxis(ctx.stage.GetUpAxis());

  // Initial time: default value unless -timecode was given. -defaultTime forces
  // the default (NaN) explicitly.
  const double init_time = opt.default_time
                               ? std::numeric_limits<double>::quiet_NaN()
                               : opt.timecode;
  if (!ExtractAndBuildBVH(ctx, init_time)) return false;

  // UsdVol volumes (OpenVDB) -> dense grids for raymarching. Extend bounds with
  // each volume's world AABB BEFORE resolving the camera, so a volume-only scene
  // (or one whose volume sits away from the origin, e.g. an explosion sim) is
  // framed and rendered instead of leaving the auto-camera looking at an empty
  // origin.
  ctx.volumes.clear();
  for (const tinyusdz::next::UsdPrim &root : ctx.stage.GetRootPrims())
    CollectVolumesNext(ctx.stage, root, matrix4d::identity(), init_time,
                       DirName(opt.input), &ctx.volumes);
  ExpandBoundsByVolume(ctx.volumes, &ctx.bounds);
  if (opt.stats && !ctx.volumes.empty())
    std::cerr << "rt volumes: " << ctx.volumes.size() << "\n";

  ResolveCameraNext(ctx);

  // Image-based lighting: an explicit --env override wins, else the first
  // UsdLuxDomeLight's texture (scaled by intensity*color). Enables the glossy
  // BRDF (roughness/metallic) + env background; absent -> camera headlight.
  BuildNextIbl(ctx.stage, opt, DirName(opt.input), &ctx.ibl);
  if (opt.stats && ctx.ibl.valid) {
    std::cerr << "ibl: " << ctx.ibl.env.width << "x" << ctx.ibl.env.height
              << " (" << (opt.env_file.empty() ? "DomeLight" : "--env") << ")\n";
  }
  // Finite UsdLux lights (Rect/Sphere/Disk/Cylinder/Distant) -> ctx.lights, so the
  // shading path lights interiors that the dome can't reach (e.g. ALab's shot rig).
  ctx.lights.finite.clear();
  for (const tinyusdz::next::UsdPrim &root : ctx.stage.GetRootPrims())
    CollectLightsNext(ctx.stage, root, matrix4d::identity(), init_time,
                      &ctx.lights);
  AppendPowerCdf(&ctx.lights.finite, &ctx.lights.finite_cdf);
  if (opt.stats && !ctx.lights.finite.empty())
    std::cerr << "rt finite lights: " << ctx.lights.finite.size() << "\n";

  return true;
}

// Render the current camera/parameters of `ctx` and write to `path`.
// Reuses the persistent BVH (no rebuild). Returns the trace time in seconds
// (or a negative value on write failure).
double RenderFrameTo(RenderContext &ctx, const std::string &path) {
  ctx.opt.width = ctx.width;
  const auto t0 = std::chrono::steady_clock::now();
  tinyusdz::Image img = RenderImage(
      ctx.scene, &ctx.direct, ctx.tris, ctx.lights,
      ctx.ibl.valid ? &ctx.ibl : nullptr, ctx.camera, ctx.opt, ctx.height,
      ctx.textures.empty() ? nullptr : &ctx.textures,
      ctx.tri_uvs.empty() ? nullptr : &ctx.tri_uvs,
      ctx.use_tlas ? ctx.tlas : nullptr,
      ctx.use_tlas ? &ctx.blas : nullptr,
      ctx.use_tlas ? &ctx.instances : nullptr,
      ctx.tri_colors.empty() ? nullptr : &ctx.tri_colors,
      ctx.tri_normals.empty() ? nullptr : &ctx.tri_normals,
      ctx.volumes.empty() ? nullptr : &ctx.volumes);
  const auto t1 = std::chrono::steady_clock::now();
  tinyusdz::image::WriteOption wopt;
  wopt.format = tinyusdz::image::WriteImageFormat::Autodetect;
  auto ret = tinyusdz::image::WriteImageToFile(path, img, wopt);
  if (!ret) {
    std::cerr << "Failed to write image: " << ret.error() << "\n";
    return -1.0;
  }
  return std::chrono::duration<double>(t1 - t0).count();
}

void PrintRTStats(const RenderContext &ctx) {
  std::cerr << "rt preview: 1\n";
  std::cerr << "rt loader: next\n";
  std::cerr << "rt meshes: " << ctx.stats.meshes << "\n";
  std::cerr << "rt skipped meshes: " << ctx.stats.skipped_meshes << "\n";
  std::cerr << "rt purpose default triangles: "
            << ctx.stats.purpose_default_triangles << "\n";
  std::cerr << "rt purpose guide triangles: "
            << ctx.stats.purpose_guide_triangles << "\n";
  if (ctx.stats.curve_strands > 0)
    std::cerr << "rt curve strands: " << ctx.stats.curve_strands << "\n";
  if (ctx.stats.curve_instances > 0)
    std::cerr << "rt curve instances: " << ctx.stats.curve_instances << "\n";
  std::cerr << "load seconds: " << ctx.load_seconds << "\n";
  std::cerr << "rt triangle stream seconds: " << ctx.stream_seconds << "\n";
  std::cerr << "rt bvh build seconds: " << ctx.bvh_seconds << "\n";
  std::cerr << "memory cap: " << MemBudget::GiB(MemBudget::Get().Cap()) << "\n";
  std::cerr << "tracked buffer peak: "
            << MemBudget::GiB(MemBudget::Get().PeakTracked()) << "\n";
  std::cerr << "process RSS: " << MemBudget::GiB(MemBudget::ProcessRSS()) << "\n";
  // ctx.stats.triangles is the (instance-expanded) renderable triangle count in
  // both paths; ctx.tris is empty in the two-level (TLAS) path.
  std::cerr << "triangles: " << ctx.stats.triangles << "\n";
  if (ctx.use_tlas) {
    size_t unique_tris = 0;
    for (const Blas &b : ctx.blas) unique_tris += b.tris.size();
    std::cerr << "rt instancing: tlas\n";
    std::cerr << "rt blas count: " << ctx.blas.size() << "\n";
    std::cerr << "rt instances: " << ctx.instances.size() << "\n";
    std::cerr << "rt point instancers: " << ctx.stats.point_instancers << "\n";
    std::cerr << "rt point instances: " << ctx.stats.point_instances << "\n";
    std::cerr << "rt nested instances: " << ctx.stats.nested_instances << "\n";
    std::cerr << "rt unique triangles: " << unique_tris << "\n";
  } else {
    lrt_tri_stats st;
    std::memset(&st, 0, sizeof(st));
    lrt_tri_scene_stats(ctx.scene, &st);
    std::cerr << "lightrt: " << lrt_tri_kernel_name(ctx.scene) << "\n";
    std::cerr << "bvh nodes: " << st.node_count << ", leaves: " << st.leaf_count
              << ", memory: " << st.memory_bytes << " bytes\n";
  }
}

// Parse an OpenUSD usdrecord FRAMESPEC list into time codes. Each comma-
// separated spec is "t", "start:end", or "start:end x stride" (stride defaults
// to 1, sign inferred from start/end). Examples: "1", "1:10", "1:10x2",
// "10:1", "1:5,8,12:20x4".
bool ParseFrameSpec(const std::string &spec, std::vector<double> *times) {
  std::string s = spec;
  for (char &c : s) {
    if (c == ',') c = ' ';
  }
  std::istringstream iss(s);
  std::string tok;
  while (iss >> tok) {
    double start = 0, end = 0, stride = 1;
    const size_t colon = tok.find(':');
    if (colon == std::string::npos) {
      try {
        start = end = std::stod(tok);
      } catch (...) {
        return false;
      }
    } else {
      std::string a = tok.substr(0, colon);
      std::string rest = tok.substr(colon + 1);
      const size_t xpos = rest.find('x');
      std::string b = (xpos == std::string::npos) ? rest : rest.substr(0, xpos);
      try {
        start = std::stod(a);
        end = std::stod(b);
        if (xpos != std::string::npos) stride = std::stod(rest.substr(xpos + 1));
      } catch (...) {
        return false;
      }
    }
    if (stride == 0) stride = 1;
    stride = std::fabs(stride);
    if (start <= end) {
      for (double t = start; t <= end + 1e-9; t += stride) times->push_back(t);
    } else {
      for (double t = start; t >= end - 1e-9; t -= stride) times->push_back(t);
    }
  }
  return !times->empty();
}

// Substitute a frame number into an output path. Runs of '#' are replaced by the
// zero-padded frame number (width = number of '#'). If there is no '#', the
// frame number is inserted before the extension (.NNNN).
std::string SubstituteFrame(const std::string &path, long frame) {
  const size_t hpos = path.find('#');
  if (hpos != std::string::npos) {
    size_t hend = hpos;
    while (hend < path.size() && path[hend] == '#') ++hend;
    const int width = int(hend - hpos);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%0*ld", width, frame);
    return path.substr(0, hpos) + buf + path.substr(hend);
  }
  const size_t dot = path.find_last_of('.');
  char buf[32];
  std::snprintf(buf, sizeof(buf), ".%04ld", frame);
  if (dot == std::string::npos) return path + buf;
  return path.substr(0, dot) + buf + path.substr(dot);
}

int RunRTPreviewNext(const Options &opt) {
  RenderContext ctx;
  if (!BuildRenderContext(opt, ctx)) return EXIT_FAILURE;
  if (opt.stats) PrintRTStats(ctx);

  // Animation: -frames renders one image per time code, re-evaluating geometry,
  // transforms and any animated camera at that time. The scene is parsed once;
  // each frame re-streams + rebuilds the BVH (geometry may deform).
  if (!opt.frames.empty()) {
    std::vector<double> times;
    if (!ParseFrameSpec(opt.frames, &times)) {
      std::cerr << "Invalid -frames FRAMESPEC: " << opt.frames << "\n";
      return EXIT_FAILURE;
    }
    if (opt.output.empty()) {
      std::cerr << "-frames requires an output path (use # for the frame "
                   "number, e.g. frame.####.png).\n";
      return EXIT_FAILURE;
    }
    // If the rendered geometry is static across time (only the camera and/or
    // nothing animates), the BVH built in BuildRenderContext is valid for every
    // frame -- reuse it and only re-resolve the camera per frame. Otherwise
    // re-stream + rebuild the BVH at each time.
    const bool geom_animated = SceneGeometryAnimated(ctx.stage, opt.mask);
    if (opt.stats) {
      std::cerr << "rt frames: " << times.size()
                << ", geometry animated: " << (geom_animated ? 1 : 0)
                << " (BVH " << (geom_animated ? "rebuilt per frame" : "reused")
                << ")\n";
    }
    for (double t : times) {
      if (geom_animated) {
        if (!ExtractAndBuildBVH(ctx, t)) return EXIT_FAILURE;
      } else {
        ctx.frame_time = t;  // static geometry: keep BVH, animate camera only
      }
      ResolveCameraNext(ctx);
      const std::string out = SubstituteFrame(opt.output, std::lround(t));
      const double secs = RenderFrameTo(ctx, out);
      if (secs < 0.0) return EXIT_FAILURE;
      std::cerr << "frame " << t << " -> " << out << "  (" << secs << "s)\n";
    }
    return EXIT_SUCCESS;
  }

  const double secs = RenderFrameTo(ctx, opt.output);
  if (secs < 0.0) return EXIT_FAILURE;
  if (opt.stats) std::cerr << "render seconds: " << secs << "\n";
  return EXIT_SUCCESS;
}


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
