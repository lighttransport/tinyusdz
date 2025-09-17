// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Refactored USDC reader implementation
// Main interface that delegates to specialized modules

#include "usdc-reader.hh"
#include "usdc-stage-reader.hh"
#include "usdc-property-reader.hh"
#include "usdc-prim-reconstruct.hh"
#include "usdc-variant-reader.hh"

#include "crate-reader.hh"
#include "stream-reader.hh"
#include "str-util.hh"
#include "path-util.hh"
#include "tiny-format.hh"
#include "common-macros.hh"

#include <chrono>
#include <algorithm>
#include <iostream>

#ifdef TINYUSDZ_ENABLE_THREAD
#include <thread>
#include <mutex>
#endif

namespace tinyusdz {

// Implementation class definition
class USDCReader::Impl {
public:
  Impl(StreamReader *sr, const USDCReaderConfig &config);
  ~Impl();
  
  bool ReadUSDC();
  
  // Main read methods
  bool ReadStage(Stage *stage);
  bool ReadLayer(Layer *layer);
  
  // Error reporting
  std::string GetError() const { return _err; }
  std::string GetWarning() const { return _warn; }
  
private:
  // Core components
  StreamReader *_sr;
  std::unique_ptr<crate::CrateReader> _crateReader;
  std::unique_ptr<usdc::StageReader> _stageReader;
  std::unique_ptr<usdc::PropertyParser> _propertyParser;
  std::unique_ptr<usdc::VariantReader> _variantReader;
  
  // Configuration
  USDCReaderConfig _config;
  usdc::StageReaderConfig _stageConfig;
  usdc::PropertyParseConfig _propertyConfig;
  usdc::VariantReaderConfig _variantConfig;
  
  // State
  std::string _err;
  std::string _warn;
  std::string _base_dir;
  
  // Initialize components
  bool Initialize();
  bool InitializeCrateReader();
  void InitializeReconstructors();
  
  // Convert configs
  void SetupConfigurations();
};

// Constructor
USDCReader::USDCReader(StreamReader *sr, const USDCReaderConfig &config) 
    : _impl(std::make_unique<Impl>(sr, config)) {
}

USDCReader::~USDCReader() = default;

// Implementation constructor
USDCReader::Impl::Impl(StreamReader *sr, const USDCReaderConfig &config)
    : _sr(sr), _config(config) {
  SetupConfigurations();
  InitializeReconstructors();
}

USDCReader::Impl::~Impl() = default;

// Setup configurations
void USDCReader::Impl::SetupConfigurations() {
  // Setup stage reader config
  _stageConfig.maxMemoryMB = _config.max_memory_limit_in_mb;
  _stageConfig.maxPrims = _config.max_num_prims;
  _stageConfig.loadPayloads = _config.load_payloads;
  _stageConfig.loadReferences = _config.load_references;
  _stageConfig.resolveVariants = true;
  _stageConfig.numThreads = _config.num_threads;
  
  // Setup property parser config
  _propertyConfig.allow_custom_properties = _config.allow_custom_prims;
  _propertyConfig.strict_type_checking = _config.strict_validation;
  _propertyConfig.max_array_size = _config.max_array_elements;
  
  // Setup variant reader config
  _variantConfig.resolveVariants = true;
  _variantConfig.allowNestedVariants = true;
  _variantConfig.maxVariantDepth = _config.max_depth;
}

// Initialize reconstructors
void USDCReader::Impl::InitializeReconstructors() {
  auto& factory = usdc::PrimReconstructorFactory::GetInstance();
  factory.InitializeDefaults();
  
  // Register custom reconstructors if needed
  // factory.RegisterReconstructor("CustomType", std::make_shared<CustomReconstructor>());
}

// Initialize crate reader
bool USDCReader::Impl::InitializeCrateReader() {
  if (_crateReader) {
    return true;
  }
  
  if (!_sr) {
    _err = "StreamReader is null";
    return false;
  }
  
  // Get base directory from stream
  if (_sr->tell() == 0) {
    // Assume we're at the beginning
    _base_dir = _config.base_dir;
    if (_base_dir.empty()) {
      _base_dir = "./";
    }
  }
  
  // Create crate reader
  _crateReader = std::make_unique<crate::CrateReader>(_sr);
  
  // Initialize crate reader
  std::string warn, err;
  if (!_crateReader->ReadBootStrap(&warn, &err)) {
    _err = "Failed to read Crate bootstrap: " + err;
    _warn = warn;
    return false;
  }
  
  if (!_crateReader->ReadTOC(&warn, &err)) {
    _err = "Failed to read Crate TOC: " + err;
    _warn = warn;
    return false;
  }
  
  if (!_crateReader->BuildLiveFieldSets(&warn, &err)) {
    _err = "Failed to build live field sets: " + err;
    _warn = warn;
    return false;
  }
  
  return true;
}

// Main initialization
bool USDCReader::Impl::Initialize() {
  if (!InitializeCrateReader()) {
    return false;
  }
  
  // Create component parsers
  _propertyParser = std::make_unique<usdc::PropertyParser>(
      *_crateReader, _propertyConfig);
  
  _variantReader = std::make_unique<usdc::VariantReader>(
      *_crateReader, _variantConfig);
  
  _stageReader = std::make_unique<usdc::StageReader>(
      _sr, _stageConfig);
  
  return true;
}

// Read USDC
bool USDCReader::Impl::ReadUSDC() {
  auto start_time = std::chrono::high_resolution_clock::now();
  
  if (!Initialize()) {
    return false;
  }
  
  // Read all sections
  std::string warn, err;
  if (!_crateReader->ReadAllSections(&warn, &err)) {
    _err = "Failed to read Crate sections: " + err;
    _warn += warn;
    return false;
  }
  
  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> diff = end_time - start_time;
  
  if (_config.debug_print) {
    std::cout << "USDC read time: " << diff.count() << " seconds\n";
  }
  
  return true;
}

// Read Stage
bool USDCReader::Impl::ReadStage(Stage *stage) {
  if (!stage) {
    _err = "Stage pointer is null";
    return false;
  }
  
  if (!ReadUSDC()) {
    return false;
  }
  
  // Use stage reader to reconstruct
  std::string warn, err;
  if (!_stageReader->ReadStage(stage, &warn, &err)) {
    _err = err;
    _warn = warn;
    return false;
  }
  
  // Apply post-processing if needed
  if (_config.validate_stage) {
    auto result = usdc::stage_utils::ValidateStage(*stage);
    if (!result.valid) {
      _err = "Stage validation failed:\n";
      for (const auto& e : result.errors) {
        _err += "  - " + e + "\n";
      }
      for (const auto& w : result.warnings) {
        _warn += "  - " + w + "\n";
      }
      return false;
    }
  }
  
  return true;
}

// Read Layer
bool USDCReader::Impl::ReadLayer(Layer *layer) {
  if (!layer) {
    _err = "Layer pointer is null";
    return false;
  }
  
  if (!ReadUSDC()) {
    return false;
  }
  
  // Use stage reader to reconstruct layer
  std::string warn, err;
  if (!_stageReader->ReadLayer(layer, &warn, &err)) {
    _err = err;
    _warn = warn;
    return false;
  }
  
  return true;
}

// Public interface methods
bool USDCReader::ReadStage(Stage *stage) {
  return _impl->ReadStage(stage);
}

bool USDCReader::ReadLayer(Layer *layer) {
  return _impl->ReadLayer(layer);
}

bool USDCReader::ReconstructStage(Stage *stage) {
  return ReadStage(stage);
}

bool USDCReader::ReconstructLayer(Layer *layer) {
  return ReadLayer(layer);
}

std::string USDCReader::GetError() const {
  return _impl->GetError();
}

std::string USDCReader::GetWarning() const {
  return _impl->GetWarning();
}

// Factory functions
bool LoadUSDCFromMemory(const uint8_t *data, size_t size, Stage *stage,
                       std::string *warn, std::string *err,
                       const USDCReaderConfig &config) {
  if (!data || !stage) {
    if (err) *err = "Invalid input parameters";
    return false;
  }
  
  MemoryStreamReader sr(data, size);
  USDCReader reader(&sr, config);
  
  if (!reader.ReadStage(stage)) {
    if (err) *err = reader.GetError();
    if (warn) *warn = reader.GetWarning();
    return false;
  }
  
  if (warn) *warn = reader.GetWarning();
  return true;
}

bool LoadUSDCLayerFromMemory(const uint8_t *data, size_t size, Layer *layer,
                            std::string *warn, std::string *err,
                            const USDCReaderConfig &config) {
  if (!data || !layer) {
    if (err) *err = "Invalid input parameters";
    return false;
  }
  
  MemoryStreamReader sr(data, size);
  USDCReader reader(&sr, config);
  
  if (!reader.ReadLayer(layer)) {
    if (err) *err = reader.GetError();
    if (warn) *warn = reader.GetWarning();
    return false;
  }
  
  if (warn) *warn = reader.GetWarning();
  return true;
}

bool LoadUSDCFromFile(const std::string &filename, Stage *stage,
                     std::string *warn, std::string *err,
                     const USDCReaderConfig &config) {
  if (filename.empty() || !stage) {
    if (err) *err = "Invalid input parameters";
    return false;
  }
  
  std::ifstream ifs(filename, std::ios::binary);
  if (!ifs.is_open()) {
    if (err) *err = "Failed to open file: " + filename;
    return false;
  }
  
  // Get file size
  ifs.seekg(0, std::ios::end);
  size_t file_size = ifs.tellg();
  ifs.seekg(0, std::ios::beg);
  
  // Read file into memory
  std::vector<uint8_t> data(file_size);
  ifs.read(reinterpret_cast<char*>(data.data()), file_size);
  
  if (!ifs.good()) {
    if (err) *err = "Failed to read file: " + filename;
    return false;
  }
  
  // Set base directory
  USDCReaderConfig local_config = config;
  local_config.base_dir = io::GetBaseDir(filename);
  
  return LoadUSDCFromMemory(data.data(), data.size(), stage, warn, err, local_config);
}

} // namespace tinyusdz