// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// LightUSD Next - Crate allocation-guard regression test
//
// A malformed/hostile USDC array can claim a gigantic element count (up to
// max_array_elements) in a tiny file; allocating count*elem before the
// file-bounds check would exhaust memory (e.g. 1e9 matrix4d = 128 GB). The
// reader must reject such counts BEFORE allocating. This test drives the array
// decoder directly with a 16-byte buffer claiming 1e9 elements and asserts it
// fails fast without a huge allocation.

#include <cassert>
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

using namespace lightusd::next;

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

static void PutU64(std::string& buf, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    buf.push_back(static_cast<char>((v >> (i * 8)) & 0xffu));
  }
}

static std::shared_ptr<CrateDataSource> MakeSource(std::string bytes) {
  return CrateDataSource::Adopt(std::move(bytes), CrateVersion{0, 8, 0});
}

int main() {
  std::cout << "=== LightUSD Next Crate Allocation-Guard Test ===" << std::endl;

  // Element types spanning the largest in-memory strides.
  assert(RejectsHugeArray(CrateTypeId::Matrix4d));  // 128 B/elem
  assert(RejectsHugeArray(CrateTypeId::Double));    // 8 B/elem
  assert(RejectsHugeArray(CrateTypeId::Vec3f));     // 12 B/elem
  assert(RejectsHugeArray(CrateTypeId::Int));       // 4 B/elem
  assert(RejectsHugeArray(CrateTypeId::Token));     // 4 B index/elem
  assert(RejectsRaisedLimitOverflowCount());
  assert(RejectsCompressedHugeBlob());

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
    assert(ok && out.as_float_array() && out.as_float_array()->size() == 2);
    assert((*out.as_float_array())[0] == 1.5f);
  }

  // AOUSD permits bool arrays to use Crate's compressed-integral encoding.
  // Decode a real delta/LZ4 payload and canonicalize non-zero lanes to true.
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
    assert(ok);
    const std::vector<uint8_t>* arr = out.as_bool_array();
    assert(arr && arr->size() == 3);
    assert((*arr)[0] == 0);
    assert((*arr)[1] == 1);
    assert((*arr)[2] == 1);

    // The compressed bit is authoritative even below the writer's preferred
    // threshold. Exercise both sides of the former 16-element reader gate.
    for (size_t count : {size_t(1), size_t(8), size_t(16)}) {
      std::vector<uint32_t> lanes(count);
      for (size_t i = 0; i < lanes.size(); ++i) {
        lanes[i] = (i == 3) ? 7u : static_cast<uint32_t>(i & 1u);
      }
      CompressResult compressed =
          WriteCompressedU32(lanes.data(), lanes.size());
      assert(compressed.success);
      std::vector<uint8_t> compressed_buf(16, 0);
      const uint64_t lane_count = lanes.size();
      const uint64_t compressed_size = compressed.data.size();
      std::memcpy(compressed_buf.data() + 8, &lane_count, 8);
      const size_t old_size = compressed_buf.size();
      compressed_buf.resize(old_size + 8 + compressed.data.size());
      std::memcpy(compressed_buf.data() + old_size, &compressed_size, 8);
      std::memcpy(compressed_buf.data() + old_size + 8,
                  compressed.data.data(), compressed.data.size());

      auto decode = [&](CrateTypeId type, Value* value) {
        ValueRep compressed_rep = ValueRep::Make(
            type, /*payload=*/8, /*is_array=*/true,
            /*is_inlined=*/false, /*is_compressed=*/true);
        return DecodeCrateArray(compressed_buf.data(), compressed_buf.size(),
                                compressed_rep, tokens,
                                1024ull * 1024 * 1024, value);
      };

      Value compressed_bool;
      assert(decode(CrateTypeId::Bool, &compressed_bool));
      const std::vector<uint8_t>* compressed_arr =
          compressed_bool.as_bool_array();
      assert(compressed_arr && compressed_arr->size() == lanes.size());
      for (size_t i = 0; i < lanes.size(); ++i) {
        assert((*compressed_arr)[i] == (lanes[i] != 0 ? 1 : 0));
      }

      Value compressed_int;
      assert(decode(CrateTypeId::Int, &compressed_int));
      const std::vector<int32_t>* ints = compressed_int.as_int_array();
      assert(ints && ints->size() == lanes.size());
      for (size_t i = 0; i < lanes.size(); ++i) {
        assert((*ints)[i] == static_cast<int32_t>(lanes[i]));
      }
    }
  }

  // max_elements is what bounds the count a COMPRESSED block may advertise.
  // Its decoded size is not derivable from the block (USD integer compression
  // is two-stage -- delta/byte-code then LZ4 -- so no fixed expansion ratio
  // holds), and Value::array_size() hands element_count straight to callers
  // for a lazy value. The lazy reader therefore passes the configured
  // max_array_elements here rather than SIZE_MAX; this pins that the cap is
  // actually enforced.
  {
    std::string bytes(8, '\0');
    PutU64(bytes, 4000000000ull);  // ~4e9 elements ...
    PutU64(bytes, 2ull);           // ... from a 2-byte compressed payload
    bytes.push_back(0);
    bytes.push_back(1);
    auto source = MakeSource(std::move(bytes));
    ValueRep rep = ValueRep::Make(CrateTypeId::Int, /*payload=*/8,
                                  /*is_array=*/true,
                                  /*is_inlined=*/false,
                                  /*is_compressed=*/true);
    LazyArrayRef ref;
    // Rejected under a sane cap ...
    assert(!ProbeArrayBlock(source, rep, size_t(1) << 30, &ref));
    // ... AND with no cap at all: the count is now also bounded by what the
    // compressed payload could physically decode to, so an absurd count can
    // no longer be published just because the caller asked for no limit.
    LazyArrayRef unbounded;
    assert(!ProbeArrayBlock(source, rep,
                            (std::numeric_limits<size_t>::max)(), &unbounded));
  }

  // ... while a count the payload CAN plausibly back is still accepted. USD
  // integer compression spends >= 2 bits per element before LZ4, and LZ4
  // cannot beat ~255:1, so 2 compressed bytes back at most a few thousand
  // elements. 8000 sits inside the margin the bound allows.
  {
    std::string bytes(8, '\0');
    PutU64(bytes, 8000ull);
    PutU64(bytes, 2ull);
    bytes.push_back(0);
    bytes.push_back(1);
    auto source = MakeSource(std::move(bytes));
    ValueRep rep = ValueRep::Make(CrateTypeId::Int, /*payload=*/8,
                                  /*is_array=*/true,
                                  /*is_inlined=*/false,
                                  /*is_compressed=*/true);
    LazyArrayRef ref;
    assert(ProbeArrayBlock(source, rep, size_t(1) << 30, &ref));
    assert(ref.element_count == 8000ull);
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
    assert(ok);
    assert(ref.source.get() == source.get());
    assert(ref.block_offset == 8);
    assert(ref.block_len == 0);
    assert(ref.element_count == 3);
    assert(ref.crate_type == CrateTypeId::Bool);
    assert(ref.value_type == TypeId::Bool);
    assert(ref.src_elem_stride == 1);
    assert(ref.is_compressed);
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
    assert(ok);
    assert(ref.block_offset == 8);
    assert(ref.block_len == 0);
    assert(ref.element_count == 2);
    assert(ref.crate_type == CrateTypeId::Float);
    assert(ref.value_type == TypeId::Float);
    assert(ref.src_elem_stride == 4);
    assert(!ref.is_compressed);
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
    assert(ok);
    assert(ref.block_offset == 8);
    assert(ref.block_len == 16);
    assert(ref.element_count == 2);
    assert(ref.src_elem_stride == 4);
  }

  std::cout << "  Allocation-guard test passed!" << std::endl;
  return 0;
}
