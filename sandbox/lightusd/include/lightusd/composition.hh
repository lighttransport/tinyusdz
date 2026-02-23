// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Composition arc types and framework
//
// USD Composition follows the LIVRPS strength ordering:
//   L - Local (direct opinions)
//   I - Inherits
//   V - VariantSets
//   R - References
//   P - Payloads
//   S - Specializes

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lightusd/path.hh"

namespace lightusd {
namespace v1 {

// Forward declarations
class Stage;
class Prim;
class Layer;

/// LayerOffset - time offset and scale for referenced layers
struct LayerOffset {
    double offset = 0.0;   // Time offset in frames
    double scale = 1.0;    // Time scale factor

    LayerOffset() = default;
    LayerOffset(double off, double sc) : offset(off), scale(sc) {}

    bool is_identity() const {
        return offset == 0.0 && scale == 1.0;
    }

    bool operator==(const LayerOffset& other) const {
        return offset == other.offset && scale == other.scale;
    }
};

/// Reference - a composition arc referencing another layer/prim
struct Reference {
    std::string asset_path;  // Asset path (empty for internal reference)
    Path prim_path;          // Target prim path (empty for default prim)
    LayerOffset layer_offset;
    // customData could be added here

    Reference() = default;

    /// Create internal reference (same layer)
    explicit Reference(const Path& prim)
        : prim_path(prim) {}

    /// Create external reference
    Reference(const std::string& asset, const Path& prim = Path())
        : asset_path(asset), prim_path(prim) {}

    /// Create external reference with offset
    Reference(const std::string& asset, const Path& prim, const LayerOffset& offset)
        : asset_path(asset), prim_path(prim), layer_offset(offset) {}

    bool is_internal() const { return asset_path.empty(); }

    bool operator==(const Reference& other) const {
        return asset_path == other.asset_path &&
               prim_path == other.prim_path &&
               layer_offset == other.layer_offset;
    }
};

/// Payload - like Reference but for deferred loading
struct Payload {
    std::string asset_path;  // Asset path (empty for internal)
    Path prim_path;          // Target prim path
    LayerOffset layer_offset;

    Payload() = default;

    explicit Payload(const std::string& asset, const Path& prim = Path())
        : asset_path(asset), prim_path(prim) {}

    Payload(const std::string& asset, const Path& prim, const LayerOffset& offset)
        : asset_path(asset), prim_path(prim), layer_offset(offset) {}

    bool is_internal() const { return asset_path.empty(); }

    bool operator==(const Payload& other) const {
        return asset_path == other.asset_path &&
               prim_path == other.prim_path &&
               layer_offset == other.layer_offset;
    }
};

/// ListEditOp - how list edits are applied
enum class ListEditOp : uint8_t {
    Explicit,   // Replace entire list (default = [...])
    Prepend,    // Add to front (prepend = [...])
    Append,     // Add to back (append = [...])
    Delete,     // Remove items (delete = [...])
    Add,        // Add if not present (add = [...])
    Reorder,    // Reorder existing items
};

/// CompositionArcType - types of composition arcs
enum class CompositionArcType : uint8_t {
    None = 0,
    SubLayer,     // Layer stacking (root layer only)
    Inherit,      // Class inheritance
    VariantSet,   // Variant selection
    Reference,    // Asset/prim reference
    Payload,      // Deferred reference
    Specialize,   // Specialization
};

/// Get string name for composition arc type
const char* composition_arc_type_name(CompositionArcType type);

// ============================================================================
// List-Editable Arc Collections
// ============================================================================

/// ReferenceList - list-editable collection of references
class ReferenceList {
public:
    ReferenceList();
    ~ReferenceList();
    ReferenceList(const ReferenceList&);
    ReferenceList(ReferenceList&&) noexcept;
    ReferenceList& operator=(const ReferenceList&);
    ReferenceList& operator=(ReferenceList&&) noexcept;

    /// Check if empty (no operations)
    bool empty() const;

    /// Get explicit list (replaces all)
    const std::vector<Reference>& explicit_items() const;
    void set_explicit(const std::vector<Reference>& refs);
    void set_explicit(std::vector<Reference>&& refs);

    /// Get prepended items
    const std::vector<Reference>& prepended_items() const;
    void prepend(const Reference& ref);
    void set_prepended(const std::vector<Reference>& refs);

    /// Get appended items
    const std::vector<Reference>& appended_items() const;
    void append(const Reference& ref);
    void set_appended(const std::vector<Reference>& refs);

    /// Get deleted items
    const std::vector<Reference>& deleted_items() const;
    void add_delete(const Reference& ref);

    /// Clear all
    void clear();

    /// Check if has explicit list
    bool has_explicit() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// PayloadList - list-editable collection of payloads
class PayloadList {
public:
    PayloadList();
    ~PayloadList();
    PayloadList(const PayloadList&);
    PayloadList(PayloadList&&) noexcept;
    PayloadList& operator=(const PayloadList&);
    PayloadList& operator=(PayloadList&&) noexcept;

    bool empty() const;

    const std::vector<Payload>& explicit_items() const;
    void set_explicit(const std::vector<Payload>& payloads);

    const std::vector<Payload>& prepended_items() const;
    void prepend(const Payload& payload);
    void set_prepended(const std::vector<Payload>& payloads);

    const std::vector<Payload>& appended_items() const;
    void append(const Payload& payload);
    void set_appended(const std::vector<Payload>& payloads);

    void clear();
    bool has_explicit() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// PathList - list-editable collection of paths (for inherits/specializes)
class PathList {
public:
    PathList();
    ~PathList();
    PathList(const PathList&);
    PathList(PathList&&) noexcept;
    PathList& operator=(const PathList&);
    PathList& operator=(PathList&&) noexcept;

    bool empty() const;

    const std::vector<Path>& explicit_items() const;
    void set_explicit(const std::vector<Path>& paths);

    const std::vector<Path>& prepended_items() const;
    void prepend(const Path& path);
    void set_prepended(const std::vector<Path>& paths);

    const std::vector<Path>& appended_items() const;
    void append(const Path& path);
    void set_appended(const std::vector<Path>& paths);

    void clear();
    bool has_explicit() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace v1
} // namespace lightusd
