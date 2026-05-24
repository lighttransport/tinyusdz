#include "value-to-json.hh"

#include <sstream>
#include <vector>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/jsonhpp/nlohmann/json.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {
namespace tydra {

namespace {

// ---------------------------------------------------------------------------
// Helper: append elements of any array-like compound type to a JSON array
// ---------------------------------------------------------------------------
template <typename T, size_t N>
void AppendCompound(nlohmann::json &arr, const std::array<T, N> &v) {
  for (size_t i = 0; i < N; i++) {
    arr.push_back(static_cast<double>(v[i]));
  }
}

template <size_t N>
void AppendCompound(nlohmann::json &arr, const std::array<int32_t, N> &v) {
  for (size_t i = 0; i < N; i++) {
    arr.push_back(v[i]);
  }
}

template <size_t N>
[[maybe_unused]]
void AppendCompound(nlohmann::json &arr, const std::array<uint32_t, N> &v) {
  for (size_t i = 0; i < N; i++) {
    arr.push_back(static_cast<int64_t>(v[i]));
  }
}

template <size_t N>
[[maybe_unused]]
void AppendCompound(nlohmann::json &arr, const std::array<int64_t, N> &v) {
  for (size_t i = 0; i < N; i++) {
    arr.push_back(v[i]);
  }
}

template <size_t N>
[[maybe_unused]]
void AppendCompound(nlohmann::json &arr, const std::array<uint64_t, N> &v) {
  for (size_t i = 0; i < N; i++) {
    arr.push_back(static_cast<double>(v[i]));
  }
}

template <size_t N>
void AppendCompound(nlohmann::json &arr, const std::array<value::half, N> &v) {
  for (size_t i = 0; i < N; i++) {
    arr.push_back(static_cast<double>(value::half_to_float(v[i])));
  }
}

// ---------------------------------------------------------------------------
// Helper: compound value to JSON array (float3, int2, etc.)
// ---------------------------------------------------------------------------
template <typename T>
nlohmann::json CompoundToJSON(const T &v) {
  nlohmann::json arr = nlohmann::json::array();
  AppendCompound(arr, v);
  return arr;
}

// ---------------------------------------------------------------------------
// Helper: element values from array to JSON
// ---------------------------------------------------------------------------
template <typename T>
nlohmann::json ArrayValuesToJSON(const std::vector<T> &vec) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto &elem : vec) {
    arr.push_back(CompoundToJSON(elem));
  }
  return arr;
}

template <>
nlohmann::json ArrayValuesToJSON<float>(const std::vector<float> &vec) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto &elem : vec) {
    arr.push_back(static_cast<double>(elem));
  }
  return arr;
}

template <>
nlohmann::json ArrayValuesToJSON<double>(const std::vector<double> &vec) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto &elem : vec) {
    arr.push_back(elem);
  }
  return arr;
}

template <>
nlohmann::json ArrayValuesToJSON<int32_t>(const std::vector<int32_t> &vec) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto &elem : vec) {
    arr.push_back(elem);
  }
  return arr;
}

template <>
nlohmann::json ArrayValuesToJSON<value::half>(const std::vector<value::half> &vec) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto &elem : vec) {
    arr.push_back(static_cast<double>(value::half_to_float(elem)));
  }
  return arr;
}

template <>
nlohmann::json ArrayValuesToJSON<std::string>(const std::vector<std::string> &vec) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto &elem : vec) {
    arr.push_back(elem);
  }
  return arr;
}

template <>
nlohmann::json ArrayValuesToJSON<value::token>(const std::vector<value::token> &vec) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto &elem : vec) {
    arr.push_back(elem.str());
  }
  return arr;
}

// ---------------------------------------------------------------------------
// Matrix helpers
// ---------------------------------------------------------------------------
template <typename T, int R, int C>
nlohmann::json MatrixToJSON(const T &m) {
  nlohmann::json arr = nlohmann::json::array();
  for (int i = 0; i < R; i++) {
    nlohmann::json row = nlohmann::json::array();
    for (int j = 0; j < C; j++) {
      row.push_back(static_cast<double>(m.m[i][j]));
    }
    arr.push_back(row);
  }
  return arr;
}

template <typename T, int R, int C>
nlohmann::json MatrixArrayToJSON(const std::vector<T> &vec) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto &m : vec) {
    arr.push_back(MatrixToJSON<T, R, C>(m));
  }
  return arr;
}

// ---------------------------------------------------------------------------
// Quaternion helpers
// ---------------------------------------------------------------------------
template <typename T>
nlohmann::json QuatToJSON(const T &q) {
  nlohmann::json arr = nlohmann::json::array();
  arr.push_back(static_cast<double>(q[0]));
  arr.push_back(static_cast<double>(q[1]));
  arr.push_back(static_cast<double>(q[2]));
  arr.push_back(static_cast<double>(q[3]));
  return arr;
}

// quath uses half type which needs half_to_float conversion
template <>
nlohmann::json QuatToJSON<value::quath>(const value::quath &q) {
  nlohmann::json arr = nlohmann::json::array();
  arr.push_back(static_cast<double>(value::half_to_float(q[0])));
  arr.push_back(static_cast<double>(value::half_to_float(q[1])));
  arr.push_back(static_cast<double>(value::half_to_float(q[2])));
  arr.push_back(static_cast<double>(value::half_to_float(q[3])));
  return arr;
}

// ---------------------------------------------------------------------------
// Dispatch by type_id to extract scalar / compound value
// Returns null JSON if type not handled
// ---------------------------------------------------------------------------
template <typename ValueType>
bool TryGetValueAs(const value::Value &val, nlohmann::json &out) {
  auto v = val.get_value<ValueType>(false);
  if (!v) return false;
  out = static_cast<double>(v.value());
  return true;
}

// Specializations for non-arithmetic types
template <> bool TryGetValueAs<bool>(const value::Value &val, nlohmann::json &out) {
  auto v = val.get_value<bool>(false);
  if (!v) return false;
  out = v.value();
  return true;
}

template <> bool TryGetValueAs<std::string>(const value::Value &val, nlohmann::json &out) {
  auto v = val.get_value<std::string>(false);
  if (!v) return false;
  out = v.value();
  return true;
}

template <> bool TryGetValueAs<value::token>(const value::Value &val, nlohmann::json &out) {
  auto v = val.get_value<value::token>(false);
  if (!v) return false;
  out = v.value().str();
  return true;
}

// ---------------------------------------------------------------------------
// Full typed dispatch for compound types -> returning value as typed JSON
// ---------------------------------------------------------------------------
// Templates for compound types
template <typename T>
bool TryGetCompound(const value::Value &val, nlohmann::json &out) {
  auto v = val.get_value<T>(false);
  if (!v) return false;
  out = CompoundToJSON(v.value());
  return true;
}

template <typename T>
[[maybe_unused]]
bool TryGetCompoundAsElements(const value::Value &val, nlohmann::json &out) {
  auto v = val.get_value<T>(false);
  if (!v) return false;
  // For compound types like float3, return as flat array of arrays
  // but since compound types ARE the value, just return the compound
  out = CompoundToJSON(v.value());
  return true;
}

// ---------------------------------------------------------------------------
// Convert a typed value to a JSON array of elements
// ---------------------------------------------------------------------------
template <typename ElemType>
bool TryGetArrayValue(const value::Value &val, nlohmann::json &out) {
  auto v = val.get_value<std::vector<ElemType>>(false);
  if (!v) return false;
  out = ArrayValuesToJSON<ElemType>(v.value());
  return true;
}

// Typed arrays (TypedArray<T>)
template <typename ElemType>
[[maybe_unused]]
bool TryGetTypedArrayValue(const value::Value &val, nlohmann::json &out) {
  auto v = val.get_value<TypedArray<ElemType>>(false);
  if (!v) return false;
  auto view = v.value();
  std::vector<ElemType> tmp;
  tmp.reserve(view.size());
  for (size_t i = 0; i < view.size(); i++) {
    tmp.push_back(view[i]);
  }
  out = ArrayValuesToJSON<ElemType>(tmp);
  return true;
}

} // namespace

// ===========================================================================
// Public: ValueToJSON
// ===========================================================================
nlohmann::json ValueToJSON(const value::Value &val) {
  if (val.is_empty()) {
    return {{"type", "null"}};
  }

  if (val.is_none()) {
    return {{"type", "None"}};
  }

  uint32_t tid = val.type_id();
  std::string type_name = val.type_name();

  // Handle arrays first
  if (val.is_array()) {
    uint32_t elem_tid = tid & ~value::TYPE_ID_1D_ARRAY_BIT;

    // Dispatch on element type
    nlohmann::json values;

    if (elem_tid == value::TypeTraits<float>::type_id()) {
      if (!TryGetArrayValue<float>(val, values)) goto fallback;
    } else if (elem_tid == value::TypeTraits<double>::type_id()) {
      if (!TryGetArrayValue<double>(val, values)) goto fallback;
    } else if (elem_tid == value::TypeTraits<int32_t>::type_id()) {
      if (!TryGetArrayValue<int32_t>(val, values)) goto fallback;
    } else if (elem_tid == value::TypeTraits<value::half>::type_id()) {
      if (!TryGetArrayValue<value::half>(val, values)) goto fallback;
    } else if (elem_tid == value::TypeTraits<std::string>::type_id()) {
      if (!TryGetArrayValue<std::string>(val, values)) goto fallback;
    } else if (elem_tid == value::TypeTraits<value::token>::type_id()) {
      if (!TryGetArrayValue<value::token>(val, values)) goto fallback;
    } else if (elem_tid == value::TypeTraits<value::float2>::type_id()) {
      if (!TryGetArrayValue<value::float2>(val, values)) goto fallback;
    } else if (elem_tid == value::TypeTraits<value::float3>::type_id()) {
      if (!TryGetArrayValue<value::float3>(val, values)) goto fallback;
    } else if (elem_tid == value::TypeTraits<value::float4>::type_id()) {
      if (!TryGetArrayValue<value::float4>(val, values)) goto fallback;
    } else if (elem_tid == value::TypeTraits<value::double2>::type_id()) {
      if (!TryGetArrayValue<value::double2>(val, values)) goto fallback;
    } else if (elem_tid == value::TypeTraits<value::double3>::type_id()) {
      if (!TryGetArrayValue<value::double3>(val, values)) goto fallback;
    } else if (elem_tid == value::TypeTraits<value::double4>::type_id()) {
      if (!TryGetArrayValue<value::double4>(val, values)) goto fallback;
    } else if (elem_tid == value::TypeTraits<value::int2>::type_id()) {
      if (!TryGetArrayValue<value::int2>(val, values)) goto fallback;
    } else if (elem_tid == value::TypeTraits<value::int3>::type_id()) {
      if (!TryGetArrayValue<value::int3>(val, values)) goto fallback;
    } else if (elem_tid == value::TypeTraits<value::int4>::type_id()) {
      if (!TryGetArrayValue<value::int4>(val, values)) goto fallback;
    } else if (elem_tid == value::TypeTraits<value::half2>::type_id()) {
      if (!TryGetArrayValue<value::half2>(val, values)) goto fallback;
    } else if (elem_tid == value::TypeTraits<value::half3>::type_id()) {
      if (!TryGetArrayValue<value::half3>(val, values)) goto fallback;
    } else if (elem_tid == value::TypeTraits<value::half4>::type_id()) {
      if (!TryGetArrayValue<value::half4>(val, values)) goto fallback;
    } else if (elem_tid == value::TypeTraits<value::matrix3d>::type_id()) {
      auto v = val.get_value<std::vector<value::matrix3d>>(false);
      if (v) values = MatrixArrayToJSON<value::matrix3d, 3, 3>(v.value());
      else goto fallback;
    } else if (elem_tid == value::TypeTraits<value::matrix4d>::type_id()) {
      auto v = val.get_value<std::vector<value::matrix4d>>(false);
      if (v) values = MatrixArrayToJSON<value::matrix4d, 4, 4>(v.value());
      else goto fallback;
    } else {
      goto fallback;
    }

    return {{"type", type_name + "[]"}, {"value", values}};
  }

  // Non-array: dispatch on type_id
  {
    // Handle scalars
    if (tid == value::TypeTraits<float>::type_id()) {
      nlohmann::json v;
      if (TryGetValueAs<float>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<double>::type_id()) {
      nlohmann::json v;
      if (TryGetValueAs<double>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<int32_t>::type_id()) {
      nlohmann::json v;
      if (TryGetValueAs<int32_t>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<uint32_t>::type_id()) {
      nlohmann::json v;
      if (TryGetValueAs<uint32_t>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<int64_t>::type_id()) {
      nlohmann::json v;
      if (TryGetValueAs<int64_t>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<uint64_t>::type_id()) {
      nlohmann::json v;
      if (TryGetValueAs<uint64_t>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<bool>::type_id()) {
      nlohmann::json v;
      if (TryGetValueAs<bool>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<std::string>::type_id()) {
      nlohmann::json v;
      if (TryGetValueAs<std::string>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<value::token>::type_id()) {
      nlohmann::json v;
      if (TryGetValueAs<value::token>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<value::half>::type_id()) {
      auto v = val.get_value<value::half>(false);
      if (v) return {{"type", type_name}, {"value", value::half_to_float(v.value())}};
    }
    // Handle compounds
    else if (tid == value::TypeTraits<value::float2>::type_id()) {
      nlohmann::json v;
      if (TryGetCompound<value::float2>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<value::float3>::type_id()) {
      nlohmann::json v;
      if (TryGetCompound<value::float3>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<value::float4>::type_id()) {
      nlohmann::json v;
      if (TryGetCompound<value::float4>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<value::double2>::type_id()) {
      nlohmann::json v;
      if (TryGetCompound<value::double2>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<value::double3>::type_id()) {
      nlohmann::json v;
      if (TryGetCompound<value::double3>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<value::double4>::type_id()) {
      nlohmann::json v;
      if (TryGetCompound<value::double4>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<value::int2>::type_id()) {
      nlohmann::json v;
      if (TryGetCompound<value::int2>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<value::int3>::type_id()) {
      nlohmann::json v;
      if (TryGetCompound<value::int3>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<value::int4>::type_id()) {
      nlohmann::json v;
      if (TryGetCompound<value::int4>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<value::half2>::type_id()) {
      nlohmann::json v;
      if (TryGetCompound<value::half2>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<value::half3>::type_id()) {
      nlohmann::json v;
      if (TryGetCompound<value::half3>(val, v))
        return {{"type", type_name}, {"value", v}};
    } else if (tid == value::TypeTraits<value::half4>::type_id()) {
      nlohmann::json v;
      if (TryGetCompound<value::half4>(val, v))
        return {{"type", type_name}, {"value", v}};
    }
    // Handle matrices
    else if (tid == value::TypeTraits<value::matrix2f>::type_id()) {
      auto v = val.get_value<value::matrix2f>(false);
      if (v) return {{"type", type_name}, {"value", MatrixToJSON<value::matrix2f, 2, 2>(v.value())}};
    } else if (tid == value::TypeTraits<value::matrix3f>::type_id()) {
      auto v = val.get_value<value::matrix3f>(false);
      if (v) return {{"type", type_name}, {"value", MatrixToJSON<value::matrix3f, 3, 3>(v.value())}};
    } else if (tid == value::TypeTraits<value::matrix4f>::type_id()) {
      auto v = val.get_value<value::matrix4f>(false);
      if (v) return {{"type", type_name}, {"value", MatrixToJSON<value::matrix4f, 4, 4>(v.value())}};
    } else if (tid == value::TypeTraits<value::matrix2d>::type_id()) {
      auto v = val.get_value<value::matrix2d>(false);
      if (v) return {{"type", type_name}, {"value", MatrixToJSON<value::matrix2d, 2, 2>(v.value())}};
    } else if (tid == value::TypeTraits<value::matrix3d>::type_id()) {
      auto v = val.get_value<value::matrix3d>(false);
      if (v) return {{"type", type_name}, {"value", MatrixToJSON<value::matrix3d, 3, 3>(v.value())}};
    } else if (tid == value::TypeTraits<value::matrix4d>::type_id()) {
      auto v = val.get_value<value::matrix4d>(false);
      if (v) return {{"type", type_name}, {"value", MatrixToJSON<value::matrix4d, 4, 4>(v.value())}};
    }
    // Handle quaternions
    else if (tid == value::TypeTraits<value::quath>::type_id()) {
      auto v = val.get_value<value::quath>(false);
      if (v) return {{"type", type_name}, {"value", QuatToJSON(v.value())}};
    } else if (tid == value::TypeTraits<value::quatf>::type_id()) {
      auto v = val.get_value<value::quatf>(false);
      if (v) return {{"type", type_name}, {"value", QuatToJSON(v.value())}};
    } else if (tid == value::TypeTraits<value::quatd>::type_id()) {
      auto v = val.get_value<value::quatd>(false);
      if (v) return {{"type", type_name}, {"value", QuatToJSON(v.value())}};
    }
    // Handle AssetPath
    else if (tid == value::TypeTraits<value::AssetPath>::type_id()) {
      auto v = val.get_value<value::AssetPath>(false);
      if (v) {
        return {{"type", type_name},
                {"value", {{"assetPath", v.value().GetAssetPath()},
                           {"resolvedPath", v.value().GetResolvedPath()}}}};
      }
    }
    // Handle dictionary
    else if (tid == value::TypeTraits<value::dict>::type_id()) {
      auto v = val.get_value<value::dict>(false);
      if (v) {
        nlohmann::json dict_obj = nlohmann::json::object();
        for (const auto &[k, v2] : v.value()) {
          dict_obj[k] = ValueToJSON(v2);
        }
        return {{"type", type_name}, {"value", dict_obj}};
      }
    }
    // Handle timecode
    else if (tid == value::TypeTraits<value::timecode>::type_id()) {
      auto v = val.get_value<value::timecode>(false);
      if (v) return {{"type", type_name}, {"value", v.value().value}};
    }
    // Role types share underlying type_id, so we need to handle them by
    // underlying type. Use the same dispatch as the underlying type.
    // We catch remaining known types via type_name string match for role types.
    else {
      // Try role type dispatch by name
      std::string utype = val.underlying_type_name();
      if (utype == "float2") {
        nlohmann::json v;
        if (TryGetCompound<value::float2>(val, v))
          return {{"type", type_name}, {"value", v}};
      } else if (utype == "float3") {
        nlohmann::json v;
        if (TryGetCompound<value::float3>(val, v))
          return {{"type", type_name}, {"value", v}};
      } else if (utype == "float4") {
        nlohmann::json v;
        if (TryGetCompound<value::float4>(val, v))
          return {{"type", type_name}, {"value", v}};
      } else if (utype == "double2") {
        nlohmann::json v;
        if (TryGetCompound<value::double2>(val, v))
          return {{"type", type_name}, {"value", v}};
      } else if (utype == "double3") {
        nlohmann::json v;
        if (TryGetCompound<value::double3>(val, v))
          return {{"type", type_name}, {"value", v}};
      } else if (utype == "double4") {
        nlohmann::json v;
        if (TryGetCompound<value::double4>(val, v))
          return {{"type", type_name}, {"value", v}};
      } else if (utype == "half2") {
        nlohmann::json v;
        if (TryGetCompound<value::half2>(val, v))
          return {{"type", type_name}, {"value", v}};
      } else if (utype == "half3") {
        nlohmann::json v;
        if (TryGetCompound<value::half3>(val, v))
          return {{"type", type_name}, {"value", v}};
      } else if (utype == "half4") {
        nlohmann::json v;
        if (TryGetCompound<value::half4>(val, v))
          return {{"type", type_name}, {"value", v}};
      }
    }
  }

fallback:
  // Fallback: serialise to string representation
  {
    auto s = val.get_value<std::string>(true);
    if (s) {
      return {{"type", type_name}, {"value", s.value()}};
    }
  }
  return {{"type", type_name},
          {"value", std::string("(unserializable type)")}};
}

// ===========================================================================
// Public: ValueToPlainJSON
// ===========================================================================
nlohmann::json ValueToPlainJSON(const value::Value &val) {
  nlohmann::json wrapped = ValueToJSON(val);
  if (wrapped.contains("value")) {
    return wrapped["value"];
  }
  return nullptr;
}

// ===========================================================================
// Public: JSONToValue
// ===========================================================================
nonstd::optional<value::Value> JSONToValue(const nlohmann::json &j,
                                            std::string *err) {
  if (!j.is_object() || !j.contains("type")) {
    if (err) *err = "JSON value must be object with 'type' field";
    return nonstd::nullopt;
  }

  std::string type_name = j["type"].get<std::string>();
  nlohmann::json val_json = j.value("value", nlohmann::json());

  // Handle None
  if (type_name == "None") {
    value::Value v;
    v = value::ValueBlock();
    return v;
  }

  // Handle null
  if (type_name == "null") {
    return value::Value();
  }

  // Helper lambdas for constructing values
  auto make_scalar = [&](auto typed_val) -> nonstd::optional<value::Value> {
    return value::Value(typed_val);
  };

  // Check if array type
  bool is_array = false;
  std::string base_type = type_name;
  if (type_name.size() >= 3 &&
      type_name.compare(type_name.size() - 2, 2, "[]") == 0) {
    is_array = true;
    base_type = type_name.substr(0, type_name.size() - 2);
  }

  if (is_array) {
    if (!val_json.is_array()) {
      if (err) *err = "Expected array value for type " + type_name;
      return nonstd::nullopt;
    }

    // Dispatch on base type
    if (base_type == "float" || base_type == "half") {
      std::vector<float> vec;
      for (const auto &elem : val_json) {
        if (elem.is_number()) { vec.push_back(elem.get<float>()); }
        else if (err) { *err = "Expected number in float array"; return nonstd::nullopt; }
      }
      return value::Value(vec);
    } else if (base_type == "double") {
      std::vector<double> vec;
      for (const auto &elem : val_json) {
        if (elem.is_number()) vec.push_back(elem.get<double>());
        else if (err) { *err = "Expected number in double array"; return nonstd::nullopt; }
      }
      return value::Value(vec);
    } else if (base_type == "int") {
      std::vector<int32_t> vec;
      for (const auto &elem : val_json) {
        if (elem.is_number()) vec.push_back(elem.get<int32_t>());
        else if (err) { *err = "Expected number in int array"; return nonstd::nullopt; }
      }
      return value::Value(vec);
    } else if (base_type == "string" || base_type == "token") {
      std::vector<std::string> vec;
      for (const auto &elem : val_json) {
        if (elem.is_string()) vec.push_back(elem.get<std::string>());
        else if (err) { *err = "Expected string in string array"; return nonstd::nullopt; }
      }
      return value::Value(vec);
    } else if (base_type == "float2" || base_type == "half2" ||
               base_type == "texCoord2f" || base_type == "texCoord2d" ||
               base_type == "texCoord2h") {
      std::vector<value::float2> vec;
      if (val_json.is_array()) {
        for (const auto &elem : val_json) {
          if (elem.is_array() && elem.size() >= 2) {
            vec.push_back({static_cast<float>(elem[0].get<double>()),
                           static_cast<float>(elem[1].get<double>())});
          }
        }
      }
      return value::Value(vec);
    } else if (base_type == "float3" || base_type == "color3f" ||
               base_type == "normal3f" || base_type == "vector3f" ||
               base_type == "point3f" || base_type == "half3" ||
               base_type == "color3h" || base_type == "color3d" ||
               base_type == "double3" || base_type == "normal3d" ||
               base_type == "vector3d" || base_type == "point3d") {
      // Route to float3 or double3 based on base type
      if (base_type.find("double") != std::string::npos ||
          base_type == "color3d" || base_type == "normal3d" ||
          base_type == "vector3d" || base_type == "point3d" ||
          base_type == "texCoord3d") {
        std::vector<value::double3> vec;
        if (val_json.is_array()) {
          for (const auto &elem : val_json) {
            if (elem.is_array() && elem.size() >= 3) {
              vec.push_back({elem[0].get<double>(), elem[1].get<double>(),
                             elem[2].get<double>()});
            }
          }
        }
        return value::Value(vec);
      } else if (base_type.find("half") != std::string::npos) {
        // half arrays come as float in JSON
        std::vector<value::half3> vec;
        if (val_json.is_array()) {
          for (const auto &elem : val_json) {
            if (elem.is_array() && elem.size() >= 3) {
              vec.push_back(
                  {value::float_to_half_full(static_cast<float>(elem[0].get<double>())),
                   value::float_to_half_full(static_cast<float>(elem[1].get<double>())),
                   value::float_to_half_full(static_cast<float>(elem[2].get<double>()))});
            }
          }
        }
        return value::Value(vec);
      } else {
        std::vector<value::float3> vec;
        if (val_json.is_array()) {
          for (const auto &elem : val_json) {
            if (elem.is_array() && elem.size() >= 3) {
              vec.push_back({static_cast<float>(elem[0].get<double>()),
                             static_cast<float>(elem[1].get<double>()),
                             static_cast<float>(elem[2].get<double>())});
            }
          }
        }
        return value::Value(vec);
      }
    } else if (base_type == "float4" || base_type == "color4f" ||
               base_type == "color4h" || base_type == "color4d" ||
               base_type == "double4" || base_type == "half4" ||
               base_type == "quath" || base_type == "quatf" ||
               base_type == "quatd") {
      if (base_type.find("double") != std::string::npos ||
          base_type == "quatd") {
        std::vector<value::double4> vec;
        if (val_json.is_array()) {
          for (const auto &elem : val_json) {
            if (elem.is_array() && elem.size() >= 4) {
              vec.push_back({elem[0].get<double>(), elem[1].get<double>(),
                             elem[2].get<double>(), elem[3].get<double>()});
            }
          }
        }
        return value::Value(vec);
      } else if (base_type.find("half") != std::string::npos ||
                 base_type == "quath") {
        std::vector<value::half4> vec;
        if (val_json.is_array()) {
          for (const auto &elem : val_json) {
            if (elem.is_array() && elem.size() >= 4) {
              vec.push_back(
                  {value::float_to_half_full(static_cast<float>(elem[0].get<double>())),
                   value::float_to_half_full(static_cast<float>(elem[1].get<double>())),
                   value::float_to_half_full(static_cast<float>(elem[2].get<double>())),
                   value::float_to_half_full(static_cast<float>(elem[3].get<double>()))});
            }
          }
        }
        return value::Value(vec);
      } else {
        std::vector<value::float4> vec;
        if (val_json.is_array()) {
          for (const auto &elem : val_json) {
            if (elem.is_array() && elem.size() >= 4) {
              vec.push_back({static_cast<float>(elem[0].get<double>()),
                             static_cast<float>(elem[1].get<double>()),
                             static_cast<float>(elem[2].get<double>()),
                             static_cast<float>(elem[3].get<double>())});
            }
          }
        }
        return value::Value(vec);
      }
    } else if (base_type == "matrix2d") {
      std::vector<value::matrix2d> vec;
      if (val_json.is_array()) {
        for (const auto &mat : val_json) {
          if (mat.is_array() && mat.size() >= 2) {
            value::matrix2d m;
            for (size_t row = 0; row < 2; row++) {
              for (size_t col = 0; col < 2; col++) {
                m.m[row][col] =
                    mat[row].is_array() && mat[row].size() > col
                        ? mat[row][col].get<double>()
                        : 0.0;
              }
            }
            vec.push_back(m);
          }
        }
      }
      return value::Value(vec);
    } else if (base_type == "matrix3d") {
      std::vector<value::matrix3d> vec;
      if (val_json.is_array()) {
        for (const auto &mat : val_json) {
          if (mat.is_array() && mat.size() >= 3) {
            value::matrix3d m;
            for (size_t row = 0; row < 3; row++) {
              for (size_t col = 0; col < 3; col++) {
                m.m[row][col] =
                    mat[row].is_array() && mat[row].size() > col
                        ? mat[row][col].get<double>()
                        : 0.0;
              }
            }
            vec.push_back(m);
          }
        }
      }
      return value::Value(vec);
    } else if (base_type == "matrix4d" || base_type == "frame4d" ||
               base_type == "matrix4f" || base_type == "matrix3f" ||
               base_type == "matrix2f") {
      if (base_type.find("f") != std::string::npos && base_type != "frame4d") {
        // float matrices
        if (base_type == "matrix2f") {
          std::vector<value::matrix2f> vec;
          if (val_json.is_array()) {
            for (const auto &mat : val_json) {
              if (mat.is_array() && mat.size() >= 2) {
                value::matrix2f m;
                for (size_t row = 0; row < 2; row++) {
                  for (size_t col = 0; col < 2; col++) {
                    m.m[row][col] =
                        mat[row].is_array() && mat[row].size() > col
                            ? static_cast<float>(mat[row][col].get<double>())
                            : 0.0f;
                  }
                }
                vec.push_back(m);
              }
            }
          }
          return value::Value(vec);
        } else if (base_type == "matrix3f") {
          std::vector<value::matrix3f> vec;
          if (val_json.is_array()) {
            for (const auto &mat : val_json) {
              if (mat.is_array() && mat.size() >= 3) {
                value::matrix3f m;
                for (size_t row = 0; row < 3; row++) {
                  for (size_t col = 0; col < 3; col++) {
                    m.m[row][col] =
                        mat[row].is_array() && mat[row].size() > col
                            ? static_cast<float>(mat[row][col].get<double>())
                            : 0.0f;
                  }
                }
                vec.push_back(m);
              }
            }
          }
          return value::Value(vec);
        } else {
          std::vector<value::matrix4f> vec;
          if (val_json.is_array()) {
            for (const auto &mat : val_json) {
              if (mat.is_array() && mat.size() >= 4) {
                value::matrix4f m;
                for (size_t row = 0; row < 4; row++) {
                  for (size_t col = 0; col < 4; col++) {
                    m.m[row][col] =
                        mat[row].is_array() && mat[row].size() > col
                            ? static_cast<float>(mat[row][col].get<double>())
                            : 0.0f;
                  }
                }
                vec.push_back(m);
              }
            }
          }
          return value::Value(vec);
        }
      } else {
        std::vector<value::matrix4d> vec;
        if (val_json.is_array()) {
          for (const auto &mat : val_json) {
            if (mat.is_array() && mat.size() >= 4) {
              value::matrix4d m;
              for (size_t row = 0; row < 4; row++) {
                for (size_t col = 0; col < 4; col++) {
                  m.m[row][col] =
                      mat[row].is_array() && mat[row].size() > col
                          ? mat[row][col].get<double>()
                          : 0.0;
                }
              }
              vec.push_back(m);
            }
          }
        }
        return value::Value(vec);
      }
    }

    if (err) *err = "Unsupported array type: " + type_name;
    return nonstd::nullopt;
  }

  // Non-array: construct scalar / compound value
  if (base_type == "float" || base_type == "half") {
    if (!val_json.is_number()) {
      if (err) *err = "Expected number for " + base_type;
      return nonstd::nullopt;
    }
    return make_scalar(static_cast<float>(val_json.get<double>()));
  } else if (base_type == "double") {
    if (!val_json.is_number()) {
      if (err) *err = "Expected number for double";
      return nonstd::nullopt;
    }
    return make_scalar(val_json.get<double>());
  } else if (base_type == "int") {
    if (!val_json.is_number()) {
      if (err) *err = "Expected number for int";
      return nonstd::nullopt;
    }
    return make_scalar(val_json.get<int32_t>());
  } else if (base_type == "uint") {
    if (!val_json.is_number()) {
      if (err) *err = "Expected number for uint";
      return nonstd::nullopt;
    }
    return make_scalar(val_json.get<uint32_t>());
  } else if (base_type == "int64") {
    if (!val_json.is_number()) {
      if (err) *err = "Expected number for int64";
      return nonstd::nullopt;
    }
    return make_scalar(val_json.get<int64_t>());
  } else if (base_type == "uint64") {
    if (!val_json.is_number()) {
      if (err) *err = "Expected number for uint64";
      return nonstd::nullopt;
    }
    return make_scalar(val_json.get<uint64_t>());
  } else if (base_type == "bool") {
    if (!val_json.is_boolean()) {
      if (err) *err = "Expected boolean for bool";
      return nonstd::nullopt;
    }
    return make_scalar(val_json.get<bool>());
  } else if (base_type == "string" || base_type == "token") {
    if (!val_json.is_string()) {
      if (err) *err = "Expected string for " + base_type;
      return nonstd::nullopt;
    }
    if (base_type == "token") {
      return make_scalar(value::token(val_json.get<std::string>()));
    }
    return make_scalar(val_json.get<std::string>());
  } else if (base_type == "timecode") {
    if (!val_json.is_number()) {
      if (err) *err = "Expected number for timecode";
      return nonstd::nullopt;
    }
    return make_scalar(value::timecode{val_json.get<double>()});
  } else if (base_type == "asset") {
    if (!val_json.is_object()) {
      if (err) *err = "Expected object for asset";
      return nonstd::nullopt;
    }
    return make_scalar(
        value::AssetPath(val_json.value("assetPath", std::string())));
  }

  // Compound types
  if (!val_json.is_array()) {
    if (err) *err = "Expected array for " + base_type;
    return nonstd::nullopt;
  }

  // float2/3/4
  if (base_type == "float2" || base_type == "half2") {
    if (val_json.size() < 2) {
      if (err) *err = "Expected 2 elements for float2";
      return nonstd::nullopt;
    }
    return make_scalar(value::float2{
        static_cast<float>(val_json[0].get<double>()),
        static_cast<float>(val_json[1].get<double>())});
  } else if (base_type == "float3" || base_type == "color3f" ||
             base_type == "normal3f" || base_type == "vector3f" ||
             base_type == "point3f" || base_type == "texCoord3f") {
    if (val_json.size() < 3) {
      if (err) *err = "Expected 3 elements for float3";
      return nonstd::nullopt;
    }
    return make_scalar(value::float3{
        static_cast<float>(val_json[0].get<double>()),
        static_cast<float>(val_json[1].get<double>()),
        static_cast<float>(val_json[2].get<double>())});
  } else if (base_type == "float4" || base_type == "color4f") {
    if (val_json.size() < 4) {
      if (err) *err = "Expected 4 elements for float4";
      return nonstd::nullopt;
    }
    return make_scalar(value::float4{
        static_cast<float>(val_json[0].get<double>()),
        static_cast<float>(val_json[1].get<double>()),
        static_cast<float>(val_json[2].get<double>()),
        static_cast<float>(val_json[3].get<double>())});
  } else if (base_type == "double2") {
    if (val_json.size() < 2) {
      if (err) *err = "Expected 2 elements for double2";
      return nonstd::nullopt;
    }
    return make_scalar(
        value::double2{val_json[0].get<double>(), val_json[1].get<double>()});
  } else if (base_type == "double3" || base_type == "color3d" ||
             base_type == "normal3d" || base_type == "vector3d" ||
             base_type == "point3d" || base_type == "texCoord3d") {
    if (val_json.size() < 3) {
      if (err) *err = "Expected 3 elements for double3";
      return nonstd::nullopt;
    }
    return make_scalar(value::double3{val_json[0].get<double>(),
                                      val_json[1].get<double>(),
                                      val_json[2].get<double>()});
  } else if (base_type == "double4" || base_type == "color4d") {
    if (val_json.size() < 4) {
      if (err) *err = "Expected 4 elements for double4";
      return nonstd::nullopt;
    }
    return make_scalar(value::double4{val_json[0].get<double>(),
                                      val_json[1].get<double>(),
                                      val_json[2].get<double>(),
                                      val_json[3].get<double>()});
  } else if (base_type == "half3" || base_type == "color3h" ||
             base_type == "normal3h" || base_type == "vector3h" ||
             base_type == "point3h" || base_type == "texCoord3h") {
    if (val_json.size() < 3) return nonstd::nullopt;
    return make_scalar(value::half3{
        value::float_to_half_full(static_cast<float>(val_json[0].get<double>())),
        value::float_to_half_full(static_cast<float>(val_json[1].get<double>())),
        value::float_to_half_full(static_cast<float>(val_json[2].get<double>()))});
  } else if (base_type == "half4" || base_type == "color4h") {
    if (val_json.size() < 4) return nonstd::nullopt;
    return make_scalar(value::half4{
        value::float_to_half_full(static_cast<float>(val_json[0].get<double>())),
        value::float_to_half_full(static_cast<float>(val_json[1].get<double>())),
        value::float_to_half_full(static_cast<float>(val_json[2].get<double>())),
        value::float_to_half_full(static_cast<float>(val_json[3].get<double>()))});
  } else if (base_type == "int2") {
    if (val_json.size() < 2) return nonstd::nullopt;
    return make_scalar(value::int2{val_json[0].get<int32_t>(),
                                   val_json[1].get<int32_t>()});
  } else if (base_type == "int3") {
    if (val_json.size() < 3) return nonstd::nullopt;
    return make_scalar(value::int3{val_json[0].get<int32_t>(),
                                   val_json[1].get<int32_t>(),
                                   val_json[2].get<int32_t>()});
  } else if (base_type == "int4") {
    if (val_json.size() < 4) return nonstd::nullopt;
    return make_scalar(value::int4{val_json[0].get<int32_t>(),
                                   val_json[1].get<int32_t>(),
                                   val_json[2].get<int32_t>(),
                                   val_json[3].get<int32_t>()});
  } else if (base_type == "quath") {
    if (val_json.size() < 4) return nonstd::nullopt;
    return make_scalar(value::quath{{
        value::float_to_half_full(static_cast<float>(val_json[0].get<double>())),
        value::float_to_half_full(static_cast<float>(val_json[1].get<double>())),
        value::float_to_half_full(static_cast<float>(val_json[2].get<double>()))},
        value::float_to_half_full(static_cast<float>(val_json[3].get<double>()))});
  } else if (base_type == "quatf") {
    if (val_json.size() < 4) return nonstd::nullopt;
    return make_scalar(value::quatf{{
        static_cast<float>(val_json[0].get<double>()),
        static_cast<float>(val_json[1].get<double>()),
        static_cast<float>(val_json[2].get<double>())},
        static_cast<float>(val_json[3].get<double>())});
  } else if (base_type == "quatd") {
    if (val_json.size() < 4) return nonstd::nullopt;
    return make_scalar(value::quatd{{val_json[0].get<double>(),
                                    val_json[1].get<double>(),
                                    val_json[2].get<double>()},
                                    val_json[3].get<double>()});
  }

  // Matrices (2D array of arrays)
  if (base_type == "matrix2f" || base_type == "matrix2d") {
    if (val_json.size() < 2) return nonstd::nullopt;
    if (base_type == "matrix2f") {
      value::matrix2f m;
      for (size_t row = 0; row < 2; row++)
        for (size_t col = 0; col < 2; col++)
          m.m[row][col] = val_json[row].is_array() && val_json[row].size() > col
                          ? static_cast<float>(val_json[row][col].get<double>())
                          : 0.0f;
      return make_scalar(m);
    } else {
      value::matrix2d m;
      for (size_t row = 0; row < 2; row++)
        for (size_t col = 0; col < 2; col++)
          m.m[row][col] = val_json[row].is_array() && val_json[row].size() > col
                          ? val_json[row][col].get<double>()
                          : 0.0;
      return make_scalar(m);
    }
  } else if (base_type == "matrix3f" || base_type == "matrix3d") {
    if (val_json.size() < 3) return nonstd::nullopt;
    if (base_type == "matrix3f") {
      value::matrix3f m;
      for (size_t row = 0; row < 3; row++)
        for (size_t col = 0; col < 3; col++)
          m.m[row][col] = val_json[row].is_array() && val_json[row].size() > col
                          ? static_cast<float>(val_json[row][col].get<double>())
                          : 0.0f;
      return make_scalar(m);
    } else {
      value::matrix3d m;
      for (size_t row = 0; row < 3; row++)
        for (size_t col = 0; col < 3; col++)
          m.m[row][col] = val_json[row].is_array() && val_json[row].size() > col
                          ? val_json[row][col].get<double>()
                          : 0.0;
      return make_scalar(m);
    }
  } else if (base_type == "matrix4f" || base_type == "matrix4d" ||
             base_type == "frame4d") {
    if (val_json.size() < 4) return nonstd::nullopt;
    if (base_type == "matrix4f") {
      value::matrix4f m;
      for (size_t row = 0; row < 4; row++)
        for (size_t col = 0; col < 4; col++)
          m.m[row][col] = val_json[row].is_array() && val_json[row].size() > col
                          ? static_cast<float>(val_json[row][col].get<double>())
                          : 0.0f;
      return make_scalar(m);
    } else {
      value::matrix4d m;
      for (size_t row = 0; row < 4; row++)
        for (size_t col = 0; col < 4; col++)
          m.m[row][col] = val_json[row].is_array() && val_json[row].size() > col
                          ? val_json[row][col].get<double>()
                          : 0.0;
      return make_scalar(m);
    }
  } else if (base_type == "dictionary") {
    if (!val_json.is_object()) {
      if (err) *err = "Expected object for dictionary";
      return nonstd::nullopt;
    }
    value::dict d;
    for (auto it = val_json.begin(); it != val_json.end(); ++it) {
      auto sub = JSONToValue(it.value(), err);
      if (sub) {
        d.emplace(it.key(), sub->get_raw());
      }
    }
    return value::Value(std::move(d));
  }

  if (err) *err = "Unsupported type: " + type_name;
  return nonstd::nullopt;
}

// ===========================================================================
// PrimMetaToJSON
// ===========================================================================
nlohmann::json PrimMetaToJSON(const PrimMeta &meta) {
  nlohmann::json j;

  if (meta.has_active()) j["active"] = meta.get_active();
  if (meta.has_hidden()) j["hidden"] = meta.get_hidden();
  if (meta.has_instanceable()) j["instanceable"] = meta.get_instanceable();

  if (meta.has_kind()) j["kind"] = meta.get_kind();
  if (meta.has_doc()) j["doc"] = meta.get_doc().value;
  if (meta.has_comment()) j["comment"] = meta.get_comment().value;
  if (meta.has_displayName()) j["displayName"] = meta.get_displayName();
  if (meta.has_customData()) j["hasCustomData"] = true;
  if (meta.has_assetInfo()) j["hasAssetInfo"] = true;

  j["authored"] = meta.authored();

  // Composition arc summaries
  if (meta.references.has_value() && !meta.references.value().empty()) {
    size_t count = 0;
    for (const auto &p : meta.references.value()) count += p.second.size();
    j["referenceCount"] = count;
  }
  if (meta.payload.has_value() && !meta.payload.value().empty()) {
    size_t count = 0;
    for (const auto &p : meta.payload.value()) count += p.second.size();
    j["payloadCount"] = count;
  }
  if (meta.inherits.has_value() && !meta.inherits.value().empty()) {
    size_t count = 0;
    for (const auto &p : meta.inherits.value()) count += p.second.size();
    j["inheritCount"] = count;
  }
  if (meta.specializes.has_value() && !meta.specializes.value().empty()) {
    j["hasSpecializes"] = true;
  }

  j["unregisteredMetasCount"] = meta.unregisteredMetas.size();

  return j;
}

// ===========================================================================
// ValueTypeToJSONSchema
// ===========================================================================
nlohmann::json ValueTypeToJSONSchema(const std::string &type_name) {
  bool is_array = false;
  std::string base = type_name;
  if (type_name.size() >= 3 &&
      type_name.compare(type_name.size() - 2, 2, "[]") == 0) {
    is_array = true;
    base = type_name.substr(0, type_name.size() - 2);
  }

  nlohmann::json schema;
  schema["type"] = is_array ? "array" : "object";

  if (is_array) {
    nlohmann::json items;
    items["type"] = "object";
    auto sub = ValueTypeToJSONSchema(base);
    if (sub.contains("properties")) {
      items["properties"] = sub["properties"];
    }
    schema["items"] = items;
    return schema;
  }

  // Build properties for the wrapped format: { type: string, value: ... }
  nlohmann::json props;
  props["type"] = {{"type", "string"}, {"enum", nlohmann::json::array({type_name})}};

  // Determine value property schema
  if (base == "float" || base == "double" || base == "half" ||
      base == "int" || base == "uint" || base == "int64" || base == "uint64" ||
      base == "timecode") {
    props["value"] = {{"type", "number"}};
  } else if (base == "bool") {
    props["value"] = {{"type", "boolean"}};
  } else if (base == "string" || base == "token") {
    props["value"] = {{"type", "string"}};
  } else if (base == "asset") {
    props["value"] = {{"type", "object"},
                      {"properties",
                       {{"assetPath", {{"type", "string"}}},
                        {"resolvedPath", {{"type", "string"}}}}}};
  } else if (base == "dictionary") {
    props["value"] = {{"type", "object"}};
  } else if (base == "None" || base == "null") {
    // no value field needed
  } else {
    // Compound - array of numbers
    props["value"] = {{"type", "array"}, {"items", {{"type", "number"}}}};
  }

  schema["properties"] = props;
  schema["required"] = nlohmann::json::array({"type", "value"});

  return schema;
}

// ===========================================================================
// GetRoleTypeNames
// ===========================================================================
std::vector<std::string> GetRoleTypeNames() {
  return {
      "color3f",   "color3d",   "color3h",   "color4f",   "color4d",
      "color4h",   "normal3f",  "normal3d",  "normal3h",  "vector3f",
      "vector3d",  "vector3h",  "point3f",   "point3d",   "point3h",
      "texCoord2f","texCoord2d","texCoord2h","texCoord3f","texCoord3d",
      "texCoord3h","frame4d",
  };
}

// ===========================================================================
// GetPrimTypeNames
// ===========================================================================
std::vector<std::string> GetPrimTypeNames() {
  return {
      "Xform",       "Scope",      "Mesh",     "Sphere",
      "Cube",        "Cylinder",   "Cone",     "Capsule",
      "Material",    "Shader",     "Camera",   "GeomSubset",
      "Skeleton",    "Joint",      "BlendShape",
      "PointLight",  "DistantLight","SphereLight",
      "RectLight",   "DiskLight",  "CylinderLight",
      "DomeLight",   "GeometryLight",
      "RigidBody",   "CollisionGroup", "JointPhysics",
      "Volume",      "Field3D",    "VolumeAsset",
      "RenderVar",   "RenderSettings", "PluginLight",
      "PluginLightFilter",
  };
}

} // namespace tydra
} // namespace tinyusdz
