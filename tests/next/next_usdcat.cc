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
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <limits>
#include <unistd.h>

#include "logger.hh"  // tinyusdz::logging (next routes diagnostics through it)
#include "next/parser/ascii-parser.hh"
#include "next/pcp/cache.hh"
#include "next/pcp/layer-registry.hh"
#include "next/resolver/asset-resolver.hh"
#include "next/stage/stage.hh"
#include "next/tinyusdz-next.hh"
#include "next/writer/usda-writer.hh"
#include "next/writer/usdc-writer.hh"

using namespace tinyusdz::next;

struct USDCReadCaps {
  size_t max_tokens = 1024 * 1024;
  size_t max_strings = 1024 * 1024;
  size_t max_fields = 10 * 1024 * 1024;
  size_t max_specs = 10 * 1024 * 1024;
  size_t max_paths = 10 * 1024 * 1024;
  size_t max_array_elements = 1024 * 1024 * 1024;
};

static bool ParseSizeArg(const char *arg, size_t *value) {
  if (!arg || !value) return false;
  char *end = nullptr;
  errno = 0;
  unsigned long long parsed = std::strtoull(arg, &end, 10);
  if (errno != 0 || !end || *end != '\0') return false;
  *value = static_cast<size_t>(parsed);
  return true;
}

static bool ParseIntArg(const char* arg, int* value) {
  if (!arg || !value) return false;
  char* end = nullptr;
  errno = 0;
  long parsed = std::strtol(arg, &end, 10);
  if (errno != 0 || !end || *end != '\0') return false;
  if (parsed < std::numeric_limits<int>::min() ||
      parsed > std::numeric_limits<int>::max()) {
    return false;
  }
  *value = static_cast<int>(parsed);
  return true;
}

static bool WriteBufferToFile(const char *path, const std::vector<uint8_t> &data) {
  if (!path) {
    return false;
  }
  std::FILE* fp = std::fopen(path, "wb");
  if (!fp) {
    std::fprintf(stderr, "ERR : cannot open output file: %s\n", path);
    return false;
  }
  const size_t written = std::fwrite(data.data(), 1, data.size(), fp);
  const bool close_ok = (std::fclose(fp) == 0);
  if (written != data.size() || !close_ok) {
    std::fprintf(stderr, "ERR : failed to write output file: %s\n", path);
    return false;
  }
  return true;
}

static bool ReadFileToBuffer(const char* path, std::vector<uint8_t>* data) {
  if (!path || !data) return false;
  std::FILE* fp = std::fopen(path, "rb");
  if (!fp) {
    std::fprintf(stderr, "ERR : cannot open input file: %s\n", path);
    return false;
  }
  if (std::fseek(fp, 0, SEEK_END) != 0) {
    std::fclose(fp);
    std::fprintf(stderr, "ERR : failed to seek input file: %s\n", path);
    return false;
  }
  const long size = std::ftell(fp);
  if (size < 0) {
    std::fclose(fp);
    std::fprintf(stderr, "ERR : failed to determine input size: %s\n", path);
    return false;
  }
  if (std::fseek(fp, 0, SEEK_SET) != 0) {
    std::fclose(fp);
    std::fprintf(stderr, "ERR : failed to rewind input file: %s\n", path);
    return false;
  }
  data->resize(static_cast<size_t>(size));
  const size_t read = data->empty()
                          ? 0
                          : std::fread(data->data(), 1, data->size(), fp);
  const bool close_ok = (std::fclose(fp) == 0);
  if (read != data->size() || !close_ok) {
    std::fprintf(stderr, "ERR : failed to read input file: %s\n", path);
    return false;
  }
  return true;
}

static void ApplyReadCapsFromValues(const USDCReadCaps& caps,
                                   pcp::LayerLoadOptions* layer_opts,
                                   USDCLoadOptions* usdc_opts) {
  if (layer_opts) {
    layer_opts->max_tokens = caps.max_tokens;
    layer_opts->max_strings = caps.max_strings;
    layer_opts->max_fields = caps.max_fields;
    layer_opts->max_specs = caps.max_specs;
    layer_opts->max_paths = caps.max_paths;
    layer_opts->max_array_elements = caps.max_array_elements;
  }

  if (usdc_opts) {
    usdc_opts->crate_options.max_tokens = caps.max_tokens;
    usdc_opts->crate_options.max_strings = caps.max_strings;
    usdc_opts->crate_options.max_fields = caps.max_fields;
    usdc_opts->crate_options.max_specs = caps.max_specs;
    usdc_opts->crate_options.max_paths = caps.max_paths;
    usdc_opts->crate_options.max_array_elements = caps.max_array_elements;
  }
}

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

static void PrintUSDAParseProfile(const USDAParseProfile& p) {
  std::fprintf(stderr,
               "[next_usda_profile] input=%llu mmap=%d open=%.1fms read=%.1fms "
               "parse=%.1fms metadata=%.1fms prims=%.1fms finalize=%.1fms\n",
               static_cast<unsigned long long>(p.input_bytes),
               p.used_mmap ? 1 : 0, p.file_open_ms, p.file_read_ms,
               p.parser_ms, p.stage_metadata_ms, p.prims_ms, p.finalize_ms);
  std::fprintf(stderr,
               "[next_usda_profile] prims=%llu properties=%llu timeSamples=%llu "
               "arrays=%llu simple=%llu arrayBytes=%llu\n",
               static_cast<unsigned long long>(p.prims),
               static_cast<unsigned long long>(p.properties),
               static_cast<unsigned long long>(p.time_samples),
               static_cast<unsigned long long>(p.arrays),
               static_cast<unsigned long long>(p.simple_arrays),
               static_cast<unsigned long long>(p.array_bytes));
  std::fprintf(stderr,
               "[next_usda_profile] numericArrays=%llu numericScalars=%llu "
               "numericBytes=%llu parallelArrays=%llu parallelScalars=%llu "
               "parallelBytes=%llu parallelFallbacks=%llu "
               "deferredArrays=%llu deferredBytes=%llu\n",
               static_cast<unsigned long long>(p.numeric_arrays),
               static_cast<unsigned long long>(p.numeric_array_scalars),
               static_cast<unsigned long long>(p.numeric_array_bytes),
               static_cast<unsigned long long>(p.parallel_arrays),
               static_cast<unsigned long long>(p.parallel_array_scalars),
               static_cast<unsigned long long>(p.parallel_array_bytes),
               static_cast<unsigned long long>(p.parallel_array_fallbacks),
               static_cast<unsigned long long>(p.deferred_arrays),
               static_cast<unsigned long long>(p.deferred_array_bytes));
}

int main(int argc, char **argv) {
  bool flatten = false;
  // Compose-free parse->write: LoadLayerFromFile (parse only, no composition) ->
  // WriteLayer. Measures RAW parse and RAW write throughput in isolation, and is
  // an idempotent parse-fidelity oracle (rewriting its own output is byte-identical).
  bool rewrite_layer = false;
  bool parse_usdc_memory = false;
  bool parse_usdc_tempfs = false;
  bool parse_only = false;
  bool finalize_usdc_stage = true;
  bool openusd_compat = false;
  // Default instance flatten = holder (the historical -f behavior). `native`
  // keeps instancing; `prototypes` = usdcat-style /Flattened_Prototype_N.
  pcp::InstanceFlattenMode inst_mode = pcp::InstanceFlattenMode::Holder;
  pcp::PrototypeNumbering proto_num = pcp::PrototypeNumbering::Deterministic;
  int parse_threads = 0;
  int write_threads = 0;
  // Parallel composition is OPT-IN via --compose-threads N (default 1 = serial,
  // no threading). It is byte-identical to serial; it helps small compose-bound
  // scenes and currently regresses huge instanced ones, so it stays off by
  // default. Independent from write/thread flags below.
  int compose_threads = 1;
  bool timing = false;
  bool parse_profile = false;
  bool async_arrays = true;
  bool parallel_prims = true;
  bool write_usdc_memory = false;
  USDCReadCaps read_caps;
  bool usdc_caps_explicit = false;
  const char *filename = nullptr;
  const char *out_path = nullptr;  // -o/--output: write flatten to a file (FdSink)
  const char *write_usdc_out_path = nullptr;  // --write-usdc-out: dump rewrite usdc buffer
  const char *temp_dir = "/dev/shm";
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "-f") == 0) {
      flatten = true;
    } else if (std::strcmp(argv[i], "--rewrite-layer") == 0) {
      rewrite_layer = true;
    } else if (std::strcmp(argv[i], "--write-usdc-memory") == 0) {
      write_usdc_memory = true;
      flatten = true;
    } else if (std::strcmp(argv[i], "--parse-usdc-memory") == 0) {
      parse_usdc_memory = true;
    } else if (std::strcmp(argv[i], "--parse-usdc-tempfs") == 0) {
      parse_usdc_tempfs = true;
    } else if (std::strcmp(argv[i], "--parse-only") == 0) {
      parse_only = true;
    } else if (std::strcmp(argv[i], "--no-stage-finalize") == 0) {
      finalize_usdc_stage = false;
    } else if (std::strcmp(argv[i], "-l") == 0) {
      flatten = false;
    } else if ((std::strcmp(argv[i], "-o") == 0 ||
                std::strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
      out_path = argv[++i];
    } else if (std::strcmp(argv[i], "--write-usdc-out") == 0 && i + 1 < argc) {
      write_usdc_out_path = argv[++i];
    } else if (std::strcmp(argv[i], "--temp-dir") == 0 && i + 1 < argc) {
      temp_dir = argv[++i];
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
    } else if (std::strcmp(argv[i], "--compose-threads") == 0 && i + 1 < argc) {
      compose_threads = std::atoi(argv[++i]);  // opt-in parallel compose (>1)
      if (compose_threads < 1) compose_threads = 1;
    } else if (std::strcmp(argv[i], "--timing") == 0) {
      timing = true;
    } else if (std::strcmp(argv[i], "--parse-profile") == 0) {
      parse_profile = true;
    } else if (std::strcmp(argv[i], "--async-arrays") == 0) {
      async_arrays = true;
    } else if (std::strcmp(argv[i], "--no-async-arrays") == 0) {
      async_arrays = false;
    } else if (std::strcmp(argv[i], "--no-parallel-prims") == 0) {
      parallel_prims = false;
    } else if (std::strcmp(argv[i], "--parse-threads") == 0 && i + 1 < argc) {
      if (!ParseIntArg(argv[++i], &parse_threads) || parse_threads < 0) {
        std::fprintf(stderr,
                     "ERR : invalid --parse-threads value: %s\n", argv[i]);
        return 2;
      }
    } else if (std::strcmp(argv[i], "--write-threads") == 0 && i + 1 < argc) {
      if (!ParseIntArg(argv[++i], &write_threads) || write_threads < 0) {
        std::fprintf(stderr,
                     "ERR : invalid --write-threads value: %s\n", argv[i]);
        return 2;
      }
    } else if ((std::strcmp(argv[i], "--max-tokens") == 0 ||
                std::strcmp(argv[i], "--usdc-max-tokens") == 0) && i + 1 < argc) {
      const char* v = argv[++i];
      if (!ParseSizeArg(v, &read_caps.max_tokens)) {
        std::fprintf(stderr, "ERR : invalid --usdc-max-tokens value: %s\n", v);
        return 2;
      }
      usdc_caps_explicit = true;
    } else if ((std::strcmp(argv[i], "--max-strings") == 0 ||
                std::strcmp(argv[i], "--usdc-max-strings") == 0) && i + 1 < argc) {
      const char* v = argv[++i];
      if (!ParseSizeArg(v, &read_caps.max_strings)) {
        std::fprintf(stderr, "ERR : invalid --usdc-max-strings value: %s\n", v);
        return 2;
      }
      usdc_caps_explicit = true;
    } else if ((std::strcmp(argv[i], "--max-fields") == 0 ||
                std::strcmp(argv[i], "--usdc-max-fields") == 0) && i + 1 < argc) {
      const char* v = argv[++i];
      if (!ParseSizeArg(v, &read_caps.max_fields)) {
        std::fprintf(stderr, "ERR : invalid --usdc-max-fields value: %s\n", v);
        return 2;
      }
      usdc_caps_explicit = true;
    } else if ((std::strcmp(argv[i], "--max-specs") == 0 ||
                std::strcmp(argv[i], "--usdc-max-specs") == 0) && i + 1 < argc) {
      const char* v = argv[++i];
      if (!ParseSizeArg(v, &read_caps.max_specs)) {
        std::fprintf(stderr, "ERR : invalid --usdc-max-specs value: %s\n", v);
        return 2;
      }
      usdc_caps_explicit = true;
    } else if ((std::strcmp(argv[i], "--max-paths") == 0 ||
                std::strcmp(argv[i], "--usdc-max-paths") == 0) && i + 1 < argc) {
      const char* v = argv[++i];
      if (!ParseSizeArg(v, &read_caps.max_paths)) {
        std::fprintf(stderr, "ERR : invalid --usdc-max-paths value: %s\n", v);
        return 2;
      }
      usdc_caps_explicit = true;
    } else if ((std::strcmp(argv[i], "--max-array-elements") == 0 ||
                std::strcmp(argv[i], "--usdc-max-array-elements") == 0) && i + 1 < argc) {
      const char* v = argv[++i];
      if (!ParseSizeArg(v, &read_caps.max_array_elements)) {
        std::fprintf(stderr, "ERR : invalid --usdc-max-array-elements value: %s\n", v);
        return 2;
      }
      usdc_caps_explicit = true;
    } else if (std::strcmp(argv[i], "--usdc-max") == 0 && i + 1 < argc) {
      const char* v = argv[++i];
      size_t cap;
      if (!ParseSizeArg(v, &cap)) {
        std::fprintf(stderr, "ERR : invalid --usdc-max value: %s\n", v);
        return 2;
      }
      read_caps.max_tokens = cap;
      read_caps.max_strings = cap;
      read_caps.max_fields = cap;
      read_caps.max_specs = cap;
      read_caps.max_paths = cap;
      read_caps.max_array_elements = cap;
      usdc_caps_explicit = true;
    } else {
      filename = argv[i];
    }
  }
  if ((parse_usdc_memory || parse_usdc_tempfs) && !usdc_caps_explicit) {
    const size_t unlimited = std::numeric_limits<size_t>::max();
    read_caps.max_tokens = unlimited;
    read_caps.max_strings = unlimited;
    read_caps.max_fields = unlimited;
    read_caps.max_specs = unlimited;
    read_caps.max_paths = unlimited;
    read_caps.max_array_elements = unlimited;
  }
  if (!filename) {
    std::fprintf(stderr, "Usage: next_usdcat [-l|-f|--rewrite-layer] [-o out.usda] "
                         "[--write-usdc-memory] "
                         "[--parse-usdc-memory] "
                         "[--parse-usdc-tempfs] "
                         "[--parse-only] "
                         "[--write-usdc-out <path.usdc>] "
                         "[--temp-dir <dir>] "
                         "[--no-stage-finalize] "
                         "[--timing] [--parse-profile] "
                         "[--no-async-arrays] [--no-parallel-prims] "
                         "[--usdc-max N] "
                         "[--usdc-max-fields N] [--usdc-max-specs N] [--usdc-max-paths N] "
                         "[--usdc-max-tokens N] [--usdc-max-strings N] [--usdc-max-array-elements N] "
                         "[--parse-threads N] [--write-threads N] "
                         "[--instance-mode native|holder|prototypes] "
                         "[--prototype-numbering deterministic|usdcat] "
                         "[--compose-threads N] "
                         "file.usd[acz]\n");
    return 2;
  }
  if ((parse_usdc_memory || parse_usdc_tempfs) && !IsUSDCPath(filename)) {
    rewrite_layer = true;
  }
  if (rewrite_layer && flatten) {
    std::fprintf(stderr, "--rewrite-layer and -f cannot be combined "
                         "(rewrite-layer is a compose-free parse->write).\n");
    return 2;
  }
  if (parse_only && !rewrite_layer) {
    std::fprintf(stderr, "--parse-only requires --rewrite-layer.\n");
    return 2;
  }

  // Opt-in wall-clock attribution (load+compose vs write). Printed to stderr with
  // a non-"WARN/ERR : " prefix so the corpus categorizer ignores it; off unless
  // --timing is set. Used to prioritize/measure perf work.
  if (timing) {
    // The next library emits its [next_build]/[next_warm]/[next_compose] timing
    // through tinyusdz::logging now (not a hardcoded stderr fprintf). Point the
    // logger at stderr and enable Info so those lines appear like before.
    tinyusdz::logging::Logger::getInstance().setStream(&std::cerr);
    tinyusdz::logging::Logger::getInstance().setLogLevel(
        tinyusdz::logging::LogLevel::Info);
  }
  using Clock = std::chrono::steady_clock;
  auto ms = [](Clock::duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
  };
  const auto t_start = Clock::now();
  USDCLoadOptions usdc_opts;
  usdc_opts.crate_options.finalize_stage = finalize_usdc_stage;
  usdc_opts.crate_options.enable_timing = timing;
  ApplyReadCapsFromValues(read_caps, nullptr, &usdc_opts);

  if ((parse_usdc_memory || parse_usdc_tempfs) && IsUSDCPath(filename)) {
    USDCLoadOptions ropts_usdc;
    ropts_usdc.crate_options.finalize_stage = finalize_usdc_stage;
    ropts_usdc.crate_options.enable_timing = timing;
    ApplyReadCapsFromValues(read_caps, nullptr, &ropts_usdc);

    if (parse_usdc_memory) {
      std::vector<uint8_t> input_data;
      const auto t_read_start = Clock::now();
      if (!ReadFileToBuffer(filename, &input_data)) {
        return 1;
      }
      const auto t_read_end = Clock::now();
      const auto t_parse_start = Clock::now();
      auto load_res = LoadUSDCFromMemoryBorrowed(input_data.data(),
                                                input_data.size(), ropts_usdc);
      const auto t_parse_end = Clock::now();
      if (!load_res.success) {
        emit_lines(load_res.error_summary.empty() ? std::string("usdc parse failed")
                                                  : load_res.error_summary,
                   "ERR : ");
        return 1;
      }
      for (const auto& warning : load_res.warnings) emit_lines(warning, "WARN : ");
      if (timing) {
        const double read_ms = ms(t_read_end - t_read_start);
        const double parse_ms = ms(t_parse_end - t_parse_start);
        std::fprintf(stderr,
                     "[next_usdcat] usdc-input-read-memory=%.1fms usdc-parse-memory=%.1fms total=%.1fms\n",
                     read_ms, parse_ms, ms(t_parse_end - t_start));
        if (!input_data.empty() && parse_ms > 0.0) {
          std::fprintf(stderr,
                       "[next_usdcat] parsed %zu bytes from memory = %.0f MB/s\n",
                       input_data.size(),
                       double(input_data.size()) / 1048576.0 /
                           (parse_ms / 1000.0));
        }
      }
    }

    if (parse_usdc_tempfs) {
      std::vector<uint8_t> input_data;
      if (!ReadFileToBuffer(filename, &input_data)) {
        return 1;
      }
      const size_t input_size = input_data.size();
      std::string tmp_template = std::string(temp_dir) + "/next-usdc-input-XXXXXX";
      std::vector<char> tmp_name(tmp_template.begin(), tmp_template.end());
      tmp_name.push_back('\0');
      const int fd = mkstemp(tmp_name.data());
      if (fd < 0) {
        std::fprintf(stderr, "ERR : failed to create temp file in %s\n", temp_dir);
        return 1;
      }
      close(fd);
      const auto t_temp_write_start = Clock::now();
      if (!WriteBufferToFile(tmp_name.data(), input_data)) {
        std::remove(tmp_name.data());
        return 1;
      }
      const auto t_temp_write_end = Clock::now();
      input_data.clear();
      input_data.shrink_to_fit();
      const auto t_temp_parse_start = Clock::now();
      auto load_res = LoadUSDCFromFile(tmp_name.data(), ropts_usdc);
      const auto t_temp_parse_end = Clock::now();
      std::remove(tmp_name.data());
      if (!load_res.success) {
        emit_lines(load_res.error_summary.empty() ? std::string("usdc parse failed")
                                                  : load_res.error_summary,
                   "ERR : ");
        return 1;
      }
      for (const auto& warning : load_res.warnings) emit_lines(warning, "WARN : ");
      if (timing) {
        const double write_ms = ms(t_temp_write_end - t_temp_write_start);
        const double parse_ms = ms(t_temp_parse_end - t_temp_parse_start);
        std::fprintf(stderr,
                     "[next_usdcat] usdc-input-write-tempfs=%.1fms usdc-parse-tempfs=%.1fms total=%.1fms\n",
                     write_ms, parse_ms, ms(t_temp_parse_end - t_start));
        if (parse_ms > 0.0) {
          std::fprintf(stderr,
                       "[next_usdcat] parsed %zu bytes from tempfs = %.0f MB/s\n",
                       input_size,
                       double(input_size) / 1048576.0 / (parse_ms / 1000.0));
        }
      }
    }
    return 0;
  }

  // Compose-free RAW parse->write path (measure parser/writer in isolation).
  if (rewrite_layer) {
    std::string warn, err;
    // Parse-thread hint (large-array parallel parse): explicit arg to the library
    // (which no longer reads the environment itself). 0 = auto, 1 = serial.
    pcp::LayerLoadOptions lopts;
    USDAParseProfile usda_profile;
    lopts.parse_num_threads = parse_threads;
    lopts.parse_async_arrays = async_arrays;
    lopts.parse_parallel_prims = parallel_prims;
    lopts.usda_profile = parse_profile ? &usda_profile : nullptr;
    lopts.finalize_usdc_stage = finalize_usdc_stage;
    lopts.enable_usdc_timing = timing;
    ApplyReadCapsFromValues(read_caps, &lopts, nullptr);
    auto layer = pcp::LoadLayerFromFile(filename, &warn, &err, lopts);  // PARSE only
    const auto t_parsed = Clock::now();
    emit_lines(warn, "WARN : ");
    if (!layer) {
      emit_lines(err.empty() ? std::string("load failed") : err, "ERR : ");
      return 1;
    }
    if (!err.empty()) emit_lines(err, "WARN : ");
    if (parse_profile) {
      PrintUSDAParseProfile(usda_profile);
    }
    if (parse_only) {
      if (timing) {
        const double pms = ms(t_parsed - t_start);
        std::fprintf(stderr,
                     "[next_usdcat] parse=%.1fms total=%.1fms\n",
                     pms, ms(t_parsed - t_start));
        long insz = 0;
        if (std::FILE* inf = std::fopen(filename, "rb")) {
          std::fseek(inf, 0, SEEK_END);
          insz = std::ftell(inf);
          std::fclose(inf);
        }
        if (insz > 0 && pms > 0.0) {
          std::fprintf(stderr, "[next_usdcat] parsed %ld bytes = %.0f MB/s\n",
                       insz, double(insz) / 1048576.0 / (pms / 1000.0));
        }
      }
      return 0;
    }

    if (parse_usdc_memory || parse_usdc_tempfs || write_usdc_out_path) {
      USDCWriteOptions wopts_usdc;
      wopts_usdc.crate_options.num_threads = write_threads;
      wopts_usdc.crate_options.enable_timing = timing;
      USDCLoadOptions ropts_usdc;
      ropts_usdc.crate_options.finalize_stage = false;
      ropts_usdc.crate_options.enable_timing = timing;
      ApplyReadCapsFromValues(read_caps, nullptr, &ropts_usdc);
      std::vector<uint8_t> usdc_data;
      const auto t_usdc_start = Clock::now();
      const auto write_res = WriteLayerToUSDCMemory(usdc_data, *layer, wopts_usdc);
      const auto t_usdc_end = Clock::now();
      if (!write_res.success) {
        std::fprintf(stderr, "ERR : %s\n",
                     write_res.error.empty() ? "usdc write failed"
                                             : write_res.error.c_str());
        return 1;
      }
      if (write_usdc_out_path) {
        if (!WriteBufferToFile(write_usdc_out_path, usdc_data)) {
          return 1;
        }
        if (timing && !parse_usdc_memory && !parse_usdc_tempfs) {
          const double pms = ms(t_parsed - t_start);
          const double wms = ms(t_usdc_end - t_usdc_start);
          std::fprintf(stderr,
                       "[next_usdcat] rewrite-layer-parse=%.1fms usdc-write=%.1fms total=%.1fms\n",
                       pms, wms, ms(t_usdc_end - t_start));
          if (write_res.bytes_written > 0 && wms > 0.0) {
            std::fprintf(stderr,
                         "[next_usdcat] wrote %zu bytes = %.0f MB/s -> %s\n",
                         write_res.bytes_written,
                         double(write_res.bytes_written) / 1048576.0 /
                             (wms / 1000.0),
                         write_usdc_out_path);
          }
        }
      }

      if (parse_usdc_memory) {
        const auto t_usdc_parse_start = Clock::now();
        auto load_res = LoadUSDCFromMemoryBorrowed(usdc_data.data(), usdc_data.size(),
                                                  ropts_usdc);
        const auto t_usdc_parse_end = Clock::now();
        if (!load_res.success) {
          emit_lines(load_res.error_summary.empty() ? std::string("usdc parse failed")
                                                   : load_res.error_summary,
                    "ERR : ");
          return 1;
        }
        if (!load_res.warnings.empty()) {
          for (const auto& warning : load_res.warnings) {
            emit_lines(warning, "WARN : ");
          }
        }
        if (timing) {
          const double pms = ms(t_parsed - t_start);
          const double wms = ms(t_usdc_end - t_usdc_start);
          const double parse_ms = ms(t_usdc_parse_end - t_usdc_parse_start);
          std::fprintf(stderr,
                       "[next_usdcat] rewrite-layer-parse=%.1fms usdc-write=%.1fms "
                       "usdc-parse-memory=%.1fms total=%.1fms\n",
                       pms, wms, parse_ms, ms(t_usdc_parse_end - t_start));
          if (write_res.bytes_written > 0 && parse_ms > 0.0) {
            std::fprintf(stderr,
                         "[next_usdcat] parsed %zu bytes from memory = %.0f MB/s\n",
                         write_res.bytes_written,
                         double(write_res.bytes_written) / 1048576.0 /
                             (parse_ms / 1000.0));
          }
        }
      }

      if (parse_usdc_tempfs) {
        std::string tmp_template = std::string(temp_dir) + "/next-usdc-parse-XXXXXX";
        std::vector<char> tmp_name(tmp_template.begin(), tmp_template.end());
        tmp_name.push_back('\0');
        const int fd = mkstemp(tmp_name.data());
        if (fd < 0) {
          std::fprintf(stderr, "ERR : failed to create temp file in %s\n", temp_dir);
          return 1;
        }
        close(fd);

        const auto t_temp_write_start = Clock::now();
        std::FILE* tfp = std::fopen(tmp_name.data(), "wb");
        if (!tfp) {
          std::remove(tmp_name.data());
          std::fprintf(stderr, "ERR : failed to open temp file for writing: %s\n",
                       tmp_name.data());
          return 1;
        }
        const size_t written = std::fwrite(usdc_data.data(), 1, usdc_data.size(),
                                          tfp);
        const bool closed = (std::fclose(tfp) == 0);
        const auto t_temp_write_end = Clock::now();
        if (!closed || written != usdc_data.size()) {
          std::remove(tmp_name.data());
          std::fprintf(stderr, "ERR : failed to write temp usdc file\n");
          return 1;
        }

        const auto t_temp_parse_start = Clock::now();
        auto load_res = LoadUSDCFromFile(tmp_name.data(), ropts_usdc);
        const auto t_temp_parse_end = Clock::now();
        std::remove(tmp_name.data());
        if (!load_res.success) {
          emit_lines(load_res.error_summary.empty() ? std::string("usdc parse failed")
                                                   : load_res.error_summary,
                    "ERR : ");
          return 1;
        }
        if (!load_res.warnings.empty()) {
          for (const auto& warning : load_res.warnings) {
            emit_lines(warning, "WARN : ");
          }
        }
        if (timing) {
          const double write_ms = ms(t_temp_write_end - t_temp_write_start);
          const double parse_ms = ms(t_temp_parse_end - t_temp_parse_start);
          std::fprintf(stderr,
                       "[next_usdcat] usdc-write-tempfs=%.1fms usdc-parse-tempfs=%.1fms total=%.1fms\n",
                       write_ms, parse_ms, ms(t_temp_parse_end - t_start));
          if (write_res.bytes_written > 0 && parse_ms > 0.0) {
            std::fprintf(stderr,
                         "[next_usdcat] parsed %zu bytes from tempfs = %.0f MB/s\n",
                         write_res.bytes_written,
                         double(write_res.bytes_written) / 1048576.0 /
                             (parse_ms / 1000.0));
          }
        }
      }
      return 0;
    }

    USDAWriteOptions wopts;
    wopts.emit_custom = openusd_compat;
    wopts.num_threads = write_threads;
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
      res = WriteLayer(w, *layer, wopts);  // flushes before returning
    }
    const bool flush_ok = (std::fflush(fp) == 0);
    const bool close_ok = out_path ? (std::fclose(fp) == 0) : true;
    if (!res.success || !flush_ok || !close_ok) {
      std::fprintf(stderr, "ERR : %s\n",
                   res.error.empty() ? "write/flush failed (disk full?)"
                                     : res.error.c_str());
      return 1;
    }
    const auto t_written = Clock::now();
    if (timing) {
      const double pms = ms(t_parsed - t_start);
      const double wms = ms(t_written - t_parsed);
      std::fprintf(stderr,
                   "[next_usdcat] parse=%.1fms write=%.1fms total=%.1fms\n",
                   pms, wms, ms(t_written - t_start));
      // RAW parse throughput is relative to the input file size.
      long insz = 0;
      if (std::FILE* inf = std::fopen(filename, "rb")) {
        std::fseek(inf, 0, SEEK_END);
        insz = std::ftell(inf);
        std::fclose(inf);
      }
      if (insz > 0 && pms > 0.0) {
        std::fprintf(stderr, "[next_usdcat] parsed %ld bytes = %.0f MB/s\n",
                     insz, double(insz) / 1048576.0 / (pms / 1000.0));
      }
      if (res.bytes_written > 0 && wms > 0.0) {
        std::fprintf(stderr, "[next_usdcat] wrote %zu bytes = %.0f MB/s%s%s\n",
                     res.bytes_written,
                     double(res.bytes_written) / 1048576.0 / (wms / 1000.0),
                     out_path ? " -> " : "", out_path ? out_path : "");
      }
    }
    return 0;
  }

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
    // Parallel compose (pre-warm sources_cache) is OPT-IN via --compose-threads N
    // and byte-identical to serial. Default 1 = serial (no threading).
    opts.num_threads = compose_threads;
    // Forward the CLI timing flag to the library (which no longer reads the env):
    // gates the [next_compose]/[next_build]/[next_warm] diagnostics.
    opts.enable_timing = timing;
    ok = pcp::ComposeStageFromFile(filename, resolver, &stage, opts, &warn, &err);
  } else {
    LoadUSDOptions load_opts;
    load_opts.usdc_options = usdc_opts;
    ok = LoadUSD(filename, &stage, load_opts, &warn, &err);
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

  // USDC output: when -o targets a .usdc/.usdz path, write the composed stage as
  // a binary crate (benchmark vehicle for the next crate writer).
  if (flatten && (write_usdc_memory || (out_path && IsUSDCPath(out_path)))) {
    USDCWriteOptions copts;
    copts.crate_options.num_threads = write_threads;
    const auto t_w0 = Clock::now();
    USDCWriteResult res;
    std::vector<uint8_t> memory;
    if (write_usdc_memory) {
      res = WriteUSDCToMemory(memory, stage, copts);
    } else {
      res = WriteUSDCToFile(out_path, stage, copts);
    }
    const auto t_written = Clock::now();
    if (!res.success) {
      std::fprintf(stderr, "ERR : %s\n",
                   res.error.empty() ? "usdc write failed" : res.error.c_str());
      return 1;
    }
    if (timing) {
      const double wms = ms(t_written - t_w0);
      std::fprintf(stderr,
                   "[next_usdcat] load+compose=%.1fms write=%.1fms total=%.1fms\n",
                   ms(t_loaded - t_start), wms, ms(t_written - t_start));
      if (res.bytes_written > 0 && wms > 0.0) {
        std::fprintf(stderr,
                     "[next_usdcat] wrote %zu bytes = %.0f MB/s -> %s "
                     "(tokens=%zu paths=%zu specs=%zu)\n",
                     res.bytes_written,
                     double(res.bytes_written) / 1048576.0 / (wms / 1000.0),
                     write_usdc_memory ? "<memory>" : out_path,
                     res.token_count, res.path_count, res.spec_count);
      }
    }
    return 0;
  }

  if (flatten) {
    USDAWriteOptions wopts;
    wopts.emit_custom = openusd_compat;
    // Parallel subtree serialization (only effective in a TINYUSDZ_ENABLE_THREAD
    // build). 0 = auto, >0 = fixed threads. Output is byte-identical regardless
    // of count.
    wopts.num_threads = write_threads;
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
    const bool flush_ok = (std::fflush(fp) == 0);
    const bool close_ok = out_path ? (std::fclose(fp) == 0) : true;
    if (!res.success || !flush_ok || !close_ok) {
      std::fprintf(stderr, "ERR : %s\n",
                   res.error.empty() ? "write/flush failed (disk full?)"
                                     : res.error.c_str());
      return 1;
    }
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
