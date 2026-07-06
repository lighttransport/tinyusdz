// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Crate Reader decode/allocation helpers

#include "crate-reader-internal.hh"
#include "safe-arithmetic.hh"
#include "../prim/path.hh"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tinyusdz {
namespace next {

bool CrateReader::Impl::ResolveFieldset(uint32_t fieldset_index,
                                        std::vector<std::pair<std::string, Value>>& out) {
  if (fieldset_indices_.empty() || fieldset_index >= fieldset_indices_.size()) {
    return false;
  }
  if (fieldset_index >= fieldset_index_to_id_.size()) {
    return false;
  }
  const uint32_t fi32 = fieldset_index_to_id_[fieldset_index];
  if (fi32 == 0xFFFFFFFFu) {
    return false;
  }
  const size_t fi = static_cast<size_t>(fi32);
  if (fi >= fieldset_counts_.size()) {
    return false;
  }
  const size_t start = static_cast<size_t>(fieldset_offsets_[fi]);
  const size_t count = static_cast<size_t>(fieldset_counts_[fi]);
  if (start >= fieldset_indices_.size() ||
      count > fieldset_indices_.size() - start) {
    return false;
  }

  out.clear();
  if (out.capacity() < count) out.reserve(count);
  for (size_t idx = start; idx < start + count; ++idx) {
    const uint32_t field_idx = fieldset_indices_[idx];
    if (field_idx >= fields_.size()) continue;

    const CrateField& field = fields_[field_idx];
    std::string_view name;
    if (!GetToken(field.token_index.value, &name)) continue;

    Value value;
    if (!UnpackValue(field.value_rep, value)) continue;
    out.emplace_back(name, std::move(value));
  }
  return true;
}

bool CrateReader::Impl::ResolveFieldset(
    uint32_t fieldset_index, std::vector<std::pair<std::string_view, Value>>& out) {
  if (fieldset_indices_.empty() || fieldset_index >= fieldset_indices_.size()) {
    return false;
  }

  if (fieldset_index >= fieldset_index_to_id_.size()) {
    return false;
  }
  const uint32_t fi32 = fieldset_index_to_id_[fieldset_index];
  if (fi32 == 0xFFFFFFFFu) {
    return false;
  }
  const size_t fi = static_cast<size_t>(fi32);
  if (fi >= fieldset_counts_.size()) {
    return false;
  }
  const size_t start = static_cast<size_t>(fieldset_offsets_[fi]);
  const size_t count = static_cast<size_t>(fieldset_counts_[fi]);
  if (start >= fieldset_indices_.size() ||
      count > fieldset_indices_.size() - start) {
    return false;
  }

  out.clear();
  if (out.capacity() < count) out.reserve(count);

  for (size_t idx = start; idx < start + count; ++idx) {
    uint32_t field_idx = fieldset_indices_[idx];

    if (field_idx < fields_.size()) {
      const CrateField& field = fields_[field_idx];

      std::string_view name;
      if (GetToken(field.token_index.value, &name)) {
        Value value;
        if (UnpackValue(field.value_rep, value)) {
          out.emplace_back(name, std::move(value));
        }
      }
    }
  }

  return true;
}

bool CrateReader::Impl::ResolveFieldsetRaw(
    uint32_t fieldset_index,
    std::vector<std::pair<std::string, ValueRep>>& out) {
  if (fieldset_indices_.empty() || fieldset_index >= fieldset_indices_.size()) return false;
  if (fieldset_index >= fieldset_index_to_id_.size()) {
    return false;
  }
  const uint32_t fi32 = fieldset_index_to_id_[fieldset_index];
  if (fi32 == 0xFFFFFFFFu) {
    return false;
  }
  const size_t fi = static_cast<size_t>(fi32);
  if (fi >= fieldset_counts_.size()) {
    return false;
  }
  const size_t start = static_cast<size_t>(fieldset_offsets_[fi]);
  const size_t count = static_cast<size_t>(fieldset_counts_[fi]);
  if (start >= fieldset_indices_.size() ||
      count > fieldset_indices_.size() - start) {
    return false;
  }

  out.clear();
  if (out.capacity() < count) out.reserve(count);
  for (size_t idx = start; idx < start + count; ++idx) {
    uint32_t field_idx = fieldset_indices_[idx];
    if (field_idx >= fields_.size()) continue;
    const CrateField& field = fields_[field_idx];
    std::string_view name;
    if (!GetToken(field.token_index.value, &name)) continue;
    out.emplace_back(name, field.value_rep);
  }
  return true;
}

bool CrateReader::Impl::ResolveFieldsetRaw(
    uint32_t fieldset_index,
    std::vector<std::pair<std::string_view, ValueRep>>& out) {
  if (fieldset_indices_.empty() || fieldset_index >= fieldset_indices_.size()) return false;

  if (fieldset_index >= fieldset_index_to_id_.size()) {
    return false;
  }
  const uint32_t fi32 = fieldset_index_to_id_[fieldset_index];
  if (fi32 == 0xFFFFFFFFu) {
    return false;
  }
  const size_t fi = static_cast<size_t>(fi32);
  if (fi >= fieldset_counts_.size()) {
    return false;
  }
  const size_t start = static_cast<size_t>(fieldset_offsets_[fi]);
  const size_t count = static_cast<size_t>(fieldset_counts_[fi]);
  if (start >= fieldset_indices_.size() ||
      count > fieldset_indices_.size() - start) {
    return false;
  }

  out.clear();
  if (out.capacity() < count) out.reserve(count);

  for (size_t idx = start; idx < start + count; ++idx) {
    uint32_t field_idx = fieldset_indices_[idx];
    if (field_idx < fields_.size()) {
      const CrateField& field = fields_[field_idx];
      std::string_view name;
      if (GetToken(field.token_index.value, &name)) {
        out.emplace_back(name, field.value_rep);
      }
    }
  }
  return true;
}

bool CrateReader::Impl::DecodePathTargets(ValueRep rep,
                                          std::vector<std::string>& out) {
  // Reads one or more [u64 count][u32 path_index * count] runs of path indices
  // (uncompressed, per pxrUSD). PathVector has a single run; PathListOp is
  // prefixed by a 1-byte ListOpHeader selecting which sublists are present. We
  // flatten every present sublist's targets (sufficient for round-tripping
  // relationship/connection targets; list-edit semantics are not preserved).
  CrateTypeId tid = rep.type_id();
  if (tid != CrateTypeId::PathVector && tid != CrateTypeId::PathListOp) {
    return false;
  }
  if (rep.payload() == 0) return true;  // empty
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;

  auto read_run = [&]() -> bool {
    uint64_t n = 0;
    if (!reader()->read_u64(n)) return false;
    if (n > options_.max_array_elements) return false;
    out.reserve(out.size() + static_cast<size_t>(n));
    auto& idxs = array_scratch().u32_indices;
    idxs.resize(static_cast<size_t>(n));
    if (!reader()->read_u32_array(idxs, static_cast<size_t>(n))) return false;
    for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
      const uint32_t idx = idxs[i];
      if (idx < paths_.size()) {
        // paths_ renders a property path as ".<primpath>/<prop>"; convert to the
        // canonical USD form "<primpath>.<prop>" so targets re-intern correctly
        // (and survive repeated round-trips). Prim targets pass through as-is.
        const std::string_view p = paths_.view(idx);
        if (!p.empty() && p[0] == '.') {
          std::string_view body = p.substr(1);  // "/a/b/prop"
          const size_t slash = body.rfind('/');
          if (slash != std::string_view::npos) {
            std::string s;
            s.reserve(body.size());
            s.append(body.data(), slash);
            s.push_back('.');
            s.append(body.data() + slash + 1, body.size() - slash - 1);
            out.push_back(std::move(s));
          } else {
            out.emplace_back(body);
          }
        } else {
          out.emplace_back(p);  // prim path "/a/b"
        }
      }
    }
    return true;
  };

  if (tid == CrateTypeId::PathVector) {
    return read_run();
  }

  // PathListOp: [u8 header][present sublists...]. ListOpHeader bits (pxrUSD):
  //   0x02 explicit, 0x04 added, 0x08 deleted, 0x10 ordered,
  //   0x20 prepended, 0x40 appended. Read order matches the legacy reader:
  //   explicit, added, prepended, appended, deleted, ordered.
  uint8_t bits = 0;
  if (!reader()->read_u8(bits)) return false;
  const uint8_t kHasExplicit = 0x02, kHasAdded = 0x04, kHasDeleted = 0x08,
                kHasOrdered = 0x10, kHasPrepended = 0x20, kHasAppended = 0x40;
  if ((bits & kHasExplicit) && !read_run()) return false;
  if ((bits & kHasAdded) && !read_run()) return false;
  if ((bits & kHasPrepended) && !read_run()) return false;
  if ((bits & kHasAppended) && !read_run()) return false;
  if ((bits & kHasDeleted) && !read_run()) return false;
  if ((bits & kHasOrdered) && !read_run()) return false;
  return true;
}

bool CrateReader::Impl::DecodePathTargets(ValueRep rep,
                                          std::vector<Path>& out) {
  // Reads one or more [u64 count][u32 path_index * count] runs of path indices
  // (uncompressed, per pxrUSD). PathVector has a single run; PathListOp is
  // prefixed by a 1-byte ListOpHeader selecting which sublists are present. We
  // flatten every present sublist's targets (sufficient for round-tripping
  // relationship/connection targets; list-edit semantics are not preserved).
  CrateTypeId tid = rep.type_id();
  if (tid != CrateTypeId::PathVector && tid != CrateTypeId::PathListOp) {
    return false;
  }
  if (rep.payload() == 0) return true;  // empty
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;

  auto append_path_index = [&](uint32_t idx) -> bool {
    if (idx >= paths_.size()) {
      return true;
    }

    const std::string_view p = paths_.view(idx);
    if (!p.empty() && p[0] == '.') {
      std::string_view body = p.substr(1);  // "/a/b/prop"
      const size_t slash = body.rfind('/');
      if (slash != std::string_view::npos) {
        std::string s;
        s.reserve(body.size());
        s.append(body.data(), slash);
        s.push_back('.');
        s.append(body.data() + slash + 1, body.size() - slash - 1);
        out.emplace_back(std::move(s));
      } else {
        out.emplace_back(std::string(body));
      }
      return true;
    }

    out.emplace_back(std::string(p));
    return true;
  };

  auto read_run = [&]() -> bool {
    uint64_t n = 0;
    if (!reader()->read_u64(n)) return false;
    if (n > options_.max_array_elements) return false;
    out.reserve(out.size() + static_cast<size_t>(n));
    auto& idxs = array_scratch().u32_indices;
    idxs.resize(static_cast<size_t>(n));
    if (!reader()->read_u32_array(idxs, static_cast<size_t>(n))) return false;
    for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
      if (!append_path_index(idxs[i])) return false;
    }
    return true;
  };

  if (tid == CrateTypeId::PathVector) {
    return read_run();
  }

  // PathListOp: [u8 header][present sublists...]. ListOpHeader bits (pxrUSD):
  //   0x02 explicit, 0x04 added, 0x08 deleted, 0x10 ordered,
  //   0x20 prepended, 0x40 appended. Read order matches the legacy reader:
  //   explicit, added, prepended, appended, deleted, ordered.
  uint8_t bits = 0;
  if (!reader()->read_u8(bits)) return false;
  const uint8_t kHasExplicit = 0x02, kHasAdded = 0x04, kHasDeleted = 0x08,
                kHasOrdered = 0x10, kHasPrepended = 0x20, kHasAppended = 0x40;
  if ((bits & kHasExplicit) && !read_run()) return false;
  if ((bits & kHasAdded) && !read_run()) return false;
  if ((bits & kHasPrepended) && !read_run()) return false;
  if ((bits & kHasAppended) && !read_run()) return false;
  if ((bits & kHasDeleted) && !read_run()) return false;
  if ((bits & kHasOrdered) && !read_run()) return false;
  return true;
}

bool CrateReader::Impl::DecodeReferenceListOp(ValueRep rep, bool is_payload,
                                              std::vector<std::string>& out) {
  const CrateTypeId tid = rep.type_id();
  if (tid != CrateTypeId::ReferenceListOp && tid != CrateTypeId::PayloadListOp) {
    return false;
  }
  if (rep.payload() == 0) return true;  // empty listop
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;

  // pxr/legacy SdfReference / SdfPayload items are read INTERLEAVED, one item
  // at a time, each:
  //   assetPath(u32 str idx), primPath(u32 path idx),
  //   [layerOffset(f64 offset, f64 scale)]  -- references always; payloads only
  //                                            from crate 0.8.0,
  //   [customData(u64 dict count + entries)] -- REFERENCES ONLY.
  // (An earlier version read all asset/path index pairs up front and put the
  // customData count before the layerOffset — that mismatched pxr for any
  // multi-item list and made pxr/legacy read `scale` as the customData count.)
  const bool has_offset = !is_payload || (version_.minor >= 8);
  const bool has_customdata = !is_payload;

  auto read_run = [&](bool keep) -> bool {
    uint64_t n = 0;
    if (!reader()->read_u64(n)) return false;
    if (n > options_.max_array_elements) return false;
    if (n == 0) return true;
    const size_t run_count = static_cast<size_t>(n);

    for (size_t i = 0; i < run_count; ++i) {
      uint32_t asset_idx = 0, path_idx = 0;
      if (!reader()->read_u32(asset_idx) || !reader()->read_u32(path_idx)) {
        return false;
      }
      double offset = 0.0, scale = 1.0;
      if (has_offset) {
        if (!reader()->read_f64(offset) || !reader()->read_f64(scale)) {
          return false;
        }
      }
      if (has_customdata) {
        uint64_t dict_count = 0;
        if (!reader()->read_u64(dict_count)) return false;
        if (dict_count != 0) {
          // next authors no per-arc customData; a non-empty dict would need the
          // dict body parsed to keep the cursor aligned, which is unsupported.
          AddWarning("Reference customData is not supported; arc dropped");
          return false;
        }
      }
      if (!keep) continue;

      std::string_view asset;
      if (!GetString(asset_idx, &asset)) return false;
      std::string_view prim;
      if (path_idx < paths_.size()) prim = paths_.view(path_idx);

      std::string arc;
      if (asset.empty()) {
        if (!prim.empty() && prim != "/") {
          arc.push_back('<');
          arc.append(prim.data(), prim.size());
          arc.push_back('>');
        }
      } else {
        arc.reserve(asset.size() + 2 +
                    ((prim.empty() || prim == "/") ? 0 : prim.size() + 2));
        arc.push_back('@');
        arc.append(asset.data(), asset.size());
        arc.push_back('@');
        if (!prim.empty() && prim != "/") {
          arc.push_back('<');
          arc.append(prim.data(), prim.size());
          arc.push_back('>');
        }
      }
      if (offset != 0.0 || scale != 1.0) {
        arc.append("?layerOffset=");
        arc.append(std::to_string(offset));
        arc.push_back(':');
        arc.append(std::to_string(scale));
      }
      out.push_back(std::move(arc));
    }
    return true;
  };

  // ListOpHeader bits / read order match DecodePathTargets.
  uint8_t bits = 0;
  if (!reader()->read_u8(bits)) return false;
  const uint8_t kHasExplicit = 0x02, kHasAdded = 0x04, kHasDeleted = 0x08,
                kHasOrdered = 0x10, kHasPrepended = 0x20, kHasAppended = 0x40;
  if ((bits & kHasExplicit) && !read_run(true)) return false;
  if ((bits & kHasAdded) && !read_run(true)) return false;
  if ((bits & kHasPrepended) && !read_run(true)) return false;
  if ((bits & kHasAppended) && !read_run(true)) return false;
  if ((bits & kHasDeleted) && !read_run(false)) return false;
  if ((bits & kHasOrdered) && !read_run(false)) return false;
  return true;
}

bool CrateReader::Impl::DecodeVariantSelectionMap(
    ValueRep rep, std::vector<std::pair<std::string, std::string>>& out) {
  if (rep.type_id() != CrateTypeId::VariantSelectionMap) return false;
  if (rep.payload() == 0) {
    out.clear();
    return true;
  }
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint64_t count = 0;
  if (!reader()->read_u64(count)) return false;
  if (count > options_.max_array_elements) return false;

  out.clear();
  out.reserve(static_cast<size_t>(count));
  auto& idxs = array_scratch().u32_indices;
  size_t pair_count = 0;
  if (!safe::mul(static_cast<size_t>(count), size_t(2), &pair_count)) return false;
  idxs.resize(pair_count);
  if (!reader()->read_u32_array(idxs, pair_count)) return false;
  for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
    const uint32_t k = idxs[(i * 2)];
    const uint32_t v = idxs[(i * 2) + 1];
    std::string_view key;
    std::string_view val;
    if (!GetString(k, &key) || !GetString(v, &val)) return false;
    out.emplace_back(std::string(key), std::string(val));
  }
  return true;
}

bool CrateReader::Impl::DecodeVariantSelectionMap(
    ValueRep rep,
    std::vector<std::pair<std::string_view, std::string_view>>* out) {
  if (!out) return false;
  if (rep.type_id() != CrateTypeId::VariantSelectionMap) return false;
  if (rep.payload() == 0) {
    out->clear();
    return true;  // empty map
  }
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint64_t count = 0;
  if (!reader()->read_u64(count)) return false;
  if (count > options_.max_array_elements) return false;
  out->clear();
  out->reserve(out->size() + static_cast<size_t>(count));
  auto& idxs = array_scratch().u32_indices;
  size_t pair_count = 0;
  if (!safe::mul(static_cast<size_t>(count), size_t(2), &pair_count)) return false;
  idxs.resize(pair_count);
  if (!reader()->read_u32_array(idxs, pair_count)) return false;
  for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
    std::string_view key;
    std::string_view val;
    if (!GetString(idxs[(i * 2)], &key) ||
        !GetString(idxs[(i * 2) + 1], &val)) {
      return false;
    }
    out->emplace_back(key, val);
  }
  return true;
}

bool CrateReader::Impl::DecodeTokenListOp(ValueRep rep,
                                          std::vector<std::string>& out) {
  const CrateTypeId tid = rep.type_id();
  if (tid != CrateTypeId::TokenListOp && tid != CrateTypeId::StringListOp) {
    return false;
  }
  if (rep.payload() == 0) return true;  // empty listop
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;

  const bool is_token = (tid == CrateTypeId::TokenListOp);
  // One [u64 count][u32 idx]* run; collect its tokens when `keep`.
  auto read_run = [&](bool keep) -> bool {
    uint64_t n = 0;
    if (!reader()->read_u64(n)) return false;
    if (n > options_.max_array_elements) return false;
    out.reserve(out.size() + static_cast<size_t>(n));
    auto& idxs = array_scratch().u32_indices;
    idxs.resize(static_cast<size_t>(n));
    if (!reader()->read_u32_array(idxs, static_cast<size_t>(n))) return false;
    if (!keep) return true;
    for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
      const uint32_t idx = idxs[i];
      if (is_token) {
        if (idx >= tokens_.size()) return false;
        out.emplace_back(tokens_.view(idx));
      } else {
        std::string_view s;
        if (!GetString(idx, &s)) return false;
        out.emplace_back(s);
      }
    }
    return true;
  };

  // ListOpHeader bits / read order match DecodeReferenceListOp.
  uint8_t bits = 0;
  if (!reader()->read_u8(bits)) return false;
  const uint8_t kHasExplicit = 0x02, kHasAdded = 0x04, kHasDeleted = 0x08,
                kHasOrdered = 0x10, kHasPrepended = 0x20, kHasAppended = 0x40;
  if ((bits & kHasExplicit) && !read_run(true)) return false;
  if ((bits & kHasAdded) && !read_run(true)) return false;
  if ((bits & kHasPrepended) && !read_run(true)) return false;
  if ((bits & kHasAppended) && !read_run(true)) return false;
  if ((bits & kHasDeleted) && !read_run(false)) return false;
  if ((bits & kHasOrdered) && !read_run(false)) return false;
  return true;
}

bool CrateReader::Impl::DecodeDictionary(ValueRep rep, Value& out, int depth) {
  if (rep.type_id() != CrateTypeId::Dictionary) return false;
  if (depth > 64) return false;
  if (rep.payload() == 0) {
    out = Value::MakeDictionary();
    return true;
  }
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint64_t count = 0;
  if (!reader()->read_u64(count)) return false;
  if (count > options_.max_array_elements) return false;

  // pxr WriteMap layout: [u64 count] then per entry [u32 keyStringIdx] followed
  // by a Write(VtValue): an int64 forward offset, the nested value data, then
  // the 8-byte ValueRep (_RecursiveWrite). So entries are *variable*-stride: the
  // ValueRep lives at (valStart + recOffset) and the next entry begins right
  // after it. We must read sequentially and restore the position after each
  // value decode (which seeks to the ValueRep's payload elsewhere).
  Value dv = Value::MakeDictionary();
  Dict* d = dv.as_dictionary();
  for (uint64_t i = 0; i < count; ++i) {
    uint32_t kidx = 0;
    if (!reader()->read_u32(kidx)) return false;
    std::string_view key;
    if (!GetString(kidx, &key)) return false;

    const size_t val_start = reader()->position();
    uint64_t rec_off_raw = 0;
    if (!reader()->read_u64(rec_off_raw)) return false;
    const int64_t rec_off = static_cast<int64_t>(rec_off_raw);
    const size_t rep_pos =
        static_cast<size_t>(static_cast<int64_t>(val_start) + rec_off);
    if (!reader()->seek(rep_pos)) return false;
    uint64_t vrep_raw = 0;
    if (!reader()->read_u64(vrep_raw)) return false;
    const size_t next_entry_pos = reader()->position();  // rep_pos + 8

    ValueRep vr(vrep_raw);
    Value cv;
    if (vr.type_id() == CrateTypeId::Dictionary) {
      DecodeDictionary(vr, cv, depth + 1);
    } else if (vr.type_id() != CrateTypeId::Invalid) {
      UnpackValue(vr, cv);
    }
    d->set(std::string(key), std::move(cv));

    if (!reader()->seek(next_entry_pos)) return false;  // resume after this value
  }
  out = std::move(dv);
  return true;
}

bool CrateReader::Impl::CheckByteAllocation(uint64_t bytes, const char* what) {
  if (bytes > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())) {
    AddError(std::string(what) + " size exceeds addressable memory");
    return false;
  }
  // Always-on guard: a decoded in-memory buffer cannot plausibly exceed the
  // input file size by more than the maximum stream compression ratio. This
  // bounds allocations driven by a malformed/hostile count even when no
  // explicit max_memory budget is configured (a tiny file can otherwise claim a
  // multi-GB array/section and exhaust memory). The +slack admits small files
  // with legitimately larger decoded buffers.
  if (reader_) {
    const uint64_t file_size = static_cast<uint64_t>(reader()->size());
    constexpr uint64_t kMaxRatio = 256;  // LZ4-ish worst-case headroom
    constexpr uint64_t kSlack = 64ull * 1024 * 1024;  // 64 MiB
    constexpr uint64_t kU64Max = (std::numeric_limits<uint64_t>::max)();
    const uint64_t cap = (file_size > (kU64Max - kSlack) / kMaxRatio)
                             ? kU64Max
                             : file_size * kMaxRatio + kSlack;
    if (bytes > cap) {
      AddError(std::string(what) +
               " exceeds file-size-relative allocation cap");
      return false;
    }
  }
  if (options_.max_memory && bytes > static_cast<uint64_t>(options_.max_memory)) {
    AddError(std::string(what) + " exceeds max_memory budget");
    return false;
  }
  return true;
}

bool CrateReader::Impl::CheckElementAllocation(uint64_t count, size_t elem_size,
                                               const char* what) {
  if (elem_size == 0) return false;
  const uint64_t max_size =
      static_cast<uint64_t>((std::numeric_limits<size_t>::max)());
  if (count > max_size / elem_size) {
    AddError(std::string(what) + " size exceeds addressable memory");
    return false;
  }
  size_t total = 0;
  if (!safe::mul(count, elem_size, &total)) {
    AddError(std::string(what) + " size exceeds addressable memory");
    return false;
  }
  return CheckByteAllocation(static_cast<uint64_t>(total), what);
}

bool CrateReader::Impl::GetToken(uint32_t index, std::string_view* out) const {
  if (!out || index >= tokens_.size()) {
    return false;
  }
  *out = tokens_.view(index);
  return true;
}

bool CrateReader::Impl::GetToken(uint32_t index, std::string& out) {
  if (index >= tokens_.size()) {
    return false;
  }
  std::string_view s;
  if (!GetToken(index, &s)) {
    return false;
  }
  out.assign(s.data(), s.size());
  return true;
}

bool CrateReader::Impl::GetString(uint32_t index, std::string_view* out) const {
  if (!out || index >= string_indices_.size()) {
    return false;
  }
  return GetToken(string_indices_[index], out);
}

bool CrateReader::Impl::GetString(uint32_t index, std::string& out) {
  if (index >= string_indices_.size()) {
    return false;
  }
  std::string_view s;
  if (!GetString(index, &s)) {
    return false;
  }
  out.assign(s.data(), s.size());
  return true;
}

void CrateReader::Impl::AddError(const std::string& msg) {
  CrateError err;
  err.offset = reader_ ? reader()->position() : 0;
  err.message = msg;
#if defined(TINYUSDZ_ENABLE_THREAD)
  std::lock_guard<std::mutex> lock(diag_mu_);
#endif
  result_.errors.push_back(err);
}

void CrateReader::Impl::AddWarning(const std::string& msg) {
#if defined(TINYUSDZ_ENABLE_THREAD)
  std::lock_guard<std::mutex> lock(diag_mu_);
#endif
  result_.warnings.push_back(msg);
}


}  // namespace next
}  // namespace tinyusdz
