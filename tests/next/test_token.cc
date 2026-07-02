// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tests for the next-core TfToken (interned string handle).

#include <cassert>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "next/types/token.hh"

using namespace tinyusdz::next;

static void test_empty() {
  std::cout << "Testing empty token..." << std::endl;
  TfToken t;
  assert(t.empty());
  assert(t.id() == 0);
  assert(t.str().empty());

  TfToken e("");
  assert(e.empty());
  assert(e == t);  // interning "" yields the reserved id 0
}

static void test_intern_dedup() {
  std::cout << "Testing interning / dedup..." << std::endl;
  TfToken a("component");
  TfToken b("component");
  TfToken c("group");
  assert(a == b);          // same content -> same id
  assert(a.id() == b.id());
  assert(a != c);
  assert(a.str() == "component");
  assert(c.str() == "group");

  // str() returns stable table-owned storage: same address for equal tokens.
  assert(&a.str() == &b.str());
}

static void test_common_preregistered() {
  std::cout << "Testing pre-registered common tokens..." << std::endl;
  // Common tokens must already be present (interned by register_common_tokens).
  assert(GetTfTokenTable().contains("vertex"));
  assert(GetTfTokenTable().contains("faceVarying"));
  assert(GetTfTokenTable().contains("assembly"));
  assert(!GetTfTokenTable().contains("definitely_not_a_common_token_xyz"));

  // Their ids are low (registered first).
  TfToken vtx("vertex");
  assert(vtx.id() != 0 && vtx.id() < 64);
}

static void test_from_id_roundtrip() {
  std::cout << "Testing FromId roundtrip..." << std::endl;
  TfToken t("subdivisionScheme_value_xyz");
  TfToken u = TfToken::FromId(t.id());
  assert(u == t);
  assert(u.str() == t.str());
}

static void test_hash_and_containers() {
  std::cout << "Testing TfToken in hash containers..." << std::endl;
  std::unordered_set<TfToken> s;
  s.insert(TfToken("a"));
  s.insert(TfToken("b"));
  s.insert(TfToken("a"));  // dup
  assert(s.size() == 2);
  assert(s.count(TfToken("a")) == 1);
  assert(s.count(TfToken("c")) == 0);

  std::unordered_map<TfToken, int> m;
  m[TfToken("purpose")] = 7;
  assert(m[TfToken("purpose")] == 7);
}

static void test_ordering() {
  std::cout << "Testing ordering helpers..." << std::endl;
  TfToken z("zzz_token");
  TfToken a("aaa_token");
  // Lexicographic comparison is by string.
  assert(a.lexical_less(z));
  assert(!z.lexical_less(a));
  // operator< is a valid strict-weak-order (id based), enough for std::map keys.
  assert((a < z) != (z < a) || (a == z));
}

int main() {
  std::cout << "=== TfToken tests ===" << std::endl;
  test_empty();
  test_intern_dedup();
  test_common_preregistered();
  test_from_id_roundtrip();
  test_hash_and_containers();
  test_ordering();
  std::cout << "\nAll token tests passed!" << std::endl;
  return 0;
}
