// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Section reading operations for Crate reader
#pragma once

#include <vector>
#include <string>
#include <functional>
#include "crate-format.hh"
#include "stream-reader.hh"
#include "prim-types.hh"
#include "memory-budget.hh"

namespace tinyusdz {
namespace crate {

// Forward declaration
class CrateReader;

// Progress callback
using ProgressCallback = std::function<bool(float progress, void* userptr)>;

class CrateSectionReader {
 public:
  CrateSectionReader(CrateReader* reader, StreamReader* sr, 
                     MemoryBudget& memory_manager)
      : _reader(reader), _sr(sr), memory_manager_(memory_manager) {}

  // Main section reading functions
  bool ReadBootStrap();
  bool ReadTOC();
  bool ReadSection(crate::Section* s);
  
  // Specific section readers
  bool ReadTokens();
  bool ReadStrings();
  bool ReadFields();
  bool ReadFieldSets();
  bool ReadSpecs();
  bool ReadPaths();
  
  // Build live field sets
  bool BuildLiveFieldSets();
  
  // Progress reporting
  bool ReportProgress(float progress);
  void SetProgressCallback(ProgressCallback callback, void* userptr = nullptr) {
    _progress_callback = callback;
    _progress_userptr = userptr;
  }

  // Getters for section data
  const std::vector<value::token>& GetTokens() const { return _tokens; }
  const std::vector<Index>& GetStringIndices() const { return _string_indices; }
  const std::vector<std::string>& GetStrings() const { return _strings; }
  const std::vector<Field>& GetFields() const { return _fields; }
  const std::vector<FieldSet>& GetFieldSets() const { return _fieldsets; }
  const std::vector<Spec>& GetSpecs() const { return _specs; }
  
  // TOC access
  const crate::TOC& GetTOC() const { return _toc; }
  const crate::BootStrap& GetBootstrap() const { return _bootstrap; }
  
  // Section indices
  int64_t GetTokensIndex() const { return _tokens_index; }
  int64_t GetStringsIndex() const { return _strings_index; }
  int64_t GetFieldsIndex() const { return _fields_index; }
  int64_t GetFieldSetsIndex() const { return _fieldsets_index; }
  int64_t GetSpecsIndex() const { return _specs_index; }
  int64_t GetPathsIndex() const { return _paths_index; }

  // Error handling
  void PushError(const std::string& msg) { _err += msg + "\n"; }
  void PushWarn(const std::string& msg) { _warn += msg + "\n"; }
  std::string GetError() const { return _err; }
  std::string GetWarning() const { return _warn; }

 private:
  CrateReader* _reader;
  StreamReader* _sr;
  MemoryBudget& memory_manager_;
  
  // Progress tracking
  ProgressCallback _progress_callback;
  void* _progress_userptr = nullptr;
  
  // Section data
  crate::BootStrap _bootstrap;
  crate::TOC _toc;
  std::vector<value::token> _tokens;
  std::vector<Index> _string_indices;
  std::vector<std::string> _strings;
  std::vector<Field> _fields;
  std::vector<FieldSet> _fieldsets;
  std::vector<Spec> _specs;
  
  // Section indices
  int64_t _tokens_index = -1;
  int64_t _strings_index = -1;
  int64_t _fields_index = -1;
  int64_t _fieldsets_index = -1;
  int64_t _specs_index = -1;
  int64_t _paths_index = -1;
  
  // Error messages
  std::string _err;
  std::string _warn;
};

}  // namespace crate
}  // namespace tinyusdz