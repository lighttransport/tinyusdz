// SPDX-License-Identifier: Apache-2.0
// tusdrender — LightRT Vulkan backend (GPU BVH traversal via lightrt_c_vk).
// Compiles to nothing unless HAVE_VULKAN is defined. Geometry flatten, ray
// generation and shading are shared with the HIP backend (tusdr_gpu_common).
#ifdef HAVE_VULKAN
#include <vector>

#include "lightrt_c_vk.h"
#include "tusdr_context.hh"
#include "tusdr_gpu_common.hh"

namespace tusdr {

bool RunVulkanLightRT(const Options &opt, const std::vector<Vec3> &base_colors,
                      const std::vector<RTPreviewStats::MeshGeometry> &geos,
                      const CameraFrame &camera, int height) {
  GpuTriScene s;
  if (!BuildGpuTriScene(base_colors, geos, &s)) return false;

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
    lrt_tri_scene_free(s.scene);
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

  // Generate every primary ray and trace the whole frame in ONE batched GPU
  // dispatch (the -vkr ray-query AS is built once, not per pixel).
  const int w = opt.width > 0 ? opt.width : 960;
  const int h = height;
  const int spp = std::max(1, opt.samples);
  std::vector<lrt_ray> rays;
  GenerateCameraRays(camera, w, h, spp, &rays);

  std::vector<lrt_hit> hits(rays.size());
  lrt_result trerr = LRT_RESULT_OK;
  int traced = -1;
  if (has_rt && opt.vulkan_rt) {
    // Build the ray-query acceleration structure directly from the indexed mesh
    // (the GPU builds a VK_INDEX_TYPE_UINT32 BLAS, so no de-indexing needed);
    // primitiveIndex == triangle build order == caller index, matching the
    // CPU/compute paths. Build once, trace the whole frame, free.
    uint32_t nverts = uint32_t(s.flat_verts.size() / 3u);
    lrt_vk_rtx_scene *rtx = lrt_vk_rtx_scene_build_indexed(
        vk, s.flat_verts.data(), nverts, s.flat_idx.data(), s.ntris, &trerr);
    if (rtx) {
      traced = lrt_vk_rtx_scene_trace(vk, rtx, rays.data(), uint32_t(rays.size()),
                                      hits.data(), &trerr);
      lrt_vk_rtx_scene_free(vk, rtx);
    }
  } else {
    traced = lrt_vk_trace_scene(vk, s.scene, rays.data(), uint32_t(rays.size()),
                                hits.data(), &trerr);
  }
  if (traced < 0) {
    std::cerr << "Vulkan trace failed (rc=" << trerr << ").\n";
    lrt_tri_scene_free(s.scene);
    lrt_vk_engine_destroy(vk);
    return false;
  }

  bool ok = ShadeAndWriteImage(opt, s, rays, hits, w, h, spp);
  if (ok) {
    std::cerr << "triangles: " << s.ntris << " (" << geos.size() << " meshes)\n";
    std::cerr << "backend: LightRT VK ("
              << (has_rt && opt.vulkan_rt ? "ray_query" : "compute trace") << ")\n";
  }
  lrt_tri_scene_free(s.scene);
  lrt_vk_engine_destroy(vk);
  return ok;
}

bool RunVulkanLightRTInstanced(const Options &opt, GpuInstancedScene &scene,
                               const CameraFrame &camera, int height) {
  if (scene.protos.empty() || scene.insts.empty()) return false;

  lrt_vk_engine_options vopts;
  std::memset(&vopts, 0, sizeof(vopts));
  vopts.device_index = -1;
  vopts.prefer_discrete = 1;
  vopts.want_ray_tracing = 1;
  lrt_result vkerr = LRT_RESULT_OK;
  lrt_vk_engine *vk = lrt_vk_engine_create(&vopts, &vkerr);
  if (!vk) {
    std::cerr << "Failed to create LightRT Vulkan engine.\n";
    return false;
  }
  uint32_t vk_caps = lrt_vk_engine_caps(vk);
  if (!(vk_caps & LRT_VK_CAP_RAY_QUERY)) {
    std::cerr << "-vkInstanced needs ray query; falling back to the flat path.\n";
    lrt_vk_engine_destroy(vk);
    return false;
  }
  std::cerr << "Vulkan device: " << lrt_vk_engine_device_name(vk) << "\n";

  // Marshal the prototype + instance lists for the C API (views into `scene`).
  std::vector<lrt_vk_proto> cprotos(scene.protos.size());
  uint64_t unique_tris = 0;
  for (size_t p = 0; p < scene.protos.size(); ++p) {
    const GpuInstProto &pr = scene.protos[p];
    cprotos[p].vertices = pr.verts.data();
    cprotos[p].nverts = uint32_t(pr.verts.size() / 3u);
    cprotos[p].indices = pr.idx.data();
    cprotos[p].ntris = pr.ntris;
    unique_tris += pr.ntris;
  }
  std::vector<lrt_vk_instance> cinsts(scene.insts.size());
  uint64_t placed_tris = 0;
  for (size_t i = 0; i < scene.insts.size(); ++i) {
    std::memcpy(cinsts[i].transform, scene.insts[i].o2w, 12u * sizeof(float));
    cinsts[i].proto = scene.insts[i].proto;
    placed_tris += scene.protos[scene.insts[i].proto].ntris;
  }

  // Pick the hit encoding: the narrow (4-word) trace packs the hit as
  // instanceId*stride + localTri, which must fit 32 bits (ninsts*maxProtoTris <
  // 2^32). When it would overflow (Moana-island scale, a large prototype poisons
  // stride), use the wide (5-word) trace that stores instanceId + localTri
  // separately -- no product to overflow, only the TLAS maxInstanceCount ceiling.
  uint32_t max_ntris = 1;
  for (const lrt_vk_proto &pr : cprotos)
    max_ntris = std::max(max_ntris, pr.ntris);
  bool wide = uint64_t(cinsts.size()) * uint64_t(max_ntris) >= 0xFFFFFFFFull;
  // Debug: force the wide encoding on any scene to validate the wide path against
  // the narrow one (they must be pixel-identical).
  if (std::getenv("TUSDR_FORCE_WIDE")) wide = true;

  const int w = opt.width > 0 ? opt.width : 960;
  const int h = height;
  const int spp = std::max(1, opt.samples);
  std::vector<lrt_ray> rays;
  GenerateCameraRays(camera, w, h, spp, &rays);
  std::vector<InstSampleHit> decoded(rays.size());

  lrt_result builderr = LRT_RESULT_OK, trerr = LRT_RESULT_OK;
  int traced = -1;
  if (wide) {
    lrt_vk_rtx_scene *rtx = lrt_vk_rtx_scene_build_instanced_wide(
        vk, cprotos.data(), uint32_t(cprotos.size()), cinsts.data(),
        uint32_t(cinsts.size()), &builderr);
    if (!rtx) {
      std::cerr << "-vkInstanced wide build failed (rc=" << builderr
                << "); falling back to the flat path.\n";
      lrt_vk_engine_destroy(vk);
      return false;
    }
    scene.stride = max_ntris;  // reported only; wide decode does not use it
    std::vector<lrt_hit_wide> hits(rays.size());
    traced = lrt_vk_rtx_scene_trace_wide(vk, rtx, rays.data(),
                                         uint32_t(rays.size()), hits.data(), &trerr);
    lrt_vk_rtx_scene_free(vk, rtx);
    if (traced >= 0)
      for (size_t i = 0; i < hits.size(); ++i) {
        const lrt_hit_wide &hd = hits[i];
        decoded[i].valid = hd.inst != 0xFFFFFFFFu;
        decoded[i].inst = hd.inst;
        decoded[i].local = hd.local;
        decoded[i].u = hd.u;
        decoded[i].v = hd.v;
      }
  } else {
    uint32_t stride = 0;
    lrt_vk_rtx_scene *rtx = lrt_vk_rtx_scene_build_instanced(
        vk, cprotos.data(), uint32_t(cprotos.size()), cinsts.data(),
        uint32_t(cinsts.size()), &stride, &builderr);
    if (!rtx) {
      std::cerr << "-vkInstanced build failed (rc=" << builderr
                << "); falling back to the flat path.\n";
      lrt_vk_engine_destroy(vk);
      return false;
    }
    scene.stride = stride;
    std::vector<lrt_hit> hits(rays.size());
    traced = lrt_vk_rtx_scene_trace(vk, rtx, rays.data(), uint32_t(rays.size()),
                                    hits.data(), &trerr);
    lrt_vk_rtx_scene_free(vk, rtx);
    if (traced >= 0)
      for (size_t i = 0; i < hits.size(); ++i) {
        const lrt_hit &hd = hits[i];
        decoded[i].valid = hd.prim_id != 0xFFFFFFFFu && stride != 0;
        if (decoded[i].valid) {
          decoded[i].inst = hd.prim_id / stride;
          decoded[i].local = hd.prim_id % stride;
        }
        decoded[i].u = hd.u;
        decoded[i].v = hd.v;
      }
  }
  if (traced < 0) {
    std::cerr << "Vulkan instanced trace failed (rc=" << trerr << ").\n";
    lrt_vk_engine_destroy(vk);
    return false;
  }

  bool ok = ShadeAndWriteImageInstanced(opt, scene, rays, decoded, w, h, spp);
  if (ok) {
    std::cerr << "backend: LightRT VK (ray_query, two-level TLAS, "
              << (wide ? "wide 64-bit hit id" : "32-bit hit id") << ")\n";
    std::cerr << "instanced: " << scene.protos.size() << " prototypes ("
              << unique_tris << " unique tris) x " << scene.insts.size()
              << " instances = " << placed_tris << " placed tris; BLAS memory "
              << "stores " << unique_tris << " (vs " << placed_tris
              << " flattened)\n";
  }
  lrt_vk_engine_destroy(vk);
  return ok;
}

}  // namespace tusdr
#endif  // HAVE_VULKAN
