// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
#pragma once

#include <string>
#include <cstdint>

#include "prim-types.hh"

namespace tinyusdz {
namespace prim {

//
// Impelemnted in pprinter.cc
//
std::string print_references(const ReferenceList &references, const uint32_t indent);
std::string print_payload(const PayloadList &payload, const uint32_t indent);


} // namespace prim
} // namespace tinyusdz
