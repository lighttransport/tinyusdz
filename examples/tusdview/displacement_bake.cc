// SPDX-License-Identifier: Apache-2.0
#include "displacement_bake.hh"

#include <cmath>

namespace tusdview {

namespace {

// Apply a wrap mode to a (possibly out-of-range) integer texel coordinate.
inline int WrapCoord(int x, int n, int mode) {
  if (n <= 1) return 0;
  switch (mode) {
    case static_cast<int>(WrapMode::Repeat): {
      int r = x % n;
      return r < 0 ? r + n : r;
    }
    case static_cast<int>(WrapMode::Mirror): {
      int period = 2 * n;
      int r = x % period;
      if (r < 0) r += period;
      return r < n ? r : (period - 1 - r);
    }
    case static_cast<int>(WrapMode::ClampToEdge):
    case static_cast<int>(WrapMode::ClampToBorder):
    default:
      return x < 0 ? 0 : (x >= n ? n - 1 : x);
  }
}

}  // namespace

float SampleTextureRed(const DrawScene& scene, int texIndex, float u, float v) {
  if (texIndex < 0 || static_cast<size_t>(texIndex) >= scene.textures.size()) {
    return 0.0f;
  }
  const DrawTextureCPU& tex = scene.textures[static_cast<size_t>(texIndex)];
  const light3d::Image& img = tex.image;
  const int W = img.width, H = img.height, C = img.channels;
  if (W <= 0 || H <= 0 || C <= 0 || img.data.empty()) return 0.0f;
  // Bilinear with half-texel centers (matches GL/VK linear filtering).
  const float fx = u * static_cast<float>(W) - 0.5f;
  const float fy = v * static_cast<float>(H) - 0.5f;
  const int x0 = static_cast<int>(std::floor(fx));
  const int y0 = static_cast<int>(std::floor(fy));
  const float tx = fx - static_cast<float>(x0);
  const float ty = fy - static_cast<float>(y0);
  auto red = [&](int x, int y) -> float {
    const int xx = WrapCoord(x, W, tex.wrapS);
    const int yy = WrapCoord(y, H, tex.wrapT);
    return static_cast<float>(img.data[(static_cast<size_t>(yy) * W + xx) * C]) /
           255.0f;
  };
  const float c00 = red(x0, y0), c10 = red(x0 + 1, y0);
  const float c01 = red(x0, y0 + 1), c11 = red(x0 + 1, y0 + 1);
  const float a = c00 + (c10 - c00) * tx;
  const float b = c01 + (c11 - c01) * tx;
  return a + (b - a) * ty;
}

bool MeshHasDisplacement(const DrawScene& scene, const DrawMeshCPU& mesh) {
  for (const DrawSubmesh& s : mesh.submeshes) {
    if (s.materialId >= 0 &&
        static_cast<size_t>(s.materialId) < scene.materials.size() &&
        scene.materials[static_cast<size_t>(s.materialId)].hasDisplacement()) {
      return true;
    }
  }
  return false;
}

bool BakeDisplacedVertices(const DrawScene& scene, const DrawMeshCPU& mesh,
                           float globalScale, std::vector<DrawVertex>* out) {
  if (!out) return false;
  out->clear();
  if (globalScale == 0.0f || mesh.vertices.empty() || mesh.indices.empty() ||
      !MeshHasDisplacement(scene, mesh)) {
    return false;
  }
  const size_t nv = mesh.vertices.size();
  // Per-vertex material (first referencing triangle wins). -1 = none.
  std::vector<int> vtxMat(nv, -1);
  for (const DrawSubmesh& s : mesh.submeshes) {
    const uint32_t end = s.indexOffset + s.indexCount;
    for (uint32_t i = s.indexOffset; i < end && i < mesh.indices.size(); ++i) {
      const uint32_t vi = mesh.indices[i];
      if (vi < nv && vtxMat[vi] < 0) vtxMat[vi] = s.materialId;
    }
  }

  *out = mesh.vertices;  // start from the rest pose, displace in place
  std::vector<bool> displaced(nv, false);
  for (size_t v = 0; v < nv; ++v) {
    const int mid = vtxMat[v];
    if (mid < 0 || static_cast<size_t>(mid) >= scene.materials.size()) continue;
    const DrawMaterialCPU& mat = scene.materials[static_cast<size_t>(mid)];
    if (!mat.hasDisplacement()) continue;
    const DrawVertex& src = mesh.vertices[v];
    float h = mat.displacementConst;
    if (mat.displacementTex >= 0) {
      h = SampleTextureRed(scene, mat.displacementTex, src.u, src.v) *
              mat.displacementTexScale +
          mat.displacementTexBias;
    }
    h *= globalScale;
    // Offset along the normalized (original) vertex normal -> watertight (a shared
    // vertex is displaced once, deterministically).
    float nx = src.nx, ny = src.ny, nz = src.nz;
    const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (nl > 1e-12f) {
      nx /= nl; ny /= nl; nz /= nl;
      (*out)[v].px = src.px + nx * h;
      (*out)[v].py = src.py + ny * h;
      (*out)[v].pz = src.pz + nz * h;
      displaced[v] = true;
    }
  }

  // Recompute normals on the deformed surface (area-weighted face normals), writing
  // back only to displaced vertices so untouched submeshes keep authored normals.
  std::vector<float> acc(nv * 3, 0.0f);
  for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
    const uint32_t a = mesh.indices[t], b = mesh.indices[t + 1],
                   c = mesh.indices[t + 2];
    if (a >= nv || b >= nv || c >= nv) continue;
    const DrawVertex& pa = (*out)[a];
    const DrawVertex& pb = (*out)[b];
    const DrawVertex& pc = (*out)[c];
    const float e1[3] = {pb.px - pa.px, pb.py - pa.py, pb.pz - pa.pz};
    const float e2[3] = {pc.px - pa.px, pc.py - pa.py, pc.pz - pa.pz};
    const float fn[3] = {e1[1] * e2[2] - e1[2] * e2[1],
                         e1[2] * e2[0] - e1[0] * e2[2],
                         e1[0] * e2[1] - e1[1] * e2[0]};  // unnormalized = area*2
    for (uint32_t vi : {a, b, c}) {
      acc[vi * 3 + 0] += fn[0];
      acc[vi * 3 + 1] += fn[1];
      acc[vi * 3 + 2] += fn[2];
    }
  }
  for (size_t v = 0; v < nv; ++v) {
    if (!displaced[v]) continue;
    float nx = acc[v * 3 + 0], ny = acc[v * 3 + 1], nz = acc[v * 3 + 2];
    const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (nl > 1e-12f) {
      (*out)[v].nx = nx / nl;
      (*out)[v].ny = ny / nl;
      (*out)[v].nz = nz / nl;
    }
  }
  return true;
}

void BakeRTDisplacement(DrawScene* scene) {
  if (!scene) return;
  for (DrawMeshCPU& mesh : scene->meshes) {
    BakeDisplacedVertices(*scene, mesh, /*globalScale=*/1.0f,
                          &mesh.rtDisplacedVertices);
  }
}

}  // namespace tusdview
