// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - High-level API Implementation

#include "tinyusdz-next.hh"
#include "composition/composition.hh"
#include "pcp/cache.hh"
#include "reader/usdz-reader.hh"
#include "resolver/asset-resolver.hh"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#if defined(TINYUSDZ_ENABLE_THREAD)
#include <thread>
#endif

namespace tinyusdz {
namespace next {

namespace {

// File format detection
enum class FileFormat {
  Unknown,
  USDA,
  USDC,
  USDZ
};

FileFormat DetectFormat(const uint8_t* data, size_t size) {
  if (size < 8) return FileFormat::Unknown;

  // USDC (Crate) magic: "PXR-USDC"
  if (std::memcmp(data, "PXR-USDC", 8) == 0) {
    return FileFormat::USDC;
  }

  // USDZ (ZIP archive) magic: "PK\x03\x04"
  if (data[0] == 'P' && data[1] == 'K' && data[2] == 0x03 && data[3] == 0x04) {
    return FileFormat::USDZ;
  }

  // USDA is text - check for ASCII/UTF-8
  // Look for #usda header or common patterns
  bool is_text = true;
  for (size_t i = 0; i < std::min(size, size_t(1024)); ++i) {
    uint8_t c = data[i];
    if (c < 0x09 || (c > 0x0D && c < 0x20 && c != 0x1B)) {
      is_text = false;
      break;
    }
  }

  if (is_text) {
    return FileFormat::USDA;
  }

  return FileFormat::Unknown;
}

FileFormat DetectFormatFromExtension(const std::string& filename) {
  size_t dot = filename.rfind('.');
  if (dot == std::string::npos) return FileFormat::Unknown;

  std::string ext = filename.substr(dot);
  // Convert to lowercase
  for (char& c : ext) {
    if (c >= 'A' && c <= 'Z') c += 32;
  }

  if (ext == ".usda") return FileFormat::USDA;
  if (ext == ".usdc") return FileFormat::USDC;
  if (ext == ".usdz") return FileFormat::USDZ;
  if (ext == ".usd") return FileFormat::Unknown;  // Need content detection

  return FileFormat::Unknown;
}

FileFormat DetectFormatFromFileHeader(const std::string& filename, std::string* err) {
  std::ifstream ifs(filename, std::ios::binary);
  if (!ifs) {
    if (err) *err = "Failed to open file for format detection: " + filename;
    return FileFormat::Unknown;
  }

  uint8_t header[4096] = {};
  ifs.read(reinterpret_cast<char*>(header), sizeof(header));
  const std::streamsize nread = ifs.gcount();
  if (nread <= 0) {
    if (err) *err = "Failed to read file header: " + filename;
    return FileFormat::Unknown;
  }
  return DetectFormat(header, static_cast<size_t>(nread));
}

size_t MinNonZero(size_t a, size_t b) {
  if (a == 0) return b;
  if (b == 0) return a;
  return std::min(a, b);
}

LoadOptions EffectiveUSDAOptions(const LoadUSDOptions& options) {
  LoadOptions out = options.usda_options;
  if (options.strict_aousd_conformance) {
    out.parse_options.strict_aousd_conformance = true;
  }
  out.parse_options.max_file_size =
      MinNonZero(out.parse_options.max_file_size, options.max_memory);
  return out;
}

USDCLoadOptions EffectiveUSDCOptions(const LoadUSDOptions& options) {
  USDCLoadOptions out = options.usdc_options;
  if (options.strict_aousd_conformance) {
    out.crate_options.strict_aousd_conformance = true;
  }
  out.crate_options.max_memory =
      MinNonZero(out.crate_options.max_memory, options.max_memory);
  return out;
}

USDZReadOptions EffectiveUSDZOptions(const LoadUSDOptions& options) {
  USDZReadOptions out = options.usdz_options;
  out.max_archive_size = MinNonZero(out.max_archive_size, options.max_memory);
  out.max_entry_size = MinNonZero(out.max_entry_size, options.max_memory);
  return out;
}

}  // namespace

bool LoadUSD(const std::string& filename, Stage* stage,
             std::string* warn, std::string* err) {
  return LoadUSD(filename, stage, LoadUSDOptions{}, warn, err);
}

bool LoadUSD(const std::string& filename, Stage* stage,
             const LoadUSDOptions& options, std::string* warn,
             std::string* err) {
  if (!stage) {
    if (err) *err = "stage is null";
    return false;
  }

  FileFormat format = DetectFormatFromExtension(filename);
  if (format == FileFormat::Unknown) {
    format = DetectFormatFromFileHeader(filename, err);
  }

  switch (format) {
    case FileFormat::USDA: {
      LoadResult result = LoadUSDAFromFile(filename, EffectiveUSDAOptions(options));
      if (!result.success) {
        if (err) *err = result.error_summary;
        return false;
      }
      if (warn && !result.warnings.empty()) {
        for (const auto& w : result.warnings) {
          *warn += w + "\n";
        }
      }
      *stage = std::move(result.stage);
      return true;
    }

    case FileFormat::USDC: {
      USDCLoadResult result =
          LoadUSDCFromFile(filename, EffectiveUSDCOptions(options));
      if (!result.success) {
        if (err) *err = result.error_summary;
        return false;
      }
      if (warn && !result.warnings.empty()) {
        for (const auto& w : result.warnings) {
          *warn += w + "\n";
        }
      }
      *stage = std::move(result.stage);
      return true;
    }

    case FileFormat::USDZ: {
      USDZReader usdz;
      if (!usdz.OpenFile(filename, EffectiveUSDZOptions(options))) {
        if (err) {
          *err = usdz.Error().empty() ? "Failed to open USDZ file"
                                      : usdz.Error();
        }
        return false;
      }
      // Try USDC first, then USDA
      int idx = usdz.FindUSDCFile();
      FileFormat inner_fmt = FileFormat::USDC;
      if (idx < 0) {
        idx = usdz.FindUSDAFile();
        inner_fmt = FileFormat::USDA;
      }
      if (idx < 0) {
        if (err) *err = "No .usdc or .usda entry found in USDZ archive";
        return false;
      }
      const uint8_t* entry_data = usdz.EntryData(idx);
      size_t entry_size = usdz.EntrySize(idx);
      if (!entry_data || entry_size == 0) {
        if (err) *err = "Empty USD entry in USDZ";
        return false;
      }

      // Delegate to USDC or USDA reader
      if (inner_fmt == FileFormat::USDC) {
        USDCLoadResult result =
            LoadUSDCFromMemory(entry_data, entry_size,
                               EffectiveUSDCOptions(options));
        if (!result.success) {
          if (err) *err = result.error_summary;
          return false;
        }
        if (warn && !result.warnings.empty()) {
          for (const auto& w : result.warnings) *warn += w + "\n";
        }
        *stage = std::move(result.stage);
      } else {
        LoadResult result = LoadUSDAFromString(
            reinterpret_cast<const char*>(entry_data), entry_size,
            EffectiveUSDAOptions(options));
        if (!result.success) {
          if (err) *err = result.error_summary;
          return false;
        }
        if (warn && !result.warnings.empty()) {
          for (const auto& w : result.warnings) *warn += w + "\n";
        }
        *stage = std::move(result.stage);
      }
      return true;
    }

    default:
      if (err) *err = "Unknown file format";
      return false;
  }
}

namespace {

// True if the root layer carries any composition arc the compositor must
// resolve: sublayers, or per-prim references/payloads/inherits/specializes/
// variants. A self-contained layer flattens to itself, so composing it is pure
// overhead (and must be skipped to preserve the lazy single-file fast path).
bool RootNeedsComposition(const Layer& root) {
  if (!root.meta().subLayers.empty()) return true;
  for (const PrimSpec& prim : root.prims()) {
    if (HasCompositionArcs(prim)) return true;
  }
  return false;
}

std::string DirOfPath(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  if (slash == std::string::npos) return ".";
  return path.substr(0, slash);
}

}  // namespace

struct StageSession::Impl {
  StageSessionOptions options;
  std::string root_identifier;
  AssetResolver resolver;
  std::unique_ptr<pcp::Cache> cache;
  Stage stage;
  std::vector<Diagnostic> diagnostics;
  std::string warning;
  std::string error;
  StageSessionMemoryStats memory_stats;
  bool open = false;

  void UpdateMemoryStats() {
    StageSessionMemoryStats next;
    if (cache) {
      const pcp::Cache::MemoryStats cache_stats = cache->GetMemoryStats();
      next.source_layer_bytes = cache_stats.source_layer_bytes;
      next.transient_cache_bytes = cache_stats.transient_cache_bytes;
      next.layer_count = cache_stats.layer_count;
      next.prim_index_count = cache_stats.prim_index_count;
      next.composed_prim_count = cache_stats.composed_prim_count;
    }
    next.composed_stage_bytes = stage.GetMemoryUsage();
    next.estimated_total_bytes = next.source_layer_bytes +
                                 next.transient_cache_bytes +
                                 next.composed_stage_bytes;
    next.peak_estimated_total_bytes = std::max(
        memory_stats.peak_estimated_total_bytes, next.estimated_total_bytes);
    memory_stats = next;
  }

  bool CheckMemoryBudget(DiagnosticDomain domain) {
    UpdateMemoryStats();
    if (options.max_total_memory == 0 ||
        memory_stats.estimated_total_bytes <= options.max_total_memory) {
      return true;
    }
    error = "aggregate memory budget exceeded: estimated " +
            std::to_string(memory_stats.estimated_total_bytes) +
            " bytes, limit " + std::to_string(options.max_total_memory) +
            " bytes";
    AddDiagnostic(DiagnosticSeverity::Error, domain, "memory_budget", error,
                  root_identifier);
    return false;
  }

  bool Progress(ProgressPhase phase, float progress,
                const std::string& message) {
    if (!options.progress_callback) return true;
    ProgressEvent event;
    event.phase = phase;
    event.progress = progress;
    event.message = message;
    event.estimated_resident_bytes = memory_stats.estimated_total_bytes;
    if (options.progress_callback(event)) return true;
    error = "operation cancelled";
    AddDiagnostic(DiagnosticSeverity::Error, DiagnosticDomain::Load,
                  "cancelled", error, root_identifier);
    return false;
  }

  void AddDiagnostic(DiagnosticSeverity severity, DiagnosticDomain domain,
                     const std::string& code, const std::string& message,
                     const std::string& path = std::string()) {
    Diagnostic d;
    d.severity = severity;
    d.domain = domain;
    d.code = code;
    d.message = message;
    d.path = path;
    diagnostics.push_back(std::move(d));
  }

  void RecordMessages(DiagnosticDomain domain) {
    if (!warning.empty()) {
      AddDiagnostic(DiagnosticSeverity::Warning, domain, "warning", warning,
                    root_identifier);
    }
    if (!error.empty()) {
      AddDiagnostic(DiagnosticSeverity::Error, domain, "error", error,
                    root_identifier);
    }
  }

  bool Rebuild(ProgressPhase phase) {
    if (!cache) return open;
    warning.clear();
    error.clear();
    if (!Progress(phase, 0.0f, "composing stage")) return false;
    Stage next_stage;
    if (!cache->BuildStage(&next_stage, &warning, &error)) {
      RecordMessages(DiagnosticDomain::Compose);
      return false;
    }
    stage = std::move(next_stage);
    RecordMessages(DiagnosticDomain::Compose);
    if (!CheckMemoryBudget(DiagnosticDomain::Compose)) return false;
    if (options.cache_retention == CacheRetention::LayersOnly) {
      cache->TrimTransientCaches();
      UpdateMemoryStats();
    }
    return Progress(phase, 1.0f, "stage ready");
  }
};

StageSession::StageSession() : impl_(new Impl()) {}
StageSession::~StageSession() = default;
StageSession::StageSession(StageSession&&) noexcept = default;
StageSession& StageSession::operator=(StageSession&&) noexcept = default;

bool StageSession::OpenFile(const std::string& filename,
                            const StageSessionOptions& options) {
  std::unique_ptr<Impl> next(new Impl());
  next->options = options;
  next->root_identifier = filename;
  next->resolver.SetConfig(options.resolver);
  if (next->resolver.GetWorkingDirectory().empty()) {
    next->resolver.SetWorkingDirectory(DirOfPath(filename));
  }
  if (!next->Progress(ProgressPhase::RootLoad, 0.0f, "loading root layer")) {
    impl_ = std::move(next);
    return false;
  }

  Stage root;
  if (!LoadUSD(filename, &root, options.load, &next->warning, &next->error)) {
    next->RecordMessages(DiagnosticDomain::Load);
    impl_ = std::move(next);
    return false;
  }
  if (!next->Progress(ProgressPhase::RootLoad, 1.0f, "root layer loaded")) {
    impl_ = std::move(next);
    return false;
  }

  if (!options.compose || !StageNeedsComposition(root)) {
    next->stage = std::move(root);
    if (!next->CheckMemoryBudget(DiagnosticDomain::Load)) {
      impl_ = std::move(next);
      return false;
    }
    next->open = true;
    next->RecordMessages(DiagnosticDomain::Load);
    impl_ = std::move(next);
    return true;
  }

  pcp::CompositionOptions composition = options.composition;
  composition.max_layer_memory =
      MinNonZero(composition.max_layer_memory, options.load.max_memory);
  composition.usda_parse_options = options.load.usda_options.parse_options;
  std::shared_ptr<Layer> root_layer(root.ReleaseRootLayer());
  auto opened = pcp::Cache::Open(next->resolver, std::move(root_layer),
                                 filename, composition);
  if (!opened) {
    next->error = opened.error();
    next->RecordMessages(DiagnosticDomain::Compose);
    impl_ = std::move(next);
    return false;
  }
  next->cache.reset(new pcp::Cache(std::move(*opened)));
  if (!next->Rebuild(ProgressPhase::Compose)) {
    impl_ = std::move(next);
    return false;
  }
  next->open = true;
  impl_ = std::move(next);
  return true;
}

const Stage& StageSession::GetStage() const { return impl_->stage; }
Stage StageSession::TakeStage() {
  if (!impl_) return Stage();
  impl_->open = false;
  impl_->cache.reset();
  return std::move(impl_->stage);
}
const StageSessionOptions& StageSession::GetOptions() const {
  return impl_->options;
}
const std::string& StageSession::GetRootIdentifier() const {
  return impl_->root_identifier;
}
bool StageSession::IsOpen() const { return impl_ && impl_->open; }
bool StageSession::IsComposed() const { return impl_ && impl_->cache != nullptr; }
bool StageSession::Rebuild() {
  return impl_ && impl_->open && impl_->Rebuild(ProgressPhase::Recompose);
}

bool StageSession::LoadPayload(const Path& prim_path,
                               pcp::Cache::LoadPolicy policy) {
  if (!impl_ || !impl_->cache) return false;
  impl_->warning.clear();
  impl_->error.clear();
  if (!impl_->cache->LoadPayload(prim_path, policy, &impl_->warning,
                                 &impl_->error)) {
    impl_->RecordMessages(DiagnosticDomain::Compose);
    return false;
  }
  return impl_->Rebuild(ProgressPhase::Recompose);
}

bool StageSession::UnloadPayload(const Path& prim_path) {
  if (!impl_ || !impl_->cache || !impl_->cache->UnloadPayload(prim_path)) {
    return false;
  }
  return impl_->Rebuild(ProgressPhase::Recompose);
}

bool StageSession::LoadPayloads(
    const std::vector<Path>& prim_paths, pcp::Cache::LoadPolicy policy) {
  if (!impl_ || !impl_->cache) return false;
  pcp::LoadRules rules = impl_->cache->GetLoadRules();
  for (const Path& path : prim_paths) {
    if (path.empty()) continue;
    if (policy == pcp::Cache::LoadPolicy::WithDescendants) {
      rules.LoadWithDescendants(path.str());
    } else {
      rules.LoadWithoutDescendants(path.str());
    }
  }
  impl_->cache->SetLoadRules(rules);
  return impl_->Rebuild(ProgressPhase::Recompose);
}

bool StageSession::SetVariantSelection(const Path& prim_path,
                                       const std::string& variant_set,
                                       const std::string& selection) {
  if (!impl_ || !impl_->cache || prim_path.empty() || variant_set.empty() ||
      selection.empty()) {
    return false;
  }
  auto selections = impl_->cache->GetVariantSelections();
  selections[prim_path.str()][variant_set] = selection;
  return SetVariantSelections(selections);
}

bool StageSession::ClearVariantSelection(const Path& prim_path,
                                         const std::string& variant_set) {
  if (!impl_ || !impl_->cache) return false;
  auto selections = impl_->cache->GetVariantSelections();
  auto path_it = selections.find(prim_path.str());
  if (path_it == selections.end()) return true;
  path_it->second.erase(variant_set);
  if (path_it->second.empty()) selections.erase(path_it);
  return SetVariantSelections(selections);
}

bool StageSession::SetVariantSelections(
    const pcp::CompositionOptions::VariantSelectionMap& selections) {
  if (!impl_ || !impl_->cache) return false;
  impl_->cache->SetVariantSelections(selections);
  return impl_->Rebuild(ProgressPhase::Recompose);
}

pcp::CompositionOptions::VariantSelectionMap
StageSession::GetVariantSelections() const {
  return impl_ && impl_->cache
             ? impl_->cache->GetVariantSelections()
             : pcp::CompositionOptions::VariantSelectionMap();
}
std::vector<Path> StageSession::GetDeferredPayloadPaths() const {
  return impl_ && impl_->cache ? impl_->cache->GetDeferredPayloadPaths()
                               : std::vector<Path>();
}
std::vector<pcp::Cache::CompositionIssue>
StageSession::GetCompositionIssues() const {
  return impl_ && impl_->cache ? impl_->cache->GetCompositionIssues()
                               : std::vector<pcp::Cache::CompositionIssue>();
}
const std::vector<Diagnostic>& StageSession::GetDiagnostics() const {
  return impl_->diagnostics;
}
StageSessionMemoryStats StageSession::GetMemoryStats() const {
  if (!impl_) return {};
  impl_->UpdateMemoryStats();
  return impl_->memory_stats;
}
void StageSession::TrimCaches() {
  if (!impl_ || !impl_->cache) return;
  impl_->cache->TrimTransientCaches();
  impl_->UpdateMemoryStats();
}
const std::string& StageSession::GetWarning() const { return impl_->warning; }
const std::string& StageSession::GetError() const { return impl_->error; }

bool LoadUSDComposed(const std::string& filename, Stage* stage,
                     std::string* warn, std::string* err,
                     const pcp::CompositionOptions* comp_opts) {
  return LoadUSDComposed(filename, stage, LoadUSDOptions{}, warn, err,
                         comp_opts);
}

bool StageNeedsComposition(const Stage& stage) {
  const Layer* root = stage.GetRootLayer();
  return root && RootNeedsComposition(*root);
}

bool ComposeLoadedStage(Stage* stage, AssetResolver& resolver,
                        const std::string& anchor_label,
                        const LoadUSDOptions& load_options,
                        std::string* warn, std::string* err,
                        const pcp::CompositionOptions* comp_opts) {
  if (!stage) {
    if (err) *err = "composition failed: null stage";
    return false;
  }
  Layer* root = stage->GetRootLayer();
  if (!root || !RootNeedsComposition(*root)) {
    // No external/internal arcs to resolve — keep the lazy stage as-is. This is
    // the byte-identical fast path for pre-flattened / self-contained scenes.
    return true;
  }
  const std::string& filename = anchor_label;

  // The root has composition arcs. Resolve them through the full PCP composition
  // engine (sublayers + references + payloads + inherits/specializes + variants
  // + relocates + instancing), anchored to the input file's directory. External
  // USDC layers load lazily.
  //
  // Keep NATIVE instancing (detect_instances=true, the default): each prototype
  // group's geometry is stored ONCE (the prototype member holds the subtree;
  // sibling instances carry instance_prototype meta and emit empty). Consumers
  // traverse instances transparently via UsdPrim::GetChildren(), which follows
  // instance_prototype to the prototype's children — so tusdrender expands every
  // instance into the flat triangle/BVH stream at its own world transform while
  // the composed STAGE keeps just one copy per prototype. (Inline expansion via
  // detect_instances=false instead duplicates every instance's geometry into the
  // stage and OOMs on large scenes like Caldera beachhead/capital.)
  pcp::CompositionOptions copts;
  copts.load_payloads = true;
  // Diagnostics: TINYUSDZ_NEXT_TIMING emits [next_build]/[next_compose] phase
  // timings; TUSDRENDER_COMPOSE_THREADS=N opts into parallel source pre-warming.
  if (std::getenv("TINYUSDZ_NEXT_TIMING")) copts.enable_timing = true;
  // Parallel composition is opt-in via TINYUSDZ_ENABLE_THREAD so wasm builds do
  // not require Emscripten pthreads / SharedArrayBuffer by default.
#if defined(TINYUSDZ_ENABLE_THREAD)
  copts.num_threads = std::thread::hardware_concurrency();
  if (copts.num_threads < 1) copts.num_threads = 1;
#else
  copts.num_threads = 1;
#endif
  copts.max_layer_memory = load_options.max_memory;
  copts.strict_aousd_conformance = load_options.strict_aousd_conformance;
  copts.usda_parse_options = load_options.usda_options.parse_options;
  if (const char *ct = std::getenv("TUSDRENDER_COMPOSE_THREADS")) {
    int n = std::atoi(ct);
#if defined(TINYUSDZ_ENABLE_THREAD)
    if (n >= 1) copts.num_threads = static_cast<unsigned>(n);
#else
    (void)n;
#endif
  }
  // Merge caller-supplied composition options (e.g. variant_overrides) into our
  // defaults. Caller-populated fields take precedence.
  if (comp_opts) {
    copts.load_payloads = comp_opts->load_payloads;
    copts.max_depth = comp_opts->max_depth;
    copts.max_namespace_depth = comp_opts->max_namespace_depth;
    copts.error_when_asset_not_found = comp_opts->error_when_asset_not_found;
    copts.detect_instances = comp_opts->detect_instances;
    copts.flatten_instances = comp_opts->flatten_instances;
    copts.instance_flatten_mode = comp_opts->instance_flatten_mode;
    copts.prototype_numbering = comp_opts->prototype_numbering;
    copts.apply_list_ops = comp_opts->apply_list_ops;
    copts.strict_aousd_conformance =
        copts.strict_aousd_conformance ||
        comp_opts->strict_aousd_conformance;
    if (comp_opts->num_threads >= 1) copts.num_threads = comp_opts->num_threads;
    copts.enable_timing = comp_opts->enable_timing || copts.enable_timing;
    if (comp_opts->payload_policy) copts.payload_policy = comp_opts->payload_policy;
    if (!comp_opts->variant_overrides.empty())
      copts.variant_overrides = comp_opts->variant_overrides;
    if (!comp_opts->variant_overrides_by_path.empty()) {
      copts.variant_overrides_by_path = comp_opts->variant_overrides_by_path;
    }
    copts.usda_parse_options = comp_opts->usda_parse_options;
    copts.max_layer_memory =
        MinNonZero(copts.max_layer_memory, comp_opts->max_layer_memory);
  }

  Stage composed;
  std::string cwarn, cerr;
  std::shared_ptr<Layer> root_layer(stage->ReleaseRootLayer());
  if (!root_layer) {
    if (err) *err = "composition failed: root layer is null";
    return false;
  }
  if (!pcp::ComposeStageFromLayer(std::move(root_layer), resolver, &composed,
                                  filename, copts, &cwarn, &cerr)) {
    if (err) {
      *err = "composition failed for " + filename +
             (cerr.empty() ? "" : ": " + cerr);
    }
    return false;
  }
  if (warn) {
    if (!cwarn.empty()) *warn += cwarn + (cwarn.back() == '\n' ? "" : "\n");
    if (!cerr.empty()) *warn += cerr + (cerr.back() == '\n' ? "" : "\n");
  }
  *stage = std::move(composed);
  return true;
}

bool ComposeLoadedStage(Stage* stage, std::string* warn, std::string* err,
                        const pcp::CompositionOptions* comp_opts,
                        const std::string& anchor_label) {
  // Memory-rooted stages have no anchor directory; external arcs resolve
  // through the resolver's custom callbacks only (or surface as warnings).
  AssetResolver resolver;
  resolver.SetWorkingDirectory("");
  return ComposeLoadedStage(stage, resolver, anchor_label, LoadUSDOptions{},
                            warn, err, comp_opts);
}

bool LoadUSDComposed(const std::string& filename, Stage* stage,
                     const LoadUSDOptions& load_options,
                     std::string* warn, std::string* err,
                     const pcp::CompositionOptions* comp_opts) {
  // Lazy single-layer load first (keeps arrays as lazy ValueRefs).
  if (!LoadUSD(filename, stage, load_options, warn, err)) {
    return false;
  }
  AssetResolver resolver;
  resolver.SetWorkingDirectory(DirOfPath(filename));
  return ComposeLoadedStage(stage, resolver, filename, load_options, warn, err,
                            comp_opts);
}

bool LoadUSDFromMemory(const uint8_t* data, size_t size, Stage* stage,
                       std::string* warn, std::string* err) {
  return LoadUSDFromMemory(data, size, stage, LoadUSDOptions{}, warn, err);
}

bool LoadUSDFromMemory(const uint8_t* data, size_t size, Stage* stage,
                       const LoadUSDOptions& options,
                       std::string* warn, std::string* err) {
  if (!stage) {
    if (err) *err = "stage is null";
    return false;
  }
  if (!data || size == 0) {
    if (err) *err = "input buffer is empty";
    return false;
  }

  switch (DetectFormat(data, size)) {
    case FileFormat::USDA: {
      LoadResult result =
          LoadUSDAFromString(reinterpret_cast<const char*>(data), size,
                             EffectiveUSDAOptions(options));
      if (!result.success) {
        if (err) *err = result.error_summary;
        return false;
      }
      if (warn && !result.warnings.empty()) {
        for (const auto& w : result.warnings) *warn += w + "\n";
      }
      *stage = std::move(result.stage);
      return true;
    }

    case FileFormat::USDC: {
      USDCLoadResult result =
          LoadUSDCFromMemory(data, size, EffectiveUSDCOptions(options));
      if (!result.success) {
        if (err) *err = result.error_summary;
        return false;
      }
      if (warn && !result.warnings.empty()) {
        for (const auto& w : result.warnings) *warn += w + "\n";
      }
      *stage = std::move(result.stage);
      return true;
    }

    case FileFormat::USDZ: {
      USDZReader usdz;
      if (!usdz.Open(data, size, EffectiveUSDZOptions(options))) {
        if (err) {
          *err = usdz.Error().empty() ? "Failed to open USDZ archive"
                                      : usdz.Error();
        }
        return false;
      }
      int idx = usdz.FindUSDCFile();
      bool inner_is_usdc = true;
      if (idx < 0) {
        idx = usdz.FindUSDAFile();
        inner_is_usdc = false;
      }
      if (idx < 0) {
        if (err) *err = "No .usdc or .usda entry found in USDZ archive";
        return false;
      }
      const uint8_t* entry_data = usdz.EntryData(idx);
      size_t entry_size = usdz.EntrySize(idx);
      if (!entry_data || entry_size == 0) {
        if (err) *err = "Empty USD entry in USDZ";
        return false;
      }
      if (inner_is_usdc) {
        USDCLoadResult result = LoadUSDCFromMemory(entry_data, entry_size,
                                                   EffectiveUSDCOptions(options));
        if (!result.success) {
          if (err) *err = result.error_summary;
          return false;
        }
        if (warn && !result.warnings.empty()) {
          for (const auto& w : result.warnings) *warn += w + "\n";
        }
        *stage = std::move(result.stage);
      } else {
        LoadResult result = LoadUSDAFromString(
            reinterpret_cast<const char*>(entry_data), entry_size,
            EffectiveUSDAOptions(options));
        if (!result.success) {
          if (err) *err = result.error_summary;
          return false;
        }
        if (warn && !result.warnings.empty()) {
          for (const auto& w : result.warnings) *warn += w + "\n";
        }
        *stage = std::move(result.stage);
      }
      return true;
    }

    default:
      if (err) *err = "Unknown data format (not USDA/USDC/USDZ)";
      return false;
  }
}

bool LoadUSDFromMemoryOwned(std::string&& data, Stage* stage,
                            const LoadUSDOptions& options,
                            std::string* warn, std::string* err) {
  if (!stage) {
    if (err) *err = "stage is null";
    return false;
  }
  if (data.empty()) {
    if (err) *err = "input buffer is empty";
    return false;
  }

  const FileFormat format = DetectFormat(
      reinterpret_cast<const uint8_t*>(data.data()), data.size());
  switch (format) {
    case FileFormat::USDA: {
      LoadResult result =
          LoadUSDAFromStringOwned(std::move(data), EffectiveUSDAOptions(options));
      if (!result.success) {
        if (err) *err = result.error_summary;
        return false;
      }
      if (warn && !result.warnings.empty()) {
        for (const auto& w : result.warnings) *warn += w + "\n";
      }
      *stage = std::move(result.stage);
      return true;
    }

    case FileFormat::USDC: {
      USDCLoadResult result =
          LoadUSDCFromMemoryOwned(std::move(data), EffectiveUSDCOptions(options));
      if (!result.success) {
        if (err) *err = result.error_summary;
        return false;
      }
      if (warn && !result.warnings.empty()) {
        for (const auto& w : result.warnings) *warn += w + "\n";
      }
      *stage = std::move(result.stage);
      return true;
    }

    case FileFormat::USDZ:
      return LoadUSDFromMemory(reinterpret_cast<const uint8_t*>(data.data()),
                               data.size(), stage, options, warn, err);

    default:
      if (err) *err = "Unknown file format";
      return false;
  }
}

bool LoadUSDA(const std::string& filename, Stage* stage,
              std::string* warn, std::string* err) {
  return LoadUSDA(filename, stage, LoadOptions{}, warn, err);
}

bool LoadUSDA(const std::string& filename, Stage* stage,
              const LoadOptions& options, std::string* warn, std::string* err) {
  if (!stage) {
    if (err) *err = "stage is null";
    return false;
  }

  LoadResult result = LoadUSDAFromFile(filename, options);
  if (!result.success) {
    if (err) *err = result.error_summary;
    return false;
  }

  if (warn && !result.warnings.empty()) {
    for (const auto& w : result.warnings) {
      *warn += w + "\n";
    }
  }

  *stage = std::move(result.stage);
  return true;
}

bool LoadUSDC(const std::string& filename, Stage* stage,
              std::string* warn, std::string* err) {
  return LoadUSDC(filename, stage, USDCLoadOptions{}, warn, err);
}

bool LoadUSDC(const std::string& filename, Stage* stage,
              const USDCLoadOptions& options, std::string* warn,
              std::string* err) {
  if (!stage) {
    if (err) *err = "stage is null";
    return false;
  }

  USDCLoadResult result = LoadUSDCFromFile(filename, options);
  if (!result.success) {
    if (err) *err = result.error_summary;
    return false;
  }

  if (warn && !result.warnings.empty()) {
    for (const auto& w : result.warnings) {
      *warn += w + "\n";
    }
  }

  *stage = std::move(result.stage);
  return true;
}

bool WriteUSDA(const Stage& stage, const std::string& filename,
               std::string* err) {
  USDAWriteResult result = WriteUSDAToFile(filename, stage);
  if (!result.success) {
    if (err) *err = result.error;
    return false;
  }
  return true;
}

bool WriteUSDC(const Stage& stage, const std::string& filename,
               std::string* err) {
  USDCWriteResult result = WriteUSDCToFile(filename, stage);
  if (!result.success) {
    if (err) *err = result.error;
    return false;
  }
  return true;
}

}  // namespace next
}  // namespace tinyusdz
