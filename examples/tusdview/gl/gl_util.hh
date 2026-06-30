// SPDX-License-Identifier: Apache-2.0
// tusdview - minimal OpenGL helpers (shader compile/link).
#pragma once

#include <glad/glad.h>

#include <string>

namespace tusdview {
namespace glutil {

// Compile + link a vertex/fragment program. Returns 0 and fills `err` on failure.
GLuint CompileProgram(const char* vsSrc, const char* fsSrc, std::string* err);

// Compile + link a vertex / geometry / fragment program (e.g. screen-space line
// expansion for anti-aliased wireframe). Returns 0 and fills `err` on failure.
GLuint CompileProgramGeom(const char* vsSrc, const char* gsSrc, const char* fsSrc,
                          std::string* err);

// Compile + link a vertex / tessellation-control / tessellation-evaluation /
// fragment program (needs an OpenGL >= 4.0 context). Returns 0 and fills `err` on
// failure (e.g. when tessellation stages are unsupported).
GLuint CompileProgramTess(const char* vsSrc, const char* tcsSrc, const char* tesSrc,
                          const char* fsSrc, std::string* err);

}  // namespace glutil
}  // namespace tusdview
