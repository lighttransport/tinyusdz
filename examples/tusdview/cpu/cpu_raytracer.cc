// SPDX-License-Identifier: Apache-2.0
#include "cpu_raytracer.hh"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>

#include "lightrt_c_tri.h"
#include "lightrt_mtlx_bridge.hh"  // kRtMaterialTexSlots

namespace tusdview {

CpuRayTracer::~CpuRayTracer() { freeScene(); }

void CpuRayTracer::freeScene() {
  if (scene_) {
    lrt_tri_scene_free(scene_);
    scene_ = nullptr;
  }
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
  triCount_ = hs_.triCount;
  truncated_ = hs_.truncated;
  if (triCount_ == 0) {
    if (err) *err = "no triangles";
    return false;
  }

  lrt_tri_build_options opts{};
  opts.quality = LRT_TRI_BUILD_FAST;  // interactive-priority, not final-quality
  opts.layout = LRT_TRI_LAYOUT_AUTO;
  opts.max_leaf_size = 0;  // default
  opts.num_threads = static_cast<unsigned>(
      std::max(1u, std::thread::hardware_concurrency()));

  lrt_result lrtErr;
  scene_ = lrt_tri_scene_build(hs_.tris.data(), triCount_, &opts, &lrtErr);
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

// Bilinear-sample an ordinary (non-UDIM, non-Ptex) RGBA8 HostTextureDesc at
// its base mip. `u`/`v` are wrapped per desc.wrapS/wrapT (0 = repeat, anything
// else = clamp, matching the raster/RT kernels' convention). No sRGB decode:
// texels are used as-is, consistent with this tracer's un-color-managed
// flat shading (CudaRayTracer/HipRayTracer decode in their GPU kernel, which
// this first cut doesn't replicate).
inline void SampleTextureBilinear(const std::vector<HostTextureDesc>& textures,
                                  const std::vector<uint8_t>& texels, int texId,
                                  float u, float v, float out[3]) {
  if (texId < 0 || static_cast<size_t>(texId) >= textures.size()) {
    out[0] = out[1] = out[2] = 1.0f;
    return;
  }
  const HostTextureDesc& d = textures[static_cast<size_t>(texId)];
  if (d.isUdim || d.isPtex || d.width <= 0 || d.height <= 0) {
    out[0] = out[1] = out[2] = 1.0f;
    return;
  }
  auto wrap = [](float x, int mode, int size) -> float {
    if (mode == 0) {  // repeat
      x = x - std::floor(x);
    } else {  // clamp
      x = std::clamp(x, 0.0f, 1.0f);
    }
    return x * static_cast<float>(size) - 0.5f;
  };
  const float fx = wrap(u, d.wrapS, d.width);
  const float fy = wrap(1.0f - v, d.wrapT, d.height);  // texel row 0 = top (v=0 at top for glTF-style UVs)
  const int x0 = static_cast<int>(std::floor(fx));
  const int y0 = static_cast<int>(std::floor(fy));
  const float tx = fx - static_cast<float>(x0);
  const float ty = fy - static_cast<float>(y0);
  auto texel = [&](int x, int y, float rgb[3]) {
    x = ((x % d.width) + d.width) % d.width;
    y = ((y % d.height) + d.height) % d.height;
    const size_t idx = static_cast<size_t>(d.offset) +
                       (static_cast<size_t>(y) * d.width + x) * 4;
    if (idx + 2 >= texels.size()) { rgb[0] = rgb[1] = rgb[2] = 1.0f; return; }
    rgb[0] = texels[idx + 0] / 255.0f;
    rgb[1] = texels[idx + 1] / 255.0f;
    rgb[2] = texels[idx + 2] / 255.0f;
  };
  float c00[3], c10[3], c01[3], c11[3];
  texel(x0, y0, c00); texel(x0 + 1, y0, c10);
  texel(x0, y0 + 1, c01); texel(x0 + 1, y0 + 1, c11);
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

}  // namespace

bool CpuRayTracer::trace(const float invViewProj[16], const float /*viewProj*/[16],
                         const float camPos[3], const float lightDir[3],
                         const float clearColor[3], float exposure, int renderMode,
                         float depthScale, const float /*sceneMin*/[3],
                         const float /*sceneExtent*/[3], int w, int h,
                         std::vector<uint8_t>* rgba, std::string* err,
                         int /*spp*/, const RtCameraLens* /*lens*/) {
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

  auto worker = [&]() {
    int y;
    while ((y = nextRow.fetch_add(1, std::memory_order_relaxed)) < h) {
      const float ndcy = 1.0f - 2.0f * ((static_cast<float>(y) + 0.5f) / static_cast<float>(h));
      for (int x = 0; x < w; ++x) {
        const float ndcx = 2.0f * ((static_cast<float>(x) + 0.5f) / static_cast<float>(w)) - 1.0f;
        float nearW[3], farW[3];
        unproject(ndcx, ndcy, 0.0f, nearW);
        unproject(ndcx, ndcy, 1.0f, farW);
        float dir[3] = {farW[0] - nearW[0], farW[1] - nearW[1], farW[2] - nearW[2]};
        const float dlen = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (dlen > 1e-8f) { dir[0] /= dlen; dir[1] /= dlen; dir[2] /= dlen; }

        lrt_ray ray{};
        ray.org[0] = camPos[0]; ray.org[1] = camPos[1]; ray.org[2] = camPos[2];
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
        Interp9(&hs_.nrms[static_cast<size_t>(tri) * 9], hit.u, hit.v, nrm);
        const float nlen = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
        if (nlen > 1e-8f) { nrm[0] /= nlen; nrm[1] /= nlen; nrm[2] /= nlen; }

        float outCol[3];
        if (renderMode == 2) {  // Normals
          outCol[0] = nrm[0] * 0.5f + 0.5f;
          outCol[1] = nrm[1] * 0.5f + 0.5f;
          outCol[2] = nrm[2] * 0.5f + 0.5f;
        } else if (renderMode == 3) {  // MaterialId (hashed color)
          const int matId = (tri < hs_.mat.size()) ? hs_.mat[tri] : -1;
          const uint32_t hcol = HashColor(static_cast<uint32_t>(matId + 1));
          outCol[0] = ((hcol >> 16) & 0xFF) / 255.0f;
          outCol[1] = ((hcol >> 8) & 0xFF) / 255.0f;
          outCol[2] = (hcol & 0xFF) / 255.0f;
        } else if (renderMode == 6) {  // Depth
          const float d = std::clamp(hit.t / std::max(1e-3f, depthScale), 0.0f, 1.0f);
          outCol[0] = outCol[1] = outCol[2] = d;
        } else {  // Shaded (default) -- flat Lambertian, one directional light + ambient
          const int matId = (tri < hs_.mat.size()) ? hs_.mat[tri] : -1;
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
            if (baseTex >= 0 && tri * 6 + 5 < hs_.uv.size()) {
              float uv[2];
              Interp6(&hs_.uv[static_cast<size_t>(tri) * 6], hit.u, hit.v, uv);
              float texCol[3];
              SampleTextureBilinear(hs_.textures, hs_.texels, baseTex, uv[0], uv[1], texCol);
              base[0] *= texCol[0]; base[1] *= texCol[1]; base[2] *= texCol[2];
            }
          }
          const float diff = std::max(0.0f, nrm[0] * lightDirN[0] + nrm[1] * lightDirN[1] +
                                                nrm[2] * lightDirN[2]);
          float shadow = 1.0f;
          if (diff > 0.0f) {
            lrt_ray shadowRay{};
            const float hitP[3] = {camPos[0] + dir[0] * hit.t, camPos[1] + dir[1] * hit.t,
                                   camPos[2] + dir[2] * hit.t};
            shadowRay.org[0] = hitP[0] + nrm[0] * 1e-3f;
            shadowRay.org[1] = hitP[1] + nrm[1] * 1e-3f;
            shadowRay.org[2] = hitP[2] + nrm[2] * 1e-3f;
            shadowRay.dir[0] = lightDirN[0]; shadowRay.dir[1] = lightDirN[1]; shadowRay.dir[2] = lightDirN[2];
            shadowRay.tmin = 1e-4f;
            shadowRay.tmax = 1e30f;
            if (lrt_tri_occluded1(scene_, &shadowRay)) shadow = 0.0f;
          }
          const float lit = 0.15f + 0.85f * diff * shadow;  // small ambient floor
          outCol[0] = base[0] * lit * exposureMul;
          outCol[1] = base[1] * lit * exposureMul;
          outCol[2] = base[2] * lit * exposureMul;
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
