// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Attribute Evaluation
//
// Provides unified attribute value resolution with:
// - Time sample interpolation
// - Default value fallback
// - Connection following for shaders
// - Type-safe accessors

#pragma once

#include "../types/value.hh"
#include "../types/interpolation.hh"
#include "../stage/stage.hh"
#include <string>
#include <optional>

namespace tinyusdz {
namespace next {

/// Evaluation options
struct EvalOptions {
  double time = 0.0;                              // Time to evaluate at
  TimeInterpolation interp = TimeInterpolation::Linear;  // Interpolation mode
  bool follow_connections = true;                 // Follow shader connections
  int max_connection_depth = 16;                  // Max connection chain depth
};

/// Evaluation result with metadata
struct EvalResult {
  Value value;
  bool success = false;
  bool from_time_sample = false;    // Value came from time sample
  bool from_default = false;        // Value came from default
  bool from_connection = false;     // Value resolved via connection
  bool interpolated = false;        // Value was interpolated
  std::string source_path;          // Path where value was found
};

/// AttributeEval - attribute value evaluation
class AttributeEval {
public:
  explicit AttributeEval(const Stage* stage);

  /// Set evaluation time
  void SetTime(double time) { options_.time = time; }
  double GetTime() const { return options_.time; }

  /// Set interpolation mode
  void SetInterpolation(TimeInterpolation mode) { options_.interp = mode; }

  /// Set connection following
  void SetFollowConnections(bool follow) { options_.follow_connections = follow; }

  /// Get current options
  const EvalOptions& GetOptions() const { return options_; }
  void SetOptions(const EvalOptions& opts) { options_ = opts; }

  // ============================================================
  // Main evaluation methods
  // ============================================================

  /// Evaluate attribute and return result with metadata
  EvalResult Eval(const UsdPrim& prim, const std::string& attr_name) const;

  /// Evaluate attribute at specific time
  EvalResult EvalAt(const UsdPrim& prim, const std::string& attr_name, double time) const;

  /// Evaluate with custom options
  EvalResult EvalWith(const UsdPrim& prim, const std::string& attr_name,
                      const EvalOptions& options) const;

  // ============================================================
  // Type-safe evaluation (returns std::optional)
  // ============================================================

  std::optional<bool> EvalBool(const UsdPrim& prim, const std::string& attr_name) const;
  std::optional<int32_t> EvalInt(const UsdPrim& prim, const std::string& attr_name) const;
  std::optional<float> EvalFloat(const UsdPrim& prim, const std::string& attr_name) const;
  std::optional<double> EvalDouble(const UsdPrim& prim, const std::string& attr_name) const;

  // Vector types (return false if not found, fill output arrays)
  bool EvalFloat2(const UsdPrim& prim, const std::string& attr_name, float* out) const;
  bool EvalFloat3(const UsdPrim& prim, const std::string& attr_name, float* out) const;
  bool EvalFloat4(const UsdPrim& prim, const std::string& attr_name, float* out) const;
  bool EvalDouble2(const UsdPrim& prim, const std::string& attr_name, double* out) const;
  bool EvalDouble3(const UsdPrim& prim, const std::string& attr_name, double* out) const;
  bool EvalDouble4(const UsdPrim& prim, const std::string& attr_name, double* out) const;

  // Matrix types
  bool EvalMatrix3f(const UsdPrim& prim, const std::string& attr_name, float* out9) const;
  bool EvalMatrix4f(const UsdPrim& prim, const std::string& attr_name, float* out16) const;
  bool EvalMatrix3d(const UsdPrim& prim, const std::string& attr_name, double* out9) const;
  bool EvalMatrix4d(const UsdPrim& prim, const std::string& attr_name, double* out16) const;

  // String types
  std::optional<std::string> EvalString(const UsdPrim& prim, const std::string& attr_name) const;
  std::optional<std::string> EvalToken(const UsdPrim& prim, const std::string& attr_name) const;
  std::optional<std::string> EvalAssetPath(const UsdPrim& prim, const std::string& attr_name) const;

  // Array types
  bool EvalFloatArray(const UsdPrim& prim, const std::string& attr_name,
                      std::vector<float>* out) const;
  bool EvalIntArray(const UsdPrim& prim, const std::string& attr_name,
                    std::vector<int32_t>* out) const;

  // ============================================================
  // Connection resolution
  // ============================================================

  /// Check if attribute has a connection
  bool HasConnection(const UsdPrim& prim, const std::string& attr_name) const;

  /// Get connection target path (e.g., "/Materials/Mat1/Shader.outputs:out")
  std::string GetConnectionPath(const UsdPrim& prim, const std::string& attr_name) const;

  /// Resolve connection and return the connected prim
  UsdPrim ResolveConnectionToPrim(const UsdPrim& prim, const std::string& attr_name) const;

  /// Resolve connection chain and get final value
  EvalResult ResolveConnection(const UsdPrim& prim, const std::string& attr_name) const;

  // ============================================================
  // Fallback value support
  // ============================================================

  /// Evaluate with fallback value
  template<typename T>
  T EvalOr(const UsdPrim& prim, const std::string& attr_name, T fallback) const;

private:
  const Stage* stage_;
  EvalOptions options_;

  // Internal helpers
  EvalResult EvalInternal(const UsdPrim& prim, const std::string& attr_name,
                          const EvalOptions& opts, int depth) const;
  EvalResult EvalFromPrimSpec(const PrimSpec* spec, const std::string& attr_name,
                              const EvalOptions& opts) const;
  EvalResult FollowConnection(const std::string& connection_path,
                              const EvalOptions& opts, int depth) const;
};

// ============================================================
// Convenience functions (use default evaluation options)
// ============================================================

/// Evaluate attribute with time sample interpolation
EvalResult EvalAttribute(const Stage& stage, const UsdPrim& prim,
                         const std::string& attr_name, double time = 0.0);

/// Quick type-safe getters (return false if not found)
bool GetFloat(const Stage& stage, const UsdPrim& prim,
              const std::string& name, float* out, double time = 0.0);
bool GetFloat3(const Stage& stage, const UsdPrim& prim,
               const std::string& name, float* out3, double time = 0.0);
bool GetDouble(const Stage& stage, const UsdPrim& prim,
               const std::string& name, double* out, double time = 0.0);
bool GetDouble3(const Stage& stage, const UsdPrim& prim,
                const std::string& name, double* out3, double time = 0.0);
bool GetMatrix4f(const Stage& stage, const UsdPrim& prim,
                 const std::string& name, float* out16, double time = 0.0);
bool GetMatrix4d(const Stage& stage, const UsdPrim& prim,
                 const std::string& name, double* out16, double time = 0.0);

// ============================================================
// Template implementation
// ============================================================

template<typename T>
T AttributeEval::EvalOr(const UsdPrim& prim, const std::string& attr_name, T fallback) const {
  EvalResult result = Eval(prim, attr_name);
  if (!result.success) {
    return fallback;
  }

  // Type dispatch based on T
  if constexpr (std::is_same_v<T, bool>) {
    const bool* v = result.value.as_bool();
    return v ? *v : fallback;
  } else if constexpr (std::is_same_v<T, int32_t>) {
    const int32_t* v = result.value.as_int();
    return v ? *v : fallback;
  } else if constexpr (std::is_same_v<T, float>) {
    const float* v = result.value.as_float();
    return v ? *v : fallback;
  } else if constexpr (std::is_same_v<T, double>) {
    const double* v = result.value.as_double();
    return v ? *v : fallback;
  } else if constexpr (std::is_same_v<T, std::string>) {
    const std::string* v = result.value.as_string();
    if (v) return *v;
    v = result.value.as_token();
    return v ? *v : fallback;
  } else {
    return fallback;
  }
}

}  // namespace next
}  // namespace tinyusdz
