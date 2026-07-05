// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Crate allocation-guard regression test
//
// A malformed/hostile USDC array can claim a gigantic element count (up to
// max_array_elements) in a tiny file; allocating count*elem before the
// file-bounds check would exhaust memory (e.g. 1e9 matrix4d = 128 GB). The
// reader must reject such counts BEFORE allocating. This test drives the array
// decoder directly with a 16-byte buffer claiming 1e9 elements and asserts it
// fails fast without a huge allocation.

#include "test-check.hh"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "next/crate/crate-data-source.hh"
#include "next/crate/crate-format.hh"
#include "next/crate/lazy-array.hh"
#include "next/types/value.hh"

using namespace tinyusdz::next;

static bool RejectsHugeArray(CrateTypeId type) {
  // 16-byte buffer; u64 count = 1e9 at offset 8. The element data is absent.
  std::vector<uint8_t> buf(16, 0);
  uint64_t count = 1000000000ull;
  std::memcpy(buf.data() + 8, &count, 8);
  ValueRep rep = ValueRep::Make(type, /*payload=*/8, /*is_array=*/true,
                                /*is_inlined=*/false);
  std::vector<std::string> tokens;
  Value out;
  // max_elements is the existing 1 GiB cap — the count is *within* it, so the
  // file-size-relative guard (not max_elements) must do the rejecting.
  bool ok = DecodeCrateArray(buf.data(), buf.size(), rep, tokens,
                             1024ull * 1024 * 1024, &out);
  return !ok;  // expected: rejected
}

static void PutU64(std::string& buf, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    buf.push_back(static_cast<char>((v >> (i * 8)) & 0xffu));
  }
}

static std::shared_ptr<CrateDataSource> MakeSource(std::string bytes) {
  return CrateDataSource::Adopt(std::move(bytes), CrateVersion{0, 8, 0});
}

static bool RejectsRaisedLimitOverflowCount() {
  // Simulate a caller raising max_array_elements above the default. The decoded
  // byte count must still be checked without overflowing count * stride.
  std::vector<uint8_t> buf(16, 0);
  uint64_t count = (std::numeric_limits<uint64_t>::max)() / 128ull + 1ull;
  std::memcpy(buf.data() + 8, &count, 8);
  ValueRep rep = ValueRep::Make(CrateTypeId::Matrix4d, /*payload=*/8,
                                /*is_array=*/true, /*is_inlined=*/false);
  std::vector<std::string> tokens;
  Value out;
  bool ok = DecodeCrateArray(buf.data(), buf.size(), rep, tokens,
                             (std::numeric_limits<size_t>::max)(), &out);
  return !ok;
}

static bool RejectsCompressedHugeBlob() {
  // Compressed int arrays read a [u64 compressedSize] before the blob. A forged
  // size must be rejected before the helper allocates the blob/prefixed buffer.
  std::vector<uint8_t> buf(24, 0);
  uint64_t count = 16;
  uint64_t comp_size = (std::numeric_limits<uint64_t>::max)();
  std::memcpy(buf.data() + 8, &count, 8);
  std::memcpy(buf.data() + 16, &comp_size, 8);
  ValueRep rep = ValueRep::Make(CrateTypeId::Int, /*payload=*/8,
                                /*is_array=*/true, /*is_inlined=*/false,
                                /*is_compressed=*/true);
  std::vector<std::string> tokens;
  Value out;
  bool ok = DecodeCrateArray(buf.data(), buf.size(), rep, tokens,
                             1024ull * 1024 * 1024, &out);
  return !ok;
}

int main() {
  std::cout << "=== TinyUSDZ Next Crate Allocation-Guard Test ===" << std::endl;

  // Element types spanning the largest in-memory strides.
  NEXT_CHECK(RejectsHugeArray(CrateTypeId::Matrix4d));  // 128 B/elem
  NEXT_CHECK(RejectsHugeArray(CrateTypeId::Double));    // 8 B/elem
  NEXT_CHECK(RejectsHugeArray(CrateTypeId::Vec3f));     // 12 B/elem
  NEXT_CHECK(RejectsHugeArray(CrateTypeId::Int));       // 4 B/elem
  NEXT_CHECK(RejectsHugeArray(CrateTypeId::Token));     // 4 B index/elem
  NEXT_CHECK(RejectsRaisedLimitOverflowCount());
  NEXT_CHECK(RejectsCompressedHugeBlob());

  // Sanity: a small, fully-present array still decodes. Layout (payload != 0,
  // since payload 0 is the empty-array marker):
  //   [8 B pad][u64 count=2][float a][float b]   rep payload -> offset 8.
  {
    std::vector<uint8_t> buf(8, 0);  // leading pad so the block offset is != 0
    auto put_u64 = [&](uint64_t v) {
      for (int i = 0; i < 8; ++i) buf.push_back(uint8_t(v >> (i * 8)));
    };
    put_u64(2);
    float a = 1.5f, b = -2.0f;
    const uint8_t* pa = reinterpret_cast<const uint8_t*>(&a);
    const uint8_t* pb = reinterpret_cast<const uint8_t*>(&b);
    buf.insert(buf.end(), pa, pa + 4);
    buf.insert(buf.end(), pb, pb + 4);
    ValueRep rep = ValueRep::Make(CrateTypeId::Float, /*payload=*/8,
                                  /*is_array=*/true, /*is_inlined=*/false);
    std::vector<std::string> tokens;
    Value out;
    bool ok = DecodeCrateArray(buf.data(), buf.size(), rep, tokens,
                               1024ull * 1024 * 1024, &out);
    NEXT_CHECK(ok && out.as_float_array() && out.as_float_array()->size() == 2);
    NEXT_CHECK((*out.as_float_array())[0] == 1.5f);
  }

  // Bool arrays are raw byte arrays in this implementation. A compressed bool
  // ValueRep is rejected explicitly rather than guessed as compressed ints.
  {
    std::vector<uint8_t> buf(8, 0);
    auto put_u64 = [&](uint64_t v) {
      for (int i = 0; i < 8; ++i) buf.push_back(uint8_t(v >> (i * 8)));
    };
    put_u64(3);
    buf.push_back(0);
    buf.push_back(1);
    buf.push_back(255);

    ValueRep raw_rep = ValueRep::Make(CrateTypeId::Bool, /*payload=*/8,
                                      /*is_array=*/true,
                                      /*is_inlined=*/false,
                                      /*is_compressed=*/false);
    std::vector<std::string> tokens;
    Value out;
    bool ok = DecodeCrateArray(buf.data(), buf.size(), raw_rep, tokens,
                               1024ull * 1024 * 1024, &out);
    NEXT_CHECK(ok);
    const std::vector<uint8_t>* arr = out.as_bool_array();
    NEXT_CHECK(arr && arr->size() == 3);
    NEXT_CHECK((*arr)[0] == 0);
    NEXT_CHECK((*arr)[1] == 1);
    NEXT_CHECK((*arr)[2] == 1);

    ValueRep compressed_rep = ValueRep::Make(CrateTypeId::Bool,
                                             /*payload=*/8,
                                             /*is_array=*/true,
                                             /*is_inlined=*/false,
                                             /*is_compressed=*/true);
    Value compressed_out;
    bool compressed_ok = DecodeCrateArray(buf.data(), buf.size(),
                                          compressed_rep, tokens,
                                          1024ull * 1024 * 1024,
                                          &compressed_out);
    NEXT_CHECK(!compressed_ok);
    NEXT_CHECK(compressed_out.is_empty());
  }

  // ProbeArrayBlock is intentionally conservative for layouts we cannot safely
  // pass through. It reports the logical count/type but leaves block_len zero,
  // which forces callers to decode/reject instead of copying guessed bytes.
  {
    std::string bytes(8, '\0');
    PutU64(bytes, 3);
    bytes.push_back(0);
    bytes.push_back(1);
    bytes.push_back(static_cast<char>(255));
    auto source = MakeSource(std::move(bytes));
    ValueRep rep = ValueRep::Make(CrateTypeId::Bool, /*payload=*/8,
                                  /*is_array=*/true,
                                  /*is_inlined=*/false,
                                  /*is_compressed=*/true);
    LazyArrayRef ref;
    bool ok = ProbeArrayBlock(source, rep, 1024, &ref);
    NEXT_CHECK(ok);
    NEXT_CHECK(ref.source.get() == source.get());
    NEXT_CHECK(ref.block_offset == 8);
    NEXT_CHECK(ref.block_len == 0);
    NEXT_CHECK(ref.element_count == 3);
    NEXT_CHECK(ref.crate_type == CrateTypeId::Bool);
    NEXT_CHECK(ref.value_type == TypeId::Bool);
    NEXT_CHECK(ref.src_elem_stride == 1);
    NEXT_CHECK(ref.is_compressed);
  }

  // A known raw layout whose payload extends past EOF is also probed without a
  // pass-through byte range. The decoder remains responsible for rejecting it.
  {
    std::string bytes(8, '\0');
    PutU64(bytes, 2);
    auto source = MakeSource(std::move(bytes));
    ValueRep rep = ValueRep::Make(CrateTypeId::Float, /*payload=*/8,
                                  /*is_array=*/true,
                                  /*is_inlined=*/false,
                                  /*is_compressed=*/false);
    LazyArrayRef ref;
    bool ok = ProbeArrayBlock(source, rep, 1024, &ref);
    NEXT_CHECK(ok);
    NEXT_CHECK(ref.block_offset == 8);
    NEXT_CHECK(ref.block_len == 0);
    NEXT_CHECK(ref.element_count == 2);
    NEXT_CHECK(ref.crate_type == CrateTypeId::Float);
    NEXT_CHECK(ref.value_type == TypeId::Float);
    NEXT_CHECK(ref.src_elem_stride == 4);
    NEXT_CHECK(!ref.is_compressed);
  }

  // Fully-present raw POD arrays get an exact pass-through range.
  {
    std::string bytes(8, '\0');
    PutU64(bytes, 2);
    float a = 3.0f;
    float b = -4.0f;
    bytes.append(reinterpret_cast<const char*>(&a), sizeof(a));
    bytes.append(reinterpret_cast<const char*>(&b), sizeof(b));
    auto source = MakeSource(std::move(bytes));
    ValueRep rep = ValueRep::Make(CrateTypeId::Float, /*payload=*/8,
                                  /*is_array=*/true,
                                  /*is_inlined=*/false,
                                  /*is_compressed=*/false);
    LazyArrayRef ref;
    bool ok = ProbeArrayBlock(source, rep, 1024, &ref);
    NEXT_CHECK(ok);
    NEXT_CHECK(ref.block_offset == 8);
    NEXT_CHECK(ref.block_len == 16);
    NEXT_CHECK(ref.element_count == 2);
    NEXT_CHECK(ref.src_elem_stride == 4);
  }

  std::cout << "  Allocation-guard test passed!" << std::endl;
  return 0;
}
