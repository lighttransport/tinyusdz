// SPDX-License-Identifier: Apache-2.0
// tusdrender — LightRT Vulkan backend (GPU BVH traversal via lightrt_c_vk).
// Compiles to nothing unless HAVE_VULKAN is defined. Geometry flatten, ray
// generation and shading are shared with the HIP backend (tusdr_gpu_common).
#ifdef HAVE_VULKAN
#include <chrono>
#include <vector>

#include "lightrt_c_vk.h"
#include "tusdr_context.hh"
#include "tusdr_gpu_common.hh"

namespace tusdr {

namespace {

// Seconds since `t0`, for the -stats per-stage timing lines.
double SecsSince(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
      .count();
}

size_t GpuTriangleCount(
    const std::vector<RTPreviewStats::MeshGeometry> &geos) {
  size_t n = 0;
  for (const auto &geo : geos) n += geo.indices.size() / 3u;
  return n;
}

size_t GpuChunkLimit() {
  size_t limit = size_t(262144);
  if (const char *s = std::getenv("TUSDR_GPU_TRIANGLE_CHUNK")) {
    char *end = nullptr;
    const unsigned long long n = std::strtoull(s, &end, 10);
    if (end != s && n > 0) limit = static_cast<size_t>(n);
  }
  return limit;
}

bool RunVulkanLightRTChunked(
    const Options &opt, const std::vector<Vec3> &base_colors,
    std::vector<RTPreviewStats::MeshGeometry> &geos,
    const CameraFrame &camera, int height, size_t chunk_limit) {
  lrt_vk_engine_options vopts;
  std::memset(&vopts, 0, sizeof(vopts));
  vopts.device_index = -1;
  vopts.prefer_discrete = 1;
  vopts.want_ray_tracing = opt.vulkan_rt;
  lrt_result vkerr = LRT_RESULT_OK;
  lrt_vk_engine *vk = lrt_vk_engine_create(&vopts, &vkerr);
  if (!vk) {
    std::cerr << "Failed to create LightRT Vulkan engine for mesh chunks (err="
              << int(vkerr) << ").\n";
    return false;
  }
  const uint32_t caps = lrt_vk_engine_caps(vk);
  bool use_hw_rt = (caps & LRT_VK_CAP_RAY_QUERY) != 0 && opt.vulkan_rt;
  std::cerr << "Vulkan device: " << lrt_vk_engine_device_name(vk) << " ("
            << (lrt_vk_device_local_bytes(1) >> 20)
            << " MiB device-local), mesh chunks=" << chunk_limit << " tris\n";
  const int w = opt.width > 0 ? opt.width : 960;
  const int h = height > 0 ? height : 540;
  const int spp = std::max(1, opt.samples);
  size_t nrays = 0;
  if (!ValidateGpuFrameSize(w, h, spp, "Vulkan", &nrays)) {
    lrt_vk_engine_destroy(vk);
    return false;
  }
  std::vector<lrt_ray> rays;
  GenerateCameraRays(camera, w, h, spp, &rays);
  std::vector<lrt_hit> hits(nrays), chunk_hits(nrays);
  for (lrt_hit &hit : hits) {
    hit.t = std::numeric_limits<float>::max();
    hit.u = hit.v = 0.0f;
    hit.prim_id = LRT_TRI_NO_HIT;
  }
  GpuTriScene shade;
  shade.ntris = static_cast<uint32_t>(GpuTriangleCount(geos));
  size_t mesh = 0, tri = 0, global_first = 0, chunk_count = 0;
  double flatten_s = 0.0, trace_s = 0.0, as_s = 0.0;
  bool used_hw_rt = false, fell_back = false;
  const bool force_vkr_fallback = std::getenv("TUSDR_FORCE_VKR_FALLBACK") != nullptr;
  while (mesh < geos.size()) {
    std::vector<Vec3> chunk_colors;
    std::vector<RTPreviewStats::MeshGeometry> chunk_geos;
    size_t chunk_tris = 0;
    if (!BuildGpuTriChunk(base_colors, geos, chunk_limit, &mesh, &tri,
                           &chunk_colors, &chunk_geos, &chunk_tris)) {
      lrt_vk_engine_destroy(vk);
      return false;
    }
    GpuTriScene scene;
    auto t0 = std::chrono::steady_clock::now();
    if (!BuildGpuTriScene(chunk_colors, chunk_geos, opt.threads,
                          !use_hw_rt, &scene, opt.quality)) {
      lrt_vk_engine_destroy(vk);
      return false;
    }
    flatten_s += SecsSince(t0);
    shade.base_colors.insert(shade.base_colors.end(), scene.base_colors.begin(),
                             scene.base_colors.end());
    shade.normals.insert(shade.normals.end(), scene.normals.begin(),
                         scene.normals.end());
    AppendGpuSmoothNormals(scene, &shade);
    std::fill(chunk_hits.begin(), chunk_hits.end(), lrt_hit{});
    for (lrt_hit &hit : chunk_hits) {
      hit.t = std::numeric_limits<float>::max();
      hit.prim_id = LRT_TRI_NO_HIT;
    }
    lrt_result trace_err = LRT_RESULT_OK;
    int traced = -1;
    if (use_hw_rt) {
      auto as_t0 = std::chrono::steady_clock::now();
      lrt_vk_rtx_scene *rtx = lrt_vk_rtx_scene_build_indexed(
          vk, scene.flat_verts.data(), uint32_t(scene.flat_verts.size() / 3u),
          scene.flat_idx.data(), scene.ntris, &trace_err);
      as_s += SecsSince(as_t0);
      if (rtx) {
        auto trace_t0 = std::chrono::steady_clock::now();
        if (!force_vkr_fallback)
          traced = lrt_vk_rtx_scene_trace(vk, rtx, rays.data(), uint32_t(nrays),
                                          chunk_hits.data(), &trace_err);
        lrt_vk_rtx_scene_free(vk, rtx);
        trace_s += SecsSince(trace_t0);
      }
      if (traced < 0) {
        std::cerr << "Vulkan mesh chunk ray-query failed at [" << global_first
                  << ", " << (global_first + chunk_tris)
                  << "]; switching remaining chunks to compute trace.\n";
        use_hw_rt = false;
        fell_back = true;
        if (!BuildGpuCpuScene(opt.threads, &scene, opt.quality)) {
          lrt_vk_engine_destroy(vk);
          return false;
        }
      } else {
        used_hw_rt = true;
      }
    }
    if (!use_hw_rt) {
      auto trace_t0 = std::chrono::steady_clock::now();
      traced = lrt_vk_trace_scene(vk, scene.scene, rays.data(), uint32_t(nrays),
                                  chunk_hits.data(), &trace_err);
      trace_s += SecsSince(trace_t0);
    }
    if (traced < 0) {
      std::cerr << "Vulkan mesh chunk trace failed [" << global_first << ", "
                << (global_first + chunk_tris) << "] (err=" << int(trace_err)
                << "): " << lrt_vk_engine_last_error(vk) << "\n";
      if (scene.scene) lrt_tri_scene_free(scene.scene);
      lrt_vk_engine_destroy(vk);
      return false;
    }
    for (size_t i = 0; i < nrays; ++i) {
      if (chunk_hits[i].prim_id != LRT_TRI_NO_HIT &&
          chunk_hits[i].t < hits[i].t) {
        hits[i] = chunk_hits[i];
        hits[i].prim_id += static_cast<uint32_t>(global_first);
      }
    }
    if (scene.scene) lrt_tri_scene_free(scene.scene);
    global_first += chunk_tris;
    ++chunk_count;
  }
  const bool ok = ShadeAndWriteImage(opt, shade, rays, hits, w, h, spp);
  if (opt.stats) {
    std::cerr << "[gpu-stats] mesh chunks " << chunk_count
              << ", flatten+bvh " << flatten_s << " s, as-build " << as_s
              << " s, trace " << trace_s << " s\n";
  }
  if (ok) {
    std::cerr << "triangles: " << shade.ntris << " (" << geos.size()
              << " meshes, " << chunk_count << " GPU chunks)\n";
    std::cerr << "backend: LightRT VK ("
              << (used_hw_rt ? "ray_query" : "compute trace")
              << (fell_back ? ", ray-query fallback" : "") << ")\n";
  }
  lrt_vk_engine_destroy(vk);
  return ok;
}

}  // namespace

bool RunVulkanLightRT(const Options &opt, const std::vector<Vec3> &base_colors,
                      std::vector<RTPreviewStats::MeshGeometry> &geos,
                      const CameraFrame &camera, int height) {
  const size_t total_triangles = GpuTriangleCount(geos);
  const size_t chunk_limit = GpuChunkLimit();
  if (total_triangles > chunk_limit) {
    return RunVulkanLightRTChunked(opt, base_colors, geos, camera, height,
                                   chunk_limit);
  }
  // Create the Vulkan engine first: whether the device traces via hardware ray
  // query decides if the CPU BVH is needed at all (the -vkr AS is GPU-built
  // from the indexed mesh, so the CPU build would be pure waste there).
  lrt_vk_engine_options vopts;
  std::memset(&vopts, 0, sizeof(vopts));
  vopts.device_index = -1;
  vopts.prefer_discrete = 1;
  vopts.want_ray_tracing = opt.vulkan_rt;
  lrt_result vkerr = LRT_RESULT_OK;
  lrt_vk_engine *vk = lrt_vk_engine_create(&vopts, &vkerr);
  if (!vk) {
    std::cerr << "Failed to create LightRT Vulkan engine.\n";
    return false;
  }

  uint32_t vk_caps = lrt_vk_engine_caps(vk);
  bool has_rt = (vk_caps & LRT_VK_CAP_RAY_QUERY) != 0;
  const bool use_hw_rt = has_rt && opt.vulkan_rt;
  std::cerr << "Vulkan device: " << lrt_vk_engine_device_name(vk) << " ("
            << (lrt_vk_device_local_bytes(1) >> 20) << " MiB device-local)\n";
  std::cerr << "Vulkan caps: compute=1"
            << ((vk_caps & LRT_VK_CAP_BUFFER_ADDRESS) ? " buf_addr" : "")
            << ((vk_caps & LRT_VK_CAP_ACCEL_STRUCT) ? " accel" : "")
            << ((vk_caps & LRT_VK_CAP_RAY_QUERY) ? " ray_query" : "")
            << "\n";

  GpuTriScene s;
  auto t0 = std::chrono::steady_clock::now();
  if (!BuildGpuTriScene(base_colors, geos, opt.threads, !use_hw_rt, &s,
                        opt.quality)) {
    lrt_vk_engine_destroy(vk);
    return false;
  }
  const double flatten_s = SecsSince(t0);
  for (RTPreviewStats::MeshGeometry &geo : geos) {
    std::vector<float>().swap(geo.positions);
    std::vector<float>().swap(geo.normals);
    std::vector<float>().swap(geo.uvs);
    std::vector<uint32_t>().swap(geo.indices);
  }

  // Generate every primary ray and trace the whole frame in ONE batched GPU
  // dispatch (the -vkr ray-query AS is built once, not per pixel).
  const int w = opt.width > 0 ? opt.width : 960;
  const int h = height;
  const int spp = std::max(1, opt.samples);
  size_t nrays = 0;
  if (!ValidateGpuFrameSize(w, h, spp, "Vulkan", &nrays)) {
    if (s.scene) lrt_tri_scene_free(s.scene);
    lrt_vk_engine_destroy(vk);
    return false;
  }
  const uint32_t ray_count = uint32_t(nrays);
  std::vector<lrt_ray> rays;
  GenerateCameraRays(camera, w, h, spp, &rays);

  std::vector<lrt_hit> hits(nrays);
  lrt_result trerr = LRT_RESULT_OK;
  int traced = -1;
  double as_build_s = 0.0, trace_s = 0.0;
  bool used_hw_rt = false;
  bool ray_query_fallback = false;
  const bool force_vkr_fallback = std::getenv("TUSDR_FORCE_VKR_FALLBACK") != nullptr;
  if (use_hw_rt) {
    // Build the ray-query acceleration structure directly from the indexed mesh
    // (the GPU builds a VK_INDEX_TYPE_UINT32 BLAS, so no de-indexing needed);
    // primitiveIndex == triangle build order == caller index, matching the
    // CPU/compute paths. Build once, trace the whole frame, free.
    uint32_t nverts = uint32_t(s.flat_verts.size() / 3u);
    t0 = std::chrono::steady_clock::now();
    lrt_vk_rtx_scene *rtx = lrt_vk_rtx_scene_build_indexed(
        vk, s.flat_verts.data(), nverts, s.flat_idx.data(), s.ntris, &trerr);
    as_build_s = SecsSince(t0);
    if (rtx) {
      t0 = std::chrono::steady_clock::now();
      if (force_vkr_fallback) {
        trerr = LRT_RESULT_UNSUPPORTED;
      } else {
        traced = lrt_vk_rtx_scene_trace(vk, rtx, rays.data(), ray_count,
                                        hits.data(), &trerr);
      }
      trace_s = SecsSince(t0);
      lrt_vk_rtx_scene_free(vk, rtx);
    }
    if (traced >= 0) {
      used_hw_rt = true;
    } else {
      std::cerr << "Vulkan ray-query trace failed (rc=" << trerr
                << "); falling back to compute trace.\n";
      t0 = std::chrono::steady_clock::now();
      if (!BuildGpuCpuScene(opt.threads, &s, opt.quality)) {
        lrt_vk_engine_destroy(vk);
        return false;
      }
      trerr = LRT_RESULT_OK;
      traced = lrt_vk_trace_scene(vk, s.scene, rays.data(), ray_count,
                                  hits.data(), &trerr);
      trace_s = SecsSince(t0);
      ray_query_fallback = traced >= 0;
    }
  } else {
    t0 = std::chrono::steady_clock::now();
    traced = lrt_vk_trace_scene(vk, s.scene, rays.data(), ray_count,
                                hits.data(), &trerr);
    trace_s = SecsSince(t0);
  }
  if (traced < 0) {
    std::cerr << "Vulkan trace failed (rc=" << trerr << ").\n";
    if (s.scene) lrt_tri_scene_free(s.scene);
    lrt_vk_engine_destroy(vk);
    return false;
  }

  t0 = std::chrono::steady_clock::now();
  bool ok = ShadeAndWriteImage(opt, s, rays, hits, w, h, spp);
  if (opt.stats) {
    std::cerr << "[gpu-stats] flatten+bvh " << flatten_s << " s, ";
    if (use_hw_rt) std::cerr << "as-build " << as_build_s << " s, ";
    std::cerr << "trace " << trace_s << " s, shade+write " << SecsSince(t0)
              << " s\n";  // compute-path trace includes the BVH upload
  }
  if (ok) {
    std::cerr << "triangles: " << s.ntris << " (" << geos.size() << " meshes)\n";
    std::cerr << "backend: LightRT VK (";
    if (used_hw_rt) {
      std::cerr << "ray_query, indexed Vulkan AS, CPU BVH skipped";
    } else if (ray_query_fallback) {
      std::cerr << "compute trace, ray_query fallback";
    } else {
      std::cerr << "compute trace";
    }
    std::cerr << ")\n";
  }
  if (s.scene) lrt_tri_scene_free(s.scene);
  lrt_vk_engine_destroy(vk);
  return ok;
}

bool RunVulkanGaussianLightRT(const Options &opt, const DirectScene *direct,
                              const CameraFrame &camera, int height) {
  if (!direct || direct->ellipse_chunks.empty())
    return false;
  lrt_vk_engine_options vopts;
  std::memset(&vopts, 0, sizeof(vopts));
  vopts.device_index = -1;
  vopts.prefer_discrete = 1;
  lrt_result err = LRT_RESULT_OK;
  lrt_vk_engine *vk = lrt_vk_engine_create(&vopts, &err);
  if (!vk) {
    std::cerr << "Failed to create LightRT Vulkan Gaussian engine (err="
              << int(err) << ").\n";
    return false;
  }
  const int w = opt.width > 0 ? opt.width : 960;
  const int h = height > 0 ? height : 540;
  const int spp = std::max(1, opt.samples);
  size_t nrays = 0;
  if (!ValidateGpuFrameSize(w, h, spp, "Vulkan Gaussian", &nrays)) {
    lrt_vk_engine_destroy(vk);
    return false;
  }
  std::vector<lrt_ray> rays;
  GenerateCameraRays(camera, w, h, spp, &rays);
  std::vector<lrt_hit> hits(nrays);
  for (lrt_hit &h : hits) {
    h.t = std::numeric_limits<float>::max();
    h.u = h.v = 0.0f;
    h.prim_id = LRT_TRI_NO_HIT;
  }
  std::vector<lrt_hit> chunk_hits(nrays);
  for (const EllipseSceneChunk &chunk : direct->ellipse_chunks) {
    int traced = lrt_vk_trace_scene(vk, chunk.scene.get(), rays.data(),
                                    uint32_t(nrays), chunk_hits.data(), &err);
    if (traced < 0) {
      std::cerr << "Vulkan Gaussian chunk trace failed [" << chunk.first
                << ", " << (chunk.first + chunk.count) << "] (err=" << int(err)
                << "): " << lrt_vk_engine_last_error(vk) << "\n";
      lrt_vk_engine_destroy(vk);
      return false;
    }
    for (size_t i = 0; i < nrays; ++i) {
      if (chunk_hits[i].prim_id != LRT_TRI_NO_HIT &&
          chunk_hits[i].t < hits[i].t) {
        hits[i] = chunk_hits[i];
        hits[i].prim_id += static_cast<uint32_t>(chunk.first);
      }
    }
  }
  size_t total_ellipses = 0;
  for (const EllipseSceneChunk &chunk : direct->ellipse_chunks)
    total_ellipses += chunk.info.size();
  const bool ok = ShadeAndWriteGaussianImage(
      opt, direct->ellipse_chunks, rays, hits, w, h, spp);
  if (ok) {
    std::cerr << "native Gaussian ellipses: " << total_ellipses
              << " in " << direct->ellipse_chunks.size() << " Vulkan chunk(s)\n"
              << "\nbackend: LightRT VK (native point/ellipse BVH)\n";
  }
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
  std::cerr << "Vulkan device: " << lrt_vk_engine_device_name(vk) << " ("
            << (lrt_vk_device_local_bytes(1) >> 20) << " MiB device-local)\n";

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
  // separately -- no product to overflow. The wide build is the MULTI-TLAS builder,
  // which additionally splits > ~16M instances across several TLASes (sharing one
  // BLAS set) so scenes past the device TLAS maxInstanceCount (2^24) -- the full
  // ~42.8M-instance Moana island -- render in full; it builds a single TLAS when
  // the scene fits, so smaller wide scenes are unaffected.
  uint32_t max_ntris = 1;
  for (const lrt_vk_proto &pr : cprotos)
    max_ntris = std::max(max_ntris, pr.ntris);
  bool wide = uint64_t(cinsts.size()) * uint64_t(max_ntris) >= 0xFFFFFFFFull ||
              cinsts.size() > 16000000u;  // multi-TLAS also needs the wide decode
  // Debug: force the wide encoding on any scene to validate the wide path against
  // the narrow one (they must be pixel-identical).
  if (std::getenv("TUSDR_FORCE_WIDE")) wide = true;

  const int w = opt.width > 0 ? opt.width : 960;
  const int h = height;
  const int spp = std::max(1, opt.samples);
  size_t nrays = 0;
  if (!ValidateGpuFrameSize(w, h, spp, "Vulkan instanced", &nrays)) {
    lrt_vk_engine_destroy(vk);
    return false;
  }
  const uint32_t ray_count = uint32_t(nrays);
  std::vector<lrt_ray> rays;
  GenerateCameraRays(camera, w, h, spp, &rays);
  std::vector<InstSampleHit> decoded(nrays);

  lrt_result builderr = LRT_RESULT_OK, trerr = LRT_RESULT_OK;
  int traced = -1;
  uint32_t scene_ntlas = 1;
  double as_build_s = 0.0, trace_s = 0.0;
  auto t0 = std::chrono::steady_clock::now();
  if (wide) {
    lrt_vk_rtx_scene *rtx = lrt_vk_rtx_scene_build_instanced_multi(
        vk, cprotos.data(), uint32_t(cprotos.size()), cinsts.data(),
        uint32_t(cinsts.size()), &builderr);
    as_build_s = SecsSince(t0);
    if (!rtx) {
      // OUT_OF_MEMORY here means the GPU ran out building the BLAS/TLAS set (e.g.
      // full Moana island: ~110k prototype BLAS exhaust VRAM). The flat fallback
      // would flatten to even MORE geometry and fail harder, so report and stop
      // rather than fall back. Lower TUSDR_INST_BUDGET to render a bounded subset.
      const bool oom = builderr == LRT_RESULT_OUT_OF_MEMORY;
      std::cerr << "-vkInstanced wide/multi build failed (rc=" << builderr << ")"
                << (oom ? "; the scene exceeds GPU memory -- lower TUSDR_INST_BUDGET "
                          "to render a bounded subset.\n"
                        : "; falling back to the flat path.\n");
      lrt_vk_engine_destroy(vk);
      return oom;  // true = handled (do not try the doomed flat path)
    }
    scene_ntlas = lrt_vk_rtx_scene_ntlas(rtx);
    scene.stride = max_ntris;  // reported only; wide decode does not use it
    std::vector<lrt_hit_wide> hits(rays.size());
    t0 = std::chrono::steady_clock::now();
    traced = lrt_vk_rtx_scene_trace_wide(vk, rtx, rays.data(),
                                         ray_count, hits.data(), &trerr);
    trace_s = SecsSince(t0);
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
    as_build_s = SecsSince(t0);
    if (!rtx) {
      std::cerr << "-vkInstanced build failed (rc=" << builderr
                << "); falling back to the flat path.\n";
      lrt_vk_engine_destroy(vk);
      return false;
    }
    scene.stride = stride;
    std::vector<lrt_hit> hits(rays.size());
    t0 = std::chrono::steady_clock::now();
    traced = lrt_vk_rtx_scene_trace(vk, rtx, rays.data(), ray_count,
                                    hits.data(), &trerr);
    trace_s = SecsSince(t0);
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

  t0 = std::chrono::steady_clock::now();
  bool ok = ShadeAndWriteImageInstanced(opt, scene, rays, decoded, w, h, spp);
  if (opt.stats) {
    std::cerr << "[gpu-stats] as-build " << as_build_s << " s, trace+decode "
              << trace_s << " s, shade+write " << SecsSince(t0) << " s\n";
  }
  if (ok) {
    std::cerr << "backend: LightRT VK (ray_query, two-level TLAS, "
              << (wide ? "wide 64-bit hit id" : "32-bit hit id");
    if (scene_ntlas > 1) std::cerr << ", " << scene_ntlas << "-way multi-TLAS";
    std::cerr << ")\n";
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
