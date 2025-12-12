// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Path class for scene graph navigation

#pragma once

#include <string>
#include <cstddef>
#include <functional>

#include "lightusd/result.hh"

namespace lightusd {
namespace v1 {

/// Path - represents a location in the USD scene hierarchy.
/// Paths can point to prims (e.g., "/World/Mesh") or properties (e.g., "/World/Mesh.points").
class Path {
public:
    /// Default constructor - creates empty/invalid path
    Path();

    /// Construct from path string
    /// Examples: "/", "/root", "/root/child", "/root.property"
    explicit Path(const char* path_str);
    explicit Path(const std::string& path_str);

    /// Construct from prim path and property name
    Path(const std::string& prim_path, const std::string& prop_name);

    /// Copy/move
    Path(const Path& other);
    Path(Path&& other) noexcept;
    Path& operator=(const Path& other);
    Path& operator=(Path&& other) noexcept;

    /// Destructor
    ~Path();

    /// Parse path string with error reporting
    static Result<Path> parse(const std::string& path_str);

    /// Create special paths
    static Path root();         // "/"
    static Path empty_path();   // ""

    /// Validity checks
    bool is_valid() const;
    bool is_empty() const;

    /// Path type checks
    bool is_absolute() const;       // Starts with "/"
    bool is_relative() const;       // Does not start with "/"
    bool is_root() const;           // Is exactly "/"
    bool is_prim_path() const;      // No property part
    bool is_property_path() const;  // Has property part

    /// Get path components
    const std::string& prim_part() const;       // e.g., "/root/mesh"
    const std::string& prop_part() const;       // e.g., "points" (without ".")
    std::string full_path() const;              // e.g., "/root/mesh.points"

    /// Get element name (last component of prim path)
    /// e.g., "mesh" from "/root/mesh"
    std::string element_name() const;

    /// Get parent path
    /// e.g., "/root" from "/root/mesh"
    Path parent() const;

    /// Get parent prim path (strips property if present, then gets parent)
    Path parent_prim_path() const;

    /// Append child element
    /// e.g., Path("/root").append_child("mesh") -> "/root/mesh"
    Path append_child(const std::string& name) const;

    /// Append property
    /// e.g., Path("/root/mesh").append_property("points") -> "/root/mesh.points"
    Path append_property(const std::string& name) const;

    /// Make path relative (remove leading "/")
    Path make_relative() const;

    /// Make path absolute (add leading "/" if not present)
    Path make_absolute() const;

    /// Check if this path has prefix
    bool has_prefix(const Path& prefix) const;

    /// Replace prefix
    Path replace_prefix(const Path& old_prefix, const Path& new_prefix) const;

    /// Comparison operators
    bool operator==(const Path& other) const;
    bool operator!=(const Path& other) const;
    bool operator<(const Path& other) const;

    /// Hash for use in containers
    size_t hash() const;

    /// Swap
    void swap(Path& other) noexcept;

private:
    std::string prim_part_;
    std::string prop_part_;
    bool valid_ = false;

    // Internal parsing helper
    void parse_impl(const std::string& path_str);
};

/// Swap specialization
inline void swap(Path& a, Path& b) noexcept {
    a.swap(b);
}

} // namespace v1
} // namespace lightusd

/// Hash specialization
namespace std {
template<>
struct hash<lightusd::v1::Path> {
    size_t operator()(const lightusd::v1::Path& p) const {
        return p.hash();
    }
};
}
