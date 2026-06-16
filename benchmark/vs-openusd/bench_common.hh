// Shared helpers for the tinyusdz vs OpenUSD benchmark harness.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace bench {

using clock_type = std::chrono::steady_clock;

inline double now_ms() {
  return std::chrono::duration<double, std::milli>(
             clock_type::now().time_since_epoch())
      .count();
}

// Runs `fn` `iters` times and prints one CSV line:
//   op,ok,iters,median_ms,min_ms,max_ms
template <typename F>
void run(const std::string &op, int iters, F fn) {
  std::vector<double> samples;
  samples.reserve(size_t(iters));
  bool ok = true;
  for (int i = 0; i < iters; i++) {
    double t0 = now_ms();
    if (!fn()) {
      ok = false;
      break;
    }
    samples.push_back(now_ms() - t0);
  }
  if (!ok || samples.empty()) {
    std::printf("%s,FAIL,0,0,0,0\n", op.c_str());
    return;
  }
  std::sort(samples.begin(), samples.end());
  double median = samples[samples.size() / 2];
  std::printf("%s,OK,%d,%.3f,%.3f,%.3f\n", op.c_str(), int(samples.size()),
              median, samples.front(), samples.back());
}

inline int parse_iters(int argc, char **argv, int default_iters = 10) {
  for (int i = 1; i + 1 < argc; i++) {
    if (std::string(argv[i]) == "--iters") {
      return std::stoi(argv[i + 1]);
    }
  }
  return default_iters;
}

}  // namespace bench
