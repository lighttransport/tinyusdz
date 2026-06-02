// SPDX-License-Identifier: Apache-2.0
#include "app.hh"

#include <glad/glad.h>
//
#include <GLFW/glfw3.h>

#include <cstdio>

#include "cascadia_mono.h"  // CascadiaMono_compressed_data / _size
#include "gui_style.hh"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "mesh_build.hh"

#if defined(HAVE_NFD)
#include "nfd.h"
#endif

namespace tusdview {

namespace {
void GlfwErrorCallback(int code, const char* desc) {
  std::fprintf(stderr, "[glfw] error %d: %s\n", code, desc);
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
}  // namespace

App::~App() {
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

  // Scale the default window with the UI scale (4K panels need a larger window
  // so the HiDPI-scaled panels aren't cramped), clamped to the monitor work area.
  int winW = static_cast<int>(1280.0f * uiScale_);
  int winH = static_cast<int>(800.0f * uiScale_);
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
  const float fontPx = 16.0f * uiScale_;
  io.Fonts->AddFontFromMemoryCompressedTTF(CascadiaMono_compressed_data,
                                           CascadiaMono_compressed_size, fontPx);

  // Maya-like dark theme, then scale rounding/padding/spacing for HiDPI.
  StyleMaya();
  ImGui::GetStyle().ScaleAllSizes(uiScale_);

  return renderer_->initImGui(err);
}

void App::applyLoaded(bool ok, bool progressive) {
  progressiveActive_ = false;
  nextMesh_ = 0;
  nextTex_ = 0;

  if (ok) {
    const std::string& up = loaded_.render.meta.upAxis;
    camera_.setUpAxis((up == "Z" || up == "z") ? 2 : 1);
    if (draw_.hasBounds) camera_.fitToScene(draw_.aabbMin, draw_.aabbMax);
  }

  if (ok && progressive) {
    // Reserve materials + texture slots now; stream meshes then textures over
    // the next frames (stepProgressiveUpload) so geometry pops in and the UI
    // stays at frame rate instead of stalling on one big upload.
    renderer_->beginScene(draw_.materials, static_cast<int>(draw_.textures.size()));
    progressiveActive_ = true;
    std::fprintf(stderr,
                 "[tusdview] loaded %s: %zu mesh(es), %zu tri(s)%s; streaming to GPU...\n",
                 loaded_.filepath.c_str(), draw_.meshes.size(), draw_.triangleCount,
                 draw_.truncated ? " [truncated]" : "");
  } else {
    // Synchronous full upload (headless / failure). draw_ is empty when !ok.
    std::string uerr;
    renderer_->uploadScene(draw_, &uerr);
    if (ok) {
      std::fprintf(stderr, "[tusdview] loaded %s: %zu mesh(es), %zu tri(s)%s\n",
                   loaded_.filepath.c_str(), draw_.meshes.size(), draw_.triangleCount,
                   draw_.truncated ? " [truncated: render budget]" : "");
    } else {
      std::fprintf(stderr, "[tusdview] load failed: %s\n", loaded_.err.c_str());
    }
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
    renderer_->appendMesh(draw_.meshes[nextMesh_++]);
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
  const bool ok = LoadUSD(path, &tmp, &loadCtrl_);
  loaded_ = std::move(tmp);
  draw_ = DrawScene{};
  if (ok) BuildDrawScene(loaded_.render, &draw_, &loadCtrl_);
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
  // Worker touches only CPU data (no GL/VK), so this is thread-safe.
  loadThread_ = std::thread([this, path, lp, dp]() {
    const bool ok = LoadUSD(path, lp, &loadCtrl_);
    if (ok) BuildDrawScene(lp->render, dp, &loadCtrl_);
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
  if (loadThread_.joinable()) {
    loadCtrl_.cancel.store(true);
    loadThread_.join();
  }
  loadActive_ = false;
  loadFinished_.store(false);
  pendingLoaded_.reset();
  pendingDraw_.reset();
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
  std::fprintf(stderr,
               "[tusdview] File dialog not available in this build. Pass a USD "
               "file on the command line.\n");
#endif
}

int App::run(const std::string& initialFile, int maxFrames,
             const std::string& screenshot) {
  std::string err;
  if (!initWindow(&err)) {
    std::fprintf(stderr, "[tusdview] %s\n", err.c_str());
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
    std::fprintf(stderr, "[tusdview] no renderer for requested backend\n");
    return 1;
  }
  if (!renderer_->init(window_, &err)) {
    std::fprintf(stderr, "[tusdview] renderer init failed: %s\n", err.c_str());
    return 1;
  }
  if (!initImGui(&err)) {
    std::fprintf(stderr, "[tusdview] ImGui init failed: %s\n", err.c_str());
    return 1;
  }

  gui_.setScene(&loaded_, &draw_);
  gui_.setBudget(&loadCtrl_);
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
  while (!glfwWindowShouldClose(window_)) {
    glfwPollEvents();

    // Pick up a completed async load, then stream its meshes/textures to the
    // GPU a little per frame so the UI stays responsive (progressive upload).
    finishLoadIfReady();
    stepProgressiveUpload();

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
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    gui_.frame(renderer_.get(), &camera_);

    // Grab the composited window on the final headless frame (--window-shot).
    if (!windowShot_.empty() && maxFrames >= 0 && frameCount == maxFrames - 1) {
      renderer_->requestWindowCapture();
    }

    ImGui::Render();
    renderer_->present();

    // Deferred actions (after the frame, outside the ImGui frame state).
    const bool reload = gui_.wantReload();
    const bool open = gui_.wantOpen();
    const bool quit = gui_.wantQuit();
    const bool cancelLoad = gui_.wantCancelLoad();
    gui_.clearActions();
    if (cancelLoad) loadCtrl_.cancel.store(true);
    if (quit) glfwSetWindowShouldClose(window_, GLFW_TRUE);
    if (reload && !loaded_.filepath.empty()) startLoadAsync(loaded_.filepath);
    if (open) openFileDialog();

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }

    if (maxFrames >= 0 && ++frameCount >= maxFrames) {
      glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
  }

  auto shot = [&](const std::string& path, bool window) {
    if (path.empty()) return;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    const bool ok = window ? renderer_->captureWindow(&rgba, &w, &h)
                           : renderer_->captureViewport(&rgba, &w, &h);
    if (ok && w > 0 && h > 0) {
      WritePPM(path, rgba, w, h);
      std::fprintf(stderr, "[tusdview] wrote %s (%dx%d)\n", path.c_str(), w, h);
    } else {
      std::fprintf(stderr, "[tusdview] capture not supported by this backend\n");
    }
  };
  shot(screenshot, /*window=*/false);
  shot(windowShot_, /*window=*/true);
  return 0;
}

}  // namespace tusdview
