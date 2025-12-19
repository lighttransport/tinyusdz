// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - C++20 Coroutine Async Fetch API
//
// This module provides a coroutine-based interface for async fetching.
// Requires C++20 and either Asyncify or JSPI for WASM suspension.
//
// Usage:
//   Task<FetchResult> load_texture(const std::string& url) {
//       auto result = co_await coro_fetch(url);
//       if (result.ok) {
//           // Use result.data
//       }
//       co_return result;
//   }
//
// The coroutine approach provides cleaner async code compared to callbacks,
// while the underlying suspension mechanism (Asyncify/JSPI) handles the
// actual WASM stack management.

#pragma once

#include "async_fetch.hh"

#if defined(LIGHTUSD_COROUTINE) && __cplusplus >= 202002L

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace lightusd {
namespace v1 {

// ============================================================================
// Task - Coroutine return type for async operations
// ============================================================================

/// A simple task type for coroutines that return a value
template <typename T>
class Task {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::optional<T> value_;
        std::exception_ptr exception_;

        Task get_return_object() {
            return Task{handle_type::from_promise(*this)};
        }

        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        void return_value(T val) {
            value_ = std::move(val);
        }

        void unhandled_exception() {
            exception_ = std::current_exception();
        }
    };

    Task(handle_type h) : handle_(h) {}

    Task(Task&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    ~Task() {
        if (handle_) handle_.destroy();
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    /// Check if the task has completed
    bool done() const { return handle_.done(); }

    /// Get the result (blocks if not ready in non-coroutine context)
    T get() {
        if (handle_.promise().exception_) {
            std::rethrow_exception(handle_.promise().exception_);
        }
        return std::move(*handle_.promise().value_);
    }

    /// Awaiter support - allows co_await on Task<T>
    bool await_ready() const noexcept { return handle_.done(); }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        return handle_;
    }

    T await_resume() {
        return get();
    }

private:
    handle_type handle_;
};

/// Specialization for void return type
template <>
class Task<void> {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::exception_ptr exception_;

        Task get_return_object() {
            return Task{handle_type::from_promise(*this)};
        }

        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        void return_void() {}

        void unhandled_exception() {
            exception_ = std::current_exception();
        }
    };

    Task(handle_type h) : handle_(h) {}

    Task(Task&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    ~Task() {
        if (handle_) handle_.destroy();
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    bool done() const { return handle_.done(); }

    void get() {
        if (handle_.promise().exception_) {
            std::rethrow_exception(handle_.promise().exception_);
        }
    }

    bool await_ready() const noexcept { return handle_.done(); }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        return handle_;
    }

    void await_resume() {
        get();
    }

private:
    handle_type handle_;
};

// ============================================================================
// FetchAwaiter - Awaitable wrapper for async_fetch
// ============================================================================

/// Awaiter for fetch operations
/// When co_await'd, this suspends the coroutine and performs the fetch
class FetchAwaiter {
public:
    FetchAwaiter(std::string url, FetchConfig config = {})
        : url_(std::move(url)), config_(std::move(config)) {}

    /// Never ready immediately - always suspend to perform fetch
    bool await_ready() const noexcept { return false; }

    /// Suspend and perform the fetch
    /// With Asyncify/JSPI, the WASM execution is suspended here
    void await_suspend(std::coroutine_handle<> handle) {
        // Perform the synchronous-style fetch (Asyncify/JSPI handles suspension)
        result_ = async_fetch(url_, config_);
        // Resume the coroutine
        handle.resume();
    }

    /// Return the fetch result
    FetchResult await_resume() {
        return std::move(result_);
    }

private:
    std::string url_;
    FetchConfig config_;
    FetchResult result_;
};

// ============================================================================
// Coroutine Fetch Functions
// ============================================================================

/// Create an awaitable fetch operation
/// @param url URL to fetch
/// @param config Optional fetch configuration
/// @return Awaitable that yields FetchResult
///
/// Usage:
///   FetchResult result = co_await coro_fetch("texture.png");
inline FetchAwaiter coro_fetch(const std::string& url, const FetchConfig& config = {}) {
    return FetchAwaiter(url, config);
}

/// Fetch multiple URLs as a coroutine
/// @param urls URLs to fetch
/// @param config Shared fetch configuration
/// @return Task that yields vector of results
///
/// Usage:
///   auto results = co_await coro_fetch_all({"a.png", "b.png"});
inline Task<std::vector<FetchResult>> coro_fetch_all(
    const std::vector<std::string>& urls,
    const FetchConfig& config = {}) {

    std::vector<FetchResult> results;
    results.reserve(urls.size());

    for (const auto& url : urls) {
        results.push_back(co_await coro_fetch(url, config));
    }

    co_return results;
}

// ============================================================================
// Generator - For streaming/progressive loading
// ============================================================================

/// Generator for yielding values one at a time
/// Useful for progressive loading where you want to yield prims as they load
template <typename T>
class Generator {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        T current_value_;
        std::exception_ptr exception_;

        Generator get_return_object() {
            return Generator{handle_type::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        std::suspend_always yield_value(T value) {
            current_value_ = std::move(value);
            return {};
        }

        void return_void() {}

        void unhandled_exception() {
            exception_ = std::current_exception();
        }
    };

    Generator(handle_type h) : handle_(h) {}

    Generator(Generator&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    Generator& operator=(Generator&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    ~Generator() {
        if (handle_) handle_.destroy();
    }

    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    /// Iterator for range-based for loops
    class iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        iterator() : handle_(nullptr) {}
        explicit iterator(handle_type h) : handle_(h) {}

        iterator& operator++() {
            handle_.resume();
            if (handle_.done()) {
                handle_ = nullptr;
            }
            return *this;
        }

        T& operator*() {
            return handle_.promise().current_value_;
        }

        bool operator==(const iterator& other) const {
            return handle_ == other.handle_;
        }

        bool operator!=(const iterator& other) const {
            return !(*this == other);
        }

    private:
        handle_type handle_;
    };

    iterator begin() {
        if (handle_) {
            handle_.resume();
            if (handle_.done()) {
                return end();
            }
        }
        return iterator{handle_};
    }

    iterator end() {
        return iterator{nullptr};
    }

    /// Manual iteration
    bool next() {
        if (!handle_ || handle_.done()) return false;
        handle_.resume();
        return !handle_.done();
    }

    /// Get current value
    T& current() {
        return handle_.promise().current_value_;
    }

    bool done() const {
        return !handle_ || handle_.done();
    }

private:
    handle_type handle_;
};

}  // namespace v1
}  // namespace lightusd

#endif // LIGHTUSD_COROUTINE && C++20
