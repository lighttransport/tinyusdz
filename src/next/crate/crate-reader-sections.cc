// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Crate Reader structural section readers

#include "crate-reader-internal.hh"
#if defined(TINYUSDZ_NEXT_PARALLEL_BUILD_STAGE)
#include "../parser/value-parser.hh"  // SubmitPoolTask (shared worker pool)
#include <condition_variable>
#include <mutex>
#include <thread>
#endif
#include "safe-arithmetic.hh"
#include "../strfmt.hh"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace tinyusdz {
namespace next {

namespace {

uint64_t CrateValueRepMinPayloadBytes(ValueRep rep) {
  if (rep.is_array()) return 8;  // array element count header.
  switch (rep.type_id()) {
    case CrateTypeId::Bool: return 1;
    case CrateTypeId::UChar: return 1;
    case CrateTypeId::Half: return 2;
    case CrateTypeId::Int:
    case CrateTypeId::UInt:
    case CrateTypeId::Float: return 4;
    case CrateTypeId::Int64:
    case CrateTypeId::UInt64:
    case CrateTypeId::Double:
    case CrateTypeId::TimeCode: return 8;
    case CrateTypeId::Vec2i:
    case CrateTypeId::Vec2f:
    case CrateTypeId::Vec2h: return 8;
    case CrateTypeId::Vec3i:
    case CrateTypeId::Vec3f:
    case CrateTypeId::Vec3h: return 12;
    case CrateTypeId::Vec4i:
    case CrateTypeId::Vec4f:
    case CrateTypeId::Vec4h:
    case CrateTypeId::Quatf:
    case CrateTypeId::Quath: return 16;
    case CrateTypeId::Vec2d: return 16;
    case CrateTypeId::Vec3d: return 24;
    case CrateTypeId::Vec4d:
    case CrateTypeId::Quatd:
    case CrateTypeId::Matrix2d: return 32;
    case CrateTypeId::Matrix3d: return 72;
    case CrateTypeId::Matrix4d: return 128;
    case CrateTypeId::Dictionary:
    case CrateTypeId::TokenVector:
    case CrateTypeId::StringVector:
    case CrateTypeId::DoubleVector:
    case CrateTypeId::PathVector:
    case CrateTypeId::VariantSelectionMap:
      return 8;
    case CrateTypeId::PathListOp:
    case CrateTypeId::ReferenceListOp:
    case CrateTypeId::PayloadListOp:
    case CrateTypeId::TokenListOp:
    case CrateTypeId::StringListOp:
      return 1;
    case CrateTypeId::TimeSamples:
      return 32;
    default:
      return 0;
  }
}

}  // namespace

bool CrateReader::Impl::ReadBootstrap() {
  // Check magic
  char magic[8];
  if (!reader()->read(magic, 8)) {
    AddError("Failed to read magic bytes");
    return false;
  }
  if (std::memcmp(magic, kCrateMagic, 8) != 0) {
    AddError("Invalid USDC magic bytes");
    return false;
  }

  // Read version
  uint8_t version_bytes[8];
  if (!reader()->read(version_bytes, 8)) {
    AddError("Failed to read version");
    return false;
  }
  version_.major = version_bytes[0];
  version_.minor = version_bytes[1];
  version_.patch = version_bytes[2];

  if (!version_.is_valid()) {
    AddError("Unsupported USDC version: " + version_.to_string());
    return false;
  }

  // Read TOC offset
  int64_t toc_offset;
  if (!reader()->read_i64(toc_offset)) {
    AddError("Failed to read TOC offset");
    return false;
  }

  if (toc_offset < static_cast<int64_t>(kCrateBootstrapSize) ||
      toc_offset >= static_cast<int64_t>(reader()->size())) {
    AddError("Invalid TOC offset");
    return false;
  }

  // Seek to TOC
  if (!reader()->seek(static_cast<size_t>(toc_offset))) {
    AddError("Failed to seek to TOC");
    return false;
  }

  return true;
}

bool CrateReader::Impl::ReadTOC() {
  uint64_t num_sections;
  if (!reader()->read_u64(num_sections)) {
    AddError("Failed to read section count");
    return false;
  }

  if (num_sections > 100) {
    AddError("Too many sections in TOC");
    return false;
  }

  toc_.sections.resize(static_cast<size_t>(num_sections));

  for (size_t i = 0; i < num_sections; i++) {
    CrateSection& s = toc_.sections[i];
    if (!reader()->read(s.name, 16)) {
      AddError("Failed to read section name");
      return false;
    }
    if (!reader()->read_i64(s.start)) {
      AddError("Failed to read section start");
      return false;
    }
    if (!reader()->read_i64(s.size)) {
      AddError("Failed to read section size");
      return false;
    }

    // Overflow-safe: s.start + s.size as int64 can wrap. Check each term
    // against the file size with subtraction that cannot overflow.
    const size_t fsize = reader()->size();
    if (s.start < 0 || s.size < 0 ||
        static_cast<size_t>(s.start) > fsize ||
        static_cast<size_t>(s.size) > fsize - static_cast<size_t>(s.start)) {
      AddError("Invalid section bounds: " + s.name_str());
      return false;
    }
  }

  return true;
}

bool CrateReader::Impl::ReadTokens() {
  const CrateSection* section = toc_.find("TOKENS");
  if (!section) {
    AddError("Missing TOKENS section");
    return false;
  }

  if (!reader()->seek(static_cast<size_t>(section->start))) {
    AddError("Failed to seek to TOKENS");
    return false;
  }

  uint64_t num_tokens;
  if (!reader()->read_u64(num_tokens)) {
    AddError("Failed to read token count");
    return false;
  }

  if (num_tokens > options_.max_tokens) {
    AddError("Too many tokens");
    return false;
  }

  uint64_t uncompressed_size, compressed_size;
  if (!reader()->read_u64(uncompressed_size) || !reader()->read_u64(compressed_size)) {
    AddError("Failed to read token compression info");
    return false;
  }
  if (!CheckByteAllocation(compressed_size, "Compressed token table") ||
      !CheckByteAllocation(uncompressed_size, "Uncompressed token table")) {
    return false;
  }

  // Use the check-before-resize read overload so a bogus compressed_size
  // cannot trigger a huge allocation before the bounds check.
  std::vector<uint8_t> compressed;
  if (!reader()->read(compressed, static_cast<size_t>(compressed_size))) {
    AddError("Failed to read compressed tokens");
    return false;
  }

  DecompressResult dr = DecompressCrateBlob(compressed.data(), compressed.size(),
                                            static_cast<size_t>(uncompressed_size));
  if (!dr.success) {
    AddError("Failed to decompress tokens: " + dr.error);
    return false;
  }

  // TokenPool addresses tokens with uint32 (offset,len) spans; a token blob
  // beyond 4 GiB would silently truncate. Reject it (absurd for real USDC; this
  // only guards hostile/corrupt input).
  if (dr.data.size() > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
    AddError("Token blob exceeds 4 GiB pooled-storage limit");
    return false;
  }

  tokens_.reserve(static_cast<size_t>(num_tokens));
  const char* ptr = reinterpret_cast<const char*>(dr.data.data());
  const char* end = ptr + dr.data.size();

  while (ptr < end && tokens_.size() < num_tokens) {
    // Bounded scan: a malformed blob whose last token lacks the NUL terminator
    // must not strlen past the decompressed buffer.
    const char* nul = static_cast<const char*>(
        std::memchr(ptr, '\0', static_cast<size_t>(end - ptr)));
    if (!nul) {
      AddError("Token table not NUL-terminated");
      return false;
    }
    tokens_.push(ptr, static_cast<size_t>(nul - ptr));
    ptr = nul + 1;
  }

  if (tokens_.size() != num_tokens) {
    AddWarning("Token count mismatch");
  }

  return true;
}

bool CrateReader::Impl::ReadStrings() {
  const CrateSection* section = toc_.find("STRINGS");
  if (!section) {
    return true;
  }

  if (!reader()->seek(static_cast<size_t>(section->start))) {
    AddError("Failed to seek to STRINGS");
    return false;
  }

  uint64_t num_strings;
  if (!reader()->read_u64(num_strings)) {
    AddError("Failed to read string count");
    return false;
  }

  if (num_strings > options_.max_strings) {
    AddError("Too many strings");
    return false;
  }
  if (!CheckElementAllocation(num_strings, sizeof(uint32_t), "String table")) {
    return false;
  }

  string_indices_.resize(static_cast<size_t>(num_strings));
  for (size_t i = 0; i < num_strings; i++) {
    uint32_t idx;
    if (!reader()->read_u32(idx)) {
      AddError("Failed to read string index");
      return false;
    }
    if (idx >= tokens_.size()) {
      AddError("String token index out of range");
      return false;
    }
    string_indices_[i] = idx;
  }

  return true;
}

bool CrateReader::Impl::ReadFields() {
  const CrateSection* section = toc_.find("FIELDS");
  if (!section) {
    AddError("Missing FIELDS section");
    return false;
  }

  if (!reader()->seek(static_cast<size_t>(section->start))) {
    AddError("Failed to seek to FIELDS");
    return false;
  }

  uint64_t num_fields;
  if (!reader()->read_u64(num_fields)) {
    AddError("Failed to read field count");
    return false;
  }

  if (num_fields > options_.max_fields) {
    AddError("Too many fields");
    return false;
  }
  if (!CheckElementAllocation(num_fields, sizeof(CrateField), "Field table") ||
      !CheckElementAllocation(num_fields, sizeof(uint32_t),
                              "Field token indices") ||
      !CheckElementAllocation(num_fields, sizeof(uint64_t), "Field value reps")) {
    return false;
  }
  size_t field_index_bytes = 0;
  size_t field_value_rep_bytes = 0;
  if (!safe::mul(num_fields, sizeof(uint32_t), &field_index_bytes) ||
      !safe::mul(num_fields, sizeof(uint64_t), &field_value_rep_bytes)) {
    AddError("Field table byte size overflow");
    return false;
  }

  uint64_t indices_size;
  if (!reader()->read_u64(indices_size)) {
    AddError("Failed to read field indices size");
    return false;
  }
  if (!CheckByteAllocation(indices_size, "Field indices")) return false;

  std::vector<uint8_t> indices_data;
  if (!reader()->read(indices_data, static_cast<size_t>(indices_size))) {
    AddError("Failed to read field indices");
    return false;
  }

  // Read the value-reps payload up front (the stream cursor moves through the
  // section sequentially) so the two independent decompressions below can be
  // overlapped under the parallel gate.
  uint64_t reps_size;
  if (!reader()->read_u64(reps_size)) {
    AddError("Failed to read reps size");
    return false;
  }
  if (!CheckByteAllocation(reps_size, "Field value reps")) return false;

  std::vector<uint64_t> value_reps(static_cast<size_t>(num_fields));

  std::vector<uint8_t> reps_data;
  if (!reader()->read(reps_data, static_cast<size_t>(reps_size))) {
    AddError("Failed to read value reps");
    return false;
  }

  // Try pxrUSD format (n_chunks LZ4 + delta-coded) first, fall back to legacy
  std::vector<uint32_t> token_indices_vec(static_cast<size_t>(num_fields));
  // Include u64 compressed_size prefix (DecompressCompressedU32 expects it)
  size_t indices_with_prefix_size = 0;
  if (!safe::add(size_t(8), indices_data.size(), &indices_with_prefix_size)) {
    AddError("Field indices payload size overflow");
    return false;
  }
  std::vector<uint8_t> indices_with_prefix(indices_with_prefix_size);
  std::memcpy(indices_with_prefix.data(), &indices_size, 8);
  if (!indices_data.empty()) {
    std::memcpy(indices_with_prefix.data() + 8, indices_data.data(),
                indices_data.size());
  }
  DecompressResult dr;
  DecompressResult rdr_pre;
  bool rdr_pre_valid = false;
#if defined(TINYUSDZ_NEXT_PARALLEL_BUILD_STAGE)
  {
    int nt = options_.num_threads;
    if (nt <= 0) {
      nt = static_cast<int>(std::thread::hardware_concurrency());
      if (nt < 1) nt = 1;
      nt = std::min(nt, 8);
    }
    if (nt > 1 && num_fields >= 65536) {
      // Token-index and value-rep payloads decompress independently: run the
      // indices on the worker pool while this thread does the reps blob. The
      // (retained) legacy fallbacks below stay serial and order-identical.
      std::mutex done_mu;
      std::condition_variable done_cv;
      bool done = false;
      const bool submitted = SubmitPoolTask(options_.num_threads, [&]() {
        dr = DecompressCompressedU32(indices_with_prefix.data(),
                                     indices_with_prefix.size(),
                                     token_indices_vec.data(),
                                     static_cast<size_t>(num_fields));
        std::lock_guard<std::mutex> lock(done_mu);
        done = true;
        done_cv.notify_all();
      });
      if (submitted) {
        rdr_pre = DecompressCrateBlob(reps_data.data(), reps_data.size(),
                                      field_value_rep_bytes);
        rdr_pre_valid = true;
        std::unique_lock<std::mutex> lock(done_mu);
        done_cv.wait(lock, [&]() { return done; });
      }
    }
  }
  if (!rdr_pre_valid)
#endif
  dr = DecompressCompressedU32(indices_with_prefix.data(), indices_with_prefix.size(),
                                                 token_indices_vec.data(),
                                                 static_cast<size_t>(num_fields));
  if (!dr.success) {
    // Fall back to legacy format
    dr = DecompressIntegers(indices_with_prefix.data() + 8, indices_with_prefix.size() - 8,
                            static_cast<size_t>(num_fields), false);
    if (!dr.success) {
      AddError("Failed to decompress field indices: " + dr.error);
      return false;
    }
    if (dr.data.size() < field_index_bytes) {
      AddError("Decompressed field indices shorter than expected");
      return false;
    }
    if (field_index_bytes > 0) {
      std::memcpy(token_indices_vec.data(), dr.data.data(), field_index_bytes);
    }
  }

  const uint32_t* token_indices = token_indices_vec.data();
  for (size_t i = 0; i < static_cast<size_t>(num_fields); ++i) {
    if (token_indices[i] >= tokens_.size()) {
      AddError("Field token index out of range");
      return false;
    }
  }

  // ValueRep[] is normally TfFastCompression/LZ4-compressed bytes. Do not
  // classify by byte count alone: small OpenUSD files can have compressed size
  // equal to the uncompressed `num_fields * 8` length.
  DecompressResult rdr =
      rdr_pre_valid ? std::move(rdr_pre)
                    : DecompressCrateBlob(reps_data.data(), reps_data.size(),
                                          field_value_rep_bytes);
  if (rdr.success && rdr.data.size() >= field_value_rep_bytes) {
    if (field_value_rep_bytes > 0) {
      std::memcpy(value_reps.data(), rdr.data.data(), field_value_rep_bytes);
    }
  } else if (reps_size == field_value_rep_bytes) {
    if (field_value_rep_bytes > 0) {
      std::memcpy(value_reps.data(), reps_data.data(), field_value_rep_bytes);
    }
  } else {
    AddError("Failed to decompress value reps");
    return false;
  }

  fields_.resize(static_cast<size_t>(num_fields));
  for (size_t i = 0; i < num_fields; i++) {
    fields_[i].token_index.value = token_indices[i];
    fields_[i].value_rep = ValueRep(value_reps[i]);
    const ValueRep rep = fields_[i].value_rep;
    if (!rep.is_inlined()) {
      const int64_t off = rep.payload_as_offset();
      if (off < 0) {
        AddError("Field ValueRep payload offset is negative");
        return false;
      }
      if (rep.payload() != 0 &&
          static_cast<uint64_t>(off) >= static_cast<uint64_t>(reader()->size())) {
        AddError("Field ValueRep payload offset is outside file");
        return false;
      }
      if (rep.payload() != 0) {
        const uint64_t min_bytes = CrateValueRepMinPayloadBytes(rep);
        const uint64_t file_size = static_cast<uint64_t>(reader()->size());
        if (min_bytes > 0 &&
            (min_bytes > file_size ||
             static_cast<uint64_t>(off) > file_size - min_bytes)) {
          AddError("Field ValueRep payload is truncated");
          return false;
        }
      }
      if (rep.is_array() && rep.payload() != 0) {
        const size_t saved = reader()->position();
        if (!reader()->seek(static_cast<size_t>(off))) {
          AddError("Failed to seek to array ValueRep payload");
          return false;
        }
        uint64_t count = 0;
        if (!reader()->read_u64(count)) {
          AddError("Failed to read array ValueRep element count");
          return false;
        }
        if (!reader()->seek(saved)) {
          AddError("Failed to restore FIELDS reader position");
          return false;
        }
        if (count > options_.max_array_elements) {
          AddError("Array ValueRep element count exceeds max_array_elements limit");
          return false;
        }
        const uint64_t stride = CrateArrayElemStride(rep.type_id());
        const uint64_t elem_bytes = stride ? stride : 1;
        if (elem_bytes != 0 &&
            count > (std::numeric_limits<uint64_t>::max)() / elem_bytes) {
          AddError("Array ValueRep payload byte size overflow");
          return false;
        }
        if (!CheckByteAllocation(count * elem_bytes, "Array ValueRep payload")) {
          return false;
        }
      } else if (rep.payload() != 0) {
        const CrateTypeId tid = rep.type_id();
        const bool count_header =
            tid == CrateTypeId::Dictionary ||
            tid == CrateTypeId::TokenVector ||
            tid == CrateTypeId::StringVector ||
            tid == CrateTypeId::DoubleVector ||
            tid == CrateTypeId::PathVector ||
            tid == CrateTypeId::VariantSelectionMap;
        if (count_header) {
          const size_t saved = reader()->position();
          if (!reader()->seek(static_cast<size_t>(off))) {
            AddError("Failed to seek to counted ValueRep payload");
            return false;
          }
          uint64_t count = 0;
          if (!reader()->read_u64(count)) {
            AddError("Failed to read counted ValueRep payload");
            return false;
          }
          if (!reader()->seek(saved)) {
            AddError("Failed to restore FIELDS reader position");
            return false;
          }
          if (count > options_.max_array_elements) {
            AddError("Counted ValueRep payload exceeds max_array_elements limit");
            return false;
          }
          if (tid == CrateTypeId::Dictionary && count > 0) {
            const uint64_t file_size = static_cast<uint64_t>(reader()->size());
            if (count >
                ((std::numeric_limits<uint64_t>::max)() - 8u) / 20u) {
              AddError("Dictionary ValueRep payload size overflow");
              return false;
            }
            const uint64_t min_payload = 8u + count * 20u;
            if (min_payload > file_size ||
                static_cast<uint64_t>(off) > file_size - min_payload) {
              AddError("Dictionary ValueRep payload is truncated");
              return false;
            }
            if (!reader()->seek(static_cast<size_t>(off + 8))) {
              AddError("Failed to seek to dictionary ValueRep entries");
              return false;
            }
            for (uint64_t entry = 0; entry < count; ++entry) {
              uint32_t key_index = 0;
              if (!reader()->read_u32(key_index)) {
                AddError("Failed to read dictionary key index");
                return false;
              }
              if (key_index >= string_indices_.size()) {
                AddError("Dictionary key string index out of range");
                return false;
              }
              const size_t value_start = reader()->position();
              uint64_t recursive_offset_raw = 0;
              if (!reader()->read_u64(recursive_offset_raw)) {
                AddError("Failed to read dictionary recursive offset");
                return false;
              }
              const int64_t recursive_offset =
                  static_cast<int64_t>(recursive_offset_raw);
              if (recursive_offset < 8) {
                AddError("Dictionary recursive offset is invalid");
                return false;
              }
              const uint64_t value_start_u64 =
                  static_cast<uint64_t>(value_start);
              const uint64_t recursive_offset_u64 =
                  static_cast<uint64_t>(recursive_offset);
              if (recursive_offset_u64 >
                  (std::numeric_limits<uint64_t>::max)() - value_start_u64 ||
                  value_start_u64 + recursive_offset_u64 >
                      file_size - sizeof(uint64_t)) {
                AddError("Dictionary recursive ValueRep is outside file");
                return false;
              }
              const size_t next_entry_pos = static_cast<size_t>(
                  value_start_u64 + recursive_offset_u64 + sizeof(uint64_t));
              if (!reader()->seek(next_entry_pos)) {
                AddError("Failed to seek to next dictionary entry");
                return false;
              }
            }
            if (!reader()->seek(saved)) {
              AddError("Failed to restore FIELDS reader position");
              return false;
            }
          }
        }

        const bool list_op =
            tid == CrateTypeId::PathListOp ||
            tid == CrateTypeId::ReferenceListOp ||
            tid == CrateTypeId::PayloadListOp ||
            tid == CrateTypeId::TokenListOp ||
            tid == CrateTypeId::StringListOp;
        if (list_op) {
          const size_t saved = reader()->position();
          if (!reader()->seek(static_cast<size_t>(off))) {
            AddError("Failed to seek to list-op ValueRep payload");
            return false;
          }
          uint8_t bits = 0;
          if (!reader()->read_u8(bits)) {
            AddError("Failed to read list-op ValueRep header");
            return false;
          }
          if (!reader()->seek(saved)) {
            AddError("Failed to restore FIELDS reader position");
            return false;
          }
          const uint8_t kKnownListOpBits = 0x7e;
          if ((bits & kKnownListOpBits) != 0) {
            const uint64_t file_size = static_cast<uint64_t>(reader()->size());
            if (file_size < 9u || static_cast<uint64_t>(off) > file_size - 9u) {
              AddError("List-op ValueRep payload is truncated");
              return false;
            }
            const uint64_t item_bytes =
                (tid == CrateTypeId::ReferenceListOp ||
                 tid == CrateTypeId::PayloadListOp) ? 8u : 4u;
            uint64_t pos = static_cast<uint64_t>(off + 1);
            const uint8_t order[] = {0x02, 0x04, 0x20, 0x40, 0x08, 0x10};
            for (uint8_t bit : order) {
              if ((bits & bit) == 0) continue;
              if (pos > file_size - sizeof(uint64_t)) {
                AddError("List-op ValueRep run is truncated");
                return false;
              }
              const size_t saved = reader()->position();
              if (!reader()->seek(static_cast<size_t>(pos))) {
                AddError("Failed to seek to list-op ValueRep run");
                return false;
              }
              uint64_t count = 0;
              if (!reader()->read_u64(count)) {
                AddError("Failed to read list-op ValueRep run count");
                return false;
              }
              if (!reader()->seek(saved)) {
                AddError("Failed to restore FIELDS reader position");
                return false;
              }
              if (count > options_.max_array_elements) {
                AddError("List-op ValueRep run count exceeds max_array_elements limit");
                return false;
              }
              if (count >
                  ((std::numeric_limits<uint64_t>::max)() - 8u) / item_bytes) {
                AddError("List-op ValueRep run byte size overflow");
                return false;
              }
              const uint64_t run_bytes = 8u + count * item_bytes;
              if (run_bytes > file_size || pos > file_size - run_bytes) {
                AddError("List-op ValueRep run is truncated");
                return false;
              }
              pos += run_bytes;
            }
          }
        }
      }
    }
  }

  return true;
}

bool CrateReader::Impl::ReadFieldsets() {
  const CrateSection* section = toc_.find("FIELDSETS");
  if (!section) {
    AddError("Missing FIELDSETS section");
    return false;
  }

  if (!reader()->seek(static_cast<size_t>(section->start))) {
    AddError("Failed to seek to FIELDSETS");
    return false;
  }

  uint64_t num_fieldsets;
  if (!reader()->read_u64(num_fieldsets)) {
    AddError("Failed to read fieldset count");
    return false;
  }
  // Publish the count NOW (synchronously, before any async DecodeFieldsetsPayload
  // dispatch) so ReadSpecs can bounds-check without racing the worker's resize.
  num_fieldsets_ = num_fieldsets;

  // fieldset_indices_ entries index into fields_; bound the count to avoid a
  // huge allocation from a malformed value (no dedicated max, reuse max_fields).
  size_t max_fieldsets = options_.max_fields;
  if (options_.max_fields >= (std::numeric_limits<size_t>::max)() -
                                   options_.max_specs) {
    max_fieldsets = std::numeric_limits<size_t>::max();
  } else {
    max_fieldsets = options_.max_fields + options_.max_specs;
  }
  if (num_fieldsets > max_fieldsets) {
    AddError("Too many fieldset indices");
    return false;
  }
  if (!CheckElementAllocation(num_fieldsets, sizeof(uint32_t),
                              "Fieldset index table")) {
    return false;
  }
  size_t fieldset_index_bytes = 0;
  if (!safe::mul(num_fieldsets, sizeof(uint32_t), &fieldset_index_bytes)) {
    AddError("Fieldset index byte size overflow");
    return false;
  }
  (void)fieldset_index_bytes;  // early overflow validation; decode recomputes

  if (section->size < 8) {
    AddError("FIELDSETS section too small");
    return false;
  }
  size_t data_size = static_cast<size_t>(section->size) - 8;
  if (!CheckByteAllocation(static_cast<uint64_t>(data_size), "Fieldset data")) {
    return false;
  }
  std::vector<uint8_t> data;
  if (!reader()->read(data, data_size)) {
    AddError("Failed to read fieldset data");
    return false;
  }

#if defined(TINYUSDZ_NEXT_PARALLEL_BUILD_STAGE)
  {
    int nt = options_.num_threads;
    if (nt <= 0) {
      nt = static_cast<int>(std::thread::hardware_concurrency());
      if (nt < 1) nt = 1;
      nt = std::min(nt, 8);
    }
    if (nt > 1 && num_fieldsets >= 65536) {
      // The fieldset payload decode (decompress + span/index builds) touches
      // only fieldset members, which nothing reads until BuildStage — run it
      // on the pool overlapped with the SPECS/PATHS section reads and join in
      // ParseFromSource right before BuildStage.
      fieldsets_pending_ = std::make_unique<PendingFieldsetsDecode>();
      PendingFieldsetsDecode* pd = fieldsets_pending_.get();
      auto data_ptr = std::make_shared<std::vector<uint8_t>>(std::move(data));
      const uint64_t nfs = num_fieldsets;
      // Snapshot the stream view NOW (main thread): the live reader_ cursor
      // keeps moving through SPECS/PATHS while the decode runs, and copying
      // it from the worker would race on the position field.
      const StreamReader reader_snapshot = *reader_;
      const bool submitted = SubmitPoolTask(
          options_.num_threads, [this, pd, data_ptr, nfs, reader_snapshot]() {
            ThreadDecodeCtx decode_ctx(reader_snapshot);
            ScopedThreadDecodeCtx scoped_ctx(&decode_ctx);
            const bool ok = DecodeFieldsetsPayload(*data_ptr, nfs);
            std::lock_guard<std::mutex> lock(pd->mu);
            pd->ok = ok;
            pd->done = true;
            pd->cv.notify_all();
          });
      if (submitted) return true;  // decode in flight
      fieldsets_pending_.reset();
      data = std::move(*data_ptr);
    }
  }
#endif
  return DecodeFieldsetsPayload(data, num_fieldsets);
}

bool CrateReader::Impl::JoinFieldsetsDecode() {
#if defined(TINYUSDZ_NEXT_PARALLEL_BUILD_STAGE)
  if (fieldsets_pending_) {
    PendingFieldsetsDecode* pd = fieldsets_pending_.get();
    bool ok = false;
    {
      std::unique_lock<std::mutex> lock(pd->mu);
      pd->cv.wait(lock, [pd]() { return pd->done; });
      ok = pd->ok;
    }
    fieldsets_pending_.reset();
    return ok;
  }
#endif
  return true;
}

bool CrateReader::Impl::DecodeFieldsetsPayload(
    const std::vector<uint8_t>& data, uint64_t num_fieldsets) {
  size_t fieldset_index_bytes = 0;
  if (!safe::mul(num_fieldsets, sizeof(uint32_t), &fieldset_index_bytes)) {
    AddError("Fieldset index byte size overflow");
    return false;
  }
  fieldset_indices_.resize(static_cast<size_t>(num_fieldsets));
  DecompressResult dr = DecompressCompressedU32(data.data(), data.size(),
                                                 fieldset_indices_.data(),
                                                 static_cast<size_t>(num_fieldsets));
  if (!dr.success) {
    // Legacy fallback: try EncodeIntegers (common-prefix, no LZ4)
    dr = DecompressIntegers(data.data(), data.size(),
                            static_cast<size_t>(num_fieldsets), false);
    if (!dr.success) {
      AddError("Failed to decompress fieldsets: " + dr.error);
      return false;
    }
    if (dr.data.size() < fieldset_index_bytes) {
      AddError("Decompressed fieldsets shorter than expected");
      return false;
    }
    if (fieldset_index_bytes > 0) {
      std::memcpy(fieldset_indices_.data(), dr.data.data(), fieldset_index_bytes);
    }
  }

  // Build compact span lookup (start/count) for each fieldset. This avoids
  // rescanning fieldset_indices_ for every spec during stage-build.
  fieldset_offsets_.clear();
  fieldset_counts_.clear();
  fieldset_offsets_.reserve(static_cast<size_t>(num_fieldsets));
  fieldset_counts_.reserve(static_cast<size_t>(num_fieldsets));
  size_t pos = 0;
  while (pos < fieldset_indices_.size()) {
    const size_t start = pos;
    while (pos < fieldset_indices_.size() && fieldset_indices_[pos] != 0xFFFFFFFFu) {
      ++pos;
    }
    if (pos >= fieldset_indices_.size()) {
      AddError("Fieldset indices missing terminator");
      return false;
    }
    const size_t span = pos - start;
    if (span > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
      AddError("Fieldset length overflow");
      return false;
    }
    fieldset_offsets_.push_back(static_cast<uint32_t>(start));
    fieldset_counts_.push_back(static_cast<uint32_t>(span));
    ++pos;
  }

  fieldset_index_to_id_.clear();
  if (!fieldset_indices_.empty()) {
    if (!CheckByteAllocation(fieldset_indices_.size() * sizeof(uint32_t),
                             "Fieldset index lookup table")) {
      return false;
    }
    fieldset_index_to_id_.assign(fieldset_indices_.size(),
                                static_cast<uint32_t>(0xFFFFFFFFu));
    for (uint32_t fi = 0; fi < fieldset_offsets_.size(); ++fi) {
      const size_t start = static_cast<size_t>(fieldset_offsets_[fi]);
      if (start >= fieldset_index_to_id_.size()) {
        AddError("Fieldset offset out of range");
        return false;
      }
      fieldset_index_to_id_[start] = fi;
    }
  }

  return true;
}

bool CrateReader::Impl::ReadSpecs() {
  const CrateSection* section = toc_.find("SPECS");
  if (!section) {
    AddError("Missing SPECS section");
    return false;
  }

  if (!reader()->seek(static_cast<size_t>(section->start))) {
    AddError("Failed to seek to SPECS");
    return false;
  }

  uint64_t num_specs;
  if (!reader()->read_u64(num_specs)) {
    AddError("Failed to read spec count");
    return false;
  }

  if (num_specs > options_.max_specs) {
    AddError("Too many specs");
    return false;
  }
  if (!CheckElementAllocation(num_specs, sizeof(CrateSpec), "Spec table") ||
      !CheckElementAllocation(num_specs, sizeof(uint32_t), "Spec path table") ||
      !CheckElementAllocation(num_specs, sizeof(uint32_t), "Spec fieldset table") ||
      !CheckElementAllocation(num_specs, sizeof(uint32_t), "Spec type table")) {
    return false;
  }

  if (section->size < 8) {
    AddError("SPECS section too small");
    return false;
  }

  specs_.resize(static_cast<size_t>(num_specs));

  uint64_t remaining_size = static_cast<uint64_t>(section->size) - 8;

  // Read 3 compressed integer arrays (delta+LZ4 Usd_IntegerCompression format)
  auto read_comp_array = [&](uint32_t* dst, size_t count, uint64_t& remaining) -> bool {
    if (remaining < 8) {
      AddError("Specs section: not enough data for array size");
      return false;
    }
    uint64_t comp_size;
    if (!reader()->read_u64(comp_size)) {
      AddError("Failed to read specs array compressed size");
      return false;
    }
    remaining -= 8;

    if (comp_size > remaining) {
      AddError("Specs compressed size exceeds remaining section data");
      return false;
    }

    // Include u64 compressed_size prefix (DecompressCompressedU32 expects it)
    std::vector<uint8_t> comp_data(8 + static_cast<size_t>(comp_size));
    std::memcpy(comp_data.data(), &comp_size, 8);
    if (!reader()->read(comp_data.data() + 8, static_cast<size_t>(comp_size))) {
      AddError("Failed to read specs compressed data");
      return false;
    }
    remaining -= comp_size;

    DecompressResult dr = DecompressCompressedU32(comp_data.data(), comp_data.size(),
                                                   dst, count);
    if (!dr.success) {
      // Fallback: try legacy EncodeIntegers (common-prefix, no LZ4)
      dr = DecompressIntegers(comp_data.data(), comp_data.size(), count, false);
      if (!dr.success) {
        AddError("Failed to decompress specs array: " + dr.error);
        return false;
      }
      size_t expected_bytes = 0;
      if (!safe::mul(count, sizeof(uint32_t), &expected_bytes)) {
        AddError("Specs array byte size overflow");
        return false;
      }
      if (dr.data.size() < expected_bytes) {
        AddError("Decompressed specs array shorter than expected");
        return false;
      }
      if (expected_bytes > 0) std::memcpy(dst, dr.data.data(), expected_bytes);
    }
    return true;
  };

  std::vector<uint32_t> path_vals(static_cast<size_t>(num_specs));
  std::vector<uint32_t> fieldset_vals(static_cast<size_t>(num_specs));
  std::vector<uint32_t> type_vals(static_cast<size_t>(num_specs));

  if (read_comp_array(path_vals.data(), static_cast<size_t>(num_specs), remaining_size) &&
      read_comp_array(fieldset_vals.data(), static_cast<size_t>(num_specs), remaining_size) &&
      read_comp_array(type_vals.data(), static_cast<size_t>(num_specs), remaining_size)) {
    for (size_t i = 0; i < num_specs; i++) {
      if (fieldset_vals[i] >= num_fieldsets_) {
        AddError("Spec fieldset index out of range");
        return false;
      }
      specs_[i].path_index.value = path_vals[i];
      specs_[i].fieldset_index.value = fieldset_vals[i];
      specs_[i].spec_type = static_cast<SpecType>(type_vals[i]);
    }
    return true;
  }

  // Fallback: try legacy single-array format
  if (!reader()->seek(static_cast<size_t>(section->start) + 8)) {
    AddError("Failed to seek to specs data for legacy read");
    return false;
  }
  size_t legacy_size = static_cast<size_t>(section->size) - 8;
  size_t legacy_plain_size = 0;
  size_t legacy_value_count = 0;
  if (!safe::mul(num_specs, size_t(12), &legacy_plain_size) ||
      !safe::mul(num_specs, size_t(3), &legacy_value_count)) {
    AddError("Specs legacy byte size overflow");
    return false;
  }
  std::vector<uint8_t> legacy_data(legacy_size);
  if (!reader()->read(legacy_data.data(), legacy_size)) {
    AddError("Failed to read specs legacy data");
    return false;
  }
  if (legacy_size == legacy_plain_size) {
    const uint8_t* ptr = legacy_data.data();
    for (size_t i = 0; i < num_specs; i++) {
      std::memcpy(&specs_[i].path_index.value, ptr, 4); ptr += 4;
      std::memcpy(&specs_[i].fieldset_index.value, ptr, 4); ptr += 4;
      if (specs_[i].fieldset_index.value >= num_fieldsets_) {
        AddError("Spec fieldset index out of range");
        return false;
      }
      uint32_t spec_type;
      std::memcpy(&spec_type, ptr, 4); ptr += 4;
      specs_[i].spec_type = static_cast<SpecType>(spec_type);
    }
  } else {
    DecompressResult dr = DecompressIntegers(legacy_data.data(), legacy_data.size(),
                                              legacy_value_count, false);
    if (dr.success) {
      const uint32_t* vals = reinterpret_cast<const uint32_t*>(dr.data.data());
      for (size_t i = 0; i < num_specs; i++) {
        if (vals[i * 3 + 1] >= num_fieldsets_) {
          AddError("Spec fieldset index out of range");
          return false;
        }
        specs_[i].path_index.value = vals[i * 3];
        specs_[i].fieldset_index.value = vals[i * 3 + 1];
        specs_[i].spec_type = static_cast<SpecType>(vals[i * 3 + 2]);
      }
    } else {
      AddError("Failed to decompress specs with any format");
      return false;
    }
  }

  return true;
}

bool CrateReader::Impl::ReadPaths() {
  const CrateSection* section = toc_.find("PATHS");
  if (!section) {
    AddError("Missing PATHS section");
    return false;
  }

  if (!reader()->seek(static_cast<size_t>(section->start))) {
    AddError("Failed to seek to PATHS");
    return false;
  }

  uint64_t num_paths;
  if (!reader()->read_u64(num_paths)) {
    AddError("Failed to read path count");
    return false;
  }

  if (num_paths == 0) {
    paths_.resize(1);
    paths_.set(0, "/");
    return true;
  }

  if (num_paths > options_.max_paths) {
    AddError("Too many paths");
    return false;
  }

  // pxrUSD writes two u64 values: [total_path_count] [encoded_tree_node_count]
  uint64_t num_encoded = num_paths;  // default: same as total
  if (!reader()->read_u64(num_encoded)) {
    // Might be a single-count format (no encoded count)
    num_encoded = num_paths;
  }

  if (num_encoded > options_.max_paths) {
    AddError("Too many encoded path nodes");
    return false;
  }

  if (!CheckElementAllocation(num_paths, sizeof(std::string), "Path table")) {
    return false;
  }
  paths_.resize(static_cast<size_t>(num_paths));
  if (paths_.size() > 0) {
    paths_.set(0, "/");
  }
  size_t n = static_cast<size_t>(num_encoded);

  // Read 3 compressed integer arrays (delta+LZ4 format). Reads (stream
  // cursor) and decodes are split so the parallel gate can overlap the three
  // independent decompressions.
  auto read_comp_blob = [&](std::vector<uint8_t>& out, const char* name) -> bool {
    uint64_t comp_size;
    if (!reader()->read_u64(comp_size)) {
      AddError(std::string("Failed to read ") + name + " compressed size");
      return false;
    }
    // Bound the compressed size against remaining bytes before allocating.
    if (comp_size > reader()->remaining()) {
      AddError(std::string(name) + " compressed size exceeds remaining data");
      return false;
    }
    // Include u64 compressed_size prefix (DecompressCompressedU32 expects it)
    out.resize(8 + static_cast<size_t>(comp_size));
    std::memcpy(out.data(), &comp_size, 8);
    if (!reader()->read(out.data() + 8, static_cast<size_t>(comp_size))) {
      AddError(std::string("Failed to read ") + name + " compressed data");
      return false;
    }
    return true;
  };
  auto decode_comp_array = [&](const std::vector<uint8_t>& comp_data,
                               uint32_t* dst, size_t count,
                               const char* name) -> bool {
    DecompressResult dr = DecompressCompressedU32(comp_data.data(), comp_data.size(),
                                                   dst, count);
    if (!dr.success) {
      // Fallback: legacy EncodeIntegers (common-prefix, no LZ4)
      dr = DecompressIntegers(comp_data.data() + 8, comp_data.size() - 8, count, false);
      if (!dr.success) {
        AddError(std::string("Failed to decompress ") + name + ": " + dr.error);
        return false;
      }
      size_t expected_bytes = 0;
      if (!safe::mul(count, sizeof(uint32_t), &expected_bytes)) {
        AddError(std::string(name) + " byte size overflow");
        return false;
      }
      if (dr.data.size() < expected_bytes) {
        AddError(std::string("Decompressed ") + name + " shorter than expected");
        return false;
      }
      if (expected_bytes > 0) std::memcpy(dst, dr.data.data(), expected_bytes);
    }
    return true;
  };

  std::vector<uint32_t> path_indices(n);
  std::vector<uint32_t> element_tokens(n);
  std::vector<uint32_t> jump_raw(n);  // stored as uint32_t, interpreted as int32_t

  {
    std::vector<uint8_t> blob_pi, blob_et, blob_j;
    if (!read_comp_blob(blob_pi, "path indices") ||
        !read_comp_blob(blob_et, "element tokens") ||
        !read_comp_blob(blob_j, "jump indices")) {
      return false;
    }
    bool ok_pi = false, ok_et = false, ok_j = false;
    bool overlapped = false;
#if defined(TINYUSDZ_NEXT_PARALLEL_BUILD_STAGE)
    if (n >= 65536) {
      std::mutex done_mu;
      std::condition_variable done_cv;
      int remaining = 2;
      auto submit = [&](std::function<void()> fn) {
        return SubmitPoolTask(options_.num_threads, std::move(fn));
      };
      const bool s1 = submit([&]() {
        ok_pi = decode_comp_array(blob_pi, path_indices.data(), n, "path indices");
        std::lock_guard<std::mutex> lock(done_mu);
        if (--remaining == 0) done_cv.notify_all();
      });
      const bool s2 = s1 && submit([&]() {
        ok_et = decode_comp_array(blob_et, element_tokens.data(), n, "element tokens");
        std::lock_guard<std::mutex> lock(done_mu);
        if (--remaining == 0) done_cv.notify_all();
      });
      if (s1 && s2) {
        ok_j = decode_comp_array(blob_j, jump_raw.data(), n, "jump indices");
        std::unique_lock<std::mutex> lock(done_mu);
        done_cv.wait(lock, [&]() { return remaining == 0; });
        overlapped = true;
      } else if (s1) {
        // Only the first task went out: decode the rest here, then join it.
        ok_et = decode_comp_array(blob_et, element_tokens.data(), n, "element tokens");
        ok_j = decode_comp_array(blob_j, jump_raw.data(), n, "jump indices");
        std::unique_lock<std::mutex> lock(done_mu);
        done_cv.wait(lock, [&]() { return remaining <= 1; });
        overlapped = true;
      }
    }
#endif
    if (!overlapped) {
      ok_pi = decode_comp_array(blob_pi, path_indices.data(), n, "path indices");
      ok_et = decode_comp_array(blob_et, element_tokens.data(), n, "element tokens");
      ok_j = decode_comp_array(blob_j, jump_raw.data(), n, "jump indices");
    }
    if (!ok_pi || !ok_et || !ok_j) return false;
  }

  std::vector<uint8_t> seen_path_slot(static_cast<size_t>(num_paths), uint8_t{0});
  for (size_t i = 0; i < n; ++i) {
    if (path_indices[i] >= num_paths) {
      AddError("Path table index out of range");
      return false;
    }
    if (seen_path_slot[path_indices[i]]) {
      AddError("Duplicate path table index");
      return false;
    }
    seen_path_slot[path_indices[i]] = uint8_t{1};

    int32_t elem_token = static_cast<int32_t>(element_tokens[i]);
    const bool is_prop = elem_token < 0;
    uint32_t token_idx = is_prop
        ? static_cast<uint32_t>(-static_cast<int64_t>(elem_token))
        : static_cast<uint32_t>(elem_token);
    if (token_idx >= tokens_.size()) {
      AddError("Path element token index out of range");
      return false;
    }

    int32_t jump = static_cast<int32_t>(jump_raw[i]);
    if (jump < -2) {
      AddError("Invalid path jump value");
      return false;
    }
    if (jump > 0 && i + static_cast<size_t>(jump) >= n) {
      AddError("Path jump points outside encoded path table");
      return false;
    }
  }

  // Reconstruct paths from the compressed tree by navigating jump offsets.
  //
  // Nodes are emitted in pre-order. jump semantics (matching the writer):
  //   jump  > 0 : node has a child (at i+1) AND a sibling (at i+jump)
  //   jump == 0 : node has a sibling only (at i+1), no child
  //   jump == -1: node has a child only (at i+1), no sibling
  //   jump == -2: leaf (no child, no sibling)
  //
  // The previous decoder kept a flat ancestor stack and popped only ONCE per
  // leaf, so it could not unwind multiple levels at a subtree boundary — the
  // stack grew without bound on deep/many-sibling trees, giving O(n^2) memory
  // (every path got longer) and corrupted/colliding paths. Passing the parent
  // path down the recursion and following the jump offset to each sibling is
  // O(num_nodes) and correct.
  paths_.resize(static_cast<size_t>(num_paths));  // all slots -> empty span

  auto element_for = [&](size_t i, bool& is_prop, std::string_view& elem) -> bool {
    int32_t elem_token = static_cast<int32_t>(element_tokens[i]);
    is_prop = elem_token < 0;
    // Promote to int64 before negating (-INT32_MIN is UB).
    uint32_t token_idx = is_prop
        ? static_cast<uint32_t>(-static_cast<int64_t>(elem_token))
        : static_cast<uint32_t>(elem_token);
    if (token_idx >= tokens_.size()) {
      AddError("Path element token index out of range");
      return false;
    }
    elem = tokens_.view(token_idx);
    return true;
  };

  bool parallel_paths_done = false;
#if defined(TINYUSDZ_NEXT_PARALLEL_BUILD_STAGE)
  {
    int nt = options_.num_threads;
    if (nt <= 0) {
      nt = static_cast<int>(std::thread::hardware_concurrency());
      if (nt < 1) nt = 1;
      nt = std::min(nt, 8);
    }
    if (nt > 1 && n >= 65536) {
      // Three passes replacing the (retained) serial walk below with identical
      // stored strings:
      //   A. sequential STRUCTURAL walk — same traversal (incl. the
      //      max_path_depth descend cap), but records only parent links, no
      //      string building. Visitation order is strictly increasing, so
      //      parent_of[i] < i always.
      //   B. sequential length pass applying the exact serial join rules.
      //   C. PARALLEL fill: each path is assembled independently by walking
      //      its ancestor chain backward into an exact-size buffer (no
      //      dependency on the parent's built string), then stored into its
      //      unique paths_ slot (slot uniqueness validated above).
      constexpr uint32_t kNoParent = UINT32_MAX;
      std::vector<uint32_t> parent_of(n, kNoParent);
      std::vector<uint8_t> visited(n, 0);
      struct WalkFrame {
        size_t next_index;
        uint32_t parent;
        size_t depth;
      };
      std::vector<WalkFrame> wstack;
      wstack.reserve(options_.max_path_depth + 1);
      {
        size_t i = 0;
        size_t depth = 0;
        uint32_t cur_parent = kNoParent;
        while (i < n) {
          visited[i] = 1;
          parent_of[i] = cur_parent;
          const int32_t jump = static_cast<int32_t>(jump_raw[i]);
          const bool has_child = (jump == -1 || jump > 0);
          const bool has_sibling = (jump == 0 || jump > 0);
          const bool descend = has_child && (depth < options_.max_path_depth);
          if (descend) {
            if (has_sibling) {
              wstack.push_back({jump > 0 ? static_cast<size_t>(i + jump) : i + 1,
                                cur_parent, depth});
            }
            cur_parent = static_cast<uint32_t>(i);
            i = i + 1;
            ++depth;
            continue;
          }
          if (!has_sibling) {
            if (wstack.empty()) break;
            const WalkFrame f = wstack.back();
            wstack.pop_back();
            i = f.next_index;
            cur_parent = f.parent;
            depth = f.depth;
            continue;
          }
          i = jump > 0 ? static_cast<size_t>(i + jump) : i + 1;
        }
      }

      // Element view (sign already validated above).
      auto elem_of = [&](size_t i) -> std::string_view {
        const int32_t elem_token = static_cast<int32_t>(element_tokens[i]);
        const uint32_t token_idx =
            elem_token < 0
                ? static_cast<uint32_t>(-static_cast<int64_t>(elem_token))
                : static_cast<uint32_t>(elem_token);
        return tokens_.view(token_idx);
      };

      std::vector<uint32_t> plen(n, 0);
      std::vector<uint8_t> rootish(n, 0);  // node path is exactly "/"
      for (size_t i = 0; i < n; ++i) {
        if (!visited[i]) continue;
        const std::string_view elem = elem_of(i);
        const uint32_t par = parent_of[i];
        const size_t base = (par == kNoParent) ? 0 : plen[par];
        if (base == 0) {
          if (elem.empty() || elem == "/") {
            plen[i] = 1;
            rootish[i] = 1;
          } else {
            plen[i] = static_cast<uint32_t>(1 + elem.size());
          }
        } else if (base == 1) {  // parent is "/"
          plen[i] = static_cast<uint32_t>(1 + elem.size());
        } else {
          plen[i] = static_cast<uint32_t>(base + 1 + elem.size());
        }
      }

      // Per-node blob windows: serial prefix sum over encoded (visited)
      // nodes; property paths carry one extra byte for the '.' prefix. After
      // the single resize_blob() below the arena never grows, so workers can
      // write bytes straight into their disjoint windows (no per-path
      // std::string at all) and record spans with place() — views handed out
      // later stay stable (see PathPool).
      std::vector<uint64_t> node_off(n, 0);
      uint64_t total_path_bytes = 0;
      for (size_t i = 0; i < n; ++i) {
        if (!visited[i]) continue;
        node_off[i] = total_path_bytes;
        const bool is_prop = static_cast<int32_t>(element_tokens[i]) < 0;
        total_path_bytes += plen[i] + (is_prop ? 1 : 0);
      }
      paths_.resize_blob(static_cast<size_t>(total_path_bytes));

      const size_t chunk = std::max<size_t>(
          16384, n / (static_cast<size_t>(nt) * 4));
      struct FillRange {
        size_t begin, end;
      };
      std::vector<FillRange> ranges;
      for (size_t b = 0; b < n; b += chunk) {
        ranges.push_back({b, std::min(n, b + chunk)});
      }
      auto fill_range = [&](const FillRange& r) {
        for (size_t i = r.begin; i < r.end; ++i) {
          if (!visited[i]) continue;
          const uint32_t store_idx = path_indices[i];
          if (store_idx >= num_paths) continue;
          const int32_t elem_token = static_cast<int32_t>(element_tokens[i]);
          const bool is_prop = elem_token < 0;
          const size_t L = plen[i];
          const uint32_t span_len =
              static_cast<uint32_t>(L + (is_prop ? 1 : 0));
          char* buf = paths_.blob_at(node_off[i]);
          if (is_prop) {
            buf[0] = '.';
            ++buf;
          }
          // Fast path: pre-order visitation gives strong tree locality, so a
          // node's parent usually sits EARLIER IN THE SAME RANGE — a range is
          // filled sequentially and parent_of[i] < i always, so the parent's
          // blob window is already written and can be block-copied (one
          // sequential memcpy instead of an ancestor-chain walk of cache
          // misses). Parents are never property nodes (properties are
          // path-tree leaves), but check anyway: a '.'-prefixed parent window
          // would corrupt the copy.
          const uint32_t par_i = parent_of[i];
          if (par_i != kNoParent && par_i >= r.begin && !rootish[i] &&
              static_cast<int32_t>(element_tokens[par_i]) >= 0) {
            const char* pstr = paths_.blob_at(node_off[par_i]);
            const size_t base = plen[par_i];
            if (base >= 1) {
              const std::string_view ei = elem_of(i);
              if (base == 1) {  // parent is "/"
                buf[0] = '/';
                std::memcpy(buf + 1, ei.data(), ei.size());
              } else {
                std::memcpy(buf, pstr, base);
                buf[base] = '/';
                std::memcpy(buf + base + 1, ei.data(), ei.size());
              }
              paths_.place(store_idx, node_off[i], span_len);
              continue;
            }
          }
          // Backward ancestor-chain fill.
          size_t j = i;
          for (;;) {
            if (rootish[j]) {
              buf[0] = '/';
              break;
            }
            const std::string_view ej = elem_of(j);
            const uint32_t par = parent_of[j];
            const size_t base = (par == kNoParent) ? 0 : plen[par];
            if (base <= 1) {
              buf[0] = '/';
              std::memcpy(buf + 1, ej.data(), ej.size());
              break;
            }
            buf[base] = '/';
            std::memcpy(buf + base + 1, ej.data(), ej.size());
            j = par;
          }
          paths_.place(store_idx, node_off[i], span_len);
        }
      };

      std::mutex done_mu;
      std::condition_variable done_cv;
      size_t remaining = ranges.size();
      for (const FillRange& r : ranges) {
        const FillRange* rp = &r;
        auto task = [&, rp]() {
          fill_range(*rp);
          std::lock_guard<std::mutex> lock(done_mu);
          if (--remaining == 0) done_cv.notify_all();
        };
        if (!SubmitPoolTask(options_.num_threads, task)) task();
      }
      if (!ranges.empty()) {
        std::unique_lock<std::mutex> lock(done_mu);
        done_cv.wait(lock, [&]() { return remaining == 0; });
      }
      parallel_paths_done = true;
    }
  }
#endif  // TINYUSDZ_NEXT_PARALLEL_BUILD_STAGE

  if (!parallel_paths_done) {
  struct PathFrame {
    size_t next_index = 0;
    size_t parent_path_len = 0;
    size_t depth = 0;
  };
  std::vector<PathFrame> stack;
  stack.reserve(options_.max_path_depth + 1);

  size_t i = 0;
  size_t depth = 0;
  std::string parent_path;
  std::string prop_scratch;  // reused ".<primpath>/<prop>" build buffer
  while (i < n) {
    bool is_prop = false;
    std::string_view elem;
    if (!element_for(i, is_prop, elem)) return false;
    int32_t jump = static_cast<int32_t>(jump_raw[i]);

    const bool is_root = parent_path.empty() && (elem.empty() || elem == "/");
    const size_t parent_len = parent_path.size();
    if (is_root) {
      parent_path = "/";
    } else if (parent_path.empty()) {
      parent_path = "/";
      parent_path.append(elem);
    } else if (parent_path == "/") {
      parent_path.append(elem);
    } else {
      parent_path.push_back('/');
      parent_path.append(elem);
    }

    const uint32_t store_idx = path_indices[i];
    if (store_idx < num_paths) {
      if (is_prop) {
        prop_scratch.clear();
        prop_scratch.reserve(parent_path.size() + 1);
        prop_scratch.push_back('.');
        prop_scratch.append(parent_path);
        paths_.set(store_idx, prop_scratch);
      } else {
        paths_.set(store_idx, parent_path);
      }
    }

    const bool has_child = (jump == -1 || jump > 0);
    const bool has_sibling = (jump == 0 || jump > 0);
    const bool descend = has_child && (depth < options_.max_path_depth);
    if (descend) {
      if (has_sibling) {
        stack.push_back({jump > 0 ? static_cast<size_t>(i + jump) : i + 1,
                         parent_len,
                         depth});
      }
      i = i + 1;
      ++depth;
      continue;
    }

    if (!has_sibling) {
      if (stack.empty()) break;
      const PathFrame frame = stack.back();
      stack.pop_back();
      i = frame.next_index;
      parent_path.resize(frame.parent_path_len);
      depth = frame.depth;
      continue;
    }

    parent_path.resize(parent_len);
    i = jump > 0 ? static_cast<size_t>(i + jump) : i + 1;
  }
  }  // !parallel_paths_done (retained serial walk)

  for (const CrateSpec& spec : specs_) {
    if (spec.path_index.value >= paths_.size()) {
      AddError("Spec path index out of range");
      return false;
    }
    if (paths_.empty_at(spec.path_index.value)) {
      AddError("Spec path index references an empty path slot");
      return false;
    }
  }

  return true;
}


}  // namespace next
}  // namespace tinyusdz
