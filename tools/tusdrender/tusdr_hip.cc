// SPDX-License-Identifier: Apache-2.0
// tusdrender — LightRT HIP/ROCm backend (GPU BVH traversal via lightrt_c_hip).
// Compiles to nothing unless HAVE_HIP is defined. Mirrors tusdr_vulkan.cc's
// compute-trace path (Path A): build a CPU BVH, upload it, trace every primary
// ray in one GPU dispatch, then shade on the CPU.
#ifdef HAVE_HIP
#include <vector>

#include "image-writer.hh"
#include "lightrt_c_hip.h"
#include "tusdr_context.hh"

namespace tusdr {

bool RunHipLightRT(const Options &opt, const std::vector<Vec3> &base_colors,
                   const std::vector<RTPreviewStats::MeshGeometry> &geos,
                   const CameraFrame &camera, int height) {
  // Flatten all meshes into a single indexed vertex/index array (same layout the
  // Vulkan backend uses).
  std::vector<float> flat_verts;
  std::vector<uint32_t> flat_idx;
  std::vector<Vec3> mesh_base_colors;
  std::vector<Vec3> mesh_normals;  // per-triangle flat normals (fallback)
  std::vector<Vec3> mesh_vn0, mesh_vn1, mesh_vn2;
  uint32_t base_idx = 0;
  for (const auto &g : geos) {
    uint32_t nv = uint32_t(g.positions.size() / 3);
    for (uint32_t j = 0; j < nv; ++j) {
      flat_verts.push_back(g.positions[j * 3 + 0]);
      flat_verts.push_back(g.positions[j * 3 + 1]);
      flat_verts.push_back(g.positions[j * 3 + 2]);
    }
    for (uint32_t j = 0; j < uint32_t(g.indices.size()); ++j) {
      flat_idx.push_back(g.indices[j] + base_idx);
    }
    const bool perVertexN = (g.normals.size() == size_t(nv) * 3u);
    auto vnorm = [&](uint32_t vi) -> Vec3 {
      return perVertexN ? Vec3{g.normals[vi * 3 + 0], g.normals[vi * 3 + 1],
                               g.normals[vi * 3 + 2]}
                        : Vec3{0, 0, 0};
    };
    for (uint32_t j = 0; j < uint32_t(g.indices.size()) / 3; ++j) {
      uint32_t i0 = g.indices[j * 3 + 0];
      uint32_t i1 = g.indices[j * 3 + 1];
      uint32_t i2 = g.indices[j * 3 + 2];
      Vec3 p0{g.positions[i0 * 3 + 0], g.positions[i0 * 3 + 1], g.positions[i0 * 3 + 2]};
      Vec3 p1{g.positions[i1 * 3 + 0], g.positions[i1 * 3 + 1], g.positions[i1 * 3 + 2]};
      Vec3 p2{g.positions[i2 * 3 + 0], g.positions[i2 * 3 + 1], g.positions[i2 * 3 + 2]};
      Vec3 e1 = Sub(p1, p0), e2 = Sub(p2, p0);
      Vec3 n = Normalize(Cross(e1, e2));
      mesh_normals.push_back(n);
      mesh_vn0.push_back(vnorm(i0));
      mesh_vn1.push_back(vnorm(i1));
      mesh_vn2.push_back(vnorm(i2));
    }
    size_t nm = (base_colors.size() > geos.size()) ? geos.size() : base_colors.size();
    Vec3 bc = (&g - &geos[0]) < nm ? base_colors[&g - &geos[0]] : Vec3{0.5f, 0.5f, 0.5f};
    for (uint32_t j = 0; j < uint32_t(g.indices.size()) / 3; ++j) {
      mesh_base_colors.push_back(bc);
    }
    base_idx += nv;
  }

  uint32_t ntris = uint32_t(flat_idx.size() / 3);
  if (ntris == 0) {
    std::cerr << "No triangles to render.\n";
    return false;
  }

  // Build the BVH scene (BVH4, matching the Vulkan compute path).
  lrt_tri_build_options bopts;
  std::memset(&bopts, 0, sizeof(bopts));
  bopts.quality = LRT_TRI_BUILD_DEFAULT;
  bopts.layout = LRT_TRI_LAYOUT_BVH4;
  bopts.num_threads = 1;

  lrt_result lrterr = LRT_RESULT_OK;
  lrt_tri_scene *scene = lrt_tri_scene_build_indexed(
      flat_verts.data(), flat_verts.size() / 3, flat_idx.data(), ntris, &bopts,
      &lrterr);
  if (!scene || lrterr != LRT_RESULT_OK) {
    std::cerr << "Failed to build LightRT scene.\n";
    return false;
  }

  // Create the HIP engine (compiles the trace kernel via hiprtc).
  lrt_hip_engine_options hopts;
  std::memset(&hopts, 0, sizeof(hopts));
  hopts.device_index = -1;
  lrt_result hiperr = LRT_RESULT_OK;
  lrt_hip_engine *hip = lrt_hip_engine_create(&hopts, &hiperr);
  if (!hip) {
    std::cerr << "HIP ray tracing unavailable (rc=" << hiperr
              << "); no ROCm device or kernel compile failed.\n";
    lrt_tri_scene_free(scene);
    return false;
  }
  std::cerr << "HIP device: " << lrt_hip_engine_device_name(hip) << "\n";

  const int w = opt.width > 0 ? opt.width : 960;
  const int h = height;
  tinyusdz::Image img;
  img.width = w;
  img.height = h;
  img.channels = 4;
  img.bpp = 8;
  img.data.resize(size_t(w) * size_t(h) * 4, 0);

  Vec3 eye = camera.origin;
  Vec3 fwd = camera.forward;
  Vec3 up = camera.up;
  Vec3 right = Normalize(Cross(fwd, up));
  Vec3 camUp = Cross(right, fwd);
  float aspect = float(w) / float(h);
  float fov = camera.yfov;
  float half_h = std::tan(fov * 0.5f);
  float half_w = half_h * aspect;
  const int spp = std::max(1, opt.samples);
  const float ambient = opt.ambient;
  const Vec3 light = Normalize(Vec3{0.5f, 0.8f, 0.6f});

  // One ray per (pixel, sample); ray index = (y*w + x)*spp + s. Each sample gets
  // a distinct Halton(2,3) sub-pixel offset so -samples actually supersamples for
  // anti-aliasing (matching the -vk/-vkr path); without this every sample reused
  // the pixel-center direction and -samples N traced N identical rays, no AA.
  const size_t nrays = size_t(w) * size_t(h) * size_t(spp);
  std::vector<lrt_ray> rays(nrays);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      size_t base = (size_t(y) * size_t(w) + size_t(x)) * size_t(spp);
      for (int s = 0; s < spp; ++s) {
        float jx, jy;
        PixelJitter(s, spp, &jx, &jy);
        float fx = (float(x) + jx) / float(w) * 2.0f - 1.0f;
        float fy = (float(y) + jy) / float(h) * 2.0f - 1.0f;
        Vec3 dir = Add(Mul(right, fx * half_w), Add(Mul(camUp, fy * half_h), fwd));
        dir = Normalize(dir);
        lrt_ray &ray = rays[base + size_t(s)];
        ray.org[0] = eye.x; ray.org[1] = eye.y; ray.org[2] = eye.z;
        ray.tmin = 0.001f;
        ray.dir[0] = dir.x; ray.dir[1] = dir.y; ray.dir[2] = dir.z;
        ray.tmax = 1.0e10f;
      }
    }
  }

  std::vector<lrt_hit> hits(nrays);
  lrt_result trerr = LRT_RESULT_OK;
  int traced = lrt_hip_trace_scene(hip, scene, rays.data(), uint32_t(nrays),
                                   hits.data(), &trerr);
  if (traced < 0) {
    std::cerr << "HIP trace failed (rc=" << trerr
              << "): " << lrt_hip_engine_last_error(hip) << "\n";
    lrt_tri_scene_free(scene);
    lrt_hip_engine_destroy(hip);
    return false;
  }

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      size_t base = (size_t(y) * size_t(w) + size_t(x)) * size_t(spp);
      Vec3 color{0, 0, 0};
      for (int s = 0; s < spp; ++s) {
        const lrt_hit &hit = hits[base + size_t(s)];
        if (hit.prim_id != 0xFFFFFFFFu && hit.prim_id < ntris) {
          Vec3 bc = hit.prim_id < mesh_base_colors.size()
                        ? mesh_base_colors[hit.prim_id]
                        : Vec3{0.5f, 0.5f, 0.5f};
          Vec3 N = mesh_normals[hit.prim_id];
          const float w0 = 1.0f - hit.u - hit.v, w1 = hit.u, w2 = hit.v;
          Vec3 sn = Add(Add(Mul(mesh_vn0[hit.prim_id], w0),
                            Mul(mesh_vn1[hit.prim_id], w1)),
                        Mul(mesh_vn2[hit.prim_id], w2));
          if (Length(sn) > 1.0e-8f) N = Normalize(sn);
          Vec3 V = Vec3{-rays[base + size_t(s)].dir[0],
                        -rays[base + size_t(s)].dir[1],
                        -rays[base + size_t(s)].dir[2]};
          if (Dot(N, V) < 0.0f) N = Mul(N, -1.0f);
          float key = std::max(0.0f, Dot(N, light));
          float head = std::max(0.0f, Dot(N, V));
          float lit = ambient + 0.8f * key + 0.35f * head;
          Vec3 shaded = Mul(bc, lit);
          color = Add(color, shaded);
        }
      }
      color = Mul(color, 1.0f / float(spp));
      size_t pi = (size_t(y) * size_t(w) + size_t(x)) * 4;
      img.data[pi + 0] = uint8_t(std::min(255.0f, color.x * 255.0f));
      img.data[pi + 1] = uint8_t(std::min(255.0f, color.y * 255.0f));
      img.data[pi + 2] = uint8_t(std::min(255.0f, color.z * 255.0f));
      img.data[pi + 3] = 255;
    }
  }

  tinyusdz::image::WriteOption wopt;
  wopt.format = tinyusdz::image::WriteImageFormat::Autodetect;
  auto ret = tinyusdz::image::WriteImageToFile(opt.output, img, wopt);
  if (!ret) {
    std::cerr << "Failed to write PNG: " << ret.error() << "\n";
    lrt_tri_scene_free(scene);
    lrt_hip_engine_destroy(hip);
    return false;
  }

  std::cerr << "triangles: " << ntris << " (" << geos.size() << " meshes)\n";
  std::cerr << "backend: LightRT HIP (compute trace)\n";
  lrt_tri_scene_free(scene);
  lrt_hip_engine_destroy(hip);
  return true;
}

}  // namespace tusdr
#endif  // HAVE_HIP
