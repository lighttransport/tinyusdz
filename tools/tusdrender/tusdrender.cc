// SPDX-License-Identifier: Apache 2.0
// Copyright 2026 - Present, Light Transport Entertainment, Inc.
//
// tusdrender: CPU preview raytrace renderer for USD scenes.
//
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>  // ProcessRSS()/AvailableSystemMemory() use std::ifstream
                    // unconditionally (was only included under TINYUSDZ_WITH_QJS)
#include <iostream>
#include <mutex>
#include <new>
#if !defined(_MSC_VER)
#include <unistd.h>  // POSIX (present under MinGW; absent with MSVC)
#endif
#include <array>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <malloc.h>  // mallopt (peak-RSS tuning, glibc)
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "asset-resolution.hh"
#include "image-loader.hh"
#include "image-writer.hh"
#include "io-util.hh"
#include "mmap-array-ref.hh"
#include "tinyusdz.hh"
#include "tsd/tinysubdiv.hh"
#include "tydra/attribute-eval.hh"
#include "tydra/render-data.hh"
#include "usdVol.hh"  // OpenVDB (.vdb) loader for the `next` volume path
#include "usdGeom.hh"
#include "value-types.hh"
#include "xform.hh"

// Experimental `next` lazy loader: fast, low-memory USDC parse used as the
// default backend for the RT preview path. tydra_next provides bit-exact
// world transforms (see src/tydra/next/scene-access.cc).
#include "next/layer/prim-spec.hh"
#include "next/pcp/prim-index.hh"
#include "next/prim/path.hh"
#include "next/reader/usdz-reader.hh"
#include "next/schema/geom-mesh.hh"
#include "next/stage/stage.hh"
#include "next/tinyusdz-next.hh"
#include "next/types/value.hh"
#include "tydra/next/scene-access.hh"



extern "C" {
#include "lightrt_c_tri.h"
}

#ifdef TINYUSDZ_WITH_QJS
#include <fstream>
#include <sstream>

#include "external/jsonhpp/nlohmann/json.hpp"
#include "tydra/js-script.hh"
extern "C" {
#include "external/quickjs-ng/quickjs.h"
}
#endif

#include "tusdr_math.hh"
#include "tusdr_types.hh"
#include "tusdr_context.hh"


// The Vulkan backend and main() below live in the global namespace; pull in the
// tusdr names they use (Vec3, Options, RTPreviewStats, qjs::*, ...).
using namespace tusdr;

// ---------------------------------------------------------------------------
// LightRT Vulkan backend: uses the LightRT C API (lightrt_c_vk.h) for GPU
// BVH traversal. Builds the scene with the existing CPU builder, uploads to
// GPU, traces camera rays, then shades hits on the CPU.
// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
#if defined(__GLIBC__)
  // Triangle streaming allocates and frees large transient per-job buffers
  // (megabytes each) while the final geometry buffers stay live. glibc's default
  // dynamic mmap threshold (which grows up to 32 MB) keeps those big frees in the
  // arena interleaved with live data, so they cannot be returned to the OS and
  // inflate peak RSS by ~1.3 GB on Island-scale scenes (isCoral 6.4 -> 5.0 GB).
  // Pinning the mmap threshold at 1 MB routes the large temporaries through
  // mmap/munmap (returned to the OS on free); only sub-MB allocations stay in the
  // arena, so the per-call syscall cost is negligible (~0.2 s on isCoral).
  mallopt(M_MMAP_THRESHOLD, 1 << 20);
  mallopt(M_TRIM_THRESHOLD, 4 << 20);
  // Grow each (sub-MB) arena in 16 MB chunks. The parallel compose allocates
  // ~140K prims' property storage; with the default minimal top-pad, glibc
  // commits a fresh page almost per prim -- ~139K mprotect() calls on isCoral
  // (97% of all syscall time, serialized under the kernel mmap_lock, which caps
  // the parallel fill near ~4 cores). A 16 MB pad commits arena memory in bulk so
  // those calls collapse to a few hundred; isCoral load 3.85 -> 3.48 s. Sized to
  // hold peak RSS under budget (128 MB pad inflated it ~400 MB; 16 MB is +~50 MB).
  mallopt(M_TOP_PAD, 16 << 20);
#endif
  Options opt;
  if (!ParseArgs(argc, argv, &opt)) {
    return EXIT_FAILURE;
  }

  // Configure the process memory budget: -maxMem <GiB>, else auto
  // min(32 GiB, 0.5 * system MemAvailable). Keeps tusdrender from being
  // OOM-killed on huge (instance-expanded) scenes; it aborts with a clear
  // message instead.
  MemBudget::Get().Init(opt.max_mem_gib);
  if (opt.stats) {
    std::cerr << "memory cap: " << MemBudget::GiB(MemBudget::Get().Cap());
    size_t avail = MemBudget::AvailableSystemMemory();
    if (avail) std::cerr << " (system avail: " << MemBudget::GiB(avail) << ")";
    std::cerr << "\n";
  }

  // Interactive / scripted modes (memory-persistent rendering over the next
  // loader): -js runs a JavaScript animation/control script, -mcp runs an MCP
  // stdio control server. Both keep the scene + BVH resident for repeated
  // re-rendering.
  if (opt.mcp || !opt.js_script.empty()) {
#ifdef TINYUSDZ_WITH_QJS
    if (opt.mcp) return RunMCPMode(opt);
    return RunJSScriptMode(opt, opt.js_script);
#else
    std::cerr << "-js/-mcp require building with -DTINYUSDZ_WITH_QJS=ON.\n";
    return EXIT_FAILURE;
#endif
  }

  // RT preview backend: the `next` lazy loader (fast, low-memory compose +
  // mmap USDC; also handles .usdz + .usda). Falls back to the legacy eager
  // loader for other inputs or when -legacyLoad is requested.
  if (opt.rt_preview && !opt.legacy_load) {
    return RunRTPreviewNext(opt);
  }

#if defined(HAVE_VULKAN) || defined(HAVE_D3D11) || defined(HAVE_HIP)
  // GPU backends (Vulkan / Direct3D 11 / HIP): load the scene through the `next`
  // lazy loader, build the geometry once, then trace on the selected GPU backend.
  if (opt.vulkan || opt.use_d3d || opt.hip) {
    // Load through next loader.
    tinyusdz::next::Stage stage;
    std::string warn, err;
    tinyusdz::next::pcp::CompositionOptions comp_opts;
    if (!opt.variant_overrides.empty())
      comp_opts.variant_overrides = opt.variant_overrides;
    if (!tinyusdz::next::LoadUSDComposed(opt.input, &stage, &warn, &err,
                                         &comp_opts)) {
      if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
      std::cerr << "Failed to load USD: " << err << "\n";
      return EXIT_FAILURE;
    }
    if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";

    // Collect meshes and build geometry.
    std::vector<tinyusdz::next::UsdPrim> mesh_prims;
    std::vector<matrix4d> worlds;
    std::vector<Vec3> base_colors;
    std::vector<int32_t> tex_ids;
    std::vector<float> roughnesses;
    std::vector<float> metallics;
    std::vector<RTPreviewStats::MeshGeometry> geos;

    {
      // Traverse and collect meshes.
      std::vector<tinyusdz::next::UsdPrim> mesh_stack;
      for (const auto &root : stage.GetRootPrims()) {
        std::vector<tinyusdz::next::UsdPrim> stack;
        stack.push_back(root);
        while (!stack.empty()) {
          auto prim = stack.back();
          stack.pop_back();
          if (prim.GetTypeName() == "Mesh") {
            mesh_stack.push_back(prim);
            Vec3 bc{0.5f, 0.5f, 0.5f};
            // Try to get displayColor.
            const tinyusdz::next::Value *dcv = prim.GetPropertyValue("primvars:displayColor");
            if (dcv) {
              const std::vector<float> *dc = dcv->as_float_array();
              if (dc && dc->size() >= 3) {
                bc = Vec3{(*dc)[0], (*dc)[1], (*dc)[2]};
              }
            }
            base_colors.push_back(bc);
            tex_ids.push_back(-1);
            roughnesses.push_back(0.5f);
            metallics.push_back(0.0f);
          }
          for (const auto &child : prim.GetChildren()) {
            stack.push_back(child);
          }
        }
      }

      // Displacement textures for the -vk/-vkr preview (loaded from disk relative
      // to the input; usdz-embedded displacement maps are not resolved here).
      std::vector<tusdr::Texture> disp_textures;
      TextureCache tc;
      tc.textures = &disp_textures;
      tc.base_dir = DirName(opt.input);
      tc.usdz = nullptr;

      // Stream geometry.
      for (auto &prim : mesh_stack) {
        RTPreviewStats::MeshGeometry geo;
        uint32_t nv = 0;
        const tinyusdz::next::Value *val = prim.GetPropertyValue("points");
        if (!val) continue;
        const std::vector<float> *pts = val->as_float_array();
        if (!pts || pts->empty()) continue;
        nv = uint32_t(pts->size() / 3);
        geo.positions = *pts;

        val = prim.GetPropertyValue("normals");
        if (val) {
          const std::vector<float> *nrm = val->as_float_array();
          if (nrm && nrm->size() >= nv * 3)
            geo.normals = *nrm;
        }
        if (geo.normals.empty()) {
          geo.normals.resize(nv * 3, 0);
        }

        val = prim.GetPropertyValue("primvars:st");
        if (val) {
          const std::vector<float> *uv = val->as_float_array();
          if (uv && !uv->empty()) {
            geo.uvs = *uv;
          }
        }
        if (geo.uvs.empty()) geo.uvs.resize(nv * 2, 0);

        // Fan-triangulate the polygons into a triangle-soup index list. The GPU
        // backends (RunVulkanLightRT / RunD3D11LightRT) consume geo.indices as
        // groups of three, so quads/n-gons MUST be split here using
        // faceVertexCounts — feeding the raw faceVertexIndices chunked by 3 drops
        // and scrambles triangles (e.g. a 468-quad + 32-tri Suzanne collapses
        // from 968 triangles to 656, rendering with holes).
        val = prim.GetPropertyValue("faceVertexIndices");
        const tinyusdz::next::Value *cval =
            prim.GetPropertyValue("faceVertexCounts");
        if (val) {
          const std::vector<int> *idx = val->as_int_array();
          const std::vector<int> *cnt = cval ? cval->as_int_array() : nullptr;
          if (idx && !idx->empty()) {
            if (cnt && !cnt->empty()) {
              size_t off = 0;
              for (int c : *cnt) {
                if (c >= 3 && off + size_t(c) <= idx->size()) {
                  int v0 = (*idx)[off];
                  for (int k = 1; k + 1 < c; ++k) {
                    geo.indices.push_back(v0);
                    geo.indices.push_back((*idx)[off + size_t(k)]);
                    geo.indices.push_back((*idx)[off + size_t(k) + 1]);
                  }
                }
                off += size_t(c < 0 ? 0 : c);
              }
            } else {
              // No counts: assume an already-triangulated soup.
              geo.indices.assign(idx->begin(), idx->end());
            }
          }
        }

        // Coarse displacement: resolve the bound material's inputs:displacement and
        // offset each vertex along its smooth normal (RunVulkanLightRT triangulates
        // indices as a soup and shades with geometric normals computed from these
        // positions, so displaced positions are all that is needed).
        if (opt.displace && opt.displace_scale != 0.0f && !geo.indices.empty()) {
          float disp_const = 0.0f;
          ScalarTex disp_tex;
          const std::vector<tinyusdz::next::Path> *bind =
              prim.GetRelationship("material:binding");
          if (bind && !bind->empty()) {
            tinyusdz::next::UsdPrim mat = stage.GetPrimAtPath((*bind)[0]);
            if (mat.IsValid()) {
              tinyusdz::next::UsdPrim surf =
                  ConnectedPrimNext(stage, mat, "outputs:surface");
              if (!surf.IsValid())
                surf = ConnectedPrimNext(stage, mat, "outputs:mtlx:surface");
              if (surf.IsValid()) {
                if (const tinyusdz::next::Value *d =
                        surf.GetPropertyValue("inputs:displacement"))
                  if (const float *f = d->as_float()) disp_const = *f;
                ResolveScalarTextureNext(stage, surf, "inputs:displacement", tc,
                                         &disp_tex);
              }
            }
          }
          if (disp_tex.id >= 0 || disp_const != 0.0f) {
            // Area-weighted smooth vertex normals from the triangle soup.
            std::vector<Vec3> vn(nv, Vec3{0.0f, 0.0f, 0.0f});
            for (size_t t = 0; t + 2 < geo.indices.size(); t += 3) {
              int a = geo.indices[t], b = geo.indices[t + 1],
                  c = geo.indices[t + 2];
              if (a < 0 || b < 0 || c < 0 || uint32_t(a) >= nv ||
                  uint32_t(b) >= nv || uint32_t(c) >= nv)
                continue;
              Vec3 pa{geo.positions[a * 3], geo.positions[a * 3 + 1],
                      geo.positions[a * 3 + 2]};
              Vec3 pb{geo.positions[b * 3], geo.positions[b * 3 + 1],
                      geo.positions[b * 3 + 2]};
              Vec3 pc{geo.positions[c * 3], geo.positions[c * 3 + 1],
                      geo.positions[c * 3 + 2]};
              Vec3 fn = Cross(Sub(pb, pa), Sub(pc, pa));
              vn[a] = Add(vn[a], fn);
              vn[b] = Add(vn[b], fn);
              vn[c] = Add(vn[c], fn);
            }
            const tusdr::Texture *dtex =
                (disp_tex.id >= 0 &&
                 size_t(disp_tex.id) < disp_textures.size())
                    ? &disp_textures[size_t(disp_tex.id)]
                    : nullptr;
            const bool per_vertex_uv = geo.uvs.size() >= size_t(nv) * 2;
            for (uint32_t v = 0; v < nv; ++v) {
              if (Length(vn[v]) < 1.0e-12f) continue;
              Vec3 n = Normalize(vn[v]);
              float h = disp_const;
              if (dtex) {
                float u = per_vertex_uv ? geo.uvs[v * 2] : 0.0f;
                float vv = per_vertex_uv ? geo.uvs[v * 2 + 1] : 0.0f;
                h = dtex->sample_channel(u, vv, 0.0f, disp_tex.ch) * disp_tex.scale +
                    disp_tex.bias;
              }
              h *= opt.displace_scale;
              geo.positions[v * 3 + 0] += n.x * h;
              geo.positions[v * 3 + 1] += n.y * h;
              geo.positions[v * 3 + 2] += n.z * h;
            }
          }
        }

        geos.push_back(std::move(geo));
      }
    }

    if (geos.empty()) {
      std::cerr << "No renderable geometry found.\n";
      return EXIT_FAILURE;
    }

    // Resolve camera.
    CameraFrame camera;
    double up_axis = 1.0; // Y-up
    {
      std::string up = stage.GetUpAxis();
      if (up == "Z") up_axis = 2.0;
      else if (up == "X") up_axis = 0.0;
    }
    tinyusdz::Axis usdUp = (up_axis == 2.0) ? tinyusdz::Axis::Z
                           : (up_axis == 0.0) ? tinyusdz::Axis::X
                           : tinyusdz::Axis::Y;

    Bounds bounds;
    for (const auto &g : geos) {
      for (size_t j = 0; j < g.positions.size() / 3; ++j) {
        bounds.lo.x = std::min(bounds.lo.x, g.positions[j * 3 + 0]);
        bounds.lo.y = std::min(bounds.lo.y, g.positions[j * 3 + 1]);
        bounds.lo.z = std::min(bounds.lo.z, g.positions[j * 3 + 2]);
        bounds.hi.x = std::max(bounds.hi.x, g.positions[j * 3 + 0]);
        bounds.hi.y = std::max(bounds.hi.y, g.positions[j * 3 + 1]);
        bounds.hi.z = std::max(bounds.hi.z, g.positions[j * 3 + 2]);
      }
    }
    bounds.valid = true;
    // Resolve the camera the SAME way the CPU -rtPreview path does
    // (ResolveCameraNext): a named camera, else the USD record camera for
    // -autoframe, else auto-fit. This keeps -vk/-vkr framed identically to the
    // -rtPreview reference (the GPU backends previously used the tilted auto-fit
    // camera here, so the same scene framed differently from the CPU image).
    const int cam_width = opt.width > 0 ? opt.width : 960;
    int out_height = opt.height;
    Options auto_opt = opt;
    auto_opt.camera.clear();
    auto_opt.width = cam_width;
    if (!opt.camera.empty()) {
      float cam_aspect = 16.0f / 9.0f;
      if (FindNextCameraFrame(stage, opt.camera, opt.timecode, &camera,
                              &cam_aspect)) {
        if (out_height <= 0)
          out_height =
              std::max(1, int(std::lround(float(cam_width) / cam_aspect)));
      } else {
        std::cerr << "WARN: camera not found: " << opt.camera
                  << ". Using auto-fit.\n";
        if (out_height <= 0) out_height = 540;
        camera = MakeCameraFrame({}, auto_opt, bounds, out_height, usdUp);
      }
    } else if (opt.autoframe) {
      camera = MakeUsdRecordCamera(bounds, usdUp, cam_width, &out_height);
    } else {
      if (out_height <= 0) out_height = 540;
      camera = MakeCameraFrame({}, auto_opt, bounds, out_height, usdUp);
    }

#ifdef HAVE_D3D11
    if (opt.use_d3d) {
      if (!RunD3D11LightRT(opt, base_colors, geos, camera, out_height)) {
        return EXIT_FAILURE;
      }
      return EXIT_SUCCESS;
    }
#endif
#ifdef HAVE_VULKAN
    if (opt.vulkan) {
      if (!RunVulkanLightRT(opt, base_colors, geos, camera, out_height)) {
        return EXIT_FAILURE;
      }
      return EXIT_SUCCESS;
    }
#endif
#ifdef HAVE_HIP
    if (opt.hip) {
      if (!RunHipLightRT(opt, base_colors, geos, camera, out_height)) {
        return EXIT_FAILURE;
      }
      return EXIT_SUCCESS;
    }
#endif
    std::cerr << "Requested GPU backend not built in.\n";
    return EXIT_FAILURE;
  }
#endif

  tinyusdz::Stage stage;
  std::string warn;
  std::string err;
  tinyusdz::USDLoadOptions load_options;
  load_options.mmap_zero_copy = opt.rt_preview;
  load_options.max_memory_limit_in_mb = opt.rt_preview ? 65536 : 16384;
  load_options.load_assets = !opt.rt_preview;
  if (opt.progress) {
    load_options.progress_callback = LoadProgress;
  }
  const auto load_t0 = std::chrono::steady_clock::now();
  if (!tinyusdz::LoadUSDFromFile(opt.input, &stage, &warn, &err, load_options)) {
    if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
    std::cerr << "Failed to load USD: " << err << "\n";
    return EXIT_FAILURE;
  }
  const auto load_t1 = std::chrono::steady_clock::now();
  const double load_seconds =
      std::chrono::duration<double>(load_t1 - load_t0).count();
  if (!warn.empty()) {
    std::cerr << "WARN: " << warn << "\n";
  }

  if (opt.rt_preview) {
    if (tinyusdz::IsUSDC(opt.input) && !stage.has_mmap_zero_copy()) {
      std::cerr << "RT preview requires mmap zero-copy metadata for USDC input. "
                << "Write flattened USDC without --compress-float-arrays.\n";
      return EXIT_FAILURE;
    }

    std::vector<float> vertices;
    std::vector<TriInfo> tris;
    Bounds bounds;
    RTPreviewStats rt_stats;
    std::string rt_err;
    if (!BuildRTPreviewScene(stage, opt, &vertices, &tris, &bounds, &rt_stats,
                             &rt_err)) {
      if (opt.stats) {
        std::cerr << "rt preview: 1\n";
        std::cerr << "rt meshes: " << rt_stats.meshes << "\n";
        std::cerr << "rt skipped meshes: " << rt_stats.skipped_meshes << "\n";
        std::cerr << "rt mmap point meshes: "
                  << rt_stats.meshes_with_mmap_points << "\n";
        std::cerr << "rt owned point meshes: "
                  << rt_stats.meshes_with_owned_points << "\n";
        std::cerr << "rt mmap deferred bytes: "
                  << rt_stats.mmap_deferred_bytes << "\n";
        std::cerr << "rt copied point bytes: " << rt_stats.copied_point_bytes
                  << "\n";
        std::cerr << "rt copied topology bytes: "
                  << rt_stats.copied_topology_bytes << "\n";
        std::cerr << "rt purpose default triangles: "
                  << rt_stats.purpose_default_triangles << "\n";
        std::cerr << "rt purpose render triangles: "
                  << rt_stats.purpose_render_triangles << "\n";
        std::cerr << "rt purpose proxy triangles: "
                  << rt_stats.purpose_proxy_triangles << "\n";
        std::cerr << "rt purpose guide triangles: "
                  << rt_stats.purpose_guide_triangles << "\n";
      }
      std::cerr << rt_err << "\n";
      return EXIT_FAILURE;
    }

    lrt_tri_build_options build_opts;
    std::memset(&build_opts, 0, sizeof(build_opts));
    build_opts.quality = opt.quality;
    build_opts.layout = LRT_TRI_LAYOUT_AUTO;
    build_opts.max_leaf_size = 0;
    build_opts.num_threads = WorkerThreadCount(opt.threads);

    const auto bvh_t0 = std::chrono::steady_clock::now();
    lrt_result lrt_err = LRT_RESULT_OK;
    lrt_tri_scene *lrt_scene =
        lrt_tri_scene_build(vertices.data(), tris.size(), &build_opts, &lrt_err);
    const auto bvh_t1 = std::chrono::steady_clock::now();
    if (!lrt_scene) {
      std::cerr << "Failed to build LightRT scene (err=" << int(lrt_err) << ").\n";
      return EXIT_FAILURE;
    }

    int height = opt.height > 0 ? opt.height : 540;
    RenderScene empty_render_scene;
    CameraFrame camera;
    if (!FindStageCameraFrame(stage, opt.camera, opt.timecode, &camera)) {
      if (!opt.camera.empty()) {
        std::cerr << "WARN: Camera not found: " << opt.camera
                  << ". Using auto-fit camera.\n";
      }
      Options auto_opt = opt;
      auto_opt.camera.clear();
      camera = MakeCameraFrame(empty_render_scene, auto_opt, bounds, height,
                               stage.metas().upAxis.get_value());
    }
    DirectScene direct_scene;
    LightCache light_cache;
    IblCache ibl_cache;

    if (opt.stats) {
      lrt_tri_stats st;
      std::memset(&st, 0, sizeof(st));
      lrt_tri_scene_stats(lrt_scene, &st);
      double bvh_seconds =
          std::chrono::duration<double>(bvh_t1 - bvh_t0).count();
      std::cerr << "rt preview: 1\n";
      std::cerr << "rt meshes: " << rt_stats.meshes << "\n";
      std::cerr << "rt skipped meshes: " << rt_stats.skipped_meshes << "\n";
      std::cerr << "rt mmap point meshes: "
                << rt_stats.meshes_with_mmap_points << "\n";
      std::cerr << "rt owned point meshes: "
                << rt_stats.meshes_with_owned_points << "\n";
      std::cerr << "rt mmap deferred bytes: "
                << rt_stats.mmap_deferred_bytes << "\n";
      std::cerr << "rt copied point bytes: " << rt_stats.copied_point_bytes
                << "\n";
      std::cerr << "rt copied topology bytes: "
                << rt_stats.copied_topology_bytes << "\n";
      std::cerr << "rt purpose default triangles: "
                << rt_stats.purpose_default_triangles << "\n";
      std::cerr << "rt purpose render triangles: "
                << rt_stats.purpose_render_triangles << "\n";
      std::cerr << "rt purpose proxy triangles: "
                << rt_stats.purpose_proxy_triangles << "\n";
      std::cerr << "rt purpose guide triangles: "
                << rt_stats.purpose_guide_triangles << "\n";
      std::cerr << "rt packed triangle bytes: "
                << rt_stats.packed_triangle_bytes << "\n";
      std::cerr << "load seconds: " << load_seconds << "\n";
      std::cerr << "rt triangle stream seconds: " << rt_stats.build_seconds
                << "\n";
      std::cerr << "rt bvh build seconds: " << bvh_seconds << "\n";
      std::cerr << "triangles: " << tris.size() << "\n";
      std::cerr << "lightrt: " << lrt_tri_kernel_name(lrt_scene) << "\n";
      std::cerr << "bvh nodes: " << st.node_count << ", leaves: "
                << st.leaf_count << ", memory: " << st.memory_bytes
                << " bytes\n";
    }

    const auto render_t0 = std::chrono::steady_clock::now();
    std::vector<FlatTri> flat_tris;
    std::vector<TriMat> flat_mats;
    SplitTriInfos(tris, &flat_tris, &flat_mats);
    tinyusdz::Image img =
        RenderImage(lrt_scene, &direct_scene, flat_tris, flat_mats, light_cache,
                    nullptr, camera, opt, height);
    const auto render_t1 = std::chrono::steady_clock::now();
    if (opt.stats) {
      std::cerr << "render seconds: "
                << std::chrono::duration<double>(render_t1 - render_t0).count()
                << "\n";
    }
    lrt_tri_scene_free(lrt_scene);

    tinyusdz::image::WriteOption wopt;
    wopt.format = tinyusdz::image::WriteImageFormat::Autodetect;
    auto ret = tinyusdz::image::WriteImageToFile(opt.output, img, wopt);
    if (!ret) {
      std::cerr << "Failed to write image: " << ret.error() << "\n";
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }

  tinyusdz::tydra::RenderScene render_scene;
  tinyusdz::tydra::RenderSceneConverter converter;
  tinyusdz::tydra::RenderSceneConverterEnv env(stage);
  env.timecode = opt.timecode;
  env.mesh_config.triangulate = !opt.direct_prims;
  env.mesh_config.subdivision_level = opt.subdivision_level;
  env.mesh_config.build_vertex_indices = !opt.direct_prims;
  env.mesh_config.compute_tangents_and_binormals = false;
  env.scene_config.load_texture_assets = true;
  env.set_search_paths({tinyusdz::io::GetBaseDir(opt.input)});
  if (opt.no_assetresolver) {
    SetupNullAssetResolution(&env.asset_resolver);
  }
  if (!converter.ConvertToRenderScene(env, &render_scene)) {
    std::cerr << "Failed to convert USD Stage to RenderScene:\n"
              << converter.GetError() << "\n";
    return EXIT_FAILURE;
  }
  std::string converter_warn = converter.GetWarning();
  if (!converter_warn.empty()) {
    std::cerr << "WARN: " << converter_warn << "\n";
  }

  std::vector<float> vertices;
  std::vector<TriInfo> tris;
  Bounds bounds;
  DirectScene direct_scene;
  LightCache light_cache;
  if (opt.direct_prims) {
    std::string direct_err;
    if (!BuildDirectScene(stage, render_scene, opt, &vertices, &tris, &bounds,
                          &direct_scene, &direct_err)) {
      std::cerr << direct_err << "\n";
      return EXIT_FAILURE;
    }
  }
  CollectAllGeometry(render_scene, &vertices, &tris, &bounds,
                     opt.direct_prims ? &direct_scene.direct_paths : nullptr,
                     &light_cache);
  const bool has_direct = direct_scene.spheres || direct_scene.round_curves ||
                          direct_scene.flat_curves || direct_scene.points ||
                          direct_scene.bez_curves || direct_scene.tets ||
                          !direct_scene.shapes.empty();

  // UsdVol volumes (OpenVDB) -> dense grids for raymarching. Built here so a
  // volume-only scene still renders and contributes to camera-framing bounds.
  std::vector<VolumeData> volumes = BuildVolumes(render_scene);
  ExpandBoundsByVolume(volumes, &bounds);

  if (tris.empty() && !has_direct && volumes.empty()) {
    std::cerr << "No renderable geometry found.\n";
    return EXIT_FAILURE;
  }

  lrt_tri_build_options build_opts;
  std::memset(&build_opts, 0, sizeof(build_opts));
  build_opts.quality = opt.quality;
  build_opts.layout = LRT_TRI_LAYOUT_AUTO;
  build_opts.max_leaf_size = 0;
  build_opts.num_threads = WorkerThreadCount(opt.threads);

  lrt_result lrt_err = LRT_RESULT_OK;
  lrt_tri_scene *lrt_scene = nullptr;
  if (!tris.empty()) {
    lrt_scene = lrt_tri_scene_build(vertices.data(), tris.size(), &build_opts, &lrt_err);
    if (!lrt_scene) {
      std::cerr << "Failed to build LightRT scene (err=" << int(lrt_err) << ").\n";
      return EXIT_FAILURE;
    }
  }

  int height = opt.height;
  if (height <= 0) {
    height = 540;
    const Node *cam_node = FindCameraNode(render_scene, opt.camera);
    if (cam_node) {
      const RenderCamera &cam = render_scene.cameras[size_t(cam_node->id)];
      if (cam.verticalAspectRatio > 0.0f) {
        height = std::max(1, int(std::round(float(opt.width) *
                                           cam.verticalAspectRatio)));
      }
    }
  }

  tinyusdz::Axis up_axis = GetUpAxis(render_scene.meta.upAxis);
  CameraFrame camera = MakeCameraFrame(render_scene, opt, bounds, height,
                                       up_axis);
  CollectLights(render_scene, &light_cache);
  IblCache ibl_cache;
  BuildIblCache(render_scene, light_cache, &ibl_cache);

  if (opt.stats) {
    lrt_tri_stats st;
    std::memset(&st, 0, sizeof(st));
    if (lrt_scene) lrt_tri_scene_stats(lrt_scene, &st);
    std::cerr << "triangles: " << tris.size() << "\n";
    std::cerr << "direct spheres: " << direct_scene.sphere_info.size() << "\n";
    std::cerr << "direct round curve segments: "
              << direct_scene.round_curve_info.size() << "\n";
    std::cerr << "direct flat curve segments: "
              << direct_scene.flat_curve_info.size() << "\n";
    std::cerr << "direct Hermite/Bezier curve segments: "
              << direct_scene.bez_curve_info.size() << "\n";
    std::cerr << "direct points: " << direct_scene.point_info.size() << "\n";
    std::cerr << "direct tetrahedra: " << direct_scene.tet_prims.size()
              << "\n";
    std::cerr << "direct analytic shapes: " << direct_scene.shapes.size()
              << "\n";
    std::cerr << "subdivision level: " << opt.subdivision_level << "\n";
    std::cerr << "lights: " << light_cache.finite.size() << "\n";
    std::cerr << "mesh light triangles: " << light_cache.mesh.size() << "\n";
    std::cerr << "domelight: " << (light_cache.has_dome ? 1 : 0) << "\n";
    std::cerr << "light sampling finite cdf entries: "
              << light_cache.finite_cdf.size() << "\n";
    std::cerr << "light sampling mesh cdf entries: "
              << light_cache.mesh_cdf.size() << "\n";
    std::cerr << "light sampling env cdf entries: "
              << light_cache.env_cdf.size() << "\n";
    std::cerr << "ibl envmap: " << (ibl_cache.valid ? 1 : 0) << "\n";
    std::cerr << "ibl diffuse size: "
              << (ibl_cache.diffuse.width * ibl_cache.diffuse.height) << "\n";
    std::cerr << "ibl prefilter levels: " << ibl_cache.prefiltered.size()
              << "\n";
    std::cerr << "ibl brdf lut size: "
              << (ibl_cache.brdf_size * ibl_cache.brdf_size) << "\n";
    if (lrt_scene) std::cerr << "lightrt: " << lrt_tri_kernel_name(lrt_scene) << "\n";
    std::cerr << "bvh nodes: " << st.node_count << ", leaves: " << st.leaf_count
              << ", memory: " << st.memory_bytes << " bytes\n";
    std::cerr << "load seconds: " << load_seconds << "\n";
  }

  if (opt.stats) {
    std::cerr << "volumes: " << volumes.size() << "\n";
  }

  const auto render_t0 = std::chrono::steady_clock::now();
  std::vector<FlatTri> flat_tris;
  std::vector<TriMat> flat_mats;
  SplitTriInfos(tris, &flat_tris, &flat_mats);
  tinyusdz::Image img =
      RenderImage(lrt_scene, &direct_scene, flat_tris, flat_mats, light_cache,
                  ibl_cache.valid ? &ibl_cache : nullptr, camera, opt, height,
                  /*textures*/ nullptr, /*tri_uvs*/ nullptr, /*tlas*/ nullptr,
                  /*blas*/ nullptr, /*instances*/ nullptr, /*tri_colors*/ nullptr,
                  /*tri_normals*/ nullptr, &volumes);
  const auto render_t1 = std::chrono::steady_clock::now();
  if (opt.stats) {
    std::cerr << "render seconds: "
              << std::chrono::duration<double>(render_t1 - render_t0).count()
              << "\n";
  }
  if (lrt_scene) lrt_tri_scene_free(lrt_scene);

  tinyusdz::image::WriteOption wopt;
  wopt.format = tinyusdz::image::WriteImageFormat::Autodetect;
  auto ret = tinyusdz::image::WriteImageToFile(opt.output, img, wopt);
  if (!ret) {
    std::cerr << "Failed to write image: " << ret.error() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
