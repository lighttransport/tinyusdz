// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Crate scalar vector value decode helpers.

#include "crate-reader-internal.hh"

#include "safe-arithmetic.hh"

namespace tinyusdz {
namespace next {

bool CrateReader::Impl::UnpackTokenOrStringVector(ValueRep rep,
                                                  CrateTypeId type_id,
                                                  Value& out) {
  if (rep.payload() == 0) {
    out = Value::MakeTokenArray(std::vector<std::string>{});
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint64_t n = 0;
  if (!reader_->read_u64(n)) return false;
  if (n > options_.max_array_elements) return false;
  // The on-disk payload is n uint32_t indices; require them to physically fit
  // before allocating idxs(n)/data(n).
  if (!reader_->has_elements(static_cast<size_t>(n), sizeof(uint32_t))) {
    return false;
  }
  size_t count = static_cast<size_t>(n);

  std::vector<std::string> data;
  auto& idxs = array_scratch_.u32_indices;
  data.reserve(count);
  idxs.resize(count);
  if (!reader_->read_u32_array(idxs, count)) return false;

  for (size_t i = 0; i < count; i++) {
    const uint32_t idx = idxs[i];
    if (type_id == CrateTypeId::TokenVector) {
      if (idx >= tokens_.size()) return false;
      data.emplace_back(tokens_.view(idx));
    } else {
      std::string_view s;
      if (!GetString(idx, &s)) return false;
      data.emplace_back(s);
    }
  }
  out = Value::MakeTokenArray(std::move(data));
  return true;
}

bool CrateReader::Impl::UnpackDoubleVector(ValueRep rep, Value& out) {
  if (rep.payload() == 0) {
    out = Value::MakeDoubleArray(std::vector<double>{});
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint64_t n = 0;
  if (!reader_->read_u64(n)) return false;
  if (n > options_.max_array_elements) return false;
  // n doubles must physically be present in the remaining file before we
  // allocate; bound the count against the file to avoid a malformed-count
  // multi-GB allocation ahead of the read below.
  if (!reader_->has_elements(static_cast<size_t>(n), sizeof(double))) {
    return false;
  }
  std::vector<double> data(static_cast<size_t>(n));
  size_t bytes;
  if (!safe::mul(static_cast<size_t>(n), sizeof(double), &bytes)) return false;
  if (n && !reader_->read(data.data(), bytes)) return false;
  out = Value::MakeDoubleArray(std::move(data));
  return true;
}

}  // namespace next
}  // namespace tinyusdz
