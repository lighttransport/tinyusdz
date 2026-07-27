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
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <string>

#include "security-policy.hh"

#include "app.hh"
#include "config.hh"
#include "log.hh"
#include "renderer.hh"
#include "tydra/next/resource-budget.hh"

namespace {

enum class LargeSceneProfile { Off, Auto, Caldera, Island, ALab };

const char* ProfileName(LargeSceneProfile p) {
  switch (p) {
    case LargeSceneProfile::Off: return "off";
    case LargeSceneProfile::Auto: return "auto";
    case LargeSceneProfile::Caldera: return "caldera";
    case LargeSceneProfile::Island: return "island";
    case LargeSceneProfile::ALab: return "alab";
  }
  return "off";
}

std::string LowerCopy(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

bool ParseProfile(const char* s, LargeSceneProfile* out) {
  if (!s || !out) return false;
  const std::string v = LowerCopy(s);
  if (v == "off") *out = LargeSceneProfile::Off;
  else if (v == "auto") *out = LargeSceneProfile::Auto;
  else if (v == "caldera") *out = LargeSceneProfile::Caldera;
  else if (v == "island") *out = LargeSceneProfile::Island;
  else if (v == "alab") *out = LargeSceneProfile::ALab;
  else return false;
  return true;
}

LargeSceneProfile DetectProfileFromPath(const std::string& path) {
  const std::string p = LowerCopy(path);
  if (p.find("caldera") != std::string::npos) return LargeSceneProfile::Caldera;
  if (p.find("island") != std::string::npos ||
      p.find("moana") != std::string::npos) return LargeSceneProfile::Island;
  if (p.find("alab") != std::string::npos ||
      p.find("animal_logic") != std::string::npos ||
      p.find("animal-logic") != std::string::npos) {
    return LargeSceneProfile::ALab;
  }
  return LargeSceneProfile::Off;
}

std::uint64_t ParseByteCount(const std::string& text) {
  std::string v = text;
  std::uint64_t mul = 1;
  if (!v.empty()) {
    char s = v.back();
    if (s == 'k' || s == 'K') { mul = 1024ull; v.pop_back(); }
    else if (s == 'm' || s == 'M') { mul = 1024ull * 1024ull; v.pop_back(); }
    else if (s == 'g' || s == 'G') {
      mul = 1024ull * 1024ull * 1024ull;
      v.pop_back();
    }
  }
  return static_cast<std::uint64_t>(std::strtoull(v.c_str(), nullptr, 10)) * mul;
}

bool ParsePrimLevel(const std::string& text, std::string* prim, int* level) {
  const size_t eq = text.rfind('=');
  const size_t colon = text.rfind(':');
  size_t sep = std::string::npos;
  if (eq != std::string::npos && colon != std::string::npos) {
    sep = std::max(eq, colon);
  } else {
    sep = (eq != std::string::npos) ? eq : colon;
  }
  if (sep == std::string::npos || sep == 0 || sep + 1 >= text.size()) return false;
  std::string value = text.substr(sep + 1);
  char* end = nullptr;
  long v = std::strtol(value.c_str(), &end, 10);
  if (!end || *end != '\0' || v < 0 || v > 16) return false;
  *prim = text.substr(0, sep);
  *level = static_cast<int>(v);
  return !prim->empty();
}

bool ParseWindowSize(const char* text, int* width, int* height) {
  if (!text || !width || !height) return false;
  char* widthEnd = nullptr;
  const long parsedWidth = std::strtol(text, &widthEnd, 10);
  if (!widthEnd || widthEnd == text || *widthEnd != 'x') return false;
  char* heightEnd = nullptr;
  const long parsedHeight = std::strtol(widthEnd + 1, &heightEnd, 10);
  if (!heightEnd || heightEnd == widthEnd + 1 || *heightEnd != '\0') return false;
  constexpr long kMaxWindowDimension = 32768;
  if (parsedWidth <= 0 || parsedWidth > kMaxWindowDimension ||
      parsedHeight <= 0 || parsedHeight > kMaxWindowDimension) {
    return false;
  }
  *width = static_cast<int>(parsedWidth);
  *height = static_cast<int>(parsedHeight);
  return true;
}

// Host memory the budget tree may plan against: MemAvailable, capped at the
// 32 GiB the policy targets (planning against a 256 GiB workstation's full RAM
// would size the stage/geometry limits far past anything sensible). Falls back
// to 32 GiB where /proc/meminfo does not exist (macOS, Windows).
uint64_t HostMemoryCapacityBytes() {
  constexpr uint64_t kTarget = tinyusdz::tydra::next::GiB(32);
  std::ifstream f("/proc/meminfo");
  std::string tok;
  while (f >> tok) {
    if (tok == "MemAvailable:") {
      uint64_t kb = 0;
      f >> kb;
      const uint64_t bytes = kb * 1024ull;
      return bytes ? std::min(bytes, kTarget) : kTarget;
    }
    std::getline(f, tok);
  }
  return kTarget;
}

}  // namespace

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
  std::string renderReport;
  std::string windowShot;
  int maxFrames = -1;
  int windowWidth = 0;
  int windowHeight = 0;
  bool windowSizeExplicit = false;
  long long maxTris = 0;      // 0 = default budget
  double maxGpuMemGiB = 0.0;  // --max-gpu-mem: raster full-mesh VRAM cap (GiB)
  // --vram-budget: the ONE number the whole budget tree descends from. Left at 0
  // it is probed from the device (see QueryDeviceLocalVramBytes).
  double vramBudgetGiB = 0.0;
  bool vramBudgetExplicit = false;
  long long maxDrawMeshes = 0;  // --max-draw-meshes: raster full-mesh count cap
  bool robustFrame = true;      // trim outlier meshes from fit-all auto-frame
  bool rtLod = false;           // --rt-lod: view-dependent RT instance LOD
  float rtLodFullPx = 0.0f;     // 0 => keep App default
  float rtLodCullPx = -1.0f;    // <0 => keep App default
  float rtLodBand = -1.0f;      // <0 => keep App default (stochastic band width)
  bool rasterLod = false;       // --raster-lod: view-dependent raster instance LOD
  float rasterLodFullPx = 0.0f; // 0 => keep App default
  float rasterLodCullPx = -1.0f;// <0 => keep App default
  LargeSceneProfile largeSceneProfile = LargeSceneProfile::Off;
  bool maxTrisExplicit = false;
  bool maxGpuMemExplicit = false;
  bool maxDrawMeshesExplicit = false;
  bool rtLodExplicit = false;
  bool rtLodFullExplicit = false;
  bool rtLodCullExplicit = false;
  bool rtLodBandExplicit = false;
  bool rasterLodExplicit = false;
  bool rasterLodFullExplicit = false;
  bool rasterLodCullExplicit = false;
  bool useNextExplicit = false;
  bool lodMaxMemExplicit = false;
  bool lodMaxVramExplicit = false;
  bool allowParentPathsExplicit = false;
  bool maxAssetBytesExplicit = false;
  std::uint64_t maxAssetReadBytes = 0;
  double timeBudget = 0.0;    // 0 = unlimited
  unsigned compositionThreads = 0;
  unsigned conversionThreads = 0;
  double uploadBudgetMs = 8.0;
  size_t streamBufferMB = 64;
  bool timing = false;
  bool quitAfterFullUpload = false;
  std::optional<float> uiScale;  // Explicit CLI override for font/widget/window scale.
  bool wantRt = false;        // request Vulkan ray tracing (if supported)
  tusdview::RendererDevicePreference devicePreference;
  bool vkDeviceExplicit = false;
  bool wantCuda = false;      // --cuda: CUDA BVH ray-traced screenshot (cuew runtime)
  std::string cudaCacheDir;   // --cuda-cache-dir: override compiled PTX cache
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
  bool useNextLoader = true;              // next-core is the default scene path
  bool noCull = false;                     // --no-cull: disable frustum culling
  bool showGrid = true;                    // --no-grid: deterministic clean capture
  bool showSkeleton = true;                // --no-skeleton: hide skeleton helpers
  float camDolly = 1.0f;                    // --cam-dolly: fitted-distance scale
  std::string cameraName;                   // --camera: USD camera to frame (--next)
  tusdview::CameraConform cameraConform{tusdview::CameraConform::Fit};
  bool cameraConformExplicit = false;
  bool viewDirExplicit = false;              // --view-dir: deterministic auto-fit view
  float viewDir[3] = {0.0f, 0.0f, -1.0f};   // normalized eye-to-target direction
  bool noComposition = false;             // --no-composition: root layer only
  std::optional<bool> deferPayloads;      // --defer-payloads / --load-payloads
  bool deferReferences = false;           // --defer-references (explicit opt-in)
  bool allowParentPaths = true;           // USD layer-relative paths may use '..'.
  // Bound the interactive viewer's default texture residency. Each of these
  // defaults remains explicitly disableable by the command-line switches.
  tusdview::TextureRuntimeOptions textureOptions;
  textureOptions.maxTextureSize = 4096;
  textureOptions.compression = tusdview::TextureCompressionMode::Auto;
  textureOptions.keepCompressed = true;
  textureOptions.generateMips = true;
  bool domeIblExplicit = false;
  bool maxTextureSizeExplicit = false;
  bool textureBudgetExplicit = false;
  std::optional<int> subdivisionLevel;
  bool subdivisionAuto = false;
  bool subdivisionAutoExplicit = false;
  int subdivisionAutoMaxLevel = 3;
  bool subdivisionAutoMaxExplicit = false;
  std::map<std::string, int> subdivisionPrimLevels;
  std::optional<double> timeCode;         // --time T: evaluate the scene at this
                                          // time code (animated screenshots)
  tusdview::SkinningMode skinningMode = tusdview::SkinningMode::Auto;
  bool playAnim = false;                  // --play: start timeline playback on load

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
    } else if (std::strcmp(argv[i], "--vk-device") == 0 && (i + 1) < argc) {
      devicePreference.vulkanDevice = argv[++i];
      vkDeviceExplicit = true;
      backend = tusdview::Backend::Vulkan;
      backendExplicit = true;
    } else if (std::strncmp(argv[i], "--vk-device=", 12) == 0) {
      devicePreference.vulkanDevice = argv[i] + 12;
      vkDeviceExplicit = true;
      backend = tusdview::Backend::Vulkan;
      backendExplicit = true;
    } else if (std::strcmp(argv[i], "--frames") == 0 && (i + 1) < argc) {
      maxFrames = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--size") == 0) {
      if ((i + 1) >= argc ||
          !ParseWindowSize(argv[++i], &windowWidth, &windowHeight)) {
        LOGE("--size must be WxH with dimensions in the range 1..32768");
        return 1;
      }
      windowSizeExplicit = true;
    } else if (std::strcmp(argv[i], "--screenshot") == 0 && (i + 1) < argc) {
      screenshot = argv[++i];
    } else if (std::strcmp(argv[i], "--render-report") == 0 &&
               (i + 1) < argc) {
      renderReport = argv[++i];
    } else if (std::strcmp(argv[i], "--max-tris") == 0 && (i + 1) < argc) {
      maxTris = std::atoll(argv[++i]);
      maxTrisExplicit = true;
    } else if (std::strcmp(argv[i], "--max-asset-bytes") == 0 && (i + 1) < argc) {
      // Override per-asset composition/resolver read cap (default 512MB).
      // Accepts a byte count with optional K/M/G suffix, e.g. 2G.
      maxAssetReadBytes = ParseByteCount(argv[++i]);
      maxAssetBytesExplicit = true;
    } else if (std::strcmp(argv[i], "--max-gpu-mem") == 0 && (i + 1) < argc) {
      maxGpuMemGiB = std::atof(argv[++i]);
      maxGpuMemExplicit = true;
    } else if (std::strcmp(argv[i], "--vram-budget") == 0 && (i + 1) < argc) {
      vramBudgetGiB = std::atof(argv[++i]);
      vramBudgetExplicit = true;
    } else if (std::strcmp(argv[i], "--max-draw-meshes") == 0 && (i + 1) < argc) {
      maxDrawMeshes = std::atoll(argv[++i]);
      maxDrawMeshesExplicit = true;
    } else if (std::strcmp(argv[i], "--no-robust-frame") == 0) {
      robustFrame = false;
    } else if (std::strcmp(argv[i], "--rt-lod") == 0) {
      rtLod = true;
      rtLodExplicit = true;
    } else if (std::strcmp(argv[i], "--no-rt-lod") == 0) {
      rtLod = false;
      rtLodExplicit = true;
    } else if (std::strcmp(argv[i], "--rt-lod-full-px") == 0 && (i + 1) < argc) {
      rtLodFullPx = static_cast<float>(std::atof(argv[++i]));
      rtLodFullExplicit = true;
    } else if (std::strcmp(argv[i], "--rt-lod-cull-px") == 0 && (i + 1) < argc) {
      rtLodCullPx = static_cast<float>(std::atof(argv[++i]));
      rtLodCullExplicit = true;
    } else if (std::strcmp(argv[i], "--rt-lod-band") == 0 && (i + 1) < argc) {
      rtLodBand = static_cast<float>(std::atof(argv[++i]));
      rtLodBandExplicit = true;
    } else if (std::strcmp(argv[i], "--raster-lod") == 0) {
      rasterLod = true;
      rasterLodExplicit = true;
    } else if (std::strcmp(argv[i], "--raster-lod-full-px") == 0 && (i + 1) < argc) {
      rasterLodFullPx = static_cast<float>(std::atof(argv[++i]));
      rasterLodFullExplicit = true;
    } else if (std::strcmp(argv[i], "--raster-lod-cull-px") == 0 && (i + 1) < argc) {
      rasterLodCullPx = static_cast<float>(std::atof(argv[++i]));
      rasterLodCullExplicit = true;
    } else if (std::strcmp(argv[i], "--large-scene-profile") == 0 && (i + 1) < argc) {
      if (!ParseProfile(argv[++i], &largeSceneProfile)) {
        LOGE("--large-scene-profile must be off, auto, caldera, island, or alab");
        return 1;
      }
    } else if (std::strncmp(argv[i], "--large-scene-profile=", 22) == 0) {
      if (!ParseProfile(argv[i] + 22, &largeSceneProfile)) {
        LOGE("--large-scene-profile must be off, auto, caldera, island, or alab");
        return 1;
      }
    } else if (std::strcmp(argv[i], "--time-budget") == 0 && (i + 1) < argc) {
      timeBudget = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--compose-threads") == 0 && (i + 1) < argc) {
      compositionThreads = static_cast<unsigned>(std::max(1, std::atoi(argv[++i])));
    } else if (std::strcmp(argv[i], "--convert-threads") == 0 && (i + 1) < argc) {
      conversionThreads = static_cast<unsigned>(std::max(1, std::atoi(argv[++i])));
    } else if (std::strcmp(argv[i], "--upload-budget-ms") == 0 && (i + 1) < argc) {
      uploadBudgetMs = std::clamp(std::atof(argv[++i]), 1.0, 33.0);
    } else if (std::strcmp(argv[i], "--stream-buffer-mb") == 0 &&
               (i + 1) < argc) {
      streamBufferMB = static_cast<size_t>(
          std::clamp(std::atoi(argv[++i]), 4, 1024));
    } else if (std::strcmp(argv[i], "--quit-after-full-upload") == 0) {
      quitAfterFullUpload = true;
    } else if (std::strcmp(argv[i], "--timing") == 0) {
      timing = true;
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
      useNextExplicit = true;
    } else if (std::strcmp(argv[i], "--legacy-load") == 0 ||
               std::strcmp(argv[i], "--legacyLoad") == 0) {
      useNextLoader = false;
      useNextExplicit = true;
    } else if (std::strcmp(argv[i], "--no-cull") == 0) {
      noCull = true;
    } else if (std::strcmp(argv[i], "--no-grid") == 0) {
      showGrid = false;
    } else if (std::strcmp(argv[i], "--no-skeleton") == 0) {
      showSkeleton = false;
    } else if (std::strcmp(argv[i], "--cam-dolly") == 0 && (i + 1) < argc) {
      camDolly = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(argv[i], "--camera") == 0 && (i + 1) < argc) {
      cameraName = argv[++i];
    } else if (std::strcmp(argv[i], "--camera-conform") == 0 &&
               (i + 1) < argc) {
      cameraConformExplicit = true;
      const std::string value = argv[++i];
      if (value == "fit") cameraConform = tusdview::CameraConform::Fit;
      else if (value == "crop") cameraConform = tusdview::CameraConform::Crop;
      else if (value == "horizontal")
        cameraConform = tusdview::CameraConform::Horizontal;
      else if (value == "vertical")
        cameraConform = tusdview::CameraConform::Vertical;
      else if (value == "none") cameraConform = tusdview::CameraConform::None;
      else {
        LOGE("--camera-conform must be fit, crop, horizontal, vertical, or none");
        return 1;
      }
    } else if (std::strcmp(argv[i], "--view-dir") == 0 && (i + 1) < argc) {
      char trailing = '\0';
      const char* value = argv[++i];
      if (std::sscanf(value, "%f,%f,%f%c", &viewDir[0], &viewDir[1],
                      &viewDir[2], &trailing) != 3 ||
          !std::isfinite(viewDir[0]) || !std::isfinite(viewDir[1]) ||
          !std::isfinite(viewDir[2])) {
        LOGE("--view-dir requires three finite comma-separated values: X,Y,Z");
        return 1;
      }
      const float len = std::sqrt(viewDir[0] * viewDir[0] +
                                  viewDir[1] * viewDir[1] +
                                  viewDir[2] * viewDir[2]);
      if (!(len > 1e-6f)) {
        LOGE("--view-dir must be non-zero");
        return 1;
      }
      viewDir[0] /= len;
      viewDir[1] /= len;
      viewDir[2] /= len;
      viewDirExplicit = true;
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
      allowParentPathsExplicit = true;
    } else if (std::strcmp(argv[i], "--texture-max-size") == 0 && (i + 1) < argc) {
      textureOptions.maxTextureSize = std::atoi(argv[++i]);
      if (textureOptions.maxTextureSize < 0) {
        textureOptions.maxTextureSize = 0;
      }
      maxTextureSizeExplicit = true;
    } else if (std::strcmp(argv[i], "--texture-budget-mb") == 0 && (i + 1) < argc) {
      textureOptions.textureBudgetMB = std::atoi(argv[++i]);
      if (textureOptions.textureBudgetMB < 0) {
        textureOptions.textureBudgetMB = 0;
      }
      textureBudgetExplicit = true;
    } else if (std::strcmp(argv[i], "--subdivision-level") == 0 && (i + 1) < argc) {
      subdivisionLevel = std::max(0, std::atoi(argv[++i]));
    } else if (std::strncmp(argv[i], "--subdivision-level=", 20) == 0) {
      subdivisionLevel = std::max(0, std::atoi(argv[i] + 20));
    } else if (std::strcmp(argv[i], "--subdivision-auto") == 0) {
      subdivisionAuto = true;
      subdivisionAutoExplicit = true;
    } else if (std::strcmp(argv[i], "--no-subdivision-auto") == 0) {
      subdivisionAuto = false;
      subdivisionAutoExplicit = true;
    } else if (std::strcmp(argv[i], "--subdivision-auto-max-level") == 0 && (i + 1) < argc) {
      subdivisionAutoMaxLevel = std::max(0, std::atoi(argv[++i]));
      subdivisionAutoMaxExplicit = true;
    } else if (std::strncmp(argv[i], "--subdivision-auto-max-level=", 29) == 0) {
      subdivisionAutoMaxLevel = std::max(0, std::atoi(argv[i] + 29));
      subdivisionAutoMaxExplicit = true;
    } else if (std::strcmp(argv[i], "--subdivision-prim") == 0 && (i + 1) < argc) {
      std::string prim;
      int level = 0;
      if (!ParsePrimLevel(argv[++i], &prim, &level)) {
        LOGE("--subdivision-prim expects /Prim/Path=N");
        return 1;
      }
      subdivisionPrimLevels[prim] = level;
    } else if (std::strncmp(argv[i], "--subdivision-prim=", 19) == 0) {
      std::string prim;
      int level = 0;
      if (!ParsePrimLevel(argv[i] + 19, &prim, &level)) {
        LOGE("--subdivision-prim expects /Prim/Path=N");
        return 1;
      }
      subdivisionPrimLevels[prim] = level;
    } else if (std::strcmp(argv[i], "--texture-compress") == 0 && (i + 1) < argc) {
      const char* mode = argv[++i];
      if (std::strcmp(mode, "off") == 0) {
        textureOptions.compression = tusdview::TextureCompressionMode::Off;
      } else if (std::strcmp(mode, "bc") == 0) {
        textureOptions.compression = tusdview::TextureCompressionMode::BCn;
      } else if (std::strcmp(mode, "bc7") == 0) {
        textureOptions.compression = tusdview::TextureCompressionMode::BC7;
      } else if (std::strcmp(mode, "astc") == 0) {
        textureOptions.compression = tusdview::TextureCompressionMode::Astc;
      } else if (std::strcmp(mode, "etc2") == 0) {
        textureOptions.compression = tusdview::TextureCompressionMode::Etc2;
      } else if (std::strcmp(mode, "auto") == 0) {
        textureOptions.compression = tusdview::TextureCompressionMode::Auto;
      } else {
        LOGE("--texture-compress must be off, bc, bc7, astc, etc2 or auto");
        return 1;
      }
    } else if (std::strcmp(argv[i], "--texture-keep-compressed") == 0 &&
               (i + 1) < argc) {
      const char* mode = argv[++i];
      if (std::strcmp(mode, "on") == 0) {
        textureOptions.keepCompressed = true;
      } else if (std::strcmp(mode, "off") == 0) {
        textureOptions.keepCompressed = false;
      } else {
        LOGE("--texture-keep-compressed must be on or off");
        return 1;
      }
    } else if (std::strcmp(argv[i], "--texture-mips") == 0 && (i + 1) < argc) {
      const char* mode = argv[++i];
      if (std::strcmp(mode, "off") == 0) {
        textureOptions.generateMips = false;
      } else if (std::strcmp(mode, "on") == 0) {
        textureOptions.generateMips = true;
      } else {
        LOGE("--texture-mips must be on or off");
        return 1;
      }
    } else if (std::strcmp(argv[i], "--dome-ibl") == 0 && (i + 1) < argc) {
      domeIblExplicit = true;
      const char* mode = argv[++i];
      if (std::strcmp(mode, "off") == 0) {
        textureOptions.domeIbl = 0;
      } else if (std::strcmp(mode, "low") == 0) {
        textureOptions.domeIbl = 1;
      } else if (std::strcmp(mode, "high") == 0) {
        textureOptions.domeIbl = 2;
      } else {
        LOGE("--dome-ibl must be off, low or high");
        return 1;
      }
    } else if (std::strcmp(argv[i], "--udim") == 0 && (i + 1) < argc) {
      const char* mode = argv[++i];
      if (std::strcmp(mode, "sparse") == 0) {
        textureOptions.udimMode = tusdview::UdimMode::Sparse;
      } else if (std::strcmp(mode, "atlas") == 0) {
        textureOptions.udimMode = tusdview::UdimMode::Atlas;
      } else {
        LOGE("--udim must be sparse or atlas");
        return 1;
      }
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
    } else if (std::strcmp(argv[i], "--play") == 0) {
      playAnim = true;
    } else if (std::strcmp(argv[i], "--rt") == 0) {
      wantRt = true;
    } else if (std::strcmp(argv[i], "--cuda") == 0) {
      wantCuda = true;
    } else if (std::strcmp(argv[i], "--cuda-cache-dir") == 0) {
      if (i + 1 >= argc) {
        LOGE("--cuda-cache-dir requires a non-empty path");
        return 1;
      }
      cudaCacheDir = argv[++i];
      if (cudaCacheDir.empty()) {
        LOGE("--cuda-cache-dir requires a non-empty path");
        return 1;
      }
    } else if (std::strncmp(argv[i], "--cuda-cache-dir=", 17) == 0) {
      cudaCacheDir = argv[i] + 17;
      if (cudaCacheDir.empty()) {
        LOGE("--cuda-cache-dir requires a non-empty path");
        return 1;
      }
    } else if (std::strcmp(argv[i], "--hip") == 0) {
      wantHip = true;
    } else if (std::strcmp(argv[i], "--rt-samples") == 0 && i + 1 < argc) {
      rtSamples = std::atoi(argv[++i]);
      if (rtSamples < 1) rtSamples = 1;
    } else if (std::strcmp(argv[i], "--lod-stream") == 0) {
      lodStream = true;
    } else if (std::strcmp(argv[i], "--max-mem") == 0 && i + 1 < argc) {
      lodMaxMem = std::atof(argv[++i]);
      lodMaxMemExplicit = true;
    } else if (std::strcmp(argv[i], "--max-vram") == 0 && i + 1 < argc) {
      lodMaxVram = std::atof(argv[++i]);
      lodMaxVramExplicit = true;
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
      else if (!std::strcmp(m, "coat-normal")) wantMode = tusdview::RenderMode::CoatNormal;
      else if (!std::strcmp(m, "coat-weight")) wantMode = tusdview::RenderMode::CoatWeight;
      else if (!std::strcmp(m, "coat-color")) wantMode = tusdview::RenderMode::CoatColor;
      else if (!std::strcmp(m, "coat-roughness")) wantMode = tusdview::RenderMode::CoatRoughness;
      else if (!std::strcmp(m, "specular-f0")) wantMode = tusdview::RenderMode::SpecularF0;
      else if (!std::strcmp(m, "ior-f0")) wantMode = tusdview::RenderMode::IorF0;
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
          "[--size WxH] [--screenshot out.png|out.jpg|out.ppm]\n"
          "                [--max-tris N] [--time-budget SECONDS] [--ui-scale S]\n"
          "                [--no-composition] [--defer-payloads | --load-payloads] "
          "[--defer-references] [--time CODE] [--skinning auto|cpu|gpu]\n"
          "                [--headless] [--mcp-stdio] [--mcp-http[=PORT]] [--mcp] "
          "[file.usd|usda|usdc|usdz]\n"
          "  --config PATH Load JSON startup config (otherwise uses the platform "
          "default config path).\n"
          "  --backend gl|vk Select renderer backend (default: Vulkan when built "
          "and available, otherwise OpenGL).\n"
          "  --vk-device INDEX|NAME  Select a Vulkan physical device by index or "
          "case-insensitive device/driver substring (e.g. 0, nvidia, rtx, llvmpipe). "
          "Also available in config as vulkan_device.\n"
          "  --rt          Use Vulkan ray tracing (ray query) when supported "
          "(implies --backend vk).\n"
          "  --cuda        Ray-trace the screenshot on CUDA (driver API + NVRTC "
          "loaded at runtime via cuew; falls back if no CUDA device).\n"
          "  --cuda-cache-dir PATH  Store compiled CUDA PTX in PATH (default: the "
          "platform cache directory under tusdview/cuda).\n"
          "  --hip         Ray-trace the screenshot on HIP/ROCm (loaded at runtime "
          "via hipew + hiprtc; falls back if no AMD/ROCm device).\n"
          "  --rt-samples N  Supersampled AA for the --cuda/--hip screenshot "
          "(Halton sub-pixel jitter; default 1 = off).\n"
          "  --max-instances N  Cap the --cuda/--hip 2-level-BVH instance count "
          "(default 16M; 0 = unlimited). Bounds the host BVH build for massively "
          "instanced scenes (e.g. Moana Island).\n"
          "  --max-tris N  Cap triangles in the CUDA/HIP software RT scene.\n"
          "  --time-budget SECONDS  Stop the headless run after this wall-time budget.\n"
          "  --size WxH    Set the render/window size. Overrides the startup config.\n"
          "  --ui-scale S  Override the interface scale factor.\n"
          "  --lod-stream  View-dependent district LOD: promote "
          "the camera-nearest districts to full under memory budgets.\n"
          "  --max-mem G / --max-vram G  Host / GPU GiB budgets for --lod-stream "
          "(0 = auto, 50%%).\n"
          "  --camera NAME Frame a named USD Camera instead of "
          "auto-fitting the whole scene (needed for vast scenes, e.g. Caldera).\n"
          "  --camera-conform MODE  Filmback policy: fit, crop, horizontal, "
          "vertical, or none (default: fit).\n"
          "  --view-dir X,Y,Z  Set the normalized world-space eye-to-target "
          "direction after auto-fit; cannot be combined with --camera.\n"
          "  --select /Prim/Path  Select a prim after loading.\n"
          "  --cam-dolly D  Apply a startup dolly offset to the framed camera.\n"
          "  --no-cull / --no-robust-frame  Disable frustum culling or robust "
          "outlier-resistant auto framing.\n"
          "  --no-grid     Hide the ground grid (useful for deterministic captures).\n"
          "  --no-skeleton Hide skeleton helper overlays (useful for AOV comparisons).\n"
          "  --dome-ibl off|fast|quality  Control DomeLight IBL precomputation.\n"
          "  --large-scene-profile off|auto|caldera|island|alab  Resolve a "
          "Vulkan realtime preset for public large scenes. Profiles set existing "
          "large-scene knobs only; explicit CLI flags win. No texture resize or "
          "compression behavior is changed.\n"
          "  --compose-threads N  Composition worker count (default: hardware, capped).\n"
          "  --convert-threads N  Geometry conversion worker count.\n"
          "  --upload-budget-ms N  Interactive upload slice, 1..33 ms.\n"
          "  --stream-buffer-mb N  Bounded CPU geometry queue for interactive "
          "OpenGL streaming (default 64 MiB).\n"
          "  --quit-after-full-upload  Exit after progressive conversion, upload, "
          "and one complete present.\n"
          "  --timing             Print detailed load/conversion timing.\n"
          "  --vram-budget G  GPU memory the large-scene budgets may plan "
          "against (GiB). Default: probed from the device. Everything else "
          "(--max-gpu-mem, texture edge/byte caps, upload staging) is derived "
          "from it, so lowering this one number rehearses a smaller card.\n"
          "  --max-asset-bytes N  Override the per-asset composition read cap "
          "(default 512M; accepts K/M/G suffix, e.g. 2G) for scenes with large "
          "single crates (e.g. Moore Lane's 896MB subLayer).\n"
          "  --wireframe   Start in wireframe render mode (raster + both RT "
          "backends draw triangle edges only).\n"
          "  --material-id Start in material-id visualization (a distinct flat "
          "color per material; all backends).\n"
          "  --mode NAME   Start in a render mode: shaded, wireframe, normals, "
          "material-id, geom-normal, uv, depth, albedo, facing, roughness, "
          "metallic, emissive, opacity, position, barycentric, prim-id, mesh-id, "
          "purpose, missing-normals, double-sided, skin-weights, tangent, "
          "uv-checker, ao, curvature, instance-id, bvh-heatmap, soft-shadow, "
          "kind, udim, uv1, blend-influence, texel-density, source-face-id, coat-normal, coat-weight, coat-color, coat-roughness, specular-f0, ior-f0.\n"
          "  --threaded    Use the optional dedicated GL/Vulkan render thread "
          "when built with TUSDVIEW_ENABLE_GL_THREAD.\n"
          "  --blend NAME=W  Manually set a blendshape weight (repeatable), "
          "overriding the SkelAnimation; honors in-between shapes. Also editable "
          "live in the Inspector's Blend Shapes panel.\n"
          "  --headless    Windowless offscreen rendering, no display needed "
          "(Vulkan only; needs --frames + --screenshot/--window-shot).\n"
          "  --next        Use the default next-core + tydra-next loader "
          "(compatibility flag).\n"
          "  --legacy-load Use the legacy eager loader.\n"
          "  --no-composition  Load the root layer only (skip USD composition arcs).\n"
          "  --defer-payloads  Lazy payloads: skip payload arcs on load; load on "
          "demand from the GUI (default for interactive runs).\n"
          "  --load-payloads   Compose payload arcs eagerly (default for "
          "--frames/headless runs).\n"
          "  --defer-references  Also defer `references` arcs (loaded on demand "
          "like payloads). Non-standard: USD assumes references always resolve, "
          "so most scene content stays unloaded until requested.\n"
          "  --allow-parent-paths  Compatibility flag: parent-directory ('..') "
          "segments are permitted by default because USD anchors them to the "
          "authoring layer.\n"
          "  --texture-max-size N  Downsize decoded textures whose longest edge "
          "exceeds N texels (default 4096; 0 = keep source size).\n"
          "  --texture-budget-mb N  Best-effort decoded texture memory budget "
          "for viewer uploads (0 = unlimited).\n"
          "  --subdivision-level N  Scene-wide conversion-time subdivision "
          "surface refinement level (0 = off). Applies only to meshes whose USD "
          "subdivisionScheme is not none.\n"
          "  --subdivision-prim /Prim/Path=N  Override subdivision level for one "
          "mesh prim. Repeatable; ':' is also accepted as the separator.\n"
          "  --subdivision-auto  Estimate per-prim subdivision levels from the "
          "auto-fit camera screen coverage and re-convert once. Explicit "
          "--subdivision-prim overrides win.\n"
          "  --subdivision-auto-max-level N  Clamp --subdivision-auto levels "
          "(default 3, hard cap 10).\n"
          "  --texture-compress off|bc|bc7|astc|etc2|auto  Request GPU texture "
          "compression (default "
          "auto; selects a supported BC/ASTC/ETC2 format). Backends without "
          "block upload support fall back to resized RGBA8.\n"
          "  --texture-mips on|off  Generate content-aware texture mip chains "
          "(default on).\n"
          "  --texture-keep-compressed  Preserve supported KTX2 block payloads "
          "instead of decoding/re-encoding them.\n"
          "  --udim sparse|atlas  UDIM handling mode (default sparse; atlas rebakes "
          "UVs and can consume much more memory on large tile sets).\n"
          "  --time CODE   Evaluate the scene at this USD time code (animated "
          "transforms/points/skinning). Useful with --frames for a screenshot at "
          "a specific frame. Interactive runs play from the Timeline panel.\n"
          "  --skinning MODE  Skinning path: auto (default), cpu, or gpu.\n"
          "  --play        Start timeline playback on load. With --frames the "
          "playback clock steps a fixed 1/60 s per frame (deterministic pose at "
          "every frame, so --screenshot captures are pixel-comparable).\n"
          "  --frame CODE  Alias for --time CODE.\n"
          "  --screenshot PATH  Save the viewport image after --frames.\n"
          "  --render-report PATH  Write a schema-versioned JSON render report.\n"
          "  --window-shot PATH  Save the complete window, including UI.\n"
          "  --raster-lod / --rt-lod  Enable view-dependent raster or Vulkan-RT "
          "LOD (--no-rt-lod disables RT LOD for deterministic full-scene capture); "
          "LOD; tune with --*-lod-full-px, --*-lod-cull-px, and --rt-lod-band.\n"
          "  --max-draw-meshes N / --max-gpu-mem G  Bound raster mesh count or "
          "geometry memory (GiB).\n"
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

  if (viewDirExplicit && !cameraName.empty()) {
    LOGE("--view-dir cannot be combined with --camera");
    return 1;
  }

  if (quitAfterFullUpload && maxFrames >= 0) {
    LOGE("--quit-after-full-upload cannot be combined with --frames");
    return 1;
  }

  LargeSceneProfile effectiveProfile = largeSceneProfile;
  if (effectiveProfile == LargeSceneProfile::Auto) {
    effectiveProfile = DetectProfileFromPath(file);
  }
  // Every large-scene budget -- --max-gpu-mem, the texture edge/byte caps, the
  // upload-staging cap, the proxy threshold -- descends from these two numbers.
  // They used to be the literals GiB(32) and GiB(16): the budget tree was
  // computed for an imaginary 32 GiB / 16 GiB machine no matter what you ran on,
  // so an 8 GiB card was handed a 8 GiB VRAM limit and a 24 GiB card left half
  // its memory unused. Probe the real values; --vram-budget overrides the GPU
  // side (useful to rehearse a smaller card, or when the probe is unavailable).
  const uint64_t hostCapacity = HostMemoryCapacityBytes();
  uint64_t vramCapacity = 0;
  if (vramBudgetExplicit && vramBudgetGiB > 0.0) {
    vramCapacity = static_cast<uint64_t>(
        vramBudgetGiB * double(tinyusdz::tydra::next::GiB(1)));
  } else {
#if defined(HAVE_VULKAN)
    vramCapacity = tusdview::QueryDeviceLocalVramBytes();
#endif
    // No Vulkan, or the probe failed: keep the historical 16 GiB assumption
    // rather than collapsing every budget to zero.
    if (vramCapacity == 0) vramCapacity = tinyusdz::tydra::next::GiB(16);
  }
  const tinyusdz::tydra::next::ResourceBudget targetBudget =
      tinyusdz::tydra::next::ComputeResourceBudget(hostCapacity, vramCapacity);
  const double targetVramGiB =
      double(targetBudget.vram_limit) / double(tinyusdz::tydra::next::GiB(1));
  const double targetHostGiB =
      double(targetBudget.host_limit) / double(tinyusdz::tydra::next::GiB(1));
  if (effectiveProfile != LargeSceneProfile::Off) {
    if (!useNextExplicit) useNextLoader = true;
    if (!backendExplicit) {
      backend = tusdview::Backend::Vulkan;
      backendExplicit = true;
    }
    if (!rasterLodExplicit) rasterLod = true;
    if (!rtLodExplicit) rtLod = true;
    if (!rtLodFullExplicit) rtLodFullPx = 64.0f;
    if (!rtLodCullExplicit) rtLodCullPx = 2.0f;
    if (!rtLodBandExplicit) rtLodBand = 0.25f;
    if (!maxAssetBytesExplicit) maxAssetReadBytes = 2ull * 1024ull * 1024ull * 1024ull;
    if (!domeIblExplicit) textureOptions.domeIbl = 0;
    // Bound texture residency the same way geometry already is. The --next
    // loader applies these while decoding, so peak RAM is bounded too.
    {
      const tinyusdz::tydra::next::TextureBudget textureBudget =
          tinyusdz::tydra::next::DeriveTextureBudget(targetBudget);
      if (!maxTextureSizeExplicit && textureBudget.max_edge > 0) {
        textureOptions.maxTextureSize = static_cast<int>(textureBudget.max_edge);
      }
      if (!textureBudgetExplicit && textureBudget.budget_bytes > 0) {
        textureOptions.textureBudgetMB =
            static_cast<int>(textureBudget.budget_bytes / (1024ull * 1024ull));
      }
    }

    if (effectiveProfile == LargeSceneProfile::Caldera) {
      if (!maxTrisExplicit) maxTris = 40000000;
      if (!maxGpuMemExplicit) maxGpuMemGiB = targetVramGiB;
      if (!maxDrawMeshesExplicit) maxDrawMeshes = 80000;
      if (!rasterLodFullExplicit) rasterLodFullPx = 48.0f;
      if (!rasterLodCullExplicit) rasterLodCullPx = 1.0f;
      if (cameraName.empty()) cameraName = "phospate_mine_overview";
    } else if (effectiveProfile == LargeSceneProfile::Island) {
      if (!maxGpuMemExplicit) maxGpuMemGiB = targetVramGiB;
      if (!maxDrawMeshesExplicit) maxDrawMeshes = 20000;
      if (!rasterLodFullExplicit) rasterLodFullPx = 48.0f;
      if (!rasterLodCullExplicit) rasterLodCullPx = 1.0f;
    } else if (effectiveProfile == LargeSceneProfile::ALab) {
      if (!maxGpuMemExplicit) maxGpuMemGiB = targetVramGiB;
      if (!maxDrawMeshesExplicit) maxDrawMeshes = 50000;
      if (!rasterLodFullExplicit) rasterLodFullPx = 36.0f;
      if (!rasterLodCullExplicit) rasterLodCullPx = 1.0f;
      if (!allowParentPathsExplicit) allowParentPaths = true;
    } else {
      if (!maxGpuMemExplicit) maxGpuMemGiB = targetVramGiB;
      if (!maxDrawMeshesExplicit) maxDrawMeshes = 40000;
      if (!rasterLodFullExplicit) rasterLodFullPx = 48.0f;
      if (!rasterLodCullExplicit) rasterLodCullPx = 1.5f;
    }
    // The district wrapper prepass composes a full proxy stage before the
    // viewer load and can consume the entire first-frame latency budget.
    // Keep it as an explicit refinement tool (`--lod-stream`); automatic
    // profiles start from deferred payload markers instead.
    if (lodStream) {
      if (!lodMaxMemExplicit) lodMaxMem = targetHostGiB;
      if (!lodMaxVramExplicit) lodMaxVram = targetVramGiB;
    }
  }
  if (maxAssetReadBytes > 0) {
    tinyusdz::security_policy::SetMaxAssetReadBytes(
        static_cast<size_t>(maxAssetReadBytes));
  }

  // Ray tracing is a Vulkan technique, so --rt implies the Vulkan backend.
  if (wantRt) {
    backend = tusdview::Backend::Vulkan;
    backendExplicit = true;
  }
  // Windowless rendering is a Vulkan-only path (GL needs a window/context).
  if (viewDirExplicit && !cameraName.empty()) {
    LOGE("--view-dir cannot be combined with the selected --camera/profile camera");
    return 1;
  }

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

  if (quitAfterFullUpload &&
      (backend != tusdview::Backend::GL || !useNextLoader || threaded || headless ||
       wantRt || wantCuda || wantHip)) {
    LOGE("--quit-after-full-upload requires interactive non-threaded OpenGL "
         "with --next");
    return 1;
  }

  if (effectiveProfile != LargeSceneProfile::Off) {
    LOGI("resource budget: vram capacity=%.1f GiB (%s) -> limit=%.1f GiB, "
         "host capacity=%.1f GiB -> limit=%.1f GiB",
         double(vramCapacity) / double(tinyusdz::tydra::next::GiB(1)),
         vramBudgetExplicit ? "--vram-budget" : "probed",
         targetVramGiB,
         double(hostCapacity) / double(tinyusdz::tydra::next::GiB(1)),
         targetHostGiB);
    LOGI("large-scene-profile %s resolved: backend=%s --next=%s "
         "--raster-lod=%s full=%.1f cull=%.1f --rt-lod=%s full=%.1f cull=%.1f "
         "--max-gpu-mem=%.1f --max-draw-meshes=%lld --max-tris=%lld",
         ProfileName(effectiveProfile),
         backend == tusdview::Backend::Vulkan ? "vk" : "gl",
         useNextLoader ? "on" : "off",
         rasterLod ? "on" : "off", rasterLodFullPx, rasterLodCullPx,
         rtLod ? "on" : "off", rtLodFullPx, rtLodCullPx,
         maxGpuMemGiB, maxDrawMeshes, maxTris);
    if (effectiveProfile == LargeSceneProfile::ALab && allowParentPaths) {
      LOGI("large-scene-profile alab: parent-relative composition paths allowed");
    }
    if (maxAssetReadBytes > 0) {
      LOGI("large-scene-profile %s: max asset read bytes=%llu",
           ProfileName(effectiveProfile),
           static_cast<unsigned long long>(maxAssetReadBytes));
    }
  }

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

  if (!cameraConformExplicit &&
      config.status == tusdview::ConfigLoadStatus::Loaded &&
      config.config.cameraConform) {
    const std::string& value = *config.config.cameraConform;
    if (value == "crop") cameraConform = tusdview::CameraConform::Crop;
    else if (value == "horizontal") cameraConform = tusdview::CameraConform::Horizontal;
    else if (value == "vertical") cameraConform = tusdview::CameraConform::Vertical;
    else if (value == "none") cameraConform = tusdview::CameraConform::None;
    else cameraConform = tusdview::CameraConform::Fit;
  }

  tusdview::App app(backend);
  if (config.status == tusdview::ConfigLoadStatus::Loaded) {
    if (config.config.fontSizePx) app.setFontSize(*config.config.fontSizePx);
    if (config.config.windowScale) app.setWindowScale(*config.config.windowScale);
    if (!windowSizeExplicit && config.config.windowWidth &&
        config.config.windowHeight) {
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
    if (config.config.invertYaw) {
      app.setInvertYaw(*config.config.invertYaw);
    }
    if (config.config.invertDolly) {
      app.setInvertDolly(*config.config.invertDolly);
    }
    app.setRecentScenes(config.config.recentScenes);
    if (config.config.vulkanDevice && !vkDeviceExplicit) {
      devicePreference.vulkanDevice = *config.config.vulkanDevice;
    }
    if (config.config.subdivisionLevel && !subdivisionLevel.has_value()) {
      subdivisionLevel = *config.config.subdivisionLevel;
    }
    if (config.config.subdivisionAuto && !subdivisionAutoExplicit) {
      subdivisionAuto = *config.config.subdivisionAuto;
    }
    if (config.config.subdivisionAutoMaxLevel && !subdivisionAutoMaxExplicit) {
      subdivisionAutoMaxLevel = *config.config.subdivisionAutoMaxLevel;
    }
    for (const auto& kv : config.config.subdivisionPrimLevels) {
      subdivisionPrimLevels.emplace(kv.first, kv.second);
    }
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
    lo.compositionThreads = compositionThreads;
    lo.conversionThreads = conversionThreads;
    lo.timing = timing;
    lo.streamBufferBytes = streamBufferMB * 1024ull * 1024ull;
    if (effectiveProfile != LargeSceneProfile::Off) {
      lo.maxMemoryBytes = static_cast<size_t>(targetBudget.host_limit);
      lo.gpuGeometryBudgetBytes = static_cast<size_t>(
          maxGpuMemExplicit && maxGpuMemGiB > 0.0
              ? maxGpuMemGiB * double(tinyusdz::tydra::next::GiB(1))
              : double(targetBudget.gpu_geometry_limit));
      lo.uploadStagingBytes =
          static_cast<size_t>(targetBudget.upload_staging_limit);
    }
    if (config.status == tusdview::ConfigLoadStatus::Loaded) {
      if (config.config.composition && !noComposition) {
        lo.composition = *config.config.composition;
      }
      if (config.config.payloadPolicy && !deferPayloads.has_value()) {
        deferPayloads = (*config.config.payloadPolicy == "defer");
      }
    }
    const bool defer = deferPayloads.value_or(
        effectiveProfile != LargeSceneProfile::Off ||
        (maxFrames < 0 && !headless));
    lo.payloadPolicy = defer ? tusdview::PayloadPolicy::DeferAll
                             : tusdview::PayloadPolicy::LoadAll;
    // Explicit opt-in only (no headless default flip): deferring references is
    // non-standard, so honor the flag verbatim even for --frames runs.
    lo.deferReferences = deferReferences;
    lo.allowParentRelativePaths = allowParentPaths;
    lo.textureOptions = textureOptions;
    lo.subdivisionLevel = std::max(0, subdivisionLevel.value_or(0));
    lo.subdivisionAuto = subdivisionAuto;
    lo.subdivisionAutoMaxLevel =
        std::max(0, std::min(10, subdivisionAutoMaxLevel));
    for (const auto& kv : subdivisionPrimLevels) {
      if (!kv.first.empty()) {
        lo.subdivisionPrimLevels[kv.first] = std::max(0, kv.second);
      }
    }
    if (timeCode.has_value()) lo.timecode = *timeCode;
    app.setLoadOptions(lo);
  }
  app.setUploadBudgetMs(uploadBudgetMs);
  app.setQuitAfterFullUpload(quitAfterFullUpload);
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
  if (windowSizeExplicit) app.setWindowSize(windowWidth, windowHeight);
  app.setUseNextLoader(useNextLoader);
  app.setCullEnabled(!noCull);
  app.setShowGrid(showGrid);
  app.setShowSkeleton(showSkeleton);
  app.setCamDolly(camDolly);
  app.setWindowShot(windowShot);
  app.setRequestRayTracing(wantRt);
  app.setDevicePreference(devicePreference);
  app.setAllowBackendFallback(!backendExplicit && backend == tusdview::Backend::Vulkan);
  app.setSkinningMode(skinningMode);
  app.setPlayAnimation(playAnim);
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
  app.setCudaCacheDir(cudaCacheDir);
  app.setHipRt(wantHip);
  app.setRtSamples(rtSamples);
  app.setRenderReport(renderReport);
  app.setRtMaxInstances(static_cast<size_t>(rtMaxInstances));
  app.setLodStream(lodStream);
  app.setLodMaxMemGiB(lodMaxMem);
  app.setLodMaxVramGiB(lodMaxVram);
  app.setCameraName(cameraName);
  app.setCameraConform(cameraConform);
  if (viewDirExplicit) app.setViewDirection(viewDir[0], viewDir[1], viewDir[2]);
  if (wantWireframe) app.setRenderMode(tusdview::RenderMode::Wireframe);
  if (wantMaterialId) app.setRenderMode(tusdview::RenderMode::MaterialId);
  if (wantMode) app.setRenderMode(*wantMode);
  for (const auto& bw : wantBlend) app.setBlendWeight(bw.first, bw.second);
  if (!wantSelect.empty()) app.setInitialSelection(wantSelect);
  return app.run(file, maxFrames, screenshot);
}
