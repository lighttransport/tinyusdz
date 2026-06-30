// SPDX-License-Identifier: Apache-2.0
// tusdview - application: owns the window, renderer, scene and GUI; runs the
// main loop.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "frame_packet.hh"

#include "camera_nav.hh"
#include "cuda/cuda_raytracer.hh"
#include "gpu_scene.hh"
#include "hip/hip_raytracer.hh"
#include "rt_scene_build.hh"  // BuildProgress (threaded RT build)
#include "gui.hh"
#include "load_control.hh"
#include "parametric_tess.hh"
#include "renderer.hh"
#include "scene_loader.hh"
#include "stream/stream_server.hh"
#if defined(TUSDVIEW_HAVE_MCP)
#include "mcp/mcp_host.hh"
#include "mcp/mcp_server.hh"
#include "tydra/js-script.hh"    // complete JSEngineState (held in Context)
#include "tydra/mcp-context.hh"  // tinyusdz::tydra::mcp::Context (library tool bridge)
#endif

struct GLFWwindow;

namespace tinyusdz { namespace next { class Stage; } }

namespace tusdview {

// Encode an RGBA8 (top-down) buffer to an image file; format chosen by extension
// (.png/.ppm). Defined in app.cc. Declared here so the MCP screenshot tool can
// reuse it.
bool WriteScreenshotImage(const std::string& path, const std::vector<uint8_t>& rgba,
                          int w, int h, std::string* err);

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

  // GPU-budget LOD for the realtime raster preview (huge assembled scenes).
  // gpuMemBudgetBytes>0 caps full-mesh VRAM; maxFullMeshes>0 caps the full-mesh
  // (draw) count; overflow meshes merge into one bbox-proxy soup. 0/0 = off.
  void setGpuBudget(std::size_t gpuMemBudgetBytes, std::size_t maxFullMeshes) {
    gpuMemBudgetBytes_ = gpuMemBudgetBytes;
    maxFullMeshes_ = maxFullMeshes;
  }

  // Robust auto-framing: trim horizon-scale outlier meshes from the fit-all bbox
  // (default on). --no-robust-frame disables it to frame the literal scene bbox.
  void setRobustFrame(bool on) { robustFrame_ = on; }

  // HiDPI UI scale (font + widget sizes). Auto-detected from the monitor at
  // startup (1.0 on standard-density displays, 2.0 on HiDPI / >=2K panels) unless
  // explicitly set via --ui-scale / --font-size / --window-scale.
  void setUiScale(float s) {
    if (s > 0.25f) {
      uiScale_ = s;
      fontSizePx_ = 16.0f * s;
      windowScale_ = s;
      uiScaleExplicit_ = true;
    }
  }
  void setFontSize(float px) {
    if (px > 4.0f) {
      fontSizePx_ = px;
      uiScale_ = px / 16.0f;
      uiScaleExplicit_ = true;
    }
  }
  void setWindowScale(float s) {
    if (s > 0.25f) { windowScale_ = s; uiScaleExplicit_ = true; }
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
  // --hip: trace the screenshot with the HIP/ROCm BVH ray tracer (hipew runtime).
  void setHipRt(bool on) { hipRt_ = on; }
  // --rt-samples N: supersampled AA for the CUDA/HIP screenshot path (1 = off).
  void setRtSamples(int n) { rtSamples_ = n < 1 ? 1 : n; }
  // --max-instances N: cap the CUDA/HIP 2-level-BVH instance count (0 = no cap).
  void setRtMaxInstances(size_t n) { rtMaxInstances_ = n; }
  // --lod-stream: view-dependent district LOD pre-pass (needs --next). Promotes
  // the camera-nearest districts to districtLod=full under the memory budgets.
  void setLodStream(bool on) { lodStream_ = on; }
  void setLodMaxMemGiB(double g) { lodMaxMemGiB_ = g; }
  void setLodMaxVramGiB(double g) { lodMaxVramGiB_ = g; }
  // --camera <name>: frame the viewer on a named USD Camera (--next path) instead
  // of auto-fitting the whole scene. Essential for vast scenes (e.g. Caldera).
  void setCameraName(const std::string& n) { cameraName_ = n; }
  // Recently-opened scenes: the config file path to persist to, and the initial
  // list loaded from it. setRecentScenes also seeds the File > Open Recent menu.
  void setConfigPath(const std::filesystem::path& p) { configPath_ = p; }
  void setRecentScenes(const std::vector<std::string>& v) {
    recentScenes_ = v;
    gui_.setRecentScenes(recentScenes_);
  }
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
  void setStreamHttp(int port) { streamHttpPort_ = port; }  // 0 = off
  // Idle-refinement codec for the stream: "png" (default) or "qoi". While the
  // view is moving, frames are sent as small low-quality JPEG; once the view is
  // stable a single full-resolution lossless frame is sent in this codec.
  void setStreamCodec(const std::string& c) {
    streamCodec_ = c;
    if (c == "png" || c == "qoi") streamIdleCodec_ = c;
  }
  // Motion-frame tuning: long-edge resolution cap (px) and JPEG quality (1-100)
  // used while the view is changing (the stable refine is always full-res lossless).
  void setStreamMotionRes(int px) {
    if (px > 0) streamMotionMaxDim_ = px;
  }
  void setStreamMotionQuality(int q) {
    if (q >= 1 && q <= 100) streamMotionJpegQ_ = q;
  }
  // Apply one browser navigation command to the camera/render state (main thread).
  void applyNavCommand(const StreamNav& cmd);

#if defined(TUSDVIEW_HAVE_MCP)
  // McpHost tool handlers (defined in mcp/app_mcp.cc; run on the main thread).
  nlohmann::json mcpLoadUsd(const nlohmann::json& a, std::string& e) override;
  nlohmann::json mcpSceneInfo(const nlohmann::json& a, std::string& e) override;
  nlohmann::json mcpGetFocusedPrim(const nlohmann::json& a, std::string& e) override;
  nlohmann::json mcpSetFocus(const nlohmann::json& a, std::string& e) override;
  nlohmann::json mcpViewport(const nlohmann::json& a, std::string& e) override;
  nlohmann::json mcpScreenshot(const nlohmann::json& a, std::string& e) override;
  nlohmann::json mcpInput(const nlohmann::json& a, std::string& e) override;
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
  // Record a successfully-opened scene at the front of the recent list (dedup,
  // capped), refresh the menu, and persist to the config path.
  void addRecentScene(const std::string& path);
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
  // --next per-frame GPU morph: upload blendshape coefficients for instanced
  // prototypes from the retained next stage at animTime_. Runs independently of
  // the Tydra-path GPU-skinning gate (which --next does not engage).
  void updateNextMorphFrameIfNeeded();
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
  // Retained lazy `next` stage (--next): kept alive so per-frame blendshape
  // weights can be sampled at animTime_ (lazy arrays stay mmap-backed). Set on
  // load; pending* is the worker-thread staging slot moved in at finishLoad.
  std::shared_ptr<tinyusdz::next::Stage> nextStage_;
  std::shared_ptr<tinyusdz::next::Stage> pendingNextStage_;
  bool hasNextMorph_{false};   // any --next draw mesh carries GPU morph channels
  float camDolly_{1.0f};       // --cam-dolly: fitted-distance scale (<1 zooms in)
  OrbitCamera camera_;
  Gui gui_;
  AdaptiveTessellator tess_;      // adaptive re-tessellation for parametric prims
  float tessQuality_{1.0f};      // tessellation quality multiplier

  float uiScale_{2.0f};      // widget scale (defaults to font px / 16)
  float fontSizePx_{32.0f};  // font size in pixels
  float windowScale_{2.0f};  // default window size multiplier
  bool uiScaleExplicit_{false};  // user set --ui-scale/--font-size/--window-scale
  // Pick a default UI/window scale from the primary monitor (no-op once explicit
  // or in headless): 1.0 on standard-density / sub-2K displays, 2.0 on HiDPI/>=2K.
  void autoDetectUiScale();
  bool hasWindowSizeOverride_{false};
  int windowWidth_{0};
  int windowHeight_{0};
  std::string windowShot_;
  bool headless_{false};  // windowless offscreen rendering (Vulkan only)
  bool cudaRt_{false};    // --cuda: CUDA BVH ray-traced screenshot (cuew runtime)
  std::string cameraName_;  // --camera: named USD camera to frame (--next path)
  std::filesystem::path configPath_;        // where to persist recent scenes
  std::vector<std::string> recentScenes_;   // newest first; File > Open Recent
  CudaRayTracer cudaTracer_;
  size_t cudaMaxTris_{32000000};  // flattened-triangle cap (instances expanded)
  std::size_t gpuMemBudgetBytes_{0};  // --max-gpu-mem: raster full-mesh VRAM cap
  std::size_t maxFullMeshes_{0};      // --max-draw-meshes: raster full-mesh count cap
  bool robustFrame_{true};            // trim outlier meshes from fit-all bbox
  bool robustBoundsValid_{false};     // robust bounds computed (pre-LOD) this load
  float robustBoundsMin_[3]{0, 0, 0};
  float robustBoundsMax_[3]{0, 0, 0};
  bool hipRt_{false};     // --hip: HIP/ROCm BVH ray-traced screenshot (hipew runtime)
  // True when a headless --cuda/--hip run owns the screenshot: the rasterized
  // upload + per-frame draw are then skipped (the RT path writes the image, the
  // raster capture is never used) -- a big win on huge scenes (Moana Island).
  bool rtOwnsScreenshot_{false};
  // True for a windowed --hip run: the HIP tracer drives the viewport per frame
  // (build once, retrace on the orbit camera, upload into the offscreen color via
  // renderer_->uploadViewportImage). The raster scene upload is skipped (CPU
  // geometry is kept for the tracer), and rendering stays single-threaded.
  bool hipInteractive_{false};
  bool hipInteractiveBuilt_{false};  // HIP scene built lazily on the first frame
  int hipBuildAnnounceFrames_{0};    // frames rendered with the "building" overlay before kicking off the build
  std::string rtBuildNote_;          // RT build status for the progress overlay
  // Background HIP scene build: the build runs on a worker thread so the UI stays
  // responsive and shows live progress instead of freezing on the multi-second build.
  std::thread hipBuildThread_;
  bool hipBuildStarted_{false};
  std::atomic<bool> hipBuildDone_{false};
  std::atomic<bool> hipBuildOk_{false};
  std::string hipBuildErr_;          // written by the worker, read after join
  BuildProgress hipBuildProgress_;   // atomics polled by the overlay
  std::chrono::steady_clock::time_point hipBuildStart_;
  // Trace the HIP viewport for one interactive frame (builds the scene on first
  // call). Returns false if HIP is unavailable / the build failed.
  bool renderHipViewport();
  // Encode an RGBA8 window grab and broadcast it. `motion`=true sends a small
  // low-quality JPEG (fast, for interaction); false sends a full-resolution
  // lossless frame in streamIdleCodec_ (the stable-state refinement).
  void streamEncodeAndPush(std::vector<uint8_t> rgba, int w, int h, bool motion);
  // Mark the streamed view as changed (resets the idle refinement timer).
  void markStreamActivity();
  HipRayTracer hipTracer_;
  int rtSamples_{1};      // --rt-samples: AA samples for the CUDA/HIP screenshot
  size_t rtMaxInstances_{16000000};  // --max-instances: CUDA/HIP instance cap (0=off)
  bool lodStream_{false}; // --lod-stream: view-dependent district LOD pre-pass
  double lodMaxMemGiB_{0.0};   // --max-mem: host budget for --lod-stream (0=auto)
  double lodMaxVramGiB_{0.0};  // --max-vram: GPU budget for --lod-stream (0=auto)

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
  // Rest-pose scene cache so a same-timecode reconvert (interactive blendshape
  // edit on the RT/CPU path) reuses the conversion instead of re-running it.
  // Touched only by the reconvert worker; cleared on reload (worker joined first).
  RestSceneCache reconvRestCache_;

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

  // WebSocket image-streaming server (browser remote view + navigation).
  int streamHttpPort_{0};
  std::string streamCodec_{"jpeg"};
  std::unique_ptr<StreamServer> streamServer_;
  // Browser input state (raw mouse/keyboard forwarded into ImGui + camera).
  bool streamCamDrag_{false};   // current drag drives the camera (not ImGui)
  int streamDragButton_{0};     // DOM button latched at press
  bool streamDragShift_{false}; // shift held at press (orbit->pan)
  float streamLastX_{0.f}, streamLastY_{0.f};  // last cursor (image space)
  // Adaptive quality: low-res low-q JPEG while moving, one full-res lossless
  // refine (PNG/QOI) once stable.
  std::string streamIdleCodec_{"png"};      // refinement codec (png/qoi)
  std::chrono::steady_clock::time_point streamLastActivity_{};
  bool streamHiQSent_{false};               // refine frame already sent for this idle
  int streamPrevClientCount_{0};
  int streamMotionMaxDim_{1280};            // long-edge cap for motion frames
  int streamMotionJpegQ_{45};               // motion JPEG quality
  int streamIdleMs_{350};                   // ms of no activity = stable
  int streamResizeW_{0}, streamResizeH_{0}; // pending headless resize (0 = none)
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
