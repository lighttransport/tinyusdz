// SPDX-License-Identifier: Apache-2.0
//
// Headless GL context via EGL's surfaceless platform (Linux/BSD), plus a WGL
// pbuffer on Windows and a CGL pixel-format context on macOS.
//
// All three load their platform library at runtime, so a build produced on a
// machine with EGL still runs on one without it — it just reports failure and
// the app uses the CPU renderer.
#include "render/gl_context.hh"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <cstring>

namespace tusdql {

namespace {

#if defined(_WIN32)

// ---------------------------------------------------------------------------
// Windows: a 1x1 hidden window + WGL context. Simpler and far more portable
// across drivers than pbuffers, and we only ever draw into an FBO anyway.
// ---------------------------------------------------------------------------

using PFN_wglCreateContext = HGLRC(WINAPI*)(HDC);
using PFN_wglMakeCurrent = BOOL(WINAPI*)(HDC, HGLRC);
using PFN_wglDeleteContext = BOOL(WINAPI*)(HGLRC);
using PFN_wglGetProcAddress = PROC(WINAPI*)(LPCSTR);

class WglContext : public GlContext {
 public:
  ~WglContext() override {
    if (gl_ && ctx_ && make_current_) make_current_(nullptr, nullptr);
    if (gl_ && ctx_ && delete_context_) delete_context_(ctx_);
    if (dc_ && hwnd_) ReleaseDC(hwnd_, dc_);
    if (hwnd_) DestroyWindow(hwnd_);
    if (gl_) FreeLibrary(gl_);
  }

  bool Init(std::string* err) {
    gl_ = LoadLibraryA("opengl32.dll");
    if (!gl_) {
      *err = "opengl32.dll not available";
      return false;
    }
    create_context_ =
        reinterpret_cast<PFN_wglCreateContext>(GetProcAddress(gl_, "wglCreateContext"));
    make_current_ =
        reinterpret_cast<PFN_wglMakeCurrent>(GetProcAddress(gl_, "wglMakeCurrent"));
    delete_context_ =
        reinterpret_cast<PFN_wglDeleteContext>(GetProcAddress(gl_, "wglDeleteContext"));
    get_proc_ = reinterpret_cast<PFN_wglGetProcAddress>(
        GetProcAddress(gl_, "wglGetProcAddress"));
    if (!create_context_ || !make_current_ || !delete_context_ || !get_proc_) {
      *err = "opengl32.dll is missing WGL entry points";
      return false;
    }

    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "tusdquicklookGL";
    RegisterClassA(&wc);  // duplicate registration is harmless

    hwnd_ = CreateWindowA("tusdquicklookGL", "", WS_OVERLAPPEDWINDOW, 0, 0, 1, 1,
                          nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd_) {
      *err = "failed to create the hidden GL window";
      return false;
    }
    dc_ = GetDC(hwnd_);
    if (!dc_) {
      *err = "failed to get a device context";
      return false;
    }

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    const int pf = ChoosePixelFormat(dc_, &pfd);
    if (!pf || !SetPixelFormat(dc_, pf, &pfd)) {
      *err = "no suitable pixel format";
      return false;
    }

    ctx_ = create_context_(dc_);
    if (!ctx_) {
      *err = "wglCreateContext failed";
      return false;
    }
    return true;
  }

  bool MakeCurrent() override { return make_current_(dc_, ctx_) != FALSE; }

  GlGetProcFn GetProcLoader() override {
    return [](const char* name) -> void* {
      // WGL only returns extension functions; core 1.1 entry points must come
      // from the DLL itself.
      static HMODULE gl = LoadLibraryA("opengl32.dll");
      void* p = nullptr;
      static PFN_wglGetProcAddress wgl_get =
          gl ? reinterpret_cast<PFN_wglGetProcAddress>(
                   GetProcAddress(gl, "wglGetProcAddress"))
             : nullptr;
      if (wgl_get) p = reinterpret_cast<void*>(wgl_get(name));
      if (!p && gl) p = reinterpret_cast<void*>(GetProcAddress(gl, name));
      return p;
    };
  }

  const char* Name() const override { return "wgl"; }

 private:
  HMODULE gl_ = nullptr;
  HWND hwnd_ = nullptr;
  HDC dc_ = nullptr;
  HGLRC ctx_ = nullptr;
  PFN_wglCreateContext create_context_ = nullptr;
  PFN_wglMakeCurrent make_current_ = nullptr;
  PFN_wglDeleteContext delete_context_ = nullptr;
  PFN_wglGetProcAddress get_proc_ = nullptr;
};

#elif defined(__APPLE__)

// ---------------------------------------------------------------------------
// macOS: CGL. Written but untested — no Mac in the loop for this work.
// ---------------------------------------------------------------------------

using CGLPixelFormatObj = void*;
using CGLContextObj = void*;
using CGLError = int;

using PFN_CGLChoosePixelFormat = CGLError (*)(const int*, CGLPixelFormatObj*, int*);
using PFN_CGLCreateContext = CGLError (*)(CGLPixelFormatObj, CGLContextObj,
                                          CGLContextObj*);
using PFN_CGLSetCurrentContext = CGLError (*)(CGLContextObj);
using PFN_CGLDestroyContext = CGLError (*)(CGLContextObj);
using PFN_CGLDestroyPixelFormat = CGLError (*)(CGLPixelFormatObj);

class CglContext : public GlContext {
 public:
  ~CglContext() override {
    if (set_current_) set_current_(nullptr);
    if (destroy_ctx_ && ctx_) destroy_ctx_(ctx_);
    if (destroy_pf_ && pf_) destroy_pf_(pf_);
    if (lib_) dlclose(lib_);
    if (gl_lib_) dlclose(gl_lib_);
  }

  bool Init(std::string* err) {
    lib_ = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY);
    if (!lib_) {
      *err = "OpenGL.framework not available";
      return false;
    }
    gl_lib_ = lib_;
    auto sym = [&](const char* n) { return dlsym(lib_, n); };
    auto choose = reinterpret_cast<PFN_CGLChoosePixelFormat>(sym("CGLChoosePixelFormat"));
    auto create = reinterpret_cast<PFN_CGLCreateContext>(sym("CGLCreateContext"));
    set_current_ = reinterpret_cast<PFN_CGLSetCurrentContext>(sym("CGLSetCurrentContext"));
    destroy_ctx_ = reinterpret_cast<PFN_CGLDestroyContext>(sym("CGLDestroyContext"));
    destroy_pf_ =
        reinterpret_cast<PFN_CGLDestroyPixelFormat>(sym("CGLDestroyPixelFormat"));
    if (!choose || !create || !set_current_) {
      *err = "CGL entry points missing";
      return false;
    }

    // kCGLPFAAccelerated = 73, kCGLPFAOpenGLProfile = 99,
    // kCGLOGLPVersion_GL3_Core = 0x3200.
    const int attribs[] = {73, 99, 0x3200, 0};
    int npix = 0;
    if (choose(attribs, &pf_, &npix) != 0 || !pf_) {
      *err = "CGLChoosePixelFormat failed";
      return false;
    }
    if (create(pf_, nullptr, &ctx_) != 0 || !ctx_) {
      *err = "CGLCreateContext failed";
      return false;
    }
    return true;
  }

  bool MakeCurrent() override { return set_current_(ctx_) == 0; }

  GlGetProcFn GetProcLoader() override {
    return [](const char* name) -> void* {
      static void* gl = dlopen(
          "/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY);
      return gl ? dlsym(gl, name) : nullptr;
    };
  }

  const char* Name() const override { return "cgl"; }

 private:
  void* lib_ = nullptr;
  void* gl_lib_ = nullptr;
  CGLPixelFormatObj pf_ = nullptr;
  CGLContextObj ctx_ = nullptr;
  PFN_CGLSetCurrentContext set_current_ = nullptr;
  PFN_CGLDestroyContext destroy_ctx_ = nullptr;
  PFN_CGLDestroyPixelFormat destroy_pf_ = nullptr;
};

#else

// ---------------------------------------------------------------------------
// Linux/BSD: EGL surfaceless. No X connection required, so this works over ssh
// and in CI containers.
// ---------------------------------------------------------------------------

using EGLDisplay = void*;
using EGLConfig = void*;
using EGLContext = void*;
using EGLSurface = void*;
using EGLBoolean = unsigned int;
using EGLint = int;
using EGLenum = unsigned int;

constexpr EGLenum EGL_PLATFORM_SURFACELESS = 0x31DD;
constexpr EGLint EGL_NONE = 0x3038;
constexpr EGLint EGL_SURFACE_TYPE = 0x3033;
constexpr EGLint EGL_PBUFFER_BIT = 0x0001;
constexpr EGLint EGL_RENDERABLE_TYPE = 0x3040;
constexpr EGLint EGL_OPENGL_BIT = 0x0008;
constexpr EGLenum EGL_OPENGL_API = 0x30A2;
constexpr EGLint EGL_CONTEXT_MAJOR_VERSION = 0x3098;
constexpr EGLint EGL_CONTEXT_MINOR_VERSION = 0x30FB;
constexpr EGLint EGL_CONTEXT_OPENGL_PROFILE_MASK = 0x30FD;
constexpr EGLint EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT = 0x00000001;

using PFN_eglGetPlatformDisplay = EGLDisplay (*)(EGLenum, void*, const EGLint*);
using PFN_eglGetDisplay = EGLDisplay (*)(void*);
using PFN_eglInitialize = EGLBoolean (*)(EGLDisplay, EGLint*, EGLint*);
using PFN_eglTerminate = EGLBoolean (*)(EGLDisplay);
using PFN_eglChooseConfig = EGLBoolean (*)(EGLDisplay, const EGLint*, EGLConfig*,
                                           EGLint, EGLint*);
using PFN_eglBindAPI = EGLBoolean (*)(EGLenum);
using PFN_eglCreateContext = EGLContext (*)(EGLDisplay, EGLConfig, EGLContext,
                                            const EGLint*);
using PFN_eglDestroyContext = EGLBoolean (*)(EGLDisplay, EGLContext);
using PFN_eglMakeCurrent = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface,
                                          EGLContext);
using PFN_eglGetProcAddress = void* (*)(const char*);

class EglContext : public GlContext {
 public:
  ~EglContext() override {
    if (make_current_ && display_) make_current_(display_, nullptr, nullptr, nullptr);
    if (destroy_context_ && display_ && ctx_) destroy_context_(display_, ctx_);
    if (terminate_ && display_) terminate_(display_);
    if (lib_) dlclose(lib_);
  }

  bool Init(std::string* err) {
    for (const char* name : {"libEGL.so.1", "libEGL.so"}) {
      lib_ = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
      if (lib_) break;
    }
    if (!lib_) {
      *err = "libEGL not available";
      return false;
    }

    auto sym = [&](const char* n) { return dlsym(lib_, n); };
    auto get_platform_display =
        reinterpret_cast<PFN_eglGetPlatformDisplay>(sym("eglGetPlatformDisplay"));
    auto get_display = reinterpret_cast<PFN_eglGetDisplay>(sym("eglGetDisplay"));
    auto initialize = reinterpret_cast<PFN_eglInitialize>(sym("eglInitialize"));
    auto choose_config = reinterpret_cast<PFN_eglChooseConfig>(sym("eglChooseConfig"));
    auto bind_api = reinterpret_cast<PFN_eglBindAPI>(sym("eglBindAPI"));
    auto create_context = reinterpret_cast<PFN_eglCreateContext>(sym("eglCreateContext"));
    terminate_ = reinterpret_cast<PFN_eglTerminate>(sym("eglTerminate"));
    destroy_context_ = reinterpret_cast<PFN_eglDestroyContext>(sym("eglDestroyContext"));
    make_current_ = reinterpret_cast<PFN_eglMakeCurrent>(sym("eglMakeCurrent"));
    get_proc_ = reinterpret_cast<PFN_eglGetProcAddress>(sym("eglGetProcAddress"));

    if (!initialize || !choose_config || !create_context || !make_current_ ||
        !get_proc_) {
      *err = "libEGL is missing required entry points";
      return false;
    }

    if (get_platform_display) {
      display_ = get_platform_display(EGL_PLATFORM_SURFACELESS, nullptr, nullptr);
    }
    if (!display_ && get_display) display_ = get_display(nullptr);
    if (!display_) {
      *err = "no EGL display";
      return false;
    }

    EGLint major = 0, minor = 0;
    if (!initialize(display_, &major, &minor)) {
      *err = "eglInitialize failed";
      display_ = nullptr;
      return false;
    }
    if (bind_api && !bind_api(EGL_OPENGL_API)) {
      *err = "desktop OpenGL not available through EGL";
      return false;
    }

    const EGLint config_attribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                     EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                                     EGL_NONE};
    EGLConfig config = nullptr;
    EGLint num_config = 0;
    if (!choose_config(display_, config_attribs, &config, 1, &num_config) ||
        num_config < 1) {
      *err = "no suitable EGL config";
      return false;
    }

    const EGLint ctx_attribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3,
                                  EGL_CONTEXT_MINOR_VERSION, 3,
                                  EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                  EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                  EGL_NONE};
    ctx_ = create_context(display_, config, nullptr, ctx_attribs);
    if (!ctx_) {
      *err = "eglCreateContext failed (no GL 3.3 core?)";
      return false;
    }
    return true;
  }

  bool MakeCurrent() override {
    // Surfaceless: no draw/read surface, everything goes to an FBO.
    return make_current_(display_, nullptr, nullptr, ctx_) != 0;
  }

  GlGetProcFn GetProcLoader() override {
    return [](const char* name) -> void* {
      static void* lib = [] {
        void* l = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
        return l ? l : dlopen("libEGL.so", RTLD_LAZY | RTLD_LOCAL);
      }();
      if (!lib) return nullptr;
      static auto get_proc =
          reinterpret_cast<PFN_eglGetProcAddress>(dlsym(lib, "eglGetProcAddress"));
      void* p = get_proc ? get_proc(name) : nullptr;
      if (p) return p;
      // Some drivers only expose core entry points through libGL.
      static void* gl = [] {
        void* g = dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
        return g ? g : dlopen("libGL.so", RTLD_LAZY | RTLD_LOCAL);
      }();
      return gl ? dlsym(gl, name) : nullptr;
    };
  }

  const char* Name() const override { return "egl"; }

 private:
  void* lib_ = nullptr;
  EGLDisplay display_ = nullptr;
  EGLContext ctx_ = nullptr;
  PFN_eglTerminate terminate_ = nullptr;
  PFN_eglDestroyContext destroy_context_ = nullptr;
  PFN_eglMakeCurrent make_current_ = nullptr;
  PFN_eglGetProcAddress get_proc_ = nullptr;
};

#endif

}  // namespace

std::unique_ptr<GlContext> CreateHeadlessGlContext(std::string* err) {
  std::string local;
  if (!err) err = &local;

#if defined(_WIN32)
  auto ctx = std::unique_ptr<WglContext>(new WglContext());
#elif defined(__APPLE__)
  auto ctx = std::unique_ptr<CglContext>(new CglContext());
#else
  auto ctx = std::unique_ptr<EglContext>(new EglContext());
#endif

  if (!ctx->Init(err)) return nullptr;
  if (!ctx->MakeCurrent()) {
    *err = "failed to make the GL context current";
    return nullptr;
  }
  return std::unique_ptr<GlContext>(ctx.release());
}

}  // namespace tusdql
