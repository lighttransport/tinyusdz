#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

// GL
//
// glad must be included before glfw3.h
// TODO: Use imgui's imgui_impl_opengl3_loader.h
#include "glad/glad.h"
//
#include <GLFW/glfw3.h>

// GUI common
#include "../common/imgui/imgui.h"
#include "../common/imgui/imgui_impl_glfw.h"
#include "../common/imgui/imgui_impl_opengl3.h"
#include "../common/trackball.h"
#include "../common/viewport_camera.hh"

// TinyUSDZ
// include relative to openglviewer example cmake top dir for clangd lsp.
#include "tinyusdz.hh"
#include "tydra/render-data.hh"
#include "tydra/scene-access.hh"

// local
#include "shader.hh"

constexpr auto kAttribPoints = "points";
constexpr auto kAttribNormals = "normals";
constexpr auto kAttribTexCoordBase = "texcoord_";
constexpr auto kAttribTexCoord0 = "texcoord_0";
constexpr auto kMaxTexCoords = 1; // TODO: multi texcoords

constexpr auto kUniformModelviewMatrix = "modelviewMatrix";
constexpr auto kUniformNormalMatrix = "normalMatrix";
constexpr auto kUniformProjectionMatrix = "projectionMatrix";

constexpr auto kUniformDiffuseTex = "diffuseTex";
constexpr auto kUniformDiffuseTexTransform = "diffuseTexTransform";
constexpr auto kUniformDiffuseTexScaleAndBias = "diffuseTexScaleAndBias"; // (sx, sy, bx, by)

constexpr auto kUniformNormaTex = "normalTex";
constexpr auto kUniformNormaTexTransform = "normalTexTransform";
constexpr auto kUniformNormalTexScaleAndBias = "normalTexScaleAndBias"; // (sx, sy, bx, by)

constexpr auto kUniformOcclusionTex = "occlusionlTex";
constexpr auto kUniformOcclusionTexTransform = "occlusionlTexTransform";
constexpr auto kUniformOcclusionTexScaleAndBias = "occlusionTexScaleAndBias"; // (sx, sy, bx, by)

// Embedded shaders
#include "shaders/no_skinning.vert_inc.hh"

#define CHECK_GL(tag) do { \
  GLenum err = glGetError(); \
  if (err != GL_NO_ERROR) { \
    std::cerr << "[" << tag << "] " << __FILE__ << ":" << __LINE__ << ":" << __func__ << " code " << std::to_string(int(err)) << "\n"; \
  } \
} while(0)

struct GLTexParams {
  std::map<std::string, GLint> uniforms;
  GLenum wrapS{GL_REPEAT};
  GLenum wrapT{GL_REPEAT};
  std::array<float, 4> borderCol{0.0f, 0.0f, 0.0f, 0.0f}; // transparent black
};

struct GLTexState {
  GLTexParams texParams;
};

// TODO: Use handle_id for key
struct GLMeshState {
  std::map<std::string, GLint> attribs;
  std::vector<GLuint> diffuseTexHandles;
  GLuint vertex_array_object{0}; // vertex array object
  GLuint num_triangles{0}; // up to 4GB triangles
};

struct GLVertexUniformState {
  GLint u_modelview{-1};
  GLint u_normal{-1}; // transpose(inverse(modelview))
  GLint u_perspective{-1};
};

struct GLProgramState {
  //std::map<std::string, GLint> uniforms;

  std::map<std::string, example::shader> shaders;
};


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

  int mouse_x = -1;
  int mouse_y = -1;

  bool mouse_left_down = false;
  bool shift_pressed = false;
  bool ctrl_pressed = false;
  bool tab_pressed = false;

  float curr_quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float prev_quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};

  std::array<float, 3> eye = {0.0f, 0.0f, 5.0f};
  std::array<float, 3> lookat = {0.0f, 0.0f, 0.0f};
  std::array<float, 3> up = {0.0f, 1.0f, 0.0f};

  example::Camera camera;
};

GUIContext gCtx;

// --- glfw ----------------------------------------------------

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

  // ctrl-q
  if ((key == GLFW_KEY_Q) && (action == GLFW_PRESS) &&
      (mods & GLFW_MOD_CONTROL)) {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
  }

  // esc
  if ((key == GLFW_KEY_ESCAPE) && (action == GLFW_PRESS)) {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
  }
}

static void mouse_move_callback(GLFWwindow* window, double x, double y) {
  auto param = reinterpret_cast<GUIContext*>(glfwGetWindowUserPointer(window));
  assert(param);

  if (param->mouse_left_down) {
    float w = static_cast<float>(param->width);
    float h = static_cast<float>(param->height);

    float x_offset = param->width - w;
    float y_offset = param->height - h;

    if (param->ctrl_pressed) {
      const float dolly_scale = 0.1f;
      param->eye[2] += dolly_scale * (param->mouse_y - static_cast<float>(y));
      param->lookat[2] +=
          dolly_scale * (param->mouse_y - static_cast<float>(y));
    } else if (param->shift_pressed) {
      const float trans_scale = 0.02f;
      param->eye[0] += trans_scale * (param->mouse_x - static_cast<float>(x));
      param->eye[1] -= trans_scale * (param->mouse_y - static_cast<float>(y));
      param->lookat[0] +=
          trans_scale * (param->mouse_x - static_cast<float>(x));
      param->lookat[1] -=
          trans_scale * (param->mouse_y - static_cast<float>(y));

    } else {
      // Adjust y.
      trackball(param->prev_quat,
                (2.f * (param->mouse_x - x_offset) - w) / static_cast<float>(w),
                (h - 2.f * (param->mouse_y - y_offset)) / static_cast<float>(h),
                (2.f * (static_cast<float>(x) - x_offset) - w) /
                    static_cast<float>(w),
                (h - 2.f * (static_cast<float>(y) - y_offset)) /
                    static_cast<float>(h));
      add_quats(param->prev_quat, param->curr_quat, param->curr_quat);
    }
  }

  param->mouse_x = static_cast<int>(x);
  param->mouse_y = static_cast<int>(y);
}

static void mouse_button_callback(GLFWwindow* window, int button, int action,
                                  int mods) {
  ImGuiIO& io = ImGui::GetIO();
  if (io.WantCaptureMouse || io.WantCaptureKeyboard) {
    return;
  }

  auto param = reinterpret_cast<GUIContext*>(glfwGetWindowUserPointer(window));
  assert(param);

  // left button
  if (button == 0) {
    if (action) {
      param->mouse_left_down = true;
      trackball(param->prev_quat, 0.0f, 0.0f, 0.0f, 0.0f);
    } else {
      param->mouse_left_down = false;
    }
  }
}

static void resize_callback(GLFWwindow* window, int width, int height) {
  auto param = reinterpret_cast<GUIContext*>(glfwGetWindowUserPointer(window));
  assert(param);

  param->width = width;
  param->height = height;
}

// ------------------------------------------------

namespace {

void SetVertexUniforms(GLVertexUniformState &state,
  tinyusdz::tydra::XformNode &xform_node,
  tinyusdz::value::matrix4f &perspective) {


  using namespace tinyusdz::value;

  // implicitly casts matrix4d to matrix4f;
  matrix4f worldmat = xform_node.get_world_matrix();
  matrix4d invtransmatd = tinyusdz::inverse(tinyusdz::upper_left_3x3_only(xform_node.get_world_matrix()));
  matrix4f invtransmat = invtransmatd;

  if (state.u_modelview > -1) {
    glUniformMatrix4fv(state.u_modelview, 1, /* transpose */false, &worldmat.m[0][0]);
  }

  if (state.u_normal > -1) {
    // TODO: transpose in GL-side?
    glUniformMatrix4fv(state.u_normal, 1,  /* transpose */false, &invtransmat.m[0][0]);
  }

  if (state.u_perspective > -1) {
    glUniformMatrix4fv(state.u_perspective, 1,  /* transpose */false, &perspective.m[0][0]);
  }
	
}

bool LoadShaders() {
  std::string s(reinterpret_cast<char *>(shaders_no_skinning_vert), shaders_no_skinning_vert_len);

  return true;
}

bool SetupShader() {

  GLuint prog_id;

  GLint modelv_loc = glGetUniformLocation(prog_id, kUniformModelviewMatrix);
  if (modelv_loc < 0) {
    std::cerr << kUniformModelviewMatrix << " not found in the vertex shader.\n";
    return false;
  }

  GLint norm_loc = glGetUniformLocation(prog_id, kUniformNormalMatrix);
  if (norm_loc < 0) {
    std::cerr << kUniformNormalMatrix << " not found in the vertex shader.\n";
    return false;
  }

  GLint proj_loc = glGetUniformLocation(prog_id, kUniformProjectionMatrix);
  if (proj_loc < 0) {
    std::cerr << kUniformProjectionMatrix << " not found in the vertex shader.\n";
    return false;
  }

  return true;
}

bool SetupTexture(const tinyusdz::tydra::RenderScene& scene,
                  tinyusdz::tydra::UVTexture& tex) {
  auto glwrapmode = [](const tinyusdz::tydra::UVTexture::WrapMode mode) {
    if (mode == tinyusdz::tydra::UVTexture::WrapMode::CLAMP_TO_EDGE) {
      return GL_CLAMP_TO_EDGE;
    } else if (mode == tinyusdz::tydra::UVTexture::WrapMode::REPEAT) {
      return GL_REPEAT;
    } else if (mode == tinyusdz::tydra::UVTexture::WrapMode::MIRROR) {
      return GL_MIRRORED_REPEAT;
    } else if (mode == tinyusdz::tydra::UVTexture::WrapMode::CLAMP_TO_BORDER) {
      return GL_CLAMP_TO_BORDER;
    }
    // Just in case: Fallback to REPEAT
    return GL_REPEAT;
  };

  GLTexState texState;
  GLTexParams texParams;

  texParams.wrapS = glwrapmode(tex.wrapS);
  texParams.wrapT = glwrapmode(tex.wrapT);

  GLuint texid{0};
  glGenTextures(1, &texid);

  glBindTexture(GL_TEXTURE_2D, texid);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, texParams.wrapS);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, texParams.wrapT);

  // For `black` wrap mode.
  // Not sure what aplha color should be for USD's `black` wrap mode.
  // There are two possibilities: opaque black or transparent black.
  // We use fully transparent(0.0) for a while.
  texParams.borderCol = {0.0f, 0.0f, 0.0f, 0.0f};
  glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, &texParams.borderCol[0]);
  CHECK_GL("texture_id[" << std::to_string(tex.texture_image_id) << "] glTexParameters");

  texState.texParams = std::move(texParams);

  int64_t image_id = tex.texture_image_id;

  if (image_id >= 0) {
    if (image_id < scene.images.size()) {
      const tinyusdz::tydra::TextureImage& image =
          scene.images[size_t(image_id)];

      if ((image.width < 1) || (image.height < 1) || (image.channels < 1)) {
        std::cerr << "Texture image is not loaded(texture file not found?).\n";
        glBindTexture(GL_TEXTURE_2D, 0);
        return false;
      }

      uint32_t bytesperpixel = 1;

      GLenum format{GL_LUMINANCE};
      if (image.channels == 1) {
        format = GL_LUMINANCE;
        bytesperpixel = 1;
      } else if (image.channels == 2) {
        format = GL_LUMINANCE_ALPHA;
        bytesperpixel = 2;
      } else if (image.channels == 3) {
        format = GL_RGB;
        bytesperpixel = 3;
      } else if (image.channels == 4) {
        format = GL_RGBA;
        bytesperpixel = 4;
      }

      GLenum type{GL_BYTE};
      if (image.texelComponentType == tinyusdz::tydra::ComponentType::UInt8) {
        type = GL_BYTE;
        bytesperpixel *= 1;
      } else if (image.texelComponentType ==
                 tinyusdz::tydra::ComponentType::Half) {
        type = GL_SHORT;
        bytesperpixel *= 2;
      } else if (image.texelComponentType ==
                 tinyusdz::tydra::ComponentType::UInt32) {
        type = GL_UNSIGNED_INT;
        bytesperpixel *= 4;
      } else if (image.texelComponentType ==
                 tinyusdz::tydra::ComponentType::Float) {
        type = GL_FLOAT;
        bytesperpixel *= 4;
      } else {
        std::cout << "Unsupported texelComponentType: "
                  << tinyusdz::tydra::to_string(image.texelComponentType)
                  << "\n";
      }

      int64_t buffer_id = image.buffer_id;
      if ((buffer_id >= 0) && (buffer_id < scene.buffers.size())) {
        const tinyusdz::tydra::BufferData& buffer =
            scene.buffers[size_t(buffer_id)];

        // byte length check.
        if (size_t(image.width) * size_t(image.height) * size_t(bytesperpixel) >
            buffer.data.size()) {
          std::cerr << "Insufficient texel data. : "
                    << "width: " << image.width << ", height " << image.height
                    << ", bytesperpixel " << bytesperpixel
                    << ", requested bytes: "
                    << size_t(image.width) * size_t(image.height) *
                           size_t(bytesperpixel)
                    << ", buffer bytes: " << std::to_string(buffer.data.size())
                    << "\n";
          // continue anyway
        } else {
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0,
                      format, type, buffer.data.data());
          CHECK_GL("texture_id[" << std::to_string(image_id) << "] glTexImage2D");
        }
      }
    }
  }

  tex.handle = texid;

  glBindTexture(GL_TEXTURE_2D, 0);

  return true;
}

static bool SetupMesh(
  tinyusdz::tydra::RenderMesh &mesh,
  GLuint program_id,
  GLMeshState &gl_state) // [out]
{
  std::vector<uint32_t> indices;

  if (mesh.faceVertexCounts.empty()) {
    // assume all triangulaged faces.
    if ((mesh.faceVertexIndices.size() % 3) != 0) {
      std::cerr << "mesh <" << mesh.abs_name << ">  faceVertexIndices.size " << std::to_string(mesh.faceVertexIndices.size()) << " must be multiple of 3\n";
    }

    for (size_t f = 0; f < mesh.faceVertexIndices.size() / 3; f++) {
      for (size_t c = 0; c < 3; c++ ) {
        indices.push_back(mesh.faceVertexIndices[3 * f + c]);
      }
    }
  } else {
    size_t faceOffset = 0;

    // Currently all faces must be triangle.
    for (size_t f = 0; f < mesh.faceVertexCounts.size(); f++) {
      if (mesh.faceVertexCounts[f] != 3) {
        std::cerr << "mesh <" << mesh.abs_name << ">  Non triangle face found at faceVertexCounts[" << f << "] (" << mesh.faceVertexCounts[f] << ")\n";
        return false;
      }

      for (size_t c = 0; c < mesh.faceVertexCounts[f]; c++ ) {
        indices.push_back(mesh.faceVertexIndices[faceOffset + c]);
      }

    faceOffset += f;
    }
  }

  glGenVertexArrays(1, &gl_state.vertex_array_object);
  CHECK_GL(mesh.abs_name << "GenVertexArrays");

  glBindVertexArray(gl_state.vertex_array_object);
  CHECK_GL(mesh.abs_name << "BindVertexArray");

  //
  // Current settings
  // - position
  // - normals
  // - texcoords0
  //
  // all vertex attribs are represented as facevarying data.
  //
  // - Static mesh(STATIC_DRAW) only

  { // position

    // expand position to facevarying data.
    // assume faces are all triangle.
    std::vector<tinyusdz::tydra::vec3> facevaryingVertices;
    facevaryingVertices.resize(indices.size() / 3);
    gl_state.num_triangles = indices.size() / 3;

    for (size_t i = 0; i < indices.size() / 3; i++) {

      size_t vi0 = indices[3 * i + 0];
      size_t vi1 = indices[3 * i + 1];
      size_t vi2 = indices[3 * i + 2];

      if (vi0 >= mesh.points.size()) {
        std::cerr << "indices[" << (3 * i + 0) << "(" << vi0 << ") exceeds mesh.points.size()(" << mesh.points.size() << ")\n";
        return false;
      }

      if (vi1 >= mesh.points.size()) {
        std::cerr << "indices[" << (3 * i + 1) << "(" << vi1 << ") exceeds mesh.points.size()(" << mesh.points.size() << ")\n";
        return false;
      }

      if (vi2 >= mesh.points.size()) {
        std::cerr << "indices[" << (3 * i + 2) << "(" << vi2 << ") exceeds mesh.points.size()(" << mesh.points.size() << ")\n";
        return false;
      }

      facevaryingVertices.push_back(mesh.points[vi0]);
      facevaryingVertices.push_back(mesh.points[vi1]);
      facevaryingVertices.push_back(mesh.points[vi2]);
    }
 
    GLuint vb;
    glGenBuffers(1, &vb);
    glBindBuffer(GL_ARRAY_BUFFER, vb);
    glBufferData(
        GL_ARRAY_BUFFER,
        facevaryingVertices.size() * sizeof(tinyusdz::tydra::vec3),
        facevaryingVertices.data(),
        GL_STATIC_DRAW
    );
    CHECK_GL("Set facevaryingVertices buffer data");

    GLint loc = glGetAttribLocation(program_id, kAttribPoints);

    if (loc > -1) {
      glEnableVertexAttribArray(loc);
      glVertexAttribPointer(
          loc,
          3,
          GL_FLOAT,
          GL_FALSE,
          /* stride */sizeof(GLfloat) * 3,
          0
      );
      CHECK_GL("VertexAttribPointer");
    } else {
      std::cerr << kAttribPoints << " attribute not found in vertex shader.\n";
      return false;
    }
  }

  if (mesh.facevaryingNormals.size()) { // normals
    GLuint vb;
    glGenBuffers(1, &vb);
    glBindBuffer(GL_ARRAY_BUFFER, vb);
    glBufferData(
        GL_ARRAY_BUFFER,
        mesh.facevaryingNormals.size() * sizeof(tinyusdz::tydra::vec3),
        mesh.facevaryingNormals.data(),
        GL_STATIC_DRAW
    );
    CHECK_GL("Set facevaryingNormals buffer data");

    GLint loc = glGetAttribLocation(program_id, kAttribNormals);

    if (loc > -1) {
      glEnableVertexAttribArray(loc);
      glVertexAttribPointer(
          loc,
          3,
          GL_FLOAT,
          GL_FALSE,
          /* stride */sizeof(GLfloat) * 3,
          0
      );
      CHECK_GL("VertexAttribPointer");
    } else {
      std::cerr << kAttribNormals << " attribute not found in vertex shader.\n";
      exit(-1);
    }
  }

  // texcoords0 only
  // TODO: multi texcoords
  if (mesh.facevaryingTexcoords.size() == 1) {
    for (const auto it : mesh.facevaryingTexcoords) {
      uint32_t slot_id = it.first;
      if (slot_id >= kMaxTexCoords) {
        std::cerr << "Texcoord slot id " << slot_id << " must be less than kMaxTexCoords " << kMaxTexCoords << "\n";
        return false;
      }

      GLuint vb;
      glGenBuffers(1, &vb);
      glBindBuffer(GL_ARRAY_BUFFER, vb);
      glBufferData(
          GL_ARRAY_BUFFER,
          it.second.size() * sizeof(tinyusdz::tydra::vec2),
          it.second.data(),
          GL_STATIC_DRAW
      );
      CHECK_GL("Set facevaryingTexcoord0 buffer data");

      std::string texattr = kAttribTexCoordBase + std::to_string(slot_id);
      GLint loc = glGetAttribLocation(program_id, texattr.c_str());

      if (loc > -1) {
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(
            loc,
            2,
            GL_FLOAT,
            GL_FALSE,
            /* stride */sizeof(GLfloat) * 2,
            0
        );
        CHECK_GL("VertexAttribPointer");
      } else {
        std::cerr << texattr << " attribute not found in vertex shader.\n";
        return false;
      }
    }
  }

#if 0 
  // Build index buffer.
  GLuint elementbuffer;
  glGenBuffers(1, &elementbuffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementbuffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);
  CHECK_GL(mesh.abs_name << "ElementArrayBuffer");
#endif

  glBindVertexArray(0);
  CHECK_GL(mesh.abs_name << "UnBind VAO");

  return true;
}

static void DrawMesh(tinyusdz::tydra::RenderMesh& mesh,
  const GLMeshState &gl_state) {

  glBindVertexArray(gl_state.vertex_array_object);
  // vertices are facevarying + triangles, so simply to call glDrawArrays.
  glDrawArrays(GL_TRIANGLES, 0, gl_state.num_triangles * 3);
  CHECK_GL("DrawArrays");
  glBindVertexArray(0);

#if 0
  // poisition
  if (gl_state.attribs.count(kAttribPoints)) {
    GLint idx = gl_state.attribs.at(kAttribPoints);
    if (idx > -1) {
      glBindBuffer(GL_ARRAY_BUFFER, idx);
      glEnableVertexAttribArray(idx);
      glVertexAttribPointer(idx, 3, GL_FLOAT, GL_FALSE, 0, 0); // vec3
    }
  }

  if (gl_state.attribs[kAttribNormal] > -1) {
    GLint idx = gl_state.attribs[kAttribNormal];
    glBindBuffer(GL_ARRAY_BUFFER, idx);
    glEnableVertexAttribArray(idx);
    glVertexAttribPointer(idx, 3, GL_FLOAT, GL_FALSE, 0, 0); // vec3, assume normal vector is already normalzed.
  }

  if (gl_state.attribs[kAttrixTexCoord0] > -1) {
    GLint idx = gl_state.attribs[kAttribTexCoord0];
    glBindBuffer(GL_ARRAY_BUFFER, idx);
    glEnableVertexAttribArray(idx);
    glVertexAttribPointer(idx, 2, GL_FLOAT, GL_FALSE, 0, 0); // vec2
  }

  GLsizei ntriangles = 0; // TODO
  glDrawArrays(GL_TRIANGLES, 0, ntriangles);

  for (const auto &attrib : program_state.attribs) {
    if (attrib.second >= 0) {
      glDisableVertexAttribArray(attrib.second);
    }
  }
#else
  // TODO
  //glBindVertexArray
#endif

}

static void DrawScene(
  const example::shader shader,
  const tinyusdz::tydra::RenderScene &scene) {
  //
  // Use single shader for the scene
  //

  // bind program
  shader.use();
  CHECK_GL("shader.use");


  glUseProgram(0);
  CHECK_GL("glUseProgram(0)");
}
#if 0
static void DrawNode(const tinyusdz::Scene& scene, const tinyusdz::Node& node) {
  if (node.type == tinyusdz::NODE_TYPE_XFORM) {
    const tinyusdz::Xform& xform = scene.xforms.at(node.index);
    glPushMatrix();

    tinyusdz::Matrix4d matrix = xform.GetMatrix();
    glMultMatrixd(reinterpret_cast<const double*>(&(matrix.m)));
  }

  for (const auto& child : node.children) {
    if ((child.index >= 0) && (child.index < scene.nodes.size())) {
      DrawNode(scene, scene.nodes.at(size_t(child.index)));
    }
  }

  if (node.type == tinyusdz::NODE_TYPE_XFORM) {
    glPopMatrix();
  }
}

#endif

static void ProcScene(const tinyusdz::Stage& stage) {
  //
  // Stage to Renderable Scene
  tinyusdz::tydra::RenderSceneConverter converter;

  // TODO
}

}  // namespace

int main(int argc, char** argv) {

  // Setup window
  glfwSetErrorCallback(error_callback);
  if (!glfwInit()) {
    exit(EXIT_FAILURE);
  }

  // Decide GL+GLSL versions
  #if defined(IMGUI_IMPL_OPENGL_ES2)
      // GL ES 2.0 + GLSL 100
      const char* glsl_version = "#version 100";
      glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
      glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
      glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
  #elif defined(__APPLE__)
      // GL 3.2 + GLSL 150
      const char* glsl_version = "#version 150";
      glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
      glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
      glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
      glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
  #else
      // GL 3.0 + GLSL 130
      const char* glsl_version = "#version 130";
      glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
      glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
      //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
      //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
  #endif

#ifdef _DEBUG_OPENGL
  glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

  std::string filename = "../../../models/suzanne.usdc";

  if (argc > 1) {
    filename = std::string(argv[1]);
  }

  std::cout << "Loading USD file " << filename << "\n";

  std::string warn;
  std::string err;
  tinyusdz::Stage stage;

  bool ret = tinyusdz::LoadUSDFromFile(filename, &stage, &warn, &err);
  if (!warn.empty()) {
    std::cerr << "WARN : " << warn << "\n";
    return EXIT_FAILURE;
  }
  if (!err.empty()) {
    std::cerr << "ERR : " << err << "\n";
    return EXIT_FAILURE;
  }

  ProcScene(stage);


  GLFWwindow* window{nullptr};
  window = glfwCreateWindow(gCtx.width, gCtx.height, "Simple USDZ GL viewer",
                            nullptr, nullptr);
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1); // vsync on

  if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
    std::cerr << "Failed to load OpenGL functions with gladLoadGL\n";
    exit(EXIT_FAILURE);
  }

  std::cout << "OpenGL " << GLVersion.major << '.' << GLVersion.minor << '\n';

  if (GLVersion.major < 2) {
    std::cerr << "OpenGL 2. or later should be available." << std::endl;
    exit(EXIT_FAILURE);
  }

#ifdef _DEBUG_OPENGL
  glEnable(GL_DEBUG_OUTPUT);
  glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  glDebugMessageCallback(reinterpret_cast<GLDEBUGPROC>(glDebugOutput), nullptr);
  glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr,
                        GL_TRUE);
#endif

  GUIContext gui_ctx;

  glfwSetWindowUserPointer(window, &gui_ctx);
  glfwSetKeyCallback(window, key_callback);
  glfwSetCursorPosCallback(window, mouse_move_callback);
  glfwSetMouseButtonCallback(window, mouse_button_callback);
  glfwSetFramebufferSizeCallback(window, resize_callback);

  bool done = false;

  ImGui::CreateContext();

  ImGui::StyleColorsDark();

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  int display_w, display_h;
  ImVec4 clear_color = {0.1f, 0.18f, 0.3f, 1.0f};

  while (!done) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Info");
    ImGui::Text("View control");
    ImGui::Text("ctrl + left mouse");
    ImGui::Text("shift + left mouse");
    ImGui::Text("left mouse");
    ImGui::End();

    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

#if 0
    // Draw scene
    if ((scene.default_root_node >= 0) && (scene.default_root_node < scene.nodes.size())) {
      DrawNode(scene, scene.nodes[scene.default_root_node]);
    } else {
      static bool printed = false;
      if (!printed) {
        std::cerr << "USD scene does not contain root node. or has invalid root node ID\n";
        std::cerr << "  # of nodes in the scene: " << scene.nodes.size() << "\n";
        std::cerr << "  scene.default_root_node: " << scene.default_root_node << "\n";
        printed = true;
      }
    }
#endif

    // Imgui

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

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
      sprintf(title, "Simple GL USDC/USDA/USDZ viewer [%dFPS]", frameCount);
      glfwSetWindowTitle(window, title);
      frameCount = 0;
      previousTime = currentTime;
    }

    glfwSwapBuffers(window);

    done = glfwWindowShouldClose(window);
  };

  std::cout << "Close window\n";

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return EXIT_SUCCESS;
}
