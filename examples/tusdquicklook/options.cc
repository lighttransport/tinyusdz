// SPDX-License-Identifier: Apache-2.0
#include "options.hh"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace tusdql {

namespace {

bool ParseInt(const char* s, int* out) {
  if (!s || !*s) return false;
  char* end = nullptr;
  long v = std::strtol(s, &end, 10);
  if (end == s || *end != '\0') return false;
  *out = static_cast<int>(v);
  return true;
}

bool ParseSize(const char* s, int* w, int* h) {
  if (!s) return false;
  const char* x = std::strchr(s, 'x');
  if (!x) x = std::strchr(s, 'X');
  if (!x) return false;
  std::string ws(s, static_cast<size_t>(x - s));
  int pw = 0, ph = 0;
  if (!ParseInt(ws.c_str(), &pw)) return false;
  if (!ParseInt(x + 1, &ph)) return false;
  if (pw <= 0 || ph <= 0) return false;
  *w = pw;
  *h = ph;
  return true;
}

// Returns nullptr when the flag is missing its value.
const char* Value(int argc, char** argv, int* i) {
  if (*i + 1 >= argc) return nullptr;
  *i += 1;
  return argv[*i];
}

// Index-aligned with ShadingMode.
const char* const kShadingModeNames[kShadingModeCount] = {
    "shaded", "albedo", "normal", "uv", "roughness", "metallic", "depth",
};

}  // namespace

const char* ShadingModeName(ShadingMode mode) {
  const int i = static_cast<int>(mode);
  if (i < 0 || i >= kShadingModeCount) return "shaded";
  return kShadingModeNames[i];
}

bool ParseShadingMode(const std::string& name, ShadingMode* out) {
  for (int i = 0; i < kShadingModeCount; i++) {
    if (name == kShadingModeNames[i]) {
      *out = static_cast<ShadingMode>(i);
      return true;
    }
  }
  return false;
}

const char* UsageText() {
  return
      "tusdquicklook — minimal portable USD Quick Look viewer\n"
      "\n"
      "usage: tusdquicklook [file-or-dir] [options]\n"
      "\n"
      "  --max-mem <MB>          preview memory cap (default 512)\n"
      "  --max-gpu-mem <MB>      GL GPU residency cap (default 512)\n"
      "  --backend auto|cpu|gl   renderer selection (default auto)\n"
      "  --spp <N>               progressive sample target (default 16)\n"
      "  --threads <N>           worker threads (default 4 UI / 8 headless)\n"
      "  --no-shadows            disable shadow rays\n"
      "  --ao                    add an ambient-occlusion pass\n"
      "  --shading-mode <mode>   shaded|albedo|normal|uv|roughness|metallic|depth\n"
      "                          (default shaded)\n"
      "  --env <file>            equirectangular environment map for IBL\n"
      "  --no-ibl                disable image-based lighting\n"
      "  --no-smoothing          disable damped camera motion\n"
      "  --no-compose            skip USD composition\n"
      "  --recursive             recurse into subfolders when browsing\n"
      "  --size <WxH>            window size (default 1280x720)\n"
      "  --screenshot <png>      headless: render one file, write PNG, exit\n"
      "  --frames <N>            progressive steps before --screenshot (default 8)\n"
      "  --mcp-stdio             MCP JSON-RPC on stdin/stdout\n"
      "  --mcp-http[=PORT]       MCP HTTP server (default port 8765)\n"
      "  --mcp                   enable both MCP transports\n"
      "  -v, --verbose           verbose logging\n"
      "  -h, --help              this message\n";
}

bool ParseOptions(int argc, char** argv, Options* opts, bool* want_help,
                  std::string* err) {
  *want_help = false;
  bool have_positional = false;

  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];

    if (a == "-h" || a == "--help") {
      *want_help = true;
      return true;
    } else if (a == "-v" || a == "--verbose") {
      opts->verbose = true;
    } else if (a == "--no-shadows") {
      opts->shadows = false;
    } else if (a == "--ao") {
      opts->ao = true;
    } else if (a == "--no-compose") {
      opts->compose = false;
    } else if (a == "--no-ibl") {
      opts->ibl = false;
    } else if (a == "--no-smoothing") {
      opts->camera_smoothing = false;
    } else if (a == "--shading-mode") {
      const char* v = Value(argc, argv, &i);
      if (!v || !ParseShadingMode(v, &opts->shading_mode)) {
        *err =
            "--shading-mode must be one of: shaded, albedo, normal, uv, "
            "roughness, metallic, depth";
        return false;
      }
    } else if (a == "--env") {
      const char* v = Value(argc, argv, &i);
      if (!v) {
        *err = "--env requires an image path";
        return false;
      }
      opts->env_path = v;
    } else if (a == "--recursive") {
      opts->recursive = true;
    } else if (a == "--max-mem") {
      const char* v = Value(argc, argv, &i);
      int mb = 0;
      if (!v || !ParseInt(v, &mb) || mb <= 0) {
        *err = "--max-mem requires a positive integer (MB)";
        return false;
      }
      opts->max_mem_bytes = static_cast<uint64_t>(mb) << 20;
    } else if (a == "--max-gpu-mem") {
      const char* v = Value(argc, argv, &i);
      int mb = 0;
      if (!v || !ParseInt(v, &mb) || mb <= 0) {
        *err = "--max-gpu-mem requires a positive integer (MB)";
        return false;
      }
      opts->max_gpu_mem_bytes = static_cast<uint64_t>(mb) << 20;
    } else if (a == "--backend") {
      const char* v = Value(argc, argv, &i);
      if (!v) {
        *err = "--backend requires auto|cpu|gl";
        return false;
      }
      const std::string b = v;
      if (b == "auto") {
        opts->backend = BackendChoice::Auto;
      } else if (b == "cpu") {
        opts->backend = BackendChoice::Cpu;
      } else if (b == "gl") {
        opts->backend = BackendChoice::Gl;
      } else {
        *err = "--backend must be one of: auto, cpu, gl";
        return false;
      }
    } else if (a == "--spp") {
      const char* v = Value(argc, argv, &i);
      if (!v || !ParseInt(v, &opts->spp) || opts->spp <= 0) {
        *err = "--spp requires a positive integer";
        return false;
      }
    } else if (a == "--threads") {
      const char* v = Value(argc, argv, &i);
      if (!v || !ParseInt(v, &opts->threads) || opts->threads < 0) {
        *err = "--threads requires a non-negative integer";
        return false;
      }
    } else if (a == "--frames") {
      const char* v = Value(argc, argv, &i);
      if (!v || !ParseInt(v, &opts->frames) || opts->frames <= 0) {
        *err = "--frames requires a positive integer";
        return false;
      }
    } else if (a == "--size") {
      const char* v = Value(argc, argv, &i);
      if (!v || !ParseSize(v, &opts->width, &opts->height)) {
        *err = "--size requires WxH, e.g. 1280x720";
        return false;
      }
    } else if (a == "--screenshot") {
      const char* v = Value(argc, argv, &i);
      if (!v) {
        *err = "--screenshot requires an output .png path";
        return false;
      }
      opts->screenshot = v;
    } else if (a == "--mcp-stdio") {
      opts->mcp_stdio = true;
    } else if (a == "--mcp-http" || a.rfind("--mcp-http=", 0) == 0) {
      const char* v = nullptr;
      if (a.rfind("--mcp-http=", 0) == 0) {
        v = a.c_str() + std::strlen("--mcp-http=");
      } else if (i + 1 < argc && argv[i + 1][0] >= '0' &&
                 argv[i + 1][0] <= '9') {
        v = argv[++i];
      }
      int port = 8765;
      if (v && (!ParseInt(v, &port) || port <= 0 || port > 65535)) {
        *err = "--mcp-http requires a port from 1 to 65535";
        return false;
      }
      opts->mcp_http_port = port;
    } else if (a == "--mcp") {
      opts->mcp_stdio = true;
      opts->mcp_http_port = 8765;
    } else if (!a.empty() && a[0] == '-') {
      *err = "unknown option: " + a;
      return false;
    } else {
      if (have_positional) {
        *err = "more than one file/directory given: " + a;
        return false;
      }
      opts->path = a;
      have_positional = true;
    }
  }

  if ((opts->mcp_stdio || opts->mcp_http_port != 0) &&
      !opts->screenshot.empty()) {
    *err = "MCP transports require an interactive run (remove --screenshot)";
    return false;
  }
#if !defined(TUSDQUICKLOOK_HAVE_MCP)
  if (opts->mcp_stdio || opts->mcp_http_port != 0) {
    *err = "MCP support is disabled; configure with -DTUSDQUICKLOOK_ENABLE_MCP=ON";
    return false;
  }
#endif

  return true;
}

int ResolveThreadCount(const Options& opts) {
  return ResolveThreadCount(opts, false);
}

int ResolveThreadCount(const Options& opts, bool interactive) {
  if (opts.threads > 0) return opts.threads;
  unsigned hc = std::thread::hardware_concurrency();
  if (hc == 0) hc = 4;
  // This is a previewer, not a farm renderer: cap so it stays a good citizen
  // on a busy workstation.
  return static_cast<int>(std::min<unsigned>(hc, interactive ? 4u : 8u));
}

}  // namespace tusdql
