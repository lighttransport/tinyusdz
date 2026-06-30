// SPDX-License-Identifier: Apache-2.0
// tusdview - a USD viewer example for tinyusdz.
//
// Usage:
//   tusdview [options] [file.usd|usda|usdc|usdz]
//   --backend gl|vk   Select rendering backend (default: vk when available)
//
// Loads a USD file via tinyusdz, converts it with the Tydra RenderScene API and
// renders it with OpenGL (and Vulkan if built with HAVE_VULKAN). The GUI uses an
// ImGui docking layout with a prim hierarchy browser, a property inspector and a
// 3D viewport with Maya-style navigation (Alt+LMB orbit / Alt+MMB pan /
// Alt+RMB+wheel dolly, 'F' to frame the scene).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

#include "security-policy.hh"

#include "app.hh"
#include "config.hh"
#include "log.hh"
#include "renderer.hh"

int main(int argc, char** argv) {
#if defined(HAVE_VULKAN)
  tusdview::Backend backend = tusdview::Backend::Vulkan;
#else
  tusdview::Backend backend = tusdview::Backend::GL;
#endif
  bool backendExplicit = false;
  std::optional<std::string> configPath;
  std::string file;
  std::string screenshot;
  std::string windowShot;
  int maxFrames = -1;
  long long maxTris = 0;      // 0 = default budget
  double maxGpuMemGiB = 0.0;  // --max-gpu-mem: raster full-mesh VRAM cap (GiB)
  long long maxDrawMeshes = 0;  // --max-draw-meshes: raster full-mesh count cap
  bool robustFrame = true;      // trim outlier meshes from fit-all auto-frame
  bool rtLod = false;           // --rt-lod: view-dependent RT instance LOD
  float rtLodFullPx = 0.0f;     // 0 => keep App default
  float rtLodCullPx = -1.0f;    // <0 => keep App default
  float rtLodBand = -1.0f;      // <0 => keep App default (stochastic band width)
  bool rasterLod = false;       // --raster-lod: view-dependent raster instance LOD
  float rasterLodFullPx = 0.0f; // 0 => keep App default
  float rasterLodCullPx = -1.0f;// <0 => keep App default
  double timeBudget = 0.0;    // 0 = unlimited
  std::optional<float> uiScale;  // Explicit CLI override for font/widget/window scale.
  bool wantRt = false;        // request Vulkan ray tracing (if supported)
  bool wantCuda = false;      // --cuda: CUDA BVH ray-traced screenshot (cuew runtime)
  bool wantHip = false;       // --hip: HIP/ROCm BVH ray-traced screenshot (hipew runtime)
  int rtSamples = 1;          // --rt-samples: AA supersamples for the CUDA/HIP path
  long long rtMaxInstances = 16000000;  // --max-instances: CUDA/HIP instance cap (0=off)
  bool lodStream = false;     // --lod-stream: view-dependent district LOD (needs --next)
  double lodMaxMem = 0.0;     // --max-mem GiB: host budget for --lod-stream (0=auto)
  double lodMaxVram = 0.0;    // --max-vram GiB: GPU budget for --lod-stream (0=auto)
  bool wantWireframe = false;  // --wireframe: start in wireframe render mode
  bool wantMaterialId = false; // --material-id: start in material-id viz mode
  std::optional<tusdview::RenderMode> wantMode;  // --mode <name>: any render mode
  std::vector<std::pair<std::string, float>> wantBlend;  // --blend NAME=WEIGHT
  std::string wantSelect;  // --select <prim path>
  bool mcpStdio = false;      // MCP server: stdio transport
  int mcpHttpPort = 0;        // MCP server: HTTP transport port (0 = off)
  int streamHttpPort = 0;     // WebSocket stream server port (0 = off)
  std::string streamCodec = "png";   // idle-refinement codec: png|qoi
  int streamMotionRes = 1280;        // motion-frame long-edge cap (px)
  int streamMotionQuality = 45;      // motion-frame JPEG quality (1-100)
  int streamIdleMs = 350;            // ms of input quiet before the lossless refine
  bool headless = false;      // windowless offscreen rendering (Vulkan only)
  bool threaded = false;      // --threaded: experimental render-thread GL path
  bool useNextLoader = false;             // --next: next loader + flat GL preview
  bool noCull = false;                     // --no-cull: disable frustum culling
  float camDolly = 1.0f;                    // --cam-dolly: fitted-distance scale
  std::string cameraName;                   // --camera: USD camera to frame (--next)
  bool noComposition = false;             // --no-composition: root layer only
  std::optional<bool> deferPayloads;      // --defer-payloads / --load-payloads
  bool deferReferences = false;           // --defer-references (explicit opt-in)
  bool allowParentPaths = false;          // --allow-parent-paths: permit '..' in
                                          // composition asset paths (e.g. ALab)
  std::optional<double> timeCode;         // --time T: evaluate the scene at this
                                          // time code (animated screenshots)
  tusdview::SkinningMode skinningMode = tusdview::SkinningMode::Auto;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--config") == 0) {
      if ((i + 1) >= argc) {
        LOGE("--config requires a path");
        return 1;
      }
      configPath = argv[++i];
    } else if (std::strncmp(argv[i], "--config=", 9) == 0) {
      configPath = argv[i] + 9;
    } else if (std::strcmp(argv[i], "--backend") == 0 && (i + 1) < argc) {
      ++i;
      if (std::strcmp(argv[i], "vk") == 0 || std::strcmp(argv[i], "vulkan") == 0) {
        backend = tusdview::Backend::Vulkan;
      } else if (std::strcmp(argv[i], "gl") == 0 ||
                 std::strcmp(argv[i], "opengl") == 0) {
        backend = tusdview::Backend::GL;
      } else {
        LOGE("--backend must be gl, opengl, vk, or vulkan");
        return 1;
      }
      backendExplicit = true;
    } else if (std::strcmp(argv[i], "--frames") == 0 && (i + 1) < argc) {
      maxFrames = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--screenshot") == 0 && (i + 1) < argc) {
      screenshot = argv[++i];
    } else if (std::strcmp(argv[i], "--max-tris") == 0 && (i + 1) < argc) {
      maxTris = std::atoll(argv[++i]);
    } else if (std::strcmp(argv[i], "--max-asset-bytes") == 0 && (i + 1) < argc) {
      // Override per-asset composition/resolver read cap (default 512MB).
      // Accepts a byte count with optional K/M/G suffix, e.g. 2G.
      std::string v = argv[++i];
      size_t mul = 1;
      if (!v.empty()) {
        char s = v.back();
        if (s == 'k' || s == 'K') { mul = 1024ull; v.pop_back(); }
        else if (s == 'm' || s == 'M') { mul = 1024ull * 1024; v.pop_back(); }
        else if (s == 'g' || s == 'G') { mul = 1024ull * 1024 * 1024; v.pop_back(); }
      }
      tinyusdz::security_policy::SetMaxAssetReadBytes(
          static_cast<size_t>(std::strtoull(v.c_str(), nullptr, 10)) * mul);
    } else if (std::strcmp(argv[i], "--max-gpu-mem") == 0 && (i + 1) < argc) {
      maxGpuMemGiB = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--max-draw-meshes") == 0 && (i + 1) < argc) {
      maxDrawMeshes = std::atoll(argv[++i]);
    } else if (std::strcmp(argv[i], "--no-robust-frame") == 0) {
      robustFrame = false;
    } else if (std::strcmp(argv[i], "--rt-lod") == 0) {
      rtLod = true;
    } else if (std::strcmp(argv[i], "--rt-lod-full-px") == 0 && (i + 1) < argc) {
      rtLodFullPx = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(argv[i], "--rt-lod-cull-px") == 0 && (i + 1) < argc) {
      rtLodCullPx = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(argv[i], "--rt-lod-band") == 0 && (i + 1) < argc) {
      rtLodBand = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(argv[i], "--raster-lod") == 0) {
      rasterLod = true;
    } else if (std::strcmp(argv[i], "--raster-lod-full-px") == 0 && (i + 1) < argc) {
      rasterLodFullPx = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(argv[i], "--raster-lod-cull-px") == 0 && (i + 1) < argc) {
      rasterLodCullPx = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(argv[i], "--time-budget") == 0 && (i + 1) < argc) {
      timeBudget = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--ui-scale") == 0 && (i + 1) < argc) {
      uiScale = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(argv[i], "--window-shot") == 0 && (i + 1) < argc) {
      windowShot = argv[++i];
    } else if (std::strcmp(argv[i], "--headless") == 0) {
      headless = true;
    } else if (std::strcmp(argv[i], "--threaded") == 0) {
      threaded = true;
    } else if (std::strcmp(argv[i], "--next") == 0) {
      useNextLoader = true;
    } else if (std::strcmp(argv[i], "--no-cull") == 0) {
      noCull = true;
    } else if (std::strcmp(argv[i], "--cam-dolly") == 0 && (i + 1) < argc) {
      camDolly = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(argv[i], "--camera") == 0 && (i + 1) < argc) {
      cameraName = argv[++i];
    } else if (std::strcmp(argv[i], "--no-composition") == 0) {
      noComposition = true;
    } else if (std::strcmp(argv[i], "--defer-payloads") == 0) {
      deferPayloads = true;
    } else if (std::strcmp(argv[i], "--load-payloads") == 0) {
      deferPayloads = false;
    } else if (std::strcmp(argv[i], "--defer-references") == 0) {
      deferReferences = true;
    } else if (std::strcmp(argv[i], "--allow-parent-paths") == 0) {
      allowParentPaths = true;
    } else if ((std::strcmp(argv[i], "--time") == 0 ||
                std::strcmp(argv[i], "--frame") == 0) &&
               (i + 1) < argc) {
      timeCode = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--skinning") == 0 && (i + 1) < argc) {
      const char* mode = argv[++i];
      if (std::strcmp(mode, "cpu") == 0) {
        skinningMode = tusdview::SkinningMode::CPU;
      } else if (std::strcmp(mode, "gpu") == 0) {
        skinningMode = tusdview::SkinningMode::GPU;
      } else if (std::strcmp(mode, "auto") == 0) {
        skinningMode = tusdview::SkinningMode::Auto;
      } else {
        LOGE("--skinning must be auto, cpu, or gpu");
        return 1;
      }
    } else if (std::strcmp(argv[i], "--rt") == 0) {
      wantRt = true;
    } else if (std::strcmp(argv[i], "--cuda") == 0) {
      wantCuda = true;
    } else if (std::strcmp(argv[i], "--hip") == 0) {
      wantHip = true;
    } else if (std::strcmp(argv[i], "--rt-samples") == 0 && i + 1 < argc) {
      rtSamples = std::atoi(argv[++i]);
      if (rtSamples < 1) rtSamples = 1;
    } else if (std::strcmp(argv[i], "--lod-stream") == 0) {
      lodStream = true;
    } else if (std::strcmp(argv[i], "--max-mem") == 0 && i + 1 < argc) {
      lodMaxMem = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--max-vram") == 0 && i + 1 < argc) {
      lodMaxVram = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--max-instances") == 0 && i + 1 < argc) {
      rtMaxInstances = std::atoll(argv[++i]);
      if (rtMaxInstances < 0) rtMaxInstances = 0;
    } else if (std::strcmp(argv[i], "--wireframe") == 0) {
      wantWireframe = true;
    } else if (std::strcmp(argv[i], "--material-id") == 0) {
      wantMaterialId = true;
    } else if (std::strcmp(argv[i], "--mode") == 0 && (i + 1) < argc) {
      const char* m = argv[++i];
      if (!std::strcmp(m, "shaded")) wantMode = tusdview::RenderMode::Shaded;
      else if (!std::strcmp(m, "wireframe")) wantMode = tusdview::RenderMode::Wireframe;
      else if (!std::strcmp(m, "normals")) wantMode = tusdview::RenderMode::Normals;
      else if (!std::strcmp(m, "material-id")) wantMode = tusdview::RenderMode::MaterialId;
      else if (!std::strcmp(m, "geom-normal")) wantMode = tusdview::RenderMode::GeomNormal;
      else if (!std::strcmp(m, "uv")) wantMode = tusdview::RenderMode::Uv;
      else if (!std::strcmp(m, "depth")) wantMode = tusdview::RenderMode::Depth;
      else if (!std::strcmp(m, "albedo")) wantMode = tusdview::RenderMode::Albedo;
      else if (!std::strcmp(m, "facing")) wantMode = tusdview::RenderMode::Facing;
      else if (!std::strcmp(m, "roughness")) wantMode = tusdview::RenderMode::Roughness;
      else if (!std::strcmp(m, "metallic")) wantMode = tusdview::RenderMode::Metallic;
      else if (!std::strcmp(m, "emissive")) wantMode = tusdview::RenderMode::Emissive;
      else if (!std::strcmp(m, "opacity")) wantMode = tusdview::RenderMode::Opacity;
      else if (!std::strcmp(m, "position")) wantMode = tusdview::RenderMode::Position;
      else if (!std::strcmp(m, "barycentric")) wantMode = tusdview::RenderMode::Barycentric;
      else if (!std::strcmp(m, "prim-id")) wantMode = tusdview::RenderMode::PrimId;
      else if (!std::strcmp(m, "mesh-id")) wantMode = tusdview::RenderMode::MeshId;
      else if (!std::strcmp(m, "purpose")) wantMode = tusdview::RenderMode::Purpose;
      else if (!std::strcmp(m, "missing-normals")) wantMode = tusdview::RenderMode::MissingNormals;
      else if (!std::strcmp(m, "double-sided")) wantMode = tusdview::RenderMode::DoubleSided;
      else if (!std::strcmp(m, "skin-weights")) wantMode = tusdview::RenderMode::SkinWeights;
      else if (!std::strcmp(m, "tangent")) wantMode = tusdview::RenderMode::Tangent;
      else if (!std::strcmp(m, "uv-checker")) wantMode = tusdview::RenderMode::UvChecker;
      else if (!std::strcmp(m, "ao")) wantMode = tusdview::RenderMode::AmbientOcclusion;
      else if (!std::strcmp(m, "curvature")) wantMode = tusdview::RenderMode::Curvature;
      else if (!std::strcmp(m, "instance-id")) wantMode = tusdview::RenderMode::InstanceId;
      else if (!std::strcmp(m, "bvh-heatmap")) wantMode = tusdview::RenderMode::BvhHeatmap;
      else if (!std::strcmp(m, "soft-shadow")) wantMode = tusdview::RenderMode::SoftShadow;
      else if (!std::strcmp(m, "kind")) wantMode = tusdview::RenderMode::Kind;
      else if (!std::strcmp(m, "udim")) wantMode = tusdview::RenderMode::UdimTile;
      else if (!std::strcmp(m, "uv1")) wantMode = tusdview::RenderMode::Uv1;
      else if (!std::strcmp(m, "blend-influence")) wantMode = tusdview::RenderMode::BlendInfluence;
      else if (!std::strcmp(m, "texel-density")) wantMode = tusdview::RenderMode::TexelDensity;
      else if (!std::strcmp(m, "source-face-id")) wantMode = tusdview::RenderMode::SourceFaceId;
      else { LOGE("--mode: unknown '%s'", m); return 1; }
    } else if (std::strcmp(argv[i], "--select") == 0 && (i + 1) < argc) {
      // Select a prim by absolute path once loaded (highlights it; a GeomSubset
      // highlights just its faces). Also handy for headless screenshots.
      wantSelect = argv[++i];
    } else if (std::strcmp(argv[i], "--blend") == 0 && (i + 1) < argc) {
      // --blend NAME=WEIGHT (repeatable): manually drive a blendshape weight,
      // overriding the SkelAnimation. Honors in-between shapes. For headless
      // posing + the Maya-like blend editor.
      const char* spec = argv[++i];
      const char* eq = std::strchr(spec, '=');
      if (!eq) { LOGE("--blend expects NAME=WEIGHT, got '%s'", spec); return 1; }
      wantBlend.emplace_back(std::string(spec, eq), std::atof(eq + 1));
    } else if (std::strcmp(argv[i], "--mcp-stdio") == 0) {
      mcpStdio = true;
    } else if (std::strncmp(argv[i], "--mcp-http", 10) == 0) {
      const char* eq = std::strchr(argv[i], '=');
      mcpHttpPort = eq ? std::atoi(eq + 1) : 8080;
      if (mcpHttpPort <= 0) mcpHttpPort = 8080;
    } else if (std::strcmp(argv[i], "--mcp") == 0) {
      mcpStdio = true;
      if (mcpHttpPort == 0) mcpHttpPort = 8080;
    } else if (std::strncmp(argv[i], "--stream-http", 13) == 0) {
      // Optional value: `--stream-http=PORT`, `--stream-http PORT`, or bare
      // `--stream-http` (defaults to 8090). The space form consumes the next
      // argument only when it starts with a digit (else it's a positional/flag).
      const char* eq = std::strchr(argv[i], '=');
      if (eq) {
        streamHttpPort = std::atoi(eq + 1);
      } else if ((i + 1) < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
        streamHttpPort = std::atoi(argv[++i]);
      } else {
        streamHttpPort = 8090;
      }
      if (streamHttpPort <= 0) streamHttpPort = 8090;
    } else if (std::strcmp(argv[i], "--stream-codec") == 0 && (i + 1) < argc) {
      streamCodec = argv[++i];
    } else if (std::strcmp(argv[i], "--stream-motion-res") == 0 && (i + 1) < argc) {
      streamMotionRes = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--stream-idle-ms") == 0 && (i + 1) < argc) {
      streamIdleMs = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--stream-motion-quality") == 0 &&
               (i + 1) < argc) {
      streamMotionQuality = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
      std::printf(
          "Usage: tusdview [--config PATH] [--backend gl|vk] [--rt] [--frames N] "
          "[--screenshot out.ppm]\n"
          "                [--max-tris N] [--time-budget SECONDS] [--ui-scale S]\n"
          "                [--no-composition] [--defer-payloads | --load-payloads] "
          "[--defer-references] [--time CODE] [--skinning auto|cpu|gpu]\n"
          "                [--headless] [--mcp-stdio] [--mcp-http[=PORT]] [--mcp] "
          "[file.usd|usda|usdc|usdz]\n"
          "  --config PATH Load JSON startup config (otherwise uses the platform "
          "default config path).\n"
          "  --backend gl|vk Select renderer backend (default: Vulkan when built "
          "and available, otherwise OpenGL).\n"
          "  --rt          Use Vulkan ray tracing (ray query) when supported "
          "(implies --backend vk).\n"
          "  --cuda        Ray-trace the screenshot on CUDA (driver API + NVRTC "
          "loaded at runtime via cuew; falls back if no CUDA device).\n"
          "  --hip         Ray-trace the screenshot on HIP/ROCm (loaded at runtime "
          "via hipew + hiprtc; falls back if no AMD/ROCm device).\n"
          "  --rt-samples N  Supersampled AA for the --cuda/--hip screenshot "
          "(Halton sub-pixel jitter; default 1 = off).\n"
          "  --max-instances N  Cap the --cuda/--hip 2-level-BVH instance count "
          "(default 16M; 0 = unlimited). Bounds the host BVH build for massively "
          "instanced scenes (e.g. Moana Island).\n"
          "  --lod-stream  View-dependent district LOD (needs --next): promote "
          "the camera-nearest districts to full under memory budgets.\n"
          "  --max-mem G / --max-vram G  Host / GPU GiB budgets for --lod-stream "
          "(0 = auto, 50%).\n"
          "  --camera NAME Frame a named USD Camera (--next path) instead of "
          "auto-fitting the whole scene (needed for vast scenes, e.g. Caldera).\n"
          "  --max-asset-bytes N  Override the per-asset composition read cap "
          "(default 512M; accepts K/M/G suffix, e.g. 2G) for scenes with large "
          "single crates (e.g. Moore Lane's 896MB subLayer).\n"
          "  --wireframe   Start in wireframe render mode (raster + both RT "
          "backends draw triangle edges only).\n"
          "  --material-id Start in material-id visualization (a distinct flat "
          "color per material; all backends).\n"
          "  --mode NAME   Start in a render mode: shaded|wireframe|normals|"
          "material-id|geom-normal|uv|depth (all backends).\n"
          "  --blend NAME=W  Manually set a blendshape weight (repeatable), "
          "overriding the SkelAnimation; honors in-between shapes. Also editable "
          "live in the Inspector's Blend Shapes panel.\n"
          "  --headless    Windowless offscreen rendering, no display needed "
          "(Vulkan only; needs --frames + --screenshot/--window-shot).\n"
          "  --next        Load via the `next` lazy loader + tydra-next converter "
          "(flat-shaded OpenGL preview for large scenes; defaults to --backend gl).\n"
          "  --no-composition  Load the root layer only (skip USD composition arcs).\n"
          "  --defer-payloads  Lazy payloads: skip payload arcs on load; load on "
          "demand from the GUI (default for interactive runs).\n"
          "  --load-payloads   Compose payload arcs eagerly (default for "
          "--frames/headless runs).\n"
          "  --defer-references  Also defer `references` arcs (loaded on demand "
          "like payloads). Non-standard: USD assumes references always resolve, "
          "so most scene content stays unloaded until requested.\n"
          "  --allow-parent-paths  Permit parent-directory ('..') segments in "
          "composition asset paths (rejected by default as unsafe). Needed by some "
          "production scenes, e.g. Animal Logic ALab's `../lightingrenderovers/`.\n"
          "  --time CODE   Evaluate the scene at this USD time code (animated "
          "transforms/points/skinning). Useful with --frames for a screenshot at "
          "a specific frame. Interactive runs play from the Timeline panel.\n"
          "  --skinning MODE  Skinning path: auto (default), cpu, or gpu.\n"
          "  --mcp-stdio   Run the MCP server over stdio (JSON-RPC on stdin/stdout).\n"
          "  --mcp-http    Run the MCP server over HTTP (default port 8080).\n"
          "  --mcp         Both transports.\n"
          "  --stream-http[=PORT]  WebSocket browser viewer streaming the window "
          "(incl. ImGui); default port 8090. Navigate/click from the browser.\n"
          "  --stream-codec png|qoi  Idle-refinement codec sent when the view is "
          "stable (default png; lossless).\n"
          "  --stream-motion-res PX  Long-edge cap for the low-quality JPEG sent "
          "while moving (default 1280).\n"
          "  --stream-motion-quality N  JPEG quality (1-100) for motion frames "
          "(default 45).\n"
          "  --stream-idle-ms MS  Input-quiet time before the lossless refine "
          "frame is sent (default 350).\n");
      return 0;
    } else if (argv[i][0] != '-') {
      file = argv[i];
    }
  }

  // Ray tracing is a Vulkan technique, so --rt implies the Vulkan backend.
  if (wantRt) {
    backend = tusdview::Backend::Vulkan;
    backendExplicit = true;
  }
  // Windowless rendering is a Vulkan-only path (GL needs a window/context).
  if (headless) {
    backend = tusdview::Backend::Vulkan;
    backendExplicit = true;
  }
  // The `next` loader is the OpenGL large-scene mesh preview; default it to the
  // GL backend unless the user picked one explicitly (it feeds either backend).
  if (useNextLoader && !backendExplicit) {
    backend = tusdview::Backend::GL;
  }

#if !defined(HAVE_VULKAN)
  if (backend == tusdview::Backend::Vulkan) {
    LOGW("Vulkan backend not compiled in; using OpenGL.");
    backend = tusdview::Backend::GL;
    wantRt = false;
  }
#endif

  const tusdview::ConfigLoadResult config = tusdview::LoadStartupConfig(configPath);
  if (config.status == tusdview::ConfigLoadStatus::Error) {
    if (!config.path.empty()) {
      LOGE("config %s: %s", config.path.string().c_str(), config.error.c_str());
    } else {
      LOGE("config: %s", config.error.c_str());
    }
    if (config.explicitPath) return 1;
    LOGW("ignoring invalid default config and continuing with built-in defaults.");
  } else if (config.status == tusdview::ConfigLoadStatus::Loaded) {
    LOGI("loaded config %s", config.path.string().c_str());
    for (const std::string& warning : config.warnings) {
      LOGW("config %s: %s", config.path.string().c_str(), warning.c_str());
    }
  }

  tusdview::App app(backend);
  if (config.status == tusdview::ConfigLoadStatus::Loaded) {
    if (config.config.fontSizePx) app.setFontSize(*config.config.fontSizePx);
    if (config.config.windowScale) app.setWindowScale(*config.config.windowScale);
    if (config.config.windowWidth && config.config.windowHeight) {
      app.setWindowSize(*config.config.windowWidth, *config.config.windowHeight);
    }
    if (config.config.orbitSensitivity) {
      app.setOrbitSensitivity(*config.config.orbitSensitivity);
    }
    if (config.config.panSensitivity) {
      app.setPanSensitivity(*config.config.panSensitivity);
    }
    if (config.config.dollySensitivity) {
      app.setDollySensitivity(*config.config.dollySensitivity);
    }
    if (config.config.invertDolly) {
      app.setInvertDolly(*config.config.invertDolly);
    }
    app.setRecentScenes(config.config.recentScenes);
  }
  // Persist the recent-scenes list back to the resolved config path (the default
  // platform path when none was given), so File > Open Recent survives restarts.
  if (!config.path.empty()) app.setConfigPath(config.path);
  // USD composition options: CLI > config > defaults. Payloads default to
  // lazy (deferred) interactively; headless/--frames runs load them eagerly so
  // screenshots are complete.
  {
    tusdview::LoadOptions lo;
    lo.composition = !noComposition;
    if (config.status == tusdview::ConfigLoadStatus::Loaded) {
      if (config.config.composition && !noComposition) {
        lo.composition = *config.config.composition;
      }
      if (config.config.payloadPolicy && !deferPayloads.has_value()) {
        deferPayloads = (*config.config.payloadPolicy == "defer");
      }
    }
    const bool defer = deferPayloads.value_or(maxFrames < 0 && !headless);
    lo.payloadPolicy = defer ? tusdview::PayloadPolicy::DeferAll
                             : tusdview::PayloadPolicy::LoadAll;
    // Explicit opt-in only (no headless default flip): deferring references is
    // non-standard, so honor the flag verbatim even for --frames runs.
    lo.deferReferences = deferReferences;
    lo.allowParentRelativePaths = allowParentPaths;
    if (timeCode.has_value()) lo.timecode = *timeCode;
    app.setLoadOptions(lo);
  }
  app.setLoadBudget(static_cast<std::size_t>(maxTris < 0 ? 0 : maxTris), timeBudget);
  app.setGpuBudget(
      maxGpuMemGiB > 0.0 ? static_cast<std::size_t>(maxGpuMemGiB * 1024.0 *
                                                    1024.0 * 1024.0)
                         : 0,
      static_cast<std::size_t>(maxDrawMeshes < 0 ? 0 : maxDrawMeshes));
  app.setRobustFrame(robustFrame);
  app.setRtLod(rtLod, rtLodFullPx, rtLodCullPx, rtLodBand);
  app.setRasterLod(rasterLod, rasterLodFullPx, rasterLodCullPx);
  if (uiScale) {
    if (*uiScale > 0.25f) {
      app.clearWindowSizeOverride();
      app.setUiScale(*uiScale);
    } else {
      LOGW("ignoring invalid --ui-scale %.3f (must be > 0.25)", *uiScale);
    }
  }
  app.setUseNextLoader(useNextLoader);
  app.setCullEnabled(!noCull);
  app.setCamDolly(camDolly);
  app.setWindowShot(windowShot);
  app.setRequestRayTracing(wantRt);
  app.setAllowBackendFallback(!backendExplicit && backend == tusdview::Backend::Vulkan);
  app.setSkinningMode(skinningMode);
  app.setMcpStdio(mcpStdio);
  app.setMcpHttp(mcpHttpPort);
  app.setStreamHttp(streamHttpPort);
  app.setStreamCodec(streamCodec);
  app.setStreamMotionRes(streamMotionRes);
  app.setStreamMotionQuality(streamMotionQuality);
  app.setStreamIdleMs(streamIdleMs);
  app.setHeadless(headless);
  app.setThreaded(threaded);
  app.setCudaRt(wantCuda);
  app.setHipRt(wantHip);
  app.setRtSamples(rtSamples);
  app.setRtMaxInstances(static_cast<size_t>(rtMaxInstances));
  app.setLodStream(lodStream);
  app.setLodMaxMemGiB(lodMaxMem);
  app.setLodMaxVramGiB(lodMaxVram);
  app.setCameraName(cameraName);
  if (wantWireframe) app.setRenderMode(tusdview::RenderMode::Wireframe);
  if (wantMaterialId) app.setRenderMode(tusdview::RenderMode::MaterialId);
  if (wantMode) app.setRenderMode(*wantMode);
  for (const auto& bw : wantBlend) app.setBlendWeight(bw.first, bw.second);
  if (!wantSelect.empty()) app.setInitialSelection(wantSelect);
  return app.run(file, maxFrames, screenshot);
}
