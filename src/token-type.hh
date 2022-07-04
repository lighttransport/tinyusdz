// SPDX-License-Identifier: MIT
// C binding for TinyUSD(Z) 
#pragma once

//
// a class for `token` type.
//
// Use foonathan/string_id to implement Token class.
//
// NOTE: database(token storage) is accessed with mutex,
// so an application should not frequently construct Token class
// among threads.
//

#include <iostream>
#include <string>

#include "nonstd/optional.hpp"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// external
#include "external/string_id/database.hpp"
#include "external/string_id/string_id.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {


namespace sid = foonathan::string_id;

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#endif

// Singleton
class TokenStorage
{
  public:
    TokenStorage(const TokenStorage &) = delete;
    TokenStorage& operator=(const TokenStorage&) = delete;
    TokenStorage(TokenStorage&&) = delete;
    TokenStorage& operator=(TokenStorage&&) = delete;

    static sid::default_database &GetInstance() {
      static sid::default_database s_database;
      return s_database;
    }

  private:
    TokenStorage() = default;
    ~TokenStorage() = default; 
};

#ifdef __clang__
#pragma clang diagnostic pop
#endif

class Token {

 public:
  Token() {}

  explicit Token(const char *str) {
    str_ = sid::string_id(str, TokenStorage::GetInstance());
  }

  const std::string str() const {
    if (!str_) {
      return std::string();
    }
    return str_.value().string();
  }

  uint64_t hash() const {
    if (!str_) {
      return 0;
    }

    // Assume non-zero hash value for non-empty string.
    return str_.value().hash_code();
  }

 private:
  nonstd::optional<sid::string_id> str_;
};

struct TokenHasher {
  inline size_t operator()(const Token &tok) const {
    return tok.hash();
  } 
};

struct TokenKeyEqual {
  bool operator()(const Token &lhs, const Token &rhs) const {
    return lhs.str() == rhs.str();
  } 
};

//std::ostream &operator<<(std::ostream &os, const Token &tok) {
//
//  os << tok.str();
//
//  return os;
//}


} // namespace tinyusdz
