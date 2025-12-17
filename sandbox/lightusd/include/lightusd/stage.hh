// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Stage class (root of scene hierarchy)

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "lightusd/result.hh"
#include "lightusd/path.hh"
#include "lightusd/prim.hh"
#include "lightusd/composition.hh"

namespace lightusd {
namespace v1 {

/// SubLayer - reference to a sublayer with optional offset
struct SubLayer {
    std::string asset_path;
    LayerOffset layer_offset;

    SubLayer() = default;
    explicit SubLayer(const std::string& path) : asset_path(path) {}
    SubLayer(const std::string& path, const LayerOffset& offset)
        : asset_path(path), layer_offset(offset) {}
};

/// Stage - the root container for a USD scene.
/// Contains root prims and stage-level metadata.
class Stage {
public:
    /// Default constructor
    Stage();

    /// Destructor
    ~Stage();

    /// Copy constructor
    Stage(const Stage& other);

    /// Move constructor
    Stage(Stage&& other) noexcept;

    /// Copy assignment
    Stage& operator=(const Stage& other);

    /// Move assignment
    Stage& operator=(Stage&& other) noexcept;

    /// Create a new empty stage
    static Stage create();

    // ========== Root Prims ==========

    /// Get root prim count
    size_t root_prim_count() const;

    /// Get root prim by index
    const Prim* root_prim(size_t index) const;

    /// Get mutable root prim by index
    Prim* root_prim_mutable(size_t index);

    /// Get root prim by name
    const Prim* root_prim(const std::string& name) const;

    /// Get mutable root prim by name
    Prim* root_prim_mutable(const std::string& name);

    /// Add root prim (takes ownership)
    bool add_root_prim(Prim prim);

    /// Remove root prim by name
    bool remove_root_prim(const std::string& name);

    /// Get all root prim names
    std::vector<std::string> root_prim_names() const;

    // ========== Prim Lookup ==========

    /// Get prim by path
    Result<const Prim*> get_prim_at_path(const Path& path) const;

    /// Get mutable prim by path
    Result<Prim*> get_prim_at_path_mutable(const Path& path);

    // ========== Traversal ==========

    /// Visitor callback signature
    /// Returns: true to continue traversal, false to stop
    typedef bool (*VisitorFn)(const Prim& prim, int depth, void* userdata);

    /// Traverse all prims depth-first
    void traverse(VisitorFn visitor, void* userdata = nullptr) const;

    /// Traverse with mutable access
    typedef bool (*MutableVisitorFn)(Prim& prim, int depth, void* userdata);
    void traverse_mutable(MutableVisitorFn visitor, void* userdata = nullptr);

    // ========== Metadata (Generic Dictionary) ==========

    /// Check if has metadata key
    bool has_metadata(const std::string& key) const;

    /// Get metadata value (returns nullptr if not found)
    const Value* get_metadata(const std::string& key) const;

    /// Set metadata value
    void set_metadata(const std::string& key, const Value& value);

    /// Remove metadata key
    bool remove_metadata(const std::string& key);

    /// Get all metadata keys
    std::vector<std::string> metadata_keys() const;

    /// Get metadata count
    size_t metadata_count() const;

    // ========== Common Stage Metadata Accessors ==========

    /// Get start time code
    double start_time_code() const;

    /// Set start time code
    void set_start_time_code(double t);

    /// Get end time code
    double end_time_code() const;

    /// Set end time code
    void set_end_time_code(double t);

    /// Get frames per second
    double frames_per_second() const;

    /// Set frames per second
    void set_frames_per_second(double fps);

    /// Get time codes per second
    double time_codes_per_second() const;

    /// Set time codes per second
    void set_time_codes_per_second(double tcps);

    /// Get up axis ("Y" or "Z")
    const std::string& up_axis() const;

    /// Set up axis
    void set_up_axis(const std::string& axis);

    /// Get meters per unit
    double meters_per_unit() const;

    /// Set meters per unit
    void set_meters_per_unit(double mpu);

    /// Get documentation string
    std::string documentation() const;

    /// Set documentation string
    void set_documentation(const std::string& doc);

    /// Get comment string
    std::string comment() const;

    /// Set comment string
    void set_comment(const std::string& comment);

    /// Get owner
    std::string owner() const;

    /// Set owner
    void set_owner(const std::string& owner);

    // ========== Custom Layer Data (nested dictionary) ==========

    /// Check if has custom layer data key
    bool has_custom_layer_data(const std::string& key) const;

    /// Get custom layer data value
    const Value* get_custom_layer_data(const std::string& key) const;

    /// Set custom layer data value
    void set_custom_layer_data(const std::string& key, const Value& value);

    /// Remove custom layer data key
    bool remove_custom_layer_data(const std::string& key);

    /// Get all custom layer data keys
    std::vector<std::string> custom_layer_data_keys() const;

    /// Get custom layer data count
    size_t custom_layer_data_count() const;

    // ========== Sublayers ==========

    /// Get sublayer count
    size_t sublayer_count() const;

    /// Get sublayer by index
    const SubLayer* sublayer(size_t index) const;

    /// Get all sublayers
    const std::vector<SubLayer>& sublayers() const;

    /// Add sublayer at end
    void add_sublayer(const SubLayer& sublayer);

    /// Add sublayer at end (convenience)
    void add_sublayer(const std::string& path);

    /// Add sublayer with offset
    void add_sublayer(const std::string& path, const LayerOffset& offset);

    /// Insert sublayer at index
    void insert_sublayer(size_t index, const SubLayer& sublayer);

    /// Remove sublayer by index
    bool remove_sublayer(size_t index);

    /// Remove sublayer by path
    bool remove_sublayer(const std::string& path);

    /// Clear all sublayers
    void clear_sublayers();

    /// Set all sublayers (replaces existing)
    void set_sublayers(const std::vector<SubLayer>& sublayers);

    // ========== Default Prim ==========

    /// Get default prim name
    const std::string& default_prim() const;

    /// Set default prim name
    void set_default_prim(const std::string& name);

    /// Get default prim (returns nullptr if not set or not found)
    const Prim* get_default_prim() const;

    // ========== Path Management ==========

    /// Update all prim paths (call after modifying hierarchy)
    void commit();

    // ========== Export ==========

    /// Export to USDA format string (default formatting)
    std::string to_usda() const;

    /// Export to USDA format string with custom options
    /// @param indent_size Number of spaces per indent level (default: 4)
    /// @param use_tabs Use tabs instead of spaces for indentation
    std::string to_usda(int indent_size, bool use_tabs = false) const;

    // ========== Utility ==========

    /// Swap contents
    void swap(Stage& other) noexcept;

    /// Clear all data
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Internal traversal helpers
    static void traverse_impl(const Prim& prim, int depth, VisitorFn visitor, void* userdata);
    static void traverse_mutable_impl(Prim& prim, int depth, MutableVisitorFn visitor, void* userdata);
};

/// Swap specialization
inline void swap(Stage& a, Stage& b) noexcept {
    a.swap(b);
}

} // namespace v1
} // namespace lightusd
