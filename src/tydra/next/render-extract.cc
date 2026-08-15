// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Shared render-oriented extraction helpers for next::Stage.

#include "render-extract.hh"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "next/schema/geom-point-instancer.hh"
#include "next/schema/geom-xform.hh"
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

void PushRecord(RenderPrimRecord&& rec, bool collect_records,
                RenderExtractResult* out) {
  if (collect_records) out->records.push_back(rec);
  if (rec.type_name == "Points" ||
      rec.type_name == "ParticleField3DGaussianSplat") {
    out->points.push_back(std::move(rec));
    return;
  }
  switch (rec.kind) {
    case RenderPrimKind::Mesh: out->meshes.push_back(std::move(rec)); break;
    case RenderPrimKind::PointInstancer:
      out->point_instancers.push_back(std::move(rec));
      break;
    case RenderPrimKind::NativeInstance:
      if (!rec.native_prototype.empty())
        out->native_prototype_holders.insert(rec.native_prototype);
      out->native_instances.push_back(std::move(rec));
      break;
    case RenderPrimKind::Light: out->lights.push_back(std::move(rec)); break;
    case RenderPrimKind::Camera: out->cameras.push_back(std::move(rec)); break;
    case RenderPrimKind::Material:
      out->materials.push_back(std::move(rec));
      break;
    case RenderPrimKind::Volume: out->volumes.push_back(std::move(rec)); break;
    case RenderPrimKind::Curve: out->curves.push_back(std::move(rec)); break;
    case RenderPrimKind::Skeleton:
      out->skeletons.push_back(std::move(rec));
      break;
    default: break;
  }
}

void CollectRec(const ::tinyusdz::next::UsdPrim& root,
                const RenderExtractOptions& options,
                const double parent_world[16],
                const std::string& inherited_purpose,
                const std::string& inherited_material,
                const std::string& inherited_strong_material,
                RenderExtractResult* out) {
  struct Frame {
    ::tinyusdz::next::UsdPrim prim;
    std::string purpose;
    std::string material;
    std::string strong_material;
    double parent_world[16];
    size_t depth = 0;
    // IsActive() walks all ancestors. Carry the accumulated state instead so
    // a deep hierarchy remains O(number of prims), not O(depth squared).
    bool active = true;
    bool animated_world = false;
  };

  std::vector<Frame> stack;
  size_t emitted_records = 0;
  Frame first;
  first.prim = root;
  first.purpose = inherited_purpose;
  first.material = inherited_material;
  first.strong_material = inherited_strong_material;
  std::memcpy(first.parent_world, parent_world, sizeof(first.parent_world));
  first.active = options.include_inactive || root.IsActive();
  stack.push_back(std::move(first));

  while (!stack.empty()) {
    Frame frame = std::move(stack.back());
    stack.pop_back();
    if (options.max_depth != 0 && frame.depth > options.max_depth) {
      out->limit_exceeded = true;
      continue;
    }
    const ::tinyusdz::next::UsdPrim& prim = frame.prim;
    if (!frame.active && !options.include_inactive) continue;

    RenderPrimRecord rec;
    rec.prim = prim;
    rec.path = prim.GetPath().str();
    rec.type_name = prim.GetTypeName();
    rec.purpose = PurposeForPrim(prim, frame.purpose);
    rec.animated_world =
        frame.animated_world ||
        ::tinyusdz::next::UsdGeomXform(prim).HasAnimatedTransform();
    const std::string local_material =
        ::tinyusdz::next::GetBoundMaterialPath(prim);
    const std::string nearest_material =
        local_material.empty() ? frame.material : local_material;
    std::string strong_material = frame.strong_material;
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
      MulRowMajor(rec.local, frame.parent_world, rec.world);
    }
    rec.kind = Classify(prim, rec.type_name, &rec.native_prototype);
    const bool stop_children =
        (rec.kind == RenderPrimKind::PointInstancer &&
         options.stop_at_point_instancers) ||
        (rec.kind == RenderPrimKind::NativeInstance &&
         options.stop_at_native_instances);
    const bool collect_this_record =
        rec.kind != RenderPrimKind::Other || options.collect_other;
    if (collect_this_record) {
      if (options.max_records != 0 && emitted_records >= options.max_records) {
        out->limit_exceeded = true;
        continue;
      }
      ++emitted_records;
    }

    // GetChildren() materializes a vector for every prim.  The iterative
    // extractor already owns its traversal stack, so walk the indexed child
    // handles directly and avoid one temporary allocation per prim.
    if (!stop_children) {
      const size_t child_count = prim.GetChildCount();
      for (size_t child_index = child_count; child_index > 0; --child_index) {
        const ::tinyusdz::next::UsdPrim child_prim =
            prim.GetChildAt(child_index - 1);
        if (!child_prim.IsValid()) continue;
        Frame child_frame;
        child_frame.prim = child_prim;
        child_frame.purpose = rec.purpose;
        child_frame.material = nearest_material;
        child_frame.strong_material = strong_material;
        std::memcpy(child_frame.parent_world, rec.world,
                    sizeof(child_frame.parent_world));
        child_frame.depth = frame.depth + 1;
        child_frame.active = options.include_inactive ||
                             (frame.active && child_prim.GetMeta().active);
        child_frame.animated_world = rec.animated_world;
        stack.push_back(std::move(child_frame));
      }
    }
    if (collect_this_record) PushRecord(std::move(rec), options.collect_records, out);
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
  return type_name == "Points" ||
         type_name == "ParticleField3DGaussianSplat" ||
         type_name == "Volume" ||
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

  // Keep traversal iterative: composed/programmatically-created stages can
  // contain thousands of nested Xforms, and recursion here would turn input
  // depth into native-stack exhaustion. Push children in reverse so the
  // resulting order remains the same as the recursive (ascending child index)
  // implementation.
  struct Entry {
    ::tinyusdz::next::UsdPrim prim;
    bool active = true;
  };
  std::vector<Entry> stack;
  stack.reserve(32);
  stack.push_back(Entry{root, true});
  while (!stack.empty()) {
    const Entry entry = std::move(stack.back());
    stack.pop_back();
    const ::tinyusdz::next::UsdPrim& prim = entry.prim;
    if (!entry.active) continue;
    if (prim.GetTypeName() == "Mesh") out->push_back(prim);

    const size_t child_count = prim.GetChildCount();
    for (size_t i = child_count; i > 0; --i) {
      const ::tinyusdz::next::UsdPrim child = prim.GetChildAt(i - 1);
      if (child.IsValid()) {
        // IsActive() walks every ancestor. The parent entry is already known
        // active, so one local metadata check preserves its result without
        // turning a deep chain into O(depth^2) work.
        stack.push_back(Entry{child, entry.active && child.GetMeta().active});
      }
    }
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
