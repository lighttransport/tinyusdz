// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Lazy array reference
//
// A LazyArrayRef points at an (undecoded) array value that still lives as bytes
// inside a retained CrateDataSource buffer. Keeping the reference instead of a
// decoded std::vector lets the read -> compose -> write pipeline move array
// values around without ever materializing their (potentially huge) payloads,
// and lets the writer copy the source block straight through when it is safe.

#pragma once

#include "crate-format.hh"      // ValueRep, CrateTypeId
#include "../types/type-id.hh"  // next::TypeId

#include <cstdint>
#include <memory>

namespace tinyusdz {
namespace next {

class CrateDataSource;

/// Lightweight descriptor for an array value stored in a retained crate buffer.
/// The on-disk block is `[u64 count][data...]` at `block_offset`.
struct LazyArrayRef {
  std::shared_ptr<CrateDataSource> source;  // keeps the backing buffer alive
  ValueRep rep;                             // original on-disk rep (flags+payload)
  uint64_t block_offset = 0;   // absolute offset of the block (0 => empty array)
  uint64_t block_len = 0;      // total block bytes for verbatim copy (0 => unknown)
  uint64_t element_count = 0;  // logical element count
  uint32_t src_elem_stride = 0;  // bytes per element as stored on disk
  CrateTypeId crate_type = CrateTypeId::Invalid;  // exact on-disk type
  TypeId value_type = TypeId::Invalid;  // next TypeId materialize() would produce
  bool is_compressed = false;  // convenience copy of rep.is_compressed()
};

/// Probe only the block header (element count + layout) of an array ValueRep,
/// without materializing the payload. `max_elements` guards against a malformed
/// count. Returns false on a malformed/out-of-bounds header.
bool ProbeArrayBlock(const std::shared_ptr<CrateDataSource>& source, ValueRep rep,
                     size_t max_elements, LazyArrayRef* out);

}  // namespace next
}  // namespace tinyusdz
