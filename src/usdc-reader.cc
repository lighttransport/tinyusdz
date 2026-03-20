// SPDX-License-Identifier: Apache 2.0
// Copyright 2020 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// USDC(Crate) reader — core: ReadUSDC, ReconstructStage, ToLayer, interface
//
// Method implementations are split across:
//   usdc-reader.cc             — this file (core I/O + interface)
//   usdc-reader-property.cc    — ParseProperty, BuildPropertyMap
//   usdc-reader-prim.cc        — ParsePrimSpec, StageMeta, type dispatch
//   usdc-reader-reconstruct.cc — node reconstruction + variant handling
//

#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "usdc-reader-impl.hh"

#if !defined(TINYUSDZ_DISABLE_MODULE_USDC_READER)

namespace tinyusdz {
namespace usdc {

namespace {}

bool USDCReader::Impl::ResolveFieldValuePairs(
    const crate::Spec &spec, const crate::FieldValuePairVector **fvs,
    crate::FieldValuePairVector *scratch) {
  if (!fvs) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Internal error: `fvs` is nullptr.");
  }

  if (_config.use_lazy_property_construction) {
    if (!scratch) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Internal error: `scratch` is nullptr.");
    }

    if (!crate_reader) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Internal error: crate reader is nullptr.");
    }

    // Release budget reserved by the previous DecodeFieldSet call before
    // clearing the scratch buffer (scratch->clear() frees the actual memory).
    if (_scratch_budget_reserved > 0) {
      crate_reader->ReleaseMemoryBudget(_scratch_budget_reserved);
      _scratch_budget_reserved = 0;
    }

    scratch->clear();

    uint64_t budget_before = crate_reader->GetMemoryUsageInBytes();
    if (!crate_reader->DecodeFieldSet(spec.fieldset_index, scratch)) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Failed to decode fieldset id: " +
                    std::to_string(spec.fieldset_index.value));
    }
    _scratch_budget_reserved =
        crate_reader->GetMemoryUsageInBytes() - budget_before;

    (*fvs) = scratch;
    return true;
  }

  if (!_live_fieldsets) {
    PUSH_ERROR_AND_RETURN_TAG(kTag,
                              "Internal error: live fieldsets is nullptr.");
  }

  if (!_live_fieldsets->count(spec.fieldset_index)) {
    PUSH_ERROR_AND_RETURN_TAG(
        kTag, "FieldSet id: " + std::to_string(spec.fieldset_index.value) +
                  " must exist in live fieldsets.");
  }

  (*fvs) = &(_live_fieldsets->at(spec.fieldset_index));
  return true;
}

bool USDCReader::Impl::ReconstructStage(Stage *stage) {

  // Report progress (90% - starting reconstruction)
  if (_progress_callback) {
    if (!_progress_callback(0.9f, _progress_userptr)) {
      PUSH_ERROR("Reconstruction cancelled by progress callback.");
      return false;
    }
  }

  DCOUT(fmt::format("# of Paths = {}", crate_reader->NumPaths()));

  if (crate_reader->NumNodes() == 0) {
    PUSH_WARN("Empty scene.");
    return true;
  }

  // Use references to avoid copying data from crate_reader
  _nodes = &crate_reader->GetNodes();
  _specs = &crate_reader->GetSpecs();
  _fields = &crate_reader->GetFields();
  _fieldset_indices = &crate_reader->GetFieldsetIndices();
  _paths = &crate_reader->GetPaths();
  _elemPaths = &crate_reader->GetElemPaths();
  _live_fieldsets = &crate_reader->GetLiveFieldSets();

  PathIndexToSpecIndexMap
      path_index_to_spec_index_map;  // path_index -> spec_index

  {
    for (size_t i = 0; i < _specs->size(); i++) {
      if ((*_specs)[i].path_index.value == ~0u) {
        continue;
      }

      if (path_index_to_spec_index_map.count((*_specs)[i].path_index.value) != 0) {
        PUSH_ERROR_AND_RETURN("Multiple PathIndex found in Crate data.");
      }

      DCOUT(fmt::format("path index[{}] -> spec index [{}]",
                        (*_specs)[i].path_index.value, uint32_t(i)));
      path_index_to_spec_index_map[(*_specs)[i].path_index.value] = uint32_t(i);
    }
  }

  stage->root_prims().clear();

  int root_node_id = 0;
  bool ret = ReconstructPrimRecursively(/* no further root for root_node */ -1,
                                        root_node_id, /* root Prim */ nullptr,
                                        /* level */ 0,
                                        path_index_to_spec_index_map, stage);

  if (!ret) {
    PUSH_ERROR_AND_RETURN("Failed to reconstruct Stage(Prim hierarchy)");
  }

  stage->compute_absolute_prim_path_and_assign_prim_id();

  // Attach mmap array table to Stage for zero-copy access
  if (_config.mmap_zero_copy && !_mmap_table.empty()) {
    stage->set_mmap_table(std::move(_mmap_table));
  }

  // Free decompression buffers after reconstruction completes.
  crate_reader->ShrinkDecompressionBuffers();

  return true;
}

bool USDCReader::Impl::ToLayer(Layer *layer) {

  if (!layer) {
    PUSH_ERROR_AND_RETURN("`layer` argument is nullptr.");
  }

  DCOUT(fmt::format("# of Paths = {}", crate_reader->NumPaths()));

  if (crate_reader->NumNodes() == 0) {
    PUSH_WARN("Empty scene.");
    return true;
  }

  // Use references to avoid copying data from crate_reader
  _nodes = &crate_reader->GetNodes();
  _specs = &crate_reader->GetSpecs();
  _fields = &crate_reader->GetFields();
  _fieldset_indices = &crate_reader->GetFieldsetIndices();
  _paths = &crate_reader->GetPaths();
  _elemPaths = &crate_reader->GetElemPaths();
  _live_fieldsets = &crate_reader->GetLiveFieldSets();

  PathIndexToSpecIndexMap
      path_index_to_spec_index_map;  // path_index -> spec_index

  {
    for (size_t i = 0; i < _specs->size(); i++) {
      if ((*_specs)[i].path_index.value == ~0u) {
        continue;
      }

      if (path_index_to_spec_index_map.count((*_specs)[i].path_index.value) != 0) {
        PUSH_ERROR_AND_RETURN("Multiple PathIndex found in Crate data.");
      }

      DCOUT(fmt::format("path index[{}] -> spec index [{}]",
                        (*_specs)[i].path_index.value, uint32_t(i)));
      path_index_to_spec_index_map[(*_specs)[i].path_index.value] = uint32_t(i);
    }
  }

  layer->primspecs().clear();

  int root_node_id = 0;
  bool ret = ReconstructPrimSpecRecursively(/* no further root for root_node */ -1,
                                        root_node_id, /* root Prim */ nullptr,
                                        /* level */ 0,
                                        path_index_to_spec_index_map, layer);

  if (!ret) {
    PUSH_ERROR_AND_RETURN("Failed to reconstruct Layer(PrimSpec hierarchy)");
  }

  return true;
}

bool USDCReader::Impl::ReadUSDC() {
  TINYUSDZ_PROFILE_FUNCTION("usdc-reader");
  if (crate_reader) {
    delete crate_reader;
  }

  // Setup CrateReaderConfig.
  crate::CrateReaderConfig config;

  // Transfer settings
  config.numThreads = _config.numThreads;
  config.use_mmap = _config.use_mmap || _config.mmap_zero_copy;

  size_t sz_mb = _config.kMaxAllowedMemoryInMB;
  if (sizeof(size_t) == 4) {
    // 32bit
    sz_mb = (std::min)(size_t(1024 * 2), sz_mb);
    config.maxMemoryBudget = sz_mb * 1024 * 1024;
  } else {
    config.maxMemoryBudget = _config.kMaxAllowedMemoryInMB * 1024ull * 1024ull;
  }

  crate_reader = new crate::CrateReader(_sr, config);

  // Pass progress callback to crate reader if set
  if (_progress_callback) {
    crate_reader->SetProgressCallback(_progress_callback, _progress_userptr);
  }

  _warn.clear();
  _err.clear();

  {
    TINYUSDZ_PROFILE_SCOPE("usdc-reader", "ReadBootStrap");
    if (!crate_reader->ReadBootStrap()) {
      _warn = crate_reader->GetWarning();
      _err = crate_reader->GetError();
      return false;
    }
  }

  {
    TINYUSDZ_PROFILE_SCOPE("usdc-reader", "ReadTOC");
    if (!crate_reader->ReadTOC()) {
      _warn = crate_reader->GetWarning();
      _err = crate_reader->GetError();
      return false;
    }
  }

  // Read known sections

  {
    TINYUSDZ_PROFILE_SCOPE("usdc-reader", "ReadTokens");
    if (!crate_reader->ReadTokens()) {
      _warn = crate_reader->GetWarning();
      _err = crate_reader->GetError();
      return false;
    }
  }

  {
    TINYUSDZ_PROFILE_SCOPE("usdc-reader", "ReadStrings");
    if (!crate_reader->ReadStrings()) {
      _warn = crate_reader->GetWarning();
      _err = crate_reader->GetError();
      return false;
    }
  }

  if (!crate_reader->ReadFields()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  if (!crate_reader->ReadFieldSets()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  if (!crate_reader->ReadPaths()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  if (!crate_reader->ReadSpecs()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  // TODO(syoyo): Read unknown sections

  if (_config.use_lazy_property_construction) {
    DCOUT("Skip BuildLiveFieldSets (lazy property construction enabled)");
  } else {
    DCOUT("BuildLiveFieldSets");
    if (!crate_reader->BuildLiveFieldSets()) {
      _warn = crate_reader->GetWarning();
      _err = crate_reader->GetError();

      return false;
    }
  }

  if (!_config.use_lazy_property_construction) {
    crate_reader->ShrinkDecompressionBuffers();
  }

  _warn += crate_reader->GetWarning();
  _err += crate_reader->GetError();

  DCOUT("Read Crate.");

  // Report final progress (100%)
  if (_progress_callback) {
    _progress_callback(1.0f, _progress_userptr);
  }

  return true;
}

//
// -- Interface --
//
USDCReader::USDCReader(StreamReader *sr, const USDCReaderConfig &config) {
  impl_ = new USDCReader::Impl(sr, config);
}

USDCReader::~USDCReader() {
  delete impl_;
  impl_ = nullptr;
}

void USDCReader::set_reader_config(const USDCReaderConfig &config) {
  impl_->set_reader_config(config);
}

const USDCReaderConfig USDCReader::get_reader_config() const {
  return impl_->get_reader_config();
}

void USDCReader::SetProgressCallback(ProgressCallback callback, void *userptr) {
  impl_->set_progress_callback(callback, userptr);
}

bool USDCReader::ReconstructStage(Stage *stage) {
  DCOUT("Reconstruct Stage.");
  return impl_->ReconstructStage(stage);
}

bool USDCReader::get_as_layer(Layer *layer) {
  return impl_->ToLayer(layer);
}

std::string USDCReader::GetError() { return impl_->GetError(); }

std::string USDCReader::GetWarning() { return impl_->GetWarning(); }

bool USDCReader::ReadUSDC() { return impl_->ReadUSDC(); }

size_t USDCReader::GetMemoryUsage() const {
  return impl_->GetMemoryUsage();
}

USDCMemoryUsageReport USDCReader::GetMemoryUsageReport() const {
  return impl_->GetMemoryUsageReport();
}

}  // namespace usdc
}  // namespace tinyusdz

#else  // TINYUSDZ_DISABLE_MODULE_USDC_READER

namespace tinyusdz {
namespace usdc {

//
// -- Interface --
//
USDCReader::USDCReader(StreamReader *sr, const USDCReaderConfig &config) {
  (void)sr;
  (void)config;
}

USDCReader::~USDCReader() {}

void USDCReader::set_reader_config(const USDCReaderConfig &config) {
  (void)config;
}

const USDCReaderConfig USDCReader::get_reader_config() const {
  return USDCReaderConfig();
}

bool USDCReader::ReconstructStage(Stage *stage) {
  (void)stage;
  DCOUT("Reconstruct Stage.");
  return false;
}

bool USDCReader::get_as_layer(Layer *layer) {
  (void)layer;
  return false;
}

std::string USDCReader::GetError() {
  return "USDC reader feature is disabled in this build.\n";
}

std::string USDCReader::GetWarning() { return ""; }

size_t USDCReader::GetMemoryUsage() const { return 0; }

USDCMemoryUsageReport USDCReader::GetMemoryUsageReport() const {
  return USDCMemoryUsageReport();
}

}  // namespace usdc
}  // namespace tinyusdz

#endif  // TINYUSDZ_DISABLE_MODULE_USDC_READER
