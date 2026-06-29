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

}  // namespace tusdr
#endif  // HAVE_VULKAN
