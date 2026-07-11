// SPDX-License-Identifier: Apache-2.0
#include "app.hh"

#include <glad/glad.h>
//
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#ifndef _WIN32
#include <unistd.h>  // sysconf (RSS page size for the post-RT-build free log)
#endif

#include "cascadia_mono.h"  // CascadiaMono_compressed_data / _size
#include "config.hh"
#include "gpu_budget_lod.hh"
#include "lod_stream.hh"
#include "gui_style.hh"
#include "image-writer.hh"
#include "external/stb_image_resize2.h"  // stbir_resize (impl lives in the lib)
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "log.hh"
#include "mesh_build.hh"
#include "next_scene_loader.hh"
#include "next/tinyusdz-next.hh"  // tnext::Stage (per-frame --next morph weights)
#include "skinning.hh"

#if defined(HAVE_NFD)
#include "nfd.h"
#endif

namespace tusdview {

namespace {
void GlfwErrorCallback(int code, const char* desc) {
  LOGE("glfw error %d: %s", code, desc);
}

// --next blendshape capability: the next loader emits GPU morph channels rather
// than the RenderScene targets SceneHasBlendShapes() looks for.
bool DrawSceneHasMorphChannels(const DrawScene& draw) {
  for (const DrawMeshCPU& m : draw.meshes)
    if (m.morphChannelCount > 0) return true;
  return false;
}

constexpr int kBaseWindowWidth = 1280;
constexpr int kBaseWindowHeight = 800;
constexpr int kMaxGpuTextureInfluences = 256;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;

struct AutoSubdivisionView {
  float fovYDeg{60.0f};
  float aspect{1.3333f};
  float yaw{0.6f};
  float pitch{0.35f};
  float quality{1.0f};
  float camDolly{1.0f};
  int viewportHeight{kBaseWindowHeight};
};

void CopyPreviewLightDir(const DrawScene& draw, float out[3]) {
  if (draw.hasPreviewLight) {
    out[0] = draw.previewLightDir[0];
    out[1] = draw.previewLightDir[1];
    out[2] = draw.previewLightDir[2];
  } else {
    out[0] = 0.40160966f;
    out[1] = 0.64257544f;
    out[2] = 0.48193160f;
  }
}

// Write top-down RGBA8 rows as a binary PPM (RGB).
void WritePPM(const std::string& path, const std::vector<uint8_t>& rgba, int w, int h) {
  FILE* fp = std::fopen(path.c_str(), "wb");
  if (!fp) return;
  std::fprintf(fp, "P6\n%d %d\n255\n", w, h);
  for (int y = 0; y < h; ++y) {
    const uint8_t* row = &rgba[static_cast<size_t>(y) * static_cast<size_t>(w) * 4];
    for (int x = 0; x < w; ++x) {
      std::fputc(row[x * 4 + 0], fp);
      std::fputc(row[x * 4 + 1], fp);
      std::fputc(row[x * 4 + 2], fp);
    }
  }
  std::fclose(fp);
}

std::string LowerExtension(const std::string& path) {
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return std::string();
  std::string ext = path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return ext;
}

light3d::Vec3 DirFromAngles(float yaw, float pitch, int upAxis) {
  const float cp = std::cos(pitch);
  const float sp = std::sin(pitch);
  const float sy = std::sin(yaw);
  const float cy = std::cos(yaw);
  if (upAxis == 2) {
    return light3d::Vec3{sy * cp, cy * cp, sp};
  }
  return light3d::Vec3{sy * cp, sp, cy * cp};
}

bool ValidAabb(const float mn[3], const float mx[3]) {
  for (int a = 0; a < 3; ++a) {
    if (!std::isfinite(mn[a]) || !std::isfinite(mx[a]) || mx[a] < mn[a]) {
      return false;
    }
  }
  return true;
}

bool AutoSubdivisionFitBounds(const DrawScene& draw, float fitMin[3],
                              float fitMax[3]) {
  float ngMin[3] = {1e30f, 1e30f, 1e30f};
  float ngMax[3] = {-1e30f, -1e30f, -1e30f};
  bool ngValid = false;
  for (const DrawMeshCPU& m : draw.meshes) {
    if (m.purpose == "guide") continue;
    if (!ValidAabb(m.aabbMin, m.aabbMax)) continue;
    for (int a = 0; a < 3; ++a) {
      ngMin[a] = std::min(ngMin[a], m.aabbMin[a]);
      ngMax[a] = std::max(ngMax[a], m.aabbMax[a]);
    }
    ngValid = true;
  }
  if (ngValid) {
    for (int a = 0; a < 3; ++a) {
      fitMin[a] = ngMin[a];
      fitMax[a] = ngMax[a];
    }
    return true;
  }
  if (draw.hasBounds && ValidAabb(draw.aabbMin, draw.aabbMax)) {
    for (int a = 0; a < 3; ++a) {
      fitMin[a] = draw.aabbMin[a];
      fitMax[a] = draw.aabbMax[a];
    }
    return true;
  }
  return false;
}

int AutoSubdivisionLevel(float projectedRadiusPx, int maxLevel) {
  if (!(projectedRadiusPx > 64.0f)) return 0;
  int level = 1;
  if (projectedRadiusPx > 160.0f) level = 2;
  if (projectedRadiusPx > 360.0f) level = 3;
  if (projectedRadiusPx > 800.0f) level = 4;
  if (projectedRadiusPx > 1600.0f) level = 5;
  if (projectedRadiusPx > 3200.0f) level = 6;
  return std::min(level, std::max(0, maxLevel));
}

bool EstimateAutoSubdivisionLevels(
    const DrawScene& draw, const AutoSubdivisionView& view, int sceneLevel,
    int maxLevel, const std::map<std::string, int>& explicitPrimLevels,
    std::map<std::string, int>* autoPrimLevels) {
  if (!autoPrimLevels) return false;
  autoPrimLevels->clear();
  maxLevel = std::max(0, std::min(maxLevel, 10));
  if (maxLevel <= 0 || draw.meshes.empty()) return false;

  float fitMin[3], fitMax[3];
  if (!AutoSubdivisionFitBounds(draw, fitMin, fitMax)) return false;

  const float cx = 0.5f * (fitMin[0] + fitMax[0]);
  const float cy = 0.5f * (fitMin[1] + fitMax[1]);
  const float cz = 0.5f * (fitMin[2] + fitMax[2]);
  const float dx = fitMax[0] - fitMin[0];
  const float dy = fitMax[1] - fitMin[1];
  const float dz = fitMax[2] - fitMin[2];
  float sceneRadius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
  if (!(sceneRadius > 1e-4f)) sceneRadius = 1.0f;

  const float fovYDeg = std::max(5.0f, std::min(175.0f, view.fovYDeg));
  const float halfV = 0.5f * fovYDeg * kDeg2Rad;
  const float aspect = std::max(0.05f, view.aspect);
  const float halfH = std::atan(std::tan(halfV) * aspect);
  const float halfMin = std::max(1e-3f, std::min(halfV, halfH));
  float fitDistance = (sceneRadius / std::sin(halfMin)) * 1.1f;
  if (view.camDolly > 0.0f) fitDistance *= view.camDolly;

  const int upAxis = (draw.upAxis == "Z") ? 2 : 1;
  const light3d::Vec3 eye =
      light3d::Vec3{cx, cy, cz} + DirFromAngles(view.yaw, view.pitch, upAxis) *
                                      std::max(fitDistance, 1e-3f);
  const float viewportH =
      static_cast<float>(std::max(1, view.viewportHeight));
  const float pixelScale = viewportH / (2.0f * std::tan(halfV));
  const float quality = std::max(0.25f, view.quality);

  for (const DrawMeshCPU& m : draw.meshes) {
    if (m.absPath.empty() || m.purpose == "guide") continue;
    if (explicitPrimLevels.find(m.absPath) != explicitPrimLevels.end()) continue;
    if (!ValidAabb(m.aabbMin, m.aabbMax)) continue;

    const float mx = 0.5f * (m.aabbMin[0] + m.aabbMax[0]);
    const float my = 0.5f * (m.aabbMin[1] + m.aabbMax[1]);
    const float mz = 0.5f * (m.aabbMin[2] + m.aabbMax[2]);
    const float mdx = m.aabbMax[0] - m.aabbMin[0];
    const float mdy = m.aabbMax[1] - m.aabbMin[1];
    const float mdz = m.aabbMax[2] - m.aabbMin[2];
    const float radius = 0.5f * std::sqrt(mdx * mdx + mdy * mdy + mdz * mdz);
    if (!(radius > 1e-6f)) continue;
    const light3d::Vec3 center{mx, my, mz};
    const float centerDist = light3d::length(center - eye);
    const float dist = std::max(1e-3f, centerDist - radius);
    const float projectedRadiusPx = (radius * pixelScale / dist) * quality;
    const int level = AutoSubdivisionLevel(projectedRadiusPx, maxLevel);
    if (level > std::max(0, sceneLevel)) {
      (*autoPrimLevels)[m.absPath] = level;
    }
  }
  return !autoPrimLevels->empty();
}

bool LoadUsdMaybeAutoSubdivision(
    const std::string& path, LoadOptions opts, LoadedScene* out, DrawScene* draw,
    bool rtPath, LoadControl* ctrl, const AutoSubdivisionView& view) {
  bool ok = LoadUSD(path, opts, out, draw, rtPath, ctrl);
  if (!ok || !draw || !opts.subdivisionAuto || (ctrl && ctrl->cancel.load())) {
    return ok;
  }

  std::map<std::string, int> autoPrimLevels;
  if (!EstimateAutoSubdivisionLevels(*draw, view, opts.subdivisionLevel,
                                     opts.subdivisionAutoMaxLevel,
                                     opts.subdivisionPrimLevels,
                                     &autoPrimLevels)) {
    return ok;
  }

  LoadOptions refinedOpts = opts;
  refinedOpts.subdivisionAuto = false;
  for (const auto& kv : autoPrimLevels) {
    refinedOpts.subdivisionPrimLevels.emplace(kv.first, kv.second);
  }

  int maxApplied = 0;
  for (const auto& kv : refinedOpts.subdivisionPrimLevels) {
    maxApplied = std::max(maxApplied, kv.second);
  }
  LOGI("auto subdivision: %zu prim override(s), max level %d",
       autoPrimLevels.size(), maxApplied);

  LoadedScene refined;
  DrawScene refinedDraw;
  if (LoadUSD(path, refinedOpts, &refined, &refinedDraw, rtPath, ctrl)) {
    *out = std::move(refined);
    *draw = std::move(refinedDraw);
    return true;
  }

  if (!refined.err.empty()) {
    out->warn += "Auto subdivision retry failed; using base mesh: " +
                 refined.err + "\n";
  }
  return ok;
}
}  // anonymous namespace

// In namespace tusdview (declared in app.hh) so the MCP screenshot tool can reuse it.
bool WriteScreenshotImage(const std::string& path,
                          const std::vector<uint8_t>& rgba, int w, int h,
                          std::string* err) {
  if (w <= 0 || h <= 0 || rgba.size() < static_cast<size_t>(w) *
                                      static_cast<size_t>(h) * 4) {
    if (err) *err = "invalid screenshot buffer";
    return false;
  }

  // .ppm: keep the dependency-free fast path. Everything else goes through the
  // shared encoder, which autodetects png/jpg/jpeg/qoi/bmp/exr from the extension
  // (fpnge/fpng PNG, libjpeg-turbo JPEG, QOI -- whatever the build enabled).
  if (LowerExtension(path) == "ppm") {
    WritePPM(path, rgba, w, h);
    return true;
  }

  tinyusdz::Image img;
  img.uri = path;
  img.width = w;
  img.height = h;
  img.channels = 4;
  img.bpp = 8;
  img.format = tinyusdz::Image::PixelFormat::UInt;
  img.data = rgba;

  tinyusdz::image::WriteOption opt;  // Autodetect by extension
  auto ret = tinyusdz::image::WriteImageToFile(path, img, opt);
  if (!ret) {
    if (err) *err = ret.error();
    return false;
  }
  return true;
}

void App::getRequestedWindowSize(int* width, int* height) const {
  if (hasWindowSizeOverride_) {
    *width = windowWidth_;
    *height = windowHeight_;
    return;
  }

  *width = static_cast<int>(static_cast<float>(kBaseWindowWidth) * windowScale_);
  *height = static_cast<int>(static_cast<float>(kBaseWindowHeight) * windowScale_);
}

App::~App() {
#if defined(TUSDVIEW_HAVE_MCP)
  // Stop the MCP transports first so no worker thread calls back into App while
  // its members are being destroyed.
  if (mcp_) mcp_->stop();
#endif
  cancelAndJoinLoad();  // must run before members the worker writes into are destroyed
  if (hipBuildThread_.joinable()) hipBuildThread_.join();  // finish any in-flight RT build
#if defined(TUSDVIEW_ENABLE_GL_THREAD)
  if (renderThreadActive_) {
    joinRenderThread();  // the render thread runs renderer_->shutdown() on its context
  } else
#endif
      if (renderer_) {
    renderer_->shutdown();
  }
  if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
  if (window_) glfwDestroyWindow(window_);
  glfwTerminate();
}

void App::autoDetectUiScale() {
  if (uiScaleExplicit_ || headless_) return;
  GLFWmonitor* mon = glfwGetPrimaryMonitor();
  if (!mon) return;
  // OS-reported HiDPI content scale (e.g. 2.0 at 200% desktop scaling).
  float xs = 1.0f, ys = 1.0f;
  glfwGetMonitorContentScale(mon, &xs, &ys);
  const GLFWvidmode* mode = glfwGetVideoMode(mon);
  const int mw = mode ? mode->width : 0;
  // Use 2x only on genuinely high-density / large panels: a HiDPI content scale,
  // or a native width of at least 2K (2560). Standard 1080p/1920 stays at 1x, so
  // the font/widgets are not oversized on a normal-DPI display.
  const float scale = (xs >= 1.5f || mw >= 2560) ? 2.0f : 1.0f;
  uiScale_ = scale;
  windowScale_ = scale;
  fontSizePx_ = 16.0f * scale;
  LOGI("ui scale: %.0fx (monitor %dpx wide, content scale %.2f)", scale, mw, xs);
}

bool App::initWindow(std::string* err) {
  glfwSetErrorCallback(GlfwErrorCallback);
  if (!glfwInit()) {
    *err = "glfwInit failed";
    return false;
  }
  autoDetectUiScale();  // before getRequestedWindowSize (windowScale_ feeds it)

  if (backend_ == Backend::GL) {
    // Request 4.1 core (the highest macOS supports) so the GPU-tessellation
    // displacement path is available; fall back to 3.3 below if creation fails.
    // 4.1 core is a strict superset of 3.3 core, so all existing shaders run as-is.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
  } else {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  }

  // Scale the default window independently from the font/widget sizing, then
  // clamp to the current monitor work area.
  int winW = 0;
  int winH = 0;
  getRequestedWindowSize(&winW, &winH);
  if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) {
    int mx = 0, my = 0, mw = 0, mh = 0;
    glfwGetMonitorWorkarea(mon, &mx, &my, &mw, &mh);
    if (mw > 0 && winW > mw) winW = mw;
    if (mh > 0 && winH > mh) winH = mh;
  }
  window_ = glfwCreateWindow(winW, winH, "tusdview", nullptr, nullptr);
  if (!window_ && backend_ == Backend::GL) {
    // 4.1 unavailable (e.g. 3.3-only hardware): retry at 3.3 core. Tessellation
    // displacement is then unavailable; coarse per-vertex displacement still works.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    window_ = glfwCreateWindow(winW, winH, "tusdview", nullptr, nullptr);
  }
  if (!window_) {
    *err = "glfwCreateWindow failed";
    return false;
  }

  if (backend_ == Backend::GL) {
    glfwMakeContextCurrent(window_);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
      *err = "Failed to load OpenGL via glad";
      return false;
    }
    if (!GLAD_GL_VERSION_3_3) {
      *err = "OpenGL 3.3 not available";
      return false;
    }
    glfwSwapInterval(1);
  }
  return true;
}

bool App::initImGui(std::string* err) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  imguiIniPath_.clear();
  std::optional<std::filesystem::path> iniPath;
  if (!configPath_.empty()) {
    iniPath = configPath_.parent_path() / "imgui.ini";
  } else {
    iniPath = DefaultImGuiIniPath();
  }
  if (iniPath) {
    std::error_code ec;
    const std::filesystem::path dir = iniPath->parent_path();
    if (!dir.empty()) {
      std::filesystem::create_directories(dir, ec);
    }
    if (ec) {
      LOGW("could not create ImGui state directory %s: %s", dir.string().c_str(),
           ec.message().c_str());
      io.IniFilename = nullptr;
    } else {
      imguiIniPath_ = iniPath->string();
      io.IniFilename = imguiIniPath_.c_str();
    }
  } else {
    io.IniFilename = nullptr;
  }

  // HiDPI: load Cascadia Mono at a scaled pixel size and scale widget metrics
  // so the UI is readable on 4K panels. Baking the scale into the font size
  // keeps the text crisp (vs. io.FontGlobalScale which just magnifies).
  io.Fonts->AddFontFromMemoryCompressedTTF(CascadiaMono_compressed_data,
                                           CascadiaMono_compressed_size, fontSizePx_);

  // Maya-like dark theme, then scale rounding/padding/spacing for HiDPI.
  StyleMaya();
  ImGui::GetStyle().ScaleAllSizes(uiScale_);

#if defined(TUSDVIEW_ENABLE_GL_THREAD)
  // Threaded: only the platform half (GLFW callbacks/input) on the main thread; the
  // GL backend half runs on the render thread in renderThreadMain().
  if (renderThreadActive_) return renderer_->initImGuiPlatform(window_, err);
#endif
  return renderer_->initImGui(err);
}

// Release a mesh's CPU geometry after it's been uploaded to the GPU. Used only
// for the static --next preview, where the CPU copy is dead weight (no
// animation re-pose, GPU skinning, or meaningful per-mesh pick on batched
// geometry). Halves resident RAM for large scenes. Keeps small metadata
// (name/purpose/world/aabb/submeshes) the GUI's visibility mask still reads.
// A mesh the RT path has to be able to re-pose: its rest vertices and skin/morph
// attributes are the ONLY inputs to BuildNextRtDeformedVertices, so freeing them
// would leave the ray tracer stuck on whatever pose the BLAS was built with.
// (The raster path can free them freely -- it deforms in the vertex shader from
// the GPU-side copies.)
static bool MeshIsDeformable(const DrawMeshCPU& m) {
  return !m.jointIdx.empty() || !m.morphDeltaHalf.empty();
}

static void FreeMeshGeometryCPU(DrawMeshCPU& m) {
  std::vector<DrawVertex>().swap(m.vertices);
  std::vector<uint32_t>().swap(m.indices);
  std::vector<float>().swap(m.vertexColors);
  // Upload-only auxiliary arrays (consumed in appendMesh / the RT build, never
  // re-read after): free them too. GPU morph/skin re-pose uploads only the
  // per-frame coefficients, not these source arrays, so dropping them is safe.
  std::vector<uint32_t>().swap(m.wireframeIndices);
  std::vector<uint32_t>().swap(m.sourceFaceId);
  std::vector<uint32_t>().swap(m.morphOffsetCount);
  std::vector<uint16_t>().swap(m.morphDeltaHalf);
  std::vector<uint16_t>().swap(m.morphChannelId);
  std::vector<uint32_t>().swap(m.jointIdx);
  std::vector<float>().swap(m.jointWt);
  std::vector<uint32_t>().swap(m.influenceOffsetCount);
  std::vector<float>().swap(m.influenceTexels);
  // instanceXforms / instanceColors / instanceOpacities are RETAINED:
  // per-instance frustum culling
  // (gui) re-tests each instance's protoAabb against the frustum and re-uploads
  // the visible subset every frame, so it needs the CPU transforms. This is the
  // CPU-culling memory cost (one CPU + one GPU copy); GPU compute culling would
  // drop the CPU copy -- a documented follow-up.
}

// Aggressive free for the HIP/CUDA RT path: after the build everything lives in
// the GPU BVH and trace() never reads draw_ geometry again, so drop ALL per-mesh
// CPU arrays (including instance transforms, which the RT path -- unlike raster
// culling -- does not need). Keeps only metadata (name/purpose/world/aabb).
static void FreeMeshGeometryCPUForRT(DrawMeshCPU& m) {
  FreeMeshGeometryCPU(m);
  std::vector<float>().swap(m.uv1);
  std::vector<float>().swap(m.morphInfluence);
  std::vector<MorphTargetCPU>().swap(m.morphs);
  std::vector<MorphTargetChannelsCPU>().swap(m.morphTargetChannels);
  std::vector<float>().swap(m.skinnedHelperPoints);
  std::vector<float>().swap(m.instanceXforms);
  std::vector<float>().swap(m.instanceColors);
  std::vector<float>().swap(m.instanceOpacities);
}

// Frames the camera must hold still before the RT LOD set is re-selected.
static constexpr int kLodSettleFrames = 4;

void App::updateRtLodCamera() {
  if (!renderer_ || !rtLodEnabled_ || !renderer_->rayTracingActive()) return;

  RtLodCamera cam;
  cam.lodEnabled = true;
  cam.proxyEnabled = true;  // distant prototypes render as shared box proxies
  cam.frustumCull = true;
  cam.fullPx = rtLodFullPx_;
  cam.cullPx = rtLodCullPx_;
  cam.bandFrac = rtLodBandFrac_;
  // GL-convention proj*view so light3d::Frustum extracts correct planes (incl. the
  // near plane, which culls behind-camera instances).
  const light3d::Mat4 vp = camera_.proj(/*zeroToOneDepth=*/false) * camera_.view();
  std::memcpy(cam.viewProj.m, vp.m, sizeof(cam.viewProj.m));
  const light3d::Vec3 eye = camera_.eye();
  const light3d::Vec3 fwd = light3d::normalize(camera_.target() - eye);
  cam.eye = eye;
  cam.forward = fwd;
  cam.nearPlane = camera_.nearPlane();

  // Hysteresis + debounce: track the camera each frame; reselect only once it has
  // held still for a few frames (so micro-jitter never rebuilds the TLAS). The
  // stale LOD set keeps rendering while moving (1 spp motion hides the lag).
  const float dist = std::max(1e-3f, camera_.distance());
  const float eyeMove = light3d::length(eye - lastLodEye_);
  const float align = light3d::dot(fwd, lastLodFwd_);
  const bool moved = !lodHaveLast_ || eyeMove > 0.02f * dist || align < 0.9997f;
  if (moved) {
    lodStillFrames_ = 0;
    lastLodEye_ = eye;
    lastLodFwd_ = fwd;
    lodHaveLast_ = true;
    lodPendingReselect_ = true;
  } else if (lodStillFrames_ < kLodSettleFrames) {
    ++lodStillFrames_;
  }
  // The pixel-size threshold needs a laid-out viewport: on the first windowed
  // frame the dock split is not computed yet and resizeViewport() reports a
  // transient tiny height (e.g. 20px), which would make focalPx so small that
  // every instance reads as sub-pixel and gets culled (a one-frame all-Proxy/Cull
  // blip). Require a sane viewport height before the first arm; the headless path
  // reports its full render height from frame 0, so it still arms immediately.
  int vpw = 0, vph = 0;
  renderer_->viewportSize(&vpw, &vph);
  const bool vpReady = vph >= 64;

  bool reselect = false;
  if (!lodArmedOnce_ && vpReady) {  // first laid-out frame: build the LOD TLAS now
    reselect = true;
    lodArmedOnce_ = true;
  } else if (lodPendingReselect_ && lodStillFrames_ >= kLodSettleFrames) {
    reselect = true;
    lodPendingReselect_ = false;
  }
  if (!reselect) return;  // renderer keeps the last snapshot until the next settle
  postGpu([this, cam]() { renderer_->setLodCamera(cam, /*reselect=*/true); });
}

void App::applyLoaded(bool ok, bool progressive) {
  // Threaded GL: the per-mesh progressive upload would free CPU geometry on the
  // main thread before the render thread drains the queued appendMesh ops (a
  // use-after-free). Use the one-shot uploadScene path instead (load stays async);
  // progressive-threaded streaming is a follow-up.
  if (renderThreadActive_) progressive = false;
  progressiveActive_ = false;
  nextMesh_ = 0;
  nextTex_ = 0;
  nextVolume_ = 0;

  if (ok) {
    // Capture the vertex total now, before the --next path frees per-mesh CPU
    // geometry on upload (otherwise the Stats panel would show 0 vertices).
    size_t vtot = 0;
    for (const DrawMeshCPU& m : draw_.meshes) vtot += m.vertices.size();
    draw_.vertexCount = vtot;
    // Record in the recent-scenes list (interactive only -- headless screenshot
    // runs must not mutate the user's config).
    if (!headless_ && !loaded_.filepath.empty()) addRecentScene(loaded_.filepath);
    ++sceneGen_;  // invalidate the MCP library-tool Stage snapshot
    readAnimationRange();  // start/end/fps; resets playback to paused at start
    if (std::isfinite(loadOpts_.timecode)) {
      animTime_ = loadOpts_.timecode;
      reconvApplied_ = animTime_;
    }
    updateSkinningEffective();
    // The tracer re-pose cache indexes draw_.meshes; this is a different scene.
    nextRestVerts_.clear();
    nextTracerPosedTime_ = std::numeric_limits<double>::quiet_NaN();
    // Robust auto-frame bounds: compute from the full per-mesh set NOW, before
    // the LOD merge collapses 80k meshes into one 42M-instance proxy (whose mass
    // would otherwise swamp the weighting). Cached for the framing step below.
    robustBoundsValid_ = false;
    if (robustFrame_ && useNextLoader_) {
      std::string rrep;
      if (ComputeRobustSceneBounds(&draw_, 0.01f, robustBoundsMin_,
                                   robustBoundsMax_, &rrep)) {
        robustBoundsValid_ = true;
        if (!rrep.empty()) LOGI("%s", rrep.c_str());
      }
    }
    // GPU-budget LOD: bound the full-mesh draw count / VRAM for huge assembled
    // scenes (e.g. Moana island ~84k meshes) by merging the long tail of small
    // meshes into one instanced bbox-proxy soup, so the per-mesh-buffer raster
    // upload doesn't create tens of thousands of buffers and stall for minutes.
    if (useNextLoader_ && (maxFullMeshes_ > 0 || gpuMemBudgetBytes_ > 0)) {
      std::string rep;
      ApplyGpuBudgetLOD(&draw_, gpuMemBudgetBytes_, maxFullMeshes_, &rep);
      if (!rep.empty()) LOGI("%s", rep.c_str());
    }
    // --next GPU morph: detect instanced prototypes carrying morph channels so
    // the per-frame coefficient upload runs (independent of Tydra GPU skinning).
    hasNextMorph_ = false;
    if (useNextLoader_) {
      for (const DrawMeshCPU& m : draw_.meshes)
        if (m.morphChannelCount > 0) { hasNextMorph_ = true; break; }
    }
    if (skinningEffective_ == SkinningMode::GPU) {
      LOGI("skinning: GPU (%s)", skinningReason_.c_str());
    } else if (skinningRequested_ == SkinningMode::GPU) {
      LOGW("skinning: requested GPU, using CPU (%s)", skinningReason_.c_str());
    } else {
      LOGI("skinning: CPU (%s)", skinningReason_.c_str());
    }
    const std::string& up = loaded_.render.meta.upAxis;
    camera_.setUpAxis((up == "Z" || up == "z") ? 2 : 1);
  }

  if (ok && hipInteractive_) {
    // Windowed --hip: no raster upload (the HIP tracer renders the viewport from
    // draw_ each frame); keep CPU geometry for the tracer build.
    LOGI("loaded %s: %zu mesh(es), %zu tri(s)%s; HIP interactive (no raster upload)",
         loaded_.filepath.c_str(), draw_.meshes.size(), draw_.triangleCount,
         draw_.truncated ? " [truncated]" : "");
  } else if (ok && progressive) {
    // Reserve materials + texture slots now; stream meshes then textures over
    // the next frames (stepProgressiveUpload) so geometry pops in and the UI
    // stays at frame rate instead of stalling on one big upload.
    renderer_->beginScene(draw_.materials, static_cast<int>(draw_.textures.size()));
    renderer_->setLights(draw_.lights);
    progressiveActive_ = true;
    LOGI("loaded %s: %zu mesh(es), %zu tri(s)%s; streaming to GPU...",
         loaded_.filepath.c_str(), draw_.meshes.size(), draw_.triangleCount,
         draw_.truncated ? " [truncated]" : "");
  } else {
    // One-shot upload (headless / threaded / failure). draw_ is empty when !ok.
    // Threaded: post upload + the CPU-geometry free together so they run, in order,
    // on the render thread (the free must not precede the upload it feeds).
    const bool freeCpu = ok && useNextLoader_ && !cudaRt_ && !hipRt_;
    // When the RT path owns the screenshot the rasterized scene is never drawn,
    // so skip the (potentially huge) raster upload entirely.
    if (!rtOwnsScreenshot_) {
      postGpu([this, freeCpu] {
        std::string uerr;
        renderer_->uploadScene(draw_, &uerr);
        // Deformable meshes keep their CPU geometry: RT re-poses from it every
        // frame (and RT can be toggled on at any time).
        if (freeCpu)
          for (DrawMeshCPU& m : draw_.meshes)
            if (!MeshIsDeformable(m)) FreeMeshGeometryCPU(m);
      });
    }
    if (ok) {
      LOGI("loaded %s: %zu mesh(es), %zu tri(s)%s", loaded_.filepath.c_str(),
           draw_.meshes.size(), draw_.triangleCount,
           draw_.truncated ? " [truncated: render budget]" : "");
    } else {
      LOGE("load failed: %s", loaded_.err.c_str());
    }
  }
  // Structured end-of-load diagnostic summary (renderer-parity work). The
  // converter's warnings and the draw-side skipped list are otherwise only
  // visible in the ImGui panel; surface a greppable, machine-parseable line so
  // headless runs and the usd-assets smoke harness can distinguish a full
  // material fallback (degraded) from a benign missing normal-map texture.
  if (ok) {
    const LoadDiagnostics diag =
        CategorizeLoadWarnings(loaded_.warn, draw_.skipped);
    if (diag.actionable() > 0) {
      LOGW(
          "load summary: degraded_materials=%d missing_textures=%d "
          "unsupported_mtlx=%d skipped=%d other=%d",
          diag.degraded_material, diag.missing_texture, diag.unsupported_mtlx,
          diag.skipped, diag.other);
      for (const std::string& ex : diag.examples) {
        LOGW("  - %s", ex.c_str());
      }
    }
  }
  // Pose the GPU frame now that the renderer holds the meshes (the per-mesh
  // morph vertex upload needs them present; the bone texture is global). For the
  // progressive path meshes stream in over later frames — the main loop re-poses
  // once streaming completes (skinFrameTime_ stays NaN until then).
  if (ok && skinningEffective_ == SkinningMode::GPU && !progressiveActive_) {
    updateGpuSkinningFrameIfNeeded();
  }
  // --next deformation: upload the initial bone matrices / blendshape
  // coefficients (the render loop re-poses each frame as animTime_ advances).
  // Skipped while streaming; the loop catches up once meshes land.
  if (ok && useNextLoader_ && (hasNextMorph_ || draw_.boneMatrixCount > 0) &&
      !progressiveActive_) {
    updateNextDeformFrameIfNeeded();
  }
  // Frame the camera AFTER the GPU pose updates draw_ bounds (so an animated
  // load, e.g. --time, frames the posed geometry, matching the CPU bake path).
  if (ok && draw_.hasBounds) {
    const float dx = draw_.aabbMax[0] - draw_.aabbMin[0];
    const float dy = draw_.aabbMax[1] - draw_.aabbMin[1];
    const float dz = draw_.aabbMax[2] - draw_.aabbMin[2];
    camera_.setSceneRadius(0.5f * std::sqrt(dx * dx + dy * dy + dz * dz));
    const int upAxis = (draw_.upAxis == "Z") ? 2 : 1;
    camera_.setUpAxis(upAxis);
    NextCameraPose campose;
    if (!cameraName_.empty() && useNextLoader_ && nextSession_ &&
        FindNextCamera(nextSession_->GetStage(), cameraName_, animTime_,
                       &campose)) {
      // Drive the orbit rig from a scene camera. The auto-fit framing is useless
      // on vast scenes (Caldera's 8 km map frames to a sub-pixel speck); a named
      // USD camera gives a meaningful district view across raster / --rt / --cuda.
      const float* E = campose.eye;
      const float* F = campose.forward;
      const float cx = 0.5f * (draw_.aabbMin[0] + draw_.aabbMax[0]);
      const float cy = 0.5f * (draw_.aabbMin[1] + draw_.aabbMax[1]);
      const float cz = 0.5f * (draw_.aabbMin[2] + draw_.aabbMax[2]);
      float d = std::sqrt((cx - E[0]) * (cx - E[0]) + (cy - E[1]) * (cy - E[1]) +
                          (cz - E[2]) * (cz - E[2]));
      if (!(d > 1e-3f)) d = std::max(1.0f, camera_.distance());
      // eye = target + dirToEye*distance with dirToEye = -forward; invert the
      // OrbitCamera DirFromAngles convention (camera_nav.cc) to recover yaw/pitch.
      const float dir[3] = {-F[0], -F[1], -F[2]};
      float yaw, pitch;
      if (upAxis == 2) {  // +Z up: dirToEye = (sy*cp, cy*cp, sp)
        pitch = std::asin(std::max(-1.0f, std::min(1.0f, dir[2])));
        yaw = std::atan2(dir[0], dir[1]);
      } else {  // +Y up: dirToEye = (sy*cp, sp, cy*cp)
        pitch = std::asin(std::max(-1.0f, std::min(1.0f, dir[1])));
        yaw = std::atan2(dir[0], dir[2]);
      }
      const light3d::Vec3 target{E[0] + F[0] * d, E[1] + F[1] * d,
                                 E[2] + F[2] * d};
      camera_.setFovYDeg(campose.fovYDeg);
      // Use the camera's authored clip range, not the auto-clip derived from the
      // (huge) whole-scene radius -- on Caldera the far-flung guide bounds push
      // the auto near plane out past the nearby district, clipping it away.
      camera_.setAutoClip(false);
      camera_.setClipPlanes(campose.zNear, campose.zFar);
      camera_.setOrbit(target, yaw, pitch, d);
      LOGI("camera: framing USD camera '%s' (fovY %.1f deg, clip %.2f..%.0f)",
           cameraName_.c_str(), campose.fovYDeg, campose.zNear, campose.zFar);
    } else {
      if (!cameraName_.empty()) {
        LOGW("camera '%s' not found (need --next + a Camera prim); auto-fitting",
             cameraName_.c_str());
      }
      // Frame on the visible geometry. Two inflators are excluded so pan/dolly
      // sensitivity (scaled by the framing distance) and the initial fit stay
      // sane: (1) guide breadcrumbs/endpoints, which span the whole map and are
      // hidden, and (2) sparse far-flung outliers (robust mass-trim). The aabb
      // metadata survives the --next CPU-geometry free, so this works post-load.
      float ngMin[3] = {1e30f, 1e30f, 1e30f}, ngMax[3] = {-1e30f, -1e30f, -1e30f};
      bool ngValid = false;
      for (const DrawMeshCPU& m : draw_.meshes) {
        if (m.purpose == "guide") continue;
        bool good = true;
        for (int a = 0; a < 3; ++a) {
          if (!(m.aabbMax[a] >= m.aabbMin[a]) || !std::isfinite(m.aabbMin[a]) ||
              !std::isfinite(m.aabbMax[a])) { good = false; break; }
        }
        if (!good) continue;
        // Instanced prototypes carry world-space bounds in aabbMin/aabbMax (set
        // by BuildDrawInstances / the next loader), so no per-instance expansion
        // is needed here.
        for (int a = 0; a < 3; ++a) {
          ngMin[a] = std::min(ngMin[a], m.aabbMin[a]);
          ngMax[a] = std::max(ngMax[a], m.aabbMax[a]);
        }
        ngValid = true;
      }
      const float* fitMin = ngValid ? ngMin : draw_.aabbMin;
      const float* fitMax = ngValid ? ngMax : draw_.aabbMax;
      if (robustBoundsValid_) {
        fitMin = robustBoundsMin_;
        fitMax = robustBoundsMax_;
      }
      {
        const float dx = fitMax[0] - fitMin[0], dy = fitMax[1] - fitMin[1],
                    dz = fitMax[2] - fitMin[2];
        camera_.setSceneRadius(0.5f * std::sqrt(dx * dx + dy * dy + dz * dz));
      }
      camera_.fitToScene(fitMin, fitMax);
      // --cam-dolly: scale the fitted distance (<1 zooms in past the framing so
      // peripheral geometry leaves the frustum -- exercises culling headlessly).
      if (camDolly_ > 0.0f && camDolly_ != 1.0f) {
        camera_.setOrbit(camera_.target(), camera_.yaw(), camera_.pitch(),
                         camera_.distance() * camDolly_);
      }
    }
  }
  gui_.setScene(&loaded_, &draw_);
  gui_.setNextStage(nextSession_ ? &nextSession_->GetStage() : nullptr);
  // Apply a one-shot --select (prim path) once the scene + draw meshes exist.
  if (!initialSelect_.empty()) {
    gui_.selectByPath(initialSelect_, -1);
    initialSelect_.clear();
  }
}

void App::stepProgressiveUpload() {
  if (!progressiveActive_) return;
  const auto t0 = std::chrono::steady_clock::now();
  auto elapsedMs = [&]() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                     t0)
        .count();
  };
  // Geometry first so meshes appear, ~4ms/frame.
  while (nextMesh_ < draw_.meshes.size()) {
    renderer_->appendMesh(draw_.meshes[nextMesh_]);
    if (useNextLoader_ && !cudaRt_ && !hipRt_ &&
        !MeshIsDeformable(draw_.meshes[nextMesh_]))
      FreeMeshGeometryCPU(draw_.meshes[nextMesh_]);
    ++nextMesh_;
    if (elapsedMs() > 4.0) break;
  }
  // Then stream textures (meshes show base color until their texture lands).
  if (nextMesh_ >= draw_.meshes.size()) {
    while (nextTex_ < draw_.textures.size()) {
      renderer_->uploadTexture(static_cast<int>(nextTex_), draw_.textures[nextTex_]);
      ++nextTex_;
      if (elapsedMs() > 7.0) break;
    }
  }
  // UsdVol volumes (OpenVDB) after meshes + textures.
  if (nextMesh_ >= draw_.meshes.size() && nextTex_ >= draw_.textures.size()) {
    while (nextVolume_ < draw_.volumes.size()) {
      renderer_->appendVolume(draw_.volumes[nextVolume_]);
      ++nextVolume_;
      if (elapsedMs() > 7.0) break;
    }
  }
  if (nextMesh_ >= draw_.meshes.size() && nextTex_ >= draw_.textures.size() &&
      nextVolume_ >= draw_.volumes.size()) {
    progressiveActive_ = false;
  }
}

void App::loadFileBlocking(const std::string& path) {
  loadCtrl_.resetProgress();
  LoadedScene tmp;
  DrawScene drawTmp;
  LoadOptions opts = loadOpts_;
  opts.gpuSkinning = wantsNextGpuSkinning();
  // View-dependent district LOD (--lod-stream): a proxy pre-pass promotes the
  // camera-nearest districts to full and writes a wrapper layer we load instead.
  // Only meaningful on the --next path (the only one that composes huge scenes
  // like Caldera). The original `path` stays as the displayed filename.
  std::string effPath = path;
  if (lodStream_ && useNextLoader_) {
    LodStreamOptions lo;
    lo.camera = cameraName_;
    lo.maxMemGiB = lodMaxMemGiB_;
    lo.maxVramGiB = lodMaxVramGiB_;
    lo.time = animTime_;
    std::string wrapper = PrepareLodStream(path, lo);
    if (!wrapper.empty()) effPath = wrapper;
  }
  if (useNextLoader_) {
    std::shared_ptr<tinyusdz::next::StageSession> session;
    const bool ok = LoadUSDViaNext(effPath, opts, &drawTmp, &tmp.warn, &tmp.err,
                                   &loadCtrl_, &session);
    tmp.ok = ok;
    tmp.filepath = path;
    tmp.render.meta.upAxis = drawTmp.upAxis;  // drive camera/grid up-axis
    if (session) {
      const tinyusdz::next::Stage& stage = session->GetStage();
      const double s = stage.GetStartTimeCode();
      const double e = stage.GetEndTimeCode();
      const double fps = stage.GetTimeCodesPerSecond();
      if (fps > 0.0) tmp.render.meta.timeCodesPerSecond = fps;
      if (e > s) {
        tmp.render.meta.startTimeCode = s;
        tmp.render.meta.endTimeCode = e;
      }
    }
    loaded_ = std::move(tmp);
    draw_ = ok ? std::move(drawTmp) : DrawScene{};
    nextSession_ = ok ? std::move(session) : nullptr;
    applyLoaded(ok, /*progressive=*/false);
    return;
  }
  const bool gpuRestLoad = std::isfinite(loadOpts_.timecode) &&
                           skinningRequested_ == SkinningMode::GPU;
  if (gpuRestLoad) opts.timecode = std::numeric_limits<double>::quiet_NaN();
  int autoW = 0, autoH = 0;
  getRequestedWindowSize(&autoW, &autoH);
  const AutoSubdivisionView autoSubdivView{
      camera_.fovYDeg(),
      autoH > 0 ? static_cast<float>(std::max(1, autoW)) /
                      static_cast<float>(autoH)
                : camera_.aspect(),
      camera_.yaw(),
      camera_.pitch(),
      tessQuality_,
      camDolly_,
      std::max(1, autoH)};
  // Streaming convert+build in one pass (also fully populates tmp.render).
  bool ok = LoadUsdMaybeAutoSubdivision(path, opts, &tmp, &drawTmp, rtPath_,
                                        &loadCtrl_, autoSubdivView);
  if (ok && gpuRestLoad) {
    const bool skeletal = SceneHasSkeletalSkinning(tmp.render);
    const bool morph = SceneHasBlendShapes(tmp.render);
    const int maxInfluences = MaxSkinInfluenceCount(tmp.render);
    const bool gpuEligible =
        renderer_ && renderer_->caps().supportsGpuSkinning &&
        (skeletal || morph) &&
        (!skeletal || (drawTmp.boneMatrixCount > 0 &&
                       (maxInfluences <= 4 ||
                        (renderer_->caps().supportsExtendedGpuSkinning &&
                         maxInfluences <= kMaxGpuTextureInfluences))));
    if (!gpuEligible) {
      DrawScene cpuDraw;
      std::string w, e;
      if (RenderSceneAtTime(tmp, loadOpts_.timecode, rtPath_, &cpuDraw, &w, &e,
                            &loadCtrl_)) {
        cpuDraw.materials = drawTmp.materials;
        cpuDraw.textures = drawTmp.textures;
        drawTmp = std::move(cpuDraw);
      } else {
        tmp.warn += w;
        tmp.err = e;
        ok = false;
      }
    }
  }
  loaded_ = std::move(tmp);
  draw_ = ok ? std::move(drawTmp) : DrawScene{};
  applyLoaded(ok, /*progressive=*/false);
}

void App::addRecentScene(const std::string& path) {
  if (path.empty()) return;
  // Normalize to an absolute path so the entry is stable regardless of cwd.
  std::string key = path;
  std::error_code ec;
  std::filesystem::path abs = std::filesystem::absolute(path, ec);
  if (!ec) key = abs.lexically_normal().string();

  auto& v = recentScenes_;
  v.erase(std::remove(v.begin(), v.end(), key), v.end());
  v.insert(v.begin(), key);
  constexpr size_t kMaxRecent = 12;
  if (v.size() > kMaxRecent) v.resize(kMaxRecent);
  gui_.setRecentScenes(v);

  if (!configPath_.empty()) {
    std::string err;
    if (!SaveRecentScenes(configPath_, v, &err)) {
      LOGW("could not save recent scenes to %s: %s", configPath_.string().c_str(),
           err.c_str());
    }
  }
}

void App::startLoadAsync(const std::string& path) {
  cancelAndJoinLoad();
  loadCtrl_.resetProgress();
  loadingPath_ = path;
  loadStart_ = std::chrono::steady_clock::now();
  loadFinished_.store(false);
  loadActive_ = true;
  pendingLoaded_ = std::make_unique<LoadedScene>();
  pendingDraw_ = std::make_unique<DrawScene>();
  LoadedScene* lp = pendingLoaded_.get();
  DrawScene* dp = pendingDraw_.get();
  // Worker touches only CPU data (no GL/VK), so this is thread-safe. The
  // streaming load convert+builds the DrawScene (dp) in one pass.
  const bool rt = rtPath_;
  LoadOptions opts = loadOpts_;
  opts.gpuSkinning = wantsNextGpuSkinning();
  // Populate GPU compressed-format capabilities so the CPU texture build can
  // cap-gate `--texture-compress` (e.g. astc -> BC7 fallback on a BC-only
  // desktop GPU). renderer_ is initialized before any load is started; caps are
  // copied by value into `opts` here, before the worker thread launches.
  if (renderer_) {
    const RendererCaps& rc = renderer_->caps();
    opts.textureOptions.caps.bc = rc.supportsBC;
    opts.textureOptions.caps.astc = rc.supportsASTC;
    opts.textureOptions.caps.etc2 = rc.supportsETC2;
    opts.textureOptions.caps.bc5 = rc.supportsBC5;
    opts.textureOptions.caps.bc6h = rc.supportsBC6H;
  }
  if (std::isfinite(opts.timecode) && skinningRequested_ == SkinningMode::GPU) {
    opts.timecode = std::numeric_limits<double>::quiet_NaN();
  }
  const bool useNext = useNextLoader_;
  int autoW = 0, autoH = 0;
  getRequestedWindowSize(&autoW, &autoH);
  const AutoSubdivisionView autoSubdivView{
      camera_.fovYDeg(),
      autoH > 0 ? static_cast<float>(std::max(1, autoW)) /
                      static_cast<float>(autoH)
                : camera_.aspect(),
      camera_.yaw(),
      camera_.pitch(),
      tessQuality_,
      camDolly_,
      std::max(1, autoH)};
  loadThread_ = std::thread([this, path, opts, lp, dp, rt, useNext,
                             autoSubdivView]() {
    if (useNext) {
      lp->ok = LoadUSDViaNext(path, opts, dp, &lp->warn, &lp->err, &loadCtrl_,
                              &pendingNextSession_);
      lp->filepath = path;
      lp->render.meta.upAxis = dp->upAxis;  // drive camera/grid up-axis
      // Surface the stage's animation range so --next gets a timeline (the Tydra
      // RenderScene meta is otherwise empty here). readAnimationRange reads these.
      if (pendingNextSession_) {
        const tinyusdz::next::Stage& stage = pendingNextSession_->GetStage();
        const double s = stage.GetStartTimeCode();
        const double e = stage.GetEndTimeCode();
        const double fps = stage.GetTimeCodesPerSecond();
        if (fps > 0.0) lp->render.meta.timeCodesPerSecond = fps;
        if (e > s) {
          lp->render.meta.startTimeCode = s;
          lp->render.meta.endTimeCode = e;
        }
      }
    } else {
      LoadUsdMaybeAutoSubdivision(path, opts, lp, dp, rt, &loadCtrl_,
                                  autoSubdivView);
    }
    loadFinished_.store(true, std::memory_order_release);
  });
}

void App::startRecomposeAsync(const std::set<std::string>& addPrimPaths) {
  if (useNextLoader_ && nextSession_) {
    if (addPrimPaths.empty() && loadOpts_.variantOverrides.empty()) return;
    cancelAndJoinLoad();
    loadCtrl_.resetProgress();
    loadingPath_ = loaded_.filepath;
    loadStart_ = std::chrono::steady_clock::now();
    loadFinished_.store(false);
    loadActive_ = true;
    pendingLoaded_ = std::make_unique<LoadedScene>();
    pendingDraw_ = std::make_unique<DrawScene>();
    pendingNextSession_ = nextSession_;
    LoadedScene* lp = pendingLoaded_.get();
    DrawScene* dp = pendingDraw_.get();
    LoadOptions opts = loadOpts_;
    opts.gpuSkinning = wantsNextGpuSkinning();
    if (!addPrimPaths.empty()) {
      opts.payloadPolicy = PayloadPolicy::Whitelist;
      opts.payloadWhitelist = addPrimPaths;
    }
    const std::string path = loaded_.filepath;
    loadThread_ = std::thread([this, path, opts, lp, dp]() {
      lp->ok = LoadUSDViaNext(path, opts, dp, &lp->warn, &lp->err, &loadCtrl_,
                              &pendingNextSession_);
      lp->filepath = path;
      lp->render.meta.upAxis = dp->upAxis;
      loadFinished_.store(true, std::memory_order_release);
    });
    return;
  }

  if (!loaded_.comp.composed || !loaded_.comp.rootLayer) return;
  if (addPrimPaths.empty() && loadOpts_.variantOverrides.empty()) return;
  cancelAndJoinLoad();
  loadCtrl_.resetProgress();
  loadingPath_ = loaded_.filepath;
  loadStart_ = std::chrono::steady_clock::now();
  loadFinished_.store(false);
  loadActive_ = true;
  pendingLoaded_ = std::make_unique<LoadedScene>();
  pendingDraw_ = std::make_unique<DrawScene>();
  LoadedScene* lp = pendingLoaded_.get();
  DrawScene* dp = pendingDraw_.get();

  // Snapshot composition state for the worker: the root layer is shared
  // (read-only) and the whitelist is the union of already-loaded payloads and
  // the new requests.
  CompositionInfo prev;
  prev.composed = true;
  prev.rootLayer = loaded_.comp.rootLayer;
  prev.searchPaths = loaded_.comp.searchPaths;
  LoadOptions opts = loadOpts_;
  opts.gpuSkinning = wantsNextGpuSkinning();
  opts.payloadPolicy = PayloadPolicy::Whitelist;
  opts.payloadWhitelist = loaded_.comp.loadedPayloads;
  opts.payloadWhitelist.insert(addPrimPaths.begin(), addPrimPaths.end());

  const std::string path = loaded_.filepath;
  const bool rt = rtPath_;
  loadThread_ = std::thread([this, path, prev, opts, lp, dp, rt]() {
    RecomposeWithPayloads(path, prev, opts, lp, dp, rt, &loadCtrl_);
    loadFinished_.store(true, std::memory_order_release);
  });
}

void App::finishLoadIfReady() {
  if (!loadActive_) return;
  if (!loadFinished_.load(std::memory_order_acquire)) return;
  if (loadThread_.joinable()) loadThread_.join();  // sync point: worker fully done
  loaded_ = std::move(*pendingLoaded_);
  const bool ok = loaded_.ok;
  draw_ = ok ? std::move(*pendingDraw_) : DrawScene{};
  nextSession_ = ok ? std::move(pendingNextSession_) : nullptr;
  pendingNextSession_.reset();
  pendingLoaded_.reset();
  pendingDraw_.reset();
  loadActive_ = false;
  applyLoaded(ok, /*progressive=*/true);  // stream to GPU over the next frames
}

void App::cancelAndJoinLoad() {
  // A pending file load supersedes any in-flight playback re-evaluation (which
  // reads loaded_ on a worker thread): stop it before loaded_ is replaced.
  cancelAndJoinReconvert();
  if (loadThread_.joinable()) {
    loadCtrl_.cancel.store(true);
    loadThread_.join();
  }
  loadActive_ = false;
  loadFinished_.store(false);
  pendingLoaded_.reset();
  pendingDraw_.reset();
  pendingNextSession_.reset();
}

void App::readAnimationRange() {
  const auto& m = loaded_.render.meta;
  animFps_ = m.timeCodesPerSecond > 0.0 ? m.timeCodesPerSecond : 24.0;
  if (m.startTimeCode.has_value() && m.endTimeCode.has_value() &&
      m.endTimeCode.value() > m.startTimeCode.value()) {
    animStart_ = m.startTimeCode.value();
    animEnd_ = m.endTimeCode.value();
    hasAnimation_ = true;
  } else {
    animStart_ = animEnd_ = 0.0;
    hasAnimation_ = false;
  }
  animTime_ = animStart_;
  reconvApplied_ = animStart_;
  animPlaying_ = false;
  reconvHasRequest_ = false;
  haveLastFrameTime_ = false;
}

const char* App::skinningModeName(SkinningMode mode) const {
  switch (mode) {
    case SkinningMode::GPU: return "gpu";
    case SkinningMode::CPU: return "cpu";
    case SkinningMode::Auto:
    default: return "auto";
  }
}

bool App::wantsGpuSkinningLoad() const {
  return skinningRequested_ == SkinningMode::GPU ||
         skinningRequested_ == SkinningMode::Auto;
}

// Should the --next loader emit GPU skinning attributes instead of baking the
// pose into the geometry at load? (LoadOptions::gpuSkinning; the Tydra path
// always emits them and decides in updateSkinningEffective.)
//
// The Vulkan ray tracer keeps the rest pose too: it cannot run the raster vertex
// shader (its BLAS is built from vertex buffers), but it re-poses those retained
// rest vertices per frame and rebuilds the BLAS -- far cheaper than what it used
// to do, which was re-run the entire converter for every new time code. See
// updateNextDeformFrameIfNeeded.
//
// The CUDA/HIP tracers read draw_ geometry directly rather than owning vertex
// buffers, so they cannot run the raster vertex shader either -- but they can
// take the same re-posed vertices, written back into draw_ before their BVH is
// built (poseNextDrawForTracer). They used to be pinned to the load-time CPU
// bake, which meant a converter re-run for every new time code and, worse, a
// SECOND deform implementation that could (and did) disagree with the shader's.
bool App::wantsNextGpuSkinning() const {
  return useNextLoader_ && wantsGpuSkinningLoad() && renderer_ &&
         renderer_->caps().supportsGpuSkinning;
}

void App::updateSkinningEffective() {
  skinningEffective_ = SkinningMode::CPU;
  skinningReason_ = "CPU skinning selected";
  skinFrame_ = SkinningFrameCPU{};
  skinFrameTime_ = std::numeric_limits<double>::quiet_NaN();
  lastRtActiveForSkinning_ = renderer_ && renderer_->rayTracingActive();
  if (renderer_) renderer_->uploadSkinningFrame(skinFrame_);

  if (skinningRequested_ == SkinningMode::CPU) return;
  if (!renderer_ || !renderer_->caps().supportsGpuSkinning) {
    skinningReason_ = "GPU skinning unsupported by renderer";
    return;
  }
  const bool rtActive = renderer_->rayTracingActive();
  // --next has no RenderScene: what it can deform is recorded in the DrawScene
  // (a bone-matrix layout for skinning, morph channels for blendshapes).
  const bool skeletal = useNextLoader_
                            ? draw_.boneMatrixCount > 0
                            : SceneHasSkeletalSkinning(loaded_.render);
  const bool morph = useNextLoader_ ? DrawSceneHasMorphChannels(draw_)
                                    : SceneHasBlendShapes(loaded_.render);
  if (!loaded_.ok || (!skeletal && !morph)) {
    skinningReason_ = "scene has no skeletal skinning or blendshapes";
    return;
  }
  // The CUDA/HIP ray tracers read CPU-side draw_ geometry directly, so the raster
  // vertex shader's deform never reaches them: they would trace the rest pose.
  // Under the LEGACY loader the only way to pose them is the CPU bake, which the
  // reconvert path writes into draw_ before the tracer builds its BVH. The next
  // loader instead re-poses the retained rest vertices straight into draw_
  // (poseNextDrawForTracer), so it keeps the GPU deform data -- one deform
  // implementation for every backend, and no converter re-run per time code.
  if ((cudaRt_ || hipRt_) && !useNextLoader_) {
    skinningReason_ = "CPU skinning (legacy loader + CUDA/HIP tracer reads CPU geometry)";
    return;
  }
  // The next loader renormalizes onto the 4 strongest influences per vertex, so
  // it never needs the extended (texture) influence path.
  const int maxInfluences =
      useNextLoader_ ? 0 : MaxSkinInfluenceCount(loaded_.render);
  if (maxInfluences > 4 &&
      (!renderer_->caps().supportsExtendedGpuSkinning ||
       maxInfluences > kMaxGpuTextureInfluences)) {
    skinningReason_ =
        (maxInfluences > kMaxGpuTextureInfluences
             ? "CPU skinning fallback: GPU texture path supports up to " +
                   std::to_string(kMaxGpuTextureInfluences) +
                   " influences, scene has " + std::to_string(maxInfluences)
             : (skinningRequested_ == SkinningMode::GPU
                    ? "CPU skinning fallback: GPU path currently supports 4 influences, scene has " +
                          std::to_string(maxInfluences)
                    : "CPU skinning selected for high influence count (" +
                          std::to_string(maxInfluences) + " > 4)"));
    return;
  }
  if (skeletal && draw_.boneMatrixCount <= 0) {
    skinningReason_ = "draw scene has no GPU bone matrix layout";
    return;
  }
  skinningEffective_ = SkinningMode::GPU;
  if (rtActive) {
    skinningReason_ = skeletal && morph
                          ? "RT skeletal + blendshape skinning"
                          : morph ? "RT blendshape morph" : "RT skeletal skinning";
  } else {
    skinningReason_ = skeletal && morph ? "GPU skeletal + blendshape skinning"
                      : morph           ? "GPU blendshape morph"
                                        : "GPU skeletal skinning";
  }
}

void App::updateGpuSkinningFrameIfNeeded() {
  // --next has no RenderScene for Tydra to pose from; it re-poses from its
  // retained Stage instead (bone matrices + GPU morph coefficients).
  if (useNextLoader_) { updateNextDeformFrameIfNeeded(); return; }
  if (skinningEffective_ != SkinningMode::GPU || !loaded_.ok) return;
  const bool hasMorph = SceneHasBlendShapes(loaded_.render);
  const bool mixed = SceneHasNonSkeletalAnimation(loaded_.render);
  // Per-mesh morph/world updates need the meshes present in the renderer; wait
  // for progressive streaming to finish (the bone texture alone is safe early).
  if (progressiveActive_ && (hasMorph || mixed)) return;
  // Manual blendshape weights (Maya-like editor) force a re-pose even at the same
  // time code, since the weights changed rather than the animation clock.
  const bool blendDirty = hasMorph && gui_.consumeBlendDirty();
  if (skinFrameTime_ == animTime_ && !blendDirty) return;  // already posed

  // Per-mesh vertex/world updates index the renderer by DrawScene mesh order;
  // that only holds when the renderer uploaded exactly these meshes. Guard it so
  // a future divergence degrades gracefully (rest pose) instead of posing the
  // wrong mesh.
  const bool idxOk =
      renderer_->meshCount() == static_cast<int>(draw_.meshes.size());
  if (!idxOk && !warnedMeshIndexMismatch_) {
    LOGW("GPU skinning: renderer mesh count (%d) != draw mesh count (%zu); "
         "skipping per-mesh morph/world updates.",
         renderer_->meshCount(), draw_.meshes.size());
    warnedMeshIndexMismatch_ = true;
  }

  // Node/xform animation alongside skinning: re-evaluate the animated mesh world
  // transforms (no geometry re-pack) and push them to the renderer.
  if (idxOk && mixed && UpdateAnimatedMeshWorlds(loaded_.stage, &draw_, animTime_)) {
    for (size_t i = 0; i < draw_.meshes.size(); ++i) {
      renderer_->updateMeshWorld(static_cast<int>(i), draw_.meshes[i].world);
    }
  }

  if (renderer_->rayTracingActive()) {
    if (!idxOk) return;
    std::vector<RtSkinnedMeshUpload> uploads;
    if (BuildRtSkinnedMeshVertices(loaded_.stage, loaded_.render, &draw_,
                                   animTime_, gui_.blendOverrides(),
                                   gui_.showSkeletonOverlay(), &uploads)) {
      for (const RtSkinnedMeshUpload& upload : uploads) {
        renderer_->updateMeshVertices(upload.meshIndex, upload.vertices);
      }
    }
    skinFrameTime_ = animTime_;
    return;
  }

  // Bone matrices for GPU skinning. Blendshapes are applied in the vertex shader
  // (GPU morph, via updateMorphWeights below), not by CPU-morphing the VBO.
  if (BuildGpuSkinningFrame(loaded_.render, &draw_, animTime_, &skinFrame_,
                            gui_.showSkeletonOverlay())) {
    renderer_->uploadSkinningFrame(skinFrame_);
  }
  // GPU blendshape morph: upload only the tiny per-channel coefficient array per
  // morphed mesh (the vertex shader sums coeff*delta). No VBO re-upload, no GPU
  // stall. Meshes whose weights fall to 0 get a zero coefficient array (no morph).
  if (hasMorph && idxOk) {
    std::vector<std::pair<int, std::vector<float>>> coeffs;
    BuildMorphChannelWeights(loaded_.stage, draw_, animTime_,
                             gui_.blendOverrides(), &coeffs);
    for (auto& mc : coeffs) {
      renderer_->updateMorphWeights(mc.first, mc.second);
    }
  }
  skinFrameTime_ = animTime_;
}

void App::updateNextDeformFrameIfNeeded() {
  if (!useNextLoader_ || !nextSession_ || !loaded_.ok) return;
  if (skinningEffective_ != SkinningMode::GPU) return;
  const bool hasSkin = draw_.boneMatrixCount > 0;
  if (!hasNextMorph_ && !hasSkin) return;
  // Manual blendshape weights (editor) re-pose even at the same time code.
  const bool blendDirty = gui_.consumeBlendDirty();
  if (skinFrameTime_ == animTime_ && !blendDirty) return;  // already posed
  // Per-mesh coefficient upload indexes the renderer by draw-mesh order; only
  // valid once the renderer holds exactly these meshes (post-streaming). Mark
  // "posed" only when we actually uploaded, so a too-early call (meshes not yet
  // streamed on the threaded path) re-tries next frame instead of latching.
  // (The bone texture is scene-wide, so it is safe to upload before then -- but
  // keep both on one clock so a partially-streamed frame is never half-posed.)
  if (renderer_->meshCount() != static_cast<int>(draw_.meshes.size())) return;

  // Ray tracing traces the vertex buffers themselves, so the raster shader's
  // deform never reaches it: re-pose the retained rest vertices on the CPU and
  // hand them to the renderer, which refills the RT vertex buffer and rebuilds
  // that mesh's BLAS. The alternative -- what this path did before -- was to
  // re-run the whole converter at every time code.
  if (renderer_->rayTracingActive()) {
    std::vector<RtSkinnedMeshUpload> uploads;
    if (BuildNextRtDeformedVertices(nextSession_->GetStage(), draw_, animTime_,
                                    gui_.blendOverrides(), &uploads)) {
      for (const RtSkinnedMeshUpload& up : uploads) {
        renderer_->updateMeshVertices(up.meshIndex, up.vertices);
      }
    }
    skinFrameTime_ = animTime_;
    return;
  }

  if (hasSkin && BuildNextSkinningFrame(nextSession_->GetStage(), &draw_,
                                        animTime_, &skinFrame_)) {
    renderer_->uploadSkinningFrame(skinFrame_);
  }
  if (hasNextMorph_) {
    std::vector<std::pair<int, std::vector<float>>> coeffs;
    BuildNextMorphWeights(nextSession_->GetStage(), draw_, animTime_,
                          gui_.blendOverrides(), &coeffs);
    for (auto& mc : coeffs) renderer_->updateMorphWeights(mc.first, mc.second);
  }
  skinFrameTime_ = animTime_;
}

bool App::sceneIsNextDeformable() const {
  return useNextLoader_ && nextSession_ && loaded_.ok &&
         (hasNextMorph_ || draw_.boneMatrixCount > 0);
}

// Write the pose at `time` into draw_ geometry, for the CUDA/HIP tracers -- which
// build their BVH from draw_ meshes rather than from renderer-owned vertex
// buffers, and so cannot be fed the way Vulkan RT is (updateMeshVertices).
//
// The rest pose is snapshotted on first use and restored before every re-pose, so
// this is idempotent and can run at any time code in any order. Only deformable
// meshes are copied. Returns true when draw_ now holds the pose at `time`.
bool App::poseNextDrawForTracer(double time) {
  if (!sceneIsNextDeformable()) return false;
  if (nextRestVerts_.empty()) {
    for (size_t i = 0; i < draw_.meshes.size(); ++i) {
      const DrawMeshCPU& m = draw_.meshes[i];
      if (m.vertices.empty()) continue;
      if (m.jointIdx.empty() && m.morphDeltaHalf.empty()) continue;
      nextRestVerts_[static_cast<int>(i)] = m.vertices;
    }
    if (nextRestVerts_.empty()) return false;
  }
  // BuildNextRtDeformedVertices deforms whatever is in draw_, so it must see the
  // REST pose -- not the pose we left behind at the previous time code.
  for (const auto& kv : nextRestVerts_) {
    const size_t i = static_cast<size_t>(kv.first);
    if (i < draw_.meshes.size()) draw_.meshes[i].vertices = kv.second;
  }
  std::vector<RtSkinnedMeshUpload> uploads;
  if (!BuildNextRtDeformedVertices(nextSession_->GetStage(), draw_, time,
                                   gui_.blendOverrides(), &uploads)) {
    return false;
  }
  for (RtSkinnedMeshUpload& up : uploads) {
    if (up.meshIndex < 0 ||
        static_cast<size_t>(up.meshIndex) >= draw_.meshes.size()) {
      continue;
    }
    draw_.meshes[static_cast<size_t>(up.meshIndex)].vertices =
        std::move(up.vertices);
  }
  nextTracerPosedTime_ = time;
  return true;
}

void App::maybeReconvertForManualBlend() {
  // The GPU path repools live in updateGpuSkinningFrameIfNeeded(); only the
  // ray-traced / CPU-skinned path needs a geometry+BLAS rebuild here.
  if (skinningEffective_ == SkinningMode::GPU) return;
  if (!loaded_.ok || !SceneHasBlendShapes(loaded_.render)) return;
  if (gui_.consumeBlendDirty()) blendReconvNeeded_ = true;
  if (blendReconvNeeded_ && !reconvActive_ && !loadActive_ && !progressiveActive_) {
    blendReconvNeeded_ = false;
    requestReconvert(animTime_);  // starts immediately (no reconvert in flight)
  }
}

void App::advancePlayback(float dtSec) {
  if (!hasAnimation_ || !animPlaying_) return;
  const double span = animEnd_ - animStart_;
  animTime_ += static_cast<double>(dtSec) * animFps_ *
               static_cast<double>(animSpeed_);
  if (animTime_ > animEnd_) {
    if (animLoop_ && span > 0.0) {
      animTime_ = animStart_ + std::fmod(animTime_ - animStart_, span);
    } else {
      animTime_ = animEnd_;
      animPlaying_ = false;
    }
  } else if (animTime_ < animStart_) {
    animTime_ = animStart_;
  }
  requestReconvert(animTime_);
}

void App::requestReconvert(double t) {
  // Skip while a fresh file load is streaming (loaded_/draw_ are in flux); the
  // worker would also race the load writing loaded_.
  if (!loaded_.ok || loadActive_) return;
  if (skinningEffective_ == SkinningMode::GPU) {
    reconvApplied_ = t;
    skinFrameTime_ = std::numeric_limits<double>::quiet_NaN();
    return;
  }
  reconvRequested_ = t;
  reconvHasRequest_ = true;
  if (!reconvActive_) startReconvertAsync(t);
}

void App::startReconvertAsync(double t) {
  reconvActive_ = true;
  reconvInFlight_ = t;
  reconvHasRequest_ = false;
  reconvFinished_.store(false);
  reconvOk_.store(false);
  reconvCtrl_.cancel.store(false);
  reconvCtrl_.resetProgress();
  reconvDraw_ = std::make_unique<DrawScene>();
  DrawScene* dp = reconvDraw_.get();
  const bool rt = rtPath_;
  // Snapshot the manual blendshape weights on the main thread (gui_ is only
  // touched here); the worker bakes them into the deformed/BLAS geometry.
  std::unordered_map<std::string, float> ovr;
  if (const auto* o = gui_.blendOverrides()) ovr = *o;
  // Worker reads loaded_ (stage/mmap/filepath) read-only; the main thread keeps
  // loaded_ alive and joins this worker (cancelAndJoinReconvert) before any
  // reload. RenderSceneAtTime skips texture decode and fills only dp->meshes.
  // The rest cache only pays off for repeated same-timecode reconverts (dragging
  // a blendshape weight while paused). During playback the timecode changes every
  // frame, so it would never hit and the per-frame copy-into-cache would be pure
  // overhead -- skip it then.
  RestSceneCache* cache = animPlaying_ ? nullptr : &reconvRestCache_;
  reconvThread_ = std::thread([this, t, dp, rt, cache, ovr = std::move(ovr)]() {
    std::string w, e;
    const bool ok = RenderSceneAtTime(loaded_, t, rt, dp, &w, &e, &reconvCtrl_,
                                      ovr.empty() ? nullptr : &ovr, cache);
    reconvOk_.store(ok, std::memory_order_relaxed);
    reconvFinished_.store(true, std::memory_order_release);
  });
}

void App::finishReconvertIfReady() {
  if (!reconvActive_) return;
  if (!reconvFinished_.load(std::memory_order_acquire)) return;
  if (reconvThread_.joinable()) reconvThread_.join();
  reconvActive_ = false;

  if (reconvOk_.load(std::memory_order_relaxed) && !reconvCtrl_.cancel.load() &&
      reconvDraw_) {
    // Swap in the re-evaluated geometry while keeping the initial load's
    // materials/textures (they don't animate). draw_ is mutated in place so the
    // GUI's pointer (and selection/visibility state) stays valid — do NOT call
    // setScene here.
    draw_.meshes = std::move(reconvDraw_->meshes);
    draw_.triangleCount = reconvDraw_->triangleCount;
    draw_.truncated = reconvDraw_->truncated;
    reconvApplied_ = reconvInFlight_;
    std::string uerr;
    renderer_->uploadScene(draw_, &uerr);  // camera untouched (no refit)
  }
  reconvDraw_.reset();

  // Coalesce: if playback advanced past the time we just computed, start the
  // next re-evaluation toward the latest requested time.
  if (reconvHasRequest_ && reconvRequested_ != reconvApplied_ && !loadActive_) {
    startReconvertAsync(reconvRequested_);
  }
}

void App::cancelAndJoinReconvert() {
  if (reconvThread_.joinable()) {
    reconvCtrl_.cancel.store(true);
    reconvThread_.join();
  }
  reconvActive_ = false;
  reconvFinished_.store(false);
  reconvHasRequest_ = false;
  reconvDraw_.reset();
  // The cached rest scene belongs to the (possibly outgoing) scene; drop it now
  // that the worker is joined. Called on reload + skinning-mode switch, not during
  // interactive blendshape reconverts (those queue), so this doesn't defeat it.
  reconvRestCache_ = RestSceneCache{};
}

void App::openFileDialog() {
#if defined(HAVE_NFD)
  NFD_Init();
  nfdu8char_t* outPath = nullptr;
  nfdu8filteritem_t filters[1] = {{"USD", "usd,usda,usdc,usdz"}};
  nfdresult_t r = NFD_OpenDialogU8(&outPath, filters, 1, nullptr);
  if (r == NFD_OKAY && outPath) {
    std::string p = outPath;
    NFD_FreePathU8(outPath);
    startLoadAsync(p);
  }
  NFD_Quit();
#else
  LOGW("File dialog not available in this build. Pass a USD file on the command line.");
#endif
}

#if defined(TUSDVIEW_ENABLE_GL_THREAD)
// --- Experimental threaded GL rendering: the render thread owns the GL context ---

void App::postGpu(std::function<void()> op) {
  if (!renderThreadActive_) { op(); return; }  // inline on the single-threaded path
  std::lock_guard<std::mutex> lk(gpuOpMutex_);
  gpuOps_.push(std::move(op));
}

void App::drainGpuOps() {
  for (;;) {
    std::function<void()> op;
    {
      std::lock_guard<std::mutex> lk(gpuOpMutex_);
      if (gpuOps_.empty()) break;
      op = std::move(gpuOps_.front());
      gpuOps_.pop();
    }
    op();
  }
}

bool App::startRenderThread() {
  renderRunning_.store(true);
  renderInitDone_.store(false);
  renderInitOk_.store(false);
  renderThread_ = std::thread(&App::renderThreadMain, this);
  while (!renderInitDone_.load(std::memory_order_acquire))
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  if (!renderInitOk_.load()) { joinRenderThread(); return false; }
  return true;
}

void App::joinRenderThread() {
  renderRunning_.store(false);
  pktCv_.notify_all();
  pktDoneCv_.notify_all();
  if (renderThread_.joinable()) renderThread_.join();
}

void App::submitFramePacket(std::unique_ptr<FramePacket> pkt, bool blockUntilDone) {
  const std::uint64_t seq = pkt->seq;
  {
    std::lock_guard<std::mutex> lk(pktMutex_);
    if (pendingPacket_ && pendingPacket_->drawData)
      FreeImDrawData(pendingPacket_->drawData);  // latest-wins: drop the stale frame
    pendingPacket_ = std::move(pkt);
  }
  pktCv_.notify_one();
  if (blockUntilDone) {  // capture/deterministic mode
    std::unique_lock<std::mutex> lk(pktMutex_);
    pktDoneCv_.wait(lk, [&] {
      return pktRenderedSeq_ >= seq || !renderRunning_.load();
    });
  }
}

void App::renderThreadMain() {
  // GL: acquire the context (released by the main thread) so all GL objects
  // (FBO/shaders + ImGui GL backend) are created on this thread's current context.
  // Vulkan has no per-thread "current context" — the only rule is that one thread
  // owns the queue submits, which this thread does — so it skips the GLFW calls.
  const bool glBackend = (backend_ == Backend::GL);
  if (glBackend) glfwMakeContextCurrent(window_);
  std::string err;
  bool ok = renderer_->init(window_, &err);
  if (!ok) LOGE("render thread: renderer init: %s", err.c_str());
  else if (!(ok = renderer_->initImGuiBackend(&err)))
    LOGE("render thread: ImGui GL backend init: %s", err.c_str());
  renderInitOk_.store(ok);
  renderInitDone_.store(true, std::memory_order_release);
  if (!ok) { if (glBackend) glfwMakeContextCurrent(nullptr); return; }

  while (renderRunning_.load()) {
    drainGpuOps();  // uploads/resize/instance-visibility, FIFO, before the frame
    std::unique_ptr<FramePacket> pkt;
    {
      std::unique_lock<std::mutex> lk(pktMutex_);
      pktCv_.wait_for(lk, std::chrono::milliseconds(4), [&] {
        return pendingPacket_ != nullptr || !renderRunning_.load();
      });
      if (pendingPacket_) pkt = std::move(pendingPacket_);
    }
    if (!pkt) continue;
    renderer_->newFrame();  // ImGui backend NewFrame (GL: ImGui_ImplOpenGL3, VK: ImGui_ImplVulkan)
    if (pkt->hasParams) {
      RenderFrameParams params = pkt->params();
      renderer_->renderFrame(params);
    }
    if (pkt->wantCapture)  // read the 3D offscreen target (GL FBO / VK offscreen img)
      renderer_->captureViewport(&renderCapture_, &renderCaptureW_, &renderCaptureH_);
    renderer_->presentThreaded(pkt->drawData, pkt->fbW, pkt->fbH);
    FreeImDrawData(pkt->drawData);
    pkt->drawData = nullptr;
    {
      std::lock_guard<std::mutex> lk(pktMutex_);
      pktRenderedSeq_ = pkt->seq;
    }
    pktDoneCv_.notify_all();
  }
  {
    std::lock_guard<std::mutex> lk(pktMutex_);
    if (pendingPacket_ && pendingPacket_->drawData)
      FreeImDrawData(pendingPacket_->drawData);
    pendingPacket_.reset();
  }
  renderer_->shutdown();
  if (glBackend) glfwMakeContextCurrent(nullptr);
}
#endif  // TUSDVIEW_ENABLE_GL_THREAD

bool App::renderHipViewport() {
  // The initial model loads on a worker thread; wait until it has been applied on
  // the main thread (finishLoadIfReady -> applyLoaded) and draw_ holds geometry.
  // Returning true (not false) here keeps hipInteractive_ enabled across the wait.
  if (loadActive_ || draw_.meshes.empty() || draw_.triangleCount == 0) {
    // Show a clear viewport while loading -- also keeps colorImg_ in a defined,
    // ImGui-sampleable layout (nothing else writes it on the HIP path).
    int vw = 0, vh = 0;
    gui_.viewportPixelSize(&vw, &vh);
    if (vw > 0 && vh > 0) {
      std::vector<uint8_t> clearPx(static_cast<size_t>(vw) * vh * 4);
      for (size_t i = 0; i + 3 < clearPx.size(); i += 4) {
        clearPx[i] = 31; clearPx[i + 1] = 31; clearPx[i + 2] = 33; clearPx[i + 3] = 255;
      }
      renderer_->uploadViewportImage(clearPx.data(), vw, vh);
    }
    return true;
  }

  // Build the HIP scene once. It runs on a BACKGROUND THREAD so the UI stays
  // responsive and the progress overlay updates live (the build is multi-second on
  // big scenes). Present a couple of frames first so the loading modal has closed
  // and the overlay is visible before the worker starts.
  if (!hipInteractiveBuilt_) {
    if (hipBuildAnnounceFrames_ < 2) {
      ++hipBuildAnnounceFrames_;
      return true;
    }
    if (!hipBuildStarted_) {
      std::string cerr;
      if (!hipTracer_.init(&cerr)) {
        LOGW("HIP ray tracing unavailable: %s; viewport stays blank.", cerr.c_str());
        hipInteractive_ = false;  // give up; avoid retrying every frame
        return false;
      }
      hipBuildStarted_ = true;
      hipBuildStart_ = std::chrono::steady_clock::now();
      const float dispScale = gui_.displacementScale();  // read on the main thread
      poseNextDrawForTracer(animTime_);  // on the main thread, before the worker reads draw_
      // The worker reads draw_ (stable while building: the re-pose below only runs
      // once the build has completed) and builds + uploads on the device
      // (hipSetDevice runs in build()).
      hipBuildThread_ = std::thread([this, dispScale] {
        std::string e;
        const bool ok = hipTracer_.build(draw_, cudaMaxTris_, rtMaxInstances_, &e,
                                         dispScale, &hipBuildProgress_);
        hipBuildErr_ = e;
        hipBuildOk_.store(ok, std::memory_order_release);
        hipBuildDone_.store(true, std::memory_order_release);
      });
      return true;
    }
    if (!hipBuildDone_.load(std::memory_order_acquire)) {
      return true;  // still building -> overlay shows live progress
    }
    if (hipBuildThread_.joinable()) hipBuildThread_.join();
    if (!hipBuildOk_.load(std::memory_order_acquire)) {
      LOGW("HIP ray tracing build failed: %s", hipBuildErr_.c_str());
      hipInteractive_ = false;
      return false;
    }
    hipInteractiveBuilt_ = true;
    LOGI("HIP interactive: %zu tris%s on %s", hipTracer_.triangleCount(),
         hipTracer_.truncated() ? " [truncated]" : "", hipTracer_.deviceName());
    // A deformable scene keeps its CPU geometry: the timeline re-poses it and
    // rebuilds the BVH below, so draw_ is read again on every new time code.
    if (sceneIsNextDeformable()) return true;
    // Otherwise the scene now lives entirely in the GPU BVH; reclaim the (large)
    // CPU geometry -- the interactive trace never reads draw_ geometry again.
    auto rssMB = [] {
      FILE* f = std::fopen("/proc/self/statm", "r");
      if (!f) return size_t(0);
      long pages = 0, res = 0;
      if (std::fscanf(f, "%ld %ld", &pages, &res) != 2) res = 0;
      std::fclose(f);
#ifdef _WIN32
      return size_t(0);  // unreachable: /proc/self/statm doesn't exist on Windows
#else
      return size_t((static_cast<long long>(res) * sysconf(_SC_PAGESIZE)) / (1024 * 1024));
#endif
    };
    const size_t before = rssMB();
    for (DrawMeshCPU& m : draw_.meshes) FreeMeshGeometryCPUForRT(m);
    const size_t after = rssMB();
    if (before > after)
      LOGI("freed CPU geometry after RT build: %zu -> %zu MB host RSS", before, after);
  }

  // Animation. The BVH is built from vertex positions, so a new time code means a
  // re-pose and a rebuild -- but a rebuild only, not the whole-converter re-run
  // this path used to need (it had none: the HIP scene was simply frozen at the
  // load time code). Synchronous: the timeline should not run ahead of what is on
  // screen, and a rebuild is a fraction of the initial build (no conversion, no
  // material/texture work).
  if (sceneIsNextDeformable() && animTime_ != nextTracerPosedTime_) {
    if (poseNextDrawForTracer(animTime_)) {
      std::string e;
      if (!hipTracer_.build(draw_, cudaMaxTris_, rtMaxInstances_, &e,
                            gui_.displacementScale())) {
        LOGW("HIP re-pose rebuild failed: %s", e.c_str());
      }
    }
  }

  int w = 0, h = 0;
  gui_.viewportPixelSize(&w, &h);
  if (w < 1 || h < 1) return true;  // viewport not laid out yet this frame

  camera_.setAspect(static_cast<float>(w) / static_cast<float>(h));
  const light3d::Mat4 pv = camera_.proj(/*zeroToOneDepth=*/true) * camera_.view();
  const light3d::Mat4 inv = pv.inverse();
  const light3d::Vec3 eye = camera_.eye();
  const float camPos[3] = {eye.x, eye.y, eye.z};
  float lightDir[3];
  CopyPreviewLightDir(draw_, lightDir);
  const float clear[3] = {0.12f, 0.12f, 0.13f};
  const int rmode = static_cast<int>(gui_.renderMode());
  float depthScale = 1.0f;
  float sceneMin[3] = {0, 0, 0}, sceneExtent[3] = {1, 1, 1};
  if (draw_.hasBounds) {
    const float dx = draw_.aabbMax[0] - draw_.aabbMin[0];
    const float dy = draw_.aabbMax[1] - draw_.aabbMin[1];
    const float dz = draw_.aabbMax[2] - draw_.aabbMin[2];
    depthScale = std::max(1e-3f, std::sqrt(dx * dx + dy * dy + dz * dz));
    for (int i = 0; i < 3; ++i) {
      sceneMin[i] = draw_.aabbMin[i];
      sceneExtent[i] = std::max(1e-4f, draw_.aabbMax[i] - draw_.aabbMin[i]);
    }
  }

  std::vector<uint8_t> rgba;
  std::string cerr;
  // spp=1: single sample for interactive frame rate (no supersampled AA).
  if (hipTracer_.trace(inv.m, pv.m, camPos, lightDir, clear, rmode, depthScale, sceneMin,
                       sceneExtent, w, h, &rgba, &cerr, /*spp=*/1)) {
    renderer_->uploadViewportImage(rgba.data(), w, h);
  } else {
    LOGW("HIP ray trace failed: %s", cerr.c_str());
  }
  return true;
}

void App::markStreamActivity() {
  streamLastActivity_ = std::chrono::steady_clock::now();
  streamHiQSent_ = false;
}

void App::streamEncodeAndPush(std::vector<uint8_t> rgba, int w, int h,
                              bool motion) {
  if (!streamServer_ || w <= 0 || h <= 0) return;

  int tw = w, th = h;
  std::vector<uint8_t> scaled;
  if (motion && std::max(w, h) > streamMotionMaxDim_) {
    // Downscale (sRGB-correct) to a small frame for fast interaction.
    const double s = double(streamMotionMaxDim_) / double(std::max(w, h));
    tw = std::max(1, int(w * s));
    th = std::max(1, int(h * s));
    scaled.resize(size_t(tw) * size_t(th) * 4);
    if (stbir_resize_uint8_srgb(rgba.data(), w, h, 0, scaled.data(), tw, th, 0,
                                STBIR_RGBA)) {
      rgba = std::move(scaled);
    } else {
      tw = w; th = h;  // resize failed: fall back to full size
    }
  }

  tinyusdz::Image img;
  img.width = tw;
  img.height = th;
  img.channels = 4;
  img.bpp = 8;
  img.format = tinyusdz::Image::PixelFormat::UInt;
  img.data = std::move(rgba);

  tinyusdz::image::WriteOption opt;
  if (motion) {
    opt.format = tinyusdz::image::WriteImageFormat::JPEG;
    opt.jpeg_quality = streamMotionJpegQ_;
  } else if (streamIdleCodec_ == "qoi") {
    opt.format = tinyusdz::image::WriteImageFormat::QOI;
  } else {  // default refinement: PNG (lossless)
    opt.format = tinyusdz::image::WriteImageFormat::PNG;
  }
  auto enc = tinyusdz::image::WriteImageToMemory(img, opt);
  if (enc) streamServer_->pushFrame(enc.value().data(), enc.value().size());
}

// Map a browser KeyboardEvent.key string to an ImGuiKey so ImGui text fields and
// keyboard navigation (arrows, backspace, enter, tab, ...) work over the stream.
static ImGuiKey StreamKeyToImGui(const std::string& k) {
  if (k.size() == 1) {
    const char c = k[0];
    if (c >= 'a' && c <= 'z') return ImGuiKey(ImGuiKey_A + (c - 'a'));
    if (c >= 'A' && c <= 'Z') return ImGuiKey(ImGuiKey_A + (c - 'A'));
    if (c >= '0' && c <= '9') return ImGuiKey(ImGuiKey_0 + (c - '0'));
    if (c == ' ') return ImGuiKey_Space;
  }
  if (k == "Enter") return ImGuiKey_Enter;
  if (k == "Backspace") return ImGuiKey_Backspace;
  if (k == "Delete") return ImGuiKey_Delete;
  if (k == "Tab") return ImGuiKey_Tab;
  if (k == "Escape") return ImGuiKey_Escape;
  if (k == "ArrowLeft") return ImGuiKey_LeftArrow;
  if (k == "ArrowRight") return ImGuiKey_RightArrow;
  if (k == "ArrowUp") return ImGuiKey_UpArrow;
  if (k == "ArrowDown") return ImGuiKey_DownArrow;
  if (k == "Home") return ImGuiKey_Home;
  if (k == "End") return ImGuiKey_End;
  if (k == "PageUp") return ImGuiKey_PageUp;
  if (k == "PageDown") return ImGuiKey_PageDown;
  return ImGuiKey_None;
}

// Apply one browser input event. Runs on the main thread BEFORE ImGui::NewFrame()
// (drained from the stream server's queue): raw mouse/keyboard events are injected
// into ImGui so its widgets are clickable, and when ImGui isn't capturing the
// mouse the same drags drive the camera (orbit/pan/dolly) -- like the desktop app.
void App::applyNavCommand(const StreamNav& c) {
  ImGuiIO& io = ImGui::GetIO();
  // Any browser input keeps the stream in interactive (low-latency) mode and
  // defers the next lossless refinement.
  markStreamActivity();
  switch (c.type) {
    case StreamNav::MouseMove: {
      io.AddMousePosEvent(c.x, c.y);
      if (streamCamDrag_) {
        const float dx = c.x - streamLastX_, dy = c.y - streamLastY_;
        if (streamDragButton_ == 1 || (streamDragButton_ == 0 && streamDragShift_))
          camera_.pan(dx, dy);                       // middle / shift+left = pan
        else if (streamDragButton_ == 2)
          camera_.dolly((dx - dy) * 0.05f);          // right = dolly
        else
          camera_.orbit(dx, dy);                     // left = orbit
      }
      streamLastX_ = c.x;
      streamLastY_ = c.y;
      break;
    }
    case StreamNav::MouseButton: {
      io.AddMousePosEvent(c.x, c.y);
      // DOM button (0=L,1=M,2=R) -> ImGui button (0=L,1=R,2=M).
      const int imguiBtn = (c.button == 1) ? 2 : (c.button == 2) ? 1 : 0;
      io.AddMouseButtonEvent(imguiBtn, c.down);
      if (c.down) {
        // Latch at press: drive the camera only when the press is over the 3D
        // viewport (not an ImGui panel/widget). The viewport is itself an ImGui
        // window, so WantCaptureMouse is always true there -- use the viewport
        // hover signal instead, mirroring the desktop navigation gate.
        streamCamDrag_ = gui_.viewportHovered();
        streamDragButton_ = c.button;
        streamDragShift_ = c.shift;
      } else {
        streamCamDrag_ = false;
      }
      streamLastX_ = c.x;
      streamLastY_ = c.y;
      break;
    }
    case StreamNav::Wheel:
      io.AddMousePosEvent(c.x, c.y);
      io.AddMouseWheelEvent(0.0f, c.wheel);
      if (gui_.viewportHovered()) camera_.dolly(c.wheel);
      break;
    case StreamNav::Key: {
      const std::string& k = c.str;
      // Keep ImGui's modifier state in sync, then feed the key event (press AND
      // release) so editing/navigation keys work in ImGui widgets.
      io.AddKeyEvent(ImGuiMod_Shift, c.shift);
      io.AddKeyEvent(ImGuiMod_Ctrl, c.ctrl);
      io.AddKeyEvent(ImGuiMod_Alt, c.alt);
      const ImGuiKey ik = StreamKeyToImGui(k);
      if (ik != ImGuiKey_None) io.AddKeyEvent(ik, c.down);
      if (!c.down) break;  // the rest reacts to presses only
      // Printable char into a focused ImGui text field; don't fire hotkeys while
      // editing text.
      if (io.WantTextInput) {
        if (k.size() == 1 && static_cast<unsigned char>(k[0]) >= 0x20)
          io.AddInputCharacter(static_cast<unsigned>(k[0]));
        break;
      }
      if (k == "w") {
        gui_.cycleWireframe();
      } else if (k == "f" || k == "a") {
        if (draw_.hasBounds) camera_.fitToScene(draw_.aabbMin, draw_.aabbMax);
      } else if (k == "0") {
        camera_.setPreset(CameraViewPreset::Isometric);
        if (draw_.hasBounds) camera_.fitToScene(draw_.aabbMin, draw_.aabbMax);
      } else if (k == "5") {
        camera_.setPreset(CameraViewPreset::Isometric);
      } else if (k == "1") {
        camera_.setPreset(CameraViewPreset::Front);
      } else if (k == "3") {
        camera_.setPreset(CameraViewPreset::Right);
      } else if (k == "7") {
        camera_.setPreset(CameraViewPreset::Top);
      }
      break;
    }
    case StreamNav::Load:
      if (!c.str.empty()) startLoadAsync(c.str);
      break;
    case StreamNav::Codec:
      // Selects the idle-refinement codec; re-send a refined frame in it.
      if (c.str == "png" || c.str == "qoi") {
        streamIdleCodec_ = c.str;
        streamHiQSent_ = false;
      }
      break;
    case StreamNav::Resize:
      // Windowed: resize the real window. Headless: queue a composite resize,
      // applied at the top of the next frame (updates winW/winH + the VK swap).
      if (c.w > 0 && c.h > 0) {
        if (!headless_ && window_) {
          glfwSetWindowSize(window_, c.w, c.h);
        } else if (headless_) {
          streamResizeW_ = c.w;
          streamResizeH_ = c.h;
        }
      }
      break;
  }
}

int App::run(const std::string& initialFile, int maxFrames,
             const std::string& screenshot) {
  std::string err;
  // Headless composite size (no monitor to clamp to); used for the windowless
  // ImGui DisplaySize and the offscreen composite image.
  int winW = 0;
  int winH = 0;
  getRequestedWindowSize(&winW, &winH);
#if defined(TUSDVIEW_ENABLE_GL_THREAD)
  // Threaded rendering applies to the windowed GL or Vulkan path (experimental;
  // includes Vulkan ray tracing). Headless keeps the inline single-threaded path.
  renderThreadActive_ = threaded_ && !headless_ &&
                        (backend_ == Backend::GL || backend_ == Backend::Vulkan);
#endif
  // A headless --cuda/--hip run writes its own screenshot from the RT trace and
  // returns before the rasterized capture is used, so the raster scene upload +
  // per-frame draw are pure waste (huge on heavily-instanced scenes). Skip them.
  rtOwnsScreenshot_ = (cudaRt_ || hipRt_) && headless_ && !screenshot.empty();
  // Windowed --hip: the HIP tracer drives the viewport per frame (build once,
  // retrace on the orbit camera). Skip the raster scene upload (which would stall
  // on huge instanced scenes like Moana Island) and render single-threaded so the
  // HIP launch + the colorImg_ upload happen on one thread.
  hipInteractive_ = hipRt_ && !headless_;
#if defined(TUSDVIEW_ENABLE_GL_THREAD)
  if (hipInteractive_) renderThreadActive_ = false;
  // Streaming captures the composited window + encodes inline each frame; run
  // single-threaded so the capture/encode happen on the context-owning thread.
  if (streamHttpPort_ > 0) renderThreadActive_ = false;
#endif
  if (headless_) {
    if (backend_ != Backend::Vulkan) {
      LOGE("--headless requires the Vulkan backend (pass --backend vk)");
      return 1;
    }
    // A streaming server runs an open-ended loop (no one-shot screenshot bound).
    if (maxFrames < 0 && streamHttpPort_ <= 0)
      maxFrames = 4;  // windowless runs are bounded by frame count
    // GLFW is never initialized in the headless path (no window, no surface).
  } else if (!initWindow(&err)) {
    LOGE("%s", err.c_str());
    return 1;
  }

  if (backend_ == Backend::GL) {
    renderer_ = CreateGLRenderer();
  }
#if defined(HAVE_VULKAN)
  else {
    renderer_ = CreateVulkanRenderer();
  }
#endif
  if (!renderer_) {
    LOGE("no renderer for requested backend");
    return 1;
  }
  renderer_->setDevicePreference(devicePreference_);
  if (headless_) renderer_->setHeadlessSize(winW, winH);

#if defined(TUSDVIEW_ENABLE_GL_THREAD)
  if (renderThreadActive_) {
    // Threaded: ImGui's GLFW platform init runs on the main thread; renderer_->init()
    // (device/FBO/shaders) + the ImGui render backend run on the render thread.
    // GL: release the context here so the render thread can make it current. VK has
    // no per-thread context, so there is nothing to release. startRenderThread blocks
    // until the render thread finishes init.
    if (!initImGui(&err)) {
      LOGE("ImGui init failed: %s", err.c_str());
      return 1;
    }
    if (backend_ == Backend::GL) glfwMakeContextCurrent(nullptr);
    if (!startRenderThread()) {
      LOGE("render thread init failed: %s", err.c_str());
      return 1;
    }
    // Vulkan ray tracing: rayTracingAvailable() reads device support set by init()
    // (already finished — startRenderThread joined on it). setRayTracing() only flips
    // flags + marks the TLAS dirty (built lazily in present() on the render thread),
    // but those fields are read there, so post it to run on the render thread.
    if (rtRequested_) {
      if (renderer_->rayTracingAvailable()) {
        rtPath_ = true;
        postGpu([this] { renderer_->setRayTracing(true); });
        LOGI("Vulkan ray tracing (ray query) enabled.");
      } else {
        LOGW("--rt requested but ray tracing is unavailable; using rasterization.");
      }
    }
  } else
#endif
  {
    if (!renderer_->init(headless_ ? nullptr : window_, &err)) {
      if (backend_ == Backend::Vulkan && allowBackendFallback_ && !headless_) {
        LOGW("Vulkan renderer init failed: %s; falling back to OpenGL.", err.c_str());
        renderer_->shutdown();
        renderer_.reset();
        backend_ = Backend::GL;
        err.clear();
        renderer_ = CreateGLRenderer();
        if (!renderer_ || !renderer_->init(window_, &err)) {
          LOGE("renderer init failed: %s", err.c_str());
          return 1;
        }
      } else {
        LOGE("renderer init failed: %s", err.c_str());
        return 1;
      }
    }

    // Activate Vulkan ray tracing if requested and supported; else stay on raster.
    if (rtRequested_) {
      if (renderer_->rayTracingAvailable()) {
        rtPath_ = true;
        renderer_->setRayTracing(true);
        LOGI("Vulkan ray tracing (ray query) enabled.");
      } else {
        LOGW("--rt requested but ray tracing is unavailable (needs the Vulkan "
             "backend on an RT-capable GPU + an RT-capable glslang at build time); "
             "using rasterization.");
      }
    }

    if (!initImGui(&err)) {
      LOGE("ImGui init failed: %s", err.c_str());
      return 1;
    }
  }

  {
    const RendererCaps& rendererCaps = renderer_->caps();
    LOGI("renderer: %s, GPU: %s, API: %s",
         rendererCaps.backend_name ? rendererCaps.backend_name : "unknown",
         rendererCaps.gpu_name.empty() ? "unknown"
                                       : rendererCaps.gpu_name.c_str(),
         rendererCaps.api_info.empty() ? "unknown"
                                       : rendererCaps.api_info.c_str());
  }

  gui_.setScene(&loaded_, &draw_);
  gui_.setNextStage(nextSession_ ? &nextSession_->GetStage() : nullptr);
  gui_.setBudget(&loadCtrl_);
  // Route the GUI's GPU side-effects (viewport resize, instance visibility) to the
  // render thread; runs inline on the single-threaded path.
  gui_.setPostGpu([this](std::function<void()> op) { postGpu(std::move(op)); });
  // Only the truly-interactive run-until-quit loop offloads per-instance culling to
  // a worker (UI responsiveness). Any fixed-frame-count run (headless or windowed
  // --frames/--screenshot) culls synchronously so screenshots stay deterministic.
  gui_.setCullAsync(maxFrames < 0);

#if defined(TUSDVIEW_HAVE_MCP)
  // Start the embedded MCP server (tool calls are drained on the main thread).
  if (mcpStdio_ || mcpHttpPort_ > 0) {
    mcp_ = std::make_unique<MCPServer>(this);
    if (mcpHttpPort_ > 0) mcp_->startHttp(mcpHttpPort_);
    if (mcpStdio_) mcp_->startStdio();
  }
#else
  if (mcpStdio_ || mcpHttpPort_ > 0) {
    LOGW("MCP requested but not compiled in (build with -DTUSDVIEW_ENABLE_MCP=ON).");
  }
#endif

  // WebSocket image-streaming server (independent of MCP; own port).
  if (streamHttpPort_ > 0) {
    streamServer_ = std::make_unique<StreamServer>();
    if (!streamServer_->start(streamHttpPort_)) streamServer_.reset();
  }

  if (!initialFile.empty()) {
    // Headless (--frames) loads synchronously so screenshots are deterministic;
    // interactive runs load on a worker thread to keep the window responsive.
    if (maxFrames >= 0) {
      loadFileBlocking(initialFile);
    } else {
      startLoadAsync(initialFile);
    }
  }

  int frameCount = 0;
  bool running = true;
  while (running) {
    // Apply a browser-requested headless composite resize (queued last frame):
    // recreate the VK swap at the new size and update the size ImGui draws at.
    if (headless_ && streamResizeW_ > 0 && streamResizeH_ > 0) {
      if (renderer_->resizeHeadless(streamResizeW_, streamResizeH_)) {
        winW = streamResizeW_;
        winH = streamResizeH_;
        markStreamActivity();  // re-render at the new size
      }
      streamResizeW_ = streamResizeH_ = 0;
    }
    if (headless_) {
      // No platform backend: drive ImGui's display size + timestep ourselves.
      ImGuiIO& hio = ImGui::GetIO();
      hio.DisplaySize = ImVec2(static_cast<float>(winW), static_cast<float>(winH));
      hio.DeltaTime = 1.0f / 60.0f;
    } else {
      if (glfwWindowShouldClose(window_)) break;
      glfwPollEvents();
    }

    // Pick up a completed async load, then stream its meshes/textures to the
    // GPU a little per frame so the UI stays responsive (progressive upload).
    finishLoadIfReady();
    stepProgressiveUpload();
    finishReconvertIfReady();  // swap in re-evaluated animation geometry

    // Advance the playback clock and request a re-evaluation at the new time
    // (interactive only; headless renders a fixed --time frame deterministically).
    if (!headless_) {
      const auto now = std::chrono::steady_clock::now();
      float dt = haveLastFrameTime_
                     ? std::chrono::duration<float>(now - lastFrameTime_).count()
                     : 0.0f;
      lastFrameTime_ = now;
      haveLastFrameTime_ = true;
      if (dt > 0.1f) dt = 0.1f;  // clamp after stalls/load hitches
      advancePlayback(dt);
    }
    if (renderer_ && renderer_->rayTracingActive() != lastRtActiveForSkinning_) {
      updateSkinningEffective();
      if (skinningEffective_ != SkinningMode::GPU && hasAnimation_) {
        requestReconvert(animTime_);
      }
    }
    updateGpuSkinningFrameIfNeeded();
    maybeReconvertForManualBlend();

    // Feed the GUI the current playback state (drawn this frame).
    Gui::TimelineInfo tl;
    tl.hasAnimation = hasAnimation_;
    tl.start = animStart_;
    tl.end = animEnd_;
    tl.fps = animFps_;
    tl.current = animTime_;
    tl.applied = (skinningEffective_ == SkinningMode::GPU &&
                  std::isfinite(skinFrameTime_))
                     ? skinFrameTime_
                     : reconvApplied_;
    tl.playing = animPlaying_;
    tl.converting = reconvActive_;
    gui_.setTimeline(tl);
    Gui::SkinningInfo si;
    si.requested = skinningRequested_;
    si.effective = skinningEffective_;
    si.reason = skinningReason_;
    gui_.setSkinning(si);

    // Feed the GUI the current load status for the loading modal.
    Gui::LoadStatus ls;
    ls.active = loadActive_;
    ls.path = loadingPath_;
    ls.meshesDone = loadCtrl_.meshesDone.load();
    ls.meshesTotal = loadCtrl_.meshesTotal.load();
    ls.elapsed = loadActive_ ? std::chrono::duration<float>(
                                   std::chrono::steady_clock::now() - loadStart_)
                                   .count()
                             : 0.0f;
    gui_.setLoadStatus(ls);

    // GPU-side progress overlay: raster geometry/texture streaming + RT build.
    // Set the RT-build note BEFORE gui_.frame() builds the overlay, so the
    // announce frame (rendered just before the blocking build) carries it.
    rtBuildNote_.clear();
    if (hipInteractive_ && !hipInteractiveBuilt_ && !loadActive_ &&
        !draw_.meshes.empty() && draw_.triangleCount > 0) {
      if (hipBuildStarted_) {
        const int ph = hipBuildProgress_.phase.load(std::memory_order_relaxed);
        const size_t d = hipBuildProgress_.done.load(std::memory_order_relaxed);
        const size_t t = hipBuildProgress_.total.load(std::memory_order_relaxed);
        const float el = std::chrono::duration<float>(
                             std::chrono::steady_clock::now() - hipBuildStart_)
                             .count();
        char buf[192];
        if (t > 0)
          std::snprintf(buf, sizeof(buf),
                        "Building ray-tracing scene \xE2\x80\x94 %s %zu/%zu  (%.0fs)",
                        BuildProgress::phaseName(ph), d, t, el);
        else
          std::snprintf(buf, sizeof(buf),
                        "Building ray-tracing scene \xE2\x80\x94 %s  (%.0fs)",
                        BuildProgress::phaseName(ph), el);
        rtBuildNote_ = buf;
      } else {
        rtBuildNote_ = "Building ray-tracing scene\xE2\x80\xA6";
      }
    }
    Gui::UploadStatus us;
    us.active = progressiveActive_;
    us.meshesDone = nextMesh_;
    us.meshesTotal = draw_.meshes.size();
    us.texDone = nextTex_;
    us.texTotal = draw_.textures.size();
    us.volDone = nextVolume_;
    us.volTotal = draw_.volumes.size();
    us.note = rtBuildNote_;
    gui_.setUploadStatus(us);

    // Apply browser input BEFORE NewFrame so injected mouse/keyboard events reach
    // ImGui this frame (widgets become clickable); camera drags are applied here
    // too. Runs on the main thread, like the MCP drain.
    if (streamServer_) {
      for (const StreamNav& c : streamServer_->takeInput()) applyNavCommand(c);
    }

    // In threaded GL, newFrame() is a GL op that runs on the render thread (just
    // before it draws the packet); the main thread only builds ImGui + the packet.
    if (!renderThreadActive_) renderer_->newFrame();
    if (!headless_) ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Raster view-dependent LOD (--raster-lod): drop sub-pixel instances + box
    // proxies on the raster instanced path. Applied before gui_.frame() (which runs
    // the instance cull). Idempotent; cheap.
    gui_.setRasterLod(rasterLodEnabled_, rasterLodFullPx_, rasterLodCullPx_);

    gui_.frame(renderer_.get(), &camera_);

    // View-dependent RT LOD: re-classify the instance set when the camera settles.
    updateRtLodCamera();

    // Actions generated by the UI. Timeline/skinning actions are applied before
    // rendering the viewport texture so they can affect this present.
    const bool reload = gui_.wantReload();
    const bool open = gui_.wantOpen();
    const bool quit = gui_.wantQuit();
    const bool cancelLoad = gui_.wantCancelLoad();
    const bool loadAllPayloads = gui_.wantLoadAllPayloads();
    std::vector<std::string> payloadReqs = gui_.takePayloadLoadRequests();
    // Timeline actions.
    const bool togglePlay = gui_.wantTogglePlay();
    const bool stopPlay = gui_.wantStop();
    const bool hasSeek = gui_.hasSeek();
    const double seekTime = gui_.seekTime();
    const bool hasSkinningModeRequest = gui_.hasSkinningModeRequest();
    const SkinningMode requestedSkinningMode = gui_.requestedSkinningMode();
    animLoop_ = gui_.loopPlayback();
    animSpeed_ = gui_.playSpeed();
    tessQuality_ = gui_.tessellationQuality();
    gui_.clearActions();

    if (hasSkinningModeRequest) {
      skinningRequested_ = requestedSkinningMode;
      updateSkinningEffective();
      if (skinningEffective_ == SkinningMode::GPU) {
        cancelAndJoinReconvert();
        updateGpuSkinningFrameIfNeeded();
      }
      else if (hasAnimation_) requestReconvert(animTime_);
    }

    // Timeline: play/pause, stop (reset to start), step, and scrub.
    if (hasAnimation_) {
      if (togglePlay) animPlaying_ = !animPlaying_;
      if (stopPlay) {
        animPlaying_ = false;
        animTime_ = animStart_;
        requestReconvert(animTime_);
      }
      const bool stepFwd = gui_.wantStepForward();
      const bool stepBwd = gui_.wantStepBackward();
      if (stepFwd || stepBwd) {
        animPlaying_ = false;
        double frameStep = 1.0 / animFps_;
        if (stepFwd) animTime_ += frameStep;
        if (stepBwd) animTime_ -= frameStep;
        if (animTime_ < animStart_) animTime_ = animStart_;
        if (animTime_ > animEnd_) animTime_ = animEnd_;
        requestReconvert(animTime_);
      }
      if (hasSeek) {
        animTime_ = seekTime;
        if (animTime_ < animStart_) animTime_ = animStart_;
        if (animTime_ > animEnd_) animTime_ = animEnd_;
        requestReconvert(animTime_);
      }
    }
    updateGpuSkinningFrameIfNeeded();

    // Render after same-frame action consumption but before ImGui submit. The
    // viewport window already emitted ImGui::Image with the texture handle; both
    // GL and Vulkan sample the texture contents later during present().
#if defined(TUSDVIEW_ENABLE_GL_THREAD)
    if (renderThreadActive_) {
      // Build the CPU-side frame packet (camera/params + compacted instance data)
      // on the main thread, deep-copy the ImGui draw lists, and hand it to the
      // render thread. The main loop never touches GL or blocks on the GPU.
      auto pkt = std::make_unique<FramePacket>();
      gui_.renderViewportScene(pkt.get());
      glfwGetFramebufferSize(window_, &pkt->fbW, &pkt->fbH);
      pkt->wantCapture =
          (maxFrames >= 0 && frameCount == maxFrames - 1 && !screenshot.empty());
      pkt->seq = ++pktSubmitSeq_;
      ImGui::Render();
      pkt->drawData = CloneImDrawData(ImGui::GetDrawData());
      // Fixed-frame runs block until the render thread has drawn this packet so
      // the screenshot is deterministic; interactive runs never wait.
      submitFramePacket(std::move(pkt), maxFrames >= 0);
    } else
#endif
    {
      // Skip the raster scene draw (and its instance culling) when the RT path
      // owns the screenshot -- only the cheap ImGui composite needs to run.
      // Windowed --hip traces the viewport with the HIP path instead of raster.
      if (hipInteractive_) {
        renderHipViewport();
      } else if (!rtOwnsScreenshot_) {
        gui_.renderViewportScene();
      }

      // Grab the composited window on the final frame (--window-shot).
      if (!windowShot_.empty() && maxFrames >= 0 && frameCount == maxFrames - 1) {
        renderer_->requestWindowCapture();
      }
      // Streaming: send a small low-quality JPEG while the view is moving, then a
      // single full-resolution lossless (PNG/QOI) frame once it goes stable. Skip
      // sending entirely once the refined frame is out and nothing has changed.
      bool streamSend = false, streamMotion = false;
      if (streamServer_) {
        const int cc = streamServer_->clientCount();
        if (cc > 0) {
          if (cc > streamPrevClientCount_) markStreamActivity();  // greet new client
          const auto now = std::chrono::steady_clock::now();
          const long idleMs =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - streamLastActivity_).count();
          streamMotion = progressiveActive_ || animPlaying_ ||
                         idleMs < streamIdleMs_;
          streamSend = streamMotion || !streamHiQSent_;
        }
        streamPrevClientCount_ = cc;
      }
      if (streamSend) renderer_->requestWindowCapture();

      ImGui::Render();
      static const bool timeFrame = std::getenv("TUSDVIEW_TIME_FRAME") != nullptr;
      const auto tp0 = std::chrono::steady_clock::now();
      renderer_->present();

      if (streamSend) {
        std::vector<uint8_t> rgba;
        int cw = 0, ch = 0;
        if (renderer_->captureWindow(&rgba, &cw, &ch) && cw > 0 && ch > 0) {
          streamEncodeAndPush(std::move(rgba), cw, ch, streamMotion);
          if (!streamMotion) streamHiQSent_ = true;  // refined this idle period
        }
      }
      if (timeFrame) {
        const double pms = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - tp0)
                               .count();
        std::fprintf(stderr, "[frame] present(GPU+readback)=%.1fms\n", pms);
      }
    }

    // Deferred actions (after the frame, outside the ImGui frame state).
#if defined(TUSDVIEW_HAVE_MCP)
    if (mcp_) mcp_->drain();  // run queued MCP tool calls on the main thread
#endif
    if (cancelLoad) loadCtrl_.cancel.store(true);
    if (reload && !loaded_.filepath.empty()) startLoadAsync(loaded_.filepath);
    if (open && !headless_) openFileDialog();
    if (gui_.wantOpenRecent() && !headless_) {
      const std::string p = gui_.recentToOpen();
      if (!p.empty()) startLoadAsync(p);
    }

    // Lazy payload on-demand load: recompose with the requested payloads added.
    // Skipped while a load is in flight (loaded_ would be the outgoing scene).
    if (!loadActive_ && (loadAllPayloads || !payloadReqs.empty())) {
      std::set<std::string> add(payloadReqs.begin(), payloadReqs.end());
      if (loadAllPayloads) {
        if (useNextLoader_ && nextSession_) {
          for (const tinyusdz::next::Path& path :
               nextSession_->GetDeferredPayloadPaths()) {
            add.insert(path.str());
          }
        } else {
          for (const auto& d : loaded_.comp.deferred) add.insert(d.primPath);
        }
      }
      startRecomposeAsync(add);
    }

    // Variant switch: recompose with the user's variant selections.
    if (!loadActive_ && gui_.wantVariantSwitch() &&
        ((useNextLoader_ && nextSession_) ||
         (loaded_.comp.composed && loaded_.comp.rootLayer))) {
      loadOpts_.variantOverrides = gui_.variantOverrides();
      startRecomposeAsync(std::set<std::string>());
    }

    if (!headless_) {
      if (quit) glfwSetWindowShouldClose(window_, GLFW_TRUE);
      ImGuiIO& io = ImGui::GetIO();
      if (!io.WantTextInput && glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
      }
    }

    if (maxFrames >= 0 && ++frameCount >= maxFrames) {
      running = false;
      if (!headless_) glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
  }

  // Headless determinism: a manual-blend (or animation) reconvert bakes the
  // deformed geometry on a worker thread; drain it so the screenshot and any
  // ray-traced BLAS (built from draw_ below) see the posed result, not the rest
  // pose. Bounded so a stuck worker can't hang the screenshot.
  if (maxFrames >= 0) {
    for (int guard = 0; guard < 2000; ++guard) {
      finishReconvertIfReady();
      maybeReconvertForManualBlend();
      if (!reconvActive_ && !blendReconvNeeded_) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  // Headless: report the last frame's frustum-cull stats (visible vs total) so
  // large-scene culling can be measured without the interactive HUD.
  if (maxFrames >= 0) {
    const Gui::RenderStats rs = gui_.renderStats();
    LOGI("render stats: meshes %zu/%zu visible, instances %zu/%zu visible, "
         "drawn tris %zu, draw calls %zu",
         rs.visibleMeshes, rs.totalMeshes, rs.visibleInstances,
         rs.totalInstances, rs.drawnTriangles, rs.drawCalls);
  }

  auto shot = [&](const std::string& path, bool window) {
    if (path.empty()) return;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    bool ok;
#if defined(TUSDVIEW_ENABLE_GL_THREAD)
    if (renderThreadActive_ && !window) {
      // The render thread owns the GL context, so it grabbed the viewport into
      // renderCapture_ when the packet's wantCapture was set (above). Use it
      // instead of calling captureViewport from this (context-less) thread.
      rgba = renderCapture_;
      w = renderCaptureW_;
      h = renderCaptureH_;
      ok = !rgba.empty();
    } else
#endif
    {
      ok = window ? renderer_->captureWindow(&rgba, &w, &h)
                  : renderer_->captureViewport(&rgba, &w, &h);
    }
    if (ok && w > 0 && h > 0) {
      std::string err;
      if (WriteScreenshotImage(path, rgba, w, h, &err)) {
        LOGI("wrote %s (%dx%d)", path.c_str(), w, h);
      } else {
        LOGW("failed to write %s: %s", path.c_str(), err.c_str());
      }
    } else {
      LOGW("capture not supported by this backend");
    }
  };
  // CUDA ray tracing: trace the loaded scene on the GPU (CUDA driver API + NVRTC
  // loaded at runtime via cuew) and write it in place of the rasterized capture.
  if (cudaRt_ && !screenshot.empty() && !draw_.empty()) {
    std::string cerr;
    // The tracer builds from draw_ geometry, which the next loader hands over in
    // its REST pose (the deform lives in the GPU skin/morph channels). Pose it.
    poseNextDrawForTracer(animTime_);
    if (!cudaTracer_.init(&cerr)) {
      LOGW("CUDA ray tracing unavailable: %s", cerr.c_str());
    } else if (!cudaTracer_.build(draw_, cudaMaxTris_, rtMaxInstances_, &cerr,
                                  gui_.displacementScale())) {
      LOGW("CUDA ray tracing build failed: %s", cerr.c_str());
    } else {
      // Use the requested window size for the screenshot. The viewport probe is
      // unreliable on the RT screenshot path: rtOwnsScreenshot_ skips the raster
      // renderViewportScene(), so resizeViewport() never runs and captureViewport()
      // would report the tiny default offscreen size (e.g. 64x20).
      int w = 0, h = 0;
      getRequestedWindowSize(&w, &h);
      if (w <= 0 || h <= 0) {
        std::vector<uint8_t> sizeProbe;
        renderer_->captureViewport(&sizeProbe, &w, &h);
      }
      if (w <= 0 || h <= 0) { w = 1024; h = 768; }
      camera_.setAspect(static_cast<float>(w) / static_cast<float>(h));
      const light3d::Mat4 pv = camera_.proj(/*zeroToOneDepth=*/true) * camera_.view();
      const light3d::Mat4 inv = pv.inverse();
      const light3d::Vec3 eye = camera_.eye();
      const float camPos[3] = {eye.x, eye.y, eye.z};
      float lightDir[3];
      CopyPreviewLightDir(draw_, lightDir);
      const float clear[3] = {0.12f, 0.12f, 0.13f};
      const int rmode = static_cast<int>(gui_.renderMode());
      float depthScale = 1.0f;
      float sceneMin[3] = {0, 0, 0}, sceneExtent[3] = {1, 1, 1};
      if (draw_.hasBounds) {
        const float dx = draw_.aabbMax[0] - draw_.aabbMin[0];
        const float dy = draw_.aabbMax[1] - draw_.aabbMin[1];
        const float dz = draw_.aabbMax[2] - draw_.aabbMin[2];
        depthScale = std::max(1e-3f, std::sqrt(dx * dx + dy * dy + dz * dz));
        for (int i = 0; i < 3; ++i) {
          sceneMin[i] = draw_.aabbMin[i];
          sceneExtent[i] = std::max(1e-4f, draw_.aabbMax[i] - draw_.aabbMin[i]);
        }
      }
      std::vector<uint8_t> rgba;
      if (cudaTracer_.trace(inv.m, pv.m, camPos, lightDir, clear, rmode, depthScale, sceneMin,
                            sceneExtent, w, h, &rgba, &cerr, rtSamples_)) {
        std::string werr;
        if (WriteScreenshotImage(screenshot, rgba, w, h, &werr)) {
          LOGI("CUDA RT wrote %s (%dx%d, %zu tris%s, %s)", screenshot.c_str(), w, h,
               cudaTracer_.triangleCount(),
               cudaTracer_.truncated() ? ", truncated" : "", cudaTracer_.deviceName());
        } else {
          LOGW("CUDA RT screenshot write failed: %s", werr.c_str());
        }
      } else {
        LOGW("CUDA ray trace failed: %s", cerr.c_str());
      }
    }
    return 0;  // CUDA path owns the screenshot; skip the rasterized capture.
  }

  // HIP/ROCm ray tracing: AMD counterpart of the CUDA path above (HIP runtime +
  // hiprtc loaded at runtime via hipew). Same scene flatten / BVH / kernel.
  if (hipRt_ && !screenshot.empty() && !draw_.empty()) {
    std::string cerr;
    poseNextDrawForTracer(animTime_);  // as CUDA above
    if (!hipTracer_.init(&cerr)) {
      LOGW("HIP ray tracing unavailable: %s", cerr.c_str());
    } else if (!hipTracer_.build(draw_, cudaMaxTris_, rtMaxInstances_, &cerr,
                                 gui_.displacementScale())) {
      LOGW("HIP ray tracing build failed: %s", cerr.c_str());
    } else {
      // Use the requested window size for the screenshot. The viewport probe is
      // unreliable on the RT screenshot path: rtOwnsScreenshot_ skips the raster
      // renderViewportScene(), so resizeViewport() never runs and captureViewport()
      // would report the tiny default offscreen size (e.g. 64x20).
      int w = 0, h = 0;
      getRequestedWindowSize(&w, &h);
      if (w <= 0 || h <= 0) {
        std::vector<uint8_t> sizeProbe;
        renderer_->captureViewport(&sizeProbe, &w, &h);
      }
      if (w <= 0 || h <= 0) { w = 1024; h = 768; }
      camera_.setAspect(static_cast<float>(w) / static_cast<float>(h));
      const light3d::Mat4 pv = camera_.proj(/*zeroToOneDepth=*/true) * camera_.view();
      const light3d::Mat4 inv = pv.inverse();
      const light3d::Vec3 eye = camera_.eye();
      const float camPos[3] = {eye.x, eye.y, eye.z};
      float lightDir[3];
      CopyPreviewLightDir(draw_, lightDir);
      const float clear[3] = {0.12f, 0.12f, 0.13f};
      const int rmode = static_cast<int>(gui_.renderMode());
      float depthScale = 1.0f;
      float sceneMin[3] = {0, 0, 0}, sceneExtent[3] = {1, 1, 1};
      if (draw_.hasBounds) {
        const float dx = draw_.aabbMax[0] - draw_.aabbMin[0];
        const float dy = draw_.aabbMax[1] - draw_.aabbMin[1];
        const float dz = draw_.aabbMax[2] - draw_.aabbMin[2];
        depthScale = std::max(1e-3f, std::sqrt(dx * dx + dy * dy + dz * dz));
        for (int i = 0; i < 3; ++i) {
          sceneMin[i] = draw_.aabbMin[i];
          sceneExtent[i] = std::max(1e-4f, draw_.aabbMax[i] - draw_.aabbMin[i]);
        }
      }
      std::vector<uint8_t> rgba;
      if (hipTracer_.trace(inv.m, pv.m, camPos, lightDir, clear, rmode, depthScale, sceneMin,
                           sceneExtent, w, h, &rgba, &cerr, rtSamples_)) {
        std::string werr;
        if (WriteScreenshotImage(screenshot, rgba, w, h, &werr)) {
          LOGI("HIP RT wrote %s (%dx%d, %zu tris%s, %s)", screenshot.c_str(), w, h,
               hipTracer_.triangleCount(),
               hipTracer_.truncated() ? ", truncated" : "", hipTracer_.deviceName());
        } else {
          LOGW("HIP RT screenshot write failed: %s", werr.c_str());
        }
      } else {
        LOGW("HIP ray trace failed: %s", cerr.c_str());
      }
    }
    return 0;  // HIP path owns the screenshot; skip the rasterized capture.
  }

  shot(screenshot, /*window=*/false);
  shot(windowShot_, /*window=*/true);
  return 0;
}

}  // namespace tusdview
