// SPDX-License-Identifier: Apache-2.0
// tusdview - a USD viewer example for tinyusdz.
//
// Usage:
//   tusdview [options] [file.usd|usda|usdc|usdz]
//   --backend gl|vk   Select rendering backend (default: gl)
//
// Loads a USD file via tinyusdz, converts it with the Tydra RenderScene API and
// renders it with OpenGL (and Vulkan if built with HAVE_VULKAN). The GUI uses an
// ImGui docking layout with a prim hierarchy browser, a property inspector and a
// 3D viewport with Maya-style navigation (Alt+LMB orbit / Alt+MMB pan /
// Alt+RMB+wheel dolly, 'F' to frame the scene).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app.hh"
#include "log.hh"
#include "renderer.hh"

int main(int argc, char** argv) {
  tusdview::Backend backend = tusdview::Backend::GL;
  std::string file;
  std::string screenshot;
  std::string windowShot;
  int maxFrames = -1;
  long long maxTris = 0;      // 0 = default budget
  double timeBudget = 0.0;    // 0 = unlimited
  float uiScale = 2.0f;       // HiDPI UI scale (font px = 16 * uiScale)
  bool wantRt = false;        // request Vulkan ray tracing (if supported)
  bool mcpStdio = false;      // MCP server: stdio transport
  int mcpHttpPort = 0;        // MCP server: HTTP transport port (0 = off)
  bool headless = false;      // windowless offscreen rendering (Vulkan only)

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--backend") == 0 && (i + 1) < argc) {
      ++i;
      if (std::strcmp(argv[i], "vk") == 0 || std::strcmp(argv[i], "vulkan") == 0) {
        backend = tusdview::Backend::Vulkan;
      } else {
        backend = tusdview::Backend::GL;
      }
    } else if (std::strcmp(argv[i], "--frames") == 0 && (i + 1) < argc) {
      maxFrames = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--screenshot") == 0 && (i + 1) < argc) {
      screenshot = argv[++i];
    } else if (std::strcmp(argv[i], "--max-tris") == 0 && (i + 1) < argc) {
      maxTris = std::atoll(argv[++i]);
    } else if (std::strcmp(argv[i], "--time-budget") == 0 && (i + 1) < argc) {
      timeBudget = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--ui-scale") == 0 && (i + 1) < argc) {
      uiScale = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(argv[i], "--window-shot") == 0 && (i + 1) < argc) {
      windowShot = argv[++i];
    } else if (std::strcmp(argv[i], "--headless") == 0) {
      headless = true;
    } else if (std::strcmp(argv[i], "--rt") == 0) {
      wantRt = true;
    } else if (std::strcmp(argv[i], "--mcp-stdio") == 0) {
      mcpStdio = true;
    } else if (std::strncmp(argv[i], "--mcp-http", 10) == 0) {
      const char* eq = std::strchr(argv[i], '=');
      mcpHttpPort = eq ? std::atoi(eq + 1) : 8080;
      if (mcpHttpPort <= 0) mcpHttpPort = 8080;
    } else if (std::strcmp(argv[i], "--mcp") == 0) {
      mcpStdio = true;
      if (mcpHttpPort == 0) mcpHttpPort = 8080;
    } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
      std::printf(
          "Usage: tusdview [--backend gl|vk] [--rt] [--frames N] "
          "[--screenshot out.ppm]\n"
          "                [--max-tris N] [--time-budget SECONDS] [--ui-scale S]\n"
          "                [--headless] [--mcp-stdio] [--mcp-http[=PORT]] [--mcp] "
          "[file.usd|usda|usdc|usdz]\n"
          "  --rt          Use Vulkan ray tracing (ray query) when supported "
          "(implies --backend vk).\n"
          "  --headless    Windowless offscreen rendering, no display needed "
          "(Vulkan only; needs --frames + --screenshot/--window-shot).\n"
          "  --mcp-stdio   Run the MCP server over stdio (JSON-RPC on stdin/stdout).\n"
          "  --mcp-http    Run the MCP server over HTTP (default port 8080).\n"
          "  --mcp         Both transports.\n");
      return 0;
    } else if (argv[i][0] != '-') {
      file = argv[i];
    }
  }

  // Ray tracing is a Vulkan technique, so --rt implies the Vulkan backend.
  if (wantRt) backend = tusdview::Backend::Vulkan;
  // Windowless rendering is a Vulkan-only path (GL needs a window/context).
  if (headless) backend = tusdview::Backend::Vulkan;

#if !defined(HAVE_VULKAN)
  if (backend == tusdview::Backend::Vulkan) {
    LOGW("Vulkan backend not compiled in; using OpenGL.");
    backend = tusdview::Backend::GL;
    wantRt = false;
  }
#endif

  tusdview::App app(backend);
  app.setLoadBudget(static_cast<std::size_t>(maxTris < 0 ? 0 : maxTris), timeBudget);
  app.setUiScale(uiScale);
  app.setWindowShot(windowShot);
  app.setRequestRayTracing(wantRt);
  app.setMcpStdio(mcpStdio);
  app.setMcpHttp(mcpHttpPort);
  app.setHeadless(headless);
  return app.run(file, maxFrames, screenshot);
}
