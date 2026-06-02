// SPDX-License-Identifier: Apache-2.0
// tusdview - application: owns the window, renderer, scene and GUI; runs the
// main loop.
#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "camera_nav.hh"
#include "gpu_scene.hh"
#include "gui.hh"
#include "load_control.hh"
#include "renderer.hh"
#include "scene_loader.hh"

struct GLFWwindow;

namespace tusdview {

class App {
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

  // Write a PPM of the full composited window after the last frame (QA).
  void setWindowShot(const std::string& path) { windowShot_ = path; }

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
};

}  // namespace tusdview
