// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Section reading operations for Crate reader - Implementation

#include "crate-section-reader.hh"
#include "crate-reader.hh"
#include "common-macros.inc"
#include "str-util.hh"
#include "tiny-format.hh"

#define kTag "[CrateSectionReader]"

#define CHECK_MEMORY_USAGE(__nbytes) \
  MEMORY_BUDGET_CHECK(memory_manager_, (__nbytes), kTag)

#define REDUCE_MEMORY_USAGE(__nbytes) \
  memory_manager_.Release(__nbytes)

// Use existing macros from common-macros.inc

namespace tinyusdz {
namespace crate {

bool CrateSectionReader::ReadBootStrap() {
  // TODO: Implement bootstrap reading
  PushError("ReadBootStrap implementation is incomplete - TODO");
  return false;
}

bool CrateSectionReader::ReadTOC() {
  // TODO: Implement TOC (Table of Contents) reading
  PushError("ReadTOC implementation is incomplete - TODO");
  return false;
}

bool CrateSectionReader::ReadSection(crate::Section* s) {
  if (!s) {
    PushError("nullptr passed to ReadSection");
    return false;
  }
  
  // TODO: Implement section reading
  PushError("ReadSection implementation is incomplete - TODO");
  return false;
}

bool CrateSectionReader::ReadTokens() {
  // TODO: Implement token reading
  PushError("ReadTokens implementation is incomplete - TODO");
  return false;
}

bool CrateSectionReader::ReadStrings() {
  // TODO: Implement string reading
  PushError("ReadStrings implementation is incomplete - TODO");
  return false;
}

bool CrateSectionReader::ReadFields() {
  // TODO: Implement field reading
  PushError("ReadFields implementation is incomplete - TODO");
  return false;
}

bool CrateSectionReader::ReadFieldSets() {
  // TODO: Implement field set reading
  PushError("ReadFieldSets implementation is incomplete - TODO");
  return false;
}

bool CrateSectionReader::ReadSpecs() {
  // TODO: Implement spec reading
  PushError("ReadSpecs implementation is incomplete - TODO");
  return false;
}

bool CrateSectionReader::ReadPaths() {
  // TODO: Implement path reading
  PushError("ReadPaths implementation is incomplete - TODO");
  return false;
}

bool CrateSectionReader::BuildLiveFieldSets() {
  // TODO: Implement live field set building
  PushError("BuildLiveFieldSets implementation is incomplete - TODO");
  return false;
}

bool CrateSectionReader::ReportProgress(float progress) {
  if (_progress_callback) {
    return _progress_callback(progress, _progress_userptr);
  }
  return true; // Continue if no callback set
}

} // namespace crate
} // namespace tinyusdz