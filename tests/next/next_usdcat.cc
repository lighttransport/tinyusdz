// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - next_usdcat
//
// Minimal tusdcat-compatible CLI for the src/next module, so the usd-wg
// asset-corpus regression gate (tests/parse-asset-corpus.mjs) can drive next
// the same way it drives the legacy loader:
//   next_usdcat [-l|-f] file.usd[acz]
//     -l  load only (parse the single layer; default)
//     -f  flatten (compose sublayers/references/payloads/... via pcp, print USDA)
// Contract: exit 0 = loaded; diagnostics on stderr prefixed "WARN : " / "ERR : ".

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "next/pcp/cache.hh"
#include "next/resolver/asset-resolver.hh"
#include "next/stage/stage.hh"
#include "next/tinyusdz-next.hh"
#include "next/writer/usda-writer.hh"

using namespace tinyusdz::next;

static void emit_lines(const std::string &msgs, const char *prefix) {
  size_t pos = 0;
  while (pos < msgs.size()) {
    size_t nl = msgs.find('\n', pos);
    if (nl == std::string::npos) nl = msgs.size();
    if (nl > pos) {
      std::fprintf(stderr, "%s%.*s\n", prefix, int(nl - pos), msgs.c_str() + pos);
    }
    pos = nl + 1;
  }
}

int main(int argc, char **argv) {
  bool flatten = false;
  bool openusd_compat = false;
  // Default instance flatten = holder (the historical -f behavior). `native`
  // keeps instancing; `prototypes` = usdcat-style /Flattened_Prototype_N.
  pcp::InstanceFlattenMode inst_mode = pcp::InstanceFlattenMode::Holder;
  pcp::PrototypeNumbering proto_num = pcp::PrototypeNumbering::Deterministic;
  const char *filename = nullptr;
  const char *out_path = nullptr;  // -o/--output: write flatten to a file (FdSink)
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "-f") == 0) {
      flatten = true;
    } else if (std::strcmp(argv[i], "-l") == 0) {
      flatten = false;
    } else if ((std::strcmp(argv[i], "-o") == 0 ||
                std::strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
      out_path = argv[++i];
    } else if (std::strcmp(argv[i], "--openusd-compat") == 0) {
      openusd_compat = true;  // re-emit deprecated qualifiers (e.g. `custom`)
    } else if (std::strcmp(argv[i], "--instance-mode") == 0 && i + 1 < argc) {
      const char *m = argv[++i];
      if (std::strcmp(m, "native") == 0)
        inst_mode = pcp::InstanceFlattenMode::Native;
      else if (std::strcmp(m, "holder") == 0)
        inst_mode = pcp::InstanceFlattenMode::Holder;
      else if (std::strcmp(m, "prototypes") == 0)
        inst_mode = pcp::InstanceFlattenMode::ExtractedPrototypes;
      else {
        std::fprintf(stderr, "Unknown --instance-mode '%s' "
                             "(native|holder|prototypes)\n", m);
        return 2;
      }
    } else if (std::strcmp(argv[i], "--prototype-numbering") == 0 && i + 1 < argc) {
      const char *m = argv[++i];
      if (std::strcmp(m, "deterministic") == 0)
        proto_num = pcp::PrototypeNumbering::Deterministic;
      else if (std::strcmp(m, "usdcat") == 0)
        proto_num = pcp::PrototypeNumbering::UsdcatCompatible;
      else {
        std::fprintf(stderr, "Unknown --prototype-numbering '%s' "
                             "(deterministic|usdcat)\n", m);
        return 2;
      }
    } else {
      filename = argv[i];
    }
  }
  if (!filename) {
    std::fprintf(stderr, "Usage: next_usdcat [-l|-f] [-o out.usda] "
                         "[--instance-mode native|holder|prototypes] "
                         "[--prototype-numbering deterministic|usdcat] "
                         "file.usd[acz]\n");
    return 2;
  }

  // Opt-in wall-clock attribution (load+compose vs write). Printed to stderr with
  // a non-"WARN/ERR : " prefix so the corpus categorizer ignores it; off unless
  // TINYUSDZ_NEXT_TIMING is set. Used to prioritize/measure perf work.
  const bool timing = std::getenv("TINYUSDZ_NEXT_TIMING") != nullptr;
  using Clock = std::chrono::steady_clock;
  auto ms = [](Clock::duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
  };
  const auto t_start = Clock::now();

  std::string warn, err;
  Stage stage;
  bool ok = false;
  if (flatten) {
    AssetResolver resolver;
    // Anchor relative asset paths (references/payloads/sublayers) to the input
    // file's directory, like usdcat/tusdcat.
    {
      std::string fn(filename);
      auto slash = fn.find_last_of("/\\");
      if (slash != std::string::npos) {
        resolver.SetWorkingDirectory(fn.substr(0, slash));
      }
    }
    pcp::CompositionOptions opts;
    opts.instance_flatten_mode = inst_mode;  // default Holder (self-contained)
    opts.prototype_numbering = proto_num;
    ok = pcp::ComposeStageFromFile(filename, resolver, &stage, opts, &warn, &err);
  } else {
    ok = LoadUSD(filename, &stage, &warn, &err);
  }
  const auto t_loaded = Clock::now();

  emit_lines(warn, "WARN : ");
  if (!ok) {
    emit_lines(err.empty() ? std::string("load failed") : err, "ERR : ");
    return 1;
  }
  // Composition errors are accumulated non-fatally; surface them as warnings
  // in flatten mode (the file still loaded).
  if (!err.empty()) emit_lines(err, "WARN : ");

  if (flatten) {
    USDAWriteOptions wopts;
    wopts.emit_custom = openusd_compat;
    // Parallel subtree serialization (only effective in a TINYUSDZ_ENABLE_THREAD
    // build). Default auto (= hardware_concurrency); TINYUSDZ_NEXT_NUM_THREADS
    // overrides (1 = serial). Output is byte-identical regardless of count.
    wopts.num_threads = 0;  // auto
    if (const char* nt = std::getenv("TINYUSDZ_NEXT_NUM_THREADS")) {
      wopts.num_threads = std::atoi(nt);
    }
    // Write through the next StreamWriter with the native C-stdio backend
    // (buffered + blocked writes). `-o <file>` targets a FILE*; otherwise stdout.
    // This is the default native sink; a WASM/WASI host would supply its own
    // StreamWriter::BlockSink instead.
    std::FILE* fp = stdout;
    if (out_path) {
      fp = std::fopen(out_path, "wb");
      if (!fp) {
        std::fprintf(stderr, "ERR : cannot open output file: %s\n", out_path);
        return 1;
      }
    }
    USDAWriteResult res;
    {
      StreamWriter w(StdioSink(fp));
      res = WriteUSDA(w, stage, wopts);  // flushes the buffer before returning
    }
    std::fflush(fp);
    if (out_path) std::fclose(fp);
    size_t bytes_written = res.bytes_written;
    const auto t_written = Clock::now();
    if (timing) {
      const double wms = ms(t_written - t_loaded);
      std::fprintf(stderr,
                   "[next_usdcat] load+compose=%.1fms write=%.1fms total=%.1fms\n",
                   ms(t_loaded - t_start), wms, ms(t_written - t_start));
      if (bytes_written > 0 && wms > 0.0) {
        std::fprintf(stderr,
                     "[next_usdcat] wrote %zu bytes = %.0f MB/s%s%s\n",
                     bytes_written,
                     double(bytes_written) / 1048576.0 / (wms / 1000.0),
                     out_path ? " -> " : "", out_path ? out_path : "");
      }
    }
  } else {
    std::printf("loaded: %zu prims\n", stage.GetStats().prim_count);
    if (timing) {
      std::fprintf(stderr, "[next_usdcat] load=%.1fms\n", ms(t_loaded - t_start));
    }
  }
  return 0;
}
