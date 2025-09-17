// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Refactored Crate reader implementation using modular architecture

#include "crate-reader-refactored.hh"
#include "common-macros.inc"
#include "tiny-format.hh"

namespace tinyusdz {
namespace crate {

// Pimpl idiom to hide implementation details
class CrateReader::Impl {
public:
  Impl(StreamReader *sr, const CrateReaderConfig &config)
    : sr_(sr),
      config_(config),
      memory_manager_(config.maxMemoryBudget),
      io_helper_(std::make_unique<CrateIOHelper>(sr, memory_manager_)),
      array_reader_(std::make_unique<CrateArrayReader>(sr, memory_manager_)),
      value_unpacker_(std::make_unique<CrateValueUnpacker>(nullptr, sr, memory_manager_)),
      path_decoder_(std::make_unique<CratePathDecoder>(nullptr, memory_manager_)),
      section_reader_(std::make_unique<CrateSectionReader>(nullptr, sr, memory_manager_)) {
  }

  StreamReader *sr_;
  CrateReaderConfig config_;
  MemoryBudgetManager memory_manager_;
  
  // Modular components
  std::unique_ptr<CrateIOHelper> io_helper_;
  std::unique_ptr<CrateArrayReader> array_reader_;
  std::unique_ptr<CrateValueUnpacker> value_unpacker_;
  std::unique_ptr<CratePathDecoder> path_decoder_;
  std::unique_ptr<CrateSectionReader> section_reader_;
  
  ProgressCallback progress_callback_;
  void *progress_userptr_ = nullptr;
  
  std::string error_;
  std::string warning_;
  
  // Parsed data - these will be populated by the modules
  std::vector<Node> nodes_;
  std::vector<value::token> tokens_;
  std::vector<crate::Index> string_indices_;
  std::vector<crate::Field> fields_;
  std::vector<crate::Index> fieldset_indices_;
  std::vector<Path> paths_;
  std::vector<Path> elem_paths_;
  std::vector<crate::Spec> specs_;
  std::map<crate::Index, FieldValuePairVector> live_fieldsets_;
};

// Public interface implementation
CrateReader::CrateReader(StreamReader *sr, const CrateReaderConfig &config)
  : _sr(sr), _config(config), memory_manager_(config.maxMemoryBudget), _impl(new Impl(sr, config)) {
  InitializeModules();
}

CrateReader::~CrateReader() {
  delete _impl;
}

void CrateReader::SetProgressCallback(ProgressCallback callback, void *userptr) {
  _progress_callback = callback;
  _progress_userptr = userptr;
  _impl->progress_callback_ = callback;
  _impl->progress_userptr_ = userptr;
}

bool CrateReader::ReportProgress(float progress) {
  if (_progress_callback) {
    return _progress_callback(progress, _progress_userptr);
  }
  return true;
}

void CrateReader::InitializeModules() {
  // Initialize modules with proper cross-references
  // For now, pass nullptr for CrateReader* since modules are basic
}

void CrateReader::SyncDataFromModules() {
  // Sync data from modules to main storage
  // This will be called after each operation
}

bool CrateReader::ReadBootStrap() {
  return _impl->section_reader_->ReadBootStrap();
}

bool CrateReader::ReadTOC() {
  return _impl->section_reader_->ReadTOC();
}

bool CrateReader::ReadSection(crate::Section *s) {
  return _impl->section_reader_->ReadSection(s);
}

bool CrateReader::ReadPaths() {
  return _impl->section_reader_->ReadPaths();
}

bool CrateReader::ReadTokens() {
  return _impl->section_reader_->ReadTokens();
}

bool CrateReader::ReadStrings() {
  return _impl->section_reader_->ReadStrings();
}

bool CrateReader::ReadFields() {
  return _impl->section_reader_->ReadFields();
}

bool CrateReader::ReadFieldSets() {
  return _impl->section_reader_->ReadFieldSets();
}

bool CrateReader::ReadSpecs() {
  return _impl->section_reader_->ReadSpecs();
}

bool CrateReader::BuildLiveFieldSets() {
  return _impl->section_reader_->BuildLiveFieldSets();
}

std::string CrateReader::GetError() {
  std::string combined_error = _err;
  if (_impl->io_helper_) combined_error += _impl->io_helper_->GetError();
  if (_impl->array_reader_) combined_error += _impl->array_reader_->GetError();
  if (_impl->value_unpacker_) combined_error += _impl->value_unpacker_->GetError();
  if (_impl->path_decoder_) combined_error += _impl->path_decoder_->GetError();
  if (_impl->section_reader_) combined_error += _impl->section_reader_->GetError();
  return combined_error;
}

std::string CrateReader::GetWarning() {
  std::string combined_warning = _warn;
  if (_impl->io_helper_) combined_warning += _impl->io_helper_->GetWarning();
  if (_impl->array_reader_) combined_warning += _impl->array_reader_->GetWarning();
  if (_impl->section_reader_) combined_warning += _impl->section_reader_->GetWarning();
  return combined_warning;
}

size_t CrateReader::NumNodes() const {
  return _impl->nodes_.size();
}

const std::vector<Node> &CrateReader::GetNodes() const {
  return _impl->nodes_;
}

const std::vector<value::token> &CrateReader::GetTokens() const {
  return _impl->tokens_;
}

const std::vector<crate::Index> &CrateReader::GetStringIndices() const {
  return _impl->string_indices_;
}

const std::vector<crate::Field> &CrateReader::GetFields() const {
  return _impl->fields_;
}

const std::vector<crate::Index> &CrateReader::GetFieldsetIndices() const {
  return _impl->fieldset_indices_;
}

const std::vector<Path> &CrateReader::GetPaths() const {
  return _impl->paths_;
}

const std::vector<Path> &CrateReader::GetElemPaths() const {
  return _impl->elem_paths_;
}

const std::vector<crate::Spec> &CrateReader::GetSpecs() const {
  return _impl->specs_;
}

const std::map<crate::Index, FieldValuePairVector> &CrateReader::GetLiveFieldSets() const {
  return _impl->live_fieldsets_;
}

// TODO: Implement remaining methods from header
// These are placeholders for now
const nonstd::optional<value::token> CrateReader::GetToken(crate::Index token_index) const {
  // TODO: Implement token lookup
  return nonstd::nullopt;
}

const nonstd::optional<value::token> CrateReader::GetStringToken(crate::Index string_index) const {
  // TODO: Implement string token lookup  
  return nonstd::nullopt;
}

bool CrateReader::HasField(const std::string &key) const {
  // TODO: Implement field existence check
  return false;
}

nonstd::optional<crate::Field> CrateReader::GetField(crate::Index index) const {
  // TODO: Implement field lookup
  return nonstd::nullopt;
}

nonstd::optional<std::string> CrateReader::GetFieldString(crate::Index index) const {
  // TODO: Implement field string lookup
  return nonstd::nullopt;
}

nonstd::optional<std::string> CrateReader::GetSpecString(crate::Index index) const {
  // TODO: Implement spec string lookup
  return nonstd::nullopt;
}

size_t CrateReader::NumPaths() const {
  return _impl->paths_.size();
}

nonstd::optional<Path> CrateReader::GetPath(crate::Index index) const {
  // TODO: Implement path lookup
  return nonstd::nullopt;
}

nonstd::optional<Path> CrateReader::GetElementPath(crate::Index index) const {
  // TODO: Implement element path lookup
  return nonstd::nullopt;
}

nonstd::optional<std::string> CrateReader::GetPathString(crate::Index index) const {
  // TODO: Implement path string lookup
  return nonstd::nullopt;
}

bool CrateReader::HasFieldValuePair(const FieldValuePairVector &fvs,
                                   const std::string &name, const std::string &tyname) {
  // TODO: Implement field value pair check
  return false;
}

bool CrateReader::HasFieldValuePair(const FieldValuePairVector &fvs,
                                   const std::string &name) {
  // TODO: Implement field value pair check
  return false;
}

nonstd::expected<FieldValuePair, std::string> CrateReader::GetFieldValuePair(
    const FieldValuePairVector &fvs, const std::string &name,
    const std::string &tyname) {
  // TODO: Implement field value pair lookup
  return nonstd::make_unexpected("Not implemented");
}

nonstd::expected<FieldValuePair, std::string> CrateReader::GetFieldValuePair(
    const FieldValuePairVector &fvs, const std::string &name) {
  // TODO: Implement field value pair lookup
  return nonstd::make_unexpected("Not implemented");
}

} // namespace crate
} // namespace tinyusdz