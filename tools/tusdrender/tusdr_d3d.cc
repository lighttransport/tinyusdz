// SPDX-License-Identifier: Apache-2.0
// tusdrender — LightRT Direct3D 11 backend (GPU BVH traversal via
// lightrt_c_d3d11). Compiles to nothing unless HAVE_D3D11 is defined.
//
// Unlike tusdr_vulkan.cc (one GPU trace call per pixel), this batches every
// primary ray into a single lrt_d3d11_trace_scene() dispatch, so a full frame
// is one GPU round-trip.
#ifdef HAVE_D3D11
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

#include "image-writer.hh"
#include "lightrt_c_d3d11.h"
#include "tusdr_context.hh"

namespace tusdr {

bool RunD3D11LightRT(const Options &opt, const std::vector<Vec3> &base_colors,
                     const std::vector<RTPreviewStats::MeshGeometry> &geos,
                     const CameraFrame &camera, int height) {
  // Flatten all meshes into one indexed vertex/index array + per-triangle
  // shading data (matches tusdr_vulkan.cc).
  std::vector<float> flat_verts;
  std::vector<uint32_t> flat_idx;
  std::vector<Vec3> tri_base_colors;
  std::vector<Vec3> tri_normals;
  uint32_t base_idx = 0;
  for (const auto &g : geos) {
    uint32_t nv = uint32_t(g.positions.size() / 3);
    for (uint32_t j = 0; j < nv; ++j) {
      flat_verts.push_back(g.positions[j * 3 + 0]);
      flat_verts.push_back(g.positions[j * 3 + 1]);
      flat_verts.push_back(g.positions[j * 3 + 2]);
    }
    for (uint32_t j = 0; j < uint32_t(g.indices.size()); ++j)
      flat_idx.push_back(g.indices[j] + base_idx);
    for (uint32_t j = 0; j < uint32_t(g.indices.size()) / 3; ++j) {
      uint32_t i0 = g.indices[j * 3 + 0], i1 = g.indices[j * 3 + 1],
               i2 = g.indices[j * 3 + 2];
      Vec3 p0{g.positions[i0 * 3 + 0], g.positions[i0 * 3 + 1], g.positions[i0 * 3 + 2]};
      Vec3 p1{g.positions[i1 * 3 + 0], g.positions[i1 * 3 + 1], g.positions[i1 * 3 + 2]};
      Vec3 p2{g.positions[i2 * 3 + 0], g.positions[i2 * 3 + 1], g.positions[i2 * 3 + 2]};
      tri_normals.push_back(Normalize(Cross(Sub(p1, p0), Sub(p2, p0))));
    }
    size_t nm = std::min(base_colors.size(), geos.size());
    Vec3 bc = (size_t(&g - &geos[0]) < nm) ? base_colors[&g - &geos[0]]
                                           : Vec3{0.5f, 0.5f, 0.5f};
    for (uint32_t j = 0; j < uint32_t(g.indices.size()) / 3; ++j)
      tri_base_colors.push_back(bc);
    base_idx += nv;
  }

  uint32_t ntris = uint32_t(flat_idx.size() / 3);
  if (ntris == 0) {
    std::cerr << "No triangles to render.\n";
    return false;
  }

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

  lrt_result d3derr = LRT_RESULT_OK;
  lrt_d3d11_engine *dev = lrt_d3d11_engine_create(/*prefer_discrete=*/1, &d3derr);
  if (!dev) {
    std::cerr << "Failed to create LightRT Direct3D 11 engine.\n";
    lrt_tri_scene_free(scene);
    return false;
  }
  std::cerr << "Direct3D 11 device: " << lrt_d3d11_engine_device_name(dev) << "\n";

  const int w = opt.width > 0 ? opt.width : 960;
  const int h = height;
  const int spp = std::max(1, opt.samples);

  // Camera basis.
  Vec3 eye = camera.origin, fwd = camera.forward, up = camera.up;
  Vec3 right = Normalize(Cross(fwd, up));
  Vec3 camUp = Cross(right, fwd);
  float aspect = float(w) / float(h);
  float half_h = std::tan(camera.yfov * 0.5f);
  float half_w = half_h * aspect;

  // Build every primary ray for the frame (spp samples per pixel), then trace
  // them all in one GPU dispatch.
  const size_t npix = size_t(w) * size_t(h);
  const size_t nrays = npix * size_t(spp);
  std::vector<lrt_ray> rays(nrays);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      for (int s = 0; s < spp; ++s) {
        float fx = (float(x) + 0.5f) / float(w) * 2.0f - 1.0f;
        float fy = (float(y) + 0.5f) / float(h) * 2.0f - 1.0f;
        Vec3 dir = Normalize(
            Add(Mul(right, fx * half_w), Add(Mul(camUp, fy * half_h), fwd)));
        lrt_ray &r = rays[(size_t(y) * w + x) * spp + s];
        r.org[0] = eye.x; r.org[1] = eye.y; r.org[2] = eye.z; r.tmin = 0.001f;
        r.dir[0] = dir.x; r.dir[1] = dir.y; r.dir[2] = dir.z; r.tmax = 1.0e10f;
      }
    }
  }

  std::vector<lrt_hit> hits(nrays);
  lrt_result trerr = LRT_RESULT_OK;
  int traced = lrt_d3d11_trace_scene(dev, scene, rays.data(),
                                     uint32_t(nrays), hits.data(), &trerr);
  if (traced < 0) {
    std::cerr << "Direct3D 11 trace failed: "
              << lrt_d3d11_engine_last_error(dev) << "\n";
    lrt_d3d11_engine_destroy(dev);
    lrt_tri_scene_free(scene);
    return false;
  }

  // Shade.
  tinyusdz::Image img;
  img.width = w; img.height = h; img.channels = 4; img.bpp = 8;
  img.data.resize(npix * 4, 0);
  const float ambient = opt.ambient;
  const Vec3 L = Normalize(Vec3{0.5f, 0.8f, 0.6f});
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      Vec3 color{0, 0, 0};
      for (int s = 0; s < spp; ++s) {
        const lrt_hit &hit = hits[(size_t(y) * w + x) * spp + s];
        if (hit.prim_id != LRT_TRI_NO_HIT && hit.prim_id < ntris) {
          Vec3 bc = hit.prim_id < tri_base_colors.size()
                        ? tri_base_colors[hit.prim_id] : Vec3{0.5f, 0.5f, 0.5f};
          Vec3 N = hit.prim_id < tri_normals.size()
                       ? tri_normals[hit.prim_id] : Vec3{0, 1, 0};
          float diff = std::max(0.0f, Dot(N, L));
          color = Add(color, Add(Mul(bc, diff), Mul(bc, ambient)));
        }
      }
      color = Mul(color, 1.0f / float(spp));
      size_t pi = (size_t(y) * w + x) * 4;
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
    lrt_d3d11_engine_destroy(dev);
    lrt_tri_scene_free(scene);
    return false;
  }

  std::cerr << "triangles: " << ntris << " (" << geos.size() << " meshes)\n";
  std::cerr << "backend: LightRT D3D11 (compute trace, " << nrays
            << " rays in 1 dispatch)\n";
  lrt_d3d11_engine_destroy(dev);
  lrt_tri_scene_free(scene);
  return true;
}

}  // namespace tusdr
#endif  // HAVE_D3D11
