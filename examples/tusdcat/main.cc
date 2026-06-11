#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

#if !defined(_WIN32)
#include <sys/resource.h>
#endif

#include "tinyusdz.hh"
#include "layer.hh"
#include "pprinter.hh"
#include "str-util.hh"
#include "io-util.hh"
#include "usd-to-json.hh"
#include "usd-dump.hh"
#include "logger.hh"
#include "crate-dump.hh"
#include "usdc-reader.hh"
#include "usdc-writer.hh"
#include "usda-writer.hh"
#include "usd-validation.hh"

#include "tydra/scene-access.hh"
#include "variant-format.hh"
#include "comp-graph-dump.hh"

struct CompositionFeatures {
  bool subLayers{true};
  bool inherits{true};
  bool variantSets{true};
  bool references{true};
  bool payload{true}; // Not 'payloads'
  bool specializes{true};
};

enum class OutputFormat {
  Infer,
  USDA,
  USDC,
  USDZ
};

static std::string GetFileExtension(const std::string &filename) {
  if (filename.find_last_of('.') != std::string::npos)
    return filename.substr(filename.find_last_of('.') + 1);
  return "";
}

static std::string str_tolower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); }
  );
  return s;
}

static bool ParseOutputFormat(const std::string &value, OutputFormat *format) {
  if (!format) {
    return false;
  }

  const std::string fmt = str_tolower(value);
  if (fmt == "usda") {
    *format = OutputFormat::USDA;
    return true;
  }
  if (fmt == "usdc") {
    *format = OutputFormat::USDC;
    return true;
  }
  if (fmt == "usdz") {
    *format = OutputFormat::USDZ;
    return true;
  }

  return false;
}

static bool InferOutputFormatFromFilename(const std::string &filename,
                                          OutputFormat *format,
                                          std::string *err) {
  if (!format) {
    if (err) {
      (*err) = "`format` is nullptr.";
    }
    return false;
  }

  const std::string lower = str_tolower(filename);
  if (tinyusdz::endsWith(lower, ".usda") ||
      tinyusdz::endsWith(lower, ".usda.zst")) {
    *format = OutputFormat::USDA;
    return true;
  }
  if (tinyusdz::endsWith(lower, ".usdc") ||
      tinyusdz::endsWith(lower, ".usdc.zst")) {
    *format = OutputFormat::USDC;
    return true;
  }
  if (tinyusdz::endsWith(lower, ".usdz")) {
    *format = OutputFormat::USDZ;
    return true;
  }

  if (err) {
    (*err) =
        "Failed to infer output format from filename `" + filename +
        "`. Use .usda, .usdc, .usdz (or .usda.zst/.usdc.zst), or specify "
        "--output-format=usda|usdc|usdz.";
  }
  return false;
}

static std::string format_memory_size(size_t bytes) {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  int unit_index = 0;
  double size = static_cast<double>(bytes);

  while (size >= 1024.0 && unit_index < 4) {
    size /= 1024.0;
    unit_index++;
  }

  std::stringstream ss;
  if (unit_index == 0) {
    ss << static_cast<size_t>(size) << " " << units[unit_index];
  } else {
    ss.precision(2);
    ss << std::fixed << size << " " << units[unit_index];
  }
  return ss.str();
}

// Memory cap for USDA *text* output of a composed stage. USDA serialization of a
// deeply-composed stage (e.g. baked vertex-animation timeSamples) can balloon to
// many GB; emitting it as one std::string then exhausts memory and the process
// is OOM-killed/aborted. When the composed stage's estimated size exceeds this
// cap we serialize to compact USDC in memory instead of the huge USDA text.
// Configurable via env `TUSDCAT_MAX_USDA_MB` (0 = unlimited). Default 0.
static size_t GetMaxUsdaOutputBytes() {
  const char *e = std::getenv("TUSDCAT_MAX_USDA_MB");
  if (!e || !e[0]) {
    return 0;  // unlimited (preserve existing behavior unless opted in)
  }
  char *end = nullptr;
  unsigned long long mb = std::strtoull(e, &end, 10);
  if (end == e) {
    return 0;
  }
  return static_cast<size_t>(mb) * 1024ull * 1024ull;
}

static bool WriteStageToFile(const tinyusdz::Stage &stage,
                             const std::string &output_path,
                             OutputFormat format,
                             bool compress_float_arrays = false) {
  std::string warn;
  std::string err;

  switch (format) {
    case OutputFormat::USDA:
      if (!tinyusdz::usda::SaveAsUSDA(output_path, stage, &warn, &err)) {
        std::cerr << "Failed to write USDA file: " << err << "\n";
        return false;
      }
      break;
    case OutputFormat::USDC: {
      tinyusdz::USDWriteOptions wopts;
      wopts.compress_float_arrays = compress_float_arrays;
      if (!tinyusdz::usdc::SaveAsUSDCToFile(output_path, stage, &warn, &err,
                                            wopts)) {
        std::cerr << "Failed to write USDC file: " << err << "\n";
        return false;
      }
      break;
    }
    case OutputFormat::USDZ: {
      const std::map<std::string, std::vector<uint8_t>> assets;
      if (!tinyusdz::SaveAsUSDZToFile(output_path, stage, assets, &warn, &err)) {
        std::cerr << "Failed to write USDZ file: " << err << "\n";
        return false;
      }
      std::cout << "Wrote USDZ to [" << output_path << "]\n";
      break;
    }
    case OutputFormat::Infer:
      std::cerr << "Internal error: output format was not resolved.\n";
      return false;
  }

  if (!warn.empty()) {
    std::cerr << "WARN: " << warn << "\n";
  }

  return true;
}

// Progress bar state
struct ProgressState {
  std::chrono::steady_clock::time_point start_time;
  bool display_started{false};
  float last_progress{0.0f};
  static constexpr int kBarWidth = 40;
  static constexpr double kDelaySeconds = 3.0;  // Don't show progress under 3 seconds
};

static bool progress_callback(float progress, void *userptr) {
  ProgressState *state = static_cast<ProgressState*>(userptr);
  if (!state) {
    return true;
  }

  auto now = std::chrono::steady_clock::now();
  double elapsed = std::chrono::duration<double>(now - state->start_time).count();

  // Don't show progress if loading takes less than 3 seconds
  if (elapsed < ProgressState::kDelaySeconds) {
    return true;
  }

  // Only update display if progress changed significantly (1% or more)
  if (progress - state->last_progress < 0.01f && progress < 1.0f) {
    return true;
  }
  state->last_progress = progress;

  if (!state->display_started) {
    state->display_started = true;
    std::cerr << "\n";  // Start on new line
  }

  int percent = static_cast<int>(progress * 100.0f);
  int filled = static_cast<int>(progress * ProgressState::kBarWidth);

  std::cerr << "\r[";
  for (int i = 0; i < ProgressState::kBarWidth; ++i) {
    if (i < filled) {
      std::cerr << "=";
    } else if (i == filled) {
      std::cerr << ">";
    } else {
      std::cerr << " ";
    }
  }
  std::cerr << "] " << std::setw(3) << percent << " %" << std::flush;

  if (progress >= 1.0f) {
    std::cerr << "\n";  // Finish with newline
  }

  return true;  // Continue parsing
}

static bool LoadUSDCWithMemoryReport(
    const std::string &filepath, const bool show_progress, tinyusdz::Stage *stage,
    tinyusdz::usdc::USDCMemoryUsageReport *memory_report, std::string *warn,
    std::string *err) {
  if (!stage) {
    if (err) {
      (*err) = "`stage` is nullptr.";
    }
    return false;
  }

  // Use mmap when available to avoid 188MB+ heap allocation for file data
  tinyusdz::io::MMapFileHandle mmap_handle;
  std::vector<uint8_t> data;
  const uint8_t *file_data = nullptr;
  size_t file_size = 0;
  std::string local_err;

  if (tinyusdz::io::IsMMapSupported()) {
    if (!tinyusdz::io::MMapFile(filepath, &mmap_handle, /* writable */ false, &local_err)) {
      if (err) {
        (*err) = "Failed to mmap file: " + local_err;
      }
      return false;
    }
    file_data = mmap_handle.addr;
    file_size = static_cast<size_t>(mmap_handle.size);
  } else {
    if (!tinyusdz::io::ReadWholeFile(&data, &local_err, filepath)) {
      if (err) {
        (*err) = local_err;
      }
      return false;
    }
    file_data = data.data();
    file_size = data.size();
  }

  tinyusdz::StreamReader sr(file_data, file_size, /* swap_endian */ false);
  tinyusdz::usdc::USDCReaderConfig config;
  if (const char *lazy_env = std::getenv("TINYUSDZ_USDC_LAZY")) {
    std::string v = str_tolower(std::string(lazy_env));
    if ((v == "0") || (v == "false") || (v == "off") || (v == "no")) {
      config.use_lazy_property_construction = false;
    } else if ((v == "1") || (v == "true") || (v == "on") || (v == "yes")) {
      config.use_lazy_property_construction = true;
    }
  }
  tinyusdz::usdc::USDCReader reader(&sr, config);

  ProgressState progress_state;
  if (show_progress) {
    progress_state.start_time = std::chrono::steady_clock::now();
    reader.SetProgressCallback(progress_callback, &progress_state);
  }

  if (!reader.ReadUSDC()) {
    if (warn) {
      (*warn) = reader.GetWarning();
    }
    if (err) {
      (*err) = reader.GetError();
    }
    if (mmap_handle.addr) {
      tinyusdz::io::UnmapFile(mmap_handle, &local_err);
    }
    return false;
  }

  if (!reader.ReconstructStage(stage)) {
    if (warn) {
      (*warn) = reader.GetWarning();
    }
    if (err) {
      (*err) = reader.GetError();
    }
    if (mmap_handle.addr) {
      tinyusdz::io::UnmapFile(mmap_handle, &local_err);
    }
    return false;
  }

  // Capture memory report AFTER ReconstructStage so peak includes
  // all DecodeFieldSet → UnpackValueRep allocations during property parsing.
  if (memory_report) {
    (*memory_report) = reader.GetMemoryUsageReport();
  }

  if (warn) {
    (*warn) = reader.GetWarning();
  }
  if (err) {
    (*err) = reader.GetError();
  }

  if (mmap_handle.addr) {
    tinyusdz::io::UnmapFile(mmap_handle, &local_err);
  }

  return true;
}

static void PrintUSDCParserMemoryReport(
    const tinyusdz::usdc::USDCMemoryUsageReport &report) {
  std::cout << "  USDC parser current usage: "
            << format_memory_size(size_t(report.current_usage_bytes)) << " ("
            << report.current_usage_bytes << " bytes)\n";
  std::cout << "  USDC parser peak usage:    "
            << format_memory_size(size_t(report.peak_usage_bytes)) << " ("
            << report.peak_usage_bytes << " bytes)\n";
  std::cout << "  USDC memory budget:        "
            << format_memory_size(size_t(report.max_budget_bytes)) << " ("
            << report.max_budget_bytes << " bytes)\n";
  std::cout << "  USDC budget remaining:     "
            << format_memory_size(size_t(report.remaining_budget_bytes)) << " ("
            << report.remaining_budget_bytes << " bytes)\n";

  if (report.max_budget_bytes > 0) {
    double ratio =
        100.0 * (double(report.peak_usage_bytes) / double(report.max_budget_bytes));
    std::cout << "  USDC peak/budget ratio:    " << std::fixed
              << std::setprecision(2) << ratio << " %\n";
  }
}

void print_help() {
  std::cout << "Usage: tusdcat [OPTIONS] input.usda/usdc/usdz\n";
  std::cout << "\n";
  std::cout << "Options:\n";
  std::cout << "  -h, --help          Show this help message\n";
  std::cout << "  -f, --flatten       Do composition (load sublayers, references,\n";
  std::cout << "                      payload, evaluate `over`, inherit, variants)\n";
  std::cout << "                      (not fully implemented yet)\n";
  std::cout << "  --composition=LIST  Specify which composition features to enable\n";
  std::cout << "                      (valid when --flatten is supplied).\n";
  std::cout << "                      Comma-separated list of:\n";
  std::cout << "                        l or subLayers, i or inherits,\n";
  std::cout << "                        v or variantSets, r or references,\n";
  std::cout << "                        p or payload, s or specializes\n";
  std::cout << "                      Example: --composition=r,p\n";
  std::cout << "  --extract-variants  Dump variants information to JSON (w.i.p)\n";
  std::cout << "  --relative          Print Path as relative Path (not implemented)\n";
  std::cout << "  -l, --loadOnly      Load/parse USD file only (validate input)\n";
  std::cout << "  -j, --json          Output parsed USD as JSON string\n";
  std::cout << "  -o, --output FILE   Write output to FILE\n";
  std::cout << "  --output-format FMT Output format: usda, usdc, usdz\n";
  std::cout << "                      Default: infer from output filename extension\n";
  std::cout << "  --compress-float-arrays\n";
  std::cout << "                      Enable OpenUSD-compatible tagged compression\n";
  std::cout << "                      for float[]/double[] arrays in USDC output\n";
  std::cout << "                      (default off).\n";
  std::cout << "  --memstat           Print memory usage statistics\n";
  std::cout << "                      (includes USDC parser budget report for .usdc)\n";
  std::cout << "  --no-asset-path-fallback Disable suffix-fallback rebasing of "
               "unresolvable composition asset paths\n";
  std::cout << "  --error-detail      Show full error stack and full source lines\n";
  std::cout << "                      (disable stack snipping and line truncation)\n";
  std::cout << "  --progress          Show ASCII progress bar\n";
  std::cout << "                      (only shown if loading takes > 3 seconds)\n";
  std::cout << "  --loglevel INT      Set logging level:\n";
  std::cout << "                        0=Debug, 1=Warn, 2=Info,\n";
  std::cout << "                        3=Error, 4=Critical, 5=Off\n";
  std::cout << "\n";
  std::cout << "Inspect options (YAML-like tree output):\n";
  std::cout << "  --inspect           Inspect Layer structure (YAML-like output)\n";
  std::cout << "  --inspect-json      Inspect Layer structure (JSON output)\n";
  std::cout << "  --value=MODE        Value printing mode:\n";
  std::cout << "                        none = schema only, no values\n";
  std::cout << "                        snip = first N items (default)\n";
  std::cout << "                        full = all values\n";
  std::cout << "  --snip=N            Show first N items in snip mode (default: 8)\n";
  std::cout << "  --path=PATTERN      Filter prims by path glob pattern\n";
  std::cout << "                      (* = any chars, ** = any path segments)\n";
  std::cout << "  --attr=PATTERN      Filter attributes by name glob pattern\n";
  std::cout << "  --time=T            Query TimeSamples at time T\n";
  std::cout << "  --time=S:E          Query TimeSamples in range [S, E]\n";
  std::cout << "\n";
  std::cout << "Low-level USDC dump options:\n";
  std::cout << "  --dumpcrate         Dump low-level USDC Crate structure (YAML)\n";
  std::cout << "                      Only works with .usdc files\n";
  std::cout << "\n";
  std::cout << "MaterialX validation options:\n";
  std::cout << "  --strict-mtlx-check Enable strict MaterialX validation\n";
  std::cout << "                      (validates info:id, index bounds, etc.)\n";
  std::cout << "  --validate         Validate against AOUSD Core semantic rules\n";
  std::cout << "                      (core schemas/metadata; binary inputs add Crate/USDZ checks)\n";
  std::cout << "  --validate-all     Validate with all rule groups (core + geom + shade + lux + physics + crate)\n";
  std::cout << "                      (adds geom/shade/lux/physics/crate checks; warning-heavy)\n";
  std::cout << "\n";
  std::cout << "Composition graph dump options:\n";
  std::cout << "  --dump-comp-graph[=FMT]  Dump composition graph\n";
  std::cout << "                           FMT: yaml (default), json, dot\n";
  std::cout << "  --comp-graph-recursive   Follow external references recursively\n";
  std::cout << "  --comp-graph-no-payload  Skip payload arcs (payload off mode)\n";
  std::cout << "  Combined with -l: parse-only mode (validate all files)\n";
  std::cout << "  Combined with --memstat: per-file memory report\n";
}

int main(int argc, char **argv) {
  // Set 32GB virtual memory limit to prevent OOM / memory thrashing
#if !defined(_WIN32)
  {
    struct rlimit mem_limit;
    mem_limit.rlim_cur = static_cast<rlim_t>(32) * 1024 * 1024 * 1024;  // 32 GB
    mem_limit.rlim_max = static_cast<rlim_t>(32) * 1024 * 1024 * 1024;
    setrlimit(RLIMIT_AS, &mem_limit);
  }
#endif

  // Enable DCOUT output if TINYUSDZ_ENABLE_DCOUT environment variable is set
  const char* enable_dcout_env = std::getenv("TINYUSDZ_ENABLE_DCOUT");
  if (enable_dcout_env != nullptr && std::strlen(enable_dcout_env) > 0) {
    // Any non-empty value enables DCOUT
    tinyusdz::g_enable_dcout_output = true;
  }

  if (argc < 2) {
    print_help();
    return EXIT_FAILURE;
  }

  bool has_flatten{false};
  bool has_relative{false};
  bool has_extract_variants{false};
  bool load_only{false};
  std::string variant_format = "yaml";  // Default format: yaml
  bool json_output{false};
  bool memstat{false};
  bool error_detail{false};
  bool show_progress{false};
  bool asset_path_fallback{true};
  bool compress_float_arrays{false};
  OutputFormat output_format{OutputFormat::Infer};

  // Inspect options
  bool do_inspect{false};
  tinyusdz::InspectOptions inspect_opts;

  // Dumpcrate option
  bool do_dumpcrate{false};

  // MaterialX validation
  bool strict_mtlx_check{false};
  bool validate_against_core{false};
  bool validate_all_groups{false};

  // Composition graph dump
  bool do_dump_comp_graph{false};
  std::string comp_graph_format = "yaml";
  bool comp_graph_recursive{false};
  bool comp_graph_no_payload{false};

  constexpr int kMaxIteration = 128;

  std::string filepath;
  std::string output_filepath;

  int input_index = -1;
  CompositionFeatures comp_features;

  for (size_t i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if ((arg.compare("-h") == 0) || (arg.compare("--help") ==0)) {
      print_help();
      return EXIT_FAILURE;
    } else if ((arg.compare("-f") == 0) || (arg.compare("--flatten") == 0)) {
      has_flatten = true;
    } else if (arg.compare("--relative") == 0) {
      has_relative = true;
    } else if ((arg.compare("-l") == 0) || (arg.compare("--loadOnly") == 0)) {
      load_only = true;
    } else if ((arg.compare("-j") == 0) || (arg.compare("--json") == 0)) {
      json_output = true;
    } else if ((arg.compare("-o") == 0) || (arg.compare("--output") == 0)) {
      if (i + 1 >= argc) {
        std::cerr << "-o/--output requires a filename argument\n";
        return EXIT_FAILURE;
      }
      i++; // Move to next argument
      output_filepath = argv[i];
    } else if (tinyusdz::startsWith(arg, "--output-format=")) {
      std::string fmt = tinyusdz::removePrefix(arg, "--output-format=");
      if (fmt.empty()) {
        std::cerr << "No format specified to --output-format.\n";
        return EXIT_FAILURE;
      }
      if (!ParseOutputFormat(fmt, &output_format)) {
        std::cerr << "Invalid output format: " << fmt
                  << ". Must be 'usda', 'usdc', or 'usdz'.\n";
        return EXIT_FAILURE;
      }
    } else if (arg.compare("--compress-float-arrays") == 0) {
      compress_float_arrays = true;
    } else if (arg.compare("--extract-variants") == 0) {
      has_extract_variants = true;
    } else if (tinyusdz::startsWith(arg, "--variant-format=")) {
      std::string fmt = tinyusdz::removePrefix(arg, "--variant-format=");
      if (fmt.empty()) {
        std::cerr << "No format specified to --variant-format.\n";
        exit(-1);
      }
      std::string fmt_lower = str_tolower(fmt);
      if (fmt_lower == "yaml" || fmt_lower == "json") {
        variant_format = fmt_lower;
      } else {
        std::cerr << "Invalid variant format: " << fmt << ". Must be 'yaml' or 'json'.\n";
        exit(-1);
      }
    } else if (arg.compare("--memstat") == 0) {
      memstat = true;
    } else if (arg.compare("--no-asset-path-fallback") == 0) {
      asset_path_fallback = false;
    } else if (arg.compare("--error-detail") == 0) {
      error_detail = true;
    } else if (arg.compare("--progress") == 0) {
      show_progress = true;
    } else if (arg.compare("--dumpcrate") == 0) {
      do_dumpcrate = true;
    } else if (arg.compare("--strict-mtlx-check") == 0) {
      strict_mtlx_check = true;
    } else if (arg.compare("--validate") == 0) {
      validate_against_core = true;
    } else if (arg.compare("--validate-all") == 0) {
      validate_against_core = true;
      validate_all_groups = true;
    } else if (tinyusdz::startsWith(arg, "--dump-comp-graph")) {
      do_dump_comp_graph = true;
      std::string rest = arg.substr(strlen("--dump-comp-graph"));
      if (rest.empty() || rest == "=yaml") {
        comp_graph_format = "yaml";
      } else if (rest == "=json") {
        comp_graph_format = "json";
      } else if (rest == "=dot") {
        comp_graph_format = "dot";
      } else {
        std::cerr << "Invalid format for --dump-comp-graph. Use: json, yaml, or dot\n";
        return EXIT_FAILURE;
      }
    } else if (arg.compare("--comp-graph-recursive") == 0) {
      comp_graph_recursive = true;
    } else if (arg.compare("--comp-graph-no-payload") == 0) {
      comp_graph_no_payload = true;
    } else if (arg.compare("--inspect") == 0) {
      do_inspect = true;
      inspect_opts.format = tinyusdz::InspectOutputFormat::Yaml;
    } else if (arg.compare("--inspect-json") == 0) {
      do_inspect = true;
      inspect_opts.format = tinyusdz::InspectOutputFormat::Json;
    } else if (tinyusdz::startsWith(arg, "--value=")) {
      std::string value_str = tinyusdz::removePrefix(arg, "--value=");
      if (value_str == "none") {
        inspect_opts.value_mode = tinyusdz::InspectValueMode::NoValue;
      } else if (value_str == "snip") {
        inspect_opts.value_mode = tinyusdz::InspectValueMode::Snip;
      } else if (value_str == "full") {
        inspect_opts.value_mode = tinyusdz::InspectValueMode::Full;
      } else {
        std::cerr << "Invalid value mode: " << value_str
                  << ". Use: none, snip, or full\n";
        return EXIT_FAILURE;
      }
    } else if (tinyusdz::startsWith(arg, "--snip=")) {
      std::string snip_str = tinyusdz::removePrefix(arg, "--snip=");
      nonstd::optional<int> snip_val = tinyusdz::atoi(snip_str);
      if (!snip_val.has_value()) {
        std::cerr << "Invalid snip value: " << snip_str << "\n";
        return EXIT_FAILURE;
      }
      if (snip_val.value() < 1) {
        std::cerr << "Invalid snip value: " << snip_val.value()
                  << ". Must be >= 1\n";
        return EXIT_FAILURE;
      }
      inspect_opts.snip_count = static_cast<size_t>(snip_val.value());
    } else if (tinyusdz::startsWith(arg, "--path=")) {
      inspect_opts.prim_path_pattern = tinyusdz::removePrefix(arg, "--path=");
    } else if (tinyusdz::startsWith(arg, "--attr=")) {
      inspect_opts.attr_pattern = tinyusdz::removePrefix(arg, "--attr=");
    } else if (tinyusdz::startsWith(arg, "--time=")) {
      std::string time_str = tinyusdz::removePrefix(arg, "--time=");
      inspect_opts.has_time_query = true;
      // Check for range format "start:end"
      size_t colon_pos = time_str.find(':');
      if (colon_pos != std::string::npos) {
        std::string start_str = time_str.substr(0, colon_pos);
        std::string end_str = time_str.substr(colon_pos + 1);
        nonstd::optional<double> t_start = tinyusdz::atod(start_str);
        nonstd::optional<double> t_end = tinyusdz::atod(end_str);
        if (!t_start.has_value() || !t_end.has_value()) {
          std::cerr << "Invalid time range: " << time_str << "\n";
          return EXIT_FAILURE;
        }
        inspect_opts.time_start = t_start.value();
        inspect_opts.time_end = t_end.value();
      } else {
        // Single time value
        nonstd::optional<double> t = tinyusdz::atod(time_str);
        if (!t.has_value()) {
          std::cerr << "Invalid time value: " << time_str << "\n";
          return EXIT_FAILURE;
        }
        inspect_opts.time_start = t.value();
        inspect_opts.time_end = t.value();
      }
    } else if (arg.compare("--loglevel") == 0) {
      if (i + 1 >= argc) {
        std::cerr << "--loglevel requires an integer argument\n";
        return EXIT_FAILURE;
      }
      i++; // Move to next argument
      {
        nonstd::optional<int> log_level = tinyusdz::atoi(argv[i]);
        if (!log_level.has_value()) {
          std::cerr << "Invalid log level argument: " << argv[i] << ". Must be an integer.\n";
          return EXIT_FAILURE;
        }
        int ll = log_level.value();
        if (ll < 0 || ll > 5) {
          std::cerr << "Invalid log level: " << ll << ". Must be between 0 and 5.\n";
          return EXIT_FAILURE;
        }
        tinyusdz::logging::Logger::getInstance().setLogLevel(
            static_cast<tinyusdz::logging::LogLevel>(ll));
      }
    } else if (tinyusdz::startsWith(arg, "--composition=")) {
      std::string value_str = tinyusdz::removePrefix(arg, "--composition=");
      if (value_str.empty()) {
        std::cerr << "No values specified to --composition.\n";
        exit(-1);
      }

      std::vector<std::string> items = tinyusdz::split(value_str, ",");
      comp_features.subLayers = false;
      comp_features.inherits = false;
      comp_features.variantSets = false;
      comp_features.references = false;
      comp_features.payload = false;
      comp_features.specializes = false;

      for (const auto &item : items) {
        if ((item == "l") || (item == "subLayers")) {
          comp_features.subLayers = true;
        } else if ((item == "i") || (item == "inherits")) {
          comp_features.inherits = true;
        } else if ((item == "v") || (item == "variantSets")) {
          comp_features.variantSets = true;
        } else if ((item == "r") || (item == "references")) {
          comp_features.references = true;
        } else if ((item == "p") || (item == "payload")) {
          comp_features.payload = true;
        } else if ((item == "s") || (item == "specializes")) {
          comp_features.specializes = true;
        } else {
          std::cerr << "Invalid string for --composition : " << item << "\n";
          exit(-1);
        }
      }

    } else {
      filepath = arg;
      input_index = i;
    }
  }

  if (filepath.empty() || (input_index < 0)) {
    std::cout << "Input USD filename missing.\n";
    return EXIT_FAILURE;
  }

  std::string warn;
  std::string err;

  std::string ext = str_tolower(GetFileExtension(filepath));
  const bool has_output_file = !output_filepath.empty();
  const bool suppress_usd_text_output = has_output_file;
  std::string base_dir;
  base_dir = tinyusdz::io::GetBaseDir(filepath);

  if ((output_format != OutputFormat::Infer) && !has_output_file) {
    std::cerr << "--output-format requires -o/--output.\n";
    return EXIT_FAILURE;
  }

  if (has_output_file && (output_format == OutputFormat::Infer)) {
    if (!InferOutputFormatFromFilename(output_filepath, &output_format, &err)) {
      std::cerr << err << "\n";
      return EXIT_FAILURE;
    }
  }

  if (validate_against_core) {
    if (has_flatten || do_dumpcrate || do_dump_comp_graph || do_inspect ||
        json_output || has_extract_variants || !output_filepath.empty()) {
      std::cerr
          << "--validate cannot be combined with other output/transform modes\n";
      return EXIT_FAILURE;
    }

    tinyusdz::USDLoadOptions options;
    options.error_detail = error_detail;

    tinyusdz::ValidationOptions validation_options;
    if (validate_all_groups) {
      validation_options = tinyusdz::MakeValidateAllOptions();
    }

    tinyusdz::USDValidationResult validation;
    const bool ret = tinyusdz::ValidateUSDFileAgainstAOUSDCore(
        filepath, validation_options, options, &validation, &warn, &err);
    if (!warn.empty()) {
      std::cerr << "WARN: " << warn << "\n";
    }

    if (!ret) {
      std::cerr << "Failed to load USD file as Layer: " << filepath << "\n";
      if (!err.empty()) {
        std::cerr << err << "\n";
      }
      return EXIT_FAILURE;
    }

    std::cout << tinyusdz::FormatValidationResult(validation);
    return validation.ok() ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  // Handle --dumpcrate mode (low-level USDC crate dump)
  if (do_dumpcrate) {
    if (ext != "usdc") {
      std::cerr << "Error: --dumpcrate only works with .usdc files\n";
      std::cerr << "  Input file: " << filepath << "\n";
      std::cerr << "  Extension: ." << ext << "\n";
      return EXIT_FAILURE;
    }

    tinyusdz::crate::DumpOptions dump_opts;
    dump_opts.format = tinyusdz::crate::OutputFormat::YAML;

    if (!tinyusdz::crate::DumpCrate(filepath, dump_opts, &err)) {
      std::cerr << "Failed to dump crate: " << err << "\n";
      return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
  }

  // Handle --dump-comp-graph mode
  if (do_dump_comp_graph) {
    comp_graph_dump::ExtractOptions opts;
    opts.skip_payloads = comp_graph_no_payload;
    opts.parse_only = load_only;
    opts.track_memory = memstat;

    comp_graph_dump::CompGraphDump graph;

    if (comp_graph_recursive) {
      std::string rec_warn, rec_err;
      if (!comp_graph_dump::ExtractCompGraphRecursive(filepath, &graph, opts,
                                                      &rec_warn, &rec_err)) {
        std::cerr << "Failed to extract composition graph: " << rec_err << "\n";
        return EXIT_FAILURE;
      }
      if (!rec_warn.empty()) {
        std::cerr << rec_warn;
      }
    } else {
      // Single file mode
      tinyusdz::Layer layer;
      bool loaded = tinyusdz::LoadLayerFromFile(filepath, &layer, &warn, &err);

      if (!warn.empty()) {
        std::cerr << "WARN: " << warn << "\n";
      }

      if (load_only) {
        // Parse-only single file: use recursive with depth=1
        comp_graph_dump::ExtractOptions single_opts = opts;
        comp_graph_dump::ExtractCompGraphRecursive(filepath, &graph, single_opts,
                                                    &warn, &err);
      } else {
        if (!loaded) {
          std::cerr << "Failed to load USD file as Layer: " << filepath << "\n";
          if (!err.empty()) {
            std::cerr << err << "\n";
          }
          return EXIT_FAILURE;
        }

        if (!comp_graph_dump::ExtractCompGraph(layer, filepath, &graph, &err)) {
          std::cerr << "Failed to extract composition graph: " << err << "\n";
          return EXIT_FAILURE;
        }

        // Apply memory tracking to the single node
        if (memstat && !graph.nodes.empty()) {
          graph.nodes[0].memory_usage =
              static_cast<int64_t>(layer.estimate_memory_usage());
        }
      }
    }

    graph.ComputeSizeSummary();

    if (comp_graph_format == "json") {
      std::cout << comp_graph_dump::CompGraphToJSON(graph);
    } else if (comp_graph_format == "yaml") {
      std::cout << comp_graph_dump::CompGraphToYAML(graph);
    } else if (comp_graph_format == "dot") {
      std::cout << comp_graph_dump::CompGraphToDOT(graph);
    }

    return EXIT_SUCCESS;
  }

  // Handle --inspect mode
  if (do_inspect) {
    // Load as Layer for inspection
    tinyusdz::Layer layer;
    bool ret = tinyusdz::LoadLayerFromFile(filepath, &layer, &warn, &err);

    if (!warn.empty()) {
      std::cerr << "WARN: " << warn << "\n";
    }

    if (!ret) {
      std::cerr << "Failed to load USD file as Layer: " << filepath << "\n";
      if (!err.empty()) {
        std::cerr << err << "\n";
      }
      return EXIT_FAILURE;
    }

    // Output inspection result
    std::string output = tinyusdz::InspectLayer(layer, inspect_opts);
    std::cout << output;

    return EXIT_SUCCESS;
  }

  if (has_flatten) {

    if (load_only) {
      std::cerr << "--flatten and --loadOnly cannot be specified at a time\n";
      return EXIT_FAILURE;
    }

    // TODO: flatten for USDZ
    if (tinyusdz::IsUSDZ(filepath)) {

      std::cout << "--flatten is ignored for USDZ at the moment.\n";

      tinyusdz::Stage stage;
      tinyusdz::USDLoadOptions usdz_options;

      // MaterialX validation
      usdz_options.strict_mtlx_check = strict_mtlx_check;
      usdz_options.error_detail = error_detail;

      // Set up progress callback if requested
      ProgressState usdz_progress_state;
      if (show_progress) {
        usdz_progress_state.start_time = std::chrono::steady_clock::now();
        usdz_options.progress_callback = progress_callback;
        usdz_options.progress_userptr = &usdz_progress_state;
      }

      bool ret = tinyusdz::LoadUSDZFromFile(filepath, &stage, &warn, &err, usdz_options);
      if (!warn.empty()) {
        std::cerr << "WARN : " << warn << "\n";
      }
      if (!err.empty()) {
        std::cerr << "ERR : " << err << "\n";
        //return EXIT_FAILURE;
      }

      if (!ret) {
        std::cerr << "Failed to load USDZ file: " << filepath << "\n";
        return EXIT_FAILURE;
      }

      if (memstat) {
        auto detail = stage.estimate_memory_usage_detail();
        std::cout << "# Memory Statistics (Stage from USDZ)\n";
        std::cout << "  Allocated (capacity): " << format_memory_size(detail.allocated_bytes)
                  << " (" << detail.allocated_bytes << " bytes)\n";
        std::cout << "  Actual (in use):      " << format_memory_size(detail.actual_bytes)
                  << " (" << detail.actual_bytes << " bytes)\n";
        if (detail.allocated_bytes > 0) {
          double efficiency = 100.0 * (double(detail.actual_bytes) / double(detail.allocated_bytes));
          std::cout << "  Efficiency:           " << std::fixed << std::setprecision(1)
                    << efficiency << " %\n";
        }
        std::cout << "\n";
      }

      if (json_output) {
#if defined(TINYUSDZ_WITH_JSON)
        auto json_result = tinyusdz::ToJSON(stage);
        if (json_result) {
          std::cout << json_result.value() << "\n";
        } else {
          std::cerr << "Failed to convert USDZ stage to JSON: " << json_result.error() << "\n";
          return EXIT_FAILURE;
        }
      } else if (!suppress_usd_text_output) {
        std::cout << to_string(stage) << "\n";
#else
      std::cerr << "JSON output is not supported in this build\n";
      return EXIT_FAILURE;
#endif
      }

      if (has_output_file) {
        if (!WriteStageToFile(stage, output_filepath, output_format,
                            compress_float_arrays)) {
          return EXIT_FAILURE;
        }
      }

      return EXIT_SUCCESS;
    }

    tinyusdz::Layer root_layer;
    bool ret = tinyusdz::LoadLayerFromFile(filepath, &root_layer, &warn, &err);
    if (warn.size()) {
      std::cerr << "WARN: " << warn << "\n"; warn.clear();
    }

    if (!ret) {
      std::cerr << "Failed to read USD data as Layer: \n";
      std::cerr << err << "\n";
      return -1;
    }

    if (memstat) {
      size_t layer_mem = root_layer.estimate_memory_usage();
      std::cout << "# Memory Statistics (Layer)\n";
      std::cout << "  Layer memory usage: " << format_memory_size(layer_mem) 
                << " (" << layer_mem << " bytes)\n\n";
    }

    if (!suppress_usd_text_output) {
      std::cout << "# input\n";
      std::cout << root_layer << "\n";
    }

    tinyusdz::Stage stage;
    stage.metas() = root_layer.metas();

    std::string warn;

    tinyusdz::AssetResolutionResolver resolver;
    resolver.set_current_working_path(base_dir);
    resolver.set_search_paths({base_dir});
    resolver.set_enable_suffix_fallback(asset_path_fallback);

    //
    // LIVRPS strength ordering
    // - [x] Local(subLayers)
    // - [x] Inherits
    // - [x] VariantSets
    // - [x] References
    // - [x] Payload
    // - [ ] Specializes
    //

    tinyusdz::Layer src_layer = root_layer;

    // tusdcat resolves assets against the local filesystem (the input file's
    // directory), where USD's parent-relative references (e.g.
    // `@../common/foo.usd@`) are legitimate and ubiquitous — OpenUSD resolves
    // them too. Allow '..' in composition asset paths.
    tinyusdz::SublayersCompositionOptions sublayer_opts;
    sublayer_opts.allow_parent_relative_paths = true;
    tinyusdz::ReferencesCompositionOptions reference_opts;
    reference_opts.allow_parent_relative_paths = true;
    tinyusdz::PayloadCompositionOptions payload_opts;
    payload_opts.allow_parent_relative_paths = true;

    // Whether to dump each INTERMEDIATE composited layer as USDA text per
    // iteration (debug aid). For heavy scenes this USDA serialization is itself
    // the blow-up (e.g. baked vertex-animation timeSamples), and it happens
    // inside the composition loop — before any post-loop memory cap. So when a
    // memory cap is set, skip these intermediate dumps; the final result is
    // still emitted (USDA, or compact USDC if over the cap) after the loop.
    const bool print_intermediate =
        !suppress_usd_text_output && (GetMaxUsdaOutputBytes() == 0);

    if (comp_features.subLayers) {
      tinyusdz::Layer composited_layer;
      if (!tinyusdz::CompositeSublayers(resolver, src_layer, &composited_layer, &warn, &err, sublayer_opts)) {
        std::cerr << "Failed to composite subLayers: " << err << "\n";
        return -1;
      }

      if (warn.size()) {
        std::cerr << "WARN: " << warn << "\n"; warn.clear();
      }

      if (print_intermediate) {
        std::cout << "# `subLayers` composited\n";
        std::cout << composited_layer << "\n";
      }

      src_layer = std::move(composited_layer);
    }

    // TODO: Find more better way to Recursively resolve references/payload/variants
    for (int i = 0; i < kMaxIteration; i++) {

      bool has_unresolved = false;

      if (comp_features.references) {
        if (!src_layer.check_unresolved_references()) {
          std::cout << "# iter " << i << ": no unresolved references.\n";
        } else {
          has_unresolved = true;

          tinyusdz::Layer composited_layer;
          // InPlace: consumes src_layer (no internal arcs) instead of holding
          // input + output copies — halves the peak of the pass.
          if (!tinyusdz::CompositeReferencesInPlace(resolver,
                  std::make_unique<tinyusdz::Layer>(std::move(src_layer)),
                  &composited_layer, &warn, &err, reference_opts)) {
            std::cerr << "Failed to composite `references`: " << err << "\n";
            return -1;
          }

          if (warn.size()) {
            std::cerr << "WARN: " << warn << "\n"; warn.clear();
          }

          if (print_intermediate) {
            std::cout << "# `references` composited\n";
            std::cout << composited_layer << "\n";
          }

          src_layer = std::move(composited_layer);
        }
      }

      if (comp_features.payload) {
        if (!src_layer.check_unresolved_payload()) {
          std::cout << "# iter " << i << ": no unresolved payload.\n";
        } else {
          has_unresolved = true;

          tinyusdz::Layer composited_layer;
          if (!tinyusdz::CompositePayloadInPlace(resolver,
                  std::make_unique<tinyusdz::Layer>(std::move(src_layer)),
                  &composited_layer, &warn, &err, payload_opts)) {
            std::cerr << "Failed to composite `payload`: " << err << "\n";
            return -1;
          }

          if (warn.size()) {
            std::cerr << "WARN: " << warn << "\n"; warn.clear();
          }

          if (print_intermediate) {
            std::cout << "# `payload` composited\n";
            std::cout << composited_layer << "\n";
          }

          src_layer = std::move(composited_layer);
        }
      }

      if (comp_features.inherits) {
        if (!src_layer.check_unresolved_inherits()) {
          std::cout << "# iter " << i << ": no unresolved inherits.\n";
        } else {
          has_unresolved = true;

          tinyusdz::Layer composited_layer;
          if (!tinyusdz::CompositeInherits(src_layer, &composited_layer, &warn, &err)) {
            std::cerr << "Failed to composite `inherits`: " << err << "\n";
            return -1;
          }

          if (warn.size()) {
            std::cerr << "WARN: " << warn << "\n"; warn.clear();
          }

          if (print_intermediate) {
            std::cout << "# `inherits` composited\n";
            std::cout << composited_layer << "\n";
          }

          src_layer = std::move(composited_layer);
        }
      }

      if (comp_features.variantSets) {
        if (!src_layer.check_unresolved_variant()) {
          std::cout << "# iter " << i << ": no unresolved variant.\n";
        } else {
          has_unresolved = true;

          tinyusdz::Layer composited_layer;
          if (!tinyusdz::CompositeVariant(src_layer, &composited_layer, &warn, &err)) {
            std::cerr << "Failed to composite `variantSet`: " << err << "\n";
            return -1;
          }

          if (warn.size()) {
            std::cerr << "WARN: " << warn << "\n"; warn.clear();
          }

          if (print_intermediate) {
            std::cout << "# `variantSet` composited\n";
            std::cout << composited_layer << "\n";
          }

          src_layer = std::move(composited_layer);
        }
      }

      // TODO
      // - [ ] specializes
      // - [ ] `class` Prim?

      std::cout << "# has_unresolved_references: " << src_layer.check_unresolved_references() << "\n";
      std::cout << "# all resolved? " << !has_unresolved << "\n";

      if (!has_unresolved) {
        std::cout << "# of composition iteration to resolve fully: " << (i + 1) << "\n";
        break;
      }

    }

    if (has_extract_variants) {
      std::cout << "\n=== VARIANT EXTRACTION (" << variant_format << ") ===\n";

      tinyusdz::Dictionary dict;
      if (!tinyusdz::ExtractVariants(src_layer, &dict, &err)) {
        std::cerr << "Failed to extract variants info: " << err;
      } else {
        if (variant_format == "json") {
          std::cout << variant_format::dictionary_to_json(dict) << "\n";
        } else {
          std::cout << variant_format::dictionary_to_yaml(dict) << "\n";
        }
      }

    }

    tinyusdz::Stage comp_stage;
    try {
      ret = LayerToStage(std::move(src_layer), &comp_stage, &warn, &err);
    } catch (const std::bad_alloc &) {
      // OOM detection: turn an allocation failure into a clean error instead of
      // an uncaught std::bad_alloc -> std::terminate -> abort().
      std::cerr << "ERR: out of memory while building the composed Stage. "
                   "Set TUSDCAT_MAX_USDA_MB or use USDC output for heavy scenes.\n";
      return EXIT_FAILURE;
    }
    if (warn.size()) {
      std::cout << warn<< "\n";
    }

    if (!ret) {
      std::cerr << err << "\n";
    }
    
    if (memstat) {
      size_t stage_mem = comp_stage.estimate_memory_usage();
      std::cout << "\n# Memory Statistics (Stage after composition)\n";
      std::cout << "  Stage memory usage: " << format_memory_size(stage_mem) 
                << " (" << stage_mem << " bytes)\n\n";
    }
    
    if (json_output) {
#if defined(TINYUSDZ_WITH_JSON)
      auto json_result = tinyusdz::ToJSON(comp_stage);
      if (json_result) {
        std::cout << json_result.value() << "\n";
      } else {
        std::cerr << "Failed to convert composed stage to JSON: " << json_result.error() << "\n";
        return EXIT_FAILURE;
      }
#else
      std::cerr << "JSON output is not supported in this build\n";
#endif
    } else if (!suppress_usd_text_output) {
      const size_t est_bytes = comp_stage.estimate_memory_usage();
      const size_t cap_bytes = GetMaxUsdaOutputBytes();
      if (cap_bytes && est_bytes > cap_bytes) {
        // Over the USDA cap: keep timeSamples compact by serializing to USDC in
        // memory (binary, far smaller than baked USDA text) instead of emitting
        // a multi-GB USDA string that would exhaust memory.
        std::vector<uint8_t> usdc_bytes;
        std::string c_warn, c_err;
        if (tinyusdz::usdc::SaveAsUSDCToMemory(comp_stage, &usdc_bytes, &c_warn,
                                               &c_err)) {
          std::cerr << "# Composed stage estimate " << format_memory_size(est_bytes)
                    << " exceeds USDA output cap " << format_memory_size(cap_bytes)
                    << "; serialized compact USDC to memory ("
                    << format_memory_size(usdc_bytes.size())
                    << ") instead of USDA text. (Use -o out.usdc to write it.)\n";
        } else {
          std::cerr << "ERR: composed stage too large for USDA output ("
                    << format_memory_size(est_bytes) << " > cap "
                    << format_memory_size(cap_bytes)
                    << ") and the compact USDC fallback failed: " << c_err << "\n";
          return EXIT_FAILURE;
        }
      } else {
        // Guard the (potentially huge) USDA serialization against allocation
        // failure: turn an out-of-memory condition into a clean error instead of
        // an uncaught std::bad_alloc -> std::terminate -> abort().
        try {
          std::cout << comp_stage.ExportToString() << "\n";
        } catch (const std::bad_alloc &) {
          std::cerr << "ERR: out of memory while serializing composed stage to "
                       "USDA text (estimate " << format_memory_size(est_bytes)
                    << "). Use USDC output (-o out.usdc) or set TUSDCAT_MAX_USDA_MB "
                       "for large composed scenes.\n";
          return EXIT_FAILURE;
        }
      }
    }

    if (has_output_file) {
      if (!WriteStageToFile(comp_stage, output_filepath, output_format)) {
        return EXIT_FAILURE;
      }
    }

    using MeshMap = tinyusdz::tydra::PathPrimMap<tinyusdz::GeomMesh>;
    MeshMap meshmap;

    tinyusdz::tydra::ListPrims(comp_stage, meshmap);

    for (const auto &item : meshmap) {

      std::cout << "Prim : " << item.first << "\n";
    }

  } else {

    tinyusdz::Stage stage;
    tinyusdz::usdc::USDCMemoryUsageReport usdc_memory_report;
    bool has_usdc_memory_report{false};

    tinyusdz::USDLoadOptions options;

    // MaterialX validation
    options.strict_mtlx_check = strict_mtlx_check;
    options.error_detail = error_detail;

    // Set up progress callback if requested
    ProgressState progress_state;
    if (show_progress) {
      progress_state.start_time = std::chrono::steady_clock::now();
      options.progress_callback = progress_callback;
      options.progress_userptr = &progress_state;
    }

    bool ret{false};
    if (ext == "usdc") {
      ret = LoadUSDCWithMemoryReport(filepath, show_progress, &stage,
                                     &usdc_memory_report, &warn, &err);
      has_usdc_memory_report = true;
    } else {
      // auto detect format.
      ret = tinyusdz::LoadUSDFromFile(filepath, &stage, &warn, &err, options);
    }
    if (!warn.empty()) {
      std::cerr << "WARN : " << warn << "\n";
    }
    if (!err.empty()) {
      std::cerr << "ERR : " << err << "\n";
      //return EXIT_FAILURE;
    }

    if (!ret) {
      std::cerr << "Failed to load USD file: " << filepath << "\n";
      return EXIT_FAILURE;
    }

    if (load_only) {
      if (memstat) {
        auto detail = stage.estimate_memory_usage_detail();
        std::cout << "# Memory Statistics (Stage)\n";
        std::cout << "  Allocated (capacity): " << format_memory_size(detail.allocated_bytes)
                  << " (" << detail.allocated_bytes << " bytes)\n";
        std::cout << "  Actual (in use):      " << format_memory_size(detail.actual_bytes)
                  << " (" << detail.actual_bytes << " bytes)\n";
        if (detail.allocated_bytes > 0) {
          double efficiency = 100.0 * (double(detail.actual_bytes) / double(detail.allocated_bytes));
          std::cout << "  Efficiency:           " << std::fixed << std::setprecision(1)
                    << efficiency << " %\n";
        }
        if (has_usdc_memory_report) {
          PrintUSDCParserMemoryReport(usdc_memory_report);
        }
      }
      return EXIT_SUCCESS;
    }

    if (memstat) {
      auto detail = stage.estimate_memory_usage_detail();
      std::cout << "# Memory Statistics (Stage)\n";
      std::cout << "  Allocated (capacity): " << format_memory_size(detail.allocated_bytes)
                << " (" << detail.allocated_bytes << " bytes)\n";
      std::cout << "  Actual (in use):      " << format_memory_size(detail.actual_bytes)
                << " (" << detail.actual_bytes << " bytes)\n";
      if (detail.allocated_bytes > 0) {
        double efficiency = 100.0 * (double(detail.actual_bytes) / double(detail.allocated_bytes));
        std::cout << "  Efficiency:           " << std::fixed << std::setprecision(1)
                  << efficiency << " %\n";
      }
      std::cout << "\n";
      if (has_usdc_memory_report) {
        PrintUSDCParserMemoryReport(usdc_memory_report);
        std::cout << "\n";
      }
    }

    if (json_output) {
#if defined(TINYUSDZ_WITH_JSON)
      auto json_result = tinyusdz::ToJSON(stage);
      if (json_result) {
        std::cout << json_result.value() << "\n";
      } else {
        std::cerr << "Failed to convert stage to JSON: " << json_result.error() << "\n";
        return EXIT_FAILURE;
      }
#else
      std::cerr << "JSON output is not supported in this build\n";
#endif
    } else if (!suppress_usd_text_output) {
      std::string s = stage.ExportToString(has_relative);
      std::cout << s << "\n";
    }

    if (has_output_file) {
      if (!WriteStageToFile(stage, output_filepath, output_format,
                            compress_float_arrays)) {
        return EXIT_FAILURE;
      }
    }

    if (has_extract_variants) {
      std::cout << "\n=== VARIANT EXTRACTION (" << variant_format << ") ===\n";

      tinyusdz::Dictionary dict;
      if (!tinyusdz::ExtractVariants(stage, &dict, &err)) {
        std::cerr << "Failed to extract variants info: " << err;
      } else {
        if (variant_format == "json") {
          std::cout << variant_format::dictionary_to_json(dict) << "\n";
        } else {
          std::cout << variant_format::dictionary_to_yaml(dict) << "\n";
        }
      }

    }
  }

  return EXIT_SUCCESS;
}
