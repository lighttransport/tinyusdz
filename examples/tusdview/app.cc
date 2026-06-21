// SPDX-License-Identifier: Apache-2.0
#include "app.hh"

#include <glad/glad.h>
//
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include "cascadia_mono.h"  // CascadiaMono_compressed_data / _size
#include "gui_style.hh"
#include "image-writer.hh"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "log.hh"
#include "mesh_build.hh"
#include "next_scene_loader.hh"
#include "skinning.hh"

#if defined(HAVE_NFD)
#include "nfd.h"
#endif

namespace tusdview {

namespace {
void GlfwErrorCallback(int code, const char* desc) {
  LOGE("glfw error %d: %s", code, desc);
}

constexpr int kBaseWindowWidth = 1280;
constexpr int kBaseWindowHeight = 800;
constexpr int kMaxGpuTextureInfluences = 256;

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

bool WriteScreenshotImage(const std::string& path,
                          const std::vector<uint8_t>& rgba, int w, int h,
                          std::string* err) {
  if (w <= 0 || h <= 0 || rgba.size() < static_cast<size_t>(w) *
                                      static_cast<size_t>(h) * 4) {
    if (err) *err = "invalid screenshot buffer";
    return false;
  }

  if (LowerExtension(path) == "png") {
    tinyusdz::Image img;
    img.uri = path;
    img.width = w;
    img.height = h;
    img.channels = 4;
    img.bpp = 8;
    img.format = tinyusdz::Image::PixelFormat::UInt;
    img.data = rgba;

    tinyusdz::image::WriteOption opt;
    opt.format = tinyusdz::image::WriteImageFormat::PNG;
    auto ret = tinyusdz::image::WriteImageToFile(path, img, opt);
    if (!ret) {
      if (err) *err = ret.error();
      return false;
    }
    return true;
  }

  WritePPM(path, rgba, w, h);
  return true;
}
}  // namespace

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
  if (renderer_) renderer_->shutdown();
  if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
  if (window_) glfwDestroyWindow(window_);
  glfwTerminate();
}

bool App::initWindow(std::string* err) {
  glfwSetErrorCallback(GlfwErrorCallback);
  if (!glfwInit()) {
    *err = "glfwInit failed";
    return false;
  }

  if (backend_ == Backend::GL) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
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
  io.IniFilename = nullptr;  // don't litter imgui.ini next to the binary

  // HiDPI: load Cascadia Mono at a scaled pixel size and scale widget metrics
  // so the UI is readable on 4K panels. Baking the scale into the font size
  // keeps the text crisp (vs. io.FontGlobalScale which just magnifies).
  io.Fonts->AddFontFromMemoryCompressedTTF(CascadiaMono_compressed_data,
                                           CascadiaMono_compressed_size, fontSizePx_);

  // Maya-like dark theme, then scale rounding/padding/spacing for HiDPI.
  StyleMaya();
  ImGui::GetStyle().ScaleAllSizes(uiScale_);

  return renderer_->initImGui(err);
}

// Release a mesh's CPU geometry after it's been uploaded to the GPU. Used only
// for the static --next preview, where the CPU copy is dead weight (no
// animation re-pose, GPU skinning, or meaningful per-mesh pick on batched
// geometry). Halves resident RAM for large scenes. Keeps small metadata
// (name/purpose/world/aabb/submeshes) the GUI's visibility mask still reads.
static void FreeMeshGeometryCPU(DrawMeshCPU& m) {
  std::vector<DrawVertex>().swap(m.vertices);
  std::vector<uint32_t>().swap(m.indices);
  std::vector<float>().swap(m.vertexColors);
  std::vector<float>().swap(m.instanceXforms);
  std::vector<float>().swap(m.instanceColors);
}

void App::applyLoaded(bool ok, bool progressive) {
  progressiveActive_ = false;
  nextMesh_ = 0;
  nextTex_ = 0;
  gpuMorphedMeshes_.clear();  // renderer re-uploads rest meshes for this scene

  if (ok) {
    ++sceneGen_;  // invalidate the MCP library-tool Stage snapshot
    readAnimationRange();  // start/end/fps; resets playback to paused at start
    if (std::isfinite(loadOpts_.timecode)) {
      animTime_ = loadOpts_.timecode;
      reconvApplied_ = animTime_;
    }
    updateSkinningEffective();
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

  if (ok && progressive) {
    // Reserve materials + texture slots now; stream meshes then textures over
    // the next frames (stepProgressiveUpload) so geometry pops in and the UI
    // stays at frame rate instead of stalling on one big upload.
    renderer_->beginScene(draw_.materials, static_cast<int>(draw_.textures.size()));
    progressiveActive_ = true;
    LOGI("loaded %s: %zu mesh(es), %zu tri(s)%s; streaming to GPU...",
         loaded_.filepath.c_str(), draw_.meshes.size(), draw_.triangleCount,
         draw_.truncated ? " [truncated]" : "");
  } else {
    // Synchronous full upload (headless / failure). draw_ is empty when !ok.
    std::string uerr;
    renderer_->uploadScene(draw_, &uerr);
    if (ok && useNextLoader_ && !cudaRt_) {  // CUDA RT needs the CPU geometry later
      for (DrawMeshCPU& m : draw_.meshes) FreeMeshGeometryCPU(m);
    }
    if (ok) {
      LOGI("loaded %s: %zu mesh(es), %zu tri(s)%s", loaded_.filepath.c_str(),
           draw_.meshes.size(), draw_.triangleCount,
           draw_.truncated ? " [truncated: render budget]" : "");
    } else {
      LOGE("load failed: %s", loaded_.err.c_str());
    }
  }
  // Pose the GPU frame now that the renderer holds the meshes (the per-mesh
  // morph vertex upload needs them present; the bone texture is global). For the
  // progressive path meshes stream in over later frames — the main loop re-poses
  // once streaming completes (skinFrameTime_ stays NaN until then).
  if (ok && skinningEffective_ == SkinningMode::GPU && !progressiveActive_) {
    updateGpuSkinningFrameIfNeeded();
  }
  // Frame the camera AFTER the GPU pose updates draw_ bounds (so an animated
  // load, e.g. --time, frames the posed geometry, matching the CPU bake path).
  if (ok && draw_.hasBounds) {
    const float dx = draw_.aabbMax[0] - draw_.aabbMin[0];
    const float dy = draw_.aabbMax[1] - draw_.aabbMin[1];
    const float dz = draw_.aabbMax[2] - draw_.aabbMin[2];
    camera_.setSceneRadius(0.5f * std::sqrt(dx * dx + dy * dy + dz * dz));
    camera_.fitToScene(draw_.aabbMin, draw_.aabbMax);
  }
  gui_.setScene(&loaded_, &draw_);
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
    if (useNextLoader_ && !cudaRt_) FreeMeshGeometryCPU(draw_.meshes[nextMesh_]);
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
  if (nextMesh_ >= draw_.meshes.size() && nextTex_ >= draw_.textures.size()) {
    progressiveActive_ = false;
  }
}

void App::loadFileBlocking(const std::string& path) {
  loadCtrl_.resetProgress();
  LoadedScene tmp;
  DrawScene drawTmp;
  LoadOptions opts = loadOpts_;
  if (useNextLoader_) {
    // `next` flat-preview path: builds only the DrawScene (no Tydra
    // RenderScene/skinning); the hierarchy browser/inspector stay empty.
    const bool ok =
        LoadUSDViaNext(path, opts, &drawTmp, &tmp.warn, &tmp.err, &loadCtrl_);
    tmp.ok = ok;
    tmp.filepath = path;
    tmp.render.meta.upAxis = drawTmp.upAxis;  // drive camera/grid up-axis
    loaded_ = std::move(tmp);
    draw_ = ok ? std::move(drawTmp) : DrawScene{};
    applyLoaded(ok, /*progressive=*/false);
    return;
  }
  const bool gpuRestLoad = std::isfinite(loadOpts_.timecode) &&
                           skinningRequested_ == SkinningMode::GPU;
  if (gpuRestLoad) opts.timecode = std::numeric_limits<double>::quiet_NaN();
  // Streaming convert+build in one pass (also fully populates tmp.render).
  bool ok = LoadUSD(path, opts, &tmp, &drawTmp, rtPath_, &loadCtrl_);
  if (ok && gpuRestLoad) {
    const bool skeletal = SceneHasSkeletalSkinning(tmp.render);
    const bool morph = SceneHasBlendShapes(tmp.render);
    const int maxInfluences = MaxSkinInfluenceCount(tmp.render);
    const bool gpuEligible =
        renderer_ && renderer_->caps().supportsGpuSkinning &&
        !renderer_->rayTracingActive() && (skeletal || morph) &&
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
  if (std::isfinite(opts.timecode) && skinningRequested_ == SkinningMode::GPU) {
    opts.timecode = std::numeric_limits<double>::quiet_NaN();
  }
  const bool useNext = useNextLoader_;
  loadThread_ = std::thread([this, path, opts, lp, dp, rt, useNext]() {
    if (useNext) {
      lp->ok = LoadUSDViaNext(path, opts, dp, &lp->warn, &lp->err, &loadCtrl_);
      lp->filepath = path;
      lp->render.meta.upAxis = dp->upAxis;  // drive camera/grid up-axis
    } else {
      LoadUSD(path, opts, lp, dp, rt, &loadCtrl_);
    }
    loadFinished_.store(true, std::memory_order_release);
  });
}

void App::startRecomposeAsync(const std::set<std::string>& addPrimPaths) {
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
  if (renderer_->rayTracingActive()) {
    skinningReason_ = "ray tracing uses CPU-skinned BLAS geometry";
    return;
  }
  const bool skeletal = SceneHasSkeletalSkinning(loaded_.render);
  const bool morph = SceneHasBlendShapes(loaded_.render);
  if (!loaded_.ok || (!skeletal && !morph)) {
    skinningReason_ = "scene has no skeletal skinning or blendshapes";
    return;
  }
  const int maxInfluences = MaxSkinInfluenceCount(loaded_.render);
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
  skinningReason_ = skeletal && morph ? "GPU skeletal + blendshape skinning"
                    : morph           ? "GPU blendshape morph"
                                      : "GPU skeletal skinning";
}

void App::updateGpuSkinningFrameIfNeeded() {
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

  std::vector<std::pair<int, std::vector<DrawVertex>>> morphed;
  if (BuildGpuSkinningFrame(loaded_.render, loaded_.stage, &draw_, animTime_,
                            &skinFrame_, gui_.showSkeletonOverlay(),
                            (hasMorph && idxOk) ? &morphed : nullptr,
                            gui_.blendOverrides())) {
    skinFrameTime_ = animTime_;
    renderer_->uploadSkinningFrame(skinFrame_);
    // Upload posed buffers for actively-morphed meshes; revert any mesh that was
    // morphed last frame but no longer is back to its rest vertices (once).
    std::set<int> active;
    for (auto& mv : morphed) {
      renderer_->updateMeshVertices(mv.first, mv.second);
      active.insert(mv.first);
    }
    for (int i : gpuMorphedMeshes_) {
      if (!active.count(i) && i >= 0 &&
          i < static_cast<int>(draw_.meshes.size())) {
        renderer_->updateMeshVertices(i, draw_.meshes[i].vertices);
      }
    }
    gpuMorphedMeshes_ = std::move(active);
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
  // Worker reads loaded_ (stage/mmap/filepath) read-only; the main thread keeps
  // loaded_ alive and joins this worker (cancelAndJoinReconvert) before any
  // reload. RenderSceneAtTime skips texture decode and fills only dp->meshes.
  reconvThread_ = std::thread([this, t, dp, rt]() {
    std::string w, e;
    const bool ok = RenderSceneAtTime(loaded_, t, rt, dp, &w, &e, &reconvCtrl_);
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

int App::run(const std::string& initialFile, int maxFrames,
             const std::string& screenshot) {
  std::string err;
  // Headless composite size (no monitor to clamp to); used for the windowless
  // ImGui DisplaySize and the offscreen composite image.
  int winW = 0;
  int winH = 0;
  getRequestedWindowSize(&winW, &winH);
  if (headless_) {
    if (backend_ != Backend::Vulkan) {
      LOGE("--headless requires the Vulkan backend (pass --backend vk)");
      return 1;
    }
    if (maxFrames < 0) maxFrames = 4;  // windowless runs are bounded by frame count
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
  if (headless_) renderer_->setHeadlessSize(winW, winH);
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

  gui_.setScene(&loaded_, &draw_);
  gui_.setBudget(&loadCtrl_);

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

    renderer_->newFrame();
    if (!headless_) ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    gui_.frame(renderer_.get(), &camera_);

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
    gui_.renderViewportScene();

    // Grab the composited window on the final frame (--window-shot).
    if (!windowShot_.empty() && maxFrames >= 0 && frameCount == maxFrames - 1) {
      renderer_->requestWindowCapture();
    }

    ImGui::Render();
    renderer_->present();

    // Deferred actions (after the frame, outside the ImGui frame state).
#if defined(TUSDVIEW_HAVE_MCP)
    if (mcp_) mcp_->drain();  // run queued MCP tool calls on the main thread
#endif
    if (cancelLoad) loadCtrl_.cancel.store(true);
    if (reload && !loaded_.filepath.empty()) startLoadAsync(loaded_.filepath);
    if (open && !headless_) openFileDialog();

    // Lazy payload on-demand load: recompose with the requested payloads added.
    // Skipped while a load is in flight (loaded_ would be the outgoing scene).
    if (!loadActive_ && (loadAllPayloads || !payloadReqs.empty())) {
      std::set<std::string> add(payloadReqs.begin(), payloadReqs.end());
      if (loadAllPayloads) {
        for (const auto& d : loaded_.comp.deferred) add.insert(d.primPath);
      }
      startRecomposeAsync(add);
    }

    // Variant switch: recompose with the user's variant selections.
    if (!loadActive_ && gui_.wantVariantSwitch() && loaded_.comp.composed &&
        loaded_.comp.rootLayer) {
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

  auto shot = [&](const std::string& path, bool window) {
    if (path.empty()) return;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    const bool ok = window ? renderer_->captureWindow(&rgba, &w, &h)
                           : renderer_->captureViewport(&rgba, &w, &h);
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
    if (!cudaTracer_.init(&cerr)) {
      LOGW("CUDA ray tracing unavailable: %s", cerr.c_str());
    } else if (!cudaTracer_.build(draw_, cudaMaxTris_, &cerr)) {
      LOGW("CUDA ray tracing build failed: %s", cerr.c_str());
    } else {
      std::vector<uint8_t> sizeProbe;
      int w = 0, h = 0;
      renderer_->captureViewport(&sizeProbe, &w, &h);
      if (w <= 0 || h <= 0) { w = 1024; h = 768; }
      camera_.setAspect(static_cast<float>(w) / static_cast<float>(h));
      const light3d::Mat4 pv = camera_.proj(/*zeroToOneDepth=*/true) * camera_.view();
      const light3d::Mat4 inv = pv.inverse();
      const light3d::Vec3 eye = camera_.eye();
      const float camPos[3] = {eye.x, eye.y, eye.z};
      const float lightDir[3] = {0.5f, 0.8f, 0.6f};  // same fixed light as the RT/raster path
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
      if (cudaTracer_.trace(inv.m, camPos, lightDir, clear, rmode, depthScale, sceneMin,
                            sceneExtent, w, h, &rgba, &cerr)) {
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

  shot(screenshot, /*window=*/false);
  shot(windowShot_, /*window=*/true);
  return 0;
}

}  // namespace tusdview
