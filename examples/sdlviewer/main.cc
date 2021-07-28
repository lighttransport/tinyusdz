
#include <algorithm>
#include <atomic>  // C++11
#include <cassert>
#include <chrono>  // C++11
#include <cmath>
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
#include "simple-render.hh"
#include "tinyusdz.hh"
#include "trackball.h"

#include "roboto_mono_embed.inc.h"

// sdlviewer
#include "gui.hh"

#if defined(USDVIEW_USE_NATIVEFILEDIALOG)
#include "nfd.h"
#endif

struct GUIContext {
  enum AOVMode {
    AOV_COLOR = 0,
    AOV_SHADING_NORMAL,
    AOV_GEOMETRIC_NORMAL,
    AOV_POSITION,
    AOV_DEPTH,
    AOV_TEXCOORD,
    AOV_VARYCOORD,
    AOV_VERTEXCOLOR
  };
  int aov_mode{AOV_COLOR};

  example::AOV aov;  // framebuffer

  int width = 1024;
  int height = 768;

  int mouse_x = -1;
  int mouse_y = -1;

  bool mouse_left_down = false;
  bool shift_pressed = false;
  bool ctrl_pressed = false;
  bool tab_pressed = false;

  float yaw = 90.0f; // for Z up scene
  float pitch = 0.0f;
  float roll = 0.0f;

  // float curr_quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  // std::array<float, 4> prev_quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};

  // std::array<float, 3> eye = {0.0f, 0.0f, 5.0f};
  // std::array<float, 3> lookat = {0.0f, 0.0f, 0.0f};
  // std::array<float, 3> up = {0.0f, 1.0f, 0.0f};

  example::RenderScene render_scene;

  example::Camera camera;

  std::atomic<bool> update_texture{false};
  std::atomic<bool> redraw{true};  // require redraw
  std::atomic<bool> quit{false};
};

GUIContext gCtx;

namespace {

inline double radians(double degree) { return 3.141592653589 * degree / 180.0; }

// https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles
std::array<double, 4> ToQuaternion(double yaw, double pitch,
                                   double roll)  // yaw (Z), pitch (Y), roll (X)
{
  // Abbreviations for the various angular functions
  double cy = std::cos(yaw * 0.5);
  double sy = std::sin(yaw * 0.5);
  double cp = std::cos(pitch * 0.5);
  double sp = std::sin(pitch * 0.5);
  double cr = std::cos(roll * 0.5);
  double sr = std::sin(roll * 0.5);

  std::array<double, 4> q;
  q[0] = cr * cp * cy + sr * sp * sy;
  q[1] = sr * cp * cy - cr * sp * sy;
  q[2] = cr * sp * cy + sr * cp * sy;
  q[3] = cr * cp * sy - sr * sp * cy;

  return q;
}

static void DrawGeomMesh(tinyusdz::GeomMesh& mesh) {}

static void DrawNode(const tinyusdz::Scene& scene, const tinyusdz::Node& node) {
  if (node.type == tinyusdz::NODE_TYPE_XFORM) {
    const tinyusdz::Xform& xform = scene.xforms.at(node.index);
  }

  for (const auto& child : node.children) {
    //DrawNode(scene, scene.nodes.at(child));
  }

  if (node.type == tinyusdz::NODE_TYPE_XFORM) {
    // glPopMatrix();
  }
}

static void Proc(const tinyusdz::Scene& scene) {
  std::cout << "num geom_meshes = " << scene.geom_meshes.size() << "\n";

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

// TODO: Use pow table for faster conversion.
inline float linearToSrgb(float x) {
  if (x <= 0.0f)
    return 0.0f;
  else if (x >= 1.0f)
    return 1.0f;
  else if (x < 0.0031308f)
    return x * 12.92f;
  else
    return std::pow(x, 1.0f / 2.4f) * 1.055f - 0.055f;
}

inline uint8_t ftouc(float f) {
  int val = int(f * 255.0f);
  val = std::max(0, std::min(255, val));

  return static_cast<uint8_t>(val);
}

void UpdateTexutre(SDL_Texture* tex, const GUIContext& ctx,
                   const example::AOV& aov) {
  int w, h;
  SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);

  if ((aov.width != w) || (aov.height != h)) {
    std::cerr << "texture size and AOV sized mismatch\n";
    return;
  }

  std::vector<uint8_t> buf;
  buf.resize(aov.width * aov.height * 4);

  if (ctx.aov_mode == GUIContext::AOV_COLOR) {
    for (size_t i = 0; i < aov.width * aov.height; i++) {
      uint8_t r = ftouc(linearToSrgb(aov.rgb[3 * i + 0]));
      uint8_t g = ftouc(linearToSrgb(aov.rgb[3 * i + 1]));
      uint8_t b = ftouc(linearToSrgb(aov.rgb[3 * i + 2]));

      buf[4 * i + 0] = r;
      buf[4 * i + 1] = g;
      buf[4 * i + 2] = b;
      buf[4 * i + 3] = 255;
    }
  } else if (ctx.aov_mode == GUIContext::AOV_SHADING_NORMAL) {
    for (size_t i = 0; i < aov.width * aov.height; i++) {
      uint8_t r = ftouc(aov.shading_normal[3 * i + 0]);
      uint8_t g = ftouc(aov.shading_normal[3 * i + 1]);
      uint8_t b = ftouc(aov.shading_normal[3 * i + 2]);

      buf[4 * i + 0] = r;
      buf[4 * i + 1] = g;
      buf[4 * i + 2] = b;
      buf[4 * i + 3] = 255;
    }
  } else if (ctx.aov_mode == GUIContext::AOV_GEOMETRIC_NORMAL) {
    for (size_t i = 0; i < aov.width * aov.height; i++) {
      uint8_t r = ftouc(aov.geometric_normal[3 * i + 0]);
      uint8_t g = ftouc(aov.geometric_normal[3 * i + 1]);
      uint8_t b = ftouc(aov.geometric_normal[3 * i + 2]);

      buf[4 * i + 0] = r;
      buf[4 * i + 1] = g;
      buf[4 * i + 2] = b;
      buf[4 * i + 3] = 255;
    }
  } else if (ctx.aov_mode == GUIContext::AOV_TEXCOORD) {
    for (size_t i = 0; i < aov.width * aov.height; i++) {
      uint8_t r = ftouc(aov.texcoords[2 * i + 0]);
      uint8_t g = ftouc(aov.texcoords[2 * i + 1]);
      uint8_t b = 255;

      buf[4 * i + 0] = r;
      buf[4 * i + 1] = g;
      buf[4 * i + 2] = b;
      buf[4 * i + 3] = 255;
    }
  }

  SDL_UpdateTexture(tex, nullptr, reinterpret_cast<const void*>(buf.data()),
                    aov.width * 4);
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

void RenderThread(GUIContext* ctx) {
  bool done = false;

  while (!done) {
    if (ctx->quit) {
      return;
    }

    if (!ctx->redraw) {
      // Give CPU some cycles.
      std::this_thread::sleep_for(std::chrono::milliseconds(33));
      continue;
    }

    example::Render(ctx->render_scene, ctx->camera, &ctx->aov);

    ctx->update_texture = true;

    ctx->redraw = false;
  }
};


#if defined(USDVIEW_USE_NATIVEFILEDIALOG)
// TODO: widechar(UTF-16) support for Windows
std::string OpenFileDialog() {

  std::string path;

  nfdchar_t *outPath;
  nfdfilteritem_t filterItem[1] = { { "USD file", "usda,usdc,usdz"} };

  nfdresult_t result = NFD_OpenDialog(&outPath, filterItem, 1, NULL);
  if ( result == NFD_OKAY )
  {
        puts("Success!");
        path = outPath;
        NFD_FreePath(outPath);
    }
    else if ( result == NFD_CANCEL )
    {
        puts("User pressed cancel.");
    }
    else
    {
        printf("Error: %s\n", NFD_GetError() );
    }


    return path;

}
#endif


}  // namespace

int main(int argc, char** argv) {
  SDL_Init(SDL_INIT_VIDEO);

  SDL_Window* window =
      SDL_CreateWindow("Simple USDZ viewer", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, 1600, 800, SDL_WINDOW_RESIZABLE);
  if (!window) {
    std::cerr << "Failed to create SDL2 window. If you are running on Linux, "
                 "probably X11 Display is not setup correctly. Check your "
                 "DISPLAY environment.\n";
    exit(-1);
  }

  SDL_Renderer* renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);

  if (!renderer) {
    std::cerr << "Failed to create SDL2 renderer. If you are running on "
                 "Linux, "
                 "probably X11 Display is not setup correctly. Check your "
                 "DISPLAY environment.\n";
    exit(-1);
  }

#ifdef _WIN32
  std::string filename = "../../models/suzanne.usdc";
#else
  std::string filename = "../../../models/suzanne.usdc";
#endif

#if defined(USDVIEW_USE_NATIVEFILEDIALOG)
  NFD_Init();

#endif

  if (argc > 1) {
    filename = std::string(argv[1]);
  } else {

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

  for (size_t i = 0; i < scene.geom_meshes.size(); i++) {
    example::DrawGeomMesh draw_mesh(&scene.geom_meshes[i]);
    gui_ctx.render_scene.draw_meshes.push_back(draw_mesh);
  }

  // Setup render mesh
  if (!gui_ctx.render_scene.Setup()) {
    std::cerr << "Failed to setup render mesh.\n";
    exit(-1);
  }

  bool done = false;

  ImGui::CreateContext();

  {
    ImGuiIO& io = ImGui::GetIO();

    ImFontConfig roboto_config;
    strcpy(roboto_config.Name, "Roboto");

    float font_size = 18.0f;
    io.Fonts->AddFontFromMemoryCompressedTTF(roboto_mono_compressed_data,
                                             roboto_mono_compressed_size,
                                             font_size, &roboto_config);
  }


  ImGuiSDL::Initialize(renderer, 1600, 800);
  // ImGui_ImplGlfw_InitForOpenGL(window, true);
  // ImGui_ImplOpenGL2_Init();

  int render_width = 512;
  int render_height = 512;

  SDL_Texture* texture =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                        SDL_TEXTUREACCESS_TARGET, render_width, render_height);
  {
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, nullptr);

    // UpdateTexutre(texture, 0);
  }

  ScreenActivate(window);

  gui_ctx.aov.Resize(render_width, render_height);
  UpdateTexutre(texture, gui_ctx, gui_ctx.aov);

  int display_w, display_h;
  ImVec4 clear_color = {0.1f, 0.18f, 0.3f, 1.0f};

  // init camera matrix
  {
    auto q = ToQuaternion(radians(gui_ctx.yaw), radians(gui_ctx.pitch),
                          radians(gui_ctx.roll));
    gui_ctx.camera.quat[0] = q[0];
    gui_ctx.camera.quat[1] = q[1];
    gui_ctx.camera.quat[2] = q[2];
    gui_ctx.camera.quat[3] = q[3];
  }

  std::thread render_thread(RenderThread, &gui_ctx);

  // Initial rendering requiest
  gui_ctx.redraw = true;

  std::map<std::string, int> aov_list = {
    { "color", GUIContext::AOV_COLOR },
    { "shading normal", GUIContext::AOV_SHADING_NORMAL },
    { "geometric normal", GUIContext::AOV_GEOMETRIC_NORMAL },
    { "texcoord", GUIContext::AOV_TEXCOORD }
  };

  std::string aov_name = "color";


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

    // Setup low-level inputs (e.g. on Win32, GetKeyboardState(), or
    // write to those fields from your Windows message loop handlers,
    // etc.)

    io.DeltaTime = 1.0f / 60.0f;
    io.MousePos =
        ImVec2(static_cast<float>(mouseX), static_cast<float>(mouseY));
    io.MouseDown[0] = buttons & SDL_BUTTON(SDL_BUTTON_LEFT);
    io.MouseDown[1] = buttons & SDL_BUTTON(SDL_BUTTON_RIGHT);
    io.MouseWheel = static_cast<float>(wheel);

    ImGui::NewFrame();

    ImGui::Begin("Scene");
    bool update = false;

    bool update_display = false;

#if defined(USDVIEW_USE_NATIVEFILEDIALOG)
    if (ImGui::Button("Open file ...")) {
      std::string filename = OpenFileDialog();
      std::cout << "TODO: Open file" << "\n";
    }
#endif

    if (example::ImGuiComboUI("aov", aov_name, aov_list)) {
      gui_ctx.aov_mode = aov_list[aov_name];
      update_display = true;
    }

    //update |= ImGui::InputFloat3("eye", gui_ctx.camera.eye);
    //update |= ImGui::InputFloat3("look_at", gui_ctx.camera.look_at);
    //update |= ImGui::InputFloat3("up", gui_ctx.camera.up);
    update |= ImGui::SliderFloat("eye.z", &gui_ctx.camera.eye[2], -1000.0, 1000.0f);
    update |= ImGui::SliderFloat("fov", &gui_ctx.camera.fov, 0.01f, 140.0f);

    // TODO: Validate coordinate definition.
    if (ImGui::SliderFloat("yaw", &gui_ctx.yaw, -360.0f, 360.0f)) {
      auto q = ToQuaternion(radians(gui_ctx.yaw), radians(gui_ctx.pitch),
                            radians(gui_ctx.roll));
      gui_ctx.camera.quat[0] = q[0];
      gui_ctx.camera.quat[1] = q[1];
      gui_ctx.camera.quat[2] = q[2];
      gui_ctx.camera.quat[3] = q[3];
      update = true;
    }
    if (ImGui::SliderFloat("pitch", &gui_ctx.pitch, -360.0f, 360.0f)) {
      auto q = ToQuaternion(radians(gui_ctx.yaw), radians(gui_ctx.pitch),
                            radians(gui_ctx.roll));
      gui_ctx.camera.quat[0] = q[0];
      gui_ctx.camera.quat[1] = q[1];
      gui_ctx.camera.quat[2] = q[2];
      gui_ctx.camera.quat[3] = q[3];
      update = true;
    }
    if (ImGui::SliderFloat("roll", &gui_ctx.roll, -360.0f, 360.0f)) {
      auto q = ToQuaternion(radians(gui_ctx.yaw), radians(gui_ctx.pitch),
                            radians(gui_ctx.roll));
      gui_ctx.camera.quat[0] = q[0];
      gui_ctx.camera.quat[1] = q[1];
      gui_ctx.camera.quat[2] = q[2];
      gui_ctx.camera.quat[3] = q[3];
      update = true;
    }
    ImGui::End();

    ImGui::Begin("Image");
    ImGui::Image(texture, ImVec2(render_width, render_height));
    ImGui::End();

    if (update) {
      gui_ctx.redraw = true;
    }

#if 0
    // Draw scene
    if ((scene.root_node >= 0) && (scene.root_node < scene.nodes.size())) {
      DrawNode(scene, scene.nodes[scene.root_node]);
    }
#endif

    // Update texture
    if (gui_ctx.update_texture || update_display) {
      // Update texture for display
      UpdateTexutre(texture, gui_ctx, gui_ctx.aov);

      gui_ctx.update_texture = false;
    }

    SDL_SetRenderDrawColor(renderer, 114, 144, 154, 255);
    SDL_RenderClear(renderer);

    // Imgui

    ImGui::Render();
    ImGuiSDL::Render(ImGui::GetDrawData());

    // static int texUpdateCount = 0;
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
    }

    SDL_RenderPresent(renderer);
  };

  // Notify render thread to exit app
  gui_ctx.quit = true;

  render_thread.join();

  ImGuiSDL::Deinitialize();

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);

  ImGui::DestroyContext();

#if defined(USDVIEW_USE_NATIVEFILEDIALOG)
  NFD_Quit();
#endif

  return EXIT_SUCCESS;
}
