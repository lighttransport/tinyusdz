// SPDX-License-Identifier: Apache-2.0
// tusdview - `next` loader -> tydra-next RenderScene -> DrawScene adapter, plus
// PointInstancer extraction into GPU-instanced draws.

#include "next_scene_loader.hh"

#include <algorithm>
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
#include "io-util.hh"                  // io::GetBaseDir
#include "usdVol.hh"                   // OpenVDB (.vdb) loader

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
bool BuildProtoMesh(const tnext::Stage& stage, tydn::RenderSceneConverter& conv,
                    const tnext::UsdPrim& mp, const matrix4d& inv_protoroot,
                    double time, DrawMeshCPU* dm, matrix4d* mesh_rel) {
  // Convert just this mesh on demand (streaming) -- avoids holding the whole
  // RenderScene in RAM.
  tydn::RenderMesh rm;
  if (!conv.ConvertMesh(mp, &rm)) return false;
  if (!FillFlatGeometry(rm, dm)) return false;
  dm->purpose = ResolveNextPurpose(stage, mp.GetPath().str());
  // Prototype displayColor is carried PER-VERTEX (FillFlatGeometry filled
  // dm->vertexColors -- uploaded to GL attrib 10 for instanced draws, shared by all
  // instances). Keep the per-instance constant neutral (white) so it doesn't tint
  // the per-vertex color; the instanced shader multiplies the two.
  if (!dm->vertexColors.empty()) {
    dm->flatColor[0] = dm->flatColor[1] = dm->flatColor[2] = 1.0f;
  }
  double mw16[16];
  tydn::ComputeWorldTransform(stage, mp, mw16, time);
  *mesh_rel = Mul4(Mat4dFromArray(mw16), inv_protoroot);
  return true;
}

// Split a prototype subtree into its DIRECT mesh prims and its NESTED instancers
// (PointInstancer / scenegraph instanceable), WITHOUT descending into the latter.
// The prototype root itself is collected only if it is a Mesh. Mirrors the
// instancer skips in the static-batching gather + tusdrender CollectProtoMeshNesting.
void SplitProtoSubtree(const tnext::UsdPrim& root,
                       std::vector<tnext::UsdPrim>* meshes,
                       std::vector<tnext::UsdPrim>* instancers) {
  std::function<void(const tnext::UsdPrim&, bool)> rec =
      [&](const tnext::UsdPrim& p, bool isRoot) {
        if (!isRoot) {
          if (p.GetTypeName() == "PointInstancer") {
            instancers->push_back(p);
            return;
          }
          const auto* s = p.GetPrimSpec();
          if (s && !s->meta().instance_prototype().empty()) {
            instancers->push_back(p);
            return;
          }
        }
        if (p.GetTypeName() == "Mesh") meshes->push_back(p);
        for (const tnext::UsdPrim& c : p.GetChildren()) rec(c, false);
      };
  rec(root, true);
}

// Emit GPU-instanced DrawMeshCPU for a prototype subtree placed at the given world
// transforms `placements`. Direct (non-instancer) meshes become one DrawMeshCPU
// each (instanceXforms = mesh_rel * each placement). NESTED instancers are
// flattened: their per-instance transforms (relative to this prototype root) are
// composed with each outer placement and the inner prototype is emitted
// recursively, so a TLAS-less GL preview still shows nested instancing -- geometry
// stays deduped (shared VBO), only the per-instance matrix list grows. Routing the
// top-level PointInstancer/native passes through this is byte-identical when nothing
// nests (same mesh order, same per-placement loop). `placementColors`, when set, is
// 3 floats/placement applied as per-instance color to this level's direct meshes.
void EmitInstancedProto(const tnext::Stage& stage,
                        tydn::RenderSceneConverter& conv,
                        const tnext::UsdPrim& protoRoot,
                        const std::vector<matrix4d>& placements,
                        const std::vector<float>* placementColors, double time,
                        DrawScene* draw, Bounds* bounds, long long* instTotal,
                        long long* effectiveTris, size_t instBudget,
                        std::unordered_set<std::string>* consumed) {
  if (placements.empty()) return;
  double pr16[16];
  tydn::ComputeWorldTransform(stage, protoRoot, pr16, time);
  const matrix4d inv_proto = ::tinyusdz::inverse(Mat4dFromArray(pr16));

  std::vector<tnext::UsdPrim> directMeshes, nestedInstancers;
  SplitProtoSubtree(protoRoot, &directMeshes, &nestedInstancers);

  const bool haveColors =
      placementColors && placementColors->size() == placements.size() * 3;

  for (const tnext::UsdPrim& mp : directMeshes) {
    if (consumed) consumed->insert(mp.GetPath().str());
    DrawMeshCPU dm;
    matrix4d mesh_rel;
    if (!BuildProtoMesh(stage, conv, mp, inv_proto, time, &dm, &mesh_rel)) continue;
    // Prototype-LOCAL bbox over the (untransformed) vertices, for per-instance
    // frustum culling + CUDA instance world-AABBs (each instance transforms it).
    if (!dm.vertices.empty()) {
      float lo[3] = {dm.vertices[0].px, dm.vertices[0].py, dm.vertices[0].pz};
      float hi[3] = {lo[0], lo[1], lo[2]};
      for (const DrawVertex& v : dm.vertices) {
        lo[0] = std::min(lo[0], v.px); hi[0] = std::max(hi[0], v.px);
        lo[1] = std::min(lo[1], v.py); hi[1] = std::max(hi[1], v.py);
        lo[2] = std::min(lo[2], v.pz); hi[2] = std::max(hi[2], v.pz);
      }
      for (int k = 0; k < 3; ++k) { dm.protoAabbMin[k] = lo[k]; dm.protoAabbMax[k] = hi[k]; }
    }
    dm.instanceXforms.reserve(placements.size() * 12);
    if (haveColors) dm.instanceColors.reserve(placements.size() * 3);
    for (size_t k = 0; k < placements.size(); ++k) {
      if (static_cast<size_t>(*instTotal) + dm.instanceXforms.size() / 12 >=
          instBudget)
        break;
      const matrix4d fin = Mul4(mesh_rel, placements[k]);
      float o2w[12];
      Mat4dToO2W(fin, o2w);
      dm.instanceXforms.insert(dm.instanceXforms.end(), o2w, o2w + 12);
      if (haveColors) {
        dm.instanceColors.push_back((*placementColors)[k * 3 + 0]);
        dm.instanceColors.push_back((*placementColors)[k * 3 + 1]);
        dm.instanceColors.push_back((*placementColors)[k * 3 + 2]);
      }
      const float tpos[3] = {static_cast<float>(fin.m[3][0]),
                             static_cast<float>(fin.m[3][1]),
                             static_cast<float>(fin.m[3][2])};
      bounds->add(tpos);
    }
    if (dm.instanceXforms.empty()) continue;
    const size_t ninst = dm.instanceXforms.size() / 12;
    *instTotal += static_cast<long long>(ninst);
    *effectiveTris += (dm.indices.size() / 3) * ninst;
    for (int k = 0; k < 3; ++k) {
      dm.aabbMin[k] = bounds->mn[k];
      dm.aabbMax[k] = bounds->mx[k];
    }
    std::memset(dm.world, 0, sizeof(dm.world));
    dm.world[0] = dm.world[5] = dm.world[10] = dm.world[15] = 1.0f;
    draw->triangleCount += dm.indices.size() / 3;
    draw->meshes.push_back(std::move(dm));
  }

  // Nested instancers: compose each per-instance transform (relative to protoRoot)
  // with every outer placement, then recurse on the inner prototype.
  static const float kIdentQuat[4] = {0, 0, 0, 1};
  static const float kUnitScale[3] = {1, 1, 1};
  for (const tnext::UsdPrim& ni : nestedInstancers) {
    if (static_cast<size_t>(*instTotal) >= instBudget) break;
    if (ni.GetTypeName() == "PointInstancer") {
      double iw16[16];
      tydn::ComputeWorldTransform(stage, ni, iw16, time);
      const matrix4d ni_rel = Mul4(Mat4dFromArray(iw16), inv_proto);
      const std::vector<float> positions = ReadFloats(ni, "positions", time);
      const size_t n = positions.size() / 3;
      const std::vector<int32_t> protoIdx = ReadInts(ni, "protoIndices", time);
      const std::vector<float> orients = ReadFloats(ni, "orientations", time);
      const std::vector<float> scales = ReadFloats(ni, "scales", time);
      const std::vector<int64_t> invis = ReadInt64s(ni, "invisibleIds", time);
      const std::unordered_set<int64_t> invisSet(invis.begin(), invis.end());
      const std::vector<tnext::Path>* iprotos = ni.GetRelationship("prototypes");
      if (!iprotos) continue;
      std::vector<std::vector<uint32_t>> byProto(iprotos->size());
      for (size_t i = 0; i < n; ++i) {
        if (!invisSet.empty() && invisSet.count(int64_t(i))) continue;
        const int pix = (i < protoIdx.size()) ? protoIdx[i] : 0;
        if (pix >= 0 && pix < int(iprotos->size())) byProto[pix].push_back(uint32_t(i));
      }
      for (size_t pix = 0; pix < iprotos->size(); ++pix) {
        if (byProto[pix].empty()) continue;
        tnext::UsdPrim innerRoot = stage.GetPrimAtPath((*iprotos)[pix]);
        if (!innerRoot.IsValid()) continue;
        std::vector<matrix4d> innerPl;
        innerPl.reserve(byProto[pix].size() * placements.size());
        bool capped = false;
        for (const matrix4d& P : placements) {
          const matrix4d eff = Mul4(ni_rel, P);  // instancer effective world
          for (uint32_t j : byProto[pix]) {
            if (static_cast<size_t>(*instTotal) + innerPl.size() >= instBudget) {
              capped = true;
              break;
            }
            const float* q =
                (orients.size() >= (j + 1) * 4) ? &orients[j * 4] : kIdentQuat;
            const float* s =
                (scales.size() >= (j + 1) * 3) ? &scales[j * 3] : kUnitScale;
            innerPl.push_back(Mul4(InstanceTRS(&positions[j * 3], q, s), eff));
          }
          if (capped) break;
        }
        EmitInstancedProto(stage, conv, innerRoot, innerPl, nullptr, time, draw,
                           bounds, instTotal, effectiveTris, instBudget, consumed);
      }
    } else {
      const auto* s = ni.GetPrimSpec();
      if (!s) continue;
      const std::string ipath = s->meta().instance_prototype();
      if (ipath.empty()) continue;
      double w16[16];
      tydn::ComputeWorldTransform(stage, ni, w16, time);
      const matrix4d m_rel = Mul4(Mat4dFromArray(w16), inv_proto);
      // The native instance's children are proxies of its prototype; consume them.
      std::vector<tnext::UsdPrim> proxies;
      GatherMeshPrims(ni, &proxies);
      if (consumed)
        for (const tnext::UsdPrim& m : proxies) consumed->insert(m.GetPath().str());
      tnext::UsdPrim innerRoot = stage.GetPrimAtPath(ipath);
      if (!innerRoot.IsValid()) continue;
      std::vector<matrix4d> innerPl;
      innerPl.reserve(placements.size());
      for (const matrix4d& P : placements) innerPl.push_back(Mul4(m_rel, P));
      EmitInstancedProto(stage, conv, innerRoot, innerPl, nullptr, time, draw,
                         bounds, instTotal, effectiveTris, instBudget, consumed);
    }
  }
}

}  // namespace

// UsdVol volumes for the `next` path: walk the stage, find Volume prims,
// resolve each `field:*` relationship to its field-asset prim, load the .vdb
// (relative to the USD file dir), and emit a DrawVolumeCPU. Extends `bounds`
// with the volume world-AABB so the camera frames it.
void BuildNextVolumes(const tnext::Stage& stage, const std::string& usdPath,
                      double time, DrawScene* draw, Bounds* bounds) {
  const std::string baseDir = tinyusdz::io::GetBaseDir(usdPath);

  std::function<void(const tnext::UsdPrim&)> rec = [&](const tnext::UsdPrim& p) {
    if (p.GetTypeName() == "Volume") {
      double w16[16];
      tydn::ComputeWorldTransform(stage, p, w16, time);

      for (const std::string& relName : p.GetRelationshipNames()) {
        if (relName.rfind("field:", 0) != 0) continue;
        const std::vector<tnext::Path>* targets = p.GetRelationship(relName);
        if (!targets || targets->empty()) continue;
        tnext::UsdPrim field = stage.GetPrimAtPath((*targets)[0]);
        if (!field) continue;

        const tnext::Value* fp = field.GetPropertyValue("filePath");
        const std::string* ap = fp ? fp->as_asset_path() : nullptr;
        if (!ap || ap->empty()) continue;
        std::string fieldName = relName.substr(std::strlen("field:"));
        if (const tnext::Value* fn = field.GetPropertyValue("fieldName")) {
          if (const std::string* tk = fn->as_token()) fieldName = *tk;
        }

        // Resolve the asset path relative to the USD file directory.
        std::string vpath = *ap;
        if (!vpath.empty() && vpath[0] != '/' && !baseDir.empty()) {
          vpath = baseDir + "/" + vpath;
        }
        std::vector<tinyusdz::usdVol::VDBGrid> grids;
        std::string vw, ve;
        if (!tinyusdz::usdVol::ReadVDBFromFile(vpath, &grids, &vw, &ve) || grids.empty()) {
          continue;
        }
        const tinyusdz::usdVol::VDBGrid* g = nullptr;
        for (const auto& gg : grids)
          if (gg.name == fieldName) { g = &gg; break; }
        if (!g) g = &grids[0];
        if (g->data.empty() || g->dim[0] <= 0 || g->dim[1] <= 0 || g->dim[2] <= 0)
          continue;

        DrawVolumeCPU dv;
        dv.name = p.GetName();
        for (int k = 0; k < 16; ++k) dv.world[k] = static_cast<float>(w16[k]);
        dv.density = g->data;
        for (int a = 0; a < 3; ++a) {
          dv.dim[a] = g->dim[a];
          dv.aabbMin[a] = float(g->origin[a]) * float(g->voxel_size[a]) +
                          float(g->world_translation[a]);
          dv.aabbMax[a] = float(g->origin[a] + g->dim[a]) * float(g->voxel_size[a]) +
                          float(g->world_translation[a]);
        }
        dv.background = g->background;

        // Extend scene bounds with the volume world-AABB (8 corners). World
        // matrix is stored row-major-USD in w16 (p' = p * M), matching meshes.
        for (int corner = 0; corner < 8; ++corner) {
          float lp[3] = {(corner & 1) ? dv.aabbMax[0] : dv.aabbMin[0],
                         (corner & 2) ? dv.aabbMax[1] : dv.aabbMin[1],
                         (corner & 4) ? dv.aabbMax[2] : dv.aabbMin[2]};
          float wp[3];
          for (int c = 0; c < 3; ++c)
            wp[c] = lp[0] * float(w16[0 * 4 + c]) + lp[1] * float(w16[1 * 4 + c]) +
                    lp[2] * float(w16[2 * 4 + c]) + float(w16[3 * 4 + c]);
          bounds->add(wp);
        }
        draw->volumes.push_back(std::move(dv));
      }
    }
    for (const tnext::UsdPrim& c : p.GetChildren()) rec(c);
  };
  for (const tnext::UsdPrim& r : stage.GetRootPrims()) rec(r);
}

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

  // --- 2. A per-mesh converter (NOT a full-scene Convert). We triangulate each
  //        mesh on demand (ConvertMesh) as we walk the stage and free it right
  //        after baking it into a batch, so the whole RenderScene's geometry
  //        (~half the load peak) is never resident at once. ---
  tydn::ConverterConfig cfg;
  cfg.mesh.triangulate = true;
  cfg.mesh.triangulation_method = tydn::MeshConfig::TriangulationMethod::Fan;
  cfg.mesh.compute_normals = true;
  cfg.mesh.build_vertex_indices = true;
  cfg.time_code = time;
  tydn::RenderSceneConverter conv(cfg);
  draw->upAxis = (stage.GetUpAxis() == "Z" || stage.GetUpAxis() == "z") ? "Z" : "Y";

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
          // Consume ALL of this prototype's mesh paths (including nested-instancer
          // ones) so the static-batching pass never draws them as base geometry --
          // prototypes can live outside the instancer subtree.
          std::vector<tnext::UsdPrim> protoMeshes;
          GatherMeshPrims(protoRoot, &protoMeshes);
          for (const tnext::UsdPrim& mp : protoMeshes)
            consumed.insert(mp.GetPath().str());
          if (byProto[pi].empty()) continue;
          // One world placement (+ optional per-instance color) per visible
          // instance; EmitInstancedProto bakes mesh_rel*placement and recurses into
          // any nested instancers under the prototype.
          std::vector<matrix4d> placements;
          std::vector<float> colors;
          placements.reserve(byProto[pi].size());
          if (perInstColor) colors.reserve(byProto[pi].size() * 3);
          for (uint32_t i : byProto[pi]) {
            const float* q =
                (orients.size() >= (i + 1) * 4) ? &orients[i * 4] : kIdentQuat;
            const float* s =
                (scales.size() >= (i + 1) * 3) ? &scales[i * 3] : kUnitScale;
            placements.push_back(
                Mul4(InstanceTRS(&positions[i * 3], q, s), instancer_world));
            if (perInstColor) {
              colors.push_back(instCol[i * 3 + 0]);
              colors.push_back(instCol[i * 3 + 1]);
              colors.push_back(instCol[i * 3 + 2]);
            }
          }
          EmitInstancedProto(stage, conv, protoRoot, placements,
                             perInstColor ? &colors : nullptr, time, draw, &bounds,
                             &instTotal, &effectiveTris, instBudget, &consumed);
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
      // GPU-instance the prototype's geometry at each native-instance placement;
      // EmitInstancedProto recurses into any nested instancers under the prototype.
      // consumed=nullptr: the prototype prim itself still renders via 3b (unchanged).
      EmitInstancedProto(stage, conv, protoRoot, kv.second, /*placementColors=*/nullptr,
                         time, draw, &bounds, &instTotal, &effectiveTris, instBudget,
                         /*consumed=*/nullptr);
    }
  }

  // --- 3b. Non-instanced meshes: STATIC BATCHING. Each mesh's vertices are baked
  //         to world space and merged into a few big buffers keyed by (purpose,
  //         geometric-normal), so a 33k-mesh scene draws in a handful of calls
  //         (one VAO/VBO/EBO per batch) instead of 33k -- far less draw-call + GL
  //         object overhead. Purpose stays per-batch so the GUI toggles still
  //         work; per-mesh pick/hide is not a goal of the flat large-scene path.
  struct Batch { DrawMeshCPU dm; bool anyColor = false; };
  std::map<std::pair<std::string, bool>, Batch> open;  // key -> current batch
  const size_t kBatchVtxCap = size_t(8) << 20;  // 8M verts/batch (indices stay 32-bit)

  auto flushBatch = [&](Batch& b) {
    if (b.dm.vertices.empty()) return;
    if (!b.anyColor) b.dm.vertexColors.clear();
    if (bounds.has)
      for (int k = 0; k < 3; ++k) {
        b.dm.aabbMin[k] = bounds.mn[k]; b.dm.aabbMax[k] = bounds.mx[k];
      }
    b.dm.submeshes.push_back(
        DrawSubmesh{0, static_cast<uint32_t>(b.dm.indices.size()), 0});
    std::memset(b.dm.world, 0, sizeof(b.dm.world));
    b.dm.world[0] = b.dm.world[5] = b.dm.world[10] = b.dm.world[15] = 1.0f;
    draw->triangleCount += b.dm.indices.size() / 3;
    draw->meshes.push_back(std::move(b.dm));
    b = Batch();
  };

  // Gather the non-prototype mesh prims by walking the stage (there is no
  // RenderScene node list now); convert + bake each one streaming.
  std::vector<tnext::UsdPrim> meshPrims;
  {
    std::function<void(const tnext::UsdPrim&)> g = [&](const tnext::UsdPrim& p) {
      // Geometry under PointInstancer prototypes and scenegraph-instance proxies
      // was already consumed + GPU-instanced above, so do NOT descend into those
      // subtrees. GetChildren() on an instanceable prim expands the prototype as
      // instance proxies, so recursing here costs O(instances x prototype-meshes)
      // of pure wasted traversal -- pathological on heavily-instanced scenes
      // (e.g. the full Moana Island). The standalone prototype prim is still
      // reached via the normal hierarchy below.
      if (p.GetTypeName() == "PointInstancer") return;
      const auto* s = p.GetPrimSpec();
      if (s && !s->meta().instance_prototype().empty()) return;
      if (p.GetTypeName() == "Mesh" && !consumed.count(p.GetPath().str()))
        meshPrims.push_back(p);
      for (const tnext::UsdPrim& c : p.GetChildren()) g(c);
    };
    for (const tnext::UsdPrim& r : stage.GetRootPrims()) g(r);
  }

  bool capped = false;
  for (const tnext::UsdPrim& mp : meshPrims) {
    if (ctrl && ctrl->cancel.load()) break;
    if (capped) break;
    tydn::RenderMesh m;
    if (!conv.ConvertMesh(mp, &m)) continue;
    DrawMeshCPU loc;
    if (!FillFlatGeometry(m, &loc)) continue;
    const std::string purpose = ResolveNextPurpose(stage, mp.GetPath().str());
    double mw16[16];
    tydn::ComputeWorldTransform(stage, mp, mw16, time);
    float Mf[16];
    for (int k = 0; k < 16; ++k) Mf[k] = static_cast<float>(mw16[k]);
    const float* M = Mf;  // row-major, p*M (same as the converter's node xform)

    Batch& b = open[{purpose, loc.geometricNormal}];
    if (!b.dm.vertices.empty() &&
        b.dm.vertices.size() + loc.vertices.size() > kBatchVtxCap) {
      flushBatch(b);  // resets b in the map slot
    }
    b.dm.purpose = purpose;
    b.dm.geometricNormal = loc.geometricNormal;
    const bool hasC = !loc.vertexColors.empty();
    // Allocate the batch color buffer only once a mesh actually contributes a
    // color: back-fill white for the vertices already in the batch. No-color
    // batches (e.g. the hotel) then never allocate a 12 B/vertex white buffer.
    if (hasC && !b.anyColor) {
      b.dm.vertexColors.assign(b.dm.vertices.size() * 3, 1.0f);
      b.anyColor = true;
    }

    // NOTE: rely on the vectors' amortized (doubling) growth -- an exact
    // reserve(size()+n) per mesh would reallocate the whole batch every mesh (O(N^2)).
    const uint32_t vbase = static_cast<uint32_t>(b.dm.vertices.size());
    for (size_t i = 0; i < loc.vertices.size(); ++i) {
      DrawVertex v = loc.vertices[i];
      float wp[3], wn[3];
      for (int c = 0; c < 3; ++c) {
        wp[c] = v.px * M[0 * 4 + c] + v.py * M[1 * 4 + c] + v.pz * M[2 * 4 + c] +
                M[3 * 4 + c];
        wn[c] = v.nx * M[0 * 4 + c] + v.ny * M[1 * 4 + c] + v.nz * M[2 * 4 + c];
      }
      v.px = wp[0]; v.py = wp[1]; v.pz = wp[2];
      const float nl = std::sqrt(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]);
      if (nl > 1e-12f) { v.nx = wn[0] / nl; v.ny = wn[1] / nl; v.nz = wn[2] / nl; }
      else { v.nx = 0; v.ny = 0; v.nz = 0; }
      b.dm.vertices.push_back(v);
      // Only emit per-vertex color once the batch has any (back-filled above);
      // white for this mesh when it has none, to stay aligned.
      if (b.anyColor) {
        if (hasC) {
          b.dm.vertexColors.push_back(loc.vertexColors[3 * i + 0]);
          b.dm.vertexColors.push_back(loc.vertexColors[3 * i + 1]);
          b.dm.vertexColors.push_back(loc.vertexColors[3 * i + 2]);
        } else {
          b.dm.vertexColors.push_back(1.0f);
          b.dm.vertexColors.push_back(1.0f);
          b.dm.vertexColors.push_back(1.0f);
        }
      }
    }
    for (uint32_t idx : loc.indices) b.dm.indices.push_back(vbase + idx);

    // World-space AABB from the 8 local-bbox corners (scene bounds for framing).
    const tydn::Float3& lo = m.bbox_min;
    const tydn::Float3& hi = m.bbox_max;
    for (int corner = 0; corner < 8; ++corner) {
      float lp[3] = {(corner & 1) ? hi.x : lo.x, (corner & 2) ? hi.y : lo.y,
                     (corner & 4) ? hi.z : lo.z};
      float wp[3];
      for (int c = 0; c < 3; ++c)
        wp[c] = lp[0] * M[0 * 4 + c] + lp[1] * M[1 * 4 + c] +
                lp[2] * M[2 * 4 + c] + M[3 * 4 + c];
      bounds.add(wp);
    }
    if (draw->triangleCount + b.dm.indices.size() / 3 > triCap) {
      draw->truncated = true;
      capped = true;
    }
  }
  for (auto& kv : open) flushBatch(kv.second);

  // UsdVol volumes (OpenVDB): emit DrawVolumeCPU + extend bounds.
  BuildNextVolumes(stage, path, time, draw, &bounds);

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

  if (draw->meshes.empty() && draw->volumes.empty()) {
    if (err) *err = "next: no renderable mesh produced";
    return false;
  }
  return true;
}

}  // namespace tusdview
