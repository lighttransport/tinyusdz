// SPDX-License-Identifier: Apache-2.0
// tusdrender — LightRT HIP/ROCm backend (GPU BVH traversal via lightrt_c_hip).
// Compiles to nothing unless HAVE_HIP is defined. Geometry flatten, ray
// generation and shading are shared with the Vulkan backend (tusdr_gpu_common):
// build a CPU BVH, upload it, trace every primary ray in one GPU dispatch, then
// shade on the CPU.
#ifdef HAVE_HIP
#include <vector>

#include "lightrt_c_hip.h"
#include "tusdr_context.hh"
#include "tusdr_gpu_common.hh"

namespace tusdr {

bool RunHipLightRT(const Options &opt, const std::vector<Vec3> &base_colors,
                   const std::vector<RTPreviewStats::MeshGeometry> &geos,
                   const CameraFrame &camera, int height) {
  GpuTriScene s;
  if (!BuildGpuTriScene(opt, base_colors, geos, &s)) return false;

  // Create the HIP engine (compiles the trace kernel via hiprtc).
  lrt_hip_engine_options hopts;
  std::memset(&hopts, 0, sizeof(hopts));
  hopts.device_index = -1;
  lrt_result hiperr = LRT_RESULT_OK;
  lrt_hip_engine *hip = lrt_hip_engine_create(&hopts, &hiperr);
  if (!hip) {
    std::cerr << "HIP ray tracing unavailable (rc=" << hiperr
              << "); no ROCm device or kernel compile failed.\n";
    lrt_tri_scene_free(s.scene);
    return false;
  }
  std::cerr << "HIP device: " << lrt_hip_engine_device_name(hip) << "\n";

  const int w = opt.width > 0 ? opt.width : 960;
  const int h = height;
  const int spp = std::max(1, opt.samples);
  std::vector<lrt_ray> rays;
  GenerateCameraRays(camera, w, h, spp, &rays);

  std::vector<lrt_hit> hits(rays.size());
  lrt_result trerr = LRT_RESULT_OK;
  int traced = lrt_hip_trace_scene(hip, s.scene, rays.data(),
                                   uint32_t(rays.size()), hits.data(), &trerr);
  if (traced < 0) {
    std::cerr << "HIP trace failed (rc=" << trerr
              << "): " << lrt_hip_engine_last_error(hip) << "\n";
    lrt_tri_scene_free(s.scene);
    lrt_hip_engine_destroy(hip);
    return false;
  }

  bool ok = ShadeAndWriteImage(opt, s, rays, hits, w, h, spp);
  if (ok) {
    std::cerr << "triangles: " << s.ntris << " (" << geos.size() << " meshes)\n";
    std::cerr << "backend: LightRT HIP (compute trace)\n";
  }
  lrt_tri_scene_free(s.scene);
  lrt_hip_engine_destroy(hip);
  return ok;
}

}  // namespace tusdr
#endif  // HAVE_HIP
