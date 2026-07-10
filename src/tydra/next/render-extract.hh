// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Shared render-oriented extraction helpers for next::Stage.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "next/schema/geom-point-instancer.hh"
#include "next/stage/stage.hh"
#include "next/types/value-view.hh"

namespace tinyusdz {
namespace tydra {
namespace next {

enum class RenderPrimKind {
  Other,
  Mesh,
  PointInstancer,
  NativeInstance,
  Light,
  Camera,
  Material,
  Volume,
  Curve,
  Skeleton
};

bool IsAnalyticGeomTypeName(const std::string& type_name);
bool IsMeshRenderableTypeName(const std::string& type_name);
bool IsUnsupportedRenderableTypeName(const std::string& type_name);

struct RenderPrimRecord {
  ::tinyusdz::next::UsdPrim prim;
  RenderPrimKind kind = RenderPrimKind::Other;
  std::string path;
  std::string type_name;
  std::string purpose = "default";
  std::string native_prototype;
  double local[16];
  double world[16];
};

struct RenderExtractOptions {
  double time_code = 0.0;
  bool include_inactive = false;
  bool stop_at_point_instancers = false;
  bool stop_at_native_instances = false;
  bool collect_other = false;
};

struct RenderExtractResult {
  std::vector<RenderPrimRecord> records;
  std::vector<RenderPrimRecord> meshes;
  std::vector<RenderPrimRecord> point_instancers;
  std::vector<RenderPrimRecord> native_instances;
  std::vector<RenderPrimRecord> lights;
  std::vector<RenderPrimRecord> cameras;
  std::vector<RenderPrimRecord> materials;
  std::vector<RenderPrimRecord> volumes;
  std::vector<RenderPrimRecord> curves;
  std::vector<RenderPrimRecord> skeletons;
  std::unordered_set<std::string> native_prototype_holders;
};

struct PointInstancerData {
  ::tinyusdz::next::UsdPrim prim;
  std::string path;
  std::vector<::tinyusdz::next::Path> prototypes;
  std::vector<int32_t> proto_indices;
  std::vector<float> positions;
  std::vector<float> orientations;
  std::vector<float> scales;
  std::vector<float> velocities;
  std::vector<float> angular_velocities;
  std::vector<int64_t> ids;
  std::vector<int64_t> invisible_ids;
  std::vector<int64_t> inactive_ids;
  std::vector<::tinyusdz::next::PointInstancerTransform> transforms;
  bool valid = false;
  std::string validation_error;
};

bool CollectRenderPrims(const ::tinyusdz::next::Stage& stage,
                        const RenderExtractOptions& options,
                        RenderExtractResult* out);

bool ReadPointInstancerData(const ::tinyusdz::next::UsdPrim& prim,
                            double time_code,
                            PointInstancerData* out);

void GatherMeshPrims(const ::tinyusdz::next::UsdPrim& root,
                     std::vector<::tinyusdz::next::UsdPrim>* out);

void CollectPrototypePaths(const ::tinyusdz::next::Stage& stage,
                           std::unordered_set<std::string>* out);

template <typename T>
struct ValueArrayRead {
  ::tinyusdz::next::ArrayScratch<T> scratch;
  ::tinyusdz::next::ArrayView<T> view;

  bool empty() const { return view.empty(); }
  size_t size() const { return view.size; }
  const T& operator[](size_t i) const { return view[i]; }
  const T* begin() const { return view.begin(); }
  const T* end() const { return view.end(); }
};

bool ReadFloatArray(const ::tinyusdz::next::UsdPrim& prim, const char* name,
                    double time, ValueArrayRead<float>* out);
bool ReadIntArray(const ::tinyusdz::next::UsdPrim& prim, const char* name,
                  double time, ValueArrayRead<int32_t>* out);
bool ReadInt64Array(const ::tinyusdz::next::UsdPrim& prim, const char* name,
                    double time, ValueArrayRead<int64_t>* out);
bool ReadUIntArray(const ::tinyusdz::next::UsdPrim& prim, const char* name,
                   double time, ValueArrayRead<uint32_t>* out);
bool ReadUInt64Array(const ::tinyusdz::next::UsdPrim& prim, const char* name,
                     double time, ValueArrayRead<uint64_t>* out);

std::vector<float> ReadFloatArrayCopy(const ::tinyusdz::next::UsdPrim& prim,
                                      const char* name, double time);
std::vector<int32_t> ReadIntArrayCopy(const ::tinyusdz::next::UsdPrim& prim,
                                      const char* name, double time);
std::vector<int64_t> ReadInt64ArrayCopy(const ::tinyusdz::next::UsdPrim& prim,
                                        const char* name, double time);
std::vector<uint32_t> ReadUIntArrayCopy(const ::tinyusdz::next::UsdPrim& prim,
                                        const char* name, double time);
std::vector<uint64_t> ReadUInt64ArrayCopy(const ::tinyusdz::next::UsdPrim& prim,
                                          const char* name, double time);

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
