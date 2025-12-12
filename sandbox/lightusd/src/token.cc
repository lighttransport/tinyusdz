// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Token implementation with string interning

#include "lightusd/token.hh"
#include <unordered_set>
#include <mutex>
#include <cstring>

namespace lightusd {
namespace v1 {

namespace {

// Global string pool for token interning
// Uses a set to ensure unique strings
class StringPool {
public:
    static StringPool& instance() {
        static StringPool pool;
        return pool;
    }

    const std::string* intern(const char* str) {
        if (!str || !*str) {
            return &kEmpty;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Try to insert - if already exists, returns iterator to existing
        auto result = pool_.insert(std::string(str));
        return &(*result.first);
    }

    const std::string* intern(const std::string& str) {
        if (str.empty()) {
            return &kEmpty;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        auto result = pool_.insert(str);
        return &(*result.first);
    }

    const std::string& empty_string() const {
        return kEmpty;
    }

private:
    StringPool() = default;
    ~StringPool() = default;

    // Disable copy/move
    StringPool(const StringPool&) = delete;
    StringPool& operator=(const StringPool&) = delete;

    std::unordered_set<std::string> pool_;
    std::mutex mutex_;
    const std::string kEmpty;
};

} // anonymous namespace

// Static empty string
const std::string Token::kEmptyString;

Token::Token()
    : interned_(&StringPool::instance().empty_string()) {
}

Token::Token(const char* str)
    : interned_(StringPool::instance().intern(str)) {
}

Token::Token(const std::string& str)
    : interned_(StringPool::instance().intern(str)) {
}

Token::Token(const Token& other)
    : interned_(other.interned_) {
}

Token::Token(Token&& other) noexcept
    : interned_(other.interned_) {
    other.interned_ = &StringPool::instance().empty_string();
}

Token& Token::operator=(const Token& other) {
    interned_ = other.interned_;
    return *this;
}

Token& Token::operator=(Token&& other) noexcept {
    interned_ = other.interned_;
    other.interned_ = &StringPool::instance().empty_string();
    return *this;
}

Token::~Token() {
    // No cleanup needed - interned strings live forever in pool
}

const char* Token::c_str() const {
    return interned_->c_str();
}

const std::string& Token::str() const {
    return *interned_;
}

bool Token::empty() const {
    return interned_->empty();
}

size_t Token::size() const {
    return interned_->size();
}

size_t Token::hash() const {
    // Use pointer as hash since interned strings are unique
    return std::hash<const void*>()(interned_);
}

bool Token::operator==(const Token& other) const {
    // Pointer comparison due to interning
    return interned_ == other.interned_;
}

bool Token::operator!=(const Token& other) const {
    return interned_ != other.interned_;
}

bool Token::operator<(const Token& other) const {
    // String comparison for ordering
    return *interned_ < *other.interned_;
}

bool Token::operator<=(const Token& other) const {
    return interned_ == other.interned_ || *interned_ < *other.interned_;
}

bool Token::operator>(const Token& other) const {
    return *interned_ > *other.interned_;
}

bool Token::operator>=(const Token& other) const {
    return interned_ == other.interned_ || *interned_ > *other.interned_;
}

bool Token::operator==(const char* str) const {
    if (!str) {
        return interned_->empty();
    }
    return *interned_ == str;
}

bool Token::operator==(const std::string& str) const {
    return *interned_ == str;
}

void Token::swap(Token& other) noexcept {
    std::swap(interned_, other.interned_);
}

} // namespace v1
} // namespace lightusd
