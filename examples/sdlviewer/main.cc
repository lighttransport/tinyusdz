
#include <algorithm>
#include <atomic>  // C++11
#include <cassert>
#include <chrono>  // C++11
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>   // C++11
#include <thread>  // C++11

// ../common/SDL2
#include <SDL.h>

#if defined(SDL_VIDEO_DRIVER_X11)
#include <SDL_syswm.h>
#endif

// common
#include "imgui.h"
#include "imgui_sdl/imgui_sdl.h"
#include "tinyusdz.hh"
#include "trackball.h"

#include "simple-render.hh"

struct GUIContext {
  enum AOVMode {
    AOV_COLOR = 0,
    AOV_NORMAL,
    AOV_POSITION,
    AOV_DEPTH,
    AOV_TEXCOORD,
    AOV_VARYCOORD,
    AOV_VERTEXCOLOR
  };
  AOVMode aov_mode{AOV_COLOR};

  example::AOV aov; // framebuffer

  int width = 1024;
  int height = 768;

  int mouse_x = -1;
  int mouse_y = -1;

  bool mouse_left_down = false;
  bool shift_pressed = false;
  bool ctrl_pressed = false;
  bool tab_pressed = false;

  float rot_x = 0.0f;
  float rot_y = 0.0f;

  //float curr_quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float prev_quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};

  //std::array<float, 3> eye = {0.0f, 0.0f, 5.0f};
  //std::array<float, 3> lookat = {0.0f, 0.0f, 0.0f};
  //std::array<float, 3> up = {0.0f, 1.0f, 0.0f};

  example::RenderScene render_scene;

  example::Camera camera;

};

GUIContext gCtx;

namespace {

static void DrawGeomMesh(tinyusdz::GeomMesh& mesh) {}

static void DrawNode(const tinyusdz::Scene& scene, const tinyusdz::Node& node) {
  if (node.type == tinyusdz::NODE_TYPE_XFORM) {
    const tinyusdz::Xform& xform = scene.xforms.at(node.index);
  }

  for (const auto& child : node.children) {
    DrawNode(scene, scene.nodes.at(child));
  }

  if (node.type == tinyusdz::NODE_TYPE_XFORM) {
    // glPopMatrix();
  }
}

static void Proc(const tinyusdz::Scene& scene) {
  std::cout << "num geom_meshes = " << scene.geom_meshes.size();

  for (auto& mesh : scene.geom_meshes) {
  }
}

static std::string GetFileExtension(const std::string& filename) {
  if (filename.find_last_of(".") != std::string::npos)
    return filename.substr(filename.find_last_of(".") + 1);
  return "";
}

static std::string str_tolower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 // static_cast<int(*)(int)>(std::tolower)         // wrong
                 // [](int c){ return std::tolower(c); }           // wrong
                 // [](char c){ return std::tolower(c); }          // wrong
                 [](unsigned char c) { return std::tolower(c); }  // correct
  );
  return s;
}

void UpdateTexutre(SDL_Texture* tex, uint32_t offt) {
  std::vector<uint8_t> buf;
  buf.resize(256 * 256 * 4);

  for (size_t y = 0; y < 256; y++) {
    for (size_t x = 0; x < 256; x++) {
      buf[4 * (y * 256 + x) + 0] = x;
      buf[4 * (y * 256 + x) + 1] = y;
      buf[4 * (y * 256 + x) + 2] = (10 * offt) % 255;
      buf[4 * (y * 256 + x) + 3] = 255;
    }
  }

  SDL_UpdateTexture(tex, nullptr, reinterpret_cast<const void*>(buf.data()),
                    256 * 4);
}

// https://discourse.libsdl.org/t/sdl-and-xserver/12610/4
static void ScreenActivate(SDL_Window* window) {
#if defined(SDL_VIDEO_DRIVER_X11)
  SDL_SysWMinfo wm;

  // Get window info.
  SDL_VERSION(&wm.version);
  SDL_GetWindowWMInfo(window, &wm);

  // Lock to display access.
  // wm.info.x11.lock_func();

  // Show the window on top.
  XMapRaised(wm.info.x11.display, wm.info.x11.window);

  // Set the focus on it.
  XSetInputFocus(wm.info.x11.display, wm.info.x11.window, RevertToParent,
                 CurrentTime);
#else
  (void)window;
#endif
}

void RenderThread(GUIContext *ctx) {

  example::Render(ctx->render_scene, ctx->camera, &ctx->aov);

};

}  // namespace

int main(int argc, char** argv) {
  SDL_Init(SDL_INIT_VIDEO);

  SDL_Window* window =
      SDL_CreateWindow("Simple USDZ viewer", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_RESIZABLE);
  if (!window) {
    std::cerr << "Failed to create SDL2 window. If you are running on Linux, probably X11 Display is not setup correctly. Check your DISPLAY environment.\n";
    exit(-1);
  }

  SDL_Renderer* renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);

  if (!renderer) {
    std::cerr << "Failed to create SDL2 renderer. If you are running on Linux, probably X11 Display is not setup correctly. Check your DISPLAY environment.\n";
    exit(-1);
  }

  std::string filename = "../../../models/suzanne.usdc";

  if (argc > 1) {
    filename = std::string(argv[1]);
  }

  std::cout << "Loading file " << filename << "\n";
  std::string ext = str_tolower(GetFileExtension(filename));

  std::string warn;
  std::string err;
  tinyusdz::Scene scene;

  if (ext.compare("usdz") == 0) {
    std::cout << "usdz\n";
    bool ret = tinyusdz::LoadUSDZFromFile(filename, &scene, &warn, &err);
    if (!warn.empty()) {
      std::cerr << "WARN : " << warn << "\n";
      return EXIT_FAILURE;
    }
    if (!err.empty()) {
      std::cerr << "ERR : " << err << "\n";
      return EXIT_FAILURE;
    }

    if (!ret) {
      std::cerr << "Failed to load USDZ file: " << filename << "\n";
      return EXIT_FAILURE;
    }
  } else {  // assume usdc
    bool ret = tinyusdz::LoadUSDCFromFile(filename, &scene, &warn, &err);
    if (!warn.empty()) {
      std::cerr << "WARN : " << warn << "\n";
      return EXIT_FAILURE;
    }
    if (!err.empty()) {
      std::cerr << "ERR : " << err << "\n";
      return EXIT_FAILURE;
    }

    if (!ret) {
      std::cerr << "Failed to load USDC file: " << filename << "\n";
      return EXIT_FAILURE;
    }
  }

  std::cout << "Loaded USDC file\n";

  Proc(scene);
  if (scene.geom_meshes.empty()) {
    exit(-1);
  }


  GUIContext gui_ctx;

  // HACK
  example::DrawGeomMesh draw_mesh(&scene.geom_meshes[0]);
  gui_ctx.render_scene.draw_meshes.push_back(draw_mesh);

  bool done = false;

  ImGui::CreateContext();

  ImGuiSDL::Initialize(renderer, 800, 600);
  // ImGui_ImplGlfw_InitForOpenGL(window, true);
  // ImGui_ImplOpenGL2_Init();

  int render_width = 256;
  int render_height = 256;

  SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_TARGET, render_width, render_height);
  {
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, nullptr);

    UpdateTexutre(texture, 0);
  }

  ScreenActivate(window);

  gui_ctx.aov.Resize(render_width, render_height);

  int display_w, display_h;
  ImVec4 clear_color = {0.1f, 0.18f, 0.3f, 1.0f};

  std::thread render_thread(RenderThread, &gui_ctx);

  while (!done) {
    ImGuiIO& io = ImGui::GetIO();

    int wheel = 0;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT)
        done = true;
      else if (e.type == SDL_WINDOWEVENT) {
        if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
          io.DisplaySize.x = static_cast<float>(e.window.data1);
          io.DisplaySize.y = static_cast<float>(e.window.data2);
        }
      } else if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_ESCAPE) {
          done = true;
        }
      } else if (e.type == SDL_KEYUP) {
      } else if (e.type == SDL_MOUSEWHEEL) {
        wheel = e.wheel.y;
      }
    }

    int mouseX, mouseY;
    const int buttons = SDL_GetMouseState(&mouseX, &mouseY);

    // Setup low-level inputs (e.g. on Win32, GetKeyboardState(), or write to
    // those fields from your Windows message loop handlers, etc.)

    io.DeltaTime = 1.0f / 60.0f;
    io.MousePos =
        ImVec2(static_cast<float>(mouseX), static_cast<float>(mouseY));
    io.MouseDown[0] = buttons & SDL_BUTTON(SDL_BUTTON_LEFT);
    io.MouseDown[1] = buttons & SDL_BUTTON(SDL_BUTTON_RIGHT);
    io.MouseWheel = static_cast<float>(wheel);

    ImGui::NewFrame();

    ImGui::Begin("Scene");
    ImGui::SliderFloat("rot x", &gui_ctx.rot_x, -360.0f, 360.0f);
    ImGui::SliderFloat("rot y", &gui_ctx.rot_y, -360.0f, 360.0f);
    ImGui::End();

    ImGui::Begin("Image");
    ImGui::Image(texture, ImVec2(256, 256));
    ImGui::End();

#if 0
    // Draw scene
    if ((scene.root_node >= 0) && (scene.root_node < scene.nodes.size())) {
      DrawNode(scene, scene.nodes[scene.root_node]);
    }
#endif

    // Update texture

    SDL_SetRenderDrawColor(renderer, 114, 144, 154, 255);
    SDL_RenderClear(renderer);

    // Imgui

    ImGui::Render();
    ImGuiSDL::Render(ImGui::GetDrawData());

    static int texUpdateCount = 0;
    static int frameCount = 0;
    static double currentTime =
        SDL_GetTicks() / 1000.0;  // GetTicks returns milliseconds
    static double previousTime = currentTime;
    static char title[256];

    frameCount++;
    currentTime = SDL_GetTicks() / 1000.0;
    const auto deltaTime = currentTime - previousTime;
    if (deltaTime >= 1.0) {
      sprintf(title, "Simple USDZ viewer [%dFPS]", frameCount);
      SDL_SetWindowTitle(window, title);
      frameCount = 0;
      previousTime = currentTime;

      UpdateTexutre(texture, texUpdateCount);
      texUpdateCount++;
      std::cout << "texUpdateCount = " << texUpdateCount << "\n";
    }

    SDL_RenderPresent(renderer);
  };

  render_thread.join();

  ImGuiSDL::Deinitialize();

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);

  ImGui::DestroyContext();

  return EXIT_SUCCESS;
}
