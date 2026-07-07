// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - crate writer type mapping helpers.

#pragma once

#include "crate-format.hh"
#include "../types/type-id.hh"

#include <cstddef>
#include <cstdint>

namespace tinyusdz {
namespace next {

CrateTypeId ToCrateTypeId(TypeId type_id);
uint32_t ArrayComps(TypeId type_id);
size_t CrateValueSize(CrateTypeId type, bool is_array);

}  // namespace next
}  // namespace tinyusdz
