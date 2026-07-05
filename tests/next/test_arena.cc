// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tests for the next-core support allocators: Arena (bump region) and
// small_vector (inline-storage vector).

#include "test-check.hh"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include "next/support/arena.hh"
#include "next/support/small_vector.hh"

using namespace tinyusdz::next;

// ---------------------------------------------------------------------------
// Arena
// ---------------------------------------------------------------------------

static void test_arena_basic() {
  std::cout << "Testing Arena basic allocation..." << std::endl;
  Arena a;
  NEXT_CHECK(a.bytes_used() == 0);

  auto* p1 = a.alloc_uninitialized<uint32_t>(4);
  NEXT_CHECK(p1 != nullptr);
  for (int i = 0; i < 4; ++i) p1[i] = uint32_t(i * 7);
  for (int i = 0; i < 4; ++i) NEXT_CHECK(p1[i] == uint32_t(i * 7));

  auto* p2 = a.alloc_uninitialized<uint64_t>(2);
  NEXT_CHECK(p2 != nullptr);
  // Distinct, non-overlapping regions.
  NEXT_CHECK(reinterpret_cast<char*>(p2) >= reinterpret_cast<char*>(p1) + 16);
  NEXT_CHECK(a.bytes_used() >= 4 * sizeof(uint32_t) + 2 * sizeof(uint64_t));
}

static void test_arena_alignment() {
  std::cout << "Testing Arena alignment..." << std::endl;
  Arena a;
  // Interleave odd-sized and aligned requests; every typed pointer must be
  // correctly aligned for its type.
  for (int i = 0; i < 200; ++i) {
    (void)a.allocate(1, 1);  // deliberately misalign the offset
    auto* d = a.alloc_uninitialized<double>(1);
    NEXT_CHECK((reinterpret_cast<uintptr_t>(d) % alignof(double)) == 0);
    *d = double(i);
    NEXT_CHECK(*d == double(i));
    auto* q = a.alloc_uninitialized<uint32_t>(1);
    NEXT_CHECK((reinterpret_cast<uintptr_t>(q) % alignof(uint32_t)) == 0);
  }
}

static void test_arena_block_spanning() {
  std::cout << "Testing Arena multi-block growth..." << std::endl;
  Arena a(256);  // small first block to force growth
  // Allocate well past a single block; keep pointers and verify integrity.
  const int kN = 5000;
  uint32_t** ptrs = new uint32_t*[kN];
  for (int i = 0; i < kN; ++i) {
    ptrs[i] = a.alloc_uninitialized<uint32_t>(1);
    *ptrs[i] = uint32_t(i);
  }
  for (int i = 0; i < kN; ++i) NEXT_CHECK(*ptrs[i] == uint32_t(i));
  NEXT_CHECK(a.bytes_reserved() > 256);  // grew beyond the first block
  delete[] ptrs;
}

static void test_arena_large_alloc() {
  std::cout << "Testing Arena oversized allocation..." << std::endl;
  Arena a(64);  // tiny default block; request is much larger
  auto* big = static_cast<char*>(a.allocate(1 << 16));
  NEXT_CHECK(big != nullptr);
  std::memset(big, 0xAB, 1 << 16);
  NEXT_CHECK(static_cast<unsigned char>(big[0]) == 0xAB);
  NEXT_CHECK(static_cast<unsigned char>(big[(1 << 16) - 1]) == 0xAB);
}

static void test_arena_copy_string() {
  std::cout << "Testing Arena copy_string..." << std::endl;
  Arena a;
  std::string_view s = a.copy_string("hello world");
  NEXT_CHECK(s.size() == 11);
  NEXT_CHECK(s == "hello world");
  NEXT_CHECK(s.data()[s.size()] == '\0');  // NUL-terminated

  std::string_view e = a.copy_string("");
  NEXT_CHECK(e.size() == 0);
  NEXT_CHECK(e.data() != nullptr);
  NEXT_CHECK(e.data()[0] == '\0');
}

static void test_arena_reset_reuse() {
  std::cout << "Testing Arena reset/reuse..." << std::endl;
  Arena a(4096);
  void* first = a.allocate(1000);
  (void)a.allocate(3000);
  const size_t reserved_before = a.bytes_reserved();
  NEXT_CHECK(a.bytes_used() >= 4000);

  a.reset();
  NEXT_CHECK(a.bytes_used() == 0);
  // Reset must not free: reserved memory is retained for reuse.
  NEXT_CHECK(a.bytes_reserved() == reserved_before);

  void* again = a.allocate(1000);
  // The first retained block is reused, so the pointer matches.
  NEXT_CHECK(again == first);
  NEXT_CHECK(a.bytes_reserved() == reserved_before);  // no new malloc
}

static void test_arena_move() {
  std::cout << "Testing Arena move semantics..." << std::endl;
  Arena a;
  auto* p = a.alloc_uninitialized<int>(1);
  *p = 42;
  const size_t used = a.bytes_used();

  Arena b(std::move(a));
  NEXT_CHECK(b.bytes_used() == used);
  NEXT_CHECK(*p == 42);          // memory still valid after move
  NEXT_CHECK(a.bytes_used() == 0);   // NOLINT: moved-from is empty/usable
  NEXT_CHECK(a.bytes_reserved() == 0);

  Arena c;
  c = std::move(b);
  NEXT_CHECK(c.bytes_used() == used);
  NEXT_CHECK(*p == 42);
}

// ---------------------------------------------------------------------------
// small_vector
// ---------------------------------------------------------------------------

static void test_sv_inline() {
  std::cout << "Testing small_vector inline storage..." << std::endl;
  small_vector<uint32_t, 4> v;
  NEXT_CHECK(v.empty() && v.inline_storage());
  for (uint32_t i = 0; i < 4; ++i) v.push_back(i);
  NEXT_CHECK(v.size() == 4);
  NEXT_CHECK(v.inline_storage());  // still inline at exactly N
  NEXT_CHECK(v.front() == 0 && v.back() == 3);
  for (uint32_t i = 0; i < 4; ++i) NEXT_CHECK(v[i] == i);
}

static void test_sv_spill() {
  std::cout << "Testing small_vector heap spill..." << std::endl;
  small_vector<uint32_t, 4> v;
  for (uint32_t i = 0; i < 1000; ++i) v.push_back(i);
  NEXT_CHECK(v.size() == 1000);
  NEXT_CHECK(!v.inline_storage());  // spilled to heap
  for (uint32_t i = 0; i < 1000; ++i) NEXT_CHECK(v[i] == i);

  // Stack-like use (mirrors LayerBuilder::prim_stack_).
  while (!v.empty()) v.pop_back();
  NEXT_CHECK(v.empty());
}

static void test_sv_nontrivial() {
  std::cout << "Testing small_vector with std::string..." << std::endl;
  small_vector<std::string, 2> v;
  v.push_back("a");
  v.emplace_back("bb");
  NEXT_CHECK(v.inline_storage());
  v.push_back("ccc");  // forces spill; must move-construct existing elements
  NEXT_CHECK(!v.inline_storage());
  NEXT_CHECK(v.size() == 3);
  NEXT_CHECK(v[0] == "a" && v[1] == "bb" && v[2] == "ccc");

  // Copy.
  small_vector<std::string, 2> c = v;
  NEXT_CHECK(c.size() == 3 && c[2] == "ccc");
  // Independent storage: mutating the copy leaves the original intact.
  c[0] = "z";
  NEXT_CHECK(v[0] == "a");
}

static void test_sv_move() {
  std::cout << "Testing small_vector move semantics..." << std::endl;
  // Heap-backed move steals the buffer.
  small_vector<std::string, 2> big;
  for (int i = 0; i < 10; ++i) big.emplace_back(std::to_string(i));
  small_vector<std::string, 2> moved = std::move(big);
  NEXT_CHECK(moved.size() == 10 && moved[9] == "9");
  NEXT_CHECK(big.empty());  // NOLINT: moved-from is empty/usable

  // Inline move relocates elements.
  small_vector<std::string, 4> inl;
  inl.push_back("x");
  inl.push_back("y");
  small_vector<std::string, 4> inl2 = std::move(inl);
  NEXT_CHECK(inl2.size() == 2 && inl2[0] == "x" && inl2[1] == "y");

  // Move assignment onto a populated target.
  small_vector<std::string, 4> tgt;
  tgt.push_back("old");
  tgt = std::move(inl2);
  NEXT_CHECK(tgt.size() == 2 && tgt[1] == "y");
}

static void test_sv_resize() {
  std::cout << "Testing small_vector resize/clear..." << std::endl;
  small_vector<int, 4> v;
  v.resize(3);
  NEXT_CHECK(v.size() == 3 && v[0] == 0 && v[2] == 0);  // value-initialized
  v[1] = 5;
  v.resize(10);
  NEXT_CHECK(v.size() == 10 && v[1] == 5 && v[9] == 0);
  v.resize(2);
  NEXT_CHECK(v.size() == 2 && v[1] == 5);
  v.clear();
  NEXT_CHECK(v.empty());
}

int main() {
  std::cout << "=== Arena / small_vector tests ===" << std::endl;

  test_arena_basic();
  test_arena_alignment();
  test_arena_block_spanning();
  test_arena_large_alloc();
  test_arena_copy_string();
  test_arena_reset_reuse();
  test_arena_move();

  test_sv_inline();
  test_sv_spill();
  test_sv_nontrivial();
  test_sv_move();
  test_sv_resize();

  std::cout << "\nAll arena/small_vector tests passed!" << std::endl;
  return 0;
}
