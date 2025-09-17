// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Variant set and variant handling for USDC reader
// Extracted from usdc-reader.cc

#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include "value-types.hh"
#include "prim-types.hh"
#include "crate-reader.hh"

namespace tinyusdz {
namespace usdc {

// Forward declarations
class Prim;
class PrimSpec;
struct PropertyMap;

// Variant-related structures
struct VariantSpec {
  std::string name;
  PrimSpec primSpec;
  PropertyMap properties;
  std::vector<Path> children;
};

struct VariantSetSpec {
  std::string name;
  std::map<std::string, VariantSpec> variants;
  std::string defaultVariant;
};

// Variant selection state
struct VariantSelection {
  std::string variantSetName;
  std::string variantName;
  bool isExplicit = false;  // User-selected vs default
};

// Variant reader configuration
struct VariantReaderConfig {
  bool resolveVariants = true;
  bool allowNestedVariants = true;
  bool strictVariantValidation = false;
  size_t maxVariantDepth = 64;
};

// Main variant reader class
class VariantReader {
public:
  VariantReader(const crate::CrateReader &reader,
               const VariantReaderConfig &config = {});
  
  // Parse variant set fields
  bool ParseVariantSetFields(
      const crate::CrateReader::VariantSpec &spec,
      const crate::FieldValuePairVector &fields,
      VariantSetSpec &variantSet,
      std::string *err);
  
  // Parse individual variant
  bool ParseVariant(
      const std::string &variantName,
      const crate::FieldValuePairVector &fields,
      VariantSpec &variant,
      std::string *err);
  
  // Build variant map for a prim
  bool BuildVariantMap(
      int32_t primIdx,
      const crate::CrateReader &reader,
      std::map<std::string, VariantSetSpec> &variantSets,
      std::string *err);
  
  // Apply variants to prim
  bool ApplyVariants(
      Prim &prim,
      const std::map<std::string, VariantSetSpec> &variantSets,
      const std::vector<VariantSelection> &selections,
      std::string *err);
  
  // Resolve variant selections
  std::vector<VariantSelection> ResolveVariantSelections(
      const Prim &prim,
      const std::map<std::string, std::string> &userSelections = {});
  
  // Check if prim has variants
  bool HasVariants(int32_t primIdx) const;
  
  // Get variant prim indices
  std::vector<int32_t> GetVariantPrimIndices(int32_t parentIdx) const;
  
private:
  const crate::CrateReader &reader_;
  VariantReaderConfig config_;
  
  // Variant tracking
  std::map<int32_t, std::set<int32_t>> variantChildren_;  // parent -> variant children
  std::map<int32_t, std::string> variantNames_;  // prim idx -> variant name
  std::set<int32_t> variantPrims_;  // All variant prim indices
  
  // Helper methods
  bool AddVariantToPrimNode(int32_t primIdx, const value::Value &variant);
  bool AddVariantChildrenToPrimNode(
      const std::string &variantName,
      const crate::CrateReader::Node &primNode,
      std::map<std::string, VariantChildrenNodes> &variantChildrenNodes);
  
  bool MergeVariantProperties(
      const PropertyMap &variantProps,
      PropertyMap &primProps);
};

// Variant utilities
namespace variant_utils {

// Variant selection helpers
struct VariantSelectionRule {
  enum Type {
    Explicit,      // User-specified selection
    Default,       // Use variant set default
    First,         // Select first variant
    Last,          // Select last variant
    Pattern,       // Match by pattern
    Conditional    // Conditional selection based on expression
  };
  
  Type type = Default;
  std::string pattern;
  std::function<bool(const std::string&)> condition;
};

VariantSelection SelectVariant(
    const VariantSetSpec &variantSet,
    const VariantSelectionRule &rule);

// Variant composition
struct VariantCompositionOptions {
  bool mergeProperties = true;
  bool overrideChildren = false;
  bool appendMetadata = true;
};

Prim ComposeVariant(
    const Prim &basePrim,
    const VariantSpec &variant,
    const VariantCompositionOptions &options = {});

// Variant validation
struct VariantValidationResult {
  bool valid = true;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
  
  struct CycleInfo {
    std::vector<std::string> path;
    std::string message;
  };
  std::vector<CycleInfo> cycles;
};

VariantValidationResult ValidateVariants(
    const std::map<std::string, VariantSetSpec> &variantSets);

bool CheckVariantCycles(
    const std::map<std::string, VariantSetSpec> &variantSets,
    std::vector<std::vector<std::string>> &cycles);

// Variant querying
std::vector<std::string> GetVariantSetNames(const Prim &prim);
std::vector<std::string> GetVariantNames(const Prim &prim, const std::string &setName);
std::string GetSelectedVariant(const Prim &prim, const std::string &setName);

bool HasVariantSet(const Prim &prim, const std::string &setName);
bool HasVariant(const Prim &prim, const std::string &setName, const std::string &variantName);

// Variant path utilities
Path GetVariantPath(const Path &primPath, const std::string &setName, const std::string &variantName);
bool IsVariantPath(const Path &path);
std::pair<std::string, std::string> ParseVariantPath(const Path &path);

// Variant debugging
std::string DumpVariantSet(const VariantSetSpec &variantSet, int indent = 0);
std::string DumpVariantSelections(const std::vector<VariantSelection> &selections);

// Variant flattening (for optimization)
struct FlattenedVariant {
  std::string key;  // "setName:variantName"
  Prim prim;
  PropertyMap properties;
};

std::vector<FlattenedVariant> FlattenVariants(
    const Prim &prim,
    const std::map<std::string, VariantSetSpec> &variantSets);

// Variant caching for performance
class VariantCache {
public:
  void CacheVariant(const std::string &key, const Prim &prim);
  bool GetCachedVariant(const std::string &key, Prim &prim) const;
  void Clear();
  size_t Size() const { return cache_.size(); }
  
private:
  std::map<std::string, std::shared_ptr<Prim>> cache_;
};

} // namespace variant_utils

} // namespace usdc
} // namespace tinyusdz