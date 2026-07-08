// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Crate Reader decode/allocation helpers

#include "crate-reader-internal.hh"
#include "safe-arithmetic.hh"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace tinyusdz {
namespace next {

bool CrateReader::Impl::ResolveFieldset(uint32_t fieldset_index,
                                         std::vector<std::pair<std::string, Value>>& out) {
  out.clear();

  if (fieldset_index >= fieldset_indices_.size()) {
    return false;
  }

  size_t start = fieldset_index;
  while (start < fieldset_indices_.size()) {
    uint32_t field_idx = fieldset_indices_[start];
    if (field_idx == 0xFFFFFFFF) break;

    if (field_idx < fields_.size()) {
      const CrateField& field = fields_[field_idx];

      std::string name;
      if (GetToken(field.token_index.value, name)) {
        Value value;
        if (UnpackValue(field.value_rep, value)) {
          out.emplace_back(std::move(name), std::move(value));
        }
      }
    }
    start++;
  }

  return true;
}

bool CrateReader::Impl::ResolveFieldsetRaw(
    uint32_t fieldset_index,
    std::vector<std::pair<std::string, ValueRep>>& out) {
  out.clear();
  if (fieldset_index >= fieldset_indices_.size()) return false;

  size_t start = fieldset_index;
  while (start < fieldset_indices_.size()) {
    uint32_t field_idx = fieldset_indices_[start];
    if (field_idx == 0xFFFFFFFF) break;
    if (field_idx < fields_.size()) {
      const CrateField& field = fields_[field_idx];
      std::string name;
      if (GetToken(field.token_index.value, name)) {
        out.emplace_back(std::move(name), field.value_rep);
      }
    }
    start++;
  }
  return true;
}

bool CrateReader::Impl::DecodePathTargets(ValueRep rep,
                                          std::vector<std::string>& out) {
  return DecodePathTargets(rep, out, /*with_markers=*/false);
}

bool CrateReader::Impl::DecodePathTargets(ValueRep rep,
                                          std::vector<std::string>& out,
                                          bool with_markers) {
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
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;

  auto read_run = [&]() -> bool {
    uint64_t n = 0;
    if (!reader_->read_u64(n)) return false;
    if (n > options_.max_array_elements) return false;
    for (uint64_t i = 0; i < n; ++i) {
      uint32_t idx = 0;
      if (!reader_->read_u32(idx)) return false;
      if (idx < paths_.size()) {
        // paths_ renders a property path as ".<primpath>/<prop>"; convert to the
        // canonical USD form "<primpath>.<prop>" so targets re-intern correctly
        // (and survive repeated round-trips). Prim targets pass through as-is.
        const std::string& p = paths_[idx];
        if (!p.empty() && p[0] == '.') {
          std::string body = p.substr(1);          // "/a/b/prop"
          size_t slash = body.rfind('/');
          if (slash != std::string::npos) {
            out.push_back(body.substr(0, slash) + "." + body.substr(slash + 1));
          } else {
            out.push_back(body);
          }
        } else {
          out.push_back(p);                        // prim path "/a/b"
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
  // With `with_markers`, a non-explicit listop's sublists are delimited by
  // "\x01P"/"\x01A"/"\x01D"/"\x01O" marker entries so the caller can
  // reconstruct the authored list-op edits.
  uint8_t bits = 0;
  if (!reader_->read_u8(bits)) return false;
  const uint8_t kHasExplicit = 0x02, kHasAdded = 0x04, kHasDeleted = 0x08,
                kHasOrdered = 0x10, kHasPrepended = 0x20, kHasAppended = 0x40;
  const bool mark = with_markers && !(bits & kHasExplicit);
  auto marked_run = [&](const char* marker) -> bool {
    if (mark) out.push_back(marker);
    return read_run();
  };
  if ((bits & kHasExplicit) && !read_run()) return false;
  if ((bits & kHasAdded) && !marked_run("\x01" "A")) return false;
  if ((bits & kHasPrepended) && !marked_run("\x01" "P")) return false;
  if ((bits & kHasAppended) && !marked_run("\x01" "A")) return false;
  if ((bits & kHasDeleted) && !(mark ? marked_run("\x01" "D") : read_run())) return false;
  if ((bits & kHasOrdered) && !(mark ? marked_run("\x01" "O") : read_run())) return false;
  return true;
}

bool CrateReader::Impl::DecodeReferenceListOp(ValueRep rep, bool is_payload,
                                              std::vector<std::string>& out) {
  const CrateTypeId tid = rep.type_id();
  if (tid != CrateTypeId::ReferenceListOp && tid != CrateTypeId::PayloadListOp) {
    return false;
  }
  if (rep.payload() == 0) return true;  // empty listop
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;

  // Payload items carry a LayerOffset only from crate 0.8.0 on.
  const bool payload_has_offset =
      !is_payload || (version_.minor >= 8);

  // Read one SdfReference/SdfPayload item; when `keep`, append its arc string.
  auto read_item = [&](bool keep) -> bool {
    uint32_t asset_idx = 0, path_idx = 0;
    if (!reader_->read_u32(asset_idx) || !reader_->read_u32(path_idx)) {
      return false;
    }
    double offset = 0.0, scale = 1.0;
    if (payload_has_offset) {
      if (!reader_->read_f64(offset) || !reader_->read_f64(scale)) {
        return false;
      }
    }
    if (!is_payload) {
      // customData dict: u64 count, then per-entry recursive offsets. UE/pxr
      // references rarely author it; decoding requires recursive value reads,
      // so bail (drop the whole listop with a warning) when non-empty.
      uint64_t dict_count = 0;
      if (!reader_->read_u64(dict_count)) return false;
      if (dict_count != 0) {
        AddWarning("Reference customData is not supported; arc dropped");
        return false;
      }
    }
    if (!keep) return true;

    std::string asset;
    GetString(asset_idx, asset);
    std::string prim = (path_idx < paths_.size()) ? paths_[path_idx] : "";
    // Internal arcs (no asset) render as "</Prim>", matching the usda parser.
    std::string arc;
    if (!asset.empty()) arc = "@" + asset + "@";
    if (!prim.empty() && prim != "/") arc += "<" + prim + ">";
    if (offset != 0.0 || scale != 1.0) {
      arc += "?layerOffset=" + std::to_string(offset) + ":" +
             std::to_string(scale);
    }
    out.push_back(std::move(arc));
    return true;
  };

  auto read_run = [&](bool keep) -> bool {
    uint64_t n = 0;
    if (!reader_->read_u64(n)) return false;
    if (n > options_.max_array_elements) return false;
    for (uint64_t i = 0; i < n; ++i) {
      if (!read_item(keep)) return false;
    }
    return true;
  };

  // ListOpHeader bits / read order match DecodePathTargets. Non-explicit
  // sublists are marker-delimited ("\x01" "P"/"A"/"D"/"O") so BuildStage can
  // reconstruct the authored list-op edits.
  uint8_t bits = 0;
  if (!reader_->read_u8(bits)) return false;
  const uint8_t kHasExplicit = 0x02, kHasAdded = 0x04, kHasDeleted = 0x08,
                kHasOrdered = 0x10, kHasPrepended = 0x20, kHasAppended = 0x40;
  const bool mark = !(bits & kHasExplicit);
  auto marked_run = [&](const char* marker, bool keep) -> bool {
    if (mark && keep) out.push_back(marker);
    return read_run(keep);
  };
  if ((bits & kHasExplicit) && !read_run(true)) return false;
  if ((bits & kHasAdded) && !marked_run("\x01" "A", true)) return false;
  if ((bits & kHasPrepended) && !marked_run("\x01" "P", true)) return false;
  if ((bits & kHasAppended) && !marked_run("\x01" "A", true)) return false;
  if ((bits & kHasDeleted) && !marked_run("\x01" "D", mark)) return false;
  if ((bits & kHasOrdered) && !marked_run("\x01" "O", mark)) return false;
  return true;
}

bool CrateReader::Impl::DecodeVariantSelectionMap(
    ValueRep rep, std::vector<std::pair<std::string, std::string>>& out) {
  if (rep.type_id() != CrateTypeId::VariantSelectionMap) return false;
  if (rep.payload() == 0) return true;  // empty map
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint64_t count = 0;
  if (!reader_->read_u64(count)) return false;
  if (count > options_.max_array_elements) return false;
  for (uint64_t i = 0; i < count; ++i) {
    uint32_t k = 0, v = 0;
    if (!reader_->read_u32(k) || !reader_->read_u32(v)) return false;
    std::string key, val;
    GetString(k, key);
    GetString(v, val);
    out.emplace_back(std::move(key), std::move(val));
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
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;

  const bool is_token = (tid == CrateTypeId::TokenListOp);
  // One [u64 count][u32 idx]* run; collect its tokens when `keep`.
  auto read_run = [&](bool keep) -> bool {
    uint64_t n = 0;
    if (!reader_->read_u64(n)) return false;
    if (n > options_.max_array_elements) return false;
    for (uint64_t i = 0; i < n; ++i) {
      uint32_t idx = 0;
      if (!reader_->read_u32(idx)) return false;
      if (!keep) continue;
      std::string s;
      if (is_token) {
        if (idx >= tokens_.size()) return false;
        s = tokens_.str(idx);
      } else {
        GetString(idx, s);
      }
      out.push_back(std::move(s));
    }
    return true;
  };

  // ListOpHeader bits / read order match DecodeReferenceListOp. Non-explicit
  // sublists are marker-delimited ("\x01" "P"/"A"/"D"/"O") so BuildStage can
  // recover the authored qualifier (e.g. `prepend apiSchemas`).
  uint8_t bits = 0;
  if (!reader_->read_u8(bits)) return false;
  const uint8_t kHasExplicit = 0x02, kHasAdded = 0x04, kHasDeleted = 0x08,
                kHasOrdered = 0x10, kHasPrepended = 0x20, kHasAppended = 0x40;
  const bool mark = !(bits & kHasExplicit);
  auto marked_run = [&](const char* marker, bool keep) -> bool {
    if (mark && keep) out.push_back(marker);
    return read_run(keep);
  };
  if ((bits & kHasExplicit) && !read_run(true)) return false;
  if ((bits & kHasAdded) && !marked_run("\x01" "A", true)) return false;
  if ((bits & kHasPrepended) && !marked_run("\x01" "P", true)) return false;
  if ((bits & kHasAppended) && !marked_run("\x01" "A", true)) return false;
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
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint64_t count = 0;
  if (!reader_->read_u64(count)) return false;
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
    if (!reader_->read_u32(kidx)) return false;
    std::string key;
    GetString(kidx, key);

    const size_t val_start = reader_->position();
    uint64_t rec_off_raw = 0;
    if (!reader_->read_u64(rec_off_raw)) return false;
    const int64_t rec_off = static_cast<int64_t>(rec_off_raw);
    const size_t rep_pos =
        static_cast<size_t>(static_cast<int64_t>(val_start) + rec_off);
    if (!reader_->seek(rep_pos)) return false;
    uint64_t vrep_raw = 0;
    if (!reader_->read_u64(vrep_raw)) return false;
    const size_t next_entry_pos = reader_->position();  // rep_pos + 8

    ValueRep vr(vrep_raw);
    Value cv;
    if (vr.type_id() == CrateTypeId::Dictionary) {
      DecodeDictionary(vr, cv, depth + 1);
    } else if (vr.type_id() != CrateTypeId::Invalid) {
      UnpackValue(vr, cv);
    }
    d->set(std::move(key), std::move(cv));

    if (!reader_->seek(next_entry_pos)) return false;  // resume after this value
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
    const uint64_t file_size = static_cast<uint64_t>(reader_->size());
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

bool CrateReader::Impl::GetToken(uint32_t index, std::string& out) {
  if (index >= tokens_.size()) {
    return false;
  }
  out = tokens_.str(index);
  return true;
}

bool CrateReader::Impl::GetString(uint32_t index, std::string& out) {
  if (index >= string_indices_.size()) {
    return false;
  }
  return GetToken(string_indices_[index], out);
}

void CrateReader::Impl::AddError(const std::string& msg) {
  CrateError err;
  err.offset = reader_ ? reader_->position() : 0;
  err.message = msg;
  result_.errors.push_back(err);
}

void CrateReader::Impl::AddWarning(const std::string& msg) {
  result_.warnings.push_back(msg);
}


}  // namespace next
}  // namespace tinyusdz
