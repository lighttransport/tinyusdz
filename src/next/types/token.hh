// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - TfToken (interned string handle, an OpenUSD TfToken analog)
//
// A TfToken is an immutable string stored ONCE in a global table and referred to
// everywhere by a compact 32-bit id. Copy, equality and hashing are O(1) on the
// id, and all tokens with the same content share one string (dedup). Use it for
// repeated identifier-like strings — model `kind`, schema names, and (eventually)
// token-valued attribute data — the role TfToken plays in USD.
//
// The name is spelled `TfToken` (not `Token`) because the USDA lexer already owns
// `tinyusdz::next::Token` for its parser token stream; this is the value-type
// token, matching OpenUSD's naming.
//
// Threading (matches the existing PropNameTable / TypeNameTable design):
//   - intern() takes a global lock (read-mostly; the new-token case is rare).
//     Once the token set is fixed (post-load), freeze() makes intern-hits and
//     reads fully lock-free against an immutable snapshot.
//   - str()/get()/compare are lock-free (snapshot / immutable storage).
// Interning must not run concurrently with frozen lock-free reads; load and read
// phases are disjoint, exactly like the sibling name tables.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace tinyusdz {
namespace next {

class TfToken {
 public:
  TfToken() = default;  // empty token (id 0 == "")
  explicit TfToken(std::string_view s);
  explicit TfToken(const char* s);
  explicit TfToken(const std::string& s);

  bool empty() const { return id_ == 0; }
  uint32_t id() const { return id_; }

  /// The interned string. Stable for the process lifetime (table-owned).
  const std::string& str() const;
  std::string_view sv() const { return str(); }
  const char* c_str() const { return str().c_str(); }

  bool operator==(const TfToken& o) const { return id_ == o.id_; }
  bool operator!=(const TfToken& o) const { return id_ != o.id_; }
  /// Fast id ordering — stable within a run but NOT lexicographic. Good for
  /// hash/set grouping; when authored/lexical order matters, compare str().
  bool operator<(const TfToken& o) const { return id_ < o.id_; }

  bool lexical_less(const TfToken& o) const { return str() < o.str(); }

  /// Reconstruct a token from a raw id (e.g. one previously obtained via id()).
  static TfToken FromId(uint32_t id) {
    TfToken t;
    t.id_ = id;
    return t;
  }

 private:
  uint32_t id_ = 0;  // 0 == empty string ""
};

/// Global interning table backing TfToken. Same RCU-snapshot + freeze design as
/// PropNameTable/TypeNameTable so reads stay lock-free under render-time fan-out.
class TfTokenTable {
 public:
  TfTokenTable();
  ~TfTokenTable();

  /// Intern a string, returning its token (existing id if already present).
  TfToken intern(std::string_view s);
  TfToken intern(const std::string& s) { return intern(std::string_view(s)); }
  TfToken intern(const char* s) {
    return s ? intern(std::string_view(s)) : TfToken();
  }

  /// String for a raw id (empty string for id 0 or out-of-range).
  const std::string& get(uint32_t id) const;

  /// Whether `s` has been interned (non-creating).
  bool contains(std::string_view s) const;

  /// Number of interned tokens (including the reserved empty token).
  size_t size() const;

  /// See the class comment. No-ops without TINYUSDZ_ENABLE_THREAD.
  void freeze();
  void unfreeze();

  /// Pre-register the reserved empty token (id 0) and common USD tokens so the
  /// hottest values get stable low ids and zero-alloc interning.
  void register_common_tokens();

 private:
  struct Impl;
  Impl* impl_;
};

/// Global TfToken table (singleton).
TfTokenTable& GetTfTokenTable();

}  // namespace next
}  // namespace tinyusdz

namespace std {
template <>
struct hash<tinyusdz::next::TfToken> {
  size_t operator()(const tinyusdz::next::TfToken& t) const noexcept {
    return std::hash<uint32_t>{}(t.id());
  }
};
}  // namespace std
