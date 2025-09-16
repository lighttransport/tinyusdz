// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Refactored Crate reader with modular design
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

#include "nonstd/optional.hpp"
#include "crate-format.hh"
#include "memory-budget.hh"
#include "prim-types.hh"
#include "stream-reader.hh"

// Modular components
#include "crate-io.hh"
#include "crate-array-reader.hh"
#include "crate-value-unpacker.hh"
#include "crate-path-decoder.hh"
#include "crate-section-reader.hh"

namespace tinyusdz {
namespace crate {

using ProgressCallback = std::function<bool(float progress, void *userptr)>;

struct CrateReaderConfig {
  int numThreads = -1;
  bool use_mmap = false;

  // Security limits
  size_t maxTOCSections = 32;
  size_t maxNumTokens = 1024 * 1024 * 64;
  size_t maxNumStrings = 1024 * 1024 * 64;
  size_t maxNumFields = 1024 * 1024 * 256;
  size_t maxNumFieldSets = 1024 * 1024 * 256;
  size_t maxNumSpecifiers = 1024 * 1024 * 256;
  size_t maxNumPaths = 1024 * 1024 * 256;
  size_t maxNumIndices = 1024 * 1024 * 256;
  size_t maxDictElements = 256;
  size_t maxArrayElements = 1024 * 1024 * 1024;
  size_t maxAssetPathElements = 512;
  size_t maxTokenLength = 4096;
  size_t maxStringLength = 1024 * 1024 * 64;
  size_t maxVariantsMapElements = 128;
  size_t maxValueRecursion = 16;
  size_t maxPathIndicesDecodeIteration = 1024 * 1024 * 256;
  size_t maxInts = 1024 * 1024 * 1024;
  size_t maxMemoryBudget = std::numeric_limits<int32_t>::max();
};

class CrateReader {
 public:
  // Use Node from path decoder
  using Node = crate::Node;

  CrateReader(StreamReader *sr,
              const CrateReaderConfig &config = CrateReaderConfig());
  ~CrateReader();

  // Progress callback
  void SetProgressCallback(ProgressCallback callback, void *userptr = nullptr);

  // Main reading operations delegated to modules
  bool ReadBootStrap();
  bool ReadTOC();
  bool ReadSection(crate::Section *s);
  bool ReadPaths();
  bool ReadTokens();
  bool ReadStrings();
  bool ReadFields();
  bool ReadFieldSets();
  bool ReadSpecs();
  bool BuildLiveFieldSets();

  // Error handling
  std::string GetError();
  std::string GetWarning();

  // Memory usage
  size_t GetMemoryUsageInMB() const {
    return memory_manager_.GetUsageInMB();
  }

  // Accessors for parsed data
  size_t NumNodes() const;
  const std::vector<Node> &GetNodes() const;
  const std::vector<value::token> &GetTokens() const;
  const std::vector<crate::Index> &GetStringIndices() const;
  const std::vector<crate::Field> &GetFields() const;
  const std::vector<crate::Index> &GetFieldsetIndices() const;
  const std::vector<Path> &GetPaths() const;
  const std::vector<Path> &GetElemPaths() const;
  const std::vector<crate::Spec> &GetSpecs() const;
  const std::map<crate::Index, FieldValuePairVector> &GetLiveFieldSets() const;

  // Token and string access
  const nonstd::optional<value::token> GetToken(crate::Index token_index) const;
  const nonstd::optional<value::token> GetStringToken(crate::Index string_index) const;

  // Field access
  bool HasField(const std::string &key) const;
  nonstd::optional<crate::Field> GetField(crate::Index index) const;
  nonstd::optional<std::string> GetFieldString(crate::Index index) const;
  nonstd::optional<std::string> GetSpecString(crate::Index index) const;

  // Path access
  size_t NumPaths() const;
  nonstd::optional<Path> GetPath(crate::Index index) const;
  nonstd::optional<Path> GetElementPath(crate::Index index) const;
  nonstd::optional<std::string> GetPathString(crate::Index index) const;

  // Field value pair utilities
  bool HasFieldValuePair(const FieldValuePairVector &fvs,
                        const std::string &name, const std::string &tyname);
  bool HasFieldValuePair(const FieldValuePairVector &fvs,
                        const std::string &name);
  nonstd::expected<FieldValuePair, std::string> GetFieldValuePair(
      const FieldValuePairVector &fvs, const std::string &name,
      const std::string &tyname);
  nonstd::expected<FieldValuePair, std::string> GetFieldValuePair(
      const FieldValuePairVector &fvs, const std::string &name);

  // Version check
  bool VersionGreaterThanOrEqualTo_0_8_0() const {
    if (_version[0] > 0) return true;
    if (_version[1] >= 8) return true;
    return false;
  }

  // Friend classes for module access
  friend class CrateIOHelper;
  friend class CrateArrayReader;
  friend class CrateValueUnpacker;
  friend class CratePathDecoder;
  friend class CrateSectionReader;

 private:
  // Modular components
  std::unique_ptr<CrateIOHelper> io_helper_;
  std::unique_ptr<CrateArrayReader> array_reader_;
  std::unique_ptr<CrateValueUnpacker> value_unpacker_;
  std::unique_ptr<CratePathDecoder> path_decoder_;
  std::unique_ptr<CrateSectionReader> section_reader_;

  // Core data
  StreamReader *_sr;
  CrateReaderConfig _config;
  uint8_t _version[3] = {0, 0, 0};
  
  // Memory management
  mutable MemoryBudgetManager memory_manager_;
  
  // Error messages
  mutable std::string _err;
  mutable std::string _warn;
  
  // Progress tracking
  ProgressCallback _progress_callback;
  void *_progress_userptr{nullptr};
  
  // Recursion guard
  std::unordered_set<uint64_t> unpackRecursionGuard;
  
  // Implementation details
  class Impl;
  Impl *_impl;

  // Private methods that coordinate modules
  bool ReportProgress(float progress);
  void InitializeModules();
  void SyncDataFromModules();
};

}  // namespace crate
}  // namespace tinyusdz