// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// Lydra - Minimal OpenGL/Vulkan-friendly data conversion utilities
// Core types: Span, Result, Error, Format, VertexBufferLayout

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace lydra {

// ============================================================================
// Span — Non-owning view (like std::span but C++17 compatible)
// ============================================================================

template <typename T>
struct Span {
    const T* ptr = nullptr;
    size_t count = 0;

    Span() = default;
    Span(const T* data, size_t size) : ptr(data), count(size) {}

    template <typename Alloc>
    Span(const std::vector<std::remove_const_t<T>, Alloc>& v)
        : ptr(v.data()), count(v.size()) {}

    const T* data() const { return ptr; }
    size_t size() const { return count; }
    bool empty() const { return count == 0; }
    size_t size_bytes() const { return count * sizeof(T); }

    const T* begin() const { return ptr; }
    const T* end() const { return ptr + count; }

    const T& operator[](size_t i) const { return ptr[i]; }
};

// ============================================================================
// Error / Result
// ============================================================================

struct Error {
    std::string message;
};

template <typename T>
struct Result {
    std::optional<T> value;
    std::optional<Error> error;

    bool ok() const { return value.has_value(); }

    T& operator*() { return *value; }
    const T& operator*() const { return *value; }

    T* operator->() { return &*value; }
    const T* operator->() const { return &*value; }

    static Result ok_value(T&& v) {
        Result r;
        r.value = std::move(v);
        return r;
    }

    static Result err(const std::string& msg) {
        Result r;
        r.error = Error{msg};
        return r;
    }
};

// ============================================================================
// Format — Vulkan-style vertex attribute format enum
// ============================================================================

enum class Format : uint32_t {
    R32_SFLOAT,
    R32G32_SFLOAT,
    R32G32B32_SFLOAT,
    R32G32B32A32_SFLOAT,
    R16_SFLOAT,
    R16G16_SFLOAT,
    R16G16B16_SFLOAT,
    R16G16B16A16_SFLOAT,
    R8G8B8A8_UNORM,
    R16_UINT,
    R32_UINT,
    R32_SINT,
};

inline uint32_t format_size(Format f) {
    switch (f) {
    case Format::R32_SFLOAT:           return 4;
    case Format::R32G32_SFLOAT:        return 8;
    case Format::R32G32B32_SFLOAT:     return 12;
    case Format::R32G32B32A32_SFLOAT:  return 16;
    case Format::R16_SFLOAT:           return 2;
    case Format::R16G16_SFLOAT:        return 4;
    case Format::R16G16B16_SFLOAT:     return 6;
    case Format::R16G16B16A16_SFLOAT:  return 8;
    case Format::R8G8B8A8_UNORM:       return 4;
    case Format::R16_UINT:             return 2;
    case Format::R32_UINT:             return 4;
    case Format::R32_SINT:             return 4;
    }
    return 0;
}

inline uint32_t format_components(Format f) {
    switch (f) {
    case Format::R32_SFLOAT:           return 1;
    case Format::R32G32_SFLOAT:        return 2;
    case Format::R32G32B32_SFLOAT:     return 3;
    case Format::R32G32B32A32_SFLOAT:  return 4;
    case Format::R16_SFLOAT:           return 1;
    case Format::R16G16_SFLOAT:        return 2;
    case Format::R16G16B16_SFLOAT:     return 3;
    case Format::R16G16B16A16_SFLOAT:  return 4;
    case Format::R8G8B8A8_UNORM:       return 4;
    case Format::R16_UINT:             return 1;
    case Format::R32_UINT:             return 1;
    case Format::R32_SINT:             return 1;
    }
    return 0;
}

// ============================================================================
// Vertex Buffer Layout — describes interleaved vertex data for GPU upload
// ============================================================================

struct VertexAttribute {
    uint32_t location;  // shader binding location
    Format format;      // data format
    uint32_t offset;    // byte offset within vertex (for interleaved)
};

struct VertexBufferLayout {
    uint32_t stride;                         // bytes per vertex
    std::vector<VertexAttribute> attributes; // attribute descriptors
};

}  // namespace lydra
