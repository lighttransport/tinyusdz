// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment, Inc.

#include "composition.hh"

#include <set>
#include <stack>

#include "asset-resolution.hh"
#include "common-macros.inc"
#include "io-util.hh"
#include "tiny-format.hh"
#include "usda-reader.hh"

#define PushError(s) \
  if (err) {         \
    (*err) += s;     \
  }

namespace tinyusdz {

namespace {

bool IsVisited(const std::vector<std::set<std::string>> layer_names_stack,
               const std::string &name) {
  for (size_t i = 0; i < layer_names_stack.size(); i++) {
    if (layer_names_stack[i].count(name)) {
      return true;
    }
  }
  return false;
}

bool CompositeSublayersRec(const AssetResolutionResolver &resolver, const Layer &in_layer,
                           std::vector<std::set<std::string>> layer_names_stack,
                           Layer *composited_layer, std::string *err, const SublayersCompositionOptions &options) {
  if (layer_names_stack.size() > options.max_depth) {
    if (err) {
      (*err) += "subLayer is nested too deeply.";
    }
    return false;
  }

  layer_names_stack.emplace_back(std::set<std::string>());
  std::set<std::string> &curr_layer_names = layer_names_stack.back();

  for (const auto &layer : in_layer.metas().subLayers) {
    std::string sublayer_asset_path = layer.GetAssetPath();
    DCOUT("Load subLayer " << sublayer_asset_path);

    // Do cyclic referencing check.
    // TODO: Use resolved name?
    if (IsVisited(layer_names_stack, sublayer_asset_path)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Circular referenceing detected for subLayer: {} in {}",
                      sublayer_asset_path, in_layer.name()));
    }

    std::string layer_filepath = resolver.resolve(sublayer_asset_path);
    if (layer_filepath.empty()) {
      PUSH_ERROR_AND_RETURN(fmt::format("{} not found in path: {}",
                                        sublayer_asset_path, resolver.search_paths_str()));
    }

    std::vector<uint8_t> sublayer_data;
    if (!tinyusdz::io::ReadWholeFile(&sublayer_data, err, layer_filepath,
                                     /* filesize_max */ 0)) {
      PUSH_ERROR_AND_RETURN("Failed to read file: " << layer_filepath);
    }

    tinyusdz::StreamReader ssr(sublayer_data.data(), sublayer_data.size(),
                               /* swap endian */ false);
    tinyusdz::usda::USDAReader sublayer_reader(&ssr);

#if 0 // not used
    // Use the first path as base_dir.
    // TODO: Use AssetResolutionResolver in USDAReder.
    //std::string base_dir;
    //if (resolver.search_paths().size()) {
    //  base_dir = resolver.search_paths()[0];
    //}
    //sublayer_reader.SetBaseDir(base_dir);
#endif

    uint32_t sublayer_load_states =
        static_cast<uint32_t>(tinyusdz::LoadState::Sublayer);

    tinyusdz::Layer sublayer;
    {
      // TODO: ReaderConfig.
      bool ret = sublayer_reader.read(sublayer_load_states);

      if (!ret) {
        PUSH_ERROR_AND_RETURN("Failed to parse : "
                              << layer_filepath << sublayer_reader.GetError());
      }

      ret = sublayer_reader.get_as_layer(&sublayer);
      if (!ret) {
        PUSH_ERROR_AND_RETURN("Failed to get " << layer_filepath
                                               << " as subLayer");
      }
    }

    curr_layer_names.insert(sublayer_asset_path);

    Layer composited_sublayer;

    // Recursively load subLayer
    if (!CompositeSublayersRec(resolver, sublayer, layer_names_stack,
                               &composited_sublayer, err, options)) {
      return false;
    }

    // NOTE: `over` specifier is ignored when merging Prims among different subLayers 
    for (auto &prim : composited_sublayer.prim_specs()) {
      if (composited_layer->has_primspec(prim.first)) {
        // Skip 
      } else {
        if (!composited_layer->emplace_primspec(prim.first, std::move(prim.second))) {
          PUSH_ERROR_AND_RETURN(fmt::format("Compositing PrimSpec {} in {} failed.", prim.first, layer_filepath));
        }
      }
    }

  }

  layer_names_stack.pop_back();

  return true;
}

}  // namespace

bool CompositeSublayers(const std::string &base_dir, const Layer &in_layer,
                        Layer *composited_layer, std::string *err, SublayersCompositionOptions options) {

  tinyusdz::AssetResolutionResolver resolver;
  resolver.set_search_paths({base_dir});

  return CompositeSublayers(resolver, in_layer, composited_layer, err, options);
}

bool CompositeSublayers(const AssetResolutionResolver &resolver, const Layer &in_layer,
                        Layer *composited_layer, std::string *err, SublayersCompositionOptions options) {

  std::vector<std::set<std::string>> layer_names_stack;

  tinyusdz::Stage stage;

  std::cout << "Resolve subLayers..\n";
  if (!CompositeSublayersRec(resolver, in_layer, layer_names_stack,
                             composited_layer, err, options)) {
    return false;
  }

  return true;
}

}  // namespace tinyusdz
