// SPDX-License-Identifier: Apache-2.0
#include "gl/gl_util.hh"

#include <vector>

namespace tusdview {
namespace glutil {

namespace {

GLuint CompileShader(GLenum type, const char* src, std::string* err) {
  GLuint sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);
  GLint ok = GL_FALSE;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    GLint len = 0;
    glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
    std::vector<char> log(static_cast<size_t>(len > 1 ? len : 1));
    glGetShaderInfoLog(sh, len, nullptr, log.data());
    if (err) {
      *err += (type == GL_VERTEX_SHADER ? "[vertex] " : "[fragment] ");
      *err += log.data();
      *err += "\n";
    }
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

}  // namespace

GLuint CompileProgram(const char* vsSrc, const char* fsSrc, std::string* err) {
  GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc, err);
  if (!vs) return 0;
  GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc, err);
  if (!fs) {
    glDeleteShader(vs);
    return 0;
  }
  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glLinkProgram(prog);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = GL_FALSE;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    GLint len = 0;
    glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
    std::vector<char> log(static_cast<size_t>(len > 1 ? len : 1));
    glGetProgramInfoLog(prog, len, nullptr, log.data());
    if (err) {
      *err += "[link] ";
      *err += log.data();
      *err += "\n";
    }
    glDeleteProgram(prog);
    return 0;
  }
  return prog;
}

}  // namespace glutil
}  // namespace tusdview
