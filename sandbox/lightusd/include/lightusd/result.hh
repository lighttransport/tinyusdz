// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Result type for error handling without exceptions

#pragma once

#include <string>
#include <utility>
#include <new>

namespace lightusd {
namespace v1 {

/// Error information
struct Error {
    std::string message;

    Error() = default;
    explicit Error(const char* msg) : message(msg ? msg : "") {}
    explicit Error(const std::string& msg) : message(msg) {}
    explicit Error(std::string&& msg) : message(std::move(msg)) {}
};

/// Create an error with a message
inline Error make_error(const char* msg) { return Error(msg); }
inline Error make_error(const std::string& msg) { return Error(msg); }
inline Error make_error(std::string&& msg) { return Error(std::move(msg)); }

/// Result type - holds either a value of type T or an Error.
/// This is the ONLY template in LightUSD, used universally for error handling.
template<typename T>
class Result {
public:
    /// Construct with a value (success)
    Result(const T& val) : has_value_(true) {
        new (&storage_.value) T(val);
    }

    Result(T&& val) : has_value_(true) {
        new (&storage_.value) T(std::move(val));
    }

    /// Construct with an error (failure)
    Result(const Error& err) : has_value_(false) {
        new (&storage_.error) Error(err);
    }

    Result(Error&& err) : has_value_(false) {
        new (&storage_.error) Error(std::move(err));
    }

    /// Copy constructor
    Result(const Result& other) : has_value_(other.has_value_) {
        if (has_value_) {
            new (&storage_.value) T(other.storage_.value);
        } else {
            new (&storage_.error) Error(other.storage_.error);
        }
    }

    /// Move constructor
    Result(Result&& other) noexcept : has_value_(other.has_value_) {
        if (has_value_) {
            new (&storage_.value) T(std::move(other.storage_.value));
        } else {
            new (&storage_.error) Error(std::move(other.storage_.error));
        }
    }

    /// Copy assignment
    Result& operator=(const Result& other) {
        if (this != &other) {
            destroy();
            has_value_ = other.has_value_;
            if (has_value_) {
                new (&storage_.value) T(other.storage_.value);
            } else {
                new (&storage_.error) Error(other.storage_.error);
            }
        }
        return *this;
    }

    /// Move assignment
    Result& operator=(Result&& other) noexcept {
        if (this != &other) {
            destroy();
            has_value_ = other.has_value_;
            if (has_value_) {
                new (&storage_.value) T(std::move(other.storage_.value));
            } else {
                new (&storage_.error) Error(std::move(other.storage_.error));
            }
        }
        return *this;
    }

    /// Destructor
    ~Result() {
        destroy();
    }

    /// Check if result contains a value (success)
    bool ok() const { return has_value_; }

    /// Explicit bool conversion
    explicit operator bool() const { return has_value_; }

    /// Access the value (undefined behavior if !ok())
    const T& value() const & { return storage_.value; }
    T& value() & { return storage_.value; }
    T&& value() && { return std::move(storage_.value); }

    /// Access the error (undefined behavior if ok())
    const Error& error() const { return storage_.error; }

    /// Get value or default
    T value_or(const T& default_val) const {
        return has_value_ ? storage_.value : default_val;
    }

    T value_or(T&& default_val) const {
        return has_value_ ? storage_.value : std::move(default_val);
    }

    /// Safe pointer access - returns nullptr if error
    const T* value_ptr() const {
        return has_value_ ? &storage_.value : nullptr;
    }

    T* value_ptr() {
        return has_value_ ? &storage_.value : nullptr;
    }

private:
    void destroy() {
        if (has_value_) {
            storage_.value.~T();
        } else {
            storage_.error.~Error();
        }
    }

    union Storage {
        T value;
        Error error;

        Storage() {}
        ~Storage() {}
    } storage_;

    bool has_value_;
};

/// Specialization for void - represents success or error with no value
template<>
class Result<void> {
public:
    /// Construct success
    Result() : has_value_(true) {}

    /// Construct with error
    Result(const Error& err) : has_value_(false), error_(err) {}
    Result(Error&& err) : has_value_(false), error_(std::move(err)) {}

    /// Copy/move constructors
    Result(const Result&) = default;
    Result(Result&&) noexcept = default;
    Result& operator=(const Result&) = default;
    Result& operator=(Result&&) noexcept = default;

    /// Check if success
    bool ok() const { return has_value_; }
    explicit operator bool() const { return has_value_; }

    /// Access error (undefined behavior if ok())
    const Error& error() const { return error_; }

private:
    bool has_value_;
    Error error_;
};

/// Helper to create a success Result<void>
inline Result<void> ok() { return Result<void>(); }

} // namespace v1
} // namespace lightusd
