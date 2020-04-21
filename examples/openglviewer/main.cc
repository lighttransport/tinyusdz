// glad must be included before glfw3.h
#include <glad/glad.h>

#include <GLFW/glfw3.h>

#include <atomic>  // C++11
#include <chrono>  // C++11
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>   // C++11
#include <thread>  // C++11

// common
#include "imgui.h"

#include "imgui_impl_glfw.h"

#include "imgui_impl_opengl2.h"

#include "trackball.h"

#include "tinyusdz.hh"

struct GUIContext {
  enum AOV {
    AOV_COLOR = 0,
    AOV_NORMAL,
    AOV_POSITION,
    AOV_DEPTH,
    AOV_TEXCOORD,
    AOV_VARYCOORD,
    AOV_VERTEXCOLOR
  };
  AOV aov{AOV_COLOR};

  int width = 1024;
  int height = 768;

  int mouse_pos_x = -1;
  int mouse_pos_y = -1;

  bool mouse_left_down = false;
  bool shift_pressed = false;
  bool ctrl_pressed = false;

  // float gShowPositionScale = 1.0f;
  // float gShowDepthRange[2] = {10.0f, 20.f};
  // bool gShowDepthPeseudoColor = true;

  float curr_quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float prev_quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

GUIContext gCtx;

static void error_callback(int error, const char* description) {
  std::cerr << "GLFW Error : " << error << ", " << description << std::endl;
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action,
                         int mods) {
  (void)scancode;

  ImGuiIO& io = ImGui::GetIO();
  if (io.WantCaptureKeyboard) {
    return;
  }

  if ((key == GLFW_KEY_LEFT_SHIFT) || (key == GLFW_KEY_RIGHT_SHIFT)) {
    auto* param =
        reinterpret_cast<GUIContext*>(glfwGetWindowUserPointer(window));

    param->shift_pressed = (action == GLFW_PRESS);
  }

  if ((key == GLFW_KEY_LEFT_CONTROL) || (key == GLFW_KEY_RIGHT_CONTROL)) {
    auto* param =
        reinterpret_cast<GUIContext*>(glfwGetWindowUserPointer(window));

    param->ctrl_pressed = (action == GLFW_PRESS);
  }

  if (key == GLFW_KEY_Q && action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL)) {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
  }
}

static void DrawGeomMesh(tinyusdz::GeomMesh &mesh)
{

}

static void DrawNode(const tinyusdz::Scene &scene, const tinyusdz::Node &node)
{
  if (node.type == tinyusdz::NODE_TYPE_XFORM) {
    const tinyusdz::Xform &xform = scene.xforms.at(node.index);
    glPushMatrix();

    glMultMatrixd(reinterpret_cast<const double *>(&(xform.matrix.m)));
  }

  for (const auto &child : node.children) {
    DrawNode(scene, scene.nodes.at(child));
  }

  if (node.type == tinyusdz::NODE_TYPE_XFORM) {
    glPopMatrix();
  }
}

int main(int argc, char** argv) {
  // Setup window
  glfwSetErrorCallback(error_callback);
  if (!glfwInit()) {
    exit(EXIT_FAILURE);
  }

#ifdef _DEBUG_OPENGL
  glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

  //// Create a GLES 3.0 context
  // glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
  // glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  // glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  // glfwWindowHint(GLFW_SAMPLES, 16);
  // glfwWindowHint(GLFW_DOUBLEBUFFER, GL_TRUE);

#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,
                 GL_TRUE);  // It looks this is important on macOS.
#endif

  GLFWwindow* window{nullptr};
  window = glfwCreateWindow(gCtx.width, gCtx.height, "Simple USDZ GL viewer",
                            nullptr, nullptr);
  glfwMakeContextCurrent(window);

  if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
    std::cerr << "Failed to load OpenGL functions with gladLoadGL\n";
    exit(EXIT_FAILURE);
  }

  std::cout << "OpenGL " << GLVersion.major << '.' << GLVersion.minor << '\n';

  if (GLVersion.major < 2) {
    std::cerr << "OpenGL 2. or later is not available." << std::endl;
    exit(EXIT_FAILURE);
  }


#ifdef _DEBUG_OPENGL
  glEnable(GL_DEBUG_OUTPUT);
  glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  glDebugMessageCallback(reinterpret_cast<GLDEBUGPROC>(glDebugOutput), nullptr);
  glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr,
                        GL_TRUE);
#endif


  glfwSwapInterval(1);  // Enable vsync
  glfwSetKeyCallback(window, key_callback);

  bool done = false;

  ImGui::CreateContext();

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL2_Init();


  int display_w, display_h;
  ImVec4 clear_color = {0.1f, 0.18f, 0.3f, 1.0f};

  while (!done) {

    glfwPollEvents();
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Bora");
    ImGui::Button("muda");
    ImGui::End();

    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ImGui::Render();
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
    glFlush();

    static int frameCount = 0;
    static double currentTime = glfwGetTime();
    static double previousTime = currentTime;
    static char title[256];

    frameCount++;
    currentTime = glfwGetTime();
    const auto deltaTime = currentTime - previousTime;
    if (deltaTime >= 1.0) {
      sprintf(title, "Simple GL USDZ viewer [%dFPS]", frameCount);
      glfwSetWindowTitle(window, title);
      frameCount = 0;
      previousTime = currentTime;
    }

    glfwSwapBuffers(window);

    done = glfwWindowShouldClose(window);
  };

  ImGui_ImplOpenGL2_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();


  return EXIT_SUCCESS;
}
