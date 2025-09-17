// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Stage and layer reconstruction for USDC reader
// Extracted from usdc-reader.cc

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include "value-types.hh"
#include "prim-types.hh"
#include "stage.hh"
#include "layer.hh"
#include "crate-reader.hh"
#include "stream-reader.hh"

namespace tinyusdz {
namespace usdc {

// Forward declarations
class PropertyParser;
class VariantReader;
class PrimReconstructor;
struct PropertyMap;

// Stage reader configuration
struct StageReaderConfig {
  // Memory limits
  size_t maxMemoryMB = 2048;  // 2GB default
  size_t maxPrims = 1000000;  // 1M prims
  size_t maxProperties = 10000000;  // 10M properties
  
  // Reading options
  bool loadPayloads = true;
  bool loadReferences = true;
  bool resolveVariants = true;
  bool populateValueBlocks = true;
  
  // Performance options
  bool lazyLoadProperties = false;
  bool cacheReferences = true;
  size_t numThreads = 1;
  
  // Validation
  bool strictValidation = false;
  bool validateSchemas = false;
};

// Stage reconstruction context
class StageReconstructionContext {
public:
  StageReconstructionContext(const StageReaderConfig &config)
      : config_(config) {}
  
  // Path tracking
  void PushPath(const Path &path);
  void PopPath();
  Path GetCurrentPath() const;
  std::vector<Path> GetPathStack() const { return pathStack_; }
  
  // Reference tracking (for cycle detection)
  bool AddReference(const Path &refPath);
  bool HasReference(const Path &refPath) const;
  void RemoveReference(const Path &refPath);
  
  // Memory tracking
  void AddMemoryUsage(size_t bytes);
  size_t GetMemoryUsage() const { return memoryUsage_; }
  bool CheckMemoryLimit() const;
  
  // Error/warning collection
  void AddError(const std::string &error);
  void AddWarning(const std::string &warning);
  
  const std::vector<std::string>& GetErrors() const { return errors_; }
  const std::vector<std::string>& GetWarnings() const { return warnings_; }
  
  bool HasErrors() const { return !errors_.empty(); }
  bool HasWarnings() const { return !warnings_.empty(); }
  
  // Statistics
  void IncrementPrimCount() { primCount_++; }
  void IncrementPropertyCount() { propertyCount_++; }
  
  size_t GetPrimCount() const { return primCount_; }
  size_t GetPropertyCount() const { return propertyCount_; }
  
private:
  StageReaderConfig config_;
  std::vector<Path> pathStack_;
  std::set<Path> references_;
  size_t memoryUsage_ = 0;
  size_t primCount_ = 0;
  size_t propertyCount_ = 0;
  std::vector<std::string> errors_;
  std::vector<std::string> warnings_;
};

// Main stage reader class
class StageReader {
public:
  StageReader(StreamReader *sr, const StageReaderConfig &config = {});
  ~StageReader();
  
  // Read and reconstruct stage
  bool ReadStage(Stage *stage, std::string *warn, std::string *err);
  
  // Read and reconstruct layer
  bool ReadLayer(Layer *layer, std::string *warn, std::string *err);
  
  // Get reader statistics
  struct Statistics {
    size_t primsRead = 0;
    size_t propertiesRead = 0;
    size_t variantsResolved = 0;
    size_t referencesResolved = 0;
    size_t memoryUsed = 0;
    double readTimeSeconds = 0.0;
  };
  
  Statistics GetStatistics() const { return stats_; }
  
private:
  // Private implementation
  class Impl;
  std::unique_ptr<Impl> impl_;
  
  Statistics stats_;
  StageReaderConfig config_;
};

// Stage reconstruction utilities
namespace stage_utils {

// Metadata reconstruction
struct StageMeta {
  std::string comment;
  std::string documentation;
  std::string defaultPrim;
  double startTimeCode = 0.0;
  double endTimeCode = 0.0;
  double timeCodesPerSecond = 24.0;
  double framesPerSecond = 24.0;
  value::token upAxis = value::token("Y");
  double metersPerUnit = 1.0;
  value::dict customData;
};

bool ReconstructStageMeta(
    const crate::FieldValuePairVector &fields,
    StageMeta &meta,
    std::string *err);

// Layer metadata
struct LayerMeta {
  std::string identifier;
  std::string comment;
  std::string documentation;
  value::dict customData;
  std::vector<std::string> subLayers;
  std::map<std::string, std::string> subLayerOffsets;
};

bool ReconstructLayerMeta(
    const crate::FieldValuePairVector &fields,
    LayerMeta &meta,
    std::string *err);

// Prim tree reconstruction
struct PrimNode {
  int32_t index;
  int32_t parent;
  std::string name;
  std::string typeName;
  Specifier specifier;
  std::vector<int32_t> children;
  PropertyMap properties;
  bool hasVariants = false;
};

bool BuildPrimTree(
    const crate::CrateReader &reader,
    std::vector<PrimNode> &nodes,
    std::string *err);

// Recursive prim reconstruction
bool ReconstructPrimRecursively(
    int parentId,
    int currentId,
    Prim *rootPrim,
    const std::vector<PrimNode> &nodes,
    StageReconstructionContext &context);

bool ReconstructPrimSpecRecursively(
    int parentId,
    int currentId,
    PrimSpec *rootPrimSpec,
    const std::vector<PrimNode> &nodes,
    StageReconstructionContext &context);

// Path index mapping
using PathIndexToSpecIndexMap = std::map<uint32_t, uint32_t>;

bool BuildPathIndexMap(
    const crate::CrateReader &reader,
    PathIndexToSpecIndexMap &map,
    std::string *err);

// Composition helpers
bool ResolveStageComposition(
    Stage &stage,
    const std::string &baseDir,
    StageReconstructionContext &context);

bool ResolveLayerComposition(
    Layer &layer,
    const std::string &baseDir,
    StageReconstructionContext &context);

// Reference resolution
struct ReferenceResolver {
  std::function<bool(const std::string &assetPath, Layer &layer)> loadLayer;
  std::map<std::string, std::shared_ptr<Layer>> cache;
  
  bool ResolveReference(
      const Reference &ref,
      Prim &prim,
      const std::string &baseDir,
      StageReconstructionContext &context);
};

// Payload resolution
struct PayloadResolver {
  std::function<bool(const std::string &assetPath, Layer &layer)> loadLayer;
  bool deferLoading = false;
  
  bool ResolvePayload(
      const Payload &payload,
      Prim &prim,
      const std::string &baseDir,
      StageReconstructionContext &context);
};

// Stage validation
struct StageValidationResult {
  bool valid = true;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
  
  struct MissingReference {
    Path primPath;
    std::string assetPath;
  };
  std::vector<MissingReference> missingReferences;
  
  struct InvalidPrim {
    Path path;
    std::string reason;
  };
  std::vector<InvalidPrim> invalidPrims;
};

StageValidationResult ValidateStage(const Stage &stage);
StageValidationResult ValidateLayer(const Layer &layer);

// Debug utilities
std::string DumpStageStructure(const Stage &stage, int maxDepth = -1);
std::string DumpLayerStructure(const Layer &layer, int maxDepth = -1);

// Stage optimization
struct StageOptimizationOptions {
  bool flattenReferences = false;
  bool mergeRedundantPrims = false;
  bool removeEmptyPrims = false;
  bool optimizeProperties = false;
};

void OptimizeStage(Stage &stage, const StageOptimizationOptions &options);

} // namespace stage_utils

} // namespace usdc
} // namespace tinyusdz