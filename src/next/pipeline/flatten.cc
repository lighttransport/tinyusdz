// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Low-memory flatten pipeline implementation

#include "flatten.hh"

#include <cstring>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iterator>  // std::istreambuf_iterator (not guaranteed via <fstream> on MSVC)
#include <set>

#include "../layer/layer.hh"
#include "../pcp/layer-registry.hh"
#include "../reader/usdc-reader.hh"
#include "../stage/stage.hh"

#include <memory>

#if defined(__EMSCRIPTEN__) && defined(TINYUSDZ_FLATTEN_MEMLOG)
#include <emscripten/heap.h>
#include "../strfmt.hh"     // AppendUInt
#include "../../logger.hh"  // TUSDZ_LOG_I (only pulled in for this opt-in diag)
#endif

namespace tinyusdz {
namespace next {
namespace pipeline {

namespace {

using Clock = std::chrono::steady_clock;

double ElapsedMs(const Clock::time_point& a, const Clock::time_point& b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

// Attribution aid: log the wasm linear-heap high-water at each flatten stage
// boundary when built with -DTINYUSDZ_FLATTEN_MEMLOG. emscripten_get_heap_size()
// is the grown ArrayBuffer size (monotonic), so the per-stage deltas attribute
// the peak to read / compose / write. Compiled out (zero cost) by default; an
// env gate can't be used because emscripten getenv() does not see process.env.
void FlattenMemLog(const char* stage) {
  (void)stage;
#if defined(__EMSCRIPTEN__) && defined(TINYUSDZ_FLATTEN_MEMLOG)
  std::string msg = "[flatten-mem] ";
  msg += stage;
  msg += " heap=";
  AppendUInt(msg,
             static_cast<size_t>(emscripten_get_heap_size()) / (1024 * 1024));
  msg += " MiB";
  TUSDZ_LOG_I(msg);
#endif
}

// True if the root layer is self-contained: no sublayers and no per-prim
// composition arcs (references/payloads/inherits/specializes/variants). Such a
// root flattens to itself, so the structural clone Compose() would do is pure
// overhead and can be skipped.
bool IsSelfContained(const Layer& root) {
  if (!root.meta().subLayers.empty()) return false;
  for (const auto& prim : root.prims()) {
    if (HasCompositionArcs(prim)) return false;
  }
  return true;
}

void CollectReferencedAssets(const Layer& layer, std::vector<std::string>* out) {
  if (!out) return;
  std::set<std::string> unique;
  for (const PrimSpec& prim : layer.prims()) {
    for (const PropSlot& slot : prim.properties().slots()) {
      const Value* value = prim.property_value(slot.name_id);
      if (!value) continue;
      const std::string* path = value->as_asset_path();
      if (path && !path->empty()) {
        unique.insert(*path);
      }
    }
  }
  out->assign(unique.begin(), unique.end());
}

// Shared post-read logic: (optionally) flatten, then write to `out` (in-memory)
// or `sink` (streaming) — exactly one is non-null. `rr.stage`'s lazy Values hold
// their own shared_ptr to the retained source buffer, so it stays alive through
// the write regardless of the reader's lifetime.
bool FlattenLayer(std::unique_ptr<Layer> root_owner, size_t input_bytes,
                  std::vector<uint8_t>* out, const CrateWriteSink* sink,
                  const FlattenOptions& opts, FlattenStats* stats,
                  std::string* err) {
  FlattenMemLog("after-read");

  const Layer* root = root_owner.get();
  if (!root) {
    if (err) *err = "no root layer";
    return false;
  }

  std::unique_ptr<Layer> composed;
  const Layer* layer = root;
  const auto compose_begin = Clock::now();
  if (opts.flatten && !IsSelfContained(*root)) {
    Compositor comp;
    comp.SetOptions(opts.composition);
    if (opts.resolver) comp.SetResolver(opts.resolver);
    if (opts.layer_loader) comp.SetLayerLoader(opts.layer_loader);
    composed = comp.Compose(*root, opts.root_anchor_path);  // structural: moves lazy refs, no decode
    if (!composed) {
      if (err) *err = "composition failed";
      return false;
    }
    if (stats) {
      for (const auto& ce : comp.GetErrors()) {
        stats->composition_errors.push_back(
            ce.message + (ce.prim_path.empty() ? "" : " at " + ce.prim_path) +
            (ce.arc_path.empty() ? "" : " (" + ce.arc_path + ")"));
      }
    }
    if (opts.fail_on_composition_error && !comp.GetErrors().empty()) {
      if (err) {
        const auto& ce = comp.GetErrors().front();
        *err = ce.message + (ce.arc_path.empty() ? "" : " (" + ce.arc_path + ")");
      }
      return false;
    }
    layer = composed.get();
  }
  const auto after_compose = Clock::now();
  FlattenMemLog("after-compose");

  size_t asset_paths_remapped = 0;
  if (!opts.asset_path_remap.empty()) {
    Layer* mutable_layer = composed ? composed.get() : root_owner.get();
    if (mutable_layer) {
      for (size_t i = 0; i < mutable_layer->prim_count(); ++i) {
        PrimSpec* prim = mutable_layer->prim_mutable(static_cast<uint32_t>(i));
        if (!prim) continue;
        asset_paths_remapped += prim->remap_asset_paths(opts.asset_path_remap);
      }
    }
  }

  CrateWriter writer(opts.write);
  CrateWriteResult wr = sink ? writer.WriteLayerToSink(*sink, *layer)
                             : writer.WriteLayerToMemory(*out, *layer);
  const auto after_write = Clock::now();
  if (!wr.success) {
    if (err) *err = wr.error.empty() ? "crate write failed" : wr.error;
    return false;
  }
  FlattenMemLog("after-write");

  if (stats) {
    stats->input_bytes = input_bytes;
    stats->output_bytes = wr.bytes_written;
    stats->prim_count = layer->prim_count();
    stats->arrays_passed_through = wr.arrays_passed_through;
    stats->arrays_reencoded = wr.arrays_reencoded;
    stats->asset_paths_remapped = asset_paths_remapped;
    stats->compose_ms = ElapsedMs(compose_begin, after_compose);
    stats->write_ms = ElapsedMs(after_compose, after_write);
    CollectReferencedAssets(*layer, &stats->referenced_assets);
  }
  return true;
}

bool FlattenLoaded(CrateReadResult&& rr, size_t input_bytes,
                   std::vector<uint8_t>* out, const CrateWriteSink* sink,
                   const FlattenOptions& opts, FlattenStats* stats,
                   std::string* err) {
  if (stats) stats->input_was_mmap = rr.source_was_mmap;
  if (!rr.success) {
    if (err) *err = rr.errors.empty() ? "crate read failed" : rr.errors[0].message;
    return false;
  }
  return FlattenLayer(rr.stage.ReleaseRootLayer(), input_bytes, out, sink, opts,
                      stats, err);
}

uint64_t FileSizeBytes(const std::string& filename) {
  std::ifstream f(filename, std::ios::binary | std::ios::ate);
  if (!f) return 0;
  std::streamoff end = f.tellg();
  return end > 0 ? static_cast<uint64_t>(end) : 0;
}

bool EndsWithNoCase(const std::string& s, const char* suffix) {
  const size_t n = std::strlen(suffix);
  if (s.size() < n) return false;
  const size_t off = s.size() - n;
  for (size_t i = 0; i < n; ++i) {
    unsigned char a = static_cast<unsigned char>(s[off + i]);
    unsigned char b = static_cast<unsigned char>(suffix[i]);
    if (std::tolower(a) != std::tolower(b)) return false;
  }
  return true;
}

}  // namespace

bool FlattenUSDCToUSDC(const uint8_t* data, size_t size, std::vector<uint8_t>& out,
                       const FlattenOptions& opts, FlattenStats* stats,
                       std::string* err) {
  if (stats) *stats = FlattenStats{};
  if (!data || size == 0) {
    if (err) *err = "empty input";
    return false;
  }
  const auto read_begin = Clock::now();
  CrateReader reader(opts.read);
  CrateReadResult rr = reader.Read(data, size);
  const auto read_end = Clock::now();
  bool ok = FlattenLoaded(std::move(rr), size, &out, nullptr, opts, stats, err);
  if (stats) stats->read_ms = ElapsedMs(read_begin, read_end);
  return ok;
}

bool FlattenUSDCToUSDCToSink(const uint8_t* data, size_t size,
                             const CrateWriteSink& sink,
                             const FlattenOptions& opts, FlattenStats* stats,
                             std::string* err) {
  if (stats) *stats = FlattenStats{};
  if (!data || size == 0) {
    if (err) *err = "empty input";
    return false;
  }
  const auto read_begin = Clock::now();
  CrateReader reader(opts.read);
  CrateReadResult rr = reader.Read(data, size);
  const auto read_end = Clock::now();
  bool ok = FlattenLoaded(std::move(rr), size, nullptr, &sink, opts, stats, err);
  if (stats) stats->read_ms = ElapsedMs(read_begin, read_end);
  return ok;
}

bool FlattenUSDCToUSDCOwned(std::string&& data, std::vector<uint8_t>& out,
                            const FlattenOptions& opts, FlattenStats* stats,
                            std::string* err) {
  if (stats) *stats = FlattenStats{};
  if (data.empty()) {
    if (err) *err = "empty input";
    return false;
  }
  const size_t input_bytes = data.size();
  const auto read_begin = Clock::now();
  CrateReader reader(opts.read);
  CrateReadResult rr = reader.ReadOwned(std::move(data));
  const auto read_end = Clock::now();
  bool ok = FlattenLoaded(std::move(rr), input_bytes, &out, nullptr, opts, stats,
                          err);
  if (stats) stats->read_ms = ElapsedMs(read_begin, read_end);
  return ok;
}

bool FlattenUSDCToUSDCOwnedToSink(std::string&& data, const CrateWriteSink& sink,
                                  const FlattenOptions& opts, FlattenStats* stats,
                                  std::string* err) {
  if (stats) *stats = FlattenStats{};
  if (data.empty()) {
    if (err) *err = "empty input";
    return false;
  }
  const size_t input_bytes = data.size();
  const auto read_begin = Clock::now();
  CrateReader reader(opts.read);
  CrateReadResult rr = reader.ReadOwned(std::move(data));
  const auto read_end = Clock::now();
  bool ok = FlattenLoaded(std::move(rr), input_bytes, nullptr, &sink, opts, stats,
                          err);
  if (stats) stats->read_ms = ElapsedMs(read_begin, read_end);
  return ok;
}

LayerLoader MakeFileSystemLayerLoader(const CrateReadOptions& read_opts) {
  return MakeFileSystemLayerLoader(read_opts, {});
}

LayerLoader MakeFileSystemLayerLoader(const CrateReadOptions& read_opts,
                                     const pcp::LayerLoadOptions& layer_load_opts) {
  return [read_opts, layer_load_opts](const std::string& resolved_path,
                     std::string* error) -> std::unique_ptr<Layer> {
    pcp::LayerLoadOptions lopts = layer_load_opts;
    lopts.max_memory = read_opts.max_memory;
    std::string warn;
    std::shared_ptr<Layer> loaded =
        pcp::LoadLayerFromFile(resolved_path, &warn, error, lopts);
    if (!loaded) return nullptr;
    std::unique_ptr<Layer> layer(new Layer(loaded->Clone()));
    layer->build_path_index();  // compositor looks prims up by path
    return layer;
  };
}

bool FlattenUSDFileToUSDC(const std::string& filename, std::vector<uint8_t>& out,
                          const FlattenOptions& opts, FlattenStats* stats,
                          std::string* err) {
  if (stats) *stats = FlattenStats{};
  const auto read_begin = Clock::now();

  FlattenOptions effective = opts;
  AssetResolver resolver;
  resolver.SetWorkingDirectory(AssetResolver::GetDirectory(filename));
  if (!effective.resolver) effective.resolver = &resolver;
  if (!effective.layer_loader) {
    pcp::LayerLoadOptions layer_load_opts;
    layer_load_opts.max_memory = opts.read.max_memory;
    layer_load_opts.usda_parse_options = opts.composition.usda_parse_options;
    layer_load_opts.parse_num_threads =
        opts.composition.usda_parse_options.num_threads;
    effective.layer_loader = MakeFileSystemLayerLoader(opts.read, layer_load_opts);
  }
  if (effective.root_anchor_path.empty()) effective.root_anchor_path = filename;

  if (EndsWithNoCase(filename, ".usdc")) {
    CrateReader reader(opts.read);
    CrateReadResult rr = reader.ReadFile(filename.c_str());
    const auto read_end = Clock::now();
    const size_t input_bytes = static_cast<size_t>(FileSizeBytes(filename));
    bool ok = FlattenLoaded(std::move(rr), input_bytes, &out, nullptr,
                            effective, stats, err);
    if (stats) stats->read_ms = ElapsedMs(read_begin, read_end);
    return ok;
  }

  pcp::LayerLoadOptions lopts;
  lopts.max_memory = opts.composition.max_layer_memory;
  if (lopts.max_memory == 0) lopts.max_memory = opts.read.max_memory;
  lopts.usda_parse_options = opts.composition.usda_parse_options;
  lopts.parse_num_threads = opts.composition.usda_parse_options.num_threads;
  std::string warn;
  std::shared_ptr<Layer> loaded =
      pcp::LoadLayerFromFile(filename, &warn, err, lopts);
  const auto read_end = Clock::now();
  if (!loaded) {
    if (err && err->empty()) *err = "failed to load root layer: " + filename;
    return false;
  }

  std::unique_ptr<Layer> root(new Layer(loaded->Clone()));
  const size_t input_bytes = static_cast<size_t>(FileSizeBytes(filename));
  bool ok = FlattenLayer(std::move(root), input_bytes, &out, nullptr, effective,
                         stats, err);
  if (stats) stats->read_ms = ElapsedMs(read_begin, read_end);
  return ok;
}

bool FlattenUSDFileToUSDCToSink(const std::string& filename,
                                const CrateWriteSink& sink,
                                const FlattenOptions& opts,
                                FlattenStats* stats, std::string* err) {
  if (stats) *stats = FlattenStats{};
  const auto read_begin = Clock::now();

  FlattenOptions effective = opts;
  AssetResolver resolver;
  resolver.SetWorkingDirectory(AssetResolver::GetDirectory(filename));
  if (!effective.resolver) effective.resolver = &resolver;
  if (!effective.layer_loader) {
    pcp::LayerLoadOptions layer_load_opts;
    layer_load_opts.max_memory = opts.read.max_memory;
    layer_load_opts.usda_parse_options = opts.composition.usda_parse_options;
    layer_load_opts.parse_num_threads =
        opts.composition.usda_parse_options.num_threads;
    effective.layer_loader = MakeFileSystemLayerLoader(opts.read, layer_load_opts);
  }
  if (effective.root_anchor_path.empty()) effective.root_anchor_path = filename;

  if (EndsWithNoCase(filename, ".usdc")) {
    CrateReader reader(opts.read);
    CrateReadResult rr = reader.ReadFile(filename.c_str());
    const auto read_end = Clock::now();
    const size_t input_bytes = static_cast<size_t>(FileSizeBytes(filename));
    bool ok = FlattenLoaded(std::move(rr), input_bytes, nullptr, &sink,
                            effective, stats, err);
    if (stats) stats->read_ms = ElapsedMs(read_begin, read_end);
    return ok;
  }

  pcp::LayerLoadOptions lopts;
  lopts.max_memory = opts.composition.max_layer_memory;
  if (lopts.max_memory == 0) lopts.max_memory = opts.read.max_memory;
  lopts.usda_parse_options = opts.composition.usda_parse_options;
  lopts.parse_num_threads = opts.composition.usda_parse_options.num_threads;
  std::string warn;
  std::shared_ptr<Layer> loaded =
      pcp::LoadLayerFromFile(filename, &warn, err, lopts);
  const auto read_end = Clock::now();
  if (!loaded) {
    if (err && err->empty()) *err = "failed to load root layer: " + filename;
    return false;
  }

  std::unique_ptr<Layer> root(new Layer(loaded->Clone()));
  const size_t input_bytes = static_cast<size_t>(FileSizeBytes(filename));
  bool ok = FlattenLayer(std::move(root), input_bytes, nullptr, &sink, effective,
                         stats, err);
  if (stats) stats->read_ms = ElapsedMs(read_begin, read_end);
  return ok;
}

}  // namespace pipeline
}  // namespace next
}  // namespace tinyusdz
