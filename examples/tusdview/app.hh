// SPDX-License-Identifier: Apache-2.0
// tusdview - application: owns the window, renderer, scene and GUI; runs the
// main loop.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>

#include "frame_packet.hh"

#include "camera_nav.hh"
#include "cuda/cuda_raytracer.hh"
#include "gpu_scene.hh"
#include "gui.hh"
#include "load_control.hh"
#include "parametric_tess.hh"
#include "renderer.hh"
#include "scene_loader.hh"
#if defined(TUSDVIEW_HAVE_MCP)
#include "mcp/mcp_host.hh"
#include "mcp/mcp_server.hh"
#include "tydra/js-script.hh"    // complete JSEngineState (held in Context)
#include "tydra/mcp-context.hh"  // tinyusdz::tydra::mcp::Context (library tool bridge)
#endif

struct GLFWwindow;

namespace tusdview {

class App
#if defined(TUSDVIEW_HAVE_MCP)
    : public McpHost
#endif
{
 public:
  explicit App(Backend backend) : backend_(backend) {}
  ~App();

  // Optional render budget (for scripting/testing). maxTris==0 keeps the default.
  void setLoadBudget(std::size_t maxTris, double convertTimeBudgetSec) {
    if (maxTris) loadCtrl_.maxTriangles = maxTris;
    loadCtrl_.convertTimeBudgetSec = convertTimeBudgetSec;
  }

  // HiDPI UI scale (font + widget sizes). Default 2.0 for 4K panels.
  void setUiScale(float s) {
    if (s > 0.25f) {
      uiScale_ = s;
      fontSizePx_ = 16.0f * s;
      windowScale_ = s;
    }
  }
  void setFontSize(float px) {
    if (px > 4.0f) {
      fontSizePx_ = px;
      uiScale_ = px / 16.0f;
    }
  }
  void setWindowScale(float s) {
    if (s > 0.25f) windowScale_ = s;
  }
  void setWindowSize(int width, int height) {
    if (width > 0 && height > 0) {
      hasWindowSizeOverride_ = true;
      windowWidth_ = width;
      windowHeight_ = height;
    }
  }
  void clearWindowSizeOverride() { hasWindowSizeOverride_ = false; }
  void setOrbitSensitivity(float s) { camera_.setOrbitSensitivity(s); }
  void setPanSensitivity(float s) { camera_.setPanSensitivity(s); }
  void setDollySensitivity(float s) { camera_.setDollySensitivity(s); }
  void setInvertDolly(bool on) { camera_.setInvertDolly(on); }

  // USD composition behavior for subsequent loads (set from CLI/config before
  // run(); payload whitelist is managed internally by recompose).
  void setLoadOptions(const LoadOptions& o) { loadOpts_ = o; }
  const LoadOptions& loadOptions() const { return loadOpts_; }

  // Use the `next` lazy loader + tydra-next converter (flat-shaded large-scene
  // mesh preview) instead of the default Tydra path. See next_scene_loader.cc.
  void setUseNextLoader(bool on) { useNextLoader_ = on; }
  void setCullEnabled(bool on) { gui_.setCullEnabled(on); }
  void setCamDolly(float f) { camDolly_ = f; }
#if defined(TUSDVIEW_ENABLE_GL_THREAD)
  // --threaded: run GL rendering on a dedicated thread so the UI loop never blocks
  // on GPU work (experimental; default off). No-op unless built with the option.
  void setThreaded(bool on) { threaded_ = on; }
#else
  void setThreaded(bool /*on*/) {}
#endif

  // Request the Vulkan ray-tracing technique at startup (honored only when the
  // device supports it; otherwise the viewer stays on rasterization).
  void setRequestRayTracing(bool on) { rtRequested_ = on; }
  void setAllowBackendFallback(bool on) { allowBackendFallback_ = on; }
  void setSkinningMode(SkinningMode mode) { skinningRequested_ = mode; }

  // Write a PPM of the full composited window after the last frame (QA).
  void setWindowShot(const std::string& path) { windowShot_ = path; }

  // Windowless rendering: no GLFW window, surface or swapchain (Vulkan only).
  // Renders frames offscreen for --screenshot / --window-shot. Requires --frames.
  void setHeadless(bool on) { headless_ = on; }
  // --cuda: trace the screenshot with the CUDA BVH ray tracer (cuew runtime).
  void setCudaRt(bool on) { cudaRt_ = on; }
  // Initial render mode (e.g. --wireframe); applies to raster + both RT backends.
  void setRenderMode(RenderMode m) { gui_.setRenderMode(m); }
  void setBlendWeight(const std::string& name, float w) {
    gui_.setBlendWeight(name, w);
  }
  void setInitialSelection(const std::string& primPath) {
    initialSelect_ = primPath;
  }

  // Embedded MCP server transports (no-op unless built with TUSDVIEW_ENABLE_MCP).
  void setMcpStdio(bool on) { mcpStdio_ = on; }
  void setMcpHttp(int port) { mcpHttpPort_ = port; }  // 0 = off

#if defined(TUSDVIEW_HAVE_MCP)
  // McpHost tool handlers (defined in mcp/app_mcp.cc; run on the main thread).
  nlohmann::json mcpLoadUsd(const nlohmann::json& a, std::string& e) override;
  nlohmann::json mcpSceneInfo(const nlohmann::json& a, std::string& e) override;
  nlohmann::json mcpGetFocusedPrim(const nlohmann::json& a, std::string& e) override;
  nlohmann::json mcpSetFocus(const nlohmann::json& a, std::string& e) override;
  nlohmann::json mcpViewport(const nlohmann::json& a, std::string& e) override;
  nlohmann::json mcpListPrims(const nlohmann::json& a, std::string& e) override;
  nlohmann::json mcpLoadPayloads(const nlohmann::json& a, std::string& e) override;
  nlohmann::json mcpTimeline(const nlohmann::json& a, std::string& e) override;
  nlohmann::json mcpSkinning(const nlohmann::json& a, std::string& e) override;
  nlohmann::json mcpCallLibraryTool(const std::string& name, const nlohmann::json& a,
                                    std::string& e) override;
#endif

  // Initialize window + renderer + ImGui, optionally load `initialFile`, then
  // run the main loop. `maxFrames >= 0` renders that many frames then exits
  // cleanly (useful for headless smoke tests). If `screenshot` is non-empty the
  // 3D viewport is written there as a PPM after the last frame (GL backend).
  // Returns process exit code.
  int run(const std::string& initialFile, int maxFrames = -1,
          const std::string& screenshot = "");

 private:
  bool initWindow(std::string* err);
  bool initImGui(std::string* err);
  void getRequestedWindowSize(int* width, int* height) const;
  void openFileDialog();

  // Synchronous load (used for headless --frames runs so screenshots are
  // deterministic) and the async path (keeps the UI responsive).
  void loadFileBlocking(const std::string& path);
  void startLoadAsync(const std::string& path);
  // Recompose the current scene with additional payloads loaded (lazy payload
  // on-demand load). `addPrimPaths` are deferred-payload prim paths to load on
  // top of those already loaded. No-op if the scene wasn't composed.
  void startRecomposeAsync(const std::set<std::string>& addPrimPaths);
  void finishLoadIfReady();
  void applyLoaded(bool ok, bool progressive);  // upload + bind on the main thread
  void stepProgressiveUpload();  // stream meshes then textures, budgeted per frame
  void cancelAndJoinLoad();

  // --- Animation playback ---
  // Read the time range (start/end/fps) from the freshly loaded scene; resets
  // playback to a paused state at the start time.
  void readAnimationRange();
  void updateSkinningEffective();
  void updateGpuSkinningFrameIfNeeded();
  // Non-GPU (ray-traced / CPU-skinned) path: when manual blendshape weights
  // change, re-bake the deformed geometry + BLAS via an async reconvert.
  void maybeReconvertForManualBlend();
  bool wantsGpuSkinningLoad() const;
  const char* skinningModeName(SkinningMode mode) const;
  // Advance the playback clock by `dtSec` and request a re-evaluation at the new
  // time (called once per frame while playing).
  void advancePlayback(float dtSec);
  // Coalesced request to re-evaluate geometry at time code `t`: starts a worker
  // if idle, else records the latest wanted time (applied when the current one
  // finishes). No-op while a file load is streaming.
  void requestReconvert(double t);
  void startReconvertAsync(double t);
  void finishReconvertIfReady();   // swap in completed geometry, preserve camera
  void cancelAndJoinReconvert();   // stop a running reconvert worker
  void registerParametricPrims();  // scan stage for parametric prims for adaptive tess

#if defined(TUSDVIEW_ENABLE_GL_THREAD)
  // --- Experimental threaded GL rendering ---
  // The render thread owns the GL context and runs every GPU op: it drains the
  // GPU-op queue (uploads), then renders the latest frame packet (scene draw +
  // ImGui composite + swap). The main thread only does events + ImGui UI build,
  // posting GPU ops + frame packets and never blocking on the GPU.
  bool startRenderThread();
  void joinRenderThread();
  void renderThreadMain();
  void postGpu(std::function<void()> op);  // queued when threaded, else inline
  void drainGpuOps();
  void submitFramePacket(std::unique_ptr<FramePacket> pkt, bool blockUntilDone);
#else
  void postGpu(std::function<void()> op) { op(); }  // inline (single-threaded build)
#endif

  Backend backend_;
  bool allowBackendFallback_{false};
  GLFWwindow* window_{nullptr};
  std::unique_ptr<Renderer> renderer_;

  LoadedScene loaded_;
  DrawScene draw_;
  LoadOptions loadOpts_;
  bool useNextLoader_{false};  // --next: next loader + tydra-next flat preview
  float camDolly_{1.0f};       // --cam-dolly: fitted-distance scale (<1 zooms in)
  OrbitCamera camera_;
  Gui gui_;
  AdaptiveTessellator tess_;      // adaptive re-tessellation for parametric prims
  float tessQuality_{1.0f};      // tessellation quality multiplier

  float uiScale_{2.0f};      // widget scale (defaults to font px / 16)
  float fontSizePx_{32.0f};  // font size in pixels
  float windowScale_{2.0f};  // default window size multiplier
  bool hasWindowSizeOverride_{false};
  int windowWidth_{0};
  int windowHeight_{0};
  std::string windowShot_;
  bool headless_{false};  // windowless offscreen rendering (Vulkan only)
  bool cudaRt_{false};    // --cuda: CUDA BVH ray-traced screenshot (cuew runtime)
  CudaRayTracer cudaTracer_;
  size_t cudaMaxTris_{32000000};  // flattened-triangle cap (instances expanded)

  // Ray tracing: requested via --rt; rtPath_ is the effective state after the
  // renderer reports capability (drives the RT-friendly conversion config).
  bool rtRequested_{false};
  bool rtPath_{false};

  SkinningMode skinningRequested_{SkinningMode::Auto};
  SkinningMode skinningEffective_{SkinningMode::CPU};
  std::string skinningReason_{"CPU path"};
  SkinningFrameCPU skinFrame_;
  double skinFrameTime_{std::numeric_limits<double>::quiet_NaN()};
  bool lastRtActiveForSkinning_{false};
  bool warnedMeshIndexMismatch_{false};

  // Async loading
  std::thread loadThread_;
  LoadControl loadCtrl_;
  std::atomic<bool> loadFinished_{false};
  bool loadActive_{false};  // main-thread-only UI flag
  std::unique_ptr<LoadedScene> pendingLoaded_;
  std::unique_ptr<DrawScene> pendingDraw_;
  std::string loadingPath_;
  std::chrono::steady_clock::time_point loadStart_;

  // Animation playback (main-thread owned unless noted).
  bool hasAnimation_{false};
  bool animPlaying_{false};
  bool animLoop_{true};
  float animSpeed_{1.0f};
  double animStart_{0.0};
  double animEnd_{0.0};
  double animFps_{24.0};
  double animTime_{0.0};         // current time code being shown
  std::chrono::steady_clock::time_point lastFrameTime_;
  bool haveLastFrameTime_{false};

  // Coalesced async re-evaluation of geometry at a time code (playback/scrub).
  std::thread reconvThread_;
  LoadControl reconvCtrl_;
  std::atomic<bool> reconvFinished_{false};
  bool reconvActive_{false};       // a reconvert worker is running
  double reconvInFlight_{0.0};     // time code the running worker computes
  double reconvRequested_{0.0};    // latest requested time code
  bool reconvHasRequest_{false};   // a (re)convert is wanted
  bool blendReconvNeeded_{false};  // manual blend weights changed (non-GPU path)
  std::string initialSelect_;      // --select: prim to select once loaded
  double reconvApplied_{0.0};      // time code currently shown
  std::unique_ptr<DrawScene> reconvDraw_;
  std::atomic<bool> reconvOk_{false};

  // Progressive GPU upload (interactive path): stream meshes then textures.
  bool progressiveActive_{false};
  size_t nextMesh_{0};
  size_t nextTex_{0};
  size_t nextVolume_{0};  // UsdVol volumes uploaded so far

#if defined(TUSDVIEW_ENABLE_GL_THREAD)
  // Experimental threaded rendering. renderThreadActive_ is true only when
  // threaded_ AND windowed GL; headless/non-threaded keep the inline path.
  bool threaded_{false};
  bool renderThreadActive_{false};
  std::thread renderThread_;
  std::atomic<bool> renderRunning_{false};
  std::atomic<bool> renderInitOk_{false};
  std::atomic<bool> renderInitDone_{false};
  std::mutex gpuOpMutex_;
  std::queue<std::function<void()>> gpuOps_;
  std::mutex pktMutex_;
  std::condition_variable pktCv_;
  std::unique_ptr<FramePacket> pendingPacket_;
  std::condition_variable pktDoneCv_;
  std::uint64_t pktRenderedSeq_{0};
  std::uint64_t pktSubmitSeq_{0};
  std::vector<uint8_t> renderCapture_;
  int renderCaptureW_{0};
  int renderCaptureH_{0};
#else
  static constexpr bool renderThreadActive_ = false;
#endif

  // MCP server (transports started in run(); commands drained each frame).
  bool mcpStdio_{false};
  int mcpHttpPort_{0};
  // Bumped on each successful load so the MCP library-tool bridge knows when to
  // re-snapshot the Stage into its Context.
  std::uint64_t sceneGen_{0};
#if defined(TUSDVIEW_HAVE_MCP)
  std::unique_ptr<MCPServer> mcp_;
  // Context for the tinyusdz library tools; its Stage is a lazy snapshot of
  // loaded_.stage, refreshed when sceneGen_ changes.
  tinyusdz::tydra::mcp::Context mcpCtx_;
  std::uint64_t mcpCtxGen_{~std::uint64_t(0)};
#endif
};

}  // namespace tusdview
