// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Layer class (single data source in composition)
//
// A Layer represents a single USD file/data source. Multiple Layers
// are combined through composition to produce a Stage.
//
// Layer vs Stage:
// - Layer: Raw data from a single source (uncomposed)
// - Stage: Composed result of layer stack + composition arcs

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "lightusd/path.hh"
#include "lightusd/value.hh"
#include "lightusd/prim.hh"

namespace lightusd {
namespace v1 {

/// Layer - a single data source containing prim specs
/// This represents the raw (uncomposed) contents of a USD file
class Layer {
public:
    Layer();
    ~Layer();
    Layer(const Layer& other);
    Layer(Layer&& other) noexcept;
    Layer& operator=(const Layer& other);
    Layer& operator=(Layer&& other) noexcept;

    /// Create a new empty layer
    static Layer create();

    /// Create layer with identifier
    static Layer create(const std::string& identifier);

    // ========== Identity ==========

    /// Get layer identifier (file path or anonymous identifier)
    const std::string& identifier() const;

    /// Set layer identifier
    void set_identifier(const std::string& id);

    /// Check if this is an anonymous layer (not backed by a file)
    bool is_anonymous() const;

    /// Get real file path (empty if anonymous)
    const std::string& real_path() const;

    /// Set real file path
    void set_real_path(const std::string& path);

    // ========== Sublayers ==========

    /// Get sublayer asset paths (weakest to strongest)
    const std::vector<std::string>& sublayer_paths() const;

    /// Set sublayer paths
    void set_sublayer_paths(const std::vector<std::string>& paths);

    /// Add sublayer path (appended, becomes strongest sublayer)
    void add_sublayer_path(const std::string& path);

    /// Insert sublayer path at index
    void insert_sublayer_path(size_t index, const std::string& path);

    /// Remove sublayer path
    bool remove_sublayer_path(const std::string& path);

    // ========== Root Prim Specs ==========

    /// Get root prim count
    size_t root_prim_count() const;

    /// Get root prim by index
    const Prim* root_prim(size_t index) const;
    Prim* root_prim_mutable(size_t index);

    /// Get root prim by name
    const Prim* root_prim(const std::string& name) const;
    Prim* root_prim_mutable(const std::string& name);

    /// Add root prim
    bool add_root_prim(Prim prim);

    /// Remove root prim
    bool remove_root_prim(const std::string& name);

    /// Get all root prim names
    std::vector<std::string> root_prim_names() const;

    // ========== Layer Metadata ==========

    /// Get default prim name
    const std::string& default_prim() const;
    void set_default_prim(const std::string& name);

    /// Get start/end time codes
    double start_time_code() const;
    void set_start_time_code(double t);
    double end_time_code() const;
    void set_end_time_code(double t);

    /// Get frames per second
    double frames_per_second() const;
    void set_frames_per_second(double fps);

    /// Get time codes per second
    double time_codes_per_second() const;
    void set_time_codes_per_second(double tcps);

    /// Get up axis
    const std::string& up_axis() const;
    void set_up_axis(const std::string& axis);

    /// Get meters per unit
    double meters_per_unit() const;
    void set_meters_per_unit(double mpu);

    /// Get documentation
    std::string documentation() const;
    void set_documentation(const std::string& doc);

    /// Get comment
    std::string comment() const;
    void set_comment(const std::string& comment);

    // ========== Generic Metadata ==========

    /// Check if has metadata key
    bool has_metadata(const std::string& key) const;

    /// Get metadata value
    const Value* get_metadata(const std::string& key) const;

    /// Set metadata value
    void set_metadata(const std::string& key, const Value& value);

    /// Remove metadata
    bool remove_metadata(const std::string& key);

    /// Get all metadata keys
    std::vector<std::string> metadata_keys() const;

    // ========== Custom Layer Data ==========

    /// Check if has custom layer data key
    bool has_custom_layer_data(const std::string& key) const;

    /// Get custom layer data value
    const Value* get_custom_layer_data(const std::string& key) const;

    /// Set custom layer data value
    void set_custom_layer_data(const std::string& key, const Value& value);

    // ========== Prim Lookup ==========

    /// Get prim at path (returns nullptr if not found)
    const Prim* get_prim_at_path(const Path& path) const;
    Prim* get_prim_at_path_mutable(const Path& path);

    // ========== Utility ==========

    /// Clear all layer data
    void clear();

    /// Swap contents
    void swap(Layer& other) noexcept;

    /// Check if layer is empty (no prims, no sublayers)
    bool empty() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Swap specialization
inline void swap(Layer& a, Layer& b) noexcept {
    a.swap(b);
}

} // namespace v1
} // namespace lightusd
