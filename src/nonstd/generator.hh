// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 TinyUSDZ contributors
//
// C++20 implementation of C++23's std::generator
// Based on the C++23 standard specification
//
// This provides a coroutine generator type for C++20 that mimics
// the behavior of C++23's std::generator

#pragma once

// Check for coroutine support
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
    #define NONSTD_GENERATOR_AVAILABLE 1
    #include <coroutine>
#elif defined(__cpp_coroutines) && __cpp_coroutines >= 201703L
    #define NONSTD_GENERATOR_AVAILABLE 1
    // For older compilers that use experimental coroutines
    #include <experimental/coroutine>
    namespace std {
        using namespace experimental;
    }
#else
    #define NONSTD_GENERATOR_AVAILABLE 0
#endif

#if NONSTD_GENERATOR_AVAILABLE

#include <exception>
#include <iterator>
#include <type_traits>
#include <utility>
#include <memory>

namespace nonstd {

template<typename T, typename Allocator = void>
class generator;

namespace detail {

template<typename T>
class generator_promise_base {
public:
    using value_type = std::remove_reference_t<T>;
    using reference = std::conditional_t<std::is_reference_v<T>, T, T&>;
    using pointer = std::add_pointer_t<value_type>;

    generator_promise_base() = default;
    generator_promise_base(const generator_promise_base&) = delete;
    generator_promise_base& operator=(const generator_promise_base&) = delete;
    generator_promise_base(generator_promise_base&&) = default;
    generator_promise_base& operator=(generator_promise_base&&) = default;

    std::suspend_always initial_suspend() noexcept { return {}; }
    
    std::suspend_always final_suspend() noexcept { return {}; }
    
    template<typename U = T>
    std::suspend_always yield_value(U&& value) noexcept(
        std::is_nothrow_constructible_v<value_type, U&&>) 
        requires std::constructible_from<value_type, U&&> {
        if constexpr (std::is_reference_v<T>) {
            value_ = std::addressof(value);
        } else {
            if (!storage_) {
                storage_ = std::make_unique<value_type>(std::forward<U>(value));
            } else {
                *storage_ = std::forward<U>(value);
            }
            value_ = storage_.get();
        }
        return {};
    }

    // Support yielding from another generator
    template<typename U, typename Alloc>
    auto yield_value(generator<U, Alloc>&& gen) noexcept
        requires std::convertible_to<U, T> {
        struct awaiter {
            generator<U, Alloc> gen_;
            typename generator<U, Alloc>::iterator iter_;
            
            awaiter(generator<U, Alloc>&& g) : gen_(std::move(g)) {}
            
            bool await_ready() noexcept { return false; }
            
            void await_suspend(std::coroutine_handle<>) noexcept {
                iter_ = gen_.begin();
            }
            
            void await_resume() noexcept {}
        };
        return awaiter{std::move(gen)};
    }

    void unhandled_exception() {
        exception_ = std::current_exception();
    }

    void return_void() noexcept {}

    reference value() const noexcept {
        return static_cast<reference>(*value_);
    }

    void rethrow_if_exception() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
    }

protected:
    std::add_pointer_t<value_type> value_ = nullptr;
    std::exception_ptr exception_;
    std::unique_ptr<value_type> storage_;
};

template<typename T, typename Allocator>
class generator_promise : public generator_promise_base<T> {
public:
    using generator_promise_base<T>::generator_promise_base;
    
    generator<T, Allocator> get_return_object() noexcept;
};

template<typename T>
class generator_promise<T, void> : public generator_promise_base<T> {
public:
    using generator_promise_base<T>::generator_promise_base;
    
    generator<T, void> get_return_object() noexcept;
    
    void* operator new(std::size_t size) {
        return ::operator new(size);
    }
    
    void operator delete(void* ptr) noexcept {
        ::operator delete(ptr);
    }
};

template<typename Promise>
class generator_iterator {
public:
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = typename Promise::value_type;
    using reference = typename Promise::reference;
    using pointer = typename Promise::pointer;

    generator_iterator() noexcept = default;
    
    explicit generator_iterator(std::coroutine_handle<Promise> handle) noexcept
        : handle_(handle) {}

    friend bool operator==(const generator_iterator& it, std::default_sentinel_t) noexcept {
        return !it.handle_ || it.handle_.done();
    }

    friend bool operator!=(const generator_iterator& it, std::default_sentinel_t s) noexcept {
        return !(it == s);
    }

    friend bool operator==(std::default_sentinel_t s, const generator_iterator& it) noexcept {
        return it == s;
    }

    friend bool operator!=(std::default_sentinel_t s, const generator_iterator& it) noexcept {
        return it != s;
    }

    generator_iterator& operator++() {
        handle_.resume();
        if (handle_.done()) {
            handle_.promise().rethrow_if_exception();
        }
        return *this;
    }

    void operator++(int) {
        ++*this;
    }

    reference operator*() const noexcept {
        return handle_.promise().value();
    }

    pointer operator->() const noexcept 
        requires (!std::is_reference_v<reference>) {
        return std::addressof(operator*());
    }

private:
    std::coroutine_handle<Promise> handle_;
};

} // namespace detail

template<typename T, typename Allocator>
class [[nodiscard]] generator {
public:
    using promise_type = detail::generator_promise<T, Allocator>;
    using iterator = detail::generator_iterator<promise_type>;
    using value_type = typename promise_type::value_type;
    using reference = typename promise_type::reference;
    using pointer = typename promise_type::pointer;

    generator() noexcept = default;

    generator(const generator&) = delete;
    generator& operator=(const generator&) = delete;

    generator(generator&& other) noexcept
        : handle_(std::exchange(other.handle_, {})) {}

    generator& operator=(generator&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    ~generator() {
        if (handle_) {
            handle_.destroy();
        }
    }

    iterator begin() {
        if (handle_) {
            handle_.resume();
            if (handle_.done()) {
                handle_.promise().rethrow_if_exception();
            }
        }
        return iterator{handle_};
    }

    std::default_sentinel_t end() const noexcept {
        return std::default_sentinel;
    }

private:
    friend promise_type;

    explicit generator(std::coroutine_handle<promise_type> handle) noexcept
        : handle_(handle) {}

    std::coroutine_handle<promise_type> handle_;
};

namespace detail {

template<typename T, typename Allocator>
generator<T, Allocator> generator_promise<T, Allocator>::get_return_object() noexcept {
    return generator<T, Allocator>{
        std::coroutine_handle<generator_promise>::from_promise(*this)
    };
}

template<typename T>
generator<T, void> generator_promise<T, void>::get_return_object() noexcept {
    return generator<T, void>{
        std::coroutine_handle<generator_promise>::from_promise(*this)
    };
}

} // namespace detail

// Helper type trait to check if generator is available
template<typename T>
struct is_generator : std::false_type {};

template<typename T, typename Allocator>
struct is_generator<generator<T, Allocator>> : std::true_type {};

template<typename T>
inline constexpr bool is_generator_v = is_generator<T>::value;

} // namespace nonstd

#else // NONSTD_GENERATOR_AVAILABLE

namespace nonstd {

// Placeholder when coroutines are not available
// This allows the header to be included without compilation errors

template<typename T, typename Allocator = void>
class generator {
public:
    // Non-functional placeholder - will cause compilation error if used
    static_assert(sizeof(T) == 0, "Coroutines are not available in this compiler/standard version");
};

template<typename T>
struct is_generator : std::false_type {};

template<typename T>
inline constexpr bool is_generator_v = is_generator<T>::value;

} // namespace nonstd

#endif // NONSTD_GENERATOR_AVAILABLE