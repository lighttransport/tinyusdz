// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Shared render-oriented extraction helpers for next::Stage.

#include "render-extract.hh"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "next/schema/geom-point-instancer.hh"
#include "next/schema/usd-shade.hh"
#include "scene-access.hh"

namespace tinyusdz {
namespace tydra {
namespace next {
namespace {

void Identity(double m[16]) {
  std::memset(m, 0, sizeof(double) * 16);
  m[0] = m[5] = m[10] = m[15] = 1.0;
}

void MulRowMajor(const double a[16], const double b[16], double out[16]) {
  double r[16];
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      r[i * 4 + j] = a[i * 4 + 0] * b[0 * 4 + j] +
                     a[i * 4 + 1] * b[1 * 4 + j] +
                     a[i * 4 + 2] * b[2 * 4 + j] +
                     a[i * 4 + 3] * b[3 * 4 + j];
    }
  }
  std::memcpy(out, r, sizeof(r));
}

bool IsLightType(const std::string& t) {
  return t == "RectLight" || t == "SphereLight" || t == "DiskLight" ||
         t == "CylinderLight" || t == "DistantLight" || t == "DomeLight" ||
         t == "DomeLight_1" || t == "PointLight" ||
         t == "GeometryLight" || t == "PortalLight" ||
         t == "PluginLight" || t == "LightFilter" ||
         t == "PluginLightFilter";
}

bool IsCurveType(const std::string& t) {
  return t == "BasisCurves" || t == "NurbsCurves" ||
         t == "HermiteCurves";
}

RenderPrimKind Classify(const ::tinyusdz::next::UsdPrim& prim,
                        const std::string& type_name,
                        std::string* native_prototype) {
  const ::tinyusdz::next::PrimSpec* spec = prim.GetPrimSpec();
  if (spec && !spec->meta().instance_prototype().empty()) {
    if (native_prototype) *native_prototype = spec->meta().instance_prototype();
    return RenderPrimKind::NativeInstance;
  }
  if (IsMeshRenderableTypeName(type_name)) return RenderPrimKind::Mesh;
  if (type_name == "PointInstancer") return RenderPrimKind::PointInstancer;
  if (IsLightType(type_name)) return RenderPrimKind::Light;
  if (type_name == "Camera") return RenderPrimKind::Camera;
  if (type_name == "Material") return RenderPrimKind::Material;
  if (type_name == "Volume") return RenderPrimKind::Volume;
  if (IsCurveType(type_name)) return RenderPrimKind::Curve;
  if (type_name == "Skeleton") return RenderPrimKind::Skeleton;
  if (IsUnsupportedRenderableTypeName(type_name)) return RenderPrimKind::Other;
  return RenderPrimKind::Other;
}

std::string PurposeForPrim(const ::tinyusdz::next::UsdPrim& prim,
                           const std::string& inherited) {
  // Purpose is inherited. Inspect only an AUTHORED local opinion here;
  // UsdPrim::GetPropertyValue also exposes the schema fallback "default",
  // which must not shadow an ancestor's authored "render/proxy/guide".
  const ::tinyusdz::next::PrimSpec* spec = prim.GetPrimSpec();
  if (const ::tinyusdz::next::Value* v =
          spec ? spec->property_value("purpose") : nullptr) {
    if (const std::string* s = v->as_token()) {
      if (*s == "render" || *s == "proxy" || *s == "guide") return *s;
      if (*s == "default") return "default";
    }
  }
  return inherited.empty() ? std::string("default") : inherited;
}

void PushRecord(const RenderPrimRecord& rec, bool collect_records,
                RenderExtractResult* out) {
  if (collect_records) out->records.push_back(rec);
  switch (rec.kind) {
    case RenderPrimKind::Mesh: out->meshes.push_back(rec); break;
    case RenderPrimKind::PointInstancer: out->point_instancers.push_back(rec); break;
    case RenderPrimKind::NativeInstance:
      out->native_instances.push_back(rec);
      if (!rec.native_prototype.empty())
        out->native_prototype_holders.insert(rec.native_prototype);
      break;
    case RenderPrimKind::Light: out->lights.push_back(rec); break;
    case RenderPrimKind::Camera: out->cameras.push_back(rec); break;
    case RenderPrimKind::Material: out->materials.push_back(rec); break;
    case RenderPrimKind::Volume: out->volumes.push_back(rec); break;
    case RenderPrimKind::Curve: out->curves.push_back(rec); break;
    case RenderPrimKind::Skeleton: out->skeletons.push_back(rec); break;
    default: break;
  }
}

void CollectRec(const ::tinyusdz::next::UsdPrim& prim,
                const RenderExtractOptions& options,
                const double parent_world[16],
                const std::string& inherited_purpose,
                const std::string& inherited_material,
                const std::string& inherited_strong_material,
                RenderExtractResult* out) {
  if (!prim.IsActive() && !options.include_inactive) return;

  RenderPrimRecord rec;
  rec.prim = prim;
  rec.path = prim.GetPath().str();
  rec.type_name = prim.GetTypeName();
  rec.purpose = PurposeForPrim(prim, inherited_purpose);
  const std::string local_material =
      ::tinyusdz::next::GetBoundMaterialPath(prim);
  const std::string nearest_material =
      local_material.empty() ? inherited_material : local_material;
  std::string strong_material = inherited_strong_material;
  if (strong_material.empty() && !local_material.empty() &&
      ::tinyusdz::next::BindingIsStrongerThanDescendants(prim)) {
    strong_material = local_material;
  }
  rec.material_path = strong_material.empty() ? nearest_material
                                               : strong_material;
  ComputeLocalTransform(prim, rec.local, options.time_code);
  if (HasResetXformStack(prim)) {
    std::memcpy(rec.world, rec.local, sizeof(rec.world));
  } else {
    MulRowMajor(rec.local, parent_world, rec.world);
  }
  rec.kind = Classify(prim, rec.type_name, &rec.native_prototype);
  if (rec.kind != RenderPrimKind::Other || options.collect_other) {
    PushRecord(rec, options.collect_records, out);
  }

  if (rec.kind == RenderPrimKind::PointInstancer &&
      options.stop_at_point_instancers) {
    return;
  }
  if (rec.kind == RenderPrimKind::NativeInstance &&
      options.stop_at_native_instances) {
    return;
  }
  for (const ::tinyusdz::next::UsdPrim& child : prim.GetChildren()) {
    CollectRec(child, options, rec.world, rec.purpose, nearest_material,
               strong_material, out);
  }
}

const ::tinyusdz::next::Value* ValueAtOrDefault(
    const ::tinyusdz::next::UsdPrim& prim,
    const ::tinyusdz::next::PropNameId& name_id, double time,
    ::tinyusdz::next::Value* hold) {
  if (!std::isnan(time)) {
    // Linear interpolation between samples (pxr semantics). The interpolated
    // value is parked in the caller-owned `hold` slot (the ValueArrayRead's
    // scratch) so the returned view stays valid while other attributes are
    // read. A shared thread_local slot here previously dangled the first
    // view whenever a caller read two animated attributes before consuming
    // the first (e.g. TetMesh points + tetVertexIndices).
    ::tinyusdz::next::Value v = prim.GetInterpolatedValue(name_id, time);
    if (!v.is_empty()) {
      *hold = std::move(v);
      return hold;
    }
    if (const ::tinyusdz::next::Value* held =
            prim.GetValueAtTime(name_id, time)) {
      return held;
    }
  }
  return prim.GetPropertyValue(name_id);
}

const ::tinyusdz::next::Value* ValueAtOrDefault(
    const ::tinyusdz::next::UsdPrim& prim, const char* name, double time,
    ::tinyusdz::next::Value* hold) {
  const auto name_id = ::tinyusdz::next::GetPropNameTable().find(name);
  if (!name_id.is_valid()) {
    return nullptr;
  }
  return ValueAtOrDefault(prim, name_id, time, hold);
}

}  // namespace

bool IsAnalyticGeomTypeName(const std::string& type_name) {
  return type_name == "Cube" || type_name == "Sphere" ||
         type_name == "Cone" || type_name == "Cylinder" ||
         type_name == "Capsule" || type_name == "Plane" ||
         type_name == "Cylinder_1" || type_name == "Capsule_1";
}

bool IsMeshRenderableTypeName(const std::string& type_name) {
  return type_name == "Mesh" || type_name == "TetMesh" ||
         IsAnalyticGeomTypeName(type_name);
}

bool IsUnsupportedRenderableTypeName(const std::string& type_name) {
  return type_name == "Points" || type_name == "Volume" ||
         type_name == "NurbsPatch";
}

bool CollectRenderPrims(const ::tinyusdz::next::Stage& stage,
                        const RenderExtractOptions& options,
                        RenderExtractResult* out) {
  if (!out) return false;
  *out = RenderExtractResult();
  if (options.collect_records) {
    const size_t estimated_records = stage.GetPrimCount();
    if (estimated_records > 0) {
      out->records.reserve(estimated_records);
    }
  }
  double identity[16];
  Identity(identity);
  for (const ::tinyusdz::next::UsdPrim& root : stage.GetRootPrims()) {
    CollectRec(root, options, identity, "default", std::string(),
               std::string(), out);
  }
  return true;
}

bool ReadPointInstancerData(const ::tinyusdz::next::UsdPrim& prim,
                            double time_code,
                            PointInstancerData* out,
                            bool compute_transforms) {
  if (!out) return false;
  *out = PointInstancerData();
  ::tinyusdz::next::UsdGeomPointInstancer pi(prim);
  if (!pi) {
    out->prim = prim;
    out->path = prim.IsValid() ? prim.GetPath().str() : std::string();
    out->validation_error = "not a PointInstancer";
    return false;
  }
  out->prim = prim;
  out->path = prim.GetPath().str();
  out->prototypes = pi.GetPrototypes();
  out->proto_indices = pi.GetProtoIndices(time_code);
  out->positions = pi.GetPositions(time_code);
  out->orientations = pi.GetOrientations(time_code);
  out->scales = pi.GetScales(time_code);
  out->velocities = pi.GetVelocities(time_code);
  out->angular_velocities = pi.GetAngularVelocities(time_code);
  out->ids = pi.GetIds(time_code);
  out->invisible_ids = pi.GetInvisibleIds(time_code);
  out->inactive_ids = pi.GetInactiveIds();
  if (compute_transforms) {
    out->transforms = pi.ComputeInstanceTransforms(time_code);
  }
  out->valid = pi.HasValidInstanceArrays(time_code, &out->validation_error);
  return true;
}

void GatherMeshPrims(const ::tinyusdz::next::UsdPrim& root,
                     std::vector<::tinyusdz::next::UsdPrim>* out) {
  if (!out || !root.IsActive()) return;
  if (root.GetTypeName() == "Mesh") out->push_back(root);
  for (const ::tinyusdz::next::UsdPrim& child : root.GetChildren()) {
    GatherMeshPrims(child, out);
  }
}

void CollectPrototypePaths(const ::tinyusdz::next::Stage& stage,
                           std::unordered_set<std::string>* out) {
  if (!out) return;
  RenderExtractOptions options;
  options.stop_at_point_instancers = true;
  options.stop_at_native_instances = true;
  options.collect_records = false;
  RenderExtractResult result;
  if (!CollectRenderPrims(stage, options, &result)) return;
  out->insert(result.native_prototype_holders.begin(),
              result.native_prototype_holders.end());
}

bool ReadFloatArray(const ::tinyusdz::next::UsdPrim& prim, const char* name,
                  double time, ValueArrayRead<float>* out) {
  const auto name_id = ::tinyusdz::next::GetPropNameTable().find(name);
  if (!name_id.is_valid()) {
    return false;
  }
  return ReadFloatArray(prim, name_id, time, out);
}

bool ReadFloatArray(const ::tinyusdz::next::UsdPrim& prim,
                   const ::tinyusdz::next::PropNameId& name_id,
                   double time, ValueArrayRead<float>* out) {
  if (!out) return false;
  const ::tinyusdz::next::Value* v =
      ValueAtOrDefault(prim, name_id, time, &out->scratch.materialized);
  if (!v) return false;
  return ::tinyusdz::next::GetFloatArrayView(*v, &out->scratch, &out->view);
}

bool ReadInt64Array(const ::tinyusdz::next::UsdPrim& prim, const char* name,
                    double time, ValueArrayRead<int64_t>* out) {
  const auto name_id = ::tinyusdz::next::GetPropNameTable().find(name);
  if (!name_id.is_valid()) {
    return false;
  }
  return ReadInt64Array(prim, name_id, time, out);
}

bool ReadIntArray(const ::tinyusdz::next::UsdPrim& prim, const char* name,
                  double time, ValueArrayRead<int32_t>* out) {
  const auto name_id = ::tinyusdz::next::GetPropNameTable().find(name);
  if (!name_id.is_valid()) {
    return false;
  }
  return ReadIntArray(prim, name_id, time, out);
}

bool ReadIntArray(const ::tinyusdz::next::UsdPrim& prim,
                  const ::tinyusdz::next::PropNameId& name_id,
                  double time, ValueArrayRead<int32_t>* out) {
  if (!out) return false;
  const ::tinyusdz::next::Value* v =
      ValueAtOrDefault(prim, name_id, time, &out->scratch.materialized);
  if (!v) return false;
  return ::tinyusdz::next::GetIntArrayView(*v, &out->scratch, &out->view);
}

bool ReadUIntArray(const ::tinyusdz::next::UsdPrim& prim, const char* name,
                   double time, ValueArrayRead<uint32_t>* out) {
  const auto name_id = ::tinyusdz::next::GetPropNameTable().find(name);
  if (!name_id.is_valid()) {
    return false;
  }
  return ReadUIntArray(prim, name_id, time, out);
}

bool ReadInt64Array(const ::tinyusdz::next::UsdPrim& prim,
                   const ::tinyusdz::next::PropNameId& name_id,
                   double time, ValueArrayRead<int64_t>* out) {
  if (!out) return false;
  const ::tinyusdz::next::Value* v =
      ValueAtOrDefault(prim, name_id, time, &out->scratch.materialized);
  if (!v) return false;
  return ::tinyusdz::next::GetInt64ArrayView(*v, &out->scratch, &out->view);
}

bool ReadUInt64Array(const ::tinyusdz::next::UsdPrim& prim, const char* name,
                     double time, ValueArrayRead<uint64_t>* out) {
  const auto name_id = ::tinyusdz::next::GetPropNameTable().find(name);
  if (!name_id.is_valid()) {
    return false;
  }
  return ReadUInt64Array(prim, name_id, time, out);
}

bool ReadUIntArray(const ::tinyusdz::next::UsdPrim& prim,
                  const ::tinyusdz::next::PropNameId& name_id, double time,
                  ValueArrayRead<uint32_t>* out) {
  if (!out) return false;
  const ::tinyusdz::next::Value* v =
      ValueAtOrDefault(prim, name_id, time, &out->scratch.materialized);
  if (!v) return false;
  return ::tinyusdz::next::GetUIntArrayView(*v, &out->scratch, &out->view);
}

bool ReadUInt64Array(const ::tinyusdz::next::UsdPrim& prim,
                    const ::tinyusdz::next::PropNameId& name_id, double time,
                    ValueArrayRead<uint64_t>* out) {
  if (!out) return false;
  const ::tinyusdz::next::Value* v =
      ValueAtOrDefault(prim, name_id, time, &out->scratch.materialized);
  if (!v) return false;
  return ::tinyusdz::next::GetUInt64ArrayView(*v, &out->scratch, &out->view);
}

std::vector<float> ReadFloatArrayCopy(const ::tinyusdz::next::UsdPrim& prim,
                                      const char* name, double time) {
  ValueArrayRead<float> r;
  if (!ReadFloatArray(prim, name, time, &r)) return {};
  return std::vector<float>(r.begin(), r.end());
}

std::vector<float> ReadFloatArrayCopy(const ::tinyusdz::next::UsdPrim& prim,
                                      const ::tinyusdz::next::PropNameId& name,
                                      double time) {
  ValueArrayRead<float> r;
  if (!ReadFloatArray(prim, name, time, &r)) return {};
  return std::vector<float>(r.begin(), r.end());
}

std::vector<int32_t> ReadIntArrayCopy(const ::tinyusdz::next::UsdPrim& prim,
                                      const char* name, double time) {
  ValueArrayRead<int32_t> r;
  if (!ReadIntArray(prim, name, time, &r)) return {};
  return std::vector<int32_t>(r.begin(), r.end());
}

std::vector<int32_t> ReadIntArrayCopy(const ::tinyusdz::next::UsdPrim& prim,
                                      const ::tinyusdz::next::PropNameId& name,
                                      double time) {
  ValueArrayRead<int32_t> r;
  if (!ReadIntArray(prim, name, time, &r)) return {};
  return std::vector<int32_t>(r.begin(), r.end());
}

std::vector<int64_t> ReadInt64ArrayCopy(const ::tinyusdz::next::UsdPrim& prim,
                                        const char* name, double time) {
  ValueArrayRead<int64_t> r;
  if (!ReadInt64Array(prim, name, time, &r)) return {};
  return std::vector<int64_t>(r.begin(), r.end());
}

std::vector<int64_t> ReadInt64ArrayCopy(const ::tinyusdz::next::UsdPrim& prim,
                                        const ::tinyusdz::next::PropNameId& name,
                                        double time) {
  ValueArrayRead<int64_t> r;
  if (!ReadInt64Array(prim, name, time, &r)) return {};
  return std::vector<int64_t>(r.begin(), r.end());
}

std::vector<uint32_t> ReadUIntArrayCopy(const ::tinyusdz::next::UsdPrim& prim,
                                        const char* name, double time) {
  ValueArrayRead<uint32_t> r;
  if (!ReadUIntArray(prim, name, time, &r)) return {};
  return std::vector<uint32_t>(r.begin(), r.end());
}

std::vector<uint32_t> ReadUIntArrayCopy(const ::tinyusdz::next::UsdPrim& prim,
                                        const ::tinyusdz::next::PropNameId& name,
                                        double time) {
  ValueArrayRead<uint32_t> r;
  if (!ReadUIntArray(prim, name, time, &r)) return {};
  return std::vector<uint32_t>(r.begin(), r.end());
}

std::vector<uint64_t> ReadUInt64ArrayCopy(const ::tinyusdz::next::UsdPrim& prim,
                                          const char* name, double time) {
  ValueArrayRead<uint64_t> r;
  if (!ReadUInt64Array(prim, name, time, &r)) return {};
  return std::vector<uint64_t>(r.begin(), r.end());
}

std::vector<uint64_t> ReadUInt64ArrayCopy(const ::tinyusdz::next::UsdPrim& prim,
                                          const ::tinyusdz::next::PropNameId& name,
                                          double time) {
  ValueArrayRead<uint64_t> r;
  if (!ReadUInt64Array(prim, name, time, &r)) return {};
  return std::vector<uint64_t>(r.begin(), r.end());
}

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
