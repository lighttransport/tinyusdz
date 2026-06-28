// SPDX-License-Identifier: Apache-2.0
// tusdrender — LightRT Vulkan backend (GPU BVH traversal via lightrt_c_vk).
// Compiles to nothing unless HAVE_VULKAN is defined.
#ifdef HAVE_VULKAN
#include <vector>

#include "image-writer.hh"
#include "lightrt_c_vk.h"
#include "tusdr_context.hh"

namespace tusdr {

bool RunVulkanLightRT(const Options &opt,
                              const std::vector<Vec3> &base_colors,
                              const std::vector<RTPreviewStats::MeshGeometry> &geos,
                              const CameraFrame &camera, int height) {
  // Build an lrt_tri_scene from the geometry.
  // Flatten all meshes into a single vertex/index array that LightRT expects.
  std::vector<float> flat_verts;
  std::vector<uint32_t> flat_idx;
  std::vector<Vec3> mesh_base_colors;
  std::vector<Vec3> mesh_normals;  // per-triangle flat normals for shading
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
    // Store per-triangle shading data.
    for (uint32_t j = 0; j < uint32_t(g.indices.size()) / 3; ++j) {
      uint32_t i0 = g.indices[j * 3 + 0];
      uint32_t i1 = g.indices[j * 3 + 1];
      uint32_t i2 = g.indices[j * 3 + 2];
      // Face normal.
      Vec3 p0{g.positions[i0 * 3 + 0], g.positions[i0 * 3 + 1], g.positions[i0 * 3 + 2]};
      Vec3 p1{g.positions[i1 * 3 + 0], g.positions[i1 * 3 + 1], g.positions[i1 * 3 + 2]};
      Vec3 p2{g.positions[i2 * 3 + 0], g.positions[i2 * 3 + 1], g.positions[i2 * 3 + 2]};
      Vec3 e1 = Sub(p1, p0), e2 = Sub(p2, p0);
      Vec3 n = Normalize(Cross(e1, e2));
      mesh_normals.push_back(n);
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

  // Build the BVH scene.
  lrt_tri_build_options bopts;
  std::memset(&bopts, 0, sizeof(bopts));
  bopts.quality = LRT_TRI_BUILD_DEFAULT;
  bopts.layout = LRT_TRI_LAYOUT_BVH4;
  bopts.num_threads = 1;

  lrt_result lrterr = LRT_RESULT_OK;
  // Indexed build: flat_verts holds the unique vertices and flat_idx the 3*ntris
  // vertex ids. (The plain lrt_tri_scene_build expects a de-indexed 9*ntris soup,
  // so passing the unique-vertex array there over-reads and fails the build.)
  lrt_tri_scene *scene = lrt_tri_scene_build_indexed(
      flat_verts.data(), flat_verts.size() / 3, flat_idx.data(), ntris, &bopts,
      &lrterr);
  if (!scene || lrterr != LRT_RESULT_OK) {
    std::cerr << "Failed to build LightRT scene.\n";
    return false;
  }

  // Create the Vulkan engine.
  lrt_vk_engine_options vopts;
  std::memset(&vopts, 0, sizeof(vopts));
  vopts.device_index = -1;
  vopts.prefer_discrete = 1;
  vopts.want_ray_tracing = opt.vulkan_rt;
  lrt_result vkerr = LRT_RESULT_OK;
  lrt_vk_engine *vk = lrt_vk_engine_create(&vopts, &vkerr);
  if (!vk) {
    std::cerr << "Failed to create LightRT Vulkan engine.\n";
    lrt_tri_scene_free(scene);
    return false;
  }

  uint32_t vk_caps = lrt_vk_engine_caps(vk);
  bool has_rt = (vk_caps & LRT_VK_CAP_RAY_QUERY) != 0;
  std::cerr << "Vulkan device: " << lrt_vk_engine_device_name(vk) << "\n";
  std::cerr << "Vulkan caps: compute=1"
            << ((vk_caps & LRT_VK_CAP_BUFFER_ADDRESS) ? " buf_addr" : "")
            << ((vk_caps & LRT_VK_CAP_ACCEL_STRUCT) ? " accel" : "")
            << ((vk_caps & LRT_VK_CAP_RAY_QUERY) ? " ray_query" : "")
            << "\n";

  // Render — generate every primary ray, trace the whole frame in ONE batched
  // GPU dispatch, then shade. (The previous code traced one ray per GPU submit
  // and, on the ray-query path, rebuilt the acceleration structure per pixel —
  // correct in spirit but minutes-slow and, for -vkr, fed the wrong vertices.)
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
  const float light_dir[3] = {0.5f, 0.8f, 0.6f};

  // One ray per (pixel, sample); ray index = (y*w + x)*spp + s.
  const size_t nrays = size_t(w) * size_t(h) * size_t(spp);
  std::vector<lrt_ray> rays(nrays);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      float fx = (float(x) + 0.5f) / float(w) * 2.0f - 1.0f;
      float fy = (float(y) + 0.5f) / float(h) * 2.0f - 1.0f;
      Vec3 dir = Add(Mul(right, fx * half_w), Add(Mul(camUp, fy * half_h), fwd));
      dir = Normalize(dir);
      size_t base = (size_t(y) * size_t(w) + size_t(x)) * size_t(spp);
      for (int s = 0; s < spp; ++s) {
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
  int traced = -1;
  if (has_rt && opt.vulkan_rt) {
    // The ray-query BLAS expects a de-indexed triangle soup (9 floats/tri, no
    // index buffer: primitiveIndex == build order == caller triangle index), but
    // flat_verts is the *indexed* unique-vertex array. Expand it once so the GPU
    // builds the same geometry the CPU/compute paths trace.
    std::vector<float> soup;
    soup.reserve(size_t(ntris) * 9u);
    for (uint32_t t = 0; t < ntris; ++t) {
      for (int k = 0; k < 3; ++k) {
        uint32_t vi = flat_idx[size_t(t) * 3u + size_t(k)];
        soup.push_back(flat_verts[size_t(vi) * 3u + 0u]);
        soup.push_back(flat_verts[size_t(vi) * 3u + 1u]);
        soup.push_back(flat_verts[size_t(vi) * 3u + 2u]);
      }
    }
    // Build the acceleration structure once, trace every ray, free.
    lrt_vk_rtx_scene *rtx = lrt_vk_rtx_scene_build(vk, soup.data(), ntris, &trerr);
    if (rtx) {
      traced = lrt_vk_rtx_scene_trace(vk, rtx, rays.data(), uint32_t(nrays),
                                      hits.data(), &trerr);
      lrt_vk_rtx_scene_free(vk, rtx);
    }
  } else {
    traced = lrt_vk_trace_scene(vk, scene, rays.data(), uint32_t(nrays),
                                hits.data(), &trerr);
  }
  if (traced < 0) {
    std::cerr << "Vulkan trace failed (rc=" << trerr << ").\n";
    lrt_tri_scene_free(scene);
    lrt_vk_engine_destroy(vk);
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
          Vec3 N = hit.prim_id < mesh_normals.size()
                       ? mesh_normals[hit.prim_id]
                       : Vec3{0, 1, 0};
          float diff = std::max(0.0f, Dot(N, Vec3{light_dir[0], light_dir[1], light_dir[2]}));
          Vec3 shaded = Add(Mul(bc, diff), Mul(bc, ambient));
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

  // Write PNG.
  tinyusdz::image::WriteOption wopt;
  wopt.format = tinyusdz::image::WriteImageFormat::Autodetect;
  auto ret = tinyusdz::image::WriteImageToFile(opt.output, img, wopt);
  if (!ret) {
    std::cerr << "Failed to write PNG: " << ret.error() << "\n";
    lrt_tri_scene_free(scene);
    lrt_vk_engine_destroy(vk);
    return false;
  }

  std::cerr << "triangles: " << ntris << " (" << geos.size() << " meshes)\n";
  std::cerr << "backend: LightRT VK ("
            << (has_rt && opt.vulkan_rt ? "ray_query" : "compute trace")
            << ")\n";
  lrt_tri_scene_free(scene);
  lrt_vk_engine_destroy(vk);
  return true;
}

}  // namespace tusdr
#endif  // HAVE_VULKAN
