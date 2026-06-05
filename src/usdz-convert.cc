// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment, Inc.

#include "usdz-convert.hh"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <map>
#include <set>
#include <utility>

#include "tinyusdz.hh"
#include "composition.hh"
#include "asset-resolution.hh"
#include "usda-writer.hh"
#include "usdc-writer.hh"
#include "usdShade.hh"
#include "core/animatable.hh"
#include "image-loader.hh"
#include "image-writer.hh"
#include "io-util.hh"
#include "pprinter.hh"
#include "str-util.hh"
#include "tydra/texture-util.hh"

// Emscripten's <stdio.h> defines `stdout` as a self-referential macro
// (`#define stdout (stdout)`) so the same identifier works both as a
// macro and as a real variable. The recursion trips clang's
// -Wdisabled-macro-expansion diagnostic at every call site that names
// `stdout` (e.g. `std::fflush(stdout)`). Undefining the macro here makes
// the bare token refer to the underlying `extern FILE *const stdout`
// variable; the FILE* value is identical. This is a no-op on platforms
// where `stdout` is not a macro (e.g. glibc, where it's a plain extern).
#ifdef __EMSCRIPTEN__
# undef stdout
#endif

namespace tinyusdz {
namespace usdz {

namespace {

void Log(bool verbose, const std::string &msg) {
  if (verbose) {
    std::printf("[tusdzconvert] %s\n", msg.c_str());
  }
}

// Coarse phase timer for progress reporting / profiling. Call begin("name") at
// the start of each phase; the elapsed time of the previous phase is printed
// when the next phase begins (and on end()). Active only when verbose.
class PhaseTimer {
 public:
  explicit PhaseTimer(bool verbose) : verbose_(verbose) {}
  void begin(const std::string &name) {
    flush();
    cur_ = name;
    start_ = std::chrono::steady_clock::now();
    active_ = true;
    if (verbose_) {
      std::printf("[tusdzconvert][phase] %-22s ...\n", name.c_str());
      std::fflush(stdout);
    }
  }
  void end() { flush(); }

 private:
  void flush() {
    if (active_ && verbose_) {
      double ms = std::chrono::duration<double, std::milli>(
                      std::chrono::steady_clock::now() - start_)
                      .count();
      total_ms_ += ms;
      std::printf("[tusdzconvert][phase] %-22s %9.1f ms  (total %9.1f ms)\n",
                  cur_.c_str(), ms, total_ms_);
      std::fflush(stdout);
    }
    active_ = false;
  }
  bool verbose_;
  bool active_ = false;
  double total_ms_ = 0.0;
  std::string cur_;
  std::chrono::steady_clock::time_point start_;
};

// Compute a relative path from `base_dir` to `target`.
// Both paths should be normalized (forward slashes, no trailing slash).
// Returns `target` unchanged when paths cannot be relativized (e.g. different
// roots on Windows).
std::string ToRelativePath(const std::string &base_dir,
                           const std::string &target) {
  // Normalize backslashes.
  std::string b = base_dir;
  std::string t = target;
  std::replace(b.begin(), b.end(), '\\', '/');
  std::replace(t.begin(), t.end(), '\\', '/');
  // Strip trailing slashes.
  while (!b.empty() && b.back() == '/') b.pop_back();
  while (!t.empty() && t.back() == '/') t.pop_back();
  // If either is empty or not absolute, return target as-is.
  if (b.empty() || t.empty() || b[0] != '/' || t[0] != '/') {
    return target;
  }
  // Split into components.
  auto split = [](const std::string &p) -> std::vector<std::string> {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start < p.size()) {
      size_t slash = p.find('/', start);
      if (slash == std::string::npos) {
        parts.push_back(p.substr(start));
        break;
      }
      if (slash > start) {
        parts.push_back(p.substr(start, slash - start));
      }
      start = slash + 1;
    }
    return parts;
  };
  std::vector<std::string> bparts = split(b);
  std::vector<std::string> tparts = split(t);
  // Find common prefix length.
  size_t common = 0;
  while (common < bparts.size() && common < tparts.size() &&
         bparts[common] == tparts[common]) {
    common++;
  }
  // Build relative path.
  std::string rel;
  for (size_t i = common; i < bparts.size(); i++) {
    rel += "../";
  }
  for (size_t i = common; i < tparts.size(); i++) {
    if (i > common) rel += "/";
    rel += tparts[i];
  }
  if (rel.empty()) {
    rel = ".";
  }
  return rel;
}

bool IsAllowedTextureExt(const std::string &ext_lower) {
  return ext_lower == "png" || ext_lower == "jpg" || ext_lower == "jpeg" ||
         ext_lower == "exr";
}

bool IsAllowedARKitTextureExt(const std::string &ext_lower) {
  return ext_lower == "png" || ext_lower == "jpg" || ext_lower == "jpeg" ||
         ext_lower == "exr";
}

bool IsAllowedARKitPrimType(const std::string &type_name) {
  static const std::set<std::string> allowed = {
      "", "Scope", "Xform", "Camera", "Shader", "Material", "Mesh",
      "Sphere", "Cube", "Cylinder", "Cone", "Capsule", "GeomSubset",
      "Points", "SkelRoot", "Skeleton", "SkelAnimation", "BlendShape",
      "SpatialAudio", "PhysicsScene", "Preliminary_ReferenceImage",
      "Preliminary_Text", "Preliminary_Trigger"};
  return allowed.count(type_name) || tinyusdz::startsWith(type_name, "RealityKit");
}

bool IsAllowedARKitShaderId(const std::string &shader_id) {
  return shader_id == "UsdPreviewSurface" || shader_id == "UsdUVTexture" ||
         shader_id == "UsdTransform2d" ||
         tinyusdz::startsWith(shader_id, "UsdPrimvarReader") ||
         tinyusdz::startsWith(shader_id, "ND_");
}

// Turn an arbitrary asset path into a safe, relative USDZ archive name.
// Absolute paths and paths escaping the archive root are reduced to a
// "textures/<basename>" form.
bool IsUnsafeUnresolvedTexturePath(const std::string &assetPath) {
  std::string p = assetPath;
  std::replace(p.begin(), p.end(), '\\', '/');
  // Reject null bytes (could corrupt ZIP entry names).
  if (p.find('\0') != std::string::npos) return true;
  return p.empty() || (p[0] == '/') || (p.find(':') != std::string::npos) ||
         (p == "..") || tinyusdz::startsWith(p, "../") ||
         (p.find("/../") != std::string::npos) ||
         tinyusdz::endsWith(p, "/..");
}

std::string RelativeToSearchPath(const std::string &asset_path,
                                 const std::vector<std::string> &search_paths) {
  std::string p = asset_path;
  std::replace(p.begin(), p.end(), '\\', '/');

  for (std::string base : search_paths) {
    if (base.empty()) {
      continue;
    }
    std::replace(base.begin(), base.end(), '\\', '/');
    while (!base.empty() && base.back() == '/') {
      base.pop_back();
    }

    if (base.empty()) {
      continue;
    }

    if (io::IsAbsPath(p)) {
      continue;
    }

    if (p == base) {
      return io::GetBaseFilename(p);
    }
    if (tinyusdz::startsWith(p, base + "/")) {
      return p.substr(base.size() + 1);
    }
  }

  return asset_path;
}

std::string SafeMissingArchiveReference(
    const std::string &asset_path,
    const std::vector<std::string> &search_paths) {
  const std::string rel = RelativeToSearchPath(asset_path, search_paths);
  if (rel != asset_path) {
    return IsUnsafeUnresolvedTexturePath(rel) ? std::string() : rel;
  }

  if (!io::IsAbsPath(asset_path)) {
    return IsUnsafeUnresolvedTexturePath(asset_path) ? std::string()
                                                    : asset_path;
  }

  for (const std::string &base : search_paths) {
    if (base.empty() || !io::IsAbsPath(base)) {
      continue;
    }
    const std::string base_rel = ToRelativePath(base, asset_path);
    if (base_rel != asset_path && !IsUnsafeUnresolvedTexturePath(base_rel)) {
      return base_rel;
    }
  }

  return std::string();
}

// Maximum ZIP entry name length (ZIP spec allows 64 KB, but we cap much lower
// to prevent pathological entry names from causing issues).
constexpr size_t kMaxArchiveNameLen = 256;

bool IsSafeArchiveSegment(const std::string &segment) {
  return !segment.empty() && segment != "." && segment != ".." &&
         segment.find('\0') == std::string::npos &&
         segment.find('/') == std::string::npos &&
         segment.find('\\') == std::string::npos &&
         segment.find(':') == std::string::npos &&
         segment.find("..") == std::string::npos;
}

std::string SanitizeArchiveName(const std::string &assetPath) {
  std::string p = assetPath;
  // Normalize backslashes.
  std::replace(p.begin(), p.end(), '\\', '/');

  const bool needs_sanitize = IsUnsafeUnresolvedTexturePath(p) ||
                              tinyusdz::startsWith(p, "./");

  std::string name = p;
  if (needs_sanitize) {
    std::string base = io::GetBaseFilename(p);
    if (!IsSafeArchiveSegment(base)) {
      base = "texture";
    }
    name = "textures/" + base;
  }
  // Reject paths that still contain unsafe archive syntax after sanitization.
  if (name.find('\0') != std::string::npos ||
      name.find('\\') != std::string::npos ||
      name.find(':') != std::string::npos ||
      name.find("..") != std::string::npos ||
      name.find("//") != std::string::npos ||
      tinyusdz::startsWith(name, "/") ||
      tinyusdz::startsWith(name, "./") ||
      tinyusdz::startsWith(name, "../") ||
      tinyusdz::endsWith(name, "/")) {
    name = "textures/texture";
  }
  // Enforce maximum archive name length.
  if (name.size() > kMaxArchiveNameLen) {
    name = "textures/texture";
  }
  return name;
}

std::string SanitizeLayerArchiveName(const std::string &assetPath) {
  std::string p = assetPath;
  std::replace(p.begin(), p.end(), '\\', '/');

  const bool needs_sanitize = IsUnsafeUnresolvedTexturePath(p) ||
                              tinyusdz::startsWith(p, "./");

  std::string name = p;
  if (needs_sanitize) {
    std::string base = io::GetBaseFilename(p);
    if (!IsSafeArchiveSegment(base)) {
      base = "layer";
    }
    name = "layers/" + base;
  }

  if (name.find('\0') != std::string::npos ||
      name.find('\\') != std::string::npos ||
      name.find(':') != std::string::npos ||
      name.find("..") != std::string::npos ||
      name.find("//") != std::string::npos ||
      tinyusdz::startsWith(name, "/") ||
      tinyusdz::startsWith(name, "./") ||
      tinyusdz::startsWith(name, "../") ||
      tinyusdz::endsWith(name, "/")) {
    name = "layers/layer";
  }
  if (name.size() > kMaxArchiveNameLen) {
    name = "layers/layer";
  }
  return name;
}

std::string MakeCollisionArchiveName(const std::string &archive_name,
                                     uint32_t suffix) {
  const std::string ext = io::GetFileExtension(archive_name);
  const size_t base_len = (!ext.empty() && archive_name.size() > ext.size() + 1)
      ? archive_name.size() - ext.size() - 1
      : archive_name.size();
  std::string base = archive_name.substr(0, base_len);
  const std::string suffix_part =
      "_" + std::to_string(suffix) + (ext.empty() ? std::string() : "." + ext);
  if (base.size() + suffix_part.size() > kMaxArchiveNameLen) {
    if (suffix_part.size() >= kMaxArchiveNameLen) {
      return "textures/texture";
    }
    base.resize(kMaxArchiveNameLen - suffix_part.size());
    if (base.empty() || tinyusdz::endsWith(base, "/")) {
      base += "texture";
      if (base.size() + suffix_part.size() > kMaxArchiveNameLen) {
        base.resize(kMaxArchiveNameLen - suffix_part.size());
      }
    }
  }
  return base + suffix_part;
}

std::string ReplaceExtension(const std::string &name, const std::string &newExtNoDot) {
  std::string ext = io::GetFileExtension(name);
  if (ext.empty()) {
    return name + "." + newExtNoDot;
  }
  return name.substr(0, name.size() - ext.size()) + newExtNoDot;
}

// Read the (default, scalar) asset path string from a UsdUVTexture.file attr.
// Path length is capped to prevent excessive memory usage downstream.
bool ReadTextureFilePath(const UsdUVTexture &tex, std::string *out) {
  const auto av = tex.file.get_value();  // optional<Animatable<AssetPath>>
  if (!av) {
    return false;
  }
  if (!av.value().is_scalar()) {
    return false;
  }
  value::AssetPath ap;
  if (!av.value().get_scalar(&ap)) {
    return false;
  }
  std::string path = ap.GetAssetPath();
  constexpr size_t kMaxPathLength = 4096;
  if (path.size() > kMaxPathLength) {
    return false;
  }
  (*out) = path;
  return !out->empty();
}

void WriteTextureFilePath(UsdUVTexture *tex, const std::string &path) {
  value::AssetPath ap(path);
  Animatable<value::AssetPath> anim(ap);
  tex->file.set_value(anim);
}

// Collect mutable UsdUVTexture pointers (paired with their current file path)
// by recursively walking the stage's Prim tree.
// Guards against excessive texture counts to prevent memory exhaustion.
void CollectTextures(Prim &prim,
                     std::vector<std::pair<UsdUVTexture *, std::string>> *out,
                     int depth = 0) {
  if (depth > 512) return;  // guard against stack overflow
  constexpr size_t kMaxTextureCount = 10000;
  if (out->size() >= kMaxTextureCount) return;  // guard against memory exhaustion
  Shader *shd = prim.get_data().as<Shader>();
  if (shd && shd->info_id == "UsdUVTexture") {
    UsdUVTexture *tex = shd->value.as<UsdUVTexture>();
    if (tex) {
      std::string path;
      if (ReadTextureFilePath(*tex, &path)) {
        out->emplace_back(tex, path);
      }
    }
  }
  for (auto &child : prim.children()) {
    CollectTextures(child, out, depth + 1);
  }
}

void CollectLayerTextureAssetPaths(const PrimSpec &primspec,
                                   std::vector<std::string> *out,
                                   int depth = 0) {
  if (depth > 512) return;
  constexpr size_t kMaxTextureCount = 10000;
  if (out->size() >= kMaxTextureCount) return;

  for (const auto &item : primspec.props()) {
    const Attribute *attr = item.second.get_attribute_or_null();
    if (!attr || !attr->has_value()) {
      continue;
    }

    value::AssetPath asset_path;
    if (!attr->get_value(&asset_path)) {
      continue;
    }

    const std::string path = asset_path.GetAssetPath();
    const std::string ext = to_lower(io::GetFileExtension(path));
    if (!path.empty() && IsAllowedTextureExt(ext)) {
      out->push_back(path);
      if (out->size() >= kMaxTextureCount) return;
    }
  }

  for (const PrimSpec &child : primspec.children()) {
    CollectLayerTextureAssetPaths(child, out, depth + 1);
    if (out->size() >= kMaxTextureCount) return;
  }
}

void CollectLayerTextureAssetPaths(const Layer &layer,
                                   std::vector<std::string> *out) {
  for (const auto &item : layer.primspecs()) {
    CollectLayerTextureAssetPaths(item.second, out);
  }
}

void CollectARKitCompatibilityWarnings(const PrimSpec &primspec,
                                       std::string *warn,
                                       int depth = 0) {
  if (depth > 512) return;

  if (!IsAllowedARKitPrimType(primspec.typeName()) && warn) {
    *warn += "ARKit compatibility: prim '" + primspec.name() +
             "' has unsupported type '" + primspec.typeName() + "'.\n";
  }

  if (primspec.typeName() == "Shader") {
    const auto prop_it = primspec.props().find("info:id");
    if (prop_it != primspec.props().end()) {
      const Attribute *attr = prop_it->second.get_attribute_or_null();
      value::token shader_id;
      if (attr && attr->get_value(&shader_id) &&
          !IsAllowedARKitShaderId(shader_id.str()) && warn) {
        *warn += "ARKit compatibility: shader '" + primspec.name() +
                 "' has unsupported info:id '" + shader_id.str() + "'.\n";
      }
    }
  }

  for (const auto &variant_set : primspec.variantSets()) {
    for (const auto &variant : variant_set.second.variantSet) {
      CollectARKitCompatibilityWarnings(variant.second, warn, depth + 1);
    }
  }
  for (const PrimSpec &child : primspec.children()) {
    CollectARKitCompatibilityWarnings(child, warn, depth + 1);
  }
}

void CollectARKitCompatibilityWarnings(const Layer &layer, std::string *warn) {
  for (const auto &item : layer.primspecs()) {
    CollectARKitCompatibilityWarnings(item.second, warn);
  }
}

void CollectCompositionAssetPaths(const PrimSpec &primspec,
                                  std::vector<std::string> *out,
                                  int depth = 0) {
  if (depth > 512) return;
  constexpr size_t kMaxLayerDependencyCount = 10000;
  if (out->size() >= kMaxLayerDependencyCount) return;

  const PrimMeta &metas = primspec.metas();
  if (metas.references) {
    for (const auto &op : metas.references.value()) {
      for (const Reference &ref : op.second) {
        const std::string path = ref.asset_path.GetAssetPath();
        if (!path.empty()) {
          out->push_back(path);
          if (out->size() >= kMaxLayerDependencyCount) return;
        }
      }
    }
  }
  if (metas.payload) {
    for (const auto &op : metas.payload.value()) {
      for (const Payload &payload : op.second) {
        const std::string path = payload.asset_path.GetAssetPath();
        if (!path.empty()) {
          out->push_back(path);
          if (out->size() >= kMaxLayerDependencyCount) return;
        }
      }
    }
  }

  for (const auto &variant_set : primspec.variantSets()) {
    for (const auto &variant : variant_set.second.variantSet) {
      CollectCompositionAssetPaths(variant.second, out, depth + 1);
      if (out->size() >= kMaxLayerDependencyCount) return;
    }
  }

  for (const PrimSpec &child : primspec.children()) {
    CollectCompositionAssetPaths(child, out, depth + 1);
    if (out->size() >= kMaxLayerDependencyCount) return;
  }
}

void CollectCompositionAssetPaths(const Layer &layer,
                                  std::vector<std::string> *out) {
  constexpr size_t kMaxLayerDependencyCount = 10000;
  for (const SubLayer &sublayer : layer.metas().subLayers) {
    const std::string path = sublayer.assetPath.GetAssetPath();
    if (!path.empty()) {
      out->push_back(path);
      if (out->size() >= kMaxLayerDependencyCount) return;
    }
  }
  for (const auto &item : layer.primspecs()) {
    CollectCompositionAssetPaths(item.second, out);
    if (out->size() >= kMaxLayerDependencyCount) return;
  }
}

size_t RemapCompositionAssetPaths(
    PrimSpec &primspec, const std::map<std::string, std::string> &remap,
    int depth = 0) {
  if (depth > 512) return 0;

  size_t n = 0;
  PrimMeta &metas = primspec.metas();
  if (metas.references) {
    for (auto &op : metas.references.value()) {
      for (Reference &ref : op.second) {
        const auto it = remap.find(ref.asset_path.GetAssetPath());
        if (it != remap.end() && it->second != ref.asset_path.GetAssetPath()) {
          ref.asset_path = value::AssetPath(it->second);
          n++;
        }
      }
    }
  }
  if (metas.payload) {
    for (auto &op : metas.payload.value()) {
      for (Payload &payload : op.second) {
        const auto it = remap.find(payload.asset_path.GetAssetPath());
        if (it != remap.end() &&
            it->second != payload.asset_path.GetAssetPath()) {
          payload.asset_path = value::AssetPath(it->second);
          n++;
        }
      }
    }
  }

  for (auto &variant_set : primspec.variantSets()) {
    for (auto &variant : variant_set.second.variantSet) {
      n += RemapCompositionAssetPaths(variant.second, remap, depth + 1);
    }
  }

  for (PrimSpec &child : primspec.children()) {
    n += RemapCompositionAssetPaths(child, remap, depth + 1);
  }
  return n;
}

size_t RemapCompositionAssetPaths(
    Layer &layer, const std::map<std::string, std::string> &remap) {
  size_t n = 0;
  for (SubLayer &sublayer : layer.metas().subLayers) {
    const auto it = remap.find(sublayer.assetPath.GetAssetPath());
    if (it != remap.end() && it->second != sublayer.assetPath.GetAssetPath()) {
      sublayer.assetPath = value::AssetPath(it->second);
      n++;
    }
  }
  for (auto &item : layer.primspecs()) {
    n += RemapCompositionAssetPaths(item.second, remap);
  }
  return n;
}

size_t RemapLayerAssetPaths(PrimSpec &primspec,
                            const std::map<std::string, std::string> &remap,
                            int depth = 0) {
  if (depth > 512) return 0;

  size_t n = 0;
  for (auto &item : primspec.props()) {
    Attribute *attr = item.second.get_attribute_or_null();
    if (!attr || !attr->has_value()) {
      continue;
    }

    value::AssetPath asset_path;
    if (!attr->get_value(&asset_path)) {
      continue;
    }

    const auto it = remap.find(asset_path.GetAssetPath());
    if (it == remap.end() || it->second == asset_path.GetAssetPath()) {
      continue;
    }

    attr->set_value(value::AssetPath(it->second));
    n++;
  }

  for (PrimSpec &child : primspec.children()) {
    n += RemapLayerAssetPaths(child, remap, depth + 1);
  }
  return n;
}

size_t RemapLayerAssetPaths(Layer &layer,
                            const std::map<std::string, std::string> &remap) {
  size_t n = 0;
  for (auto &item : layer.primspecs()) {
    n += RemapLayerAssetPaths(item.second, remap);
  }
  return n;
}

bool ComposeLayerToFixedPoint(AssetResolutionResolver &resolver,
                              const Layer &root_layer,
                              Layer *composited_layer,
                              std::string *warn,
                              std::string *err) {
  if (!composited_layer) {
    if (err) {
      (*err) = "composited_layer is null.";
    }
    return false;
  }

  Layer src_layer = root_layer;

  if (!src_layer.metas().subLayers.empty()) {
    Layer tmp;
    if (!tinyusdz::CompositeSublayers(resolver, src_layer, &tmp, warn, err)) {
      return false;
    }
    src_layer = std::move(tmp);
  }

  constexpr int kMaxIteration = 64;
  for (int i = 0; i < kMaxIteration; i++) {
    bool has_unresolved = false;

    if (src_layer.check_unresolved_references()) {
      has_unresolved = true;
      Layer tmp;
      if (!tinyusdz::CompositeReferences(resolver, src_layer, &tmp, warn, err)) {
        return false;
      }
      src_layer = std::move(tmp);
    }

    if (src_layer.check_unresolved_payload()) {
      has_unresolved = true;
      Layer tmp;
      if (!tinyusdz::CompositePayload(resolver, src_layer, &tmp, warn, err)) {
        return false;
      }
      src_layer = std::move(tmp);
    }

    if (src_layer.check_unresolved_inherits()) {
      has_unresolved = true;
      Layer tmp;
      if (!tinyusdz::CompositeInherits(src_layer, &tmp, warn, err)) {
        return false;
      }
      src_layer = std::move(tmp);
    }

    if (src_layer.check_unresolved_variant()) {
      has_unresolved = true;
      Layer tmp;
      if (!tinyusdz::CompositeVariant(src_layer, &tmp, warn, err)) {
        return false;
      }
      src_layer = std::move(tmp);
    }

    if (!has_unresolved) {
      *composited_layer = std::move(src_layer);
      return true;
    }
  }

  if (err) {
    (*err) += "Composition did not converge before the iteration limit.";
  }
  return false;
}

bool ReadAssetBytes(AssetResolutionResolver &resolver, const std::string &assetPath,
                    std::vector<uint8_t> *out, std::string *warn,
                    std::string *err) {
  const std::string resolved = resolver.resolve(assetPath);
  if (resolved.empty()) {
    // The resolver may not handle absolute filesystem paths.  Fall back to
    // reading the path directly if it is already a filesystem path.
    std::string ioerr;
    if (io::ReadWholeFile(out, &ioerr, assetPath)) {
      return true;
    }
    if (err) (*err) = "Asset not found: " + assetPath;
    return false;
  }
  Asset asset;
  std::string owarn;
  if (!resolver.open_asset(resolved, assetPath, &asset, &owarn, err)) {
    return false;
  }
  if (warn && !owarn.empty()) {
    (*warn) += owarn;
  }
  // Cap asset size to avoid unbounded allocation from malicious assets.
  constexpr size_t kMaxAssetBytes = size_t(256) * 1024 * 1024;  // 256 MiB
  if (asset.size() > kMaxAssetBytes) {
    if (err) {
      (*err) = "Asset size exceeds 256 MiB limit.";
    }
    return false;
  }
  out->assign(asset.data(), asset.data() + asset.size());
  return true;
}

bool ReadAssetBytesWithBase(AssetResolutionResolver &resolver,
                            const std::string &assetPath,
                            const std::string &base_dir,
                            std::vector<uint8_t> *out, std::string *warn,
                            std::string *err) {
  std::string local_err;
  if (ReadAssetBytes(resolver, assetPath, out, warn, &local_err)) {
    return true;
  }

  if (!base_dir.empty() && !io::IsAbsPath(assetPath)) {
    const std::string candidate = io::JoinPath(base_dir, assetPath);
    std::string ioerr;
    if (io::ReadWholeFile(out, &ioerr, candidate)) {
      return true;
    }
  }

  if (err) {
    *err = local_err.empty() ? ("Asset not found: " + assetPath) : local_err;
  }
  return false;
}

// Process one texture's bytes: optionally decode -> resize -> re-encode.
// On success fills `out_bytes` and `out_ext` (lowercase, no dot) and sets the
// resize/reencode flags. Falls back to passthrough on decode failure.
bool ProcessTexture(const std::vector<uint8_t> &src_bytes,
                    const std::string &src_ext_lower,
                    const UsdzConvertOptions &opts, const std::string &uri,
                    std::vector<uint8_t> *out_bytes, std::string *out_ext,
                    bool *resized, bool *reencoded, std::string *warn) {
  *resized = false;
  *reencoded = false;

  // Decide the target extension.
  std::string target_ext = src_ext_lower;
  if (opts.texture_format == OutputTextureFormat::PNG) {
    target_ext = "png";
  } else if (opts.texture_format == OutputTextureFormat::JPEG) {
    target_ext = "jpg";
  }
  if (opts.arkit_compatible && !IsAllowedARKitTextureExt(target_ext)) {
    target_ext = "png";
  }
  const bool transcode = (target_ext != src_ext_lower) &&
                         !(target_ext == "jpg" && src_ext_lower == "jpeg");

  const bool need_resize = opts.max_texture_size > 0;
  const bool want_process =
      need_resize || transcode || (opts.reencode && (target_ext == "png" ||
                                                     target_ext == "jpg" ||
                                                     target_ext == "jpeg"));

  if (!want_process) {
    *out_bytes = src_bytes;
    *out_ext = src_ext_lower;
    return true;
  }

  // Decode.
  auto decoded = image::LoadImageFromMemory(src_bytes.data(), src_bytes.size(), uri);
  if (!decoded) {
    if (warn) {
      (*warn) += "Could not decode texture (" + uri + "): " + decoded.error() +
                 " - copying through unchanged.\n";
    }
    *out_bytes = src_bytes;
    *out_ext = src_ext_lower;
    return true;
  }

  Image img = std::move(decoded.value().image);

  // EXR / non-8bit: don't attempt resize/transcode in v1; pass through.
  if (img.bpp != 8) {
    *out_bytes = src_bytes;
    *out_ext = src_ext_lower;
    return true;
  }

  // Validate decoded image dimensions (guard against corrupt/malicious headers).
  const int kMaxDimension = 32768;
  if (img.width <= 0 || img.height <= 0 || img.channels < 1 || img.channels > 4) {
    if (warn) {
      (*warn) += "Invalid image dimensions for " + uri +
                 " - copying through unchanged.\n";
    }
    *out_bytes = src_bytes;
    *out_ext = src_ext_lower;
    return true;
  }
  if (img.width > kMaxDimension || img.height > kMaxDimension) {
    if (warn) {
      (*warn) += "Image " + uri + " exceeds max dimension (" +
                 std::to_string(img.width) + "x" + std::to_string(img.height) +
                 ") - copying through unchanged.\n";
    }
    *out_bytes = src_bytes;
    *out_ext = src_ext_lower;
    return true;
  }

  // Resize (cap longest edge).
  if (need_resize) {
    const int longest = (std::max)(img.width, img.height);
    if (longest > opts.max_texture_size) {
      const double scale = double(opts.max_texture_size) / double(longest);
      int nw = (std::max)(1, int(img.width * scale + 0.5));
      int nh = (std::max)(1, int(img.height * scale + 0.5));
      Image resized_img;
      std::string rerr;
      if (tydra::ResizeImage(img, nw, nh, &resized_img, tydra::ResizeFilter::Auto,
                             &rerr)) {
        img = std::move(resized_img);
        *resized = true;
      } else if (warn) {
        (*warn) += "Resize failed for " + uri + ": " + rerr + "\n";
      }
    }
  }

  // Encode.
  image::WriteOption wopt;
  wopt.png_encoder = opts.png_encoder;
  wopt.jpeg_quality = opts.jpeg_quality;
  if (target_ext == "png") {
    wopt.format = image::WriteImageFormat::PNG;
  } else if (target_ext == "jpg" || target_ext == "jpeg") {
    wopt.format = image::WriteImageFormat::JPEG;
    // JPEG cannot carry alpha; drop it.
    if (img.channels == 4) {
      const size_t npix = size_t(img.width) * size_t(img.height);
      if (img.data.size() < npix * 4) {
        if (warn) {
          (*warn) += "Undersized image buffer for " + uri +
                     " - copying through unchanged.\n";
        }
        *out_bytes = src_bytes;
        *out_ext = src_ext_lower;
        return true;
      }
      Image rgb;
      rgb.width = img.width;
      rgb.height = img.height;
      rgb.channels = 3;
      rgb.bpp = 8;
      rgb.format = img.format;
      rgb.colorspace = img.colorspace;
      rgb.data.resize(npix * 3);
      for (size_t i = 0; i < npix; i++) {
        rgb.data[3 * i + 0] = img.data[4 * i + 0];
        rgb.data[3 * i + 1] = img.data[4 * i + 1];
        rgb.data[3 * i + 2] = img.data[4 * i + 2];
      }
      img = std::move(rgb);
    }
  } else {
    // Unsupported target for re-encode; pass through original bytes.
    *out_bytes = src_bytes;
    *out_ext = src_ext_lower;
    return true;
  }

  auto enc = image::WriteImageToMemory(img, wopt);
  if (!enc) {
    if (warn) {
      (*warn) += "Encode failed for " + uri + ": " + enc.error() +
                 " - copying through unchanged.\n";
    }
    *out_bytes = src_bytes;
    *out_ext = src_ext_lower;
    return true;
  }

  *out_bytes = std::move(enc.value());
  *out_ext = target_ext;
  *reencoded = true;
  return true;
}

// Expand a UDIM texture asset path (e.g. `diffuse.<UDIM>.png`), pack each
// resolved tile (1001..1100) into `assets`, and produce the archive-side
// `<UDIM>` pattern (e.g. `textures/diffuse.<UDIM>.png`). Each tile is run
// through ProcessTexture (resize/re-encode) like an ordinary texture.
//
// Returns false only on a hard error (caller should abort). On success,
// `*packed` indicates whether at least one tile was packed and
// `*archive_pattern` holds the rewritten `<UDIM>` reference.
bool ExpandAndPackUDIM(const std::string &udim_path,
                       AssetResolutionResolver &resolver,
                       const std::vector<std::string> &search_paths,
                       const std::string &base_dir,
                       const UsdzConvertOptions &options,
                       std::map<std::string, std::vector<uint8_t>> *assets,
                       std::string *archive_pattern, bool *packed,
                       UsdzConvertStats *stats, std::string *warn,
                       std::string *err) {
  (void)err;
  *packed = false;

  std::string prefix, suffix;
  if (!io::SplitUDIMPath(udim_path, &prefix, &suffix)) {
    return true;  // not a UDIM path
  }

  std::string pattern_archive;
  for (uint32_t id = 1001; id <= 1100; id++) {
    const std::string tile_orig = prefix + std::to_string(id) + suffix;

    std::vector<uint8_t> src_bytes;
    std::string rerr;
    if (!ReadAssetBytesWithBase(resolver, tile_orig, base_dir, &src_bytes, warn,
                                &rerr)) {
      continue;  // tile not present
    }

    const std::string src_ext = to_lower(io::GetFileExtension(tile_orig));
    std::vector<uint8_t> out_bytes;
    std::string out_ext;
    bool resized = false, reencoded = false;
    std::string pwarn;
    if (!ProcessTexture(src_bytes, src_ext, options, tile_orig, &out_bytes,
                        &out_ext, &resized, &reencoded, &pwarn)) {
      if (warn) *warn += "Failed to process UDIM tile " + tile_orig + "\n";
      continue;
    }
    if (warn) *warn += pwarn;

    if (!IsAllowedTextureExt(out_ext)) {
      out_ext = "png";
    }

    std::string tile_archive =
        SanitizeArchiveName(RelativeToSearchPath(tile_orig, search_paths));
    if (tile_archive == tile_orig) {
      tile_archive = SanitizeArchiveName(tile_orig);
    }
    if (to_lower(io::GetFileExtension(tile_archive)) != out_ext) {
      tile_archive = ReplaceExtension(tile_archive, out_ext);
    }

    // Avoid collisions across distinct tile bytes.
    std::string unique_name = tile_archive;
    uint32_t collision_suffix = 1;
    while (assets->count(unique_name) &&
           (*assets)[unique_name] != out_bytes) {
      unique_name = MakeCollisionArchiveName(tile_archive, collision_suffix++);
    }
    tile_archive = unique_name;

    (*assets)[tile_archive] = std::move(out_bytes);

    if (pattern_archive.empty()) {
      // Derive the archive `<UDIM>` pattern from the first packed tile by
      // replacing the tile id with the `<UDIM>` token.
      const std::string idstr = std::to_string(id);
      const size_t pos = tile_archive.rfind(idstr);
      if (pos != std::string::npos) {
        pattern_archive = tile_archive.substr(0, pos) + "<UDIM>" +
                          tile_archive.substr(pos + idstr.size());
      } else {
        pattern_archive = tile_archive;
      }
    }

    if (stats) {
      stats->num_textures++;
      if (resized) stats->num_textures_resized++;
      if (reencoded) {
        stats->num_textures_reencoded++;
      } else {
        stats->num_textures_passthrough++;
      }
    }

    Log(options.verbose, "  UDIM tile " + tile_orig + " -> " + tile_archive +
                             (resized ? " [resized]" : "") +
                             (reencoded ? " [reencoded]" : " [passthrough]"));

    *packed = true;
  }

  if (*packed) {
    *archive_pattern = pattern_archive;
  }
  return true;
}

struct NonFlattenLayerPackage {
  Layer layer;
  std::string source_path;
  std::string base_dir;
  std::string archive_name;
};

std::string ResolveDependencySourcePath(AssetResolutionResolver &resolver,
                                        const std::string &asset_path,
                                        const std::string &base_dir) {
  std::string result;
  if (asset_path.empty()) {
    return result;
  }
  if (io::IsAbsPath(asset_path)) {
    result = asset_path;
    return result;
  }
  if (!base_dir.empty()) {
    result = io::JoinPath(base_dir, asset_path);
    if (io::FileExists(result)) {
      return result;
    }
  }
  result = resolver.resolve(asset_path);
  if (result.empty()) {
    result = asset_path;
  }
  return result;
}

bool LoadLayerForPackaging(AssetResolutionResolver &resolver,
                           const std::string &source_path,
                           const std::string &authored_path,
                           Layer *layer, std::string *warn,
                           std::string *err) {
  if (io::FileExists(source_path)) {
    return tinyusdz::LoadLayerFromFile(source_path, layer, warn, err);
  }

  const std::string resolved = resolver.resolve(source_path);
  if (!resolved.empty()) {
    return tinyusdz::LoadLayerFromAsset(resolver, resolved, layer, warn, err);
  }

  const std::string authored_resolved = resolver.resolve(authored_path);
  if (!authored_resolved.empty()) {
    return tinyusdz::LoadLayerFromAsset(resolver, authored_resolved, layer, warn,
                                        err);
  }

  if (err) {
    *err = "Failed to resolve layer asset: " + authored_path;
  }
  return false;
}

std::string MakeLayerArchiveName(const std::string &source_path,
                                 const std::string &authored_path,
                                 const std::vector<std::string> &search_paths,
                                 USDZRootLayerFormat format) {
  std::string name = RelativeToSearchPath(source_path, search_paths);
  if (name == source_path && !authored_path.empty()) {
    name = authored_path;
  }
  name = SanitizeLayerArchiveName(name);
  const std::string ext = (format == USDZRootLayerFormat::USDA) ? "usda" : "usdc";
  if (to_lower(io::GetFileExtension(name)) != ext) {
    name = ReplaceExtension(name, ext);
  }
  return name;
}

bool SerializeLayerForUSDZ(const Layer &layer, USDZRootLayerFormat format,
                           std::vector<uint8_t> *out, std::string *warn,
                           std::string *err) {
  if (format == USDZRootLayerFormat::USDA) {
    const std::string text = tinyusdz::print_layer(layer, 0);
    if (text.empty()) {
      if (err) *err = "USDA serialization produced empty data.";
      return false;
    }
    out->assign(text.begin(), text.end());
    return true;
  }

  return tinyusdz::usdc::SaveAsUSDCToMemory(layer, out, warn, err);
}

bool ConvertNonFlattenUSDZ(const UsdzConvertOptions &options,
                           AssetResolutionResolver &resolver,
                           const std::vector<std::string> &search_paths,
                           UsdzConvertStats *stats, std::string *warn,
                           std::string *err) {
  const std::string &root_input = options.inputs[0];
  Log(options.verbose, "Loading + packaging layers (no flatten): " + root_input);

  std::vector<NonFlattenLayerPackage> packages;
  packages.push_back(NonFlattenLayerPackage{});
  std::string lwarn, lerr;
  if (!tinyusdz::LoadLayerFromFile(root_input, &packages[0].layer, &lwarn,
                                   &lerr)) {
    if (err) *err = "Failed to load root layer: " + lerr;
    return false;
  }
  if (warn) *warn += lwarn;
  packages[0].source_path = root_input;
  packages[0].base_dir = io::GetBaseDir(root_input);

  if (options.upAxis != Axis::Invalid) {
    packages[0].layer.metas().upAxis.set_value(options.upAxis);
  } else if (options.arkit_compatible) {
    packages[0].layer.metas().upAxis.set_value(Axis::Y);
  }
  if (options.metersPerUnit > 0.0) {
    packages[0].layer.metas().metersPerUnit.set_value(options.metersPerUnit);
  }
  if (!options.copyright.empty() || !options.url.empty()) {
    std::string doc;
    if (!options.copyright.empty()) doc += options.copyright;
    if (!options.url.empty()) {
      if (!doc.empty()) doc += "\n";
      doc += options.url;
    }
    packages[0].layer.metas().doc = doc;
  }

  std::map<std::string, size_t> source_to_package;
  source_to_package[root_input] = 0;
  std::vector<std::map<std::string, std::string>> composition_remaps;
  composition_remaps.resize(1);

  std::deque<size_t> queue;
  queue.push_back(0);
  constexpr size_t kMaxLayerCount = 1000;
  while (!queue.empty()) {
    const size_t package_index = queue.front();
    queue.pop_front();

    std::vector<std::string> deps;
    CollectCompositionAssetPaths(packages[package_index].layer, &deps);
    for (const std::string &dep : deps) {
      const std::string source = ResolveDependencySourcePath(
          resolver, dep, packages[package_index].base_dir);

      auto seen = source_to_package.find(source);
      if (seen == source_to_package.end()) {
        if (packages.size() >= kMaxLayerCount) {
          if (err) *err = "Too many layer dependencies while packaging USDZ.";
          return false;
        }

        NonFlattenLayerPackage pkg;
        pkg.source_path = source;
        pkg.base_dir = io::GetBaseDir(source);
        pkg.archive_name = MakeLayerArchiveName(source, dep, search_paths,
                                                USDZRootLayerFormat::USDA);

        uint32_t suffix = 1;
        bool archive_collision = true;
        while (archive_collision) {
          archive_collision = false;
          for (const NonFlattenLayerPackage &existing : packages) {
            if (existing.archive_name == pkg.archive_name) {
              archive_collision = true;
              pkg.archive_name = MakeCollisionArchiveName(pkg.archive_name,
                                                          suffix++);
              break;
            }
          }
        }

        std::string dwarn, derr;
        if (!LoadLayerForPackaging(resolver, source, dep, &pkg.layer, &dwarn,
                                   &derr)) {
          if (err) *err = "Failed to load dependency layer: " + derr;
          return false;
        }
        if (warn) *warn += dwarn;

        const size_t new_index = packages.size();
        source_to_package[source] = new_index;
        composition_remaps.push_back(std::map<std::string, std::string>());
        packages.push_back(std::move(pkg));
        queue.push_back(new_index);
        seen = source_to_package.find(source);
      }

      composition_remaps[package_index][dep] =
          packages[seen->second].archive_name;
    }
  }

  for (size_t i = 0; i < packages.size(); i++) {
    if (!composition_remaps[i].empty()) {
      const size_t n = RemapCompositionAssetPaths(packages[i].layer,
                                                  composition_remaps[i]);
      Log(options.verbose, "Remapped " + std::to_string(n) +
                               " composition path(s) in package layer " +
                               std::to_string(i) + ".");
    }
  }

  struct TextureRef {
    size_t package_index{0};
    std::string authored_path;
    std::string source_path;
  };

  std::vector<TextureRef> texture_refs;
  for (size_t i = 0; i < packages.size(); i++) {
    std::vector<std::string> paths;
    CollectLayerTextureAssetPaths(packages[i].layer, &paths);
    for (const std::string &path : paths) {
      TextureRef ref;
      ref.package_index = i;
      ref.authored_path = path;
      ref.source_path = ResolveDependencySourcePath(resolver, path,
                                                    packages[i].base_dir);
      texture_refs.push_back(std::move(ref));
    }
  }
  Log(options.verbose, "Found " + std::to_string(texture_refs.size()) +
                           " texture reference(s).");

  std::map<std::string, std::vector<uint8_t>> assets;
  std::map<std::string, std::string> source_to_archive;
  std::vector<std::map<std::string, std::string>> texture_remaps;
  texture_remaps.resize(packages.size());

  for (const TextureRef &ref : texture_refs) {
    auto existing = source_to_archive.find(ref.source_path);
    if (existing != source_to_archive.end()) {
      texture_remaps[ref.package_index][ref.authored_path] = existing->second;
      continue;
    }

    // UDIM texture: expand to tiles, pack each, and rewrite the authored
    // `<UDIM>` reference to the archive-side `<UDIM>` pattern.
    if (io::IsUDIMPath(ref.authored_path)) {
      std::string udim_pattern;
      bool udim_packed = false;
      if (!ExpandAndPackUDIM(ref.authored_path, resolver, search_paths,
                             packages[ref.package_index].base_dir, options,
                             &assets, &udim_pattern, &udim_packed, stats, warn,
                             err)) {
        return false;
      }
      if (udim_packed) {
        source_to_archive[ref.source_path] = udim_pattern;
        texture_remaps[ref.package_index][ref.authored_path] = udim_pattern;
      } else if (warn) {
        *warn += "No UDIM tiles found for '" + ref.authored_path + "'.\n";
      }
      continue;
    }

    std::string archive_name = SanitizeArchiveName(
        RelativeToSearchPath(ref.source_path, search_paths));
    if (archive_name == ref.source_path) {
      archive_name = SanitizeArchiveName(ref.authored_path);
    }

    std::vector<uint8_t> src_bytes;
    std::string rerr;
    if (!ReadAssetBytesWithBase(resolver, ref.authored_path,
                                packages[ref.package_index].base_dir,
                                &src_bytes, warn, &rerr)) {
      const std::string missing_ref =
          SafeMissingArchiveReference(ref.authored_path, search_paths);
      if (missing_ref.empty()) {
        if (err) {
          *err = "Unsafe texture path could not be packed: " +
                 ref.authored_path + " (" + rerr + ")";
        }
        return false;
      }
      texture_remaps[ref.package_index][ref.authored_path] = missing_ref;
      if (warn) {
        *warn += "Skipping unreadable texture '" + ref.authored_path + "': " +
                 rerr + "\n";
      }
      continue;
    }

    const std::string src_ext = to_lower(io::GetFileExtension(ref.authored_path));
    std::vector<uint8_t> out_bytes;
    std::string out_ext;
    bool resized = false;
    bool reencoded = false;
    std::string pwarn;
    if (!ProcessTexture(src_bytes, src_ext, options, ref.authored_path,
                        &out_bytes, &out_ext, &resized, &reencoded, &pwarn)) {
      if (warn) *warn += "Failed to process texture " + ref.authored_path + "\n";
      continue;
    }
    if (warn) *warn += pwarn;

    if (options.arkit_compatible && !IsAllowedARKitTextureExt(out_ext)) {
      if (err) {
        *err = "ARKit-compatible USDZ cannot package texture '" +
               ref.authored_path + "' with unsupported output extension '" +
               out_ext + "'.";
      }
      return false;
    }

    if (!IsAllowedTextureExt(out_ext)) {
      out_ext = "png";
    }
    if (to_lower(io::GetFileExtension(archive_name)) != out_ext) {
      archive_name = ReplaceExtension(archive_name, out_ext);
    }

    std::string unique_name = archive_name;
    uint32_t suffix = 1;
    while (assets.count(unique_name) && assets[unique_name] != out_bytes) {
      unique_name = MakeCollisionArchiveName(archive_name, suffix++);
    }
    archive_name = unique_name;

    assets[archive_name] = std::move(out_bytes);
    source_to_archive[ref.source_path] = archive_name;
    texture_remaps[ref.package_index][ref.authored_path] = archive_name;

    if (stats) {
      stats->num_textures++;
      if (resized) stats->num_textures_resized++;
      if (reencoded) {
        stats->num_textures_reencoded++;
      } else {
        stats->num_textures_passthrough++;
      }
    }
    Log(options.verbose, "  " + ref.authored_path + " -> " + archive_name +
                             (resized ? " [resized]" : "") +
                             (reencoded ? " [reencoded]" : " [passthrough]"));
  }

  for (size_t i = 0; i < packages.size(); i++) {
    if (!texture_remaps[i].empty()) {
      RemapLayerAssetPaths(packages[i].layer, texture_remaps[i]);
    }
  }

  for (size_t i = 1; i < packages.size(); i++) {
    std::vector<uint8_t> layer_bytes;
    std::string swarn, serr;
    if (!SerializeLayerForUSDZ(packages[i].layer, USDZRootLayerFormat::USDA,
                               &layer_bytes,
                               &swarn, &serr)) {
      if (err) *err = "Failed to serialize dependency layer: " + serr;
      return false;
    }
    if (warn) *warn += swarn;
    assets[packages[i].archive_name] = std::move(layer_bytes);
  }

  tinyusdz::USDZWriteOptions write_options;
  write_options.root_layer_format = tinyusdz::USDZRootLayerFormat::USDA;
  if (options.usdz_root_layer_format == USDZRootLayerFormat::USDC && warn) {
    *warn += "Non-flatten USDZ uses a USDA root layer to preserve composition "
             "arcs for downstream loaders.\n";
  }

  std::string swarn, serr;
  if (!tinyusdz::SaveAsUSDZToFile(options.output, packages[0].layer, assets,
                                  write_options, &swarn, &serr)) {
    if (err) *err = "Failed to write USDZ: " + serr;
    return false;
  }
  if (warn) *warn += swarn;

  std::vector<uint8_t> usdz_bytes;
  std::string ioerr;
  if (io::ReadWholeFile(&usdz_bytes, &ioerr, options.output,
                        /*max*/ 512 * 1024 * 1024)) {
    std::string vwarn, verr;
    if (!tinyusdz::ValidateUSDZ(usdz_bytes.data(), usdz_bytes.size(), &vwarn,
                                &verr)) {
      if (warn) *warn += "USDZ validation reported issues: " + verr + "\n";
    } else {
      if (warn) *warn += vwarn;
    }
    if (stats) stats->output_size = usdz_bytes.size();
  } else if (warn) {
    *warn += "Could not read output USDZ for validation: " + ioerr + "\n";
  }

  Log(options.verbose, "Packaged " + std::to_string(packages.size()) +
                           " layer(s) without flattening.");
  return true;
}

}  // namespace

size_t RemapTextureAssetPaths(Stage &stage,
                              const std::map<std::string, std::string> &remap) {
  std::vector<std::pair<UsdUVTexture *, std::string>> textures;
  for (auto &p : stage.root_prims()) {
    CollectTextures(p, &textures);
  }
  size_t n = 0;
  for (auto &kv : textures) {
    auto it = remap.find(kv.second);
    if (it != remap.end() && it->second != kv.second) {
      WriteTextureFilePath(kv.first, it->second);
      n++;
    }
  }
  return n;
}

bool Convert(const UsdzConvertOptions &options, UsdzConvertStats *stats,
             std::string *warn, std::string *err) {
  if (options.inputs.empty()) {
    if (err) (*err) = "No input USD file specified.";
    return false;
  }
  if (options.output.empty()) {
    if (err) (*err) = "No output USDZ file specified.";
    return false;
  }
  if (options.max_texture_size < 0) {
    if (err) (*err) = "max_texture_size must be non-negative.";
    return false;
  }

  PhaseTimer timer(options.verbose);

  const std::string &root_input = options.inputs[0];

  // --- Asset resolution setup (handles on-disk and in-USDZ assets) ---
  AssetResolutionResolver resolver;
  USDZAsset usdz_asset;  // kept alive for the resolver's lifetime
  bool root_is_usdz = false;
  {
    std::string fmt;
    if (tinyusdz::IsUSD(root_input, &fmt) && fmt == "usdz") {
      root_is_usdz = true;
    }
  }

  std::vector<std::string> search_paths;
  for (const auto &in : options.inputs) {
    const std::string dir = io::GetBaseDir(in);
    if (!dir.empty()) {
      search_paths.push_back(dir);
    }
  }
  if (search_paths.empty()) {
    search_paths.push_back("./");
  }
  resolver.set_search_paths(search_paths);

  if (root_is_usdz) {
    std::string w;
    if (!tinyusdz::ReadUSDZAssetInfoFromFile(root_input, &usdz_asset, &w, err)) {
      if (err) (*err) = "Failed to read USDZ asset info: " + (err ? *err : std::string());
      return false;
    }
    if (warn) (*warn) += w;
    if (!tinyusdz::SetupUSDZAssetResolution(resolver, &usdz_asset)) {
      if (err) (*err) = "Failed to set up USDZ asset resolution for: " + root_input;
      return false;
    }
  }

  if (!options.flatten && options.arkit_compatible && warn) {
    *warn += "ARKit-compatible USDZ requires a flattened package; ignoring "
             "-noFlatten.\n";
  }

  if (!options.flatten && !options.arkit_compatible &&
      options.output_format == OutputFormat::USDZ) {
    return ConvertNonFlattenUSDZ(options, resolver, search_paths, stats, warn,
                                 err);
  }

  // --- Load + (optionally) flatten the stage ---
  Stage stage;
  Layer layer_for_write;
  bool has_layer_for_write = false;
  std::string lwarn, lerr;

  if (options.flatten || options.arkit_compatible) {
    Log(options.verbose, "Loading + compositing (flatten): " + root_input);
    timer.begin("load-layer");
    Layer root_layer;
    if (!tinyusdz::LoadLayerFromFile(root_input, &root_layer, &lwarn, &lerr)) {
      if (err) (*err) = "Failed to load layer: " + lerr;
      return false;
    }
    // Additional inputs become (weaker) sublayers.
    for (size_t i = 1; i < options.inputs.size(); i++) {
      SubLayer sl;
      sl.assetPath = value::AssetPath(options.inputs[i]);
      root_layer.metas().subLayers.push_back(sl);
    }

    timer.begin("compose-fixedpoint");
    Layer composited;
    if (!ComposeLayerToFixedPoint(resolver, root_layer, &composited, &lwarn,
                                  &lerr)) {
      if (err) (*err) = "Composition failed: " + lerr;
      return false;
    }
    layer_for_write = std::move(composited);
    has_layer_for_write = true;
    timer.begin("layer-to-stage");
    if (!tinyusdz::LayerToStage(Layer(layer_for_write), &stage, &lwarn, &lerr)) {
      if (err) (*err) = "LayerToStage failed: " + lerr;
      return false;
    }
  } else {
    Log(options.verbose, "Loading (no flatten): " + root_input);
    timer.begin("load-layer");
    // Load as a Layer (no composition) so usdc/usda output goes through the
    // faithful PrimSpec-based writers. Loading into a Stage here would route
    // output through the typed Stage->crate reconstruction, which injects
    // schema fallbacks (purpose/visibility), drops relationships not modeled by
    // the typed schema (e.g. skel:animationSource), and loses `uniform`
    // variability. (USDZ -noFlatten is handled earlier by ConvertNonFlattenUSDZ;
    // all downstream `stage` uses below are guarded by !has_layer_for_write.)
    if (!tinyusdz::LoadLayerFromFile(root_input, &layer_for_write, &lwarn,
                                     &lerr)) {
      if (err) (*err) = "Failed to load layer: " + lerr;
      return false;
    }
    has_layer_for_write = true;
  }
  if (warn) (*warn) += lwarn;

  if (options.arkit_compatible) {
    if (has_layer_for_write) {
      CollectARKitCompatibilityWarnings(layer_for_write, warn);
    } else if (warn) {
      *warn += "ARKit compatibility: unable to inspect Layer-level prim "
               "metadata for warnings.\n";
    }
  }

  // --- ARKit / metadata fixups ---
  if (options.upAxis != Axis::Invalid) {
    stage.metas().upAxis.set_value(options.upAxis);
    if (has_layer_for_write) {
      layer_for_write.metas().upAxis.set_value(options.upAxis);
    }
  } else if (options.arkit_compatible) {
    // ARKit expects Y-up.
    stage.metas().upAxis.set_value(Axis::Y);
    if (has_layer_for_write) {
      layer_for_write.metas().upAxis.set_value(Axis::Y);
    }
  }
  if (options.metersPerUnit > 0.0) {
    stage.metas().metersPerUnit.set_value(options.metersPerUnit);
    if (has_layer_for_write) {
      layer_for_write.metas().metersPerUnit.set_value(options.metersPerUnit);
    }
  }
  if (!options.copyright.empty() || !options.url.empty()) {
    std::string doc;
    if (!options.copyright.empty()) doc += options.copyright;
    if (!options.url.empty()) {
      if (!doc.empty()) doc += "\n";
      doc += options.url;
    }
    stage.metas().doc = doc;
    if (has_layer_for_write) {
      layer_for_write.metas().doc = doc;
    }
  }

  // --- Enumerate textures ---
  timer.begin("enumerate-textures");
  std::vector<std::pair<UsdUVTexture *, std::string>> textures;
  std::vector<std::string> texture_paths;
  if (has_layer_for_write) {
    CollectLayerTextureAssetPaths(layer_for_write, &texture_paths);
  } else {
    for (auto &p : stage.root_prims()) {
      CollectTextures(p, &textures);
    }
    for (const auto &texture : textures) {
      texture_paths.push_back(texture.second);
    }
  }
  Log(options.verbose,
      "Found " + std::to_string(texture_paths.size()) + " texture reference(s).");

  // Texture packing is only relevant when writing a USDZ archive.
  // For flat USDC/USDA output, textures remain as external references.
  std::map<std::string, std::vector<uint8_t>> assets;  // archive name -> bytes
  std::map<std::string, std::string> path_to_archive;  // original -> archive name

  if (options.output_format == OutputFormat::USDZ) {
    Log(options.verbose,
        "Processing textures for USDZ packaging.");
    timer.begin("process-textures");

  const bool budget_mode = options.target_texture_bytes > 0;

  // Budget mode: decode all unique textures and shrink them globally to fit
  // options.target_texture_bytes before packing.
  std::map<std::string, std::pair<std::vector<uint8_t>, std::string>> fitted;  // orig -> (bytes, ext)
  if (budget_mode) {
    std::vector<std::string> fit_paths;
    std::vector<tydra::FitTextureInput> fit_inputs;
    std::set<std::string> seen;
    for (const std::string &orig : texture_paths) {
      if (seen.count(orig)) continue;
      seen.insert(orig);

      // UDIM textures are expanded and packed per-tile in the main loop below
      // (the budget fitter operates on single decoded images).
      if (io::IsUDIMPath(orig)) continue;

      std::vector<uint8_t> src_bytes;
      std::string rerr;
      if (!ReadAssetBytes(resolver, orig, &src_bytes, warn, &rerr)) {
        const std::string missing_ref =
            SafeMissingArchiveReference(orig, search_paths);
        if (missing_ref.empty()) {
          if (err) {
            (*err) = "Unsafe texture path could not be packed: " + orig +
                     " (" + rerr + ")";
          }
          return false;
        }
        path_to_archive[orig] = missing_ref;
        if (warn) (*warn) += "Skipping unreadable texture '" + orig + "': " + rerr + "\n";
        continue;
      }

      tydra::FitTextureInput fi;
      fi.ext = to_lower(io::GetFileExtension(orig));
      auto dec = image::LoadImageFromMemory(src_bytes.data(), src_bytes.size(), orig);
      if (dec && dec.value().image.bpp == 8) {
        fi.image = std::move(dec.value().image);
        fi.reencodable = true;
      } else {
        fi.reencodable = false;  // EXR/16-bit: keep as-is (fixed overhead)
        fi.original_bytes = src_bytes;  // only needed when not reencodable
      }
      fit_paths.push_back(orig);
      fit_inputs.push_back(std::move(fi));
    }

    tydra::FitTextureOptions fopts;
    fopts.target_total_bytes = options.target_texture_bytes;
    fopts.strategy = (options.fit_strategy == FitStrategy::Quality)
                         ? tydra::FitStrategy::Quality
                         : tydra::FitStrategy::Size;
    fopts.start_max_size = options.max_texture_size;
    fopts.min_texture_size = options.fit_min_texture_size;
    fopts.min_jpeg_quality = options.fit_min_jpeg_quality;
    fopts.jpeg_quality = options.jpeg_quality;
    fopts.png_encoder = options.png_encoder;

    std::vector<tydra::FitTextureOutput> fouts;
    std::string fwarn, ferr;
    if (!tydra::FitTexturesToBudget(fit_inputs, fopts, &fouts, &fwarn, &ferr)) {
      if (err) (*err) = "Texture budget fit failed: " + ferr;
      return false;
    }
    if (warn) (*warn) += fwarn;
    if (fouts.size() != fit_paths.size()) {
      if (err) (*err) = "Internal error: FitTexturesToBudget returned wrong number of outputs.";
      return false;
    }
    for (size_t i = 0; i < fit_paths.size(); i++) {
      fitted[fit_paths[i]] = {std::move(fouts[i].bytes), fouts[i].ext};
    }
  }

  for (const std::string &orig : texture_paths) {
    if (path_to_archive.count(orig)) {
      continue;  // already processed
    }

    // UDIM texture: expand to tiles, pack each, and rewrite the authored
    // `<UDIM>` reference to the archive-side `<UDIM>` pattern.
    if (io::IsUDIMPath(orig)) {
      std::string udim_pattern;
      bool udim_packed = false;
      if (!ExpandAndPackUDIM(orig, resolver, search_paths, /*base_dir*/ "",
                             options, &assets, &udim_pattern, &udim_packed,
                             stats, warn, err)) {
        return false;
      }
      if (udim_packed) {
        path_to_archive[orig] = udim_pattern;
        Log(options.verbose, "  " + orig + " -> " + udim_pattern + " [UDIM]");
      } else if (warn) {
        (*warn) += "No UDIM tiles found for '" + orig + "'.\n";
      }
      continue;
    }

    std::string archive_name =
        SanitizeArchiveName(RelativeToSearchPath(orig, search_paths));

    std::vector<uint8_t> out_bytes;
    std::string out_ext;
    bool resized = false, reencoded = false;

    if (budget_mode) {
      auto it = fitted.find(orig);
      if (it == fitted.end()) {
        // Unreadable safe relative path; leave the original reference unchanged.
        continue;
      }
      out_bytes = it->second.first;
      out_ext = it->second.second;
      resized = (options.fit_strategy == FitStrategy::Size);
      reencoded = true;
    } else {
      std::string src_ext = to_lower(io::GetFileExtension(orig));
      std::vector<uint8_t> src_bytes;
      std::string rerr;
      if (!ReadAssetBytes(resolver, orig, &src_bytes, warn, &rerr)) {
        const std::string missing_ref =
            SafeMissingArchiveReference(orig, search_paths);
        if (missing_ref.empty()) {
          if (err) {
            (*err) = "Unsafe texture path could not be packed: " + orig +
                     " (" + rerr + ")";
          }
          return false;
        }
        path_to_archive[orig] = missing_ref;
        if (warn) {
          (*warn) += "Skipping unreadable texture '" + orig + "': " + rerr + "\n";
        }
        // Leave safe missing references untouched instead of creating broken
        // sanitized references to assets that are not present in the archive.
        continue;
      }
      std::string pwarn;
      if (!ProcessTexture(src_bytes, src_ext, options, orig, &out_bytes, &out_ext,
                     &resized, &reencoded, &pwarn)) {
        if (warn) (*warn) += "Failed to process texture " + orig + "\n";
        continue;
      }
      if (warn) (*warn) += pwarn;
    }

    if (options.arkit_compatible && !IsAllowedARKitTextureExt(out_ext)) {
      if (err) {
        *err = "ARKit-compatible USDZ cannot package texture '" + orig +
               "' with unsupported output extension '" + out_ext + "'.";
      }
      return false;
    }

    if (!IsAllowedTextureExt(out_ext)) {
      // Should not happen, but guard the archive against invalid extensions.
      out_ext = "png";
    }
    if (to_lower(io::GetFileExtension(archive_name)) != out_ext) {
      archive_name = ReplaceExtension(archive_name, out_ext);
    }

    // Avoid archive name collisions for distinct sources.
    std::string unique_name = archive_name;
    uint32_t suffix = 1;
    const uint32_t kMaxSuffix = 100000;
    bool collision_ok = true;
    while (assets.count(unique_name) &&
           assets[unique_name] != out_bytes) {
      if (suffix > kMaxSuffix) {
        if (warn) {
          (*warn) += "Too many archive name collisions for " + archive_name +
                     "; skipping texture to avoid overwrite.\n";
        }
        // Exhausted suffixes: do NOT proceed with a name that already exists
        // with different bytes (that would overwrite the existing entry in
        // the ZIP archive). Skip this texture instead.
        collision_ok = false;
        break;
      }
      unique_name = MakeCollisionArchiveName(archive_name, suffix);
      suffix++;
    }
    if (!collision_ok) {
      continue;  // skip this texture; do NOT add to assets
    }
    archive_name = unique_name;

    assets[archive_name] = std::move(out_bytes);
    path_to_archive[orig] = archive_name;

    if (stats) {
      stats->num_textures++;
      if (resized) stats->num_textures_resized++;
      if (reencoded) {
        stats->num_textures_reencoded++;
      } else {
        stats->num_textures_passthrough++;
      }
    }
    Log(options.verbose, "  " + orig + " -> " + archive_name +
                             (resized ? " [resized]" : "") +
                             (reencoded ? " [reencoded]" : " [passthrough]"));
  }

  // --- Rewrite texture file paths to the archive-relative names ---
  timer.begin("rewrite-texture-paths");
  if (has_layer_for_write) {
    RemapLayerAssetPaths(layer_for_write, path_to_archive);
    if (options.arkit_compatible) {
      std::string rwarn, rerr;
      if (!tinyusdz::LayerToStage(Layer(layer_for_write), &stage, &rwarn,
                                  &rerr)) {
        if (err) *err = "LayerToStage after ARKit remap failed: " + rerr;
        return false;
      }
      if (warn) *warn += rwarn;
    }
  } else {
    for (auto &kv : textures) {
      UsdUVTexture *tex = kv.first;
      const std::string &orig = kv.second;
      auto it = path_to_archive.find(orig);
      if (it == path_to_archive.end()) continue;
      if (it->second != orig) {
        WriteTextureFilePath(tex, it->second);
      }
    }
  }

  } else {
    Log(options.verbose,
        "Rewriting absolute texture paths to relative (flat output).");
    std::string out_dir = io::GetBaseDir(options.output);
    if (out_dir.empty()) {
      out_dir = ".";
    }
    std::map<std::string, std::string> remap;
    for (const std::string &orig : texture_paths) {
      if (remap.count(orig)) continue;
      // Only relativize absolute paths.
      if (orig.empty() || !io::IsAbsPath(orig)) {
        continue;
      }
      std::string rel = ToRelativePath(out_dir, orig);
      if (rel != orig) {
        remap[orig] = rel;
      }
    }
    if (!remap.empty()) {
      size_t n = has_layer_for_write
                     ? RemapLayerAssetPaths(layer_for_write, remap)
                     : RemapTextureAssetPaths(stage, remap);
      Log(options.verbose,
          "Relativized " + std::to_string(n) + " texture path(s).");
    }
  }  // if USDZ

  // --- Write output ---
  timer.begin("write-output");
  bool write_ok = false;
  std::string swarn, serr;
  switch (options.output_format) {
    case OutputFormat::USDZ: {
      Log(options.verbose, "Writing USDZ: " + options.output);
      tinyusdz::USDZWriteOptions write_options;
      write_options.root_layer_format =
          (options.arkit_compatible)
              ? tinyusdz::USDZRootLayerFormat::USDC
          : (options.usdz_root_layer_format == USDZRootLayerFormat::USDA)
              ? tinyusdz::USDZRootLayerFormat::USDA
              : tinyusdz::USDZRootLayerFormat::USDC;
      if (options.arkit_compatible &&
          options.usdz_root_layer_format != USDZRootLayerFormat::USDC &&
          warn) {
        *warn += "ARKit-compatible USDZ requires a USDC root layer; ignoring "
                 "--rootLayerFormat.\n";
      }
      write_ok = has_layer_for_write
          ? (options.arkit_compatible
                 ? tinyusdz::SaveAsUSDZToFile(options.output, stage, assets,
                                              write_options, &swarn, &serr)
                 : tinyusdz::SaveAsUSDZToFile(options.output, layer_for_write,
                                              assets, write_options, &swarn,
                                              &serr))
          : tinyusdz::SaveAsUSDZToFile(options.output, stage, assets,
                                       write_options, &swarn, &serr);
      if (!write_ok && err) (*err) = "Failed to write USDZ: " + serr;
      break;
    }
    case OutputFormat::USDC: {
      Log(options.verbose, "Writing USDC: " + options.output);
      write_ok = has_layer_for_write
          ? tinyusdz::usdc::SaveAsUSDCToFile(options.output, layer_for_write,
                                             &swarn, &serr)
          : tinyusdz::usdc::SaveAsUSDCToFile(options.output, stage, &swarn, &serr);
      if (!write_ok && err) (*err) = "Failed to write USDC: " + serr;
      break;
    }
    case OutputFormat::USDA: {
      Log(options.verbose, "Writing USDA: " + options.output);
      if (has_layer_for_write) {
        std::ofstream ofs(options.output);
        if (ofs) {
          ofs << tinyusdz::print_layer(layer_for_write, 0);
          write_ok = bool(ofs);
        } else {
          serr = "Failed to open output file.";
          write_ok = false;
        }
      } else {
        write_ok = tinyusdz::usda::SaveAsUSDA(options.output, stage, &swarn, &serr);
      }
      if (!write_ok && err) (*err) = "Failed to write USDA: " + serr;
      break;
    }
  }
  if (!write_ok) {
    return false;
  }
  if (warn) (*warn) += swarn;

  // When writing USDZ, read back and validate.
  if (options.output_format == OutputFormat::USDZ) {
    timer.begin("validate-usdz");
    std::vector<uint8_t> usdz_bytes;
    std::string ioerr;
    if (io::ReadWholeFile(&usdz_bytes, &ioerr, options.output,
                          /*max*/ 512 * 1024 * 1024)) {
      std::string vwarn, verr;
      if (!tinyusdz::ValidateUSDZ(usdz_bytes.data(), usdz_bytes.size(), &vwarn,
                                  &verr)) {
        if (warn) {
          (*warn) += "USDZ validation reported issues: " + verr + "\n";
        }
      } else {
        Log(options.verbose, "USDZ validation: OK");
      }
      if (stats) stats->output_size = usdz_bytes.size();
    }
  } else {
    if (stats) {
      // For flat files, report filesystem size when available.
      stats->output_size = 0;
    }
  }

  timer.end();
  return true;
}

bool RepackTextureFiles(const RepackSpec &spec, const std::string &outputFile,
                        image::PngEncoder png_encoder, std::string *warn,
                        std::string *err) {
  if (spec.out_channels < 1 || spec.out_channels > 4) {
    if (err) (*err) = "RepackTextureFiles: out_channels must be 1-4.";
    return false;
  }

  // Gather unique input files and load them once.
  const RepackChannel *chans[4] = {&spec.r, &spec.g, &spec.b, &spec.a};
  std::vector<Image> images;
  std::map<std::string, int> file_to_index;

  for (int c = 0; c < spec.out_channels; c++) {
    const RepackChannel &rc = *chans[c];
    if (rc.input_file.empty()) continue;
    if (file_to_index.count(rc.input_file)) continue;
    auto loaded = image::LoadImageFromFile(rc.input_file);
    if (!loaded) {
      if (err) (*err) = "RepackTextureFiles: failed to load '" + rc.input_file +
                        "': " + loaded.error();
      return false;
    }
    file_to_index[rc.input_file] = int(images.size());
    images.push_back(std::move(loaded.value().image));
  }

  // Build the channel-pack spec referencing loaded images by index.
  tydra::ChannelPackSpec pspec;
  pspec.out_channels = spec.out_channels;
  pspec.out_width = spec.out_width;
  pspec.out_height = spec.out_height;
  tydra::ChannelSource *dst[4] = {&pspec.r, &pspec.g, &pspec.b, &pspec.a};
  for (int c = 0; c < spec.out_channels; c++) {
    const RepackChannel &rc = *chans[c];
    if (rc.input_file.empty()) {
      dst[c]->input_index = -1;
      dst[c]->constant = rc.constant;
    } else {
      dst[c]->input_index = file_to_index[rc.input_file];
      dst[c]->channel = rc.channel;
    }
  }

  Image packed;
  std::string perr;
  if (!tydra::PackChannels(images, pspec, &packed, &perr)) {
    if (err) (*err) = "RepackTextureFiles: " + perr;
    return false;
  }

  image::WriteOption wopt;
  wopt.png_encoder = png_encoder;
  auto ret = image::WriteImageToFile(outputFile, packed, wopt);
  if (!ret) {
    if (err) (*err) = "RepackTextureFiles: failed to write '" + outputFile +
                      "': " + ret.error();
    return false;
  }
  // No warnings to emit; PackChannels returns errors only.
  (void)warn;
  return true;
}

}  // namespace usdz
}  // namespace tinyusdz
