// SPDX-License-Identifier: Apache-2.0
// tusdview - application: owns the window, renderer, scene and GUI; runs the
// main loop.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "camera_nav.hh"
#include "gpu_scene.hh"
#include "gui.hh"
#include "load_control.hh"
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
    if (s > 0.25f) uiScale_ = s;
  }

  // Request the Vulkan ray-tracing technique at startup (honored only when the
  // device supports it; otherwise the viewer stays on rasterization).
  void setRequestRayTracing(bool on) { rtRequested_ = on; }

  // Write a PPM of the full composited window after the last frame (QA).
  void setWindowShot(const std::string& path) { windowShot_ = path; }

  // Windowless rendering: no GLFW window, surface or swapchain (Vulkan only).
  // Renders frames offscreen for --screenshot / --window-shot. Requires --frames.
  void setHeadless(bool on) { headless_ = on; }

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
  void openFileDialog();

  // Synchronous load (used for headless --frames runs so screenshots are
  // deterministic) and the async path (keeps the UI responsive).
  void loadFileBlocking(const std::string& path);
  void startLoadAsync(const std::string& path);
  void finishLoadIfReady();
  void applyLoaded(bool ok, bool progressive);  // upload + bind on the main thread
  void stepProgressiveUpload();  // stream meshes then textures, budgeted per frame
  void cancelAndJoinLoad();

  Backend backend_;
  GLFWwindow* window_{nullptr};
  std::unique_ptr<Renderer> renderer_;

  LoadedScene loaded_;
  DrawScene draw_;
  OrbitCamera camera_;
  Gui gui_;

  float uiScale_{2.0f};  // HiDPI scale (font px = 16 * uiScale_)
  std::string windowShot_;
  bool headless_{false};  // windowless offscreen rendering (Vulkan only)

  // Ray tracing: requested via --rt; rtPath_ is the effective state after the
  // renderer reports capability (drives the RT-friendly conversion config).
  bool rtRequested_{false};
  bool rtPath_{false};

  // Async loading
  std::thread loadThread_;
  LoadControl loadCtrl_;
  std::atomic<bool> loadFinished_{false};
  bool loadActive_{false};  // main-thread-only UI flag
  std::unique_ptr<LoadedScene> pendingLoaded_;
  std::unique_ptr<DrawScene> pendingDraw_;
  std::string loadingPath_;
  std::chrono::steady_clock::time_point loadStart_;

  // Progressive GPU upload (interactive path): stream meshes then textures.
  bool progressiveActive_{false};
  size_t nextMesh_{0};
  size_t nextTex_{0};

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
