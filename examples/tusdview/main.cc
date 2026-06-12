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
#include <optional>
#include <string>

#include "app.hh"
#include "config.hh"
#include "log.hh"
#include "renderer.hh"

int main(int argc, char** argv) {
  tusdview::Backend backend = tusdview::Backend::GL;
  std::optional<std::string> configPath;
  std::string file;
  std::string screenshot;
  std::string windowShot;
  int maxFrames = -1;
  long long maxTris = 0;      // 0 = default budget
  double timeBudget = 0.0;    // 0 = unlimited
  std::optional<float> uiScale;  // Explicit CLI override for font/widget/window scale.
  bool wantRt = false;        // request Vulkan ray tracing (if supported)
  bool mcpStdio = false;      // MCP server: stdio transport
  int mcpHttpPort = 0;        // MCP server: HTTP transport port (0 = off)
  bool headless = false;      // windowless offscreen rendering (Vulkan only)
  bool noComposition = false;             // --no-composition: root layer only
  std::optional<bool> deferPayloads;      // --defer-payloads / --load-payloads
  bool deferReferences = false;           // --defer-references (explicit opt-in)
  std::optional<double> timeCode;         // --time T: evaluate the scene at this
                                          // time code (animated screenshots)
  tusdview::SkinningMode skinningMode = tusdview::SkinningMode::Auto;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--config") == 0) {
      if ((i + 1) >= argc) {
        LOGE("--config requires a path");
        return 1;
      }
      configPath = argv[++i];
    } else if (std::strncmp(argv[i], "--config=", 9) == 0) {
      configPath = argv[i] + 9;
    } else if (std::strcmp(argv[i], "--backend") == 0 && (i + 1) < argc) {
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
    } else if (std::strcmp(argv[i], "--no-composition") == 0) {
      noComposition = true;
    } else if (std::strcmp(argv[i], "--defer-payloads") == 0) {
      deferPayloads = true;
    } else if (std::strcmp(argv[i], "--load-payloads") == 0) {
      deferPayloads = false;
    } else if (std::strcmp(argv[i], "--defer-references") == 0) {
      deferReferences = true;
    } else if ((std::strcmp(argv[i], "--time") == 0 ||
                std::strcmp(argv[i], "--frame") == 0) &&
               (i + 1) < argc) {
      timeCode = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--skinning") == 0 && (i + 1) < argc) {
      const char* mode = argv[++i];
      if (std::strcmp(mode, "cpu") == 0) {
        skinningMode = tusdview::SkinningMode::CPU;
      } else if (std::strcmp(mode, "gpu") == 0) {
        skinningMode = tusdview::SkinningMode::GPU;
      } else if (std::strcmp(mode, "auto") == 0) {
        skinningMode = tusdview::SkinningMode::Auto;
      } else {
        LOGE("--skinning must be auto, cpu, or gpu");
        return 1;
      }
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
          "Usage: tusdview [--config PATH] [--backend gl|vk] [--rt] [--frames N] "
          "[--screenshot out.ppm]\n"
          "                [--max-tris N] [--time-budget SECONDS] [--ui-scale S]\n"
          "                [--no-composition] [--defer-payloads | --load-payloads] "
          "[--defer-references] [--time CODE] [--skinning auto|cpu|gpu]\n"
          "                [--headless] [--mcp-stdio] [--mcp-http[=PORT]] [--mcp] "
          "[file.usd|usda|usdc|usdz]\n"
          "  --config PATH Load JSON startup config (otherwise uses the platform "
          "default config path).\n"
          "  --rt          Use Vulkan ray tracing (ray query) when supported "
          "(implies --backend vk).\n"
          "  --headless    Windowless offscreen rendering, no display needed "
          "(Vulkan only; needs --frames + --screenshot/--window-shot).\n"
          "  --no-composition  Load the root layer only (skip USD composition arcs).\n"
          "  --defer-payloads  Lazy payloads: skip payload arcs on load; load on "
          "demand from the GUI (default for interactive runs).\n"
          "  --load-payloads   Compose payload arcs eagerly (default for "
          "--frames/headless runs).\n"
          "  --defer-references  Also defer `references` arcs (loaded on demand "
          "like payloads). Non-standard: USD assumes references always resolve, "
          "so most scene content stays unloaded until requested.\n"
          "  --time CODE   Evaluate the scene at this USD time code (animated "
          "transforms/points/skinning). Useful with --frames for a screenshot at "
          "a specific frame. Interactive runs play from the Timeline panel.\n"
          "  --skinning MODE  Skinning path: auto (default), cpu, or gpu.\n"
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

  const tusdview::ConfigLoadResult config = tusdview::LoadStartupConfig(configPath);
  if (config.status == tusdview::ConfigLoadStatus::Error) {
    if (!config.path.empty()) {
      LOGE("config %s: %s", config.path.string().c_str(), config.error.c_str());
    } else {
      LOGE("config: %s", config.error.c_str());
    }
    if (config.explicitPath) return 1;
    LOGW("ignoring invalid default config and continuing with built-in defaults.");
  } else if (config.status == tusdview::ConfigLoadStatus::Loaded) {
    LOGI("loaded config %s", config.path.string().c_str());
    for (const std::string& warning : config.warnings) {
      LOGW("config %s: %s", config.path.string().c_str(), warning.c_str());
    }
  }

  tusdview::App app(backend);
  if (config.status == tusdview::ConfigLoadStatus::Loaded) {
    if (config.config.fontSizePx) app.setFontSize(*config.config.fontSizePx);
    if (config.config.windowScale) app.setWindowScale(*config.config.windowScale);
    if (config.config.windowWidth && config.config.windowHeight) {
      app.setWindowSize(*config.config.windowWidth, *config.config.windowHeight);
    }
    if (config.config.orbitSensitivity) {
      app.setOrbitSensitivity(*config.config.orbitSensitivity);
    }
    if (config.config.panSensitivity) {
      app.setPanSensitivity(*config.config.panSensitivity);
    }
    if (config.config.dollySensitivity) {
      app.setDollySensitivity(*config.config.dollySensitivity);
    }
    if (config.config.invertDolly) {
      app.setInvertDolly(*config.config.invertDolly);
    }
  }
  // USD composition options: CLI > config > defaults. Payloads default to
  // lazy (deferred) interactively; headless/--frames runs load them eagerly so
  // screenshots are complete.
  {
    tusdview::LoadOptions lo;
    lo.composition = !noComposition;
    if (config.status == tusdview::ConfigLoadStatus::Loaded) {
      if (config.config.composition && !noComposition) {
        lo.composition = *config.config.composition;
      }
      if (config.config.payloadPolicy && !deferPayloads.has_value()) {
        deferPayloads = (*config.config.payloadPolicy == "defer");
      }
    }
    const bool defer = deferPayloads.value_or(maxFrames < 0 && !headless);
    lo.payloadPolicy = defer ? tusdview::PayloadPolicy::DeferAll
                             : tusdview::PayloadPolicy::LoadAll;
    // Explicit opt-in only (no headless default flip): deferring references is
    // non-standard, so honor the flag verbatim even for --frames runs.
    lo.deferReferences = deferReferences;
    if (timeCode.has_value()) lo.timecode = *timeCode;
    app.setLoadOptions(lo);
  }
  app.setLoadBudget(static_cast<std::size_t>(maxTris < 0 ? 0 : maxTris), timeBudget);
  if (uiScale) {
    if (*uiScale > 0.25f) {
      app.clearWindowSizeOverride();
      app.setUiScale(*uiScale);
    } else {
      LOGW("ignoring invalid --ui-scale %.3f (must be > 0.25)", *uiScale);
    }
  }
  app.setWindowShot(windowShot);
  app.setRequestRayTracing(wantRt);
  app.setSkinningMode(skinningMode);
  app.setMcpStdio(mcpStdio);
  app.setMcpHttp(mcpHttpPort);
  app.setHeadless(headless);
  return app.run(file, maxFrames, screenshot);
}
