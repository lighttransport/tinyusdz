// SPDX-License-Identifier: Apache-2.0
//
// lusdquicklook — headless OpenGL context creation.
//
// Deliberately offscreen. lightui is a software-blit UI with no GL window path
// and no native-handle accessor, so rather than patching the vendored UI, the
// GL backend renders into an FBO and reads back into the same blit-ready buffer
// the CPU tracer produces. One UI code path, no changes to lightui, and it
// works on a machine with no compositor.
//
// Every platform library is opened at runtime (dlopen / LoadLibrary): a machine
// without libEGL must fall back to the CPU renderer, never fail to start.
#pragma once

#include <memory>
#include <string>

namespace lusdql {

// Function-pointer loader for GL entry points, valid while the context is
// current.
using GlGetProcFn = void* (*)(const char* name);

class GlContext {
 public:
  virtual ~GlContext() = default;
  virtual bool MakeCurrent() = 0;
  virtual GlGetProcFn GetProcLoader() = 0;
  virtual const char* Name() const = 0;
};

// Returns nullptr (with a reason in `err`) when no headless GL context can be
// created. That is an expected outcome, not an error worth surfacing loudly.
std::unique_ptr<GlContext> CreateHeadlessGlContext(std::string* err);

}  // namespace lusdql
