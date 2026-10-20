// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// LightUSD Next - Asset Resolution Implementation

#include "asset-resolver.hh"
#include "../safe-file-size.hh"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <cstring>
#include <new>
#include <utility>
#if !defined(__EMSCRIPTEN__)
#include <filesystem>
#endif

#ifdef _WIN32
#include <direct.h>
#include <sys/stat.h>
#define PATH_SEPARATOR '\\'
#define ALT_PATH_SEPARATOR '/'
#else
#include <unistd.h>
#include <sys/stat.h>
#define PATH_SEPARATOR '/'
#define ALT_PATH_SEPARATOR '\\'
#endif

namespace lightusd {
namespace next {

bool AssetPayloadBudget::SetLimit(uint64_t limit_bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (limit_bytes < used_bytes_) return false;
  limit_bytes_ = limit_bytes;
  return true;
}

std::shared_ptr<void> AssetPayloadBudget::Reserve(uint64_t bytes) {
  if (bytes == 0) return {};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (bytes > limit_bytes_ - std::min(used_bytes_, limit_bytes_)) return {};
    used_bytes_ += bytes;
  }
  const auto owner = shared_from_this();
  return std::shared_ptr<void>(new uint8_t{0}, [owner, bytes](void* token) {
    delete static_cast<uint8_t*>(token);
    owner->Release(bytes);
  });
}

uint64_t AssetPayloadBudget::UsedBytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return used_bytes_;
}

uint64_t AssetPayloadBudget::LimitBytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return limit_bytes_;
}

void AssetPayloadBudget::Release(uint64_t bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  used_bytes_ = bytes > used_bytes_ ? 0 : used_bytes_ - bytes;
}

namespace {

// Global default resolver
std::unique_ptr<AssetResolver> g_default_resolver;

bool FileExistsImpl(const std::string& path) {
#if defined(__EMSCRIPTEN__)
  // wasm builds may have no filesystem at all (-sFILESYSTEM=0), where even a
  // stat syscall aborts. Resolution there goes through the custom resolver
  // (asset-cache backed); plain file paths never exist.
  (void)path;
  return false;
#elif defined(_WIN32)
  std::ifstream f(path);
  return f.good();
#else
  // Follow symlinks: production USD asset trees commonly use them for shared
  // payload and texture roots. The subsequent reader still enforces its size
  // and format limits.
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

bool HasParentComponent(const std::string& path) {
  size_t begin = 0;
  while (begin <= path.size()) {
    size_t end = path.find_first_of("/\\", begin);
    if (end == std::string::npos) end = path.size();
    if (path.compare(begin, end - begin, "..") == 0) return true;
    if (end == path.size()) break;
    begin = end + 1;
  }
  return false;
}

std::string FindRecursively(const std::string& root,
                            const std::string& asset_path) {
#if defined(__EMSCRIPTEN__)
  (void)root;
  (void)asset_path;
  return {};
#else
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path root_path(root);
  if (!fs::is_directory(root_path, ec) || ec) return {};

  const fs::path wanted(asset_path);
  std::vector<std::string> matches;
  fs::recursive_directory_iterator it(
      root_path, fs::directory_options::skip_permission_denied, ec);
  const fs::recursive_directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) {
      ec.clear();
      continue;
    }
    if (!it->is_regular_file(ec) || ec) {
      ec.clear();
      continue;
    }
    // Recursive search is a basename lookup. Authored directory suffixes are
    // already handled deterministically by the normal and suffix-fallback
    // paths before reaching this broader search.
    if (it->path().filename() == wanted.filename()) {
      matches.push_back(it->path().lexically_normal().string());
    }
  }
  if (matches.empty()) return {};
  std::sort(matches.begin(), matches.end());
  return matches.front();
#endif
}

std::string NormalizePackageEntryPath(std::string path) {
  if (path.empty() || path.front() == '/' || path.front() == '\\' ||
      (path.size() >= 2 &&
       ((path[0] >= 'A' && path[0] <= 'Z') ||
        (path[0] >= 'a' && path[0] <= 'z')) &&
       path[1] == ':')) {
    return {};
  }
  for (char& c : path) {
    if (c == '\\') c = '/';
  }

  std::vector<std::string> parts;
  size_t start = 0;
  while (start < path.size()) {
    size_t end = path.find('/', start);
    if (end == std::string::npos) end = path.size();
    std::string part = path.substr(start, end - start);
    if (part == "..") {
      if (parts.empty()) return {};
      parts.pop_back();
    } else if (!part.empty() && part != ".") {
      parts.push_back(part);
    }
    start = end + 1;
  }

  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) out += '/';
    out += parts[i];
  }
  return out;
}

std::string GetCurrentWorkingDirectory() {
#if defined(__EMSCRIPTEN__)
  // No filesystem (and the getcwd syscall ABORTS under -sFILESYSTEM=0).
  return ".";
#elif defined(_WIN32)
  char buffer[4096];
  if (_getcwd(buffer, sizeof(buffer))) {
    return std::string(buffer);
  }
#else
  char buffer[4096];
  if (getcwd(buffer, sizeof(buffer))) {
    return std::string(buffer);
  }
#endif
  return ".";
}

char ContextSearchPathSeparator() {
#ifdef _WIN32
  return ';';
#else
  return ':';
#endif
}

std::string LowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  return value;
}

std::vector<std::string> ParseContextSearchPaths(const std::string& text) {
  std::vector<std::string> paths;
  if (text.empty()) return paths;
  const char separator = ContextSearchPathSeparator();
  size_t start = 0;
  while (start <= text.size()) {
    const size_t end = text.find(separator, start);
    const size_t stop = end == std::string::npos ? text.size() : end;
    if (stop > start) paths.push_back(text.substr(start, stop - start));
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return paths;
}

ResolverContext NormalizeContext(ResolverContext context) {
  if (context.search_paths.empty() && !context.context_string.empty()) {
    context.search_paths = ParseContextSearchPaths(context.context_string);
  }
  context.is_default =
      context.context_string.empty() && context.search_paths.empty();
  return context;
}

std::string PackageFileExtension(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos ||
      (slash != std::string::npos && dot < slash) ||
      dot + 1 >= path.size()) {
    return {};
  }
  return LowerAscii(path.substr(dot + 1));
}

std::string NormalizePackageExtension(const std::string& extension) {
  const size_t start =
      (!extension.empty() && extension.front() == '.') ? 1 : 0;
  if (start >= extension.size()) return {};
  return LowerAscii(extension.substr(start));
}

void AppendCacheKeyPart(std::string* key, const std::string& part) {
  key->append(std::to_string(part.size()));
  key->push_back('|');
  key->append(part);
}

std::string MakeResolveCacheKey(const std::string& asset_path,
                                const std::string& anchor_path,
                                bool allow_suffix_fallback,
                                const ResolverContext& context) {
  std::string key;
  AppendCacheKeyPart(&key, asset_path);
  AppendCacheKeyPart(&key, anchor_path);
  key.push_back(allow_suffix_fallback ? '1' : '0');
  AppendCacheKeyPart(&key, context.context_string);
  for (const std::string& path : context.search_paths) {
    AppendCacheKeyPart(&key, path);
  }
  return key;
}

// Per-thread context bindings. The node pointer is also handed to callers
// through BindContext(binding_data) so nested/out-of-order unbinds are exact.
struct ContextBindingNode {
  uint64_t identity = 0;
  ResolverContext context;
};
thread_local std::vector<std::unique_ptr<ContextBindingNode>>
    g_context_bindings;

ContextBindingNode* FindActiveContextBinding(uint64_t identity) {
  for (auto it = g_context_bindings.rbegin(); it != g_context_bindings.rend();
       ++it) {
    if (*it && (*it)->identity == identity) return it->get();
  }
  return nullptr;
}

void EraseContextBinding(const void* node) {
  if (!node) return;
  for (auto it = g_context_bindings.begin(); it != g_context_bindings.end();
       ++it) {
    if (it->get() == node) {
      g_context_bindings.erase(it);
      return;
    }
  }
}

// Per-thread scoped resolution caches. shared_ptr keeps a node alive if a
// nested Resolve() still holds it while EndCacheScope() removes it.
struct CacheScopeNode {
  uint64_t identity = 0;
  std::unordered_map<std::string, ResolvedAsset> entries;
};
thread_local std::vector<std::shared_ptr<CacheScopeNode>> g_cache_scopes;

std::shared_ptr<CacheScopeNode> FindActiveCacheScope(uint64_t identity) {
  for (auto it = g_cache_scopes.rbegin(); it != g_cache_scopes.rend(); ++it) {
    if (*it && (*it)->identity == identity) return *it;
  }
  return {};
}

void EraseCacheScope(const void* node) {
  if (!node) return;
  for (auto it = g_cache_scopes.begin(); it != g_cache_scopes.end(); ++it) {
    if (it->get() == node) {
      g_cache_scopes.erase(it);
      return;
    }
  }
}

}  // namespace

// ============================================================
// AssetResolver
// ============================================================

uint64_t AssetResolver::NextIdentity() {
  static std::atomic<uint64_t> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

void AssetResolver::NormalizeConfigContext() {
  config_.context = NormalizeContext(std::move(config_.context));
}

AssetResolver::AssetResolver() {
  config_.working_directory = GetCurrentWorkingDirectory();
}

AssetResolver::AssetResolver(const ResolverConfig& config) : config_(config) {
  if (config_.working_directory.empty()) {
    config_.working_directory = GetCurrentWorkingDirectory();
  }
  NormalizeConfigContext();
}

AssetResolver::AssetResolver(const AssetResolver& other) {
  std::lock_guard<std::mutex> lock(other.registry_mutex_);
  identity_ = NextIdentity();
  config_ = other.config_;
  custom_resolver_ = other.custom_resolver_;
  asset_reader_ = other.asset_reader_;
  scheme_handlers_ = other.scheme_handlers_;
  package_resolvers_ = other.package_resolvers_;
  memory_assets_ = other.memory_assets_;
  next_anonymous_id_ = other.next_anonymous_id_;
}

AssetResolver& AssetResolver::operator=(const AssetResolver& other) {
  if (this == &other) return *this;
  std::scoped_lock lock(registry_mutex_, other.registry_mutex_);
  identity_ = NextIdentity();
  config_ = other.config_;
  custom_resolver_ = other.custom_resolver_;
  asset_reader_ = other.asset_reader_;
  scheme_handlers_ = other.scheme_handlers_;
  package_resolvers_ = other.package_resolvers_;
  memory_assets_ = other.memory_assets_;
  next_anonymous_id_ = other.next_anonymous_id_;
  return *this;
}

AssetResolver::AssetResolver(AssetResolver&& other) noexcept {
  std::lock_guard<std::mutex> lock(other.registry_mutex_);
  identity_ = other.identity_;
  other.identity_ = NextIdentity();
  config_ = std::move(other.config_);
  custom_resolver_ = std::move(other.custom_resolver_);
  asset_reader_ = std::move(other.asset_reader_);
  scheme_handlers_ = std::move(other.scheme_handlers_);
  package_resolvers_ = std::move(other.package_resolvers_);
  memory_assets_ = std::move(other.memory_assets_);
  next_anonymous_id_ = other.next_anonymous_id_;
}

AssetResolver& AssetResolver::operator=(AssetResolver&& other) noexcept {
  if (this == &other) return *this;
  std::scoped_lock lock(registry_mutex_, other.registry_mutex_);
  identity_ = other.identity_;
  other.identity_ = NextIdentity();
  config_ = std::move(other.config_);
  custom_resolver_ = std::move(other.custom_resolver_);
  asset_reader_ = std::move(other.asset_reader_);
  scheme_handlers_ = std::move(other.scheme_handlers_);
  package_resolvers_ = std::move(other.package_resolvers_);
  memory_assets_ = std::move(other.memory_assets_);
  next_anonymous_id_ = other.next_anonymous_id_;
  return *this;
}

AssetResolver::~AssetResolver() = default;

void AssetResolver::SetWorkingDirectory(const std::string& dir) {
  config_.working_directory = dir;
}

void AssetResolver::AddSearchPath(const std::string& path) {
  config_.search_paths.push_back(path);
}

void AssetResolver::SetSearchPaths(const std::vector<std::string>& paths) {
  config_.search_paths = paths;
}

void AssetResolver::ClearSearchPaths() {
  config_.search_paths.clear();
}

void AssetResolver::SetCustomResolver(ResolverCallback callback) {
  custom_resolver_ = std::move(callback);
}

void AssetResolver::SetAssetReader(AssetReadCallback reader) {
  asset_reader_ = std::move(reader);
}

bool AssetResolver::HasUserCallbacks() const {
  if (custom_resolver_ || asset_reader_) return true;
  std::lock_guard<std::mutex> lock(registry_mutex_);
  if (!package_resolvers_.empty()) return true;
  for (const auto& entry : scheme_handlers_) {
    if (entry.second.resolver || entry.second.reader) return true;
  }
  return false;
}

void AssetResolver::RegisterScheme(const std::string& scheme,
                                   ResolverCallback resolver,
                                   AssetReadCallback reader) {
  std::string normalized = scheme;
  if (!normalized.empty() && normalized.back() == ':') normalized.pop_back();
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (normalized.empty() ||
      GetIdentifierScheme(normalized + ":asset") != normalized) {
    return;
  }
  std::lock_guard<std::mutex> lock(registry_mutex_);
  scheme_handlers_[normalized] =
      SchemeHandler{std::move(resolver), std::move(reader)};
}

bool AssetResolver::UnregisterScheme(const std::string& scheme) {
  std::string normalized = scheme;
  if (!normalized.empty() && normalized.back() == ':') normalized.pop_back();
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::lock_guard<std::mutex> lock(registry_mutex_);
  return scheme_handlers_.erase(normalized) != 0;
}

bool AssetResolver::HasScheme(const std::string& scheme) const {
  std::string normalized = scheme;
  if (!normalized.empty() && normalized.back() == ':') normalized.pop_back();
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::lock_guard<std::mutex> lock(registry_mutex_);
  return scheme_handlers_.find(normalized) != scheme_handlers_.end();
}

std::string AssetResolver::RegisterMemoryAsset(const std::string& identifier,
                                               std::vector<uint8_t> bytes) {
  return RegisterMemoryAssetView(identifier,
      std::make_shared<const std::vector<uint8_t>>(std::move(bytes)));
}

std::string AssetResolver::RegisterMemoryAssetView(
    const std::string& identifier,
    std::shared_ptr<const std::vector<uint8_t>> bytes) {
  if (!bytes) return {};
  std::lock_guard<std::mutex> lock(registry_mutex_);
  std::string key = identifier;
  if (key.empty()) {
    key = "usd-anon:" + std::to_string(next_anonymous_id_++);
  }
  memory_assets_[key] = std::move(bytes);
  return key;
}

bool AssetResolver::UnregisterMemoryAsset(const std::string& identifier) {
  std::lock_guard<std::mutex> lock(registry_mutex_);
  return memory_assets_.erase(identifier) != 0;
}

void AssetResolver::ClearMemoryAssets() {
  std::lock_guard<std::mutex> lock(registry_mutex_);
  memory_assets_.clear();
}

std::shared_ptr<const std::vector<uint8_t>> AssetResolver::GetMemoryAssetView(
    const std::string& identifier) const {
  std::lock_guard<std::mutex> lock(registry_mutex_);
  const auto it = memory_assets_.find(identifier);
  return it == memory_assets_.end() ? nullptr : it->second;
}

bool AssetResolver::ReadAsset(const std::string& resolved_path,
                              std::vector<uint8_t>* out,
                              std::string* err) const {
  if (!out) {
    if (err) *err += "ReadAsset: null output buffer\n";
    return false;
  }
  out->clear();

  std::shared_ptr<const std::vector<uint8_t>> memory;
  AssetReadCallback scheme_reader;
  const std::string scheme = GetIdentifierScheme(resolved_path);
  {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    const auto memory_it = memory_assets_.find(resolved_path);
    if (memory_it != memory_assets_.end()) memory = memory_it->second;
    const auto scheme_it = scheme_handlers_.find(scheme);
    if (scheme_it != scheme_handlers_.end()) scheme_reader = scheme_it->second.reader;
  }
  if (memory) {
    *out = *memory;
    return true;
  }
  if (scheme_reader) return scheme_reader(resolved_path, out, err);

  if (asset_reader_) {
    return asset_reader_(resolved_path, out, err);
  }

  if (!scheme.empty()) {
    if (err) *err += "ReadAsset: no reader registered for scheme `" + scheme +
                     "`: " + resolved_path + "\n";
    return false;
  }

#if defined(__EMSCRIPTEN__)
  // No filesystem under -sFILESYSTEM=0; bytes must come from a custom reader.
  if (err) {
    *err += "ReadAsset: no asset reader installed (wasm build has no "
            "filesystem): " + resolved_path + "\n";
  }
  return false;
#else
  std::ifstream f(resolved_path, std::ios::binary | std::ios::ate);
  if (!f) {
    if (err) *err += "ReadAsset: failed to open: " + resolved_path + "\n";
    return false;
  }
  size_t size = 0;
  if (!SafeStreamSize(f, 0, &size)) {
    if (err) *err += "ReadAsset: implausible file size: " + resolved_path + "\n";
    return false;
  }
  f.seekg(0, std::ios::beg);
  out->resize(size);
  if (size > 0 &&
      !f.read(reinterpret_cast<char*>(out->data()), size)) {
    if (err) *err += "ReadAsset: short read: " + resolved_path + "\n";
    out->clear();
    return false;
  }
  return true;
#endif
}

const std::vector<std::string>& AssetResolver::EffectiveSearchPaths() const {
  if (const ContextBindingNode* active = FindActiveContextBinding(identity_)) {
    // A scoped binding replaces the persistent context. Binding a default
    // context explicitly falls back to the configured search paths.
    return active->context.is_default ? config_.search_paths
                                      : active->context.search_paths;
  }
  return config_.context.is_default ? config_.search_paths
                                    : config_.context.search_paths;
}

AssetResolver::PackageResolverCallback AssetResolver::FindPackageResolver(
    const std::string& package_file) const {
  const std::string extension = PackageFileExtension(package_file);
  if (extension.empty()) return {};
  std::lock_guard<std::mutex> lock(registry_mutex_);
  const auto it = package_resolvers_.find(extension);
  return it == package_resolvers_.end() ? PackageResolverCallback{} : it->second;
}

ResolvedAsset AssetResolver::Resolve(const std::string& asset_path,
                                      const std::string& anchor_path,
                                      bool allow_suffix_fallback) const {
  const std::shared_ptr<CacheScopeNode> cache =
      FindActiveCacheScope(identity_);
  std::string cache_key;
  if (cache && !asset_path.empty()) {
    cache_key = MakeResolveCacheKey(asset_path, anchor_path,
                                    allow_suffix_fallback,
                                    GetCurrentContext());
    const auto it = cache->entries.find(cache_key);
    if (it != cache->entries.end()) return it->second;
  }
  ResolvedAsset result =
      ResolveInternal(asset_path, anchor_path, allow_suffix_fallback);
  if (cache && !cache_key.empty()) {
    cache->entries.emplace(std::move(cache_key), result);
  }
  return result;
}

std::string AssetResolver::ResolvePath(const std::string& asset_path,
                                        const std::string& anchor_path,
                                        bool allow_suffix_fallback) const {
  ResolvedAsset resolved =
      Resolve(asset_path, anchor_path, allow_suffix_fallback);
  return resolved.resolved_path;
}

bool AssetResolver::Exists(const std::string& asset_path,
                           const std::string& anchor_path) const {
  ResolvedAsset resolved = Resolve(asset_path, anchor_path);
  return resolved.exists;
}

std::string AssetResolver::CreateIdentifier(const std::string& asset_path,
                                             const std::string& anchor_path) const {
  const ResolvedAsset resolved = Resolve(asset_path, anchor_path);
  if (resolved.exists || IsAbsolutePath(asset_path) ||
      IsPackagePath(asset_path) || !GetIdentifierScheme(asset_path).empty()) {
    return resolved.resolved_path;
  }
  // Identifiers must remain context-dependent even when the asset has not
  // been created yet; otherwise two layers authoring the same relative token
  // collide in caches.
  const std::string base = anchor_path.empty()
                               ? config_.working_directory
                               : GetDirectory(anchor_path);
  return NormalizePath(JoinPath(base, asset_path));
}

ResolvedAsset AssetResolver::ResolveInternal(const std::string& asset_path,
                                              const std::string& anchor_path,
                                              bool allow_suffix_fallback) const {
  ResolvedAsset result;
  result.original_path = asset_path;

  if (asset_path.empty()) {
    return result;
  }

  // Reject `..` escapes before any lookup -- this is a containment check, so it
  // must run ahead of the scheme/memory-asset resolution below.
  if (!config_.allow_parent_paths && HasParentComponent(asset_path)) {
    return result;
  }

  ResolverCallback scheme_resolver;
  bool memory_asset = false;
  const std::string identifier_scheme = GetIdentifierScheme(asset_path);
  {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    memory_asset = memory_assets_.find(asset_path) != memory_assets_.end();
    const auto it = scheme_handlers_.find(identifier_scheme);
    if (it != scheme_handlers_.end()) scheme_resolver = it->second.resolver;
  }
  if (memory_asset) {
    result.resolved_path = asset_path;
    result.exists = true;
    return result;
  }

  // Check for package path (USDZ or a custom package registered by extension).
  if (IsPackagePath(asset_path)) {
    result.is_package = true;
    std::string package_file;
    std::string package_entry;
    if (!ParsePackagePath(asset_path, &package_file, &package_entry)) {
      return result;
    }
    result.package_path = package_file;
    result.asset_in_package = NormalizePackageEntryPath(package_entry);
    if (result.asset_in_package.empty()) return result;

    const PackageResolverCallback package_resolver =
        FindPackageResolver(package_file);
    std::string custom_resolved;
    if (package_resolver &&
        package_resolver(package_file, result.asset_in_package,
                         &custom_resolved) &&
        !custom_resolved.empty()) {
      result.resolved_path = std::move(custom_resolved);
      result.exists = true;
      return result;
    }

    // Resolve the package file itself.
    ResolvedAsset pkg = ResolveInternal(package_file, anchor_path,
                                        allow_suffix_fallback);
    if (pkg.exists) {
      // Give a registered package resolver a second chance with the resolved
      // path so virtual package formats can still observe the real container.
      if (package_resolver && pkg.resolved_path != package_file) {
        custom_resolved.clear();
        if (package_resolver(pkg.resolved_path, result.asset_in_package,
                             &custom_resolved) &&
            !custom_resolved.empty()) {
          result.resolved_path = std::move(custom_resolved);
          result.package_path = pkg.resolved_path;
          result.exists = true;
          return result;
        }
      }
      result.resolved_path =
          pkg.resolved_path + "[" + result.asset_in_package + "]";
      result.package_path = pkg.resolved_path;
      result.exists = true;  // Assume asset exists in package if package exists
    }
    return result;
  }

  // Explicit schemes never fall through into filesystem path handling.
  if (!identifier_scheme.empty()) {
    std::string scheme_result;
    if (scheme_resolver) {
      scheme_result = scheme_resolver(asset_path, anchor_path);
    } else if (custom_resolver_) {
      // Backward compatibility: the catch-all callback historically emulated
      // URI schemes before explicit registration existed.
      scheme_result = custom_resolver_(asset_path, anchor_path);
    }
    if (!scheme_result.empty()) {
      result.resolved_path = scheme_result;
      result.exists = true;
    } else {
      result.resolved_path = asset_path;
    }
    return result;
  }

  // Try custom resolver first
  if (custom_resolver_) {
    std::string custom_result = custom_resolver_(asset_path, anchor_path);
    if (!custom_result.empty()) {
      result.resolved_path = GetIdentifierScheme(custom_result).empty()
                                 ? NormalizePath(custom_result)
                                 : custom_result;
      // A custom resolver returning a path asserts the asset resolved; its
      // result need not be a filesystem path (e.g. a wasm asset-cache key),
      // so don't second-guess it with a file probe.
      result.exists = true;
      return result;
    }
  }

  // Absolute path
  if (IsAbsolutePath(asset_path)) {
    if (config_.allow_absolute_paths) {
      result.resolved_path = NormalizePath(asset_path);
      result.exists = FileExists(result.resolved_path);
    }
    // A missing absolute path falls through to the suffix fallback below
    // (assets authored with another machine's absolute prefix).
    if (result.exists ||
        !(allow_suffix_fallback && config_.enable_suffix_fallback)) {
      return result;
    }
  }

  // Relative paths authored inside a package layer resolve to another entry in
  // that same package. This lets pkg.usdz[root.usda] reference @geom.usda@
  // without escaping to the filesystem beside pkg.usdz.
  if (!anchor_path.empty() && IsPackagePath(anchor_path)) {
    std::string anchor_package;
    std::string anchor_entry;
    if (ParsePackagePath(anchor_path, &anchor_package, &anchor_entry)) {
      ResolvedAsset pkg;
      if (!config_.allow_absolute_paths && IsAbsolutePath(anchor_package)) {
        // The package path is not newly-authored input: it is the already
        // resolved container portion of `anchor_path`. Reusing that exact
        // container does not grant access to another absolute path.
        pkg.resolved_path = NormalizePath(anchor_package);
        pkg.exists = FileExists(pkg.resolved_path);
      } else {
        pkg = ResolveInternal(anchor_package, "");
      }
      if (pkg.exists) {
        const std::string entry_dir = GetDirectory(anchor_entry);
        std::string entry = (entry_dir == "." || entry_dir.empty())
                                ? asset_path
                                : JoinPath(entry_dir, asset_path);
        entry = NormalizePackageEntryPath(entry);
        if (entry.empty()) return result;
        result.is_package = true;
        result.package_path = pkg.resolved_path;
        result.asset_in_package = entry;
        result.resolved_path = pkg.resolved_path + "[" + entry + "]";
        result.exists = true;  // The package exists; entry validation happens on open.
        return result;
      }
    }
  }

  // Relative path - try relative to anchor first
  if (!anchor_path.empty()) {
    std::string anchor_dir = GetDirectory(anchor_path);
    std::string candidate = JoinPath(anchor_dir, asset_path);
    candidate = NormalizePath(candidate);
    if (FileExists(candidate)) {
      result.resolved_path = candidate;
      result.exists = true;
      return result;
    }
  }

  // Try relative to working directory
  {
    std::string candidate = JoinPath(config_.working_directory, asset_path);
    candidate = NormalizePath(candidate);
    if (FileExists(candidate)) {
      result.resolved_path = candidate;
      result.exists = true;
      return result;
    }
  }

  // Search in the effective search paths (scoped context, persistent context,
  // or configured paths).
  const std::vector<std::string>& search_paths = EffectiveSearchPaths();
  for (const auto& search_path : search_paths) {
    std::string candidate = JoinPath(search_path, asset_path);
    candidate = NormalizePath(candidate);
    if (FileExists(candidate)) {
      result.resolved_path = candidate;
      result.exists = true;
      return result;
    }
  }

  if (config_.search_recursively) {
    for (const auto& search_path : search_paths) {
      const std::string candidate = FindRecursively(search_path, asset_path);
      if (!candidate.empty()) {
        result.resolved_path = NormalizePath(candidate);
        result.exists = true;
        return result;
      }
    }
  }

  // Suffix fallback: rehome paths authored with absolute / machine-specific
  // prefixes by retrying progressively shorter suffixes (down to the
  // basename) through the same anchor / working-dir / search-path order.
  if (allow_suffix_fallback && config_.enable_suffix_fallback) {
    for (const std::string& suffix : SuffixCandidates(asset_path)) {
      if (suffix == asset_path) continue;
      ResolvedAsset alt =
          ResolveInternal(suffix, anchor_path, /*allow_suffix_fallback=*/false);
      if (alt.exists) {
        alt.original_path = asset_path;
        return alt;
      }
    }
  }

  // Not found - return original path normalized
  result.resolved_path = NormalizePath(asset_path);
  result.exists = false;
  return result;
}

bool AssetResolver::FileExists(const std::string& path) const {
  return FileExistsImpl(path);
}

// ============================================================
// Static path utilities
// ============================================================

bool AssetResolver::IsAbsolutePath(const std::string& path) {
  if (path.empty()) return false;

#ifdef _WIN32
  // Windows: C:\ or \\network or /path
  if (path.length() >= 2 && path[1] == ':') return true;
  if (path.length() >= 2 && path[0] == '\\' && path[1] == '\\') return true;
#endif

  return path[0] == '/' || path[0] == '\\';
}

std::string AssetResolver::GetIdentifierScheme(
    const std::string& identifier) {
  const size_t colon = identifier.find(':');
  if (colon == std::string::npos || colon == 0) return {};
  // Do not classify Windows drive paths as URI schemes on any host platform.
  if (colon == 1 && std::isalpha(static_cast<unsigned char>(identifier[0])) &&
      identifier.size() > 2 &&
      (identifier[2] == '/' || identifier[2] == '\\')) {
    return {};
  }
  if (!std::isalpha(static_cast<unsigned char>(identifier[0]))) return {};
  for (size_t i = 1; i < colon; ++i) {
    const unsigned char c = static_cast<unsigned char>(identifier[i]);
    if (!(std::isalnum(c) || c == '+' || c == '-' || c == '.')) return {};
  }
  std::string scheme = identifier.substr(0, colon);
  std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return scheme;
}

bool AssetResolver::IsPackagePath(const std::string& path) {
  // Package paths look like: /path/to/file.usdz[asset.usda]
  size_t bracket = path.find('[');
  if (bracket == std::string::npos || bracket == 0) return false;
  const size_t end = path.find(']', bracket);
  return end != std::string::npos && end == path.size() - 1 &&
         end > bracket + 1;
}

bool AssetResolver::ParsePackagePath(const std::string& path,
                                      std::string* package_file,
                                      std::string* asset_in_package) {
  size_t bracket_start = path.find('[');
  if (bracket_start == std::string::npos) return false;

  size_t bracket_end = path.find(']', bracket_start);
  if (bracket_end == std::string::npos || bracket_end + 1 != path.size() ||
      bracket_start == 0 || bracket_end == bracket_start + 1 ||
      !package_file || !asset_in_package) {
    return false;
  }

  *package_file = path.substr(0, bracket_start);
  *asset_in_package = path.substr(bracket_start + 1, bracket_end - bracket_start - 1);
  return true;
}

std::string AssetResolver::JoinPath(const std::string& base, const std::string& path) {
  if (base.empty()) return path;
  if (path.empty()) return base;

  char last = base.back();
  if (last == '/' || last == '\\') {
    return base + path;
  }
  return base + PATH_SEPARATOR + path;
}

std::string AssetResolver::GetDirectory(const std::string& path) {
  size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) return ".";
  return path.substr(0, pos);
}


std::string AssetResolver::NormalizePath(const std::string& path) {
  if (path.empty()) return path;

  std::string result = path;

  // Normalize separators
  for (char& c : result) {
    if (c == ALT_PATH_SEPARATOR) {
      c = PATH_SEPARATOR;
    }
  }

  // Handle . and ..
  std::vector<std::string> parts;
  size_t start = 0;

  // Preserve leading slashes for absolute paths
  size_t prefix_len = 0;
  if (!result.empty() && result[0] == PATH_SEPARATOR) {
    prefix_len = 1;
#ifdef _WIN32
    if (result.length() >= 2 && result[1] == PATH_SEPARATOR) {
      prefix_len = 2;  // UNC path
    }
#endif
  }
#ifdef _WIN32
  // Handle drive letter
  if (result.length() >= 2 && result[1] == ':') {
    prefix_len = 2;
    if (result.length() >= 3 && result[2] == PATH_SEPARATOR) {
      prefix_len = 3;
    }
  }
#endif

  start = prefix_len;

  while (start < result.length()) {
    size_t end = result.find(PATH_SEPARATOR, start);
    if (end == std::string::npos) {
      end = result.length();
    }

    std::string part = result.substr(start, end - start);

    if (part == "..") {
      if (!parts.empty() && parts.back() != "..") {
        parts.pop_back();
      } else if (prefix_len == 0) {
        parts.push_back(part);
      }
    } else if (part != "." && !part.empty()) {
      parts.push_back(part);
    }

    start = end + 1;
  }

  // Reconstruct path
  std::string normalized = result.substr(0, prefix_len);
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0 || (prefix_len > 0 && normalized.back() != PATH_SEPARATOR)) {
      normalized += PATH_SEPARATOR;
    }
    normalized += parts[i];
  }

  if (normalized.empty()) {
    normalized = ".";
  }

  return normalized;
}

std::vector<std::string> AssetResolver::SuffixCandidates(
    const std::string& asset_path) {
  std::vector<std::string> candidates;
  if (asset_path.empty()) return candidates;

  std::string p = asset_path;
  std::replace(p.begin(), p.end(), '\\', '/');

  // Strip a Windows drive prefix (e.g. "F:")
  if ((p.size() >= 2) &&
      (((p[0] >= 'A') && (p[0] <= 'Z')) || ((p[0] >= 'a') && (p[0] <= 'z'))) &&
      (p[1] == ':')) {
    p = p.substr(2);
  }

  // Strip leading '/', './' and '../' runs.
  size_t pos = 0;
  while (pos < p.size()) {
    if (p[pos] == '/') {
      pos++;
    } else if (p.compare(pos, 2, "./") == 0) {
      pos += 2;
    } else if (p.compare(pos, 3, "../") == 0) {
      pos += 3;
    } else {
      break;
    }
  }
  p = p.substr(pos);

  // Emit progressively shorter suffixes, longest first, down to the basename.
  while (p.size()) {
    if (p != asset_path) {
      candidates.push_back(p);
    }
    size_t slash_loc = p.find('/');
    if (slash_loc == std::string::npos) {
      break;
    }
    p = p.substr(slash_loc + 1);
  }

  return candidates;
}

std::string AssetResolver::MakeRelative(const std::string& path, const std::string& base) {
  std::string norm_path = NormalizePath(path);
  std::string norm_base = NormalizePath(base);

  // Simple implementation - just check if path starts with base
  if (norm_path.find(norm_base) == 0) {
    size_t base_len = norm_base.length();
    if (base_len < norm_path.length()) {
      if (norm_path[base_len] == PATH_SEPARATOR) {
        return norm_path.substr(base_len + 1);
      }
    }
  }

  return path;
}


// ============================================================
// OpenUSD-parity additions
// ============================================================

ResolvedAsset AssetResolver::ResolveForNewAsset(const std::string& asset_path,
                                              const std::string& anchor_path) const {
  ResolvedAsset result;
  result.original_path = asset_path;
  if (asset_path.empty()) return result;

  if (!config_.allow_parent_paths && HasParentComponent(asset_path)) {
    return result;
  }

  const std::string identifier_scheme = GetIdentifierScheme(asset_path);
  if (!identifier_scheme.empty()) {
    // Preserve scheme/custom resolver semantics, without requiring existence.
    return Resolve(asset_path, anchor_path, /*allow_suffix_fallback=*/false);
  }

  if (IsPackagePath(asset_path)) {
    std::string package_file;
    std::string package_entry;
    if (!ParsePackagePath(asset_path, &package_file, &package_entry)) {
      return result;
    }
    if (!config_.allow_absolute_paths && IsAbsolutePath(package_file)) {
      return result;
    }
    result.is_package = true;
    result.asset_in_package = NormalizePackageEntryPath(package_entry);
    if (result.asset_in_package.empty()) return result;
    const ResolvedAsset package = ResolveForNewAsset(package_file, anchor_path);
    result.package_path = package.resolved_path.empty()
                              ? NormalizePath(package_file)
                              : package.resolved_path;
    result.resolved_path =
        result.package_path + "[" + result.asset_in_package + "]";
    result.exists = false;
    return result;
  }

  if (IsAbsolutePath(asset_path)) {
    if (!config_.allow_absolute_paths) return result;
    result.resolved_path = NormalizePath(asset_path);
    result.exists = FileExists(result.resolved_path);
    return result;
  }

  if (!anchor_path.empty() && IsPackagePath(anchor_path)) {
    std::string anchor_package;
    std::string anchor_entry;
    if (ParsePackagePath(anchor_path, &anchor_package, &anchor_entry)) {
      const std::string entry_dir = GetDirectory(anchor_entry);
      std::string entry = (entry_dir == "." || entry_dir.empty())
                              ? asset_path
                              : JoinPath(entry_dir, asset_path);
      entry = NormalizePackageEntryPath(entry);
      if (entry.empty()) return result;
      ResolvedAsset package;
      if (!config_.allow_absolute_paths && IsAbsolutePath(anchor_package)) {
        // The package portion of the anchor is already resolved input, not a
        // newly-authored absolute path.
        package.resolved_path = NormalizePath(anchor_package);
        package.exists = FileExists(package.resolved_path);
      } else {
        package =
            ResolveInternal(anchor_package, "", /*allow_suffix_fallback=*/false);
      }
      result.is_package = true;
      result.package_path = package.exists ? package.resolved_path
                                           : NormalizePath(anchor_package);
      result.asset_in_package = entry;
      result.resolved_path = result.package_path + "[" + entry + "]";
      result.exists = false;
      return result;
    }
  }

  const std::string base = anchor_path.empty()
                               ? config_.working_directory
                               : GetDirectory(anchor_path);
  result.resolved_path =
      base.empty() ? NormalizePath(asset_path)
                   : NormalizePath(JoinPath(base, asset_path));
  result.exists = FileExists(result.resolved_path);
  return result;
}

ResolverContext AssetResolver::CreateDefaultContext() const {
  return ResolverContext{};
}

ResolverContext AssetResolver::CreateContextFromString(
    const std::string& context_str) const {
  ResolverContext ctx;
  ctx.context_string = context_str;
  return NormalizeContext(std::move(ctx));
}

void AssetResolver::BindContext(const ResolverContext& context,
                                void** binding_data) {
  if (binding_data) *binding_data = nullptr;
  std::unique_ptr<ContextBindingNode> node(
      new (std::nothrow) ContextBindingNode());
  if (!node) return;
  node->identity = identity_;
  node->context = NormalizeContext(context);
  ContextBindingNode* raw = node.get();
  g_context_bindings.push_back(std::move(node));
  if (binding_data) *binding_data = raw;
}

void AssetResolver::UnbindContext(const ResolverContext& context,
                                  void** binding_data) {
  (void)context;
  if (binding_data && *binding_data) {
    EraseContextBinding(*binding_data);
    *binding_data = nullptr;
    return;
  }
  for (size_t i = g_context_bindings.size(); i > 0; --i) {
    if (g_context_bindings[i - 1] &&
        g_context_bindings[i - 1]->identity == identity_) {
      g_context_bindings.erase(g_context_bindings.begin() +
                               static_cast<std::ptrdiff_t>(i - 1));
      return;
    }
  }
}

ResolverContext AssetResolver::GetCurrentContext() const {
  if (const ContextBindingNode* active = FindActiveContextBinding(identity_)) {
    return active->context;
  }
  return config_.context;
}

bool AssetResolver::IsContextDependentPath(const std::string& asset_path) const {
  // A path is context-dependent if it contains scheme-like or relative
  // components that may resolve differently under different contexts.
  return GetIdentifierScheme(asset_path).empty() && !IsAbsolutePath(asset_path);
}

AssetInfo AssetResolver::GetAssetInfo(const std::string& asset_path,
                                     const ResolvedAsset& resolved_path) const {
  AssetInfo info;
  info.resolved_path = resolved_path.resolved_path;
  info.exists = resolved_path.exists;

  std::string name_source;
  std::string repository_source;
  if (resolved_path.is_package) {
    name_source = resolved_path.asset_in_package;
    repository_source = resolved_path.package_path;
  }

  // Fall back to parsing the resolved/asset identifier when the caller did not
  // populate the package fields (or the asset is unresolved).
  const std::string& identifier = !resolved_path.resolved_path.empty()
                                      ? resolved_path.resolved_path
                                      : asset_path;
  if (IsPackagePath(identifier)) {
    std::string package_file;
    std::string package_entry;
    if (ParsePackagePath(identifier, &package_file, &package_entry)) {
      if (name_source.empty()) name_source = package_entry;
      if (repository_source.empty()) repository_source = package_file;
    }
  }
  if (!resolved_path.is_package) {
    if (name_source.empty()) {
      name_source = resolved_path.resolved_path.empty()
                        ? asset_path
                        : resolved_path.resolved_path;
    }
    if (repository_source.empty()) repository_source = name_source;
  }

  const size_t slash = name_source.find_last_of("/\\");
  info.asset_name =
      (slash == std::string::npos) ? name_source : name_source.substr(slash + 1);
  info.repository_path =
      repository_source.empty() ? std::string() : GetDirectory(repository_source);
  return info;
}

AssetTimestamp AssetResolver::GetModificationTimestamp(
    const std::string& asset_path, const ResolvedAsset& resolved_path) const {
  AssetTimestamp ts;
#if defined(__EMSCRIPTEN__)
  (void)asset_path;
  (void)resolved_path;
  return ts;
#else
  std::string path;
  if (resolved_path.is_package && !resolved_path.package_path.empty()) {
    path = resolved_path.package_path;
  } else {
    path = resolved_path.resolved_path.empty() ? asset_path
                                               : resolved_path.resolved_path;
  }
  if (path.empty()) return ts;

  struct stat st;
  if (stat(path.c_str(), &st) != 0 || st.st_mtime < 0) return ts;
  ts.seconds = static_cast<uint64_t>(st.st_mtime);
#if defined(__APPLE__)
  ts.nanoseconds = static_cast<uint64_t>(st.st_mtimespec.tv_nsec);
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__)
  ts.nanoseconds = static_cast<uint64_t>(st.st_mtim.tv_nsec);
#endif
  ts.valid = true;
  return ts;
#endif
}

std::shared_ptr<std::vector<uint8_t>> AssetResolver::OpenAsset(
    const ResolvedAsset& resolved_path) const {
  auto out = std::make_shared<std::vector<uint8_t>>();
  std::string err;
  if (ReadAsset(resolved_path.resolved_path, out.get(), &err)) {
    return out;
  }
  return nullptr;
}

void AssetResolver::BeginCacheScope(void** cache_scope_data) {
  if (cache_scope_data) *cache_scope_data = nullptr;
  std::shared_ptr<CacheScopeNode> node(
      new (std::nothrow) CacheScopeNode());
  if (!node) return;
  node->identity = identity_;
  CacheScopeNode* raw = node.get();
  g_cache_scopes.push_back(std::move(node));
  if (cache_scope_data) *cache_scope_data = raw;
}

void AssetResolver::EndCacheScope(void** cache_scope_data) {
  if (cache_scope_data && *cache_scope_data) {
    EraseCacheScope(*cache_scope_data);
    *cache_scope_data = nullptr;
    return;
  }
  for (size_t i = g_cache_scopes.size(); i > 0; --i) {
    if (g_cache_scopes[i - 1] &&
        g_cache_scopes[i - 1]->identity == identity_) {
      g_cache_scopes.erase(g_cache_scopes.begin() +
                           static_cast<std::ptrdiff_t>(i - 1));
      return;
    }
  }
}

ResolverScopedCache::ResolverScopedCache(AssetResolver* resolver) : resolver_(resolver) {
  if (resolver_) resolver_->BeginCacheScope(&data_);
}

ResolverScopedCache::~ResolverScopedCache() {
  // data_ stays null when the scope could not be allocated; do not let a
  // failed Begin pop an unrelated outer scope.
  if (resolver_ && data_) resolver_->EndCacheScope(&data_);
}

void AssetResolver::RegisterPackageResolver(const std::string& extension,
                                          PackageResolverCallback callback) {
  const std::string normalized = NormalizePackageExtension(extension);
  if (normalized.empty() || !callback) return;
  std::lock_guard<std::mutex> lock(registry_mutex_);
  package_resolvers_[normalized] = std::move(callback);
}

bool AssetResolver::UnregisterPackageResolver(const std::string& extension) {
  const std::string normalized = NormalizePackageExtension(extension);
  if (normalized.empty()) return false;
  std::lock_guard<std::mutex> lock(registry_mutex_);
  return package_resolvers_.erase(normalized) > 0;
}


// ============================================================
// Global resolver
// ============================================================

AssetResolver& GetDefaultResolver() {
  if (!g_default_resolver) {
    g_default_resolver = std::make_unique<AssetResolver>();
  }
  return *g_default_resolver;
}

void SetDefaultResolver(std::unique_ptr<AssetResolver> resolver) {
  g_default_resolver = std::move(resolver);
}

}  // namespace next
}  // namespace lightusd
