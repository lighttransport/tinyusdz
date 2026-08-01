// SPDX-License-Identifier: Apache-2.0
//
// tusdquicklook — command-line options.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace tusdql {

enum class BackendChoice {
  Auto,  // GL raster when a capable GPU is present, else CPU
  Cpu,
  Gl,
};

// What the viewport shows. Everything but Shaded is a debug AOV: a direct view
// of one input to the shading model, with no lighting. Both backends must
// compute these identically — see the parity note in render/shade.hh.
enum class ShadingMode : uint8_t {
  Shaded,
  Albedo,
  Normal,
  Uv,
  Roughness,
  Metallic,
  Depth,
};

// Lowercase CLI/UI name, e.g. "roughness". Never null.
const char* ShadingModeName(ShadingMode mode);
// Parse a name produced by ShadingModeName. False when unrecognized.
bool ParseShadingMode(const std::string& name, ShadingMode* out);
// UI order, terminated by count. Kept in sync with the enum.
constexpr int kShadingModeCount = 7;

struct Options {
  // File or directory to open. Empty = current directory.
  std::string path;

  // Process-wide preview memory cap. Clamped at startup against
  // ComputeResourceBudget(MemAvailable) so a huge --max-mem cannot promise
  // more than the machine has.
  uint64_t max_mem_bytes = 512ull << 20;

  BackendChoice backend = BackendChoice::Auto;

  int spp = 16;      // progressive sample target
  int threads = 0;   // 0 = min(hardware_concurrency, 8)

  bool shadows = true;
  bool ao = false;
  bool compose = true;
  bool recursive = false;

  ShadingMode shading_mode = ShadingMode::Shaded;

  // Image-based lighting from a dome light (or --env). Off falls back to the
  // flat hemispheric ambient term.
  bool ibl = true;
  // Explicit equirectangular environment map, overriding any authored dome.
  // Also what makes the headless IBL test deterministic.
  std::string env_path;

  // Damped camera motion. Always off headless so --frames output cannot depend
  // on wall-clock timing.
  bool camera_smoothing = true;

  int width = 1280;
  int height = 720;

  // Headless: render `frames` progressive steps, write a PNG, exit.
  std::string screenshot;
  int frames = 8;

  bool verbose = false;
};

// Parse argv. Returns false and fills `err` on a bad argument. Sets
// `*want_help` when --help/-h was given (caller should print usage and exit 0).
bool ParseOptions(int argc, char** argv, Options* opts, bool* want_help,
                  std::string* err);

const char* UsageText();

// Resolved thread count for the render/BVH workers.
int ResolveThreadCount(const Options& opts);

}  // namespace tusdql
