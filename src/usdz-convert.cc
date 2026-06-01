// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment, Inc.

#include "usdz-convert.hh"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <utility>

#include "tinyusdz.hh"
#include "composition.hh"
#include "asset-resolution.hh"
#include "usdShade.hh"
#include "core/animatable.hh"
#include "image-loader.hh"
#include "image-writer.hh"
#include "io-util.hh"
#include "str-util.hh"
#include "tydra/texture-util.hh"

namespace tinyusdz {
namespace usdz {

namespace {

void Log(bool verbose, const std::string &msg) {
  if (verbose) {
    std::printf("[tusdzconvert] %s\n", msg.c_str());
  }
}

bool IsAllowedTextureExt(const std::string &ext_lower) {
  return ext_lower == "png" || ext_lower == "jpg" || ext_lower == "jpeg" ||
         ext_lower == "exr";
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

bool ReadAssetBytes(AssetResolutionResolver &resolver, const std::string &assetPath,
                    std::vector<uint8_t> *out, std::string *warn,
                    std::string *err) {
  const std::string resolved = resolver.resolve(assetPath);
  if (resolved.empty()) {
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

  // --- Load + (optionally) flatten the stage ---
  Stage stage;
  std::string lwarn, lerr;

  if (options.flatten) {
    Log(options.verbose, "Loading + compositing (flatten): " + root_input);
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

    Layer composited;
    if (!tinyusdz::CompositeAllArcs(resolver, root_layer, &composited, &lwarn,
                                    &lerr)) {
      if (err) (*err) = "Composition failed: " + lerr;
      return false;
    }
    if (!tinyusdz::LayerToStage(std::move(composited), &stage, &lwarn, &lerr)) {
      if (err) (*err) = "LayerToStage failed: " + lerr;
      return false;
    }
  } else {
    Log(options.verbose, "Loading (no flatten): " + root_input);
    if (!tinyusdz::LoadUSDFromFile(root_input, &stage, &lwarn, &lerr)) {
      if (err) (*err) = "Failed to load USD: " + lerr;
      return false;
    }
  }
  if (warn) (*warn) += lwarn;

  // --- ARKit / metadata fixups ---
  if (options.upAxis != Axis::Invalid) {
    stage.metas().upAxis.set_value(options.upAxis);
  } else if (options.arkit_compatible) {
    // ARKit expects Y-up.
    stage.metas().upAxis.set_value(Axis::Y);
  }
  if (options.metersPerUnit > 0.0) {
    stage.metas().metersPerUnit.set_value(options.metersPerUnit);
  }
  if (!options.copyright.empty() || !options.url.empty()) {
    std::string doc;
    if (!options.copyright.empty()) doc += options.copyright;
    if (!options.url.empty()) {
      if (!doc.empty()) doc += "\n";
      doc += options.url;
    }
    stage.metas().doc = doc;
  }

  // --- Enumerate textures ---
  std::vector<std::pair<UsdUVTexture *, std::string>> textures;
  for (auto &p : stage.root_prims()) {
    CollectTextures(p, &textures);
  }
  Log(options.verbose,
      "Found " + std::to_string(textures.size()) + " texture reference(s).");

  // --- Process unique textures, build the asset map ---
  std::map<std::string, std::vector<uint8_t>> assets;  // archive name -> bytes
  std::map<std::string, std::string> path_to_archive;  // original -> archive name

  const bool budget_mode = options.target_texture_bytes > 0;

  // Budget mode: decode all unique textures and shrink them globally to fit
  // options.target_texture_bytes before packing.
  std::map<std::string, std::pair<std::vector<uint8_t>, std::string>> fitted;  // orig -> (bytes, ext)
  if (budget_mode) {
    std::vector<std::string> fit_paths;
    std::vector<tydra::FitTextureInput> fit_inputs;
    std::set<std::string> seen;
    for (auto &kv : textures) {
      const std::string &orig = kv.second;
      if (seen.count(orig)) continue;
      seen.insert(orig);

      std::vector<uint8_t> src_bytes;
      std::string rerr;
      if (!ReadAssetBytes(resolver, orig, &src_bytes, warn, &rerr)) {
        if (IsUnsafeUnresolvedTexturePath(orig)) {
          if (err) {
            (*err) = "Unsafe texture path could not be packed: " + orig +
                     " (" + rerr + ")";
          }
          return false;
        }
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

  for (auto &kv : textures) {
    const std::string &orig = kv.second;
    if (path_to_archive.count(orig)) {
      continue;  // already processed
    }

    std::string archive_name = SanitizeArchiveName(orig);

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
        if (IsUnsafeUnresolvedTexturePath(orig)) {
          if (err) {
            (*err) = "Unsafe texture path could not be packed: " + orig +
                     " (" + rerr + ")";
          }
          return false;
        }
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
  for (auto &kv : textures) {
    UsdUVTexture *tex = kv.first;
    const std::string &orig = kv.second;
    auto it = path_to_archive.find(orig);
    if (it == path_to_archive.end()) continue;
    if (it->second != orig) {
      WriteTextureFilePath(tex, it->second);
    }
  }

  // --- Write + validate USDZ ---
  Log(options.verbose, "Writing USDZ: " + options.output);
  std::string swarn, serr;
  if (!tinyusdz::SaveAsUSDZToFile(options.output, stage, assets, &swarn, &serr)) {
    if (err) (*err) = "Failed to write USDZ: " + serr;
    return false;
  }
  if (warn) (*warn) += swarn;

  // Read back + validate.
  std::vector<uint8_t> usdz_bytes;
  std::string ioerr;
  if (io::ReadWholeFile(&usdz_bytes, &ioerr, options.output, /*max*/ 512 * 1024 * 1024)) {
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
