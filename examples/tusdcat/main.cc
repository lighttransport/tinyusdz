#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "tinyusdz.hh"
#include "layer.hh"
#include "pprinter.hh"
#include "str-util.hh"
#include "io-util.hh"
#include "usd-to-json.hh"
#include "usd-dump.hh"
#include "logger.hh"
#include "crate-writer.hh"
#include "crate-dump.hh"
#include "usdc-reader.hh"

#include "tydra/scene-access.hh"
#include "variant-format.hh"

struct CompositionFeatures {
  bool subLayers{true};
  bool inherits{true};
  bool variantSets{true};
  bool references{true};
  bool payload{true}; // Not 'payloads'
  bool specializes{true};
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

// Helper function to write Stage to USDC file
static bool WriteStageToUSdc(const tinyusdz::Stage& stage, const std::string& output_path) {
  using namespace tinyusdz::experimental;

  std::cout << "Writing USDC to: " << output_path << "\n";

  CrateWriter writer(output_path);
  std::string err;

  if (!writer.Open(&err)) {
    std::cerr << "Failed to open USDC writer: " << err << "\n";
    return false;
  }

  if (!writer.ConvertStageToSpecs(stage, &err)) {
    std::cerr << "Failed to convert Stage to USDC specs: " << err << "\n";
    return false;
  }

  if (!writer.Finalize(&err)) {
    std::cerr << "Failed to finalize USDC file: " << err << "\n";
    return false;
  }

  writer.Close();

  std::cout << "Successfully wrote USDC file: " << output_path << "\n";
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
  std::cout << "  -o, --output FILE   Write output to USDC file\n";
  std::cout << "  --memstat           Print memory usage statistics\n";
  std::cout << "                      (includes USDC parser budget report for .usdc)\n";
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
}

int main(int argc, char **argv) {
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
  bool show_progress{false};

  // Inspect options
  bool do_inspect{false};
  tinyusdz::InspectOptions inspect_opts;

  // Dumpcrate option
  bool do_dumpcrate{false};

  // MaterialX validation
  bool strict_mtlx_check{false};

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
    } else if (arg.compare("--progress") == 0) {
      show_progress = true;
    } else if (arg.compare("--dumpcrate") == 0) {
      do_dumpcrate = true;
    } else if (arg.compare("--strict-mtlx-check") == 0) {
      strict_mtlx_check = true;
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
      try {
        int snip_val = std::stoi(snip_str);
        if (snip_val < 1) {
          std::cerr << "Invalid snip value: " << snip_val
                    << ". Must be >= 1\n";
          return EXIT_FAILURE;
        }
        inspect_opts.snip_count = static_cast<size_t>(snip_val);
      } catch (...) {
        std::cerr << "Invalid snip value: " << snip_str << "\n";
        return EXIT_FAILURE;
      }
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
        try {
          inspect_opts.time_start = std::stod(start_str);
          inspect_opts.time_end = std::stod(end_str);
        } catch (...) {
          std::cerr << "Invalid time range: " << time_str << "\n";
          return EXIT_FAILURE;
        }
      } else {
        // Single time value
        try {
          double t = std::stod(time_str);
          inspect_opts.time_start = t;
          inspect_opts.time_end = t;
        } catch (...) {
          std::cerr << "Invalid time value: " << time_str << "\n";
          return EXIT_FAILURE;
        }
      }
    } else if (arg.compare("--loglevel") == 0) {
      if (i + 1 >= argc) {
        std::cerr << "--loglevel requires an integer argument\n";
        return EXIT_FAILURE;
      }
      i++; // Move to next argument
      try {
        int log_level = std::stoi(argv[i]);
        if (log_level < 0 || log_level > 5) {
          std::cerr << "Invalid log level: " << log_level << ". Must be between 0 and 5.\n";
          return EXIT_FAILURE;
        }
        tinyusdz::logging::Logger::getInstance().setLogLevel(
            static_cast<tinyusdz::logging::LogLevel>(log_level));
      } catch (const std::invalid_argument& e) {
        std::cerr << "Invalid log level argument: " << argv[i] << ". Must be an integer.\n";
        return EXIT_FAILURE;
      } catch (const std::out_of_range& e) {
        std::cerr << "Log level value out of range: " << argv[i] << "\n";
        return EXIT_FAILURE;
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
  std::string base_dir;
  base_dir = tinyusdz::io::GetBaseDir(filepath);

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
      } else {
        std::cout << to_string(stage) << "\n";
#else
      std::cerr << "JSON output is not supported in this build\n";
      return EXIT_FAILURE;
#endif
      }

      return EXIT_SUCCESS;
    }

    tinyusdz::Layer root_layer;
    bool ret = tinyusdz::LoadLayerFromFile(filepath, &root_layer, &warn, &err);
    if (warn.size()) {
      std::cout << "WARN: " << warn << "\n";
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

    std::cout << "# input\n";
    std::cout << root_layer << "\n";

    tinyusdz::Stage stage;
    stage.metas() = root_layer.metas();

    std::string warn;

    tinyusdz::AssetResolutionResolver resolver;
    resolver.set_current_working_path(base_dir);
    resolver.set_search_paths({base_dir});

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
    if (comp_features.subLayers) {
      tinyusdz::Layer composited_layer;
      if (!tinyusdz::CompositeSublayers(resolver, src_layer, &composited_layer, &warn, &err)) {
        std::cerr << "Failed to composite subLayers: " << err << "\n";
        return -1;
      }

      if (warn.size()) {
        std::cout << "WARN: " << warn << "\n";
      }

      std::cout << "# `subLayers` composited\n";
      std::cout << composited_layer << "\n";

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
          if (!tinyusdz::CompositeReferences(resolver, src_layer, &composited_layer, &warn, &err)) {
            std::cerr << "Failed to composite `references`: " << err << "\n";
            return -1;
          }

          if (warn.size()) {
            std::cout << "WARN: " << warn << "\n";
          }

          std::cout << "# `references` composited\n";
          std::cout << composited_layer << "\n";

          src_layer = std::move(composited_layer);
        }
      }

      if (comp_features.payload) {
        if (!src_layer.check_unresolved_payload()) {
          std::cout << "# iter " << i << ": no unresolved payload.\n";
        } else {
          has_unresolved = true;

          tinyusdz::Layer composited_layer;
          if (!tinyusdz::CompositePayload(resolver, src_layer, &composited_layer, &warn, &err)) {
            std::cerr << "Failed to composite `payload`: " << err << "\n";
            return -1;
          }

          if (warn.size()) {
            std::cout << "WARN: " << warn << "\n";
          }

          std::cout << "# `payload` composited\n";
          std::cout << composited_layer << "\n";

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
            std::cout << "WARN: " << warn << "\n";
          }

          std::cout << "# `inherits` composited\n";
          std::cout << composited_layer << "\n";

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
            std::cout << "WARN: " << warn << "\n";
          }

          std::cout << "# `variantSet` composited\n";
          std::cout << composited_layer << "\n";

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
    ret = LayerToStage(std::move(src_layer), &comp_stage, &warn, &err);
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
    } else {
      std::cout << comp_stage.ExportToString() << "\n";
    }

    // Write to USDC if output file is specified
    if (!output_filepath.empty()) {
      if (!WriteStageToUSdc(comp_stage, output_filepath)) {
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
    } else {
      std::string s = stage.ExportToString(has_relative);
      std::cout << s << "\n";
    }

    // Write to USDC if output file is specified
    if (!output_filepath.empty()) {
      if (!WriteStageToUSdc(stage, output_filepath)) {
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
