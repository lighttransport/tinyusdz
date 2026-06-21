// SPDX-License-Identifier: Apache-2.0
// tusdview - `next` loader -> tydra-next RenderScene -> DrawScene adapter, plus
// PointInstancer extraction into GPU-instanced draws.

#include "next_scene_loader.hh"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "log.hh"

// `next` + tydra-next (built on demand; see CMakeLists.txt).
#include "next/tinyusdz-next.hh"
#include "tydra/next/render-converter.hh"
#include "tydra/next/scene-access.hh"  // ComputeWorldTransform
#include "value-types.hh"              // value::matrix4d / quatf / double3
#include "xform.hh"                    // to_matrix3x3 / to_matrix / inverse

namespace tusdview {

namespace tydn = ::tinyusdz::tydra::next;
namespace tnext = ::tinyusdz::next;
using matrix4d = ::tinyusdz::value::matrix4d;

namespace {

// Pack a tinyusdz row-major matrix4d (m[row][col], row-vector p*M) into a 3x4
// object-to-world (12 floats): row k holds the coefficients of output component
// k, i.e. worldP.k = dot(vec4(p,1), o2w_row_k). Matches tusdrender Mat4ToObj2World
// and the instanced vertex shader's aRow0/1/2.
inline void Mat4dToO2W(const matrix4d& m, float out[12]) {
  for (int k = 0; k < 3; ++k) {
    out[k * 4 + 0] = static_cast<float>(m.m[0][k]);
    out[k * 4 + 1] = static_cast<float>(m.m[1][k]);
    out[k * 4 + 2] = static_cast<float>(m.m[2][k]);
    out[k * 4 + 3] = static_cast<float>(m.m[3][k]);
  }
}

inline matrix4d Mat4dFromArray(const double d[16]) {
  matrix4d m;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) m.m[i][j] = d[i * 4 + j];
  return m;
}

// Row-major matrix multiply matching tinyusdz value::Mult: (a*b).m[j][i] =
// sum_k a.m[j][k]*b.m[k][i]. Row-vector convention: p*(a*b) applies `a` first.
inline matrix4d Mul4(const matrix4d& a, const matrix4d& b) {
  matrix4d r;
  for (int j = 0; j < 4; ++j)
    for (int i = 0; i < 4; ++i) {
      double v = 0.0;
      for (int k = 0; k < 4; ++k) v += a.m[j][k] * b.m[k][i];
      r.m[j][i] = v;
    }
  return r;
}

// Per-instance local transform from position + orientation quaternion (xyzw) +
// scale, matching tusdrender's InstanceTRS (p * S * R, translation in row 3).
inline matrix4d InstanceTRS(const float* pos, const float* q_xyzw,
                            const float* s3) {
  ::tinyusdz::value::quatf q;
  q.imag[0] = q_xyzw[0];
  q.imag[1] = q_xyzw[1];
  q.imag[2] = q_xyzw[2];
  q.real = q_xyzw[3];
  ::tinyusdz::value::matrix3d rot = ::tinyusdz::to_matrix3x3(q);
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) rot.m[i][j] *= static_cast<double>(s3[i]);
  ::tinyusdz::value::double3 t{static_cast<double>(pos[0]),
                               static_cast<double>(pos[1]),
                               static_cast<double>(pos[2])};
  return ::tinyusdz::to_matrix(rot, t);
}

// Lazy array readers: try the time sample then the default opinion; materialize a
// lazy (mmap-backed) value. Mirror tusdrender's ReadFloatArrayLazy.
std::vector<float> ReadFloats(const tnext::UsdPrim& p, const char* name, double t) {
  auto pull = [](const tnext::Value* v) -> std::vector<float> {
    if (!v) return {};
    if (v->is_lazy()) {
      tnext::Value tmp = v->materialized_copy();
      if (const auto* a = tmp.as_float_array()) return *a;
      return {};
    }
    if (const auto* a = v->as_float_array()) return *a;
    return {};
  };
  std::vector<float> r = pull(p.GetValueAtTime(name, t));
  if (r.empty()) r = pull(p.GetPropertyValue(name));
  return r;
}
std::vector<int32_t> ReadInts(const tnext::UsdPrim& p, const char* name, double t) {
  auto pull = [](const tnext::Value* v) -> std::vector<int32_t> {
    if (!v) return {};
    if (v->is_lazy()) {
      tnext::Value tmp = v->materialized_copy();
      if (const auto* a = tmp.as_int_array()) return *a;
      return {};
    }
    if (const auto* a = v->as_int_array()) return *a;
    return {};
  };
  std::vector<int32_t> r = pull(p.GetValueAtTime(name, t));
  if (r.empty()) r = pull(p.GetPropertyValue(name));
  return r;
}
std::vector<int64_t> ReadInt64s(const tnext::UsdPrim& p, const char* name, double t) {
  auto pull = [](const tnext::Value* v) -> std::vector<int64_t> {
    if (!v) return {};
    if (v->is_lazy()) {
      tnext::Value tmp = v->materialized_copy();
      if (const auto* a = tmp.as_int64_array()) return *a;
      return {};
    }
    if (const auto* a = v->as_int64_array()) return *a;
    return {};
  };
  std::vector<int64_t> r = pull(p.GetValueAtTime(name, t));
  if (r.empty()) r = pull(p.GetPropertyValue(name));
  return r;
}

// Build interleaved DrawVertex geometry (mesh-LOCAL space) + indices from a
// tydra-next RenderMesh. Returns false if there is no renderable geometry.
bool FillFlatGeometry(const tydn::RenderMesh& m, DrawMeshCPU* dm) {
  const size_t np = m.point_count();
  if (np == 0 || m.triangulated_indices.size() < 3) return false;
  std::vector<float> pts = m.points.flatten();
  std::vector<uint32_t> idx = m.triangulated_indices.flatten();
  // Authored vertex normals -> smooth shading; otherwise shade geometrically in
  // the shader (screen-derivative normal), which reads correctly on hard
  // surfaces instead of being smeared by averaged smooth normals.
  std::vector<float> nrm;
  const bool authoredNormals = m.has_normals() &&
                               m.normals_interp == tydn::Interpolation::Vertex &&
                               m.normals.size() == 3 * np;
  if (authoredNormals) nrm = m.normals.flatten();
  else nrm.assign(3 * np, 0.0f);
  dm->geometricNormal = !authoredNormals;

  std::vector<float> uv;
  const bool hasUV = m.has_texcoords() &&
                     m.texcoords_0_interp == tydn::Interpolation::Vertex &&
                     m.texcoords_0.size() == 2 * np;
  if (hasUV) uv = m.texcoords_0.flatten();

  // Per-vertex displayColor: Vertex (per-point) directly; Constant broadcast.
  std::vector<float> col;
  if (!m.colors.empty()) {
    std::vector<float> c = m.colors.flatten();
    if (m.colors_interp == tydn::Interpolation::Vertex && c.size() >= 3 * np) {
      col = std::move(c);
    } else if (m.colors_interp == tydn::Interpolation::Constant && c.size() >= 3) {
      col.resize(3 * np);
      for (size_t i = 0; i < np; ++i) {
        col[3 * i + 0] = c[0]; col[3 * i + 1] = c[1]; col[3 * i + 2] = c[2];
      }
    }
  }

  dm->name = m.name;
  dm->absPath = m.prim_path;
  dm->vertices.resize(np);
  for (size_t i = 0; i < np; ++i) {
    DrawVertex& v = dm->vertices[i];
    v.px = pts[3 * i + 0]; v.py = pts[3 * i + 1]; v.pz = pts[3 * i + 2];
    v.nx = nrm[3 * i + 0]; v.ny = nrm[3 * i + 1]; v.nz = nrm[3 * i + 2];
    v.u = hasUV ? uv[2 * i + 0] : 0.0f;
    v.v = hasUV ? uv[2 * i + 1] : 0.0f;
  }
  if (!col.empty()) dm->vertexColors = std::move(col);
  dm->indices = std::move(idx);
  dm->submeshes.push_back(
      DrawSubmesh{0, static_cast<uint32_t>(dm->indices.size()), 0});
  return true;
}

struct Bounds {
  bool has = false;
  float mn[3]{0, 0, 0}, mx[3]{0, 0, 0};
  void add(const float p[3]) {
    if (!has) { for (int k = 0; k < 3; ++k) mn[k] = mx[k] = p[k]; has = true; }
    else {
      for (int k = 0; k < 3; ++k) {
        mn[k] = std::min(mn[k], p[k]); mx[k] = std::max(mx[k], p[k]);
      }
    }
  }
};

// Collect all Mesh prim paths under `root` (inclusive).
void GatherMeshPrims(const tnext::UsdPrim& root,
                     std::vector<tnext::UsdPrim>* out) {
  if (root.GetTypeName() == "Mesh") out->push_back(root);
  for (const tnext::UsdPrim& c : root.GetChildren()) GatherMeshPrims(c, out);
}

// Resolve a prim's inherited USD `purpose` (default/render/proxy/guide): the
// nearest authored, non-"default" purpose walking self->ancestors, else
// "default". Matches mesh_build.cc ResolveInheritedPurpose for the next stage.
std::string ResolveNextPurpose(const tnext::Stage& stage, const std::string& abs) {
  std::string p = abs;
  while (!p.empty()) {
    tnext::UsdPrim prim = stage.GetPrimAtPath(p);
    if (prim.IsValid()) {
      if (const tnext::Value* v = prim.GetPropertyValue("purpose")) {
        if (const std::string* t = v->as_token()) {
          if (!t->empty() && *t != "default" && *t != "inherited") return *t;
        }
      }
    }
    if (p == "/") break;
    const size_t slash = p.find_last_of('/');
    p = (slash == std::string::npos || slash == 0) ? "/" : p.substr(0, slash);
  }
  return "default";
}

// Build a prototype mesh's local geometry (+ flat displayColor) from the
// converter, and its mesh-local -> proto-root-local transform `mesh_rel`. Shared
// by the PointInstancer and native-instance passes. Returns false if the mesh has
// no converter geometry.
bool BuildProtoMesh(const tnext::Stage& stage, const tydn::RenderScene& scene,
                    const tnext::UsdPrim& mp, const matrix4d& inv_protoroot,
                    double time, DrawMeshCPU* dm, matrix4d* mesh_rel) {
  auto it = scene.mesh_by_path.find(mp.GetPath().str());
  if (it == scene.mesh_by_path.end()) return false;
  const tydn::RenderMesh& rm = scene.meshes[static_cast<size_t>(it->second)];
  if (!FillFlatGeometry(rm, dm)) return false;
  dm->purpose = ResolveNextPurpose(stage, mp.GetPath().str());
  // Prototype displayColor -> per-draw constant (average of the mesh's colors).
  if (!rm.colors.empty()) {
    std::vector<float> cols = rm.colors.flatten();
    const size_t nc = cols.size() / 3;
    if (nc) {
      double r = 0, g = 0, b = 0;
      for (size_t c = 0; c < nc; ++c) {
        r += cols[3 * c + 0]; g += cols[3 * c + 1]; b += cols[3 * c + 2];
      }
      dm->flatColor[0] = float(r / nc);
      dm->flatColor[1] = float(g / nc);
      dm->flatColor[2] = float(b / nc);
    }
  }
  double mw16[16];
  tydn::ComputeWorldTransform(stage, mp, mw16, time);
  *mesh_rel = Mul4(Mat4dFromArray(mw16), inv_protoroot);
  return true;
}

}  // namespace

bool LoadUSDViaNext(const std::string& path, const LoadOptions& opts,
                    DrawScene* draw, std::string* warn, std::string* err,
                    LoadControl* ctrl) {
  // --- 1. Compose with the next loader (default options load payloads). ---
  tnext::Stage stage;
  std::string lwarn, lerr;
  if (!tnext::LoadUSDComposed(path, &stage, &lwarn, &lerr, /*comp_opts=*/nullptr)) {
    if (err) *err = "next: compose failed: " + lerr;
    return false;
  }
  if (warn && !lwarn.empty()) *warn = lwarn;
  const double time = std::isnan(opts.timecode) ? 0.0 : opts.timecode;

  // --- 2. Convert to a triangulated tydra-next RenderScene (geometry only;
  //        PointInstancer is NOT expanded by the converter). ---
  tydn::ConverterConfig cfg;
  cfg.mesh.triangulate = true;
  cfg.mesh.triangulation_method = tydn::MeshConfig::TriangulationMethod::Fan;
  cfg.mesh.compute_normals = true;
  cfg.mesh.build_vertex_indices = true;
  cfg.time_code = time;
  tydn::RenderSceneConverter conv(cfg);
  tydn::ConvertResult res = conv.Convert(stage);
  if (!res.success) {
    if (err) *err = "next: convert failed: " + res.error;
    return false;
  }
  const tydn::RenderScene& scene = res.scene;
  draw->upAxis = (scene.up_axis == tydn::RenderScene::UpAxis::Z) ? "Z" : "Y";

  draw->meshes.clear();
  draw->materials.clear();
  draw->textures.clear();
  draw->skipped.clear();
  draw->triangleCount = 0;
  draw->truncated = false;
  draw->materials.emplace_back();  // default gray material (index 0)

  Bounds bounds;
  const std::size_t triCap =
      ctrl ? ctrl->maxTriangles : std::numeric_limits<std::size_t>::max();

  // --- 3a. PointInstancer pass: emit one GPU-instanced DrawMeshCPU per prototype
  //         mesh. Prototype geometry lives at the converter's authored location,
  //         so we re-express it relative to the prototype root and bake each
  //         instance's placement into a per-instance model matrix. ---
  std::unordered_set<std::string> consumed;  // proto mesh paths (skip in 3b)
  long long instTotal = 0;
  long long effectiveTris = 0;
  // Optional cap on total emitted instances (VRAM budget / headless software-GL
  // testing). Each instance matrix is 64 B, so e.g. 50M ~= 3.2 GB.
  size_t instBudget = std::numeric_limits<size_t>::max();
  if (const char* mc = std::getenv("TUSDVIEW_NEXT_MAX_INSTANCES")) {
    instBudget = static_cast<size_t>(std::strtoull(mc, nullptr, 10));
  }

  std::function<void(const tnext::UsdPrim&)> walk = [&](const tnext::UsdPrim& p) {
    if (p.GetTypeName() == "PointInstancer") {
      double iw16[16];
      tydn::ComputeWorldTransform(stage, p, iw16, time);
      const matrix4d instancer_world = Mat4dFromArray(iw16);

      const std::vector<float> positions = ReadFloats(p, "positions", time);
      const size_t n = positions.size() / 3;
      const std::vector<int32_t> protoIdx = ReadInts(p, "protoIndices", time);
      const std::vector<float> orients = ReadFloats(p, "orientations", time);
      const std::vector<float> scales = ReadFloats(p, "scales", time);
      const std::vector<int64_t> invis = ReadInt64s(p, "invisibleIds", time);
      std::unordered_set<int64_t> invisSet(invis.begin(), invis.end());
      // Optional per-instance displayColor on the instancer (rgb/instance).
      const std::vector<float> instCol = ReadFloats(p, "primvars:displayColor", time);
      const bool perInstColor = (instCol.size() == 3 * n && n > 0);

      const std::vector<tnext::Path>* protos = p.GetRelationship("prototypes");
      if (protos) {
        // Bucket instance indices by prototype for an O(instances) pass.
        std::vector<std::vector<uint32_t>> byProto(protos->size());
        for (size_t i = 0; i < n; ++i) {
          if (!invisSet.empty() && invisSet.count(int64_t(i))) continue;
          int pi = (i < protoIdx.size()) ? protoIdx[i] : 0;
          if (pi >= 0 && pi < int(protos->size())) byProto[pi].push_back(uint32_t(i));
        }
        static const float kIdentQuat[4] = {0, 0, 0, 1};
        static const float kUnitScale[3] = {1, 1, 1};

        for (size_t pi = 0; pi < protos->size(); ++pi) {
          tnext::UsdPrim protoRoot = stage.GetPrimAtPath((*protos)[pi]);
          if (!protoRoot.IsValid()) continue;
          double pr16[16];
          tydn::ComputeWorldTransform(stage, protoRoot, pr16, time);
          const matrix4d inv_protoroot = ::tinyusdz::inverse(Mat4dFromArray(pr16));

          std::vector<tnext::UsdPrim> protoMeshes;
          GatherMeshPrims(protoRoot, &protoMeshes);
          for (const tnext::UsdPrim& mp : protoMeshes) {
            consumed.insert(mp.GetPath().str());
            if (byProto[pi].empty()) continue;
            DrawMeshCPU dm;
            matrix4d mesh_rel;
            if (!BuildProtoMesh(stage, scene, mp, inv_protoroot, time, &dm,
                                &mesh_rel))
              continue;

            dm.instanceXforms.reserve(byProto[pi].size() * 12);
            if (perInstColor) dm.instanceColors.reserve(byProto[pi].size() * 3);
            for (uint32_t i : byProto[pi]) {
              if (static_cast<size_t>(instTotal) +
                      dm.instanceXforms.size() / 12 >= instBudget)
                break;
              const float* q =
                  (orients.size() >= (i + 1) * 4) ? &orients[i * 4] : kIdentQuat;
              const float* s =
                  (scales.size() >= (i + 1) * 3) ? &scales[i * 3] : kUnitScale;
              const matrix4d inst_world =
                  Mul4(InstanceTRS(&positions[i * 3], q, s), instancer_world);
              const matrix4d fin = Mul4(mesh_rel, inst_world);
              float o2w[12];
              Mat4dToO2W(fin, o2w);
              dm.instanceXforms.insert(dm.instanceXforms.end(), o2w, o2w + 12);
              if (perInstColor) {
                dm.instanceColors.push_back(instCol[i * 3 + 0]);
                dm.instanceColors.push_back(instCol[i * 3 + 1]);
                dm.instanceColors.push_back(instCol[i * 3 + 2]);
              }
              const float tpos[3] = {static_cast<float>(fin.m[3][0]),
                                     static_cast<float>(fin.m[3][1]),
                                     static_cast<float>(fin.m[3][2])};
              bounds.add(tpos);
            }
            if (dm.instanceXforms.empty()) continue;
            const size_t ninst = dm.instanceXforms.size() / 12;
            instTotal += static_cast<long long>(ninst);
            effectiveTris += (dm.indices.size() / 3) * ninst;
            for (int k = 0; k < 3; ++k) {
              dm.aabbMin[k] = bounds.mn[k]; dm.aabbMax[k] = bounds.mx[k];
            }
            std::memset(dm.world, 0, sizeof(dm.world));
            dm.world[0] = dm.world[5] = dm.world[10] = dm.world[15] = 1.0f;
            // Count UNIQUE (uploaded) triangles toward the budget, not the
            // instance-expanded total -- instances share one geometry buffer, so
            // the VRAM cost is unique geom + the instance matrices (capped by
            // TUSDVIEW_NEXT_MAX_INSTANCES), not effective triangles.
            draw->triangleCount += dm.indices.size() / 3;
            draw->meshes.push_back(std::move(dm));
          }
        }
      }
      return;  // do not descend into a PointInstancer's prototypes as geometry
    }
    for (const tnext::UsdPrim& c : p.GetChildren()) walk(c);
  };
  for (const tnext::UsdPrim& r : stage.GetRootPrims()) walk(r);

  // --- 3a-native. Scenegraph (instanceable) instances: prims that share an
  //     instance_prototype are flattened by the converter (one mesh set per
  //     instance). Group them and GPU-instance the prototype's geometry instead;
  //     the prototype prim itself still renders via 3b. ---
  {
    std::map<std::string, std::vector<matrix4d>> nativeGroups;
    std::function<void(const tnext::UsdPrim&)> wn = [&](const tnext::UsdPrim& p) {
      if (p.GetTypeName() == "PointInstancer") return;  // handled above
      const auto* s = p.GetPrimSpec();
      if (s && !s->meta().instance_prototype().empty()) {
        double w16[16];
        tydn::ComputeWorldTransform(stage, p, w16, time);
        nativeGroups[s->meta().instance_prototype()].push_back(Mat4dFromArray(w16));
        // The instance's children are proxies of the prototype; the converter
        // flattened them, so consume those mesh paths (excluded from 3b).
        std::vector<tnext::UsdPrim> ms;
        GatherMeshPrims(p, &ms);
        for (const tnext::UsdPrim& m : ms) consumed.insert(m.GetPath().str());
        return;
      }
      for (const tnext::UsdPrim& c : p.GetChildren()) wn(c);
    };
    for (const tnext::UsdPrim& r : stage.GetRootPrims()) wn(r);

    for (const auto& kv : nativeGroups) {
      // A single instance is just the prototype shown twice; let 3b draw it.
      if (kv.second.size() < 2) continue;
      tnext::UsdPrim protoRoot = stage.GetPrimAtPath(kv.first);
      if (!protoRoot.IsValid()) continue;
      double pr16[16];
      tydn::ComputeWorldTransform(stage, protoRoot, pr16, time);
      const matrix4d inv_protoroot = ::tinyusdz::inverse(Mat4dFromArray(pr16));
      std::vector<tnext::UsdPrim> protoMeshes;
      GatherMeshPrims(protoRoot, &protoMeshes);
      for (const tnext::UsdPrim& mp : protoMeshes) {
        DrawMeshCPU dm;
        matrix4d mesh_rel;
        if (!BuildProtoMesh(stage, scene, mp, inv_protoroot, time, &dm, &mesh_rel))
          continue;
        dm.instanceXforms.reserve(kv.second.size() * 12);
        for (const matrix4d& iw : kv.second) {
          if (static_cast<size_t>(instTotal) + dm.instanceXforms.size() / 12 >=
              instBudget)
            break;
          const matrix4d fin = Mul4(mesh_rel, iw);
          float o2w[12];
          Mat4dToO2W(fin, o2w);
          dm.instanceXforms.insert(dm.instanceXforms.end(), o2w, o2w + 12);
          const float tpos[3] = {static_cast<float>(fin.m[3][0]),
                                 static_cast<float>(fin.m[3][1]),
                                 static_cast<float>(fin.m[3][2])};
          bounds.add(tpos);
        }
        if (dm.instanceXforms.empty()) continue;
        const size_t ninst = dm.instanceXforms.size() / 12;
        instTotal += static_cast<long long>(ninst);
        effectiveTris += (dm.indices.size() / 3) * ninst;
        for (int k = 0; k < 3; ++k) {
          dm.aabbMin[k] = bounds.mn[k]; dm.aabbMax[k] = bounds.mx[k];
        }
        std::memset(dm.world, 0, sizeof(dm.world));
        dm.world[0] = dm.world[5] = dm.world[10] = dm.world[15] = 1.0f;
        draw->triangleCount += dm.indices.size() / 3;
        draw->meshes.push_back(std::move(dm));
      }
    }
  }

  // --- 3b. Non-instanced meshes: every converter Mesh node not consumed as a
  //         prototype, placed by its world transform (the original flat path). ---
  for (const tydn::SceneNode& node : scene.nodes) {
    if (ctrl && ctrl->cancel.load()) break;
    if (node.type != tydn::NodeType::Mesh) continue;
    if (consumed.count(node.prim_path)) continue;
    int32_t mesh_id = -1;
    if (node.data_id >= 0 &&
        static_cast<size_t>(node.data_id) < scene.meshes.size()) {
      mesh_id = node.data_id;
    } else {
      auto mit = scene.mesh_by_path.find(node.prim_path);
      if (mit != scene.mesh_by_path.end()) mesh_id = mit->second;
    }
    if (mesh_id < 0 || static_cast<size_t>(mesh_id) >= scene.meshes.size())
      continue;
    const tydn::RenderMesh& m = scene.meshes[static_cast<size_t>(mesh_id)];

    DrawMeshCPU dm;
    if (!FillFlatGeometry(m, &dm)) continue;
    dm.purpose = ResolveNextPurpose(stage, node.prim_path);
    // node.world_transform.m is row-major float[16]; the row->column storage swap
    // + geometric transpose cancel, so copy element-wise (see Mat4dToColMajor).
    for (int k = 0; k < 16; ++k) dm.world[k] = node.world_transform.m[k];

    // World-space AABB from the 8 local-bbox corners.
    const tydn::Float3& lo = m.bbox_min;
    const tydn::Float3& hi = m.bbox_max;
    Bounds mb;
    for (int corner = 0; corner < 8; ++corner) {
      float lp[3] = {(corner & 1) ? hi.x : lo.x, (corner & 2) ? hi.y : lo.y,
                     (corner & 4) ? hi.z : lo.z};
      float wp[3];
      const float* M = node.world_transform.m;
      for (int c = 0; c < 3; ++c)
        wp[c] = lp[0] * M[0 * 4 + c] + lp[1] * M[1 * 4 + c] +
                lp[2] * M[2 * 4 + c] + M[3 * 4 + c];
      mb.add(wp);
      bounds.add(wp);
    }
    for (int k = 0; k < 3; ++k) { dm.aabbMin[k] = mb.mn[k]; dm.aabbMax[k] = mb.mx[k]; }
    draw->triangleCount += dm.indices.size() / 3;
    draw->meshes.push_back(std::move(dm));
    if (draw->triangleCount > triCap) { draw->truncated = true; break; }
  }

  if (bounds.has) {
    for (int k = 0; k < 3; ++k) {
      draw->aabbMin[k] = bounds.mn[k]; draw->aabbMax[k] = bounds.mx[k];
    }
    draw->hasBounds = true;
  }

  // Purpose breakdown (so the GUI's purpose toggles have something to hide).
  size_t nGuide = 0, nProxy = 0, nRender = 0;
  for (const DrawMeshCPU& dm : draw->meshes) {
    if (dm.purpose == "guide") ++nGuide;
    else if (dm.purpose == "proxy") ++nProxy;
    else if (dm.purpose == "render") ++nRender;
  }
  LOGI("next: '%s' -> %zu draws (%zu guide, %zu proxy, %zu render), %lld instances, "
       "%zu unique tris (%lld effective), instXform VRAM ~%.2f GB, up=%s%s",
       path.c_str(), draw->meshes.size(), nGuide, nProxy, nRender, instTotal,
       draw->triangleCount, effectiveTris, double(instTotal) * 48.0 / 1e9,
       draw->upAxis.c_str(), draw->truncated ? " (truncated)" : "");

  if (draw->meshes.empty()) {
    if (err) *err = "next: no renderable mesh produced";
    return false;
  }
  return true;
}

}  // namespace tusdview
