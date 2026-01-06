// SPDX-License-Identifier: MIT 
#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace tinyusdz {

template <typename T>
struct TypeTraits {
    static constexpr uint8_t type_id() { return 0; }
};

class static_any {
private:
    static constexpr size_t kSmallBufferSize = 24;
    static constexpr size_t kAlignment = 8;
    
    struct vtable {
        void (*destroy)(void*);
        void (*copy)(void*, const void*);
        void (*move)(void*, void*);
        size_t size;
        size_t alignment;
    };
    
    template <typename T>
    static void destroy_small(void* ptr) {
        reinterpret_cast<T*>(ptr)->~T();
    }
    
    template <typename T>
    static void destroy_large(void* ptr) {
        delete reinterpret_cast<T*>(*reinterpret_cast<void**>(ptr));
        *reinterpret_cast<void**>(ptr) = nullptr;
    }
    
    template <typename T>
    static void copy_small(void* dst, const void* src) {
        new (dst) T(*reinterpret_cast<const T*>(src));
    }
    
    template <typename T>
    static void copy_large(void* dst, const void* src) {
        *reinterpret_cast<T**>(dst) = new T(**reinterpret_cast<T* const*>(src));
    }
    
    template <typename T>
    static void move_small(void* dst, void* src) {
        new (dst) T(std::move(*reinterpret_cast<T*>(src)));
        reinterpret_cast<T*>(src)->~T();
    }
    
    template <typename T>
    static void move_large(void* dst, void* src) {
        *reinterpret_cast<T**>(dst) = *reinterpret_cast<T**>(src);
        *reinterpret_cast<T**>(src) = nullptr;
    }
    
    template <typename T>
    static vtable* get_vtable() {
        constexpr bool is_small = sizeof(T) <= kSmallBufferSize && 
                                  alignof(T) <= kAlignment;
        static vtable vt = {
            is_small ? &destroy_small<T> : &destroy_large<T>,
            is_small ? &copy_small<T> : &copy_large<T>,
            is_small ? &move_small<T> : &move_large<T>,
            sizeof(T),
            alignof(T)
        };
        return &vt;
    }
    
    alignas(kAlignment) uint8_t storage_[kSmallBufferSize];
    vtable* vtable_;
    uint8_t type_id_;
    
    template <typename T>
    T* get_ptr() noexcept {
        if (sizeof(T) <= kSmallBufferSize && alignof(T) <= kAlignment) {
            return reinterpret_cast<T*>(&storage_[0]);
        } else {
            return *reinterpret_cast<T**>(&storage_[0]);
        }
    }
    
    template <typename T>
    const T* get_ptr() const noexcept {
        if (sizeof(T) <= kSmallBufferSize && alignof(T) <= kAlignment) {
            return reinterpret_cast<const T*>(&storage_[0]);
        } else {
            return *reinterpret_cast<T* const*>(&storage_[0]);
        }
    }
    
public:
    static_any() noexcept : vtable_(nullptr), type_id_(0) {
        std::memset(storage_, 0, sizeof(storage_));
    }
    
    template <typename T, typename = typename std::enable_if<!std::is_same<typename std::decay<T>::type, static_any>::value>::type>
    static_any(T&& value) : type_id_(TypeTraits<typename std::decay<T>::type>::type_id()) {
        using DecayT = typename std::decay<T>::type;
        static_assert(TypeTraits<DecayT>::type_id() > 0 && TypeTraits<DecayT>::type_id() <= 255,
                      "Type must have a valid type_id between 1 and 255");
        
        vtable_ = get_vtable<DecayT>();
        
        if (sizeof(DecayT) <= kSmallBufferSize && alignof(DecayT) <= kAlignment) {
            new (&storage_[0]) DecayT(std::forward<T>(value));
        } else {
            *reinterpret_cast<DecayT**>(&storage_[0]) = new DecayT(std::forward<T>(value));
        }
    }
    
    static_any(const static_any& other) : vtable_(other.vtable_), type_id_(other.type_id_) {
        if (vtable_) {
            vtable_->copy(&storage_[0], &other.storage_[0]);
        } else {
            std::memset(storage_, 0, sizeof(storage_));
        }
    }
    
    static_any(static_any&& other) noexcept : vtable_(other.vtable_), type_id_(other.type_id_) {
        if (vtable_) {
            vtable_->move(&storage_[0], &other.storage_[0]);
            other.vtable_ = nullptr;
            other.type_id_ = 0;
        } else {
            std::memset(storage_, 0, sizeof(storage_));
        }
    }
    
    ~static_any() {
        reset();
    }
    
    static_any& operator=(const static_any& other) {
        if (this != &other) {
            reset();
            vtable_ = other.vtable_;
            type_id_ = other.type_id_;
            if (vtable_) {
                vtable_->copy(&storage_[0], &other.storage_[0]);
            }
        }
        return *this;
    }
    
    static_any& operator=(static_any&& other) noexcept {
        if (this != &other) {
            reset();
            vtable_ = other.vtable_;
            type_id_ = other.type_id_;
            if (vtable_) {
                vtable_->move(&storage_[0], &other.storage_[0]);
                other.vtable_ = nullptr;
                other.type_id_ = 0;
            }
        }
        return *this;
    }
    
    template <typename T, typename = typename std::enable_if<!std::is_same<typename std::decay<T>::type, static_any>::value>::type>
    static_any& operator=(T&& value) {
        reset();
        *this = static_any(std::forward<T>(value));
        return *this;
    }
    
    template <typename T>
    bool emplace(T&& value) {
        reset();
        *this = static_any(std::forward<T>(value));
        return true;
    }
    
    void reset() noexcept {
        if (vtable_) {
            vtable_->destroy(&storage_[0]);
            vtable_ = nullptr;
            type_id_ = 0;
            std::memset(storage_, 0, sizeof(storage_));
        }
    }
    
    bool has_value() const noexcept {
        return vtable_ != nullptr;
    }
    
    uint8_t type_id() const noexcept {
        return type_id_;
    }
    
    template <typename T>
    bool is() const noexcept {
        return type_id_ == TypeTraits<T>::type_id();
    }
    
    template <typename T>
    T* cast() noexcept {
        if (!is<T>()) {
            return nullptr;
        }
        return get_ptr<T>();
    }
    
    template <typename T>
    const T* cast() const noexcept {
        if (!is<T>()) {
            return nullptr;
        }
        return get_ptr<T>();
    }
    
    template <typename T>
    T& as() noexcept {
        return *get_ptr<T>();
    }
    
    template <typename T>
    const T& as() const noexcept {
        return *get_ptr<T>();
    }
    
    void swap(static_any& other) noexcept {
        static_any temp(std::move(other));
        other = std::move(*this);
        *this = std::move(temp);
    }
};

inline void swap(static_any& a, static_any& b) noexcept {
    a.swap(b);
}

}
