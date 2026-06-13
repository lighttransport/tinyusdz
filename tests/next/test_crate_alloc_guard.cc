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

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "next/crate/crate-data-source.hh"
#include "next/crate/crate-format.hh"
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

int main() {
  std::cout << "=== TinyUSDZ Next Crate Allocation-Guard Test ===" << std::endl;

  // Element types spanning the largest in-memory strides.
  assert(RejectsHugeArray(CrateTypeId::Matrix4d));  // 128 B/elem
  assert(RejectsHugeArray(CrateTypeId::Double));    // 8 B/elem
  assert(RejectsHugeArray(CrateTypeId::Vec3f));     // 12 B/elem
  assert(RejectsHugeArray(CrateTypeId::Int));       // 4 B/elem
  assert(RejectsHugeArray(CrateTypeId::Token));     // 4 B index/elem

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

  std::cout << "  Allocation-guard test passed!" << std::endl;
  return 0;
}
