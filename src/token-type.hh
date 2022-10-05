// SPDX-License-Identifier: MIT
// C binding for TinyUSD(Z) 
#pragma once

//
// a class for `token` type.
//
// `token` is a short-length string and composed of alphanum + limited symbols(e.g. '@', '{', ...).
// It should not contain newline.
//
// TINYUSDZ_USE_STRING_ID_FOR_TOKEN_TYPE
//
// Use foonathan/string_id to implement Token class.
//
// NOTE: database(token storage) is accessed with mutex,
// so an application should not frequently construct Token class
// among threads.
//
// ---
//
// Seems there is no beneficial usecase in TinyUSDZ to use hash for token,
// so use naiive implementation by default.
//

#include <iostream>
#include <string>

#include "nonstd/optional.hpp"

#if 0 // defined(TINYUSDZ_USE_STRING_ID_FOR_TOKEN_TYPE)

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

#else // TINYUSDZ_USE_STRING_ID_FOR_TOKEN_TYPE
#include <functional>
#endif // TINYUSDZ_USE_STRING_ID_FOR_TOKEN_TYPE

namespace tinyusdz {

#if defined(TINYUSDZ_USE_STRING_ID_FOR_TOKEN_TYPE)

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

  explicit Token(const std::string &str) {
    str_ = sid::string_id(str.c_str(), TokenStorage::GetInstance());
  }

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

  bool valid() const {
    // TODO
    return true;
  }

 private:
  nonstd::optional<sid::string_id> str_;
};

inline bool operator==(const Token &tok, const std::string &rhs) {
  return tok.str().compare(rhs) == 0;
}

struct TokenHasher {
  inline size_t operator()(const Token &tok) const {
    return size_t(tok.hash());
  } 
};

struct TokenKeyEqual {
  bool operator()(const Token &lhs, const Token &rhs) const {
    return lhs.str() == rhs.str();
  } 
};

#else // TINYUSDZ_USE_STRING_ID_FOR_TOKEN_TYPE

class Token {

 public:
  Token() {}

  explicit Token(const std::string &str) {
    str_ = str;
  }

  explicit Token(const char *str) {
    str_ = str;
  }

  const std::string &str() const {
    return str_;
  }

  bool valid() const {
    // TODO
    return true;
  } 

  // No string hash for TinyUSDZ
#if 0
  uint64_t hash() const {
    if (!str_) {
      return 0;
    }

    // Assume non-zero hash value for non-empty string.
    return str_.value().hash_code();
  }
#endif


 private:
  std::string str_;
};

struct TokenHasher {
  inline size_t operator()(const Token &tok) const {
    return std::hash<std::string>()(tok.str());
  } 
};

struct TokenKeyEqual {
  bool operator()(const Token &lhs, const Token &rhs) const {
    return lhs.str() == rhs.str();
  } 
};


#endif // TINYUSDZ_USE_STRING_ID_FOR_TOKEN_TYPE

} // namespace tinyusdz
