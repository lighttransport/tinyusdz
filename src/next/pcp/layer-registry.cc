// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP LayerRegistry implementation

#include "layer-registry.hh"

#include "../layer/asset-anchor.hh"
#include "../reader/usda-reader.hh"
#include "../reader/usdc-reader.hh"
#include "../reader/usdz-reader.hh"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

namespace tinyusdz {
namespace next {
namespace pcp {

namespace {

size_t MinNonZero(size_t a, size_t b) {
  if (a == 0) return b;
  if (b == 0) return a;
  return std::min(a, b);
}

std::string ToLowerExt(const std::string &path) {
  std::string p = path;
  size_t bracket = p.find('[');
  if (bracket != std::string::npos) p.resize(bracket);
  auto dot = p.find_last_of('.');
  if (dot == std::string::npos) return "";
  std::string ext = p.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(::tolower(c)); });
  return ext;
}

std::string NormalizeEntryName(std::string name) {
  std::replace(name.begin(), name.end(), '\\', '/');
  while (!name.empty() && name.front() == '/') {
    name.erase(name.begin());
  }
  return name;
}

bool EndsWithNoCase(const std::string &s, const char *suffix) {
  const size_t n = std::strlen(suffix);
  if (s.size() < n) return false;
  for (size_t i = 0; i < n; ++i) {
    unsigned char a = static_cast<unsigned char>(s[s.size() - n + i]);
    unsigned char b = static_cast<unsigned char>(suffix[i]);
    if (std::tolower(a) != std::tolower(b)) return false;
  }
  return true;
}

std::shared_ptr<Layer> ConvertLoadedUSDA(LoadResult &&r,
                                         const std::string &label,
                                         std::string *warn, std::string *err) {
  if (!r.success) {
    if (err) {
      *err += "Failed to load USDA layer: " + label + " : " +
              r.error_summary + "\n";
    }
    return nullptr;
  }
  for (const auto &w : r.warnings) {
    if (warn) *warn += w + "\n";
  }
  return r.stage.ReleaseRootLayer();
}

std::shared_ptr<Layer> ConvertLoadedUSDC(USDCLoadResult &&r,
                                         const std::string &label,
                                         std::string *err) {
  if (!r.success) {
    if (err) {
      *err += "Failed to load USDC layer: " + label + " : " +
              r.error_summary + "\n";
    }
    return nullptr;
  }
  return r.stage.ReleaseRootLayer();
}

ParseOptions MakeUSDAParseOptions(const LayerLoadOptions &options) {
  ParseOptions popts = options.usda_parse_options;
  popts.num_threads = options.parse_num_threads > 0 ? options.parse_num_threads
                                                    : popts.num_threads;
  popts.max_file_size = MinNonZero(popts.max_file_size, options.max_memory);
  return popts;
}

std::shared_ptr<Layer> LoadLayerFromUSDZEntry(USDZReader &reader,
                                              const std::string &package_label,
                                              const std::string &entry_name,
                                              const LayerLoadOptions &options,
                                              std::string *warn,
                                              std::string *err) {
  int idx = -1;
  if (entry_name.empty()) {
    idx = reader.FindUSDCFile();
    if (idx < 0) idx = reader.FindUSDAFile();
  } else {
    const std::string want = NormalizeEntryName(entry_name);
    for (size_t i = 0; i < reader.NumEntries(); ++i) {
      if (NormalizeEntryName(reader.EntryName(i)) == want) {
        idx = static_cast<int>(i);
        break;
      }
    }
  }
  if (idx < 0) {
    if (err) {
      *err += "USDZ layer entry not found: " + package_label +
              (entry_name.empty() ? std::string() : "[" + entry_name + "]") +
              "\n";
    }
    return nullptr;
  }

  const uint8_t *data = reader.EntryData(static_cast<size_t>(idx));
  const size_t size = reader.EntrySize(static_cast<size_t>(idx));
  if (!data || size == 0) {
    if (err) *err += "USDZ layer entry is empty\n";
    return nullptr;
  }

  const std::string label = package_label + "[" + reader.EntryName(static_cast<size_t>(idx)) + "]";
  bool is_usdc = EndsWithNoCase(reader.EntryName(static_cast<size_t>(idx)), ".usdc");
  bool is_usda = EndsWithNoCase(reader.EntryName(static_cast<size_t>(idx)), ".usda");
  if (!is_usdc && !is_usda && size >= 8 &&
      std::memcmp(data, "PXR-USDC", 8) == 0) {
    is_usdc = true;
  }
  if (is_usdc) {
    USDCLoadOptions lopts;
    lopts.crate_options.max_memory = options.max_memory;
    return ConvertLoadedUSDC(LoadUSDCFromMemory(data, size, lopts), label, err);
  }
  if (is_usda) {
    LoadOptions lopts;
    lopts.parse_options = MakeUSDAParseOptions(options);
    return ConvertLoadedUSDA(
        LoadUSDAFromString(reinterpret_cast<const char *>(data), size, lopts),
        label, warn, err);
  }

  if (err) *err += "Unsupported USDZ layer entry format: " + label + "\n";
  return nullptr;
}

std::shared_ptr<Layer> LoadLayerFromUSDZ(const std::string &package_file,
                                         const std::string &entry_name,
                                         const LayerLoadOptions &options,
                                         std::string *warn, std::string *err) {
  USDZReadOptions zopts;
  zopts.max_archive_size = options.max_memory;
  zopts.max_entry_size = options.max_memory;

  USDZReader reader;
  if (!reader.OpenFile(package_file, zopts)) {
    if (err) {
      *err += "Failed to load USDZ package: " + package_file + " : " +
              (reader.Error().empty() ? "open failed" : reader.Error()) + "\n";
    }
    return nullptr;
  }

  return LoadLayerFromUSDZEntry(reader, package_file, entry_name, options,
                                warn, err);
}

}  // namespace

namespace {

// Stamp every prim in a freshly-loaded layer with the directory its RELATIVE
// asset paths anchor to: the layer's own directory. Composition flattens prims
// from many layers into one, so without this the authoring layer -- and with it
// the only correct anchor for `@../tex/foo.png@` -- is lost. See asset-anchor.hh.
//
// USDZ is deliberately excluded: paths inside a package are package-relative and
// are resolved against the archive, not the filesystem, so they must keep
// anchor 0 (the consumer's existing package handling takes over).
void StampAssetAnchor(Layer *layer, const std::string &resolved_path) {
  if (!layer) return;
  const size_t slash = resolved_path.find_last_of("/\\");
  if (slash == std::string::npos) return;  // bare filename: cwd, nothing to add

  const uint32_t id = InternAssetAnchor(resolved_path.substr(0, slash));
  if (id == 0) return;
  for (size_t i = 0; i < layer->prim_count(); ++i) {
    if (PrimSpec *ps = layer->prim_mutable(static_cast<uint32_t>(i))) {
      ps->set_asset_anchor_id(id);
    }
  }
}

std::shared_ptr<Layer> LoadLayerFromFileUnstamped(
    const std::string &resolved_path, std::string *warn, std::string *err,
    const LayerLoadOptions &options);

}  // namespace

std::shared_ptr<Layer> LoadLayerFromFile(const std::string &resolved_path,
                                         std::string *warn, std::string *err,
                                         const LayerLoadOptions &options) {
  std::shared_ptr<Layer> layer =
      LoadLayerFromFileUnstamped(resolved_path, warn, err, options);
  // Package entries resolve inside the archive; leave them at anchor 0.
  if (layer && !AssetResolver::IsPackagePath(resolved_path)) {
    StampAssetAnchor(layer.get(), resolved_path);
  }
  return layer;
}

namespace {

std::shared_ptr<Layer> LoadLayerFromFileUnstamped(
    const std::string &resolved_path, std::string *warn, std::string *err,
    const LayerLoadOptions &options) {
  if (AssetResolver::IsPackagePath(resolved_path)) {
    std::string package_file;
    std::string entry_name;
    if (AssetResolver::ParsePackagePath(resolved_path, &package_file,
                                        &entry_name)) {
      return LoadLayerFromUSDZ(package_file, entry_name, options, warn, err);
    }
  }

  std::string ext = ToLowerExt(resolved_path);

  // `.usd` is ambiguous (USDA text OR crate binary). Disambiguate by the crate
  // magic ("PXR-USDC"); UE-exported scenes ship the root layer as `.usd` crate.
  if (ext == "usd") {
    char magic[8] = {0};
    std::ifstream f(resolved_path, std::ios::binary);
    if (f.read(magic, sizeof(magic)) &&
        std::memcmp(magic, "PXR-USDC", sizeof(magic)) == 0) {
      ext = "usdc";
    } else {
      ext = "usda";
    }
  }

  if (ext == "usda") {
    LoadOptions lopts;
    lopts.parse_options = options.usda_parse_options;
    lopts.parse_options.num_threads =
        options.parse_num_threads > 0 ? options.parse_num_threads
                                     : lopts.parse_options.num_threads;
    lopts.parse_options.max_file_size =
        MinNonZero(lopts.parse_options.max_file_size, options.max_memory);
    return ConvertLoadedUSDA(LoadUSDAFromFile(resolved_path, lopts),
                             resolved_path, warn, err);
  }

  if (ext == "usdc") {
    USDCLoadOptions lopts;
    lopts.crate_options.max_memory = options.max_memory;
    return ConvertLoadedUSDC(LoadUSDCFromFile(resolved_path, lopts),
                             resolved_path, err);
  }

  if (ext == "usdz") {
    return LoadLayerFromUSDZ(resolved_path, std::string(), options, warn, err);
  }

  if (err) {
    *err += "Unsupported layer file format for: " + resolved_path + "\n";
  }
  return nullptr;
}

}  // namespace

std::shared_ptr<Layer> LoadLayerFromFile(const std::string &resolved_path,
                                         std::string *warn, std::string *err,
                                         int parse_num_threads) {
  LayerLoadOptions options;
  options.parse_num_threads = parse_num_threads;
  options.usda_parse_options.num_threads = parse_num_threads;
  return LoadLayerFromFile(resolved_path, warn, err, options);
}

std::shared_ptr<Layer> LoadLayerFromMemory(const std::string &key,
                                           const uint8_t *data, size_t size,
                                           std::string *warn, std::string *err,
                                           const LayerLoadOptions &options) {
  if (!data || size == 0) {
    if (err) *err += "Empty layer buffer for: " + key + "\n";
    return nullptr;
  }
  if (options.max_memory > 0 && size > options.max_memory) {
    if (err) {
      *err += "Layer buffer exceeds max_memory for: " + key + "\n";
    }
    return nullptr;
  }

  if (size >= 8 && std::memcmp(data, "PXR-USDC", 8) == 0) {
    USDCLoadOptions lopts;
    lopts.crate_options.max_memory = options.max_memory;
    return ConvertLoadedUSDC(LoadUSDCFromMemory(data, size, lopts), key, err);
  }

  if (size >= 4 && std::memcmp(data, "PK\x03\x04", 4) == 0) {
    // A package-path key ("pkg.usdz[entry]") selects a specific entry;
    // otherwise the first .usdc (then .usda) entry is the root layer.
    std::string entry_name;
    if (AssetResolver::IsPackagePath(key)) {
      std::string package_file;
      AssetResolver::ParsePackagePath(key, &package_file, &entry_name);
    }
    USDZReadOptions zopts;
    zopts.max_archive_size = options.max_memory;
    zopts.max_entry_size = options.max_memory;
    USDZReader reader;
    if (!reader.Open(data, size, zopts)) {
      if (err) {
        *err += "Failed to open USDZ layer buffer: " + key + " : " +
                (reader.Error().empty() ? "open failed" : reader.Error()) +
                "\n";
      }
      return nullptr;
    }
    return LoadLayerFromUSDZEntry(reader, key, entry_name, options, warn, err);
  }

  LoadOptions lopts;
  lopts.parse_options = MakeUSDAParseOptions(options);
  return ConvertLoadedUSDA(
      LoadUSDAFromString(reinterpret_cast<const char *>(data), size, lopts),
      key, warn, err);
}

std::shared_ptr<Layer> LoadLayerFromMemoryOwned(const std::string &key,
                                                std::string &&data,
                                                std::string *warn,
                                                std::string *err,
                                                const LayerLoadOptions &options) {
  if (data.empty()) {
    if (err) *err += "Empty layer buffer for: " + key + "\n";
    return nullptr;
  }
  if (options.max_memory > 0 && data.size() > options.max_memory) {
    if (err) {
      *err += "Layer buffer exceeds max_memory for: " + key + "\n";
    }
    return nullptr;
  }

  if (data.size() >= 8 && std::memcmp(data.data(), "PXR-USDC", 8) == 0) {
    USDCLoadOptions lopts;
    lopts.crate_options.max_memory = options.max_memory;
    return ConvertLoadedUSDC(LoadUSDCFromMemoryOwned(std::move(data), lopts),
                             key, err);
  }

  if (data.size() >= 4 && std::memcmp(data.data(), "PK\x03\x04", 4) == 0) {
    // USDZ entries are parsed with copying readers, so the archive buffer can
    // be released when this call returns.
    return LoadLayerFromMemory(key,
                               reinterpret_cast<const uint8_t *>(data.data()),
                               data.size(), warn, err, options);
  }

  LoadOptions lopts;
  lopts.parse_options = MakeUSDAParseOptions(options);
  return ConvertLoadedUSDA(LoadUSDAFromStringOwned(std::move(data), lopts),
                           key, warn, err);
}

std::shared_ptr<Layer> LayerRegistry::GetOrLoad(AssetResolver &resolver,
                                                const std::string &asset_path,
                                                const std::string &anchor,
                                                std::string *warn,
                                                std::string *err,
                                                const LayerLoadOptions &options) {
  ResolvedAsset resolved_asset = resolver.Resolve(asset_path, anchor);
  const std::string resolved = resolved_asset.resolved_path;
  if (resolved.empty()) {
    if (err) *err += "Failed to resolve asset path: " + asset_path + "\n";
    return nullptr;
  }

#if defined(TINYUSDZ_ENABLE_THREAD)
  std::shared_future<LoadOutcome> wait_fut;
  std::shared_ptr<std::promise<LoadOutcome>> my_promise;
  {
    std::lock_guard<std::mutex> lk(*mu_);
    auto it = by_resolved_.find(resolved);
    if (it != by_resolved_.end()) return it->second;  // already parsed

    auto fit = in_flight_.find(resolved);
    if (fit != in_flight_.end()) {
      wait_fut = fit->second;  // another thread is parsing this path
    } else {
      // Become the loader: publish a future, then parse outside the lock.
      my_promise = std::make_shared<std::promise<LoadOutcome>>();
      in_flight_.emplace(resolved, my_promise->get_future().share());
    }
  }

  if (!my_promise) {
    // Someone else is loading this exact path; wait for them (outside the lock).
    LoadOutcome outcome = wait_fut.get();
    if (warn) *warn += outcome.warn;
    if (err) *err += outcome.err;
    return outcome.layer;
  }

  // Parse WITHOUT holding the lock, so other paths load concurrently.
  LoadOutcome outcome;
  outcome.layer = LoadLayerFromFile(resolved, &outcome.warn, &outcome.err,
                                    options);
  {
    std::lock_guard<std::mutex> lk(*mu_);
    if (outcome.layer) {
      ++parse_count_;
      by_resolved_.emplace(resolved, outcome.layer);
    }
    in_flight_.erase(resolved);  // a failed load is retried by the next caller
  }
  if (warn) *warn += outcome.warn;
  if (err) *err += outcome.err;
  my_promise->set_value(outcome);  // unblock any waiters
  return outcome.layer;
#else
  auto it = by_resolved_.find(resolved);
  if (it != by_resolved_.end()) {
    return it->second;  // Cache hit -- no re-parse.
  }

  std::shared_ptr<Layer> layer = LoadLayerFromFile(resolved, warn, err,
                                                   options);
  if (!layer) {
    return nullptr;
  }

  ++parse_count_;
  by_resolved_.emplace(resolved, layer);
  return layer;
#endif
}

void LayerRegistry::Drop(const std::string &resolved_path) {
#if defined(TINYUSDZ_ENABLE_THREAD)
  std::lock_guard<std::mutex> lk(*mu_);
#endif
  by_resolved_.erase(resolved_path);
}

}  // namespace pcp
}  // namespace next
}  // namespace tinyusdz
