// SPDX-License-Identifier: Apache-2.0
// tusdview - minimal OpenGL helpers (shader compile/link).
#pragma once

#include <glad/glad.h>

#include <string>

namespace tusdview {
namespace glutil {

// Compile + link a vertex/fragment program. Returns 0 and fills `err` on failure.
GLuint CompileProgram(const char* vsSrc, const char* fsSrc, std::string* err);

}  // namespace glutil
}  // namespace tusdview
