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
