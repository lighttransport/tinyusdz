// SPDX-License-Identifier: Apache-2.0
// tusdview - `next` loader -> tydra-next RenderScene -> DrawScene adapter.

#include "next_scene_loader.hh"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "log.hh"

// `next` + tydra-next (built on demand; see CMakeLists.txt).
#include "next/tinyusdz-next.hh"
#include "tydra/next/render-converter.hh"

namespace tusdview {

namespace tydn = ::tinyusdz::tydra::next;
namespace tnext = ::tinyusdz::next;

namespace {

// USD row-vector transform of a point by a row-major 4x4 (`p_row * M`), the
// convention tinyusdz world transforms use. `out` and `p` are xyz.
inline void XformPointUSD(const float* M, const float* p, float* out) {
  for (int c = 0; c < 3; ++c) {
    out[c] = p[0] * M[0 * 4 + c] + p[1] * M[1 * 4 + c] +
             p[2] * M[2 * 4 + c] + M[3 * 4 + c];
  }
}

// Convert a USD row-major 4x4 (`m[row*4+col]`, row-vector `p*M`) into the
// light3d/GL `world[16]` (column-major, `M*p`). For the same geometric transform
// M_gl = transpose(M_usd); combined with the storage-order swap this reduces to a
// straight element copy (see mesh_build.cc MatToColMajor) — NOT a transpose.
inline void MatToColMajor(const float* rm, float* cm) {
  for (int k = 0; k < 16; ++k) cm[k] = rm[k];
}

// Area-weighted smooth vertex normals from a triangle-indexed position buffer.
// (tydra-next declares ComputeSmoothNormals but doesn't define it, and the
// Caldera proxy meshes ship without authored normals, so we compute our own.)
void ComputeSmoothNormals(const std::vector<float>& pts, size_t np,
                          const std::vector<uint32_t>& idx,
                          std::vector<float>* out) {
  out->assign(3 * np, 0.0f);
  float* n = out->data();
  const size_t ntri = idx.size() / 3;
  for (size_t t = 0; t < ntri; ++t) {
    const uint32_t a = idx[3 * t + 0], b = idx[3 * t + 1], c = idx[3 * t + 2];
    if (a >= np || b >= np || c >= np) continue;
    const float* pa = &pts[3 * a];
    const float* pb = &pts[3 * b];
    const float* pc = &pts[3 * c];
    const float e0[3] = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
    const float e1[3] = {pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]};
    // Cross product magnitude == 2*area, so this weights by face area.
    const float fn[3] = {e0[1] * e1[2] - e0[2] * e1[1],
                         e0[2] * e1[0] - e0[0] * e1[2],
                         e0[0] * e1[1] - e0[1] * e1[0]};
    for (uint32_t v : {a, b, c}) {
      n[3 * v + 0] += fn[0]; n[3 * v + 1] += fn[1]; n[3 * v + 2] += fn[2];
    }
  }
  for (size_t i = 0; i < np; ++i) {
    float* v = &n[3 * i];
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1e-12f) { v[0] /= len; v[1] /= len; v[2] /= len; }
    else { v[0] = 0.0f; v[1] = 1.0f; v[2] = 0.0f; }  // degenerate → up
  }
}

}  // namespace

bool LoadUSDViaNext(const std::string& path, const LoadOptions& opts,
                    DrawScene* draw, std::string* warn, std::string* err,
                    LoadControl* ctrl) {
  // --- 1. Compose with the next loader (default options load payloads, matching
  //        tusdrender's BuildRenderContext). Variant overrides: follow-up. ---
  tnext::Stage stage;
  std::string lwarn, lerr;
  if (!tnext::LoadUSDComposed(path, &stage, &lwarn, &lerr, /*comp_opts=*/nullptr)) {
    if (err) *err = "next: compose failed: " + lerr;
    return false;
  }
  if (warn && !lwarn.empty()) *warn = lwarn;

  // --- 2. Convert to a triangulated tydra-next RenderScene. ---
  tydn::ConverterConfig cfg;
  cfg.mesh.triangulate = true;
  cfg.mesh.triangulation_method = tydn::MeshConfig::TriangulationMethod::Fan;
  cfg.mesh.compute_normals = true;       // converter may leave normals empty; we
  cfg.mesh.build_vertex_indices = true;  // recompute below when so.
  cfg.time_code = std::isnan(opts.timecode) ? 0.0 : opts.timecode;

  tydn::RenderSceneConverter conv(cfg);
  tydn::ConvertResult res = conv.Convert(stage);
  if (!res.success) {
    if (err) *err = "next: convert failed: " + res.error;
    return false;
  }
  const tydn::RenderScene& scene = res.scene;

  // --- 3. Adapt RenderScene -> DrawScene (flat shaded). ---
  draw->meshes.clear();
  draw->materials.clear();
  draw->textures.clear();
  draw->skipped.clear();
  draw->triangleCount = 0;
  draw->truncated = false;

  // Single default gray material (so every submesh references a real material).
  draw->materials.emplace_back();  // DrawMaterialCPU defaults: baseColor 0.8 gray

  const std::size_t triCap =
      ctrl ? ctrl->maxTriangles : std::numeric_limits<std::size_t>::max();

  bool hasBounds = false;
  float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};

  for (const tydn::SceneNode& node : scene.nodes) {
    if (ctrl && ctrl->cancel.load()) break;
    if (node.type != tydn::NodeType::Mesh) continue;
    // The converter links a mesh node to its RenderMesh by path (it does not set
    // SceneNode::data_id), so resolve via mesh_by_path. Fall back to data_id if a
    // future converter starts populating it.
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

    const size_t np = m.point_count();
    if (np == 0 || m.triangulated_indices.size() < 3) continue;

    std::vector<float> pts = m.points.flatten();              // 3*np
    std::vector<uint32_t> idx = m.triangulated_indices.flatten();

    // Normals: reuse vertex-aligned authored normals, else compute smooth
    // normals from the triangulated topology (converter often leaves these
    // empty for proxy assets like Caldera).
    std::vector<float> nrm;
    if (m.has_normals() &&
        m.normals_interp == tydn::Interpolation::Vertex &&
        m.normals.size() == 3 * np) {
      nrm = m.normals.flatten();
    } else {
      ComputeSmoothNormals(pts, np, idx, &nrm);
    }

    // UVs (optional; vertex-aligned only).
    std::vector<float> uv;
    const bool hasUV = m.has_texcoords() &&
                       m.texcoords_0_interp == tydn::Interpolation::Vertex &&
                       m.texcoords_0.size() == 2 * np;
    if (hasUV) uv = m.texcoords_0.flatten();

    DrawMeshCPU dm;
    dm.name = m.name;
    dm.absPath = m.prim_path;
    dm.vertices.resize(np);
    for (size_t i = 0; i < np; ++i) {
      DrawVertex& v = dm.vertices[i];
      v.px = pts[3 * i + 0]; v.py = pts[3 * i + 1]; v.pz = pts[3 * i + 2];
      v.nx = nrm[3 * i + 0]; v.ny = nrm[3 * i + 1]; v.nz = nrm[3 * i + 2];
      v.u = hasUV ? uv[2 * i + 0] : 0.0f;
      v.v = hasUV ? uv[2 * i + 1] : 0.0f;
    }
    dm.indices = std::move(idx);
    dm.submeshes.push_back(
        DrawSubmesh{0, static_cast<uint32_t>(dm.indices.size()), 0});
    MatToColMajor(node.world_transform.m, dm.world);
    dm.doubleSided = false;

    // World-space AABB: transform the 8 local-bbox corners by the node world
    // transform (USD row-vector convention). Used for per-mesh + scene framing.
    float wmin[3], wmax[3];
    {
      const tydn::Float3& lo = m.bbox_min;
      const tydn::Float3& hi = m.bbox_max;
      bool first = true;
      for (int corner = 0; corner < 8; ++corner) {
        float lp[3] = {(corner & 1) ? hi.x : lo.x,
                       (corner & 2) ? hi.y : lo.y,
                       (corner & 4) ? hi.z : lo.z};
        float wp[3];
        XformPointUSD(node.world_transform.m, lp, wp);
        for (int k = 0; k < 3; ++k) {
          if (first) { wmin[k] = wmax[k] = wp[k]; }
          else { wmin[k] = std::min(wmin[k], wp[k]); wmax[k] = std::max(wmax[k], wp[k]); }
        }
        first = false;
      }
    }
    for (int k = 0; k < 3; ++k) { dm.aabbMin[k] = wmin[k]; dm.aabbMax[k] = wmax[k]; }
    if (!hasBounds) {
      for (int k = 0; k < 3; ++k) { bmin[k] = wmin[k]; bmax[k] = wmax[k]; }
      hasBounds = true;
    } else {
      for (int k = 0; k < 3; ++k) {
        bmin[k] = std::min(bmin[k], wmin[k]);
        bmax[k] = std::max(bmax[k], wmax[k]);
      }
    }

    draw->triangleCount += dm.indices.size() / 3;
    draw->meshes.push_back(std::move(dm));

    if (ctrl) ctrl->meshesDone.store(static_cast<long long>(draw->meshes.size()));
    if (draw->triangleCount > triCap) {
      draw->truncated = true;
      LOGE("next: triangle budget (%zu) hit; scene truncated to %zu meshes",
           triCap, draw->meshes.size());
      break;
    }
  }

  if (hasBounds) {
    for (int k = 0; k < 3; ++k) { draw->aabbMin[k] = bmin[k]; draw->aabbMax[k] = bmax[k]; }
    draw->hasBounds = true;
  }

  LOGI("next: loaded '%s' -> %zu meshes, %zu triangles%s", path.c_str(),
       draw->meshes.size(), draw->triangleCount,
       draw->truncated ? " (truncated)" : "");

  if (draw->meshes.empty()) {
    if (err) *err = "next: no renderable mesh produced";
    return false;
  }
  return true;
}

}  // namespace tusdview
