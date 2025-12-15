// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Value Clips API for animation stitching

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

#include "lightusd/path.hh"

namespace lightusd {
namespace v1 {

/// TimeCodePair - maps stage time to clip index or clip time
struct TimeCodePair {
    double stage_time = 0.0;
    double value = 0.0;  // clip index for 'active', clip time for 'times'

    TimeCodePair() = default;
    TimeCodePair(double st, double v) : stage_time(st), value(v) {}

    bool operator==(const TimeCodePair& other) const {
        return stage_time == other.stage_time && value == other.value;
    }
};

/// ClipSet - a named set of clips for animation stitching
///
/// Example USDA:
/// ```
/// clips = {
///     "default" = {
///         assetPaths = [@./walk.usd@, @./run.usd@]
///         primPath = "/Character"
///         active = [(0, 0), (24, 1)]
///         times = [(0, 0), (24, 24)]
///         manifestAssetPath = @./manifest.usd@
///     }
/// }
/// ```
class ClipSet {
public:
    ClipSet();
    ~ClipSet();
    ClipSet(const ClipSet& other);
    ClipSet(ClipSet&& other) noexcept;
    ClipSet& operator=(const ClipSet& other);
    ClipSet& operator=(ClipSet&& other) noexcept;

    /// Get clip set name
    const std::string& name() const;
    void set_name(const std::string& name);

    /// Asset paths - list of clip files
    const std::vector<std::string>& asset_paths() const;
    void set_asset_paths(const std::vector<std::string>& paths);
    void add_asset_path(const std::string& path);
    void clear_asset_paths();

    /// Prim path - path to animated prim in clip files
    const Path& prim_path() const;
    void set_prim_path(const Path& path);

    /// Active clips - maps stage time ranges to clip indices
    /// Each pair is (stage_time, clip_index)
    const std::vector<TimeCodePair>& active() const;
    void set_active(const std::vector<TimeCodePair>& active);
    void add_active(double stage_time, double clip_index);
    void clear_active();

    /// Times - maps stage time to clip time
    /// Each pair is (stage_time, clip_time)
    const std::vector<TimeCodePair>& times() const;
    void set_times(const std::vector<TimeCodePair>& times);
    void add_time(double stage_time, double clip_time);
    void clear_times();

    /// Manifest asset path - optional manifest for optimization
    const std::string& manifest_asset_path() const;
    void set_manifest_asset_path(const std::string& path);
    bool has_manifest() const;

    /// Template asset path - for clip file naming pattern
    const std::string& template_asset_path() const;
    void set_template_asset_path(const std::string& path);
    bool has_template() const;

    /// Template start/end/stride for file patterns
    double template_start_time() const;
    void set_template_start_time(double t);

    double template_end_time() const;
    void set_template_end_time(double t);

    double template_stride() const;
    void set_template_stride(double s);

    /// Check if clip set is valid (has required fields)
    bool is_valid() const;

    /// Clear all data
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// ClipSets - collection of named clip sets for a prim
class ClipSets {
public:
    ClipSets();
    ~ClipSets();
    ClipSets(const ClipSets& other);
    ClipSets(ClipSets&& other) noexcept;
    ClipSets& operator=(const ClipSets& other);
    ClipSets& operator=(ClipSets&& other) noexcept;

    /// Check if empty
    bool empty() const;

    /// Get number of clip sets
    size_t size() const;

    /// Check if has clip set with name
    bool has(const std::string& name) const;

    /// Get clip set by name (returns nullptr if not found)
    const ClipSet* get(const std::string& name) const;
    ClipSet* get_mutable(const std::string& name);

    /// Add or replace clip set
    void set(const std::string& name, ClipSet clip_set);

    /// Remove clip set by name
    bool remove(const std::string& name);

    /// Get all clip set names
    std::vector<std::string> names() const;

    /// Clear all clip sets
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// ClipInfo - resolved clip information at a specific stage time.
/// Result of resolving which clip is active and how time maps.
struct ClipInfo {
    std::string asset_path;       // Resolved clip file path
    Path prim_path;               // Path to prim in clip file
    double clip_time = 0.0;       // Time value in clip file
    size_t clip_index = 0;        // Index in asset_paths array
    bool valid = false;           // Whether clip info is valid

    ClipInfo() = default;

    bool is_valid() const { return valid && !asset_path.empty(); }
};

/// ClipResolver - resolves clips for a prim at given stage times.
/// Handles both explicit asset paths and template-based clip files.
class ClipResolver {
public:
    ClipResolver();
    ~ClipResolver();
    ClipResolver(const ClipResolver& other);
    ClipResolver(ClipResolver&& other) noexcept;
    ClipResolver& operator=(const ClipResolver& other);
    ClipResolver& operator=(ClipResolver&& other) noexcept;

    /// Set the clip set to resolve
    void set_clip_set(const ClipSet& clip_set);

    /// Get the clip set
    const ClipSet* clip_set() const;

    /// Resolve clip info at a specific stage time.
    /// Returns which clip file to load and what time to query.
    ClipInfo resolve(double stage_time) const;

    /// Get the active clip index at a stage time.
    /// Returns -1 if no clip is active.
    int active_clip_index(double stage_time) const;

    /// Map stage time to clip time (for the active clip).
    /// Returns the interpolated clip time.
    double map_time(double stage_time) const;

    /// Get all clip asset paths (expanded from template if needed).
    /// For template clips, generates paths based on stride/start/end.
    std::vector<std::string> expanded_asset_paths() const;

    /// Get time range covered by clips [start, end].
    /// Returns (0,0) if no clips defined.
    std::pair<double, double> stage_time_range() const;

    /// Get all stage times where clips change (active boundaries).
    std::vector<double> clip_boundaries() const;

    /// Check if resolver is valid (has a clip set with data).
    bool is_valid() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Expand template asset path with time substitution.
/// Template format: "clips/frame.###.usd" where ### is replaced by
/// zero-padded frame number (e.g., "clips/frame.001.usd").
/// Also supports printf-style: "clips/frame.%03d.usd".
std::string expand_template_path(const std::string& template_path, double time);

/// Generate all asset paths from template pattern.
/// start_time, end_time, stride: timing parameters
std::vector<std::string> generate_template_paths(
    const std::string& template_path,
    double start_time, double end_time, double stride);

/// Interpolate between two time values.
/// alpha: interpolation factor (0 = a, 1 = b)
double lerp_time(double a, double b, double alpha);

/// Find interpolation alpha between two time points.
/// Returns value in [0, 1] representing where 't' falls between 'a' and 'b'.
double inverse_lerp(double a, double b, double t);

} // namespace v1
} // namespace lightusd
