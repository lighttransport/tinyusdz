// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USD Attribute definition
// Represents a typed attribute with optional time samples

#pragma once

#include "../types/value.hh"
#include <map>
#include <string>

namespace tinyusdz {
namespace next {

/// Interpolation type for attributes
enum class Interpolation : uint8_t {
  Constant,   // Value doesn't vary
  Uniform,    // One value per face/element
  Varying,    // Linearly interpolated
  Vertex,     // Per-vertex values
  FaceVarying // Per-face-vertex values
};

/// Variability of an attribute
enum class Variability : uint8_t {
  Varying,  // Can have time samples
  Uniform   // Single value only
};

/// Attribute - a named, typed value that may vary over time
class Attribute {
public:
  /// Default constructor
  Attribute() = default;

  /// Construct with name and type
  Attribute(const std::string& name, TypeId type_id);
  Attribute(std::string&& name, TypeId type_id);

  /// Copy and move
  Attribute(const Attribute&) = default;
  Attribute(Attribute&&) = default;
  Attribute& operator=(const Attribute&) = default;
  Attribute& operator=(Attribute&&) = default;

  // ============================================================
  // Attribute metadata
  // ============================================================

  /// Get attribute name
  const std::string& name() const { return name_; }

  /// Get the declared type
  TypeId type_id() const { return type_id_; }

  /// Check if attribute has a value (default or time-sampled)
  bool has_value() const;

  /// Check if attribute has time samples
  bool has_time_samples() const { return !time_samples_.empty(); }

  /// Get interpolation type
  Interpolation interpolation() const { return interpolation_; }
  void set_interpolation(Interpolation interp) { interpolation_ = interp; }

  /// Get variability
  Variability variability() const { return variability_; }
  void set_variability(Variability var) { variability_ = var; }

  /// Check if this is a custom attribute
  bool is_custom() const { return is_custom_; }
  void set_custom(bool custom) { is_custom_ = custom; }

  // ============================================================
  // Value access
  // ============================================================

  /// Get the default value (no time sample)
  const Value& default_value() const { return default_value_; }
  Value& default_value() { return default_value_; }

  /// Set the default value
  void set_default(Value value);

  /// Get time sample map
  const std::map<double, Value>& time_samples() const { return time_samples_; }

  /// Add a time sample
  void add_time_sample(double time, Value value);

  /// Get value at specific time (interpolated if necessary)
  Value value_at(double time) const;

  /// Get the list of time sample times
  std::vector<double> sample_times() const;

  /// Clear all time samples
  void clear_time_samples();

  // ============================================================
  // Connection support
  // ============================================================

  /// Check if attribute has a connection
  bool has_connection() const { return !connection_path_.empty(); }

  /// Get connection path
  const std::string& connection_path() const { return connection_path_; }

  /// Set connection path
  void set_connection(const std::string& path);

private:
  std::string name_;
  TypeId type_id_ = TypeId::Invalid;
  Interpolation interpolation_ = Interpolation::Constant;
  Variability variability_ = Variability::Varying;
  bool is_custom_ = false;

  Value default_value_;
  std::map<double, Value> time_samples_;
  std::string connection_path_;
};

}  // namespace next
}  // namespace tinyusdz
