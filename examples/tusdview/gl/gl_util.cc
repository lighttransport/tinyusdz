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
      const char* tag = "[shader] ";
      if (type == GL_VERTEX_SHADER) tag = "[vertex] ";
      else if (type == GL_FRAGMENT_SHADER) tag = "[fragment] ";
#ifdef GL_TESS_CONTROL_SHADER
      else if (type == GL_TESS_CONTROL_SHADER) tag = "[tess-control] ";
      else if (type == GL_TESS_EVALUATION_SHADER) tag = "[tess-eval] ";
#endif
      *err += tag;
      *err += log.data();
      *err += "\n";
    }
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

GLuint LinkProgram(const GLuint* shaders, int count, std::string* err) {
  GLuint prog = glCreateProgram();
  for (int i = 0; i < count; ++i) glAttachShader(prog, shaders[i]);
  glLinkProgram(prog);
  for (int i = 0; i < count; ++i) glDeleteShader(shaders[i]);
  GLint ok = GL_FALSE;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    GLint len = 0;
    glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
    std::vector<char> log(static_cast<size_t>(len > 1 ? len : 1));
    glGetProgramInfoLog(prog, len, nullptr, log.data());
    if (err) { *err += "[link] "; *err += log.data(); *err += "\n"; }
    glDeleteProgram(prog);
    return 0;
  }
  return prog;
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
  GLuint shaders[2] = {vs, fs};
  return LinkProgram(shaders, 2, err);
}

GLuint CompileProgramGeom(const char* vsSrc, const char* gsSrc, const char* fsSrc,
                          std::string* err) {
#if defined(GL_GEOMETRY_SHADER)
  GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc, err);
  if (!vs) return 0;
  GLuint gs = CompileShader(GL_GEOMETRY_SHADER, gsSrc, err);
  if (!gs) { glDeleteShader(vs); return 0; }
  GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc, err);
  if (!fs) { glDeleteShader(vs); glDeleteShader(gs); return 0; }
  GLuint shaders[3] = {vs, gs, fs};
  return LinkProgram(shaders, 3, err);
#else
  (void)vsSrc; (void)gsSrc; (void)fsSrc;
  if (err) *err += "geometry shaders unsupported by this GL header\n";
  return 0;
#endif
}

GLuint CompileProgramTess(const char* vsSrc, const char* tcsSrc, const char* tesSrc,
                          const char* fsSrc, std::string* err) {
#if defined(GL_TESS_CONTROL_SHADER) && defined(GL_TESS_EVALUATION_SHADER)
  GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc, err);
  if (!vs) return 0;
  GLuint tcs = CompileShader(GL_TESS_CONTROL_SHADER, tcsSrc, err);
  if (!tcs) { glDeleteShader(vs); return 0; }
  GLuint tes = CompileShader(GL_TESS_EVALUATION_SHADER, tesSrc, err);
  if (!tes) { glDeleteShader(vs); glDeleteShader(tcs); return 0; }
  GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc, err);
  if (!fs) { glDeleteShader(vs); glDeleteShader(tcs); glDeleteShader(tes); return 0; }
  GLuint shaders[4] = {vs, tcs, tes, fs};
  return LinkProgram(shaders, 4, err);
#else
  (void)vsSrc; (void)tcsSrc; (void)tesSrc; (void)fsSrc;
  if (err) *err += "tessellation shaders unsupported by this GL header\n";
  return 0;
#endif
}

}  // namespace glutil
}  // namespace tusdview
