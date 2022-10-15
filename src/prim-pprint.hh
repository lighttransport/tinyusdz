// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
#pragma once

#include <string>
#include <cstdint>

#include "prim-types.hh"

namespace tinyusdz {
namespace prim {

//
// Impelemnted in pprinter.cc at the moment.
//
std::string print_references(const ReferenceList &references, const uint32_t indent);
std::string print_payload(const PayloadList &payload, const uint32_t indent);
std::string print_layeroffset(const LayerOffset &layeroffset, const uint32_t indent);

std::string print_prim(const Prim &prim, const uint32_t indent=0);

} // namespace prim
} // namespace tinyusdz
