// SPDX-License-Identifier: Apache-2.0
#include "cpu_raytracer.hh"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>

#include "lightrt_c_tri.h"
#include "lightrt_mtlx_bridge.hh"  // kRtMaterialTexSlots

namespace tusdview {

namespace {
inline void TransformPoint(const float m[12], const float p[3], float out[3]);
inline void TransformNormal(const float m[12], const float p[3], float out[3]);
}

CpuRayTracer::~CpuRayTracer() { freeScene(); }

void CpuRayTracer::freeScene() {
  if (scene_) {
    lrt_tri_scene_free(scene_);
    scene_ = nullptr;
  }
  cpuTris_.clear();
  cpuNrms_.clear();
  cpuUv_.clear();
  cpuUv1_.clear();
  cpuMat_.clear();
  cpuInstance_.clear();
}

bool CpuRayTracer::build(const DrawScene& scene, size_t maxTris, size_t maxInstances,
                         std::string* err, float displacementScale,
                         BuildProgress* progress) {
  freeScene();
  hs_ = HostScene();  // drop the previous flatten before building the next one
  if (!BuildHostScene(scene, maxTris, maxInstances, displacementScale, &hs_, err,
                      progress, /*refitOut=*/nullptr, textureBudgetBytes_)) {
    return false;
  }
  truncated_ = hs_.truncated;
  if (hs_.triCount == 0 || hs_.instances.empty()) {
    if (err) *err = "no triangles";
    return false;
  }

  // lightrt_c's public triangle builder accepts one world-space triangle
  // stream, while HostScene deliberately stores each prototype once plus a
  // shared BLAS/TLAS. Expand the instances here so primary and shadow rays
  // see the same transforms as the GPU paths. Traverse each instance's BLAS
  // rather than assuming a mesh's triangles occupy one contiguous range: the
  // assembled BLAS is global and leaf order is part of its contract.
  size_t expandedCount = 0;
  for (const Inst& inst : hs_.instances) {
    if (inst.blasRoot < 0 || static_cast<size_t>(inst.blasRoot) >= hs_.blas.size())
      continue;
    std::vector<int> stack(1, inst.blasRoot);
    while (!stack.empty()) {
      const int nodeIndex = stack.back();
      stack.pop_back();
      if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= hs_.blas.size()) continue;
      const Node& node = hs_.blas[static_cast<size_t>(nodeIndex)];
      if (node.count > 0) {
        if (node.left >= 0 && node.count <= static_cast<int>(hs_.triCount) - node.left)
          expandedCount += static_cast<size_t>(node.count);
      } else {
        stack.push_back(node.left);
        stack.push_back(node.right);
      }
    }
  }
  if (expandedCount == 0) {
    if (err) *err = "no instance triangles";
    return false;
  }
  cpuTris_.reserve(expandedCount * 9);
  cpuNrms_.reserve(expandedCount * 9);
  cpuUv_.reserve(expandedCount * 6);
  cpuUv1_.reserve(expandedCount * 6);
  cpuMat_.reserve(expandedCount);
  cpuInstance_.reserve(expandedCount);

  for (size_t instanceIndex = 0; instanceIndex < hs_.instances.size(); ++instanceIndex) {
    const Inst& inst = hs_.instances[instanceIndex];
    if (inst.blasRoot < 0 || static_cast<size_t>(inst.blasRoot) >= hs_.blas.size()) continue;
    std::vector<int> stack(1, inst.blasRoot);
    while (!stack.empty()) {
      const int nodeIndex = stack.back();
      stack.pop_back();
      if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= hs_.blas.size()) continue;
      const Node& node = hs_.blas[static_cast<size_t>(nodeIndex)];
      if (node.count <= 0) {
        stack.push_back(node.left);
        stack.push_back(node.right);
        continue;
      }
      if (node.left < 0 || node.count > static_cast<int>(hs_.triCount) - node.left) continue;
      for (int t = 0; t < node.count; ++t) {
        const size_t src = static_cast<size_t>(node.left + t);
        const float* srcTri = &hs_.tris[src * 9];
        for (int v = 0; v < 3; ++v) {
          float p[3];
          TransformPoint(inst.o2w, srcTri + v * 3, p);
          cpuTris_.insert(cpuTris_.end(), p, p + 3);
        }
        if (src * 9 + 9 <= hs_.nrms.size()) {
          for (int v = 0; v < 3; ++v) {
            float n[3];
            TransformNormal(inst.w2o, &hs_.nrms[src * 9 + v * 3], n);
            cpuNrms_.insert(cpuNrms_.end(), n, n + 3);
          }
        } else {
          cpuNrms_.insert(cpuNrms_.end(), 9, 0.0f);
        }
        if (src * 6 + 6 <= hs_.uv.size())
          cpuUv_.insert(cpuUv_.end(), hs_.uv.begin() + static_cast<ptrdiff_t>(src * 6),
                        hs_.uv.begin() + static_cast<ptrdiff_t>(src * 6 + 6));
        else
          cpuUv_.insert(cpuUv_.end(), 6, 0.0f);
        if (src * 6 + 6 <= hs_.uv1.size())
          cpuUv1_.insert(cpuUv1_.end(), hs_.uv1.begin() + static_cast<ptrdiff_t>(src * 6),
                         hs_.uv1.begin() + static_cast<ptrdiff_t>(src * 6 + 6));
        else
          cpuUv1_.insert(cpuUv1_.end(), 6, 0.0f);
        cpuMat_.push_back(src < hs_.mat.size() ? hs_.mat[src] : -1);
        cpuInstance_.push_back(static_cast<int>(instanceIndex));
      }
    }
  }
  // Keep the public count consistent with CUDA/HIP: it reports the unique
  // prototype stream accepted by --max-tris, not the private expanded copy.
  triCount_ = hs_.triCount;
  const size_t expandedTriCount = cpuTris_.size() / 9;

  lrt_tri_build_options opts{};
  opts.quality = LRT_TRI_BUILD_FAST;  // interactive-priority, not final-quality
  opts.layout = LRT_TRI_LAYOUT_AUTO;
  opts.max_leaf_size = 0;  // default
  opts.num_threads = static_cast<unsigned>(
      std::max(1u, std::thread::hardware_concurrency()));

  lrt_result lrtErr;
  scene_ = lrt_tri_scene_build(cpuTris_.data(), expandedTriCount, &opts, &lrtErr);
  if (!scene_) {
    if (err) *err = "lrt_tri_scene_build failed";
    return false;
  }
  return true;
}

namespace {

// Barycentric-interpolate one of HostScene's 9-floats/tri vertex arrays
// (tris, nrms, cols) at a hit's (u, v); Moller-Trumbore convention: hit point
// = (1-u-v)*v0 + u*v1 + v*v2.
inline void Interp9(const float* base9, float u, float v, float out[3]) {
  const float w = 1.0f - u - v;
  for (int c = 0; c < 3; ++c) {
    out[c] = w * base9[c] + u * base9[3 + c] + v * base9[6 + c];
  }
}

// Same convention as Interp9 but for HostScene::uv/uv1 (2 floats/vertex).
inline void Interp6(const float* base6, float u, float v, float out[2]) {
  const float w = 1.0f - u - v;
  for (int c = 0; c < 2; ++c) {
    out[c] = w * base6[c] + u * base6[2 + c] + v * base6[4 + c];
  }
}

inline void Normalize3(float v[3]) {
  const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (len > 1.0e-8f) {
    v[0] /= len; v[1] /= len; v[2] /= len;
  }
}

inline float PackedIesFactor(const float* lp, const float lightToPoint[3]) {
  if (!lp || std::fabs(lp[52] - 2.0f) > 0.25f) return 1.0f;
  float axis[3] = {lp[8], lp[9], lp[10]};
  Normalize3(axis);
  float d[3] = {lightToPoint[0], lightToPoint[1], lightToPoint[2]};
  Normalize3(d);
  const float c = std::clamp(axis[0] * d[0] + axis[1] * d[1] + axis[2] * d[2],
                             -1.0f, 1.0f);
  const float fy = std::clamp(std::acos(c) * 57.2957795131f / 60.0f,
                              0.0f, 3.0f);
  const int y0 = static_cast<int>(std::floor(fy));
  const int y1 = std::min(y0 + 1, 3);
  const float t = fy - static_cast<float>(y0);
  float axisX[3] = {lp[40], lp[41], lp[42]};
  float axisY[3] = {lp[44], lp[45], lp[46]};
  Normalize3(axisX); Normalize3(axisY);
  float az = std::atan2(d[0] * axisY[0] + d[1] * axisY[1] + d[2] * axisY[2],
                        d[0] * axisX[0] + d[1] * axisX[1] + d[2] * axisX[2]) *
             57.2957795131f;
  if (az < 0.0f) az += 360.0f;
  const float fx = az / 60.0f;
  const int x0 = static_cast<int>(std::floor(fx)) % 6;
  const int x1 = (x0 + 1) % 6;
  const float tx = fx - std::floor(fx);
  const float a0 = lp[56 + y0 * 6 + x0] +
                   (lp[56 + y0 * 6 + x1] - lp[56 + y0 * 6 + x0]) * tx;
  const float a1 = lp[56 + y1 * 6 + x0] +
                   (lp[56 + y1 * 6 + x1] - lp[56 + y1 * 6 + x0]) * tx;
  return a0 + (a1 - a0) * t;
}

inline void TransformPoint(const float m[12], const float p[3], float out[3]) {
  out[0] = m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
  out[1] = m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7];
  out[2] = m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11];
}

inline void TransformNormal(const float m[12], const float p[3], float out[3]) {
  out[0] = m[0] * p[0] + m[4] * p[1] + m[8] * p[2];
  out[1] = m[1] * p[0] + m[5] * p[1] + m[9] * p[2];
  out[2] = m[2] * p[0] + m[6] * p[1] + m[10] * p[2];
  Normalize3(out);
}

// Bilinear-sample an RGBA8 HostTextureDesc at
// its base mip. Deliberately mirrors sampleTexLevel/wrapCoord in
// raytracer_kernel_src.txt (the CUDA/HIP kernel) so CPU RT matches the GPU
// tracers: same wrap-mode encoding (0 and 3 = clamp, 2 = mirror, everything
// else = repeat), same [0,1] -> [0, size-1] mapping with clamp-to-edge on the
// upper neighbour, and NO V flip -- HostScene::uv already carries the
// convention the kernel samples with. No sRGB decode: texels are used as-is,
// consistent with this tracer's un-color-managed flat shading.
inline void SampleTextureBilinear(const std::vector<HostTextureDesc>& textures,
                                  const std::vector<uint8_t>& texels, int texId,
                                  float u, float v, float out[3]) {
  if (texId < 0 || static_cast<size_t>(texId) >= textures.size()) {
    out[0] = out[1] = out[2] = 1.0f;
    return;
  }
  const HostTextureDesc* desc = &textures[static_cast<size_t>(texId)];
  if (desc->isUdim) {
    const int tileU = static_cast<int>(std::floor(u));
    const int tileV = static_cast<int>(std::floor(v));
    const int lut = tileU + tileV * 10;
    if (lut < 0 || lut >= 100 || desc->udimLayer[lut] < 0 ||
        static_cast<size_t>(desc->udimLayer[lut]) >= textures.size()) {
      out[0] = out[1] = out[2] = 1.0f;
      return;
    }
    texId = desc->udimLayer[lut];
    u -= static_cast<float>(tileU);
    v -= static_cast<float>(tileV);
    desc = &textures[static_cast<size_t>(texId)];
  }
  if (desc->isPtex || desc->width <= 0 || desc->height <= 0) {
    out[0] = out[1] = out[2] = 1.0f;
    return;
  }
  const HostTextureDesc& d = *desc;
  auto wrapCoord = [](float x, int mode) -> float {
    if (mode == 0 || mode == 3) return std::clamp(x, 0.0f, 1.0f);  // clamp
    if (mode == 2) {                                               // mirror
      const float t = std::floor(x);
      const float f = x - t;
      return (static_cast<int>(t) & 1) ? (1.0f - f) : f;
    }
    return x - std::floor(x);  // repeat
  };
  const float su = wrapCoord(u, d.wrapS) * static_cast<float>(d.width - 1);
  const float sv = wrapCoord(v, d.wrapT) * static_cast<float>(d.height - 1);
  const int x0 = static_cast<int>(std::floor(su));
  const int y0 = static_cast<int>(std::floor(sv));
  const int x1 = (x0 + 1 < d.width) ? (x0 + 1) : (d.width - 1);
  const int y1 = (y0 + 1 < d.height) ? (y0 + 1) : (d.height - 1);
  const float tx = su - static_cast<float>(x0);
  const float ty = sv - static_cast<float>(y0);
  auto texel = [&](int x, int y, float rgb[3]) {
    x = std::clamp(x, 0, d.width - 1);
    y = std::clamp(y, 0, d.height - 1);
    const size_t idx = static_cast<size_t>(d.offset) +
                       (static_cast<size_t>(y) * d.width + x) * 4;
    if (idx + 2 >= texels.size()) { rgb[0] = rgb[1] = rgb[2] = 1.0f; return; }
    rgb[0] = texels[idx + 0] / 255.0f;
    rgb[1] = texels[idx + 1] / 255.0f;
    rgb[2] = texels[idx + 2] / 255.0f;
  };
  float c00[3], c10[3], c01[3], c11[3];
  texel(x0, y0, c00); texel(x1, y0, c10);
  texel(x0, y1, c01); texel(x1, y1, c11);
  for (int c = 0; c < 3; ++c) {
    const float top = c00[c] * (1 - tx) + c10[c] * tx;
    const float bot = c01[c] * (1 - tx) + c11[c] * tx;
    out[c] = top * (1 - ty) + bot * ty;
  }
}

inline uint32_t HashColor(uint32_t id) {
  uint32_t x = id * 2654435761u + 1u;
  x ^= x >> 16; x *= 0x85ebca6bu; x ^= x >> 13; x *= 0xc2b2ae35u; x ^= x >> 16;
  return x;
}

// Scalar material lanes (opacity, roughness, masks) need the same UDIM
// resolution and bilinear filtering as RGB graph/image lanes.
inline float SampleTextureBilinearChannel(
    const std::vector<HostTextureDesc>& textures,
    const std::vector<uint8_t>& texels, int texId, float u, float v,
    int channel) {
  if (texId < 0 || static_cast<size_t>(texId) >= textures.size()) return 1.0f;
  const HostTextureDesc* desc = &textures[static_cast<size_t>(texId)];
  if (desc->isUdim) {
    const int tileU = static_cast<int>(std::floor(u));
    const int tileV = static_cast<int>(std::floor(v));
    const int lut = tileU + tileV * 10;
    if (lut < 0 || lut >= 100 || desc->udimLayer[lut] < 0 ||
        static_cast<size_t>(desc->udimLayer[lut]) >= textures.size()) return 1.0f;
    texId = desc->udimLayer[lut];
    u -= static_cast<float>(tileU);
    v -= static_cast<float>(tileV);
    desc = &textures[static_cast<size_t>(texId)];
  }
  if (desc->isPtex || desc->width <= 0 || desc->height <= 0) return 1.0f;
  const HostTextureDesc& d = *desc;
  channel = std::clamp(channel, 0, 3);
  auto wrapCoord = [](float x, int mode) -> float {
    if (mode == 0 || mode == 3) return std::clamp(x, 0.0f, 1.0f);
    if (mode == 2) {
      const float t = std::floor(x);
      const float f = x - t;
      return (static_cast<int>(t) & 1) ? (1.0f - f) : f;
    }
    return x - std::floor(x);
  };
  const float su = wrapCoord(u, d.wrapS) * static_cast<float>(d.width - 1);
  const float sv = wrapCoord(v, d.wrapT) * static_cast<float>(d.height - 1);
  const int x0 = static_cast<int>(std::floor(su));
  const int y0 = static_cast<int>(std::floor(sv));
  const int x1 = (x0 + 1 < d.width) ? x0 + 1 : d.width - 1;
  const int y1 = (y0 + 1 < d.height) ? y0 + 1 : d.height - 1;
  const float tx = su - static_cast<float>(x0);
  const float ty = sv - static_cast<float>(y0);
  auto texel = [&](int x, int y) {
    x = std::clamp(x, 0, d.width - 1);
    y = std::clamp(y, 0, d.height - 1);
    const size_t idx = static_cast<size_t>(d.offset) +
                       (static_cast<size_t>(y) * d.width + x) * 4u +
                       static_cast<size_t>(channel);
    return idx < texels.size() ? texels[idx] / 255.0f : 1.0f;
  };
  const float top = texel(x0, y0) * (1.0f - tx) + texel(x1, y0) * tx;
  const float bot = texel(x0, y1) * (1.0f - tx) + texel(x1, y1) * tx;
  return top * (1.0f - ty) + bot * ty;
}

std::array<float, 4> EvalCpuMaterialXGraph(
    const HostScene& scene, int materialId, int route, float u, float v,
    float u1, float v1) {
  constexpr int kStride = kRtMaterialGraphFloats;
  std::array<float, 4> zero{0.0f, 0.0f, 0.0f, 0.0f};
  const size_t base = static_cast<size_t>(materialId) * kStride;
  if (materialId < 0 || route < 0 ||
      base + kRtMaterialGraphHeaderFloats > scene.matGraph.size()) return zero;
  const int count = std::clamp(static_cast<int>(scene.matGraph[base]), 0,
                               kRtMaterialGraphMaxNodes);
  const int wanted = static_cast<int>(std::floor(
      scene.matGraph[base + 1 + route] + 0.5f));
  if (count <= 0 || wanted < 0 || wanted >= count) return zero;
  std::array<std::array<float, 4>, kRtMaterialGraphMaxNodes> values{};
  // CompileMaterialXGraphRuntime canonicalizes dependencies before consumers
  // and rejects cycles, so one pass is sufficient. The historical 64-pass
  // fixed-point loop multiplied graph shading cost without changing results.
  for (int i = 0; i < count; ++i) {
      const size_t p = base + kRtMaterialGraphHeaderFloats +
                       static_cast<size_t>(i) * kRtMaterialGraphNodeFloats;
      const int op = static_cast<int>(scene.matGraph[p] + 0.5f);
      const int ia = static_cast<int>(std::floor(scene.matGraph[p + 1] + 0.5f));
      const int ib = static_cast<int>(std::floor(scene.matGraph[p + 2] + 0.5f));
      const int ic = static_cast<int>(std::floor(scene.matGraph[p + 3] + 0.5f));
      const auto fallback = [&](int input) {
        return std::array<float, 4>{
            scene.matGraph[p + 4 + input * 4],
            scene.matGraph[p + 5 + input * 4],
            scene.matGraph[p + 6 + input * 4],
            scene.matGraph[p + 7 + input * 4]};
      };
      const auto a = ia >= 0 && ia < count ? values[ia] : fallback(0);
      const auto b = ib >= 0 && ib < count ? values[ib] : fallback(1);
      const auto c = ic >= 0 && ic < count ? values[ic] : fallback(2);
      const int textureId = static_cast<int>(std::floor(
          scene.matGraph[p + 16] + 0.5f));
      auto& dst = values[i];
      if (op == static_cast<int>(MaterialXGraphOpCPU::Constant)) dst = fallback(0);
      else if (op == static_cast<int>(MaterialXGraphOpCPU::Image) ||
               op == static_cast<int>(MaterialXGraphOpCPU::TiledImage)) {
        float rgb[3];
        float gu = u, gv = v;
        const int uvInput = static_cast<int>(std::floor(
            scene.matGraph[p + 15] + 0.5f));
        if (uvInput == 0) { gu = a[0]; gv = a[1]; }
        else if (uvInput == 1) { gu = b[0]; gv = b[1]; }
        else if (uvInput == 2) { gu = c[0]; gv = c[1]; }
        const float su = gu * scene.matGraph[p + 17] + scene.matGraph[p + 19];
        const float sv = gv * scene.matGraph[p + 18] + scene.matGraph[p + 20];
        SampleTextureBilinear(scene.textures, scene.texels, textureId, su, sv,
                              rgb);
        dst = {rgb[0], rgb[1], rgb[2], 1.0f};
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::NormalMap)) {
        float nx = a[0] * 2.0f - 1.0f, ny = a[1] * 2.0f - 1.0f,
              nz = a[2] * 2.0f - 1.0f;
        const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
        dst = length > 1.0e-6f ? std::array<float, 4>{nx / length * 0.5f + 0.5f,
                                                       ny / length * 0.5f + 0.5f,
                                                       nz / length * 0.5f + 0.5f,
                                                       a[3]} : a;
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Add)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = a[cidx] + b[cidx];
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Subtract)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = a[cidx] - b[cidx];
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Multiply)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = a[cidx] * b[cidx];
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Divide)) {
        for (int cidx = 0; cidx < 4; ++cidx)
          dst[cidx] = a[cidx] / std::max(std::fabs(b[cidx]), 1.0e-6f);
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Mix)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = a[cidx] + (b[cidx] - a[cidx]) * c[cidx];
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Clamp)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = std::clamp(a[cidx], b[cidx], c[cidx]);
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Dot)) {
        const float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        dst = {d, d, d, d};
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Normalized)) {
        const float length = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        dst = length > 1.0e-6f ? std::array<float, 4>{a[0] / length, a[1] / length,
                                                       a[2] / length, a[3]} : a;
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Power)) {
        for (int cidx = 0; cidx < 4; ++cidx)
          dst[cidx] = std::pow(std::max(a[cidx], 0.0f), b[cidx]);
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Minimum)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = std::min(a[cidx], b[cidx]);
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Maximum)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = std::max(a[cidx], b[cidx]);
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Absolute)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = std::fabs(a[cidx]);
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::SquareRoot)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = std::sqrt(std::max(a[cidx], 0.0f));
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Sine)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = std::sin(a[cidx]);
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Cosine)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = std::cos(a[cidx]);
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Luminance)) {
        const float l = a[0] * 0.2126f + a[1] * 0.7152f + a[2] * 0.0722f;
        dst = {l, l, l, a[3]};
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Select)) {
        dst = (a[0] >= 0.5f) ? b : c;
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Texcoord)) {
        const bool secondSet = c[2] > 0.5f;
        dst = {secondSet ? u1 : u, secondSet ? v1 : v, 0.0f, 1.0f};
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Floor)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = std::floor(a[cidx]);
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Ceil)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = std::ceil(a[cidx]);
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Fract)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = a[cidx] - std::floor(a[cidx]);
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Step)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = a[cidx] < b[cidx] ? 0.0f : 1.0f;
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Smoothstep)) {
        for (int cidx = 0; cidx < 4; ++cidx) {
          const float t = std::clamp((a[cidx] - b[cidx]) /
                                         std::max(c[cidx] - b[cidx], 1.0e-6f),
                                     0.0f, 1.0f);
          dst[cidx] = t * t * (3.0f - 2.0f * t);
        }
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Cross)) {
        dst = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
               a[0] * b[1] - a[1] * b[0], a[3]};
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Length)) {
        const float l = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        dst = {l, l, l, l};
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Noise2D)) {
        const float n = std::sin((a[0] + u * 17.0f) * 127.1f +
                                 (a[1] + v * 31.0f) * 311.7f) * 43758.5453f;
        const float f = n - std::floor(n);
        dst = {f, f, f, f};
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Noise3D)) {
        const float n = std::sin((a[0] + u * 17.0f) * 127.1f +
                                 (a[1] + v * 31.0f) * 311.7f +
                                 a[2] * 74.7f) * 43758.5453f;
        const float f = n - std::floor(n);
        dst = {f, f, f, f};
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Tangent)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = std::tan(a[cidx]);
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Exponential)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = std::exp(a[cidx]);
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Logarithm)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = std::log(std::max(a[cidx], 1.0e-6f));
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Modulo)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = std::fmod(a[cidx], std::max(std::fabs(b[cidx]), 1.0e-6f));
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Atan2)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = std::atan2(a[cidx], b[cidx]);
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Sign)) {
        for (int cidx = 0; cidx < 4; ++cidx)
          dst[cidx] = a[cidx] > 0.0f ? 1.0f : (a[cidx] < 0.0f ? -1.0f : 0.0f);
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Round)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = std::round(a[cidx]);
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Invert)) {
        for (int cidx = 0; cidx < 4; ++cidx) dst[cidx] = 1.0f - a[cidx];
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Remap)) {
        for (int cidx = 0; cidx < 4; ++cidx) {
          const float t = (a[cidx] - b[cidx]) / std::max(c[cidx] - b[cidx], 1.0e-6f);
          dst[cidx] = t;
        }
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::GeometricNormal)) {
        dst = {0.0f, 0.0f, 1.0f, 1.0f};
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::GeometricTangent)) {
        dst = {1.0f, 0.0f, 0.0f, 1.0f};
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Rotate3D)) {
        const float al = a[0] * 0.0174532925199433f;
        const float alen = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
        if (alen > 1.0e-6f) {
          const float ax = b[0] / alen, ay = b[1] / alen, az = b[2] / alen;
          const float cs = std::cos(al), sn = std::sin(al);
          const float d = ax * c[0] + ay * c[1] + az * c[2];
          dst = {c[0] * cs + (ay * c[2] - az * c[1]) * sn + ax * d * (1.0f - cs),
                 c[1] * cs + (az * c[0] - ax * c[2]) * sn + ay * d * (1.0f - cs),
                 c[2] * cs + (ax * c[1] - ay * c[0]) * sn + az * d * (1.0f - cs), c[3]};
        } else dst = c;
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Transform2D)) {
        float tx = a[0] * scene.matGraph[p + 17] + scene.matGraph[p + 19];
        float ty = a[1] * scene.matGraph[p + 18] + scene.matGraph[p + 20];
        const float angle = scene.matGraph[p + 15] * 0.0174532925199433f;
        const float cs = std::cos(angle), sn = std::sin(angle);
        dst = {tx * cs - ty * sn, tx * sn + ty * cs, a[2], a[3]};
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Combine)) {
        dst = {a[0], b[0], c[0], 1.0f};
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Extract)) {
        const int lane = std::clamp(static_cast<int>(std::floor(b[0] + 0.5f)), 0, 3);
        dst = {a[lane], a[lane], a[lane], a[lane]};
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Convert)) {
        dst = a;
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Position)) {
        dst = {0.0f, 0.0f, 0.0f, 1.0f};
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::HsvAdjust)) {
        const float mx = std::max(a[0], std::max(a[1], a[2]));
        const float mn = std::min(a[0], std::min(a[1], a[2]));
        const float chroma = mx - mn;
        float hue = 0.0f;
        if (chroma > 1.0e-6f) {
          if (mx == a[0]) hue = std::fmod((a[1] - a[2]) / chroma, 6.0f);
          else if (mx == a[1]) hue = (a[2] - a[0]) / chroma + 2.0f;
          else hue = (a[0] - a[1]) / chroma + 4.0f;
          hue /= 6.0f;
        }
        hue = hue + b[0] - std::floor(hue + b[0]);
        const float sat = mx > 1.0e-6f ? chroma / mx : 0.0f;
        const float s2 = std::clamp(sat * b[1], 0.0f, 1.0f);
        const float v2 = std::max(mx * b[2], 0.0f);
        const float hp = hue * 6.0f, x = v2 * s2 *
            (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
        const float cc = v2 * s2, m = v2 - cc;
        const int sector = static_cast<int>(std::floor(hp)) % 6;
        const float rgb[6][3] = {{cc,x,0},{x,cc,0},{0,cc,x},
                                 {0,x,cc},{x,0,cc},{cc,0,x}};
        dst = {rgb[sector][0] + m, rgb[sector][1] + m,
               rgb[sector][2] + m, a[3]};
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::HeightToNormal)) {
        dst = {0.5f, 0.5f, 1.0f, 1.0f};
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Arcsine)) {
        for (int cidx = 0; cidx < 4; ++cidx)
          dst[cidx] = std::asin(std::clamp(a[cidx], -1.0f, 1.0f));
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Arccosine)) {
        for (int cidx = 0; cidx < 4; ++cidx)
          dst[cidx] = std::acos(std::clamp(a[cidx], -1.0f, 1.0f));
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Contrast)) {
        for (int cidx = 0; cidx < 4; ++cidx)
          dst[cidx] = (a[cidx] - c[cidx]) * b[cidx] + c[cidx];
      } else if (op == static_cast<int>(MaterialXGraphOpCPU::Swizzle)) {
        for (int cidx = 0; cidx < 4; ++cidx) {
          const int selector = std::clamp(
              static_cast<int>(std::floor(b[cidx] + 0.5f)), 0, 5);
          dst[cidx] = selector < 4 ? a[selector]
                                   : (selector == 5 ? 1.0f : 0.0f);
        }
      } else dst = fallback(0);
  }
  return values[wanted];
}

}  // namespace

bool CpuRayTracer::trace(const float invViewProj[16], const float /*viewProj*/[16],
                         const float camPos[3], const float lightDir[3],
                         const float clearColor[3], float exposure, int renderMode,
                         float depthScale, const float /*sceneMin*/[3],
                         const float /*sceneExtent*/[3], int w, int h,
                         std::vector<uint8_t>* rgba, std::string* err,
                         int spp, const RtCameraLens* lens) {
  const int sampleCount = std::max(1, lens && lens->enabled() ? std::max(spp, 8) : spp);
  if (sampleCount == 1) {
    return traceSingle(invViewProj, camPos, lightDir, clearColor, exposure,
                       renderMode, depthScale, w, h, 0.0f, 0.0f, lens, rgba,
                       err);
  }
  std::vector<uint32_t> accum(static_cast<size_t>(w) * h * 4, 0);
  std::vector<uint8_t> sample;
  for (int i = 0; i < sampleCount; ++i) {
    const float x = std::fmod((static_cast<float>(i) + 0.5f) /
                                  static_cast<float>(sampleCount) + 0.37f,
                              1.0f) - 0.5f;
    uint32_t bits = static_cast<uint32_t>(i + 1);
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    const float y = std::fmod(static_cast<float>(bits) * 2.3283064365386963e-10f +
                                  0.11f, 1.0f) - 0.5f;
    if (!traceSingle(invViewProj, camPos, lightDir, clearColor, exposure,
                     renderMode, depthScale, w, h, x, y, lens, &sample, err))
      return false;
    for (size_t p = 0; p < accum.size(); ++p)
      accum[p] += sample[p];
  }
  rgba->resize(accum.size());
  for (size_t p = 0; p < accum.size(); ++p)
    (*rgba)[p] = static_cast<uint8_t>((accum[p] +
                                       static_cast<uint32_t>(sampleCount / 2)) /
                                      static_cast<unsigned>(sampleCount));
  return true;
}

bool CpuRayTracer::traceSingle(const float invViewProj[16],
                               const float camPos[3], const float lightDir[3],
                               const float clearColor[3], float exposure,
                               int renderMode, float depthScale, int w, int h,
                               float sampleX, float sampleY,
                               const RtCameraLens* lens,
                               std::vector<uint8_t>* rgba, std::string* err) {
  if (!scene_ || w < 1 || h < 1) {
    if (err) *err = "no scene / invalid size";
    return false;
  }
  rgba->assign(static_cast<size_t>(w) * h * 4, 0);

  // Unproject a pixel's NDC coordinate through invViewProj: same formula as
  // Gui::pickMesh's CPU picking ray (gui.cc), so CPU RT's camera exactly
  // matches what the user clicks on. z=0 is the near plane (z01 depth, which
  // App always passes here -- camera_.proj(/*zeroToOneDepth=*/true)).
  auto unproject = [&](float ndcx, float ndcy, float nz, float out[3]) {
    const float* m = invViewProj;  // column-major: m[col*4 + row]
    const float ox = m[0] * ndcx + m[4] * ndcy + m[8] * nz + m[12];
    const float oy = m[1] * ndcx + m[5] * ndcy + m[9] * nz + m[13];
    const float oz = m[2] * ndcx + m[6] * ndcy + m[10] * nz + m[14];
    const float ow = m[3] * ndcx + m[7] * ndcy + m[11] * nz + m[15];
    const float inv = (ow != 0.0f) ? 1.0f / ow : 1.0f;
    out[0] = ox * inv; out[1] = oy * inv; out[2] = oz * inv;
  };

  float centerNear[3], centerFar[3];
  unproject(0.0f, 0.0f, 0.0f, centerNear);
  unproject(0.0f, 0.0f, 1.0f, centerFar);
  float cameraForward[3] = {centerFar[0] - centerNear[0],
                            centerFar[1] - centerNear[1],
                            centerFar[2] - centerNear[2]};
  Normalize3(cameraForward);

  const float lightLen = std::sqrt(lightDir[0] * lightDir[0] +
                                   lightDir[1] * lightDir[1] +
                                   lightDir[2] * lightDir[2]);
  float lightDirN[3] = {0, 1, 0};
  if (lightLen > 1e-8f) {
    lightDirN[0] = lightDir[0] / lightLen;
    lightDirN[1] = lightDir[1] / lightLen;
    lightDirN[2] = lightDir[2] / lightLen;
  }
  const float exposureMul = std::pow(2.0f, exposure);

  const unsigned nThreads = std::max(1u, std::thread::hardware_concurrency());
  std::atomic<int> nextRow{0};
  uint8_t* pixels = rgba->data();

  auto cpuLayerAlpha = [&](int tri, float bu, float bv) {
    if (tri < 0 || static_cast<size_t>(tri) >= cpuMat_.size()) return 1.0f;
    const int mid = cpuMat_[static_cast<size_t>(tri)];
    if (mid < 0 || static_cast<size_t>(mid) * 6 + 5 >= hs_.matPbr.size())
      return 1.0f;
    float alpha = hs_.matPbr[static_cast<size_t>(mid) * 6 + 5];
    float mode = 0.0f, cutoff = 0.5f;
    if (static_cast<size_t>(mid) * kLightRtOpenPBRFloats + 55 <
        hs_.matLightRt.size()) {
      const float* lrt = &hs_.matLightRt[static_cast<size_t>(mid) *
                                          kLightRtOpenPBRFloats];
      mode = lrt[54];
      cutoff = lrt[55];
      if (lrt[51] > 0.5f) alpha = lrt[39];
    }
    if (static_cast<size_t>(tri) * 12 + 11 < hs_.cols.size()) {
      const float w0 = 1.0f - bu - bv;
      alpha *= hs_.cols[static_cast<size_t>(tri) * 12 + 3] * w0 +
               hs_.cols[static_cast<size_t>(tri) * 12 + 7] * bu +
               hs_.cols[static_cast<size_t>(tri) * 12 + 11] * bv;
    }
    if (tri < static_cast<int>(cpuInstance_.size())) {
      const int instance = cpuInstance_[static_cast<size_t>(tri)];
      if (instance >= 0 && static_cast<size_t>(instance) < hs_.instances.size())
        alpha *= hs_.instances[static_cast<size_t>(instance)].tint[3];
    }
    if (mid >= 0 && !hs_.matTex.empty() &&
        static_cast<size_t>(mid) * kRtMaterialTexSlots + 5 < hs_.matTex.size() &&
        tri * 6 + 5 < static_cast<int>(cpuUv_.size())) {
      const int opacityTex = hs_.matTex[static_cast<size_t>(mid) *
                                        kRtMaterialTexSlots + 5];
      if (opacityTex >= 0) {
        float uv[2];
        Interp6(&cpuUv_[static_cast<size_t>(tri) * 6], bu, bv, uv);
        int channel = 0;
        float scale = 1.0f;
        float bias = 0.0f;
        const size_t paramBase = static_cast<size_t>(mid) *
                                 kRtMaterialTextureParamFloats;
        if (paramBase + 68 < hs_.matTexParam.size()) {
          const float* param = &hs_.matTexParam[paramBase];
          channel = static_cast<int>(param[66] + 0.5f);
          scale = param[67];
          bias = param[68];
        }
        alpha *= SampleTextureBilinearChannel(
            hs_.textures, hs_.texels, opacityTex, uv[0], uv[1], channel) *
                      scale + bias;
      }
    }
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (mode > 0.5f && mode < 1.5f) alpha = alpha >= cutoff ? 1.0f : 0.0f;
    return alpha;
  };

  auto cpuLayerColor = [&](int tri, float bu, float bv) {
    float color[3] = {0.6f, 0.6f, 0.6f};
    if (tri < 0 || static_cast<size_t>(tri) >= cpuMat_.size())
      return std::array<float, 3>{color[0], color[1], color[2]};
    const int mid = cpuMat_[static_cast<size_t>(tri)];
    if (mid >= 0 && static_cast<size_t>(mid) * 3 + 2 < hs_.matBase.size()) {
      color[0] = hs_.matBase[static_cast<size_t>(mid) * 3 + 0];
      color[1] = hs_.matBase[static_cast<size_t>(mid) * 3 + 1];
      color[2] = hs_.matBase[static_cast<size_t>(mid) * 3 + 2];
      if (static_cast<size_t>(tri) * 6 + 5 < cpuUv_.size()) {
        float uv[2];
        Interp6(&cpuUv_[static_cast<size_t>(tri) * 6], bu, bv, uv);
        float uv1[2] = {uv[0], uv[1]};
        if (static_cast<size_t>(tri) * 6 + 5 < cpuUv1_.size())
          Interp6(&cpuUv1_[static_cast<size_t>(tri) * 6], bu, bv, uv1);
        if (!hs_.matTex.empty() && static_cast<size_t>(mid) * kRtMaterialTexSlots <
                                      hs_.matTex.size()) {
          const int tex = hs_.matTex[static_cast<size_t>(mid) * kRtMaterialTexSlots];
          float sampled[3];
          SampleTextureBilinear(hs_.textures, hs_.texels, tex, uv[0], uv[1], sampled);
          color[0] *= sampled[0]; color[1] *= sampled[1]; color[2] *= sampled[2];
        }
        const size_t graphBase = static_cast<size_t>(mid) * kRtMaterialGraphFloats;
        if (graphBase + kRtMaterialGraphHeaderFloats <= hs_.matGraph.size() &&
            hs_.matGraph[graphBase + 1] >= 0.0f) {
          const auto graph = EvalCpuMaterialXGraph(hs_, mid, 0, uv[0], uv[1],
                                                   uv1[0], uv1[1]);
          color[0] = graph[0]; color[1] = graph[1]; color[2] = graph[2];
        }
      }
    }
    if (mid >= 0) {
      const size_t lrtBase = static_cast<size_t>(mid) * kLightRtOpenPBRFloats;
      if (lrtBase + 79 < hs_.matLightRt.size()) {
        const float volumeOpacity =
            std::clamp(1.0f - std::exp(-std::max(hs_.matLightRt[lrtBase + 75],
                                                  0.0f) * 0.1f),
                       0.0f, 1.0f);
        for (int c = 0; c < 3; ++c) {
          color[c] *= 1.0f +
                      (hs_.matLightRt[lrtBase + 72 + c] - 1.0f) *
                          volumeOpacity;
          color[c] += hs_.matLightRt[lrtBase + 76 + c] *
                      std::max(hs_.matLightRt[lrtBase + 79], 0.0f) *
                      volumeOpacity;
        }
      }
    }
    float normal[3] = {0.0f, 0.0f, 1.0f};
    if (static_cast<size_t>(tri) * 9 + 8 < cpuTris_.size()) {
      const float* tv = &cpuTris_[static_cast<size_t>(tri) * 9];
      const float e1[3] = {tv[3] - tv[0], tv[4] - tv[1], tv[5] - tv[2]};
      const float e2[3] = {tv[6] - tv[0], tv[7] - tv[1], tv[8] - tv[2]};
      normal[0] = e1[1] * e2[2] - e1[2] * e2[1];
      normal[1] = e1[2] * e2[0] - e1[0] * e2[2];
      normal[2] = e1[0] * e2[1] - e1[1] * e2[0];
      const float nl = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] +
                                 normal[2] * normal[2]);
      if (nl > 1.0e-6f) {
        normal[0] /= nl; normal[1] /= nl; normal[2] /= nl;
      }
    }
    const float nl = std::max(0.0f, normal[0] * lightDirN[0] +
                                      normal[1] * lightDirN[1] +
                                      normal[2] * lightDirN[2]);
    // Match the primary RT material response for intermediate layers: retain
    // a compact dielectric/metallic GGX lobe instead of reducing every
    // underlayer to Lambertian color. This keeps the bounded compositor cheap
    // while preserving authored roughness and metalness contrast.
    float metallic = 0.0f;
    float roughness = 0.35f;
    float subsurface = 0.0f;
    float subsurfaceScale = 1.0f;
    float coatWeight = 0.0f;
    float coatRoughness = 0.2f;
    float subsurfaceColor[3] = {color[0], color[1], color[2]};
    float coatColor[3] = {1.0f, 1.0f, 1.0f};
    if (mid >= 0) {
      const size_t lrtBase = static_cast<size_t>(mid) * kLightRtOpenPBRFloats;
      if (lrtBase + 42 < hs_.matLightRt.size()) {
        metallic = std::clamp(hs_.matLightRt[lrtBase + 40], 0.0f, 1.0f);
        roughness = std::clamp(hs_.matLightRt[lrtBase + 42], 0.03f, 1.0f);
        if (lrtBase + 43 < hs_.matLightRt.size()) {
          subsurfaceColor[0] = std::max(0.0f, hs_.matLightRt[lrtBase + 16]);
          subsurfaceColor[1] = std::max(0.0f, hs_.matLightRt[lrtBase + 17]);
          subsurfaceColor[2] = std::max(0.0f, hs_.matLightRt[lrtBase + 18]);
          subsurface = std::clamp(hs_.matLightRt[lrtBase + 19], 0.0f, 1.0f);
          subsurfaceScale = std::max(0.0f, hs_.matLightRt[lrtBase + 23]);
          coatColor[0] = std::max(0.0f, hs_.matLightRt[lrtBase + 24]);
          coatColor[1] = std::max(0.0f, hs_.matLightRt[lrtBase + 25]);
          coatColor[2] = std::max(0.0f, hs_.matLightRt[lrtBase + 26]);
          coatWeight = std::clamp(hs_.matLightRt[lrtBase + 27], 0.0f, 1.0f);
          coatRoughness = std::clamp(hs_.matLightRt[lrtBase + 44], 0.03f, 1.0f);
        }
      }
    }
    float hit[3] = {0.0f, 0.0f, 0.0f};
    if (static_cast<size_t>(tri) * 9 + 8 < cpuTris_.size()) {
      const float* tv = &cpuTris_[static_cast<size_t>(tri) * 9];
      const float w0 = 1.0f - bu - bv;
      for (int c = 0; c < 3; ++c)
        hit[c] = tv[c] * w0 + tv[3 + c] * bu + tv[6 + c] * bv;
    }
    float view[3] = {camPos[0] - hit[0], camPos[1] - hit[1], camPos[2] - hit[2]};
    Normalize3(view);
    float halfVec[3] = {lightDirN[0] + view[0], lightDirN[1] + view[1],
                         lightDirN[2] + view[2]};
    Normalize3(halfVec);
    const float nh = std::max(0.0f, normal[0] * halfVec[0] +
                                      normal[1] * halfVec[1] +
                                      normal[2] * halfVec[2]);
    const float f0 = 0.04f * (1.0f - metallic) + metallic;
    const float spec = std::pow(nh, std::max(1.0f, 2.0f /
                                                 (roughness * roughness) - 2.0f));
    const float lit = 0.15f + 0.85f * nl;
    const float radiusGain = std::clamp(std::sqrt(std::max(subsurfaceScale, 0.0f)) *
                                            2.0f, 0.0f, 1.0f);
    const float diffusion = subsurface * (0.2f + 0.8f * (1.0f - nl) * radiusGain);
    const float coatSpec = coatWeight * 0.04f * nl /
                           std::max(0.08f, coatRoughness * coatRoughness);
    const float scale = lit * (1.0f - metallic) + f0 * spec + coatSpec;
    return std::array<float, 3>{
        color[0] * scale * (1.0f - diffusion) + subsurfaceColor[0] * diffusion + coatColor[0] * coatSpec,
        color[1] * scale * (1.0f - diffusion) + subsurfaceColor[1] * diffusion + coatColor[1] * coatSpec,
        color[2] * scale * (1.0f - diffusion) + subsurfaceColor[2] * diffusion + coatColor[2] * coatSpec};
  };

  auto cpuLayerTransmission = [&](int tri) {
    std::array<float, 3> tint{1.0f, 1.0f, 1.0f};
    if (tri < 0 || static_cast<size_t>(tri) >= cpuMat_.size()) return tint;
    const int mid = cpuMat_[static_cast<size_t>(tri)];
    if (mid < 0) return tint;
    const size_t base = static_cast<size_t>(mid) * kLightRtOpenPBRFloats;
    if (base + 11 >= hs_.matLightRt.size()) return tint;
    const float weight = std::clamp(hs_.matLightRt[base + 11], 0.0f, 1.0f);
    const float depth = base + 15 < hs_.matLightRt.size()
                            ? std::max(hs_.matLightRt[base + 15], 0.0f)
                            : 0.0f;
    for (int c = 0; c < 3; ++c) {
      const float color = std::clamp(hs_.matLightRt[base + 8 + c], 0.0f, 1.0f);
      const float scatter = base + 12 + static_cast<size_t>(c) < hs_.matLightRt.size()
                                ? std::max(hs_.matLightRt[base + 12 + c], 0.0f)
                                : 0.0f;
      tint[c] = std::exp(-weight * depth *
                         (std::max(0.0f, 1.0f - color) + 0.25f * scatter));
    }
    // Match the CUDA/Vulkan OpenPBR transmission dispersion approximation.
    // The compact material lane stores the authored dispersion strength and
    // scale separately; Abbe number increases the effect for low-dispersion
    // media while remaining bounded for malformed input.
    const float dispersion = base + 62 < hs_.matLightRt.size()
                                 ? std::clamp(
                                       hs_.matLightRt[base + 60] *
                                           hs_.matLightRt[base + 62] *
                                           (base + 61 < hs_.matLightRt.size() &&
                                                    hs_.matLightRt[base + 61] > 0.0f
                                                ? 1.0f +
                                                      20.0f / std::max(
                                                          hs_.matLightRt[base + 61],
                                                          1.0f)
                                                : 1.0f),
                                       0.0f, 1.0f)
                                 : 0.0f;
    tint[0] *= 1.0f + 0.35f * dispersion;
    tint[2] *= 1.0f - 0.25f * dispersion;
    return tint;
  };

  auto cpuLayerTransmissionWeight = [&](int tri) {
    if (tri < 0 || static_cast<size_t>(tri) >= cpuMat_.size()) return 0.0f;
    const int mid = cpuMat_[static_cast<size_t>(tri)];
    if (mid < 0) return 0.0f;
    const size_t base = static_cast<size_t>(mid) * kLightRtOpenPBRFloats;
    return base + 11 < hs_.matLightRt.size()
               ? std::clamp(hs_.matLightRt[base + 11], 0.0f, 1.0f)
               : 0.0f;
  };

  struct CpuRefractResult {
    std::array<float, 3> color{0.0f, 0.0f, 0.0f};
    std::array<float, 3> origin{0.0f, 0.0f, 0.0f};
    std::array<float, 3> dir{0.0f, 0.0f, -1.0f};
    // The compositor needs the first continuation ray independently of the
    // bounded three-interface color approximation.  Reusing the latter's
    // terminal ray skips any interfaces that the underlayer walk should see.
    std::array<float, 3> continuationOrigin{0.0f, 0.0f, 0.0f};
    std::array<float, 3> continuationDir{0.0f, 0.0f, -1.0f};
    bool hasContinuation{false};
  };
  auto cpuLayerRefracted = [&](int tri, float bu, float bv,
                               const float rayOrigin[3], const float rayDir[3]) {
    CpuRefractResult result;
    std::copy(rayOrigin, rayOrigin + 3, result.origin.begin());
    std::copy(rayDir, rayDir + 3, result.dir.begin());
    std::array<float, 3> throughput{1.0f, 1.0f, 1.0f};
    int currentTri = tri;
    float currentBu = bu, currentBv = bv;
    float currentDir[3] = {rayDir[0], rayDir[1], rayDir[2]};
    for (int bounce = 0; bounce < 3; ++bounce) {
      if (currentTri < 0 ||
          static_cast<size_t>(currentTri) * 9 + 8 >= cpuTris_.size())
        return result;
      const float* tv = &cpuTris_[static_cast<size_t>(currentTri) * 9];
      const float p0[3] = {tv[0], tv[1], tv[2]};
      const float p1[3] = {tv[3], tv[4], tv[5]};
      const float p2[3] = {tv[6], tv[7], tv[8]};
      float n[3] = {(p1[1] - p0[1]) * (p2[2] - p0[2]) -
                       (p1[2] - p0[2]) * (p2[1] - p0[1]),
                   (p1[2] - p0[2]) * (p2[0] - p0[0]) -
                       (p1[0] - p0[0]) * (p2[2] - p0[2]),
                   (p1[0] - p0[0]) * (p2[1] - p0[1]) -
                       (p1[1] - p0[1]) * (p2[0] - p0[0])};
      const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
      if (nl <= 1.0e-8f) return result;
      for (float& v : n) v /= nl;
      const float normalDir = n[0] * currentDir[0] +
                              n[1] * currentDir[1] +
                              n[2] * currentDir[2];
      const bool entering = normalDir < 0.0f;
      if (normalDir > 0.0f) {
        for (float& v : n) v = -v;
      }
      int mid = currentTri < static_cast<int>(cpuMat_.size())
                    ? cpuMat_[static_cast<size_t>(currentTri)]
                    : -1;
      const size_t mb = mid >= 0 ? static_cast<size_t>(mid) *
                                      kLightRtOpenPBRFloats
                                : hs_.matLightRt.size();
      const float ior = mb + 43 < hs_.matLightRt.size()
                            ? std::max(hs_.matLightRt[mb + 43], 1.0f)
                            : 1.0f;
      const float eta = entering ? 1.0f / ior : ior;
      const float cosi = -(n[0] * currentDir[0] + n[1] * currentDir[1] +
                           n[2] * currentDir[2]);
      const float k = 1.0f - eta * eta * (1.0f - cosi * cosi);
      float refr[3];
      if (k < 0.0f) {
        const float dot = n[0] * currentDir[0] + n[1] * currentDir[1] +
                          n[2] * currentDir[2];
        for (int c = 0; c < 3; ++c) refr[c] = currentDir[c] - 2.0f * dot * n[c];
      } else {
        const float scale = eta * cosi - std::sqrt(k);
        for (int c = 0; c < 3; ++c) refr[c] = eta * currentDir[c] + scale * n[c];
      }
      const float rl = std::sqrt(refr[0] * refr[0] + refr[1] * refr[1] +
                                 refr[2] * refr[2]);
      if (rl <= 1.0e-8f) return result;
      for (float& v : refr) v /= rl;
      const auto tint = cpuLayerTransmission(currentTri);
      for (int c = 0; c < 3; ++c) throughput[c] *= tint[c];
      float hit[3] = {p0[0] * (1.0f - currentBu - currentBv) +
                          p1[0] * currentBu + p2[0] * currentBv,
                      p0[1] * (1.0f - currentBu - currentBv) +
                          p1[1] * currentBu + p2[1] * currentBv,
                      p0[2] * (1.0f - currentBu - currentBv) +
                          p1[2] * currentBu + p2[2] * currentBv};
      lrt_ray nextRay{};
      for (int c = 0; c < 3; ++c) {
        nextRay.org[c] = hit[c] + refr[c] * 1.0e-4f;
        nextRay.dir[c] = refr[c];
        result.origin[c] = nextRay.org[c];
        result.dir[c] = refr[c];
        if (bounce == 0) {
          result.continuationOrigin[c] = nextRay.org[c];
          result.continuationDir[c] = refr[c];
        }
      }
      if (bounce == 0) result.hasContinuation = true;
      nextRay.tmin = 1.0e-4f;
      nextRay.tmax = 1.0e30f;
      lrt_hit nextHit{};
      if (!lrt_tri_intersect1(scene_, &nextRay, &nextHit) ||
          nextHit.prim_id == LRT_TRI_NO_HIT)
        return result;
      currentTri = static_cast<int>(nextHit.prim_id);
      currentBu = nextHit.u;
      currentBv = nextHit.v;
      std::copy(refr, refr + 3, currentDir);
      if (bounce == 2) {
        const auto color = cpuLayerColor(currentTri, currentBu, currentBv);
        result.color = {color[0] * throughput[0], color[1] * throughput[1],
                        color[2] * throughput[2]};
        return result;
      }
    }
    return result;
  };

  auto worker = [&]() {
    int y;
    while ((y = nextRow.fetch_add(1, std::memory_order_relaxed)) < h) {
      const float ndcy = 1.0f - 2.0f *
          ((static_cast<float>(y) + 0.5f + sampleY) / static_cast<float>(h));
      for (int x = 0; x < w; ++x) {
        const float ndcx = 2.0f *
            ((static_cast<float>(x) + 0.5f + sampleX) / static_cast<float>(w)) - 1.0f;
        float nearW[3], farW[3];
        unproject(ndcx, ndcy, 0.0f, nearW);
        unproject(ndcx, ndcy, 1.0f, farW);
        float dir[3] = {farW[0] - nearW[0], farW[1] - nearW[1], farW[2] - nearW[2]};
        const float dlen = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (dlen > 1e-8f) { dir[0] /= dlen; dir[1] /= dlen; dir[2] /= dlen; }

        float rayOrg[3] = {camPos[0], camPos[1], camPos[2]};
        if (lens && lens->enabled()) {
          const float forwardDot = std::max(
              1.0e-4f, dir[0] * cameraForward[0] +
                             dir[1] * cameraForward[1] +
                             dir[2] * cameraForward[2]);
          const float focusT = lens->focusDistance / forwardDot;
          const float focus[3] = {camPos[0] + dir[0] * focusT,
                                  camPos[1] + dir[1] * focusT,
                                  camPos[2] + dir[2] * focusT};
          float up[3] = {0.0f, 1.0f, 0.0f};
          if (std::fabs(dir[1]) > 0.95f) {
            up[0] = 1.0f;
            up[1] = 0.0f;
          }
          float right[3] = {up[1] * dir[2] - up[2] * dir[1],
                            up[2] * dir[0] - up[0] * dir[2],
                            up[0] * dir[1] - up[1] * dir[0]};
          const float rlen = std::sqrt(right[0] * right[0] + right[1] * right[1] +
                                       right[2] * right[2]);
          if (rlen > 1e-8f) {
            for (float& value : right) value /= rlen;
            const float disk = sampleX * sampleX + sampleY * sampleY <= 1.0f
                                   ? lens->apertureRadius
                                   : 0.0f;
            rayOrg[0] += right[0] * sampleX * disk;
            rayOrg[1] += right[1] * sampleX * disk;
            rayOrg[2] += right[2] * sampleX * disk;
            const float trueUp[3] = {dir[1] * right[2] - dir[2] * right[1],
                                     dir[2] * right[0] - dir[0] * right[2],
                                     dir[0] * right[1] - dir[1] * right[0]};
            rayOrg[0] += trueUp[0] * sampleY * disk;
            rayOrg[1] += trueUp[1] * sampleY * disk;
            rayOrg[2] += trueUp[2] * sampleY * disk;
            dir[0] = focus[0] - rayOrg[0];
            dir[1] = focus[1] - rayOrg[1];
            dir[2] = focus[2] - rayOrg[2];
            const float newLen = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] +
                                           dir[2] * dir[2]);
            if (newLen > 1e-8f)
              for (float& value : dir) value /= newLen;
          }
        }

        lrt_ray ray{};
        ray.org[0] = rayOrg[0]; ray.org[1] = rayOrg[1]; ray.org[2] = rayOrg[2];
        ray.dir[0] = dir[0]; ray.dir[1] = dir[1]; ray.dir[2] = dir[2];
        ray.tmin = 1e-4f;
        ray.tmax = 1e30f;

        uint8_t* px = pixels + (static_cast<size_t>(y) * w + x) * 4;
        lrt_hit hit{};
        if (!lrt_tri_intersect1(scene_, &ray, &hit) || hit.prim_id == LRT_TRI_NO_HIT) {
          px[0] = static_cast<uint8_t>(std::clamp(clearColor[0] * 255.0f, 0.0f, 255.0f));
          px[1] = static_cast<uint8_t>(std::clamp(clearColor[1] * 255.0f, 0.0f, 255.0f));
          px[2] = static_cast<uint8_t>(std::clamp(clearColor[2] * 255.0f, 0.0f, 255.0f));
          px[3] = 255;
          continue;
        }

        const uint32_t tri = hit.prim_id;
        float nrm[3];
        Interp9(&cpuNrms_[static_cast<size_t>(tri) * 9], hit.u, hit.v, nrm);
        const float nlen = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
        if (nlen > 1e-8f) { nrm[0] /= nlen; nrm[1] /= nlen; nrm[2] /= nlen; }

        float outCol[3];
        if (renderMode == 2) {  // Normals
          outCol[0] = nrm[0] * 0.5f + 0.5f;
          outCol[1] = nrm[1] * 0.5f + 0.5f;
          outCol[2] = nrm[2] * 0.5f + 0.5f;
        } else if (renderMode == 3) {  // MaterialId (hashed color)
          const int matId = (tri < cpuMat_.size()) ? cpuMat_[tri] : -1;
          const uint32_t hcol = HashColor(static_cast<uint32_t>(matId + 1));
          outCol[0] = ((hcol >> 16) & 0xFF) / 255.0f;
          outCol[1] = ((hcol >> 8) & 0xFF) / 255.0f;
          outCol[2] = (hcol & 0xFF) / 255.0f;
        } else if (renderMode == 6) {  // Depth
          const float d = std::clamp(hit.t / std::max(1e-3f, depthScale), 0.0f, 1.0f);
          outCol[0] = outCol[1] = outCol[2] = d;
        } else {  // Shaded (default) -- flat Lambertian, one directional light + ambient
          const int matId = (tri < cpuMat_.size()) ? cpuMat_[tri] : -1;
          float base[3] = {0.8f, 0.8f, 0.8f};
          if (matId >= 0 && static_cast<size_t>(matId) * 3 + 3 <= hs_.matBase.size()) {
            base[0] = hs_.matBase[static_cast<size_t>(matId) * 3 + 0];
            base[1] = hs_.matBase[static_cast<size_t>(matId) * 3 + 1];
            base[2] = hs_.matBase[static_cast<size_t>(matId) * 3 + 2];
          }
          // Base-color texture (matTex slot 0), UV set 0. No other semantic
          // slots (metallic/roughness/normal/emissive/opacity) yet -- a first
          // cut, replacing the flat constant fallback with the sampled texel
          // when the material has one.
          if (matId >= 0 && !hs_.matTex.empty() &&
              static_cast<size_t>(matId) * kRtMaterialTexSlots < hs_.matTex.size()) {
            const int baseTex = hs_.matTex[static_cast<size_t>(matId) * kRtMaterialTexSlots + 0];
            if (baseTex >= 0 && tri * 6 + 5 < cpuUv_.size()) {
              float uv[2];
              Interp6(&cpuUv_[static_cast<size_t>(tri) * 6], hit.u, hit.v, uv);
              float texCol[3];
              SampleTextureBilinear(hs_.textures, hs_.texels, baseTex, uv[0], uv[1], texCol);
              base[0] *= texCol[0]; base[1] *= texCol[1]; base[2] *= texCol[2];
            }
          }
          if (matId >= 0 && static_cast<size_t>(matId) * kRtMaterialGraphFloats +
                                  kRtMaterialGraphHeaderFloats <= hs_.matGraph.size()) {
            const size_t graphBase = static_cast<size_t>(matId) * kRtMaterialGraphFloats;
            if (hs_.matGraph[graphBase + 1] >= 0.0f && tri * 6 + 5 < cpuUv_.size()) {
              float uv[2];
              Interp6(&cpuUv_[static_cast<size_t>(tri) * 6], hit.u, hit.v, uv);
              float uv1[2] = {uv[0], uv[1]};
              if (static_cast<size_t>(tri) * 6 + 5 < cpuUv1_.size())
                Interp6(&cpuUv1_[static_cast<size_t>(tri) * 6], hit.u, hit.v, uv1);
              const auto graphBaseColor = EvalCpuMaterialXGraph(hs_, matId, 0,
                                                                  uv[0], uv[1],
                                                                  uv1[0], uv1[1]);
              base[0] = graphBaseColor[0];
              base[1] = graphBaseColor[1];
              base[2] = graphBaseColor[2];
            }
          }
          if (tri < cpuInstance_.size()) {
            const int instanceIndex = cpuInstance_[tri];
            if (instanceIndex >= 0 && static_cast<size_t>(instanceIndex) < hs_.instances.size()) {
              const Inst& inst = hs_.instances[static_cast<size_t>(instanceIndex)];
              base[0] *= inst.tint[0];
              base[1] *= inst.tint[1];
              base[2] *= inst.tint[2];
            }
          }
          const float hitP[3] = {rayOrg[0] + dir[0] * hit.t,
                                 rayOrg[1] + dir[1] * hit.t,
                                 rayOrg[2] + dir[2] * hit.t};
          float direct = 0.0f;
          bool hadDirectLight = false;
          const int lightCount = hs_.numLights;
          for (int li = 0; li < lightCount; ++li) {
            const float* lp = &hs_.lightParams[static_cast<size_t>(li) *
                                                kRtLightParamFloats];
            const int lightType = static_cast<int>(lp[0] + 0.5f);
            if (lightType == static_cast<int>(DrawLightCPU::Type::Dome) ||
                lp[1] <= 0.5f) continue;
            const int instanceIndex = tri < cpuInstance_.size() ? cpuInstance_[tri] : -1;
            if (li < 32 && instanceIndex >= 0 &&
                static_cast<size_t>(instanceIndex) < hs_.instances.size() &&
                (hs_.instances[static_cast<size_t>(instanceIndex)].directLightMask &
                 (1u << li)) == 0) continue;
            hadDirectLight = true;
            float lightP[3] = {lp[4], lp[5], lp[6]};
            float emitterN[3] = {0.0f, 0.0f, 0.0f};
            float emitter[3] = {lp[12], lp[13], lp[14]};
            if (lightType == static_cast<int>(DrawLightCPU::Type::Geometry) &&
                lp[52] > 2.75f && lp[52] < 3.25f) {
              const int triOffset = static_cast<int>(lp[53] + 0.5f);
              const int instance = static_cast<int>(lp[55] + 0.5f);
              if (triOffset >= 0 && instance >= 0 &&
                  static_cast<size_t>(triOffset) < hs_.triCount &&
                  static_cast<size_t>(instance) < hs_.instances.size()) {
                const float* tv = &hs_.tris[static_cast<size_t>(triOffset) * 9];
                float center[3] = {(tv[0] + tv[3] + tv[6]) / 3.0f,
                                   (tv[1] + tv[4] + tv[7]) / 3.0f,
                                   (tv[2] + tv[5] + tv[8]) / 3.0f};
                const Inst& inst = hs_.instances[static_cast<size_t>(instance)];
                TransformPoint(inst.o2w, center, lightP);
                float e1[3] = {tv[3] - tv[0], tv[4] - tv[1], tv[5] - tv[2]};
                float e2[3] = {tv[6] - tv[0], tv[7] - tv[1], tv[8] - tv[2]};
                float no[3] = {e1[1] * e2[2] - e1[2] * e2[1],
                               e1[2] * e2[0] - e1[0] * e2[2],
                               e1[0] * e2[1] - e1[1] * e2[0]};
                TransformNormal(inst.w2o, no, emitterN);
                const int mid = (static_cast<size_t>(triOffset) < hs_.mat.size())
                                    ? hs_.mat[static_cast<size_t>(triOffset)] : -1;
                if (mid >= 0 && static_cast<size_t>(mid) * 6 + 5 < hs_.matPbr.size()) {
                  emitter[0] = hs_.matPbr[static_cast<size_t>(mid) * 6 + 2];
                  emitter[1] = hs_.matPbr[static_cast<size_t>(mid) * 6 + 3];
                  emitter[2] = hs_.matPbr[static_cast<size_t>(mid) * 6 + 4];
                }
              }
            }
            float L[3] = {lightP[0] - hitP[0], lightP[1] - hitP[1],
                          lightP[2] - hitP[2]};
            float attenuation = 1.0f;
            float maxShadowT = 1.0e30f;
            if (lightType == static_cast<int>(DrawLightCPU::Type::Distant)) {
              L[0] = -lp[8]; L[1] = -lp[9]; L[2] = -lp[10];
            } else {
              const float d2 = std::max(1.0e-6f, L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
              maxShadowT = std::sqrt(d2);
              attenuation = 1.0f / d2;
            }
            Normalize3(L);
            float shape = 1.0f;
            if (lightType == static_cast<int>(DrawLightCPU::Type::Geometry)) {
              shape *= std::max(0.0f, emitterN[0] * -L[0] +
                                      emitterN[1] * -L[1] + emitterN[2] * -L[2]);
            }
            const float nl = std::max(0.0f, nrm[0] * L[0] + nrm[1] * L[1] + nrm[2] * L[2]);
            if (nl <= 0.0f || shape <= 0.0f) continue;
            float shadow = 1.0f;
            if ((static_cast<int>(lp[1] + 0.5f) & (1 << 1)) != 0) {
              lrt_ray shadowRay{};
              shadowRay.org[0] = hitP[0] + nrm[0] * 1e-3f;
              shadowRay.org[1] = hitP[1] + nrm[1] * 1e-3f;
              shadowRay.org[2] = hitP[2] + nrm[2] * 1e-3f;
              shadowRay.dir[0] = L[0]; shadowRay.dir[1] = L[1]; shadowRay.dir[2] = L[2];
              shadowRay.tmin = 1e-4f; shadowRay.tmax = maxShadowT;
              bool shadowLinked = true;
              if (li < 32 && instanceIndex >= 0 &&
                  static_cast<size_t>(instanceIndex) < hs_.instances.size()) {
                shadowLinked = (hs_.instances[static_cast<size_t>(instanceIndex)].shadowLightMask &
                                (1u << li)) != 0;
              }
              if (shadowLinked && lrt_tri_occluded1(scene_, &shadowRay)) shadow = 0.0f;
            }
            const float lightToPoint[3] = {-L[0], -L[1], -L[2]};
            const float radiance = std::max(0.0f, emitter[0]) * 0.3333333f +
                                   std::max(0.0f, emitter[1]) * 0.3333333f +
                                   std::max(0.0f, emitter[2]) * 0.3333333f;
            direct += nl * attenuation * shape * shadow * radiance *
                      PackedIesFactor(lp, lightToPoint) * lp[29];
          }
          const float diff = hadDirectLight ? direct :
              std::max(0.0f, nrm[0] * lightDirN[0] + nrm[1] * lightDirN[1] +
                             nrm[2] * lightDirN[2]);
          // Keep the CPU preview on the same compact OpenPBR material lanes
          // as the CUDA/Vulkan paths.  It remains a single-light preview, but
          // no longer collapses metallic, transmission, coat, and subsurface
          // materials to an unconditional Lambertian response.
          float metallic = 0.0f, roughness = 0.5f, transmission = 0.0f;
          float specWeight = 1.0f, ior = 1.5f, coatWeight = 0.0f;
          float coatRoughness = 0.2f;
          float subsurface = 0.0f, subsurfaceRadius = 1.0f;
          float anisotropy = 0.0f, anisotropyRotation = 0.0f;
          float sheenWeight = 0.0f, sheenRoughness = 0.3f;
          float coatAffectColor = 0.0f, coatAffectRoughness = 0.0f;
          float coatDarkening = 0.0f;
          float thinFilmWeight = 0.0f, thinFilmThickness = 0.0f;
          float thinFilmIor = 1.5f;
          float emission[3] = {0.0f, 0.0f, 0.0f};
          float specColor[3] = {1.0f, 1.0f, 1.0f};
          float sheenColor[3] = {1.0f, 1.0f, 1.0f};
          float coatColor[3] = {1.0f, 1.0f, 1.0f};
          if (matId >= 0) {
            const size_t pb = static_cast<size_t>(matId) * 6;
            if (pb + 5 < hs_.matPbr.size()) {
              metallic = std::clamp(hs_.matPbr[pb + 0], 0.0f, 1.0f);
              roughness = std::clamp(hs_.matPbr[pb + 1], 0.03f, 1.0f);
              emission[0] = hs_.matPbr[pb + 2];
              emission[1] = hs_.matPbr[pb + 3];
              emission[2] = hs_.matPbr[pb + 4];
            }
            const size_t lb = static_cast<size_t>(matId) * kLightRtOpenPBRFloats;
            if (lb + 79 < hs_.matLightRt.size()) {
              const float* lrt = &hs_.matLightRt[lb];
              specColor[0] = std::max(0.0f, lrt[4]);
              specColor[1] = std::max(0.0f, lrt[5]);
              specColor[2] = std::max(0.0f, lrt[6]);
              specWeight = std::clamp(lrt[7], 0.0f, 1.0f);
              sheenColor[0] = std::max(0.0f, lrt[28]);
              sheenColor[1] = std::max(0.0f, lrt[29]);
              sheenColor[2] = std::max(0.0f, lrt[30]);
              sheenWeight = std::clamp(lrt[31], 0.0f, 1.0f);
              transmission = std::clamp(lrt[11], 0.0f, 1.0f);
              subsurface = std::clamp(lrt[19], 0.0f, 1.0f);
              subsurfaceRadius = std::max(
                  0.0f, (0.2126f * lrt[20] + 0.7152f * lrt[21] +
                         0.0722f * lrt[22]) * lrt[23]);
              coatWeight = std::clamp(lrt[27], 0.0f, 1.0f);
              coatColor[0] = std::max(0.0f, lrt[24]);
              coatColor[1] = std::max(0.0f, lrt[25]);
              coatColor[2] = std::max(0.0f, lrt[26]);
              coatRoughness = std::clamp(lrt[44], 0.03f, 1.0f);
              sheenRoughness = std::clamp(lrt[46], 0.0f, 1.0f);
              thinFilmWeight = std::clamp(lrt[48], 0.0f, 1.0f);
              thinFilmThickness = std::max(lrt[49], 0.0f);
              thinFilmIor = std::max(lrt[50], 1.0f);
              ior = std::max(lrt[43], 1.0f);
              anisotropy = std::clamp(lrt[57] + 0.5f * lrt[59], -1.0f, 1.0f);
              anisotropyRotation = lrt[58];
              coatAffectColor = std::clamp(lrt[67], 0.0f, 1.0f);
              coatAffectRoughness = std::clamp(lrt[68], 0.0f, 1.0f);
              coatDarkening = std::clamp(lrt[70], 0.0f, 1.0f);
              emission[0] += lrt[76] * std::max(lrt[79], 0.0f);
              emission[1] += lrt[77] * std::max(lrt[79], 0.0f);
              emission[2] += lrt[78] * std::max(lrt[79], 0.0f);
            }
          }
          const float viewLen = std::sqrt(
              (camPos[0] - hitP[0]) * (camPos[0] - hitP[0]) +
              (camPos[1] - hitP[1]) * (camPos[1] - hitP[1]) +
              (camPos[2] - hitP[2]) * (camPos[2] - hitP[2]));
          const float nv = std::max(1.0e-4f,
                                    (nrm[0] * (camPos[0] - hitP[0]) +
                                     nrm[1] * (camPos[1] - hitP[1]) +
                                     nrm[2] * (camPos[2] - hitP[2])) /
                                        std::max(viewLen, 1.0e-4f));
          float directionalRoughness = roughness;
          if (std::fabs(anisotropy) > 1.0e-4f &&
              static_cast<size_t>(tri) * 6 + 5 < cpuUv_.size() &&
              static_cast<size_t>(tri) * 9 + 8 < cpuTris_.size()) {
            const float* tv = &cpuTris_[static_cast<size_t>(tri) * 9];
            const float* uv = &cpuUv_[static_cast<size_t>(tri) * 6];
            const float e1[3] = {tv[3] - tv[0], tv[4] - tv[1], tv[5] - tv[2]};
            const float e2[3] = {tv[6] - tv[0], tv[7] - tv[1], tv[8] - tv[2]};
            const float du1 = uv[2] - uv[0], dv1 = uv[3] - uv[1];
            const float du2 = uv[4] - uv[0], dv2 = uv[5] - uv[1];
            const float det = du1 * dv2 - du2 * dv1;
            if (std::fabs(det) > 1.0e-8f) {
              float t[3] = {(e1[0] * dv2 - e2[0] * dv1) / det,
                            (e1[1] * dv2 - e2[1] * dv1) / det,
                            (e1[2] * dv2 - e2[2] * dv1) / det};
              const float tn = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
              if (tn > 1.0e-6f) {
                for (float& c : t) c /= tn;
                float b[3] = {nrm[1] * t[2] - nrm[2] * t[1],
                              nrm[2] * t[0] - nrm[0] * t[2],
                              nrm[0] * t[1] - nrm[1] * t[0]};
                const float rot = anisotropyRotation * 0.01745329252f;
                const float cr = std::cos(rot), sr = std::sin(rot);
                const float rt[3] = {t[0] * cr + b[0] * sr,
                                     t[1] * cr + b[1] * sr,
                                     t[2] * cr + b[2] * sr};
                const float rb[3] = {-t[0] * sr + b[0] * cr,
                                     -t[1] * sr + b[1] * cr,
                                     -t[2] * sr + b[2] * cr};
                float h[3] = {lightDirN[0] + (camPos[0] - hitP[0]) / viewLen,
                              lightDirN[1] + (camPos[1] - hitP[1]) / viewLen,
                              lightDirN[2] + (camPos[2] - hitP[2]) / viewLen};
                const float hn = std::sqrt(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
                if (hn > 1.0e-6f) {
                  for (float& c : h) c /= hn;
                  const float ht = h[0] * rt[0] + h[1] * rt[1] + h[2] * rt[2];
                  const float hb = h[0] * rb[0] + h[1] * rb[1] + h[2] * rb[2];
                  directionalRoughness = std::clamp(
                      roughness * (1.0f + anisotropy * (ht * ht - hb * hb)),
                      0.03f, 1.0f);
                }
              }
            }
          }
          const float f0 = std::pow((ior - 1.0f) / (ior + 1.0f), 2.0f);
          const float fresnel = f0 + (1.0f - f0) *
              std::pow(1.0f - std::clamp(nv, 0.0f, 1.0f), 5.0f);
          if (thinFilmWeight > 0.0f && thinFilmThickness > 0.0f) {
            const float phase = 6.28318530718f * thinFilmIor *
                                thinFilmThickness *
                                (1.0f - std::clamp(nv, 0.0f, 1.0f));
            const float phaseMul[3] = {1.0f, 1.17f, 1.35f};
            const float phaseOff[3] = {0.0f, 2.1f, 4.2f};
            for (int c = 0; c < 3; ++c) {
              const float film = 0.5f + 0.5f *
                  std::cos(phase * phaseMul[c] + phaseOff[c]);
              const float filmF = std::clamp(
                  f0 + (1.0f - f0) * film * 0.65f, 0.0f, 1.0f);
              specColor[c] *= 1.0f - thinFilmWeight +
                              thinFilmWeight * (filmF / std::max(f0, 1.0e-4f));
            }
          }
          // Keep the CPU preview aligned with the packed OpenPBR coat and
          // sheen controls used by the Vulkan/CUDA paths.  These bounded
          // energy adjustments avoid turning either lobe into an additive
          // brightness spike on grazing views.
          const float coatColorMix = std::clamp(coatWeight * coatAffectColor,
                                                0.0f, 1.0f);
          for (int c = 0; c < 3; ++c) {
            base[c] *= 1.0f - 0.2f * std::clamp(coatWeight * coatDarkening,
                                                0.0f, 1.0f);
            base[c] *= 1.0f + (coatColor[c] - 1.0f) * coatColorMix;
          }
          const float coatRoughMix = std::clamp(coatWeight * coatAffectRoughness,
                                                0.0f, 1.0f);
          const float lobeRoughness = roughness * (1.0f - coatRoughMix) +
                                      std::max(0.03f, coatRoughness) * coatRoughMix;
          const float diffuseWeight = (1.0f - metallic) *
                                      (1.0f - transmission) *
                                      (1.0f - fresnel);
          const float radiusGain = std::clamp(std::sqrt(subsurfaceRadius) * 8.0f,
                                               0.0f, 1.0f);
          const float diffusion = subsurface *
              (0.25f + 0.75f * (1.0f - diff) * radiusGain);
          const float specular = specWeight * (fresnel + metallic) *
              diff / std::max(0.08f, directionalRoughness * directionalRoughness * 4.0f);
          const float coat = coatWeight * 0.04f * diff /
                             std::max(0.08f, lobeRoughness * lobeRoughness);
          const float sheen = sheenWeight *
              (0.5f + 0.5f * sheenRoughness) *
              std::pow(1.0f - std::clamp(nv, 0.0f, 1.0f), 2.0f) * diff;
          const float lit = 0.15f + 0.85f * diff * diffuseWeight + diffusion;
          outCol[0] = (base[0] * lit + specColor[0] * specular +
                       sheenColor[0] * sheen + coat + emission[0]) * exposureMul;
          outCol[1] = (base[1] * lit + specColor[1] * specular +
                       sheenColor[1] * sheen + coat + emission[1]) * exposureMul;
          outCol[2] = (base[2] * lit + specColor[2] * specular +
                       sheenColor[2] * sheen + coat + emission[2]) * exposureMul;
          if (renderMode == 0) {
            const int mid = (tri < cpuMat_.size()) ? cpuMat_[tri] : -1;
            float alphaMode = 0.0f;
            if (mid >= 0 && static_cast<size_t>(mid) * kLightRtOpenPBRFloats + 54 <
                               hs_.matLightRt.size())
              alphaMode = hs_.matLightRt[static_cast<size_t>(mid) *
                                         kLightRtOpenPBRFloats + 54];
            const float topAlpha = cpuLayerAlpha(static_cast<int>(tri), hit.u, hit.v);
            if (alphaMode > 0.5f && alphaMode < 1.5f && topAlpha <= 0.0f) {
              outCol[0] = clearColor[0]; outCol[1] = clearColor[1];
              outCol[2] = clearColor[2];
            } else if (alphaMode > 1.5f && topAlpha < 1.0f) {
              const float topColor[3] = {outCol[0], outCol[1], outCol[2]};
              const auto refracted = cpuLayerRefracted(
                  static_cast<int>(tri), hit.u, hit.v, rayOrg, dir);
              const auto topTint = cpuLayerTransmission(static_cast<int>(tri));
              const float topTransmission = cpuLayerTransmissionWeight(
                  static_cast<int>(tri));
              float underAccum[3] = {0.0f, 0.0f, 0.0f};
              float throughput[3] = {1.0f, 1.0f, 1.0f};
              // Continue the underlayer walk from the last refracted interface.
              // This preserves the changed direction through stacked glass while
              // retaining the original camera ray for non-transmissive materials.
              float walkOrigin[3] = {rayOrg[0], rayOrg[1], rayOrg[2]};
              float walkDir[3] = {dir[0], dir[1], dir[2]};
              float cursor = hit.t + 1.0e-4f;
              if (topTransmission > 0.0f) {
                if (refracted.hasContinuation) {
                  std::copy(refracted.continuationOrigin.begin(),
                            refracted.continuationOrigin.end(), walkOrigin);
                  std::copy(refracted.continuationDir.begin(),
                            refracted.continuationDir.end(), walkDir);
                }
                cursor = 1.0e-4f;
              }
              for (int layer = 0; layer < 7 &&
                   (throughput[0] + throughput[1] + throughput[2]) > 0.009f;
                   ++layer) {
                lrt_ray nextRay{};
                nextRay.org[0] = walkOrigin[0]; nextRay.org[1] = walkOrigin[1];
                nextRay.org[2] = walkOrigin[2];
                nextRay.dir[0] = walkDir[0]; nextRay.dir[1] = walkDir[1];
                nextRay.dir[2] = walkDir[2];
                nextRay.tmin = cursor; nextRay.tmax = 1.0e30f;
                lrt_hit nextHit{};
                if (!lrt_tri_intersect1(scene_, &nextRay, &nextHit) ||
                    nextHit.prim_id == LRT_TRI_NO_HIT) break;
                const int nextTri = static_cast<int>(nextHit.prim_id);
                const float layerAlpha = cpuLayerAlpha(nextTri, nextHit.u, nextHit.v);
                const auto layerColor = cpuLayerColor(nextTri, nextHit.u, nextHit.v);
                underAccum[0] += layerColor[0] * throughput[0] * layerAlpha;
                underAccum[1] += layerColor[1] * throughput[1] * layerAlpha;
                underAccum[2] += layerColor[2] * throughput[2] * layerAlpha;
                const auto layerTint = cpuLayerTransmission(nextTri);
                for (int c = 0; c < 3; ++c)
                  throughput[c] *= layerTint[c] * (1.0f - layerAlpha);
                // Intermediate transparent interfaces can change the
                // direction of the remaining walk.  Propagate that bounded
                // refraction instead of continuing every layer along the
                // previous ray; opaque/alpha-only layers remain straight.
                if (cpuLayerTransmissionWeight(nextTri) > 0.0f) {
                  const auto layerRefracted = cpuLayerRefracted(
                      nextTri, nextHit.u, nextHit.v, walkOrigin, walkDir);
                  std::copy(layerRefracted.origin.begin(),
                            layerRefracted.origin.end(), walkOrigin);
                  std::copy(layerRefracted.dir.begin(),
                            layerRefracted.dir.end(), walkDir);
                } else {
                  for (int c = 0; c < 3; ++c)
                    walkOrigin[c] += walkDir[c] * nextHit.t + walkDir[c] * 1.0e-4f;
                }
                cursor = 1.0e-4f;
                if (layerAlpha >= 0.999f) break;
              }
              for (int c = 0; c < 3; ++c) {
                const float straightUnder = underAccum[c] +
                    clearColor[c] * throughput[c];
                const float straightTransmitted = straightUnder * topTint[c];
                const float transmitted = straightTransmitted *
                    (1.0f - topTransmission) +
                    refracted.color[c] * topTransmission;
                outCol[c] = topColor[c] * topAlpha +
                            transmitted * (1.0f - topAlpha);
              }
            }
          }
        }
        px[0] = static_cast<uint8_t>(std::clamp(outCol[0] * 255.0f, 0.0f, 255.0f));
        px[1] = static_cast<uint8_t>(std::clamp(outCol[1] * 255.0f, 0.0f, 255.0f));
        px[2] = static_cast<uint8_t>(std::clamp(outCol[2] * 255.0f, 0.0f, 255.0f));
        px[3] = 255;
      }
    }
  };

  std::vector<std::thread> pool;
  pool.reserve(nThreads > 1 ? nThreads - 1 : 0);
  for (unsigned i = 1; i < nThreads; ++i) pool.emplace_back(worker);
  worker();
  for (auto& t : pool) t.join();
  return true;
}

}  // namespace tusdview
