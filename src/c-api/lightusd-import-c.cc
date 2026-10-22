// SPDX-License-Identifier: Apache-2.0
#include "lightusd-import-c.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <new>

#include "c-internal.hh"
#include "external/filesystem/include/ghc/filesystem.hpp"
#include "next/resolver/asset-resolver.hh"
#ifdef LIGHTUSD_WITH_USDOBJ
#include "next/reader/obj-reader.hh"
#endif
#include "tydra/asset-import/import.hh"
#include "tydra/next/urdf-to-usd.hh"
namespace ia = lightusd::tydra::import_asset;
using lightusd_internal::Fail;
namespace {
lightusd_status Options(const lightusd_import_options* in, ia::Options* out) {
  if (!in) return LIGHTUSD_OK;
  if (in->struct_size < sizeof(*in) || in->format > LIGHTUSD_IMPORT_MJCF ||
      in->max_memory_bytes > SIZE_MAX || in->max_input_bytes > SIZE_MAX ||
      !in->max_depth)
    return Fail(LIGHTUSD_ERR_INVALID_ARG, "Invalid import options");
  const char* formats[] = {"auto", "obj", "urdf", "mjcf"};
  out->format = formats[in->format];
  if (in->max_memory_bytes)
    out->max_memory = static_cast<size_t>(in->max_memory_bytes);
  if (in->max_input_bytes)
    out->max_input = static_cast<size_t>(in->max_input_bytes);
  out->max_depth = in->max_depth;
  if (in->settings_json) {
    ia::Json settings;
    lightusd::minijson::Error error;
    if (!lightusd::minijson::Parse(in->settings_json, &settings, &error) ||
        !settings.is_object())
      return Fail(LIGHTUSD_ERR_INVALID_ARG,
                  "Import settings must be a JSON object");
    for (const auto& member : *settings.object_items()) {
      const auto& key = member.key;
      const auto& v = member.value();
      if (key == "up_axis" && v.is_string())
        out->up_axis = v.get<std::string>();
      else if (key == "allow_missing" && v.is_boolean())
        out->allow_missing = v.get<bool>();
      else if (key == "tessellate_collision_shapes" && v.is_boolean())
        out->tessellate_collision_shapes = v.get<bool>();
      else if (key == "asset_dirs" && v.is_array()) {
        for (const auto& dir : v) {
          if (!dir.is_string())
            return Fail(LIGHTUSD_ERR_INVALID_ARG,
                        "asset_dirs must contain strings");
          out->asset_dirs.push_back(dir.get<std::string>());
        }
      } else if (key == "packages" && v.is_object()) {
        for (const auto& package : *v.object_items()) {
          if (!package.value().is_string())
            return Fail(LIGHTUSD_ERR_INVALID_ARG,
                        "package roots must be strings");
          out->packages[package.key] = package.value().get<std::string>();
        }
      } else
        return Fail(LIGHTUSD_ERR_INVALID_ARG,
                    "Unknown or invalid import setting: " + key);
    }
  }
  if (in->resolver) {
    auto* resolver = lightusd_internal::AssetResolverForLoad(in->resolver);
    out->read = [resolver](const std::string& name, const std::string& anchor,
                           std::vector<uint8_t>* bytes, std::string* resolved,
                           std::string* err) {
      auto asset = resolver->Resolve(name, anchor, false);
      if (!asset.exists) {
        if (err) *err = "Unresolved import asset: " + name;
        return false;
      }
      *resolved = asset.resolved_path;
      return resolver->ReadAsset(asset, bytes, err);
    };
  }
  return LIGHTUSD_OK;
}
lightusd_status Failure(const std::string& error) {
  if (error.find("budget") != std::string::npos ||
      error.find("limit") != std::string::npos)
    return Fail(LIGHTUSD_ERR_RESOURCE_LIMIT, error);
  return Fail(LIGHTUSD_ERR_PARSE, error);
}
#ifdef LIGHTUSD_WITH_USDOBJ
bool IsOBJ(const std::string& source, const ia::Options& options) {
  std::string ext = ghc::filesystem::path(source).extension().string();
  for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return options.format == "obj" || (options.format == "auto" && ext == ".obj");
}
lightusd_status FinishOBJ(std::shared_ptr<lightusd::next::Layer> layer,
                         const std::string& source, std::string warnings,
                         lightusd_stage** out) {
  auto* handle = new (std::nothrow) lightusd_stage;
  if (!handle) return Fail(LIGHTUSD_ERR_OUT_OF_MEMORY, "Cannot allocate imported stage");
  auto assets = layer->meta().imported_assets;
  if (assets) {
    for (const auto& item : assets->by_identifier) {
      const auto name = "textures/" + std::to_string(handle->import_assets.size()) +
          "_" + ghc::filesystem::path(item.first).filename().string();
      handle->import_assets[name] = *item.second;
      handle->import_asset_paths[item.first] = name;
    }
    layer->meta().imported_assets.reset();
  }
  handle->stage.SetRootLayer(std::move(*layer));
  handle->warnings = std::move(warnings);
  handle->source_dir = ghc::filesystem::path(source).parent_path().string();
  *out = handle;
  return LIGHTUSD_OK;
}
#endif
lightusd_status Finish(ia::Result& result, const std::string& source,
                       lightusd_stage** out) {
  auto* handle = new (std::nothrow) lightusd_stage;
  if (!handle)
    return Fail(LIGHTUSD_ERR_OUT_OF_MEMORY, "Cannot allocate imported stage");
  std::string warn, err;
  if (!lightusd::tydra::next::ConvertURDFPayloadToUSDStage(
          result.payload, &handle->stage, &warn, &err)) {
    delete handle;
    return Failure(err);
  }
  handle->warnings = result.warnings + warn;
  handle->source_dir = ghc::filesystem::path(source).parent_path().string();
  // source_filename is intentionally empty: a USD provenance query must not
  // try to reload a robot or OBJ file through the USD-only core loader.
  handle->import_assets = std::move(result.assets);
  // Collect source -> archive names from the material import contract.
  for (const auto& mat : result.payload["materials"]) {
    if (mat.contains("texturePackage"))
      handle->import_asset_paths[mat["texture"].get<std::string>()] =
          mat["texturePackage"].get<std::string>();
    for (const auto& map : mat["maps"])
      if (map.contains("package"))
        handle->import_asset_paths[map["file"].get<std::string>()] =
            map["package"].get<std::string>();
  }
  *out = handle;
  return LIGHTUSD_OK;
}
}  // namespace
extern "C" {
void lightusd_import_options_init(lightusd_import_options* o) {
  if (!o) return;
  *o = {};
  o->struct_size = sizeof(*o);
  o->max_memory_bytes = 1024ull * 1024 * 1024;
  o->max_input_bytes = 512ull * 1024 * 1024;
  o->max_depth = 64;
}
lightusd_status lightusd_import_file(const char* filename,
                                     const lightusd_import_options* options,
                                     lightusd_stage** out) {
  if (out) *out = nullptr;
  if (!filename || !out)
    return Fail(LIGHTUSD_ERR_INVALID_ARG, "Import filename/output is null");
  ia::Options opts;
  auto status = Options(options, &opts);
  if (status != LIGHTUSD_OK) return status;
  ia::Result result;
  std::string err;
#ifdef LIGHTUSD_WITH_USDOBJ
  if (IsOBJ(filename, opts)) {
    std::string warn;
    auto layer = lightusd::next::LoadOBJLayerFromFile(filename, opts, &warn, &err);
    if (!layer) return Failure(err);
    return FinishOBJ(std::move(layer), filename, std::move(warn), out);
  }
#endif
  if (!ia::BuildPayloadFile(filename, opts, &result, &err)) return Failure(err);
  return Finish(result, filename, out);
}
lightusd_status lightusd_import_memory(const uint8_t* data, size_t size,
                                       const char* source,
                                       const lightusd_import_options* options,
                                       lightusd_stage** out) {
  if (out) *out = nullptr;
  if ((!data && size) || !out)
    return Fail(LIGHTUSD_ERR_INVALID_ARG, "Import data/output is null");
  ia::Options opts;
  auto status = Options(options, &opts);
  if (status != LIGHTUSD_OK) return status;
  if (size > opts.max_input || size > opts.max_memory)
    return Fail(LIGHTUSD_ERR_RESOURCE_LIMIT, "Import input limit exceeded");
  ia::Result result;
  std::string err;
#ifdef LIGHTUSD_WITH_USDOBJ
  if (IsOBJ(source ? source : "", opts)) {
    std::string warn;
    auto layer = lightusd::next::LoadOBJLayerFromMemory(source ? source : "",
        data, size, opts, &warn, &err);
    if (!layer) return Failure(err);
    return FinishOBJ(std::move(layer), source ? source : "", std::move(warn), out);
  }
#endif
  std::string text(size ? reinterpret_cast<const char*>(data) : "", size);
  if (!ia::BuildPayload(text, source ? source : "", opts, &result, &err))
    return Failure(err);
  return Finish(result, source ? source : "", out);
}
}
