// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Token class (interned string)

#pragma once

#include <string>
#include <cstddef>
#include <functional>

namespace lightusd {
namespace v1 {

/// Token - an interned string for efficient comparison and storage.
/// Tokens are commonly used for USD attribute/prim names and other identifiers.
/// String interning ensures each unique string is stored only once in memory.
class Token {
public:
    /// Default constructor - creates empty token
    Token();

    /// Construct from C string
    explicit Token(const char* str);

    /// Construct from std::string
    explicit Token(const std::string& str);

    /// Copy/move constructors
    Token(const Token& other);
    Token(Token&& other) noexcept;

    /// Copy/move assignment
    Token& operator=(const Token& other);
    Token& operator=(Token&& other) noexcept;

    /// Destructor
    ~Token();

    /// Get C string representation
    const char* c_str() const;

    /// Get std::string reference
    const std::string& str() const;

    /// Check if empty
    bool empty() const;

    /// Get string length
    size_t size() const;

    /// Get hash value (for use in hash containers)
    size_t hash() const;

    /// Comparison operators
    bool operator==(const Token& other) const;
    bool operator!=(const Token& other) const;
    bool operator<(const Token& other) const;
    bool operator<=(const Token& other) const;
    bool operator>(const Token& other) const;
    bool operator>=(const Token& other) const;

    /// Compare with string
    bool operator==(const char* str) const;
    bool operator==(const std::string& str) const;

    /// Swap
    void swap(Token& other) noexcept;

private:
    // Pointer to interned string in global string pool
    const std::string* interned_;

    // Static empty string for empty tokens
    static const std::string kEmptyString;
};

/// Swap specialization
inline void swap(Token& a, Token& b) noexcept {
    a.swap(b);
}

} // namespace v1
} // namespace lightusd

/// Hash specialization for std::unordered_map/set
namespace std {
template<>
struct hash<lightusd::v1::Token> {
    size_t operator()(const lightusd::v1::Token& t) const {
        return t.hash();
    }
};
}
