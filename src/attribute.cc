// SPDX-License-Identifier: Apache 2.0

///
/// @file attribute.cc
/// @brief USD Attribute class implementation
///

#include "prim-types.hh"  // Must be included first for type definitions
#include "attribute.hh"

namespace tinyusdz {

///
/// Construct connection attribute.
///
Attribute::Attribute(const Path &v) {
  set_connection(v);
}

Attribute::Attribute(const std::vector<Path> &vs) {
  set_connections(vs);
}

std::string Attribute::type_name() const {
  if (_type_name.size()) {
    return _type_name;
  }

  if (!is_connection()) {
    // Fallback. May be unreliable(`var` could be empty).
    return _var.type_name();
  }

  return std::string();
}

uint32_t Attribute::type_id() const {
  if (_type_name.size()) {
    return value::GetTypeId(_type_name);
  }

  if (!is_connection()) {
    // Fallback. May be unreliable(`var` could be empty).
    return _var.type_id();
  }

  return value::TYPE_ID_INVALID;
}

bool Attribute::is_value() const {
  if (is_connection()) {
    return false;
  }

  if (is_timesamples()) {
    return false;
  }

  if (is_blocked()) {
    return false;
  }

  return true;
}

template <typename T>
bool Attribute::get(const double t, T *dst,
           value::TimeSampleInterpolationType tinterp) const {
  if (!dst) {
    return false;
  }

  if (value::TimeCode(t).is_default()) {
    if (has_value()) {
      nonstd::optional<T> v = _var.get_value<T>();
      if (v) {
        (*dst) = v.value();
        return true;
      }
    }
  }

  if (has_timesamples()) {
    return _var.get_interpolated_value(t, tinterp, dst);
  }

  // try to get 'defaut' value
  return get_value(dst);
}

template <typename T>
TypedArrayView<const T> Attribute::get_value_view(bool strict_cast) const {
  // Only support scalar (default) values, not timesamples
  if (has_timesamples()) {
    return TypedArrayView<const T>();  // Empty view for timesamples
  }
  
  if (is_blocked()) {
    return TypedArrayView<const T>();  // Empty view for blocked attributes
  }
  
  if (is_connection()) {
    return TypedArrayView<const T>();  // Empty view for connections
  }
  
  if (!has_value()) {
    return TypedArrayView<const T>();  // Empty view if no value
  }
  
  // Get the underlying value and create a view using Value::as_view()
  const primvar::PrimVar& pvar = get_var();
  const value::Value& val = pvar.value_raw();
  
  return val.as_view<T>(strict_cast);
}

template <typename T>
TypedArrayView<T> Attribute::get_value_view(bool strict_cast) {
  // Only support scalar (default) values, not timesamples
  if (has_timesamples()) {
    return TypedArrayView<T>();  // Empty view for timesamples
  }
  
  if (is_blocked()) {
    return TypedArrayView<T>();  // Empty view for blocked attributes
  }
  
  if (is_connection()) {
    return TypedArrayView<T>();  // Empty view for connections
  }
  
  if (!has_value()) {
    return TypedArrayView<T>();  // Empty view if no value
  }
  
  // Get the underlying value and create a view using Value::as_view()
  primvar::PrimVar& pvar = get_var();
  value::Value& val = pvar.value_raw();
  
  return val.as_view<T>(strict_cast);
}

const AttrMeta &Attribute::metas() const {
  static AttrMeta empty;
  return _metas ? *_metas : empty;
}

AttrMeta &Attribute::metas() {
  if (!_metas) {
    _metas = new AttrMetas();
  }
  return *_metas;
}

bool Attribute::is_blocked() const {
  if (has_timesamples()) {
    return false;
  }

  return _var.is_blocked(); 
}

// variability() and is_uniform() functions are now inlined in attribute.hh

bool Attribute::is_connection() const {
  if (has_timesamples()) {
    return false;
  }

  if (has_blocked()) {
    return false;
  }

  if (has_value()) {
    return false;
  }

  return _paths.size() > 0;
}

bool Attribute::is_timesamples() const {
  if (has_default()) {
    return false;
  }

  if (has_connections()) {
    return false;
  }

  return _var.is_timesamples();
}

void Attribute::set_connection(const Path &path) {
  _paths.clear();
  _paths.push_back(path);
}

void Attribute::set_connections(const std::vector<Path> &paths) { 
  _paths = paths; 
}

nonstd::optional<Path> Attribute::get_connection() const {
  if (_paths.size() == 1) {
    return _paths[0];
  }
  return nonstd::nullopt;
}

// Memory usage estimation implementation for Attribute
size_t Attribute::estimate_memory_usage() const {
  size_t total = sizeof(Attribute);
  
  // String storage
  total += _name.capacity();
  total += _type_name.capacity();
  
  // PrimVar memory - basic estimate
  // TODO: For more accurate estimation, PrimVar should have its own estimate_memory_usage method
  total += sizeof(primvar::PrimVar);
  // The PrimVar contains value::Value and value::TimeSamples which can be large
  // This is a basic estimate - actual size depends on the stored data type and time samples
  
  // Connection paths
  total += _paths.capacity() * sizeof(Path);
  for (const auto& path : _paths) {
    // Path internally contains strings, estimate their capacity
    total += path.full_path_name().capacity();
  }
  
  // Attribute metadata (if allocated)
  if (_metas) {
    total += sizeof(AttrMeta); // Basic size of metadata structure
    // TODO: Add detailed AttrMeta internal memory estimation if needed
  }
  
  return total;
}

// Explicit template instantiations for commonly used types
// This ensures that the template methods are available when linking
template bool Attribute::get<float>(const double, float*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<double>(const double, double*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<int>(const double, int*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::float2>(const double, value::float2*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::float3>(const double, value::float3*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::float4>(const double, value::float4*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::double2>(const double, value::double2*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::double3>(const double, value::double3*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::double4>(const double, value::double4*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::int2>(const double, value::int2*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::int3>(const double, value::int3*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::int4>(const double, value::int4*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::matrix2f>(const double, value::matrix2f*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::matrix3f>(const double, value::matrix3f*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::matrix4f>(const double, value::matrix4f*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::matrix2d>(const double, value::matrix2d*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::matrix3d>(const double, value::matrix3d*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::matrix4d>(const double, value::matrix4d*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::token>(const double, value::token*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::string>(const double, std::string*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<bool>(const double, bool*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<unsigned int>(const double, unsigned int*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::half>(const double, value::half*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::half2>(const double, value::half2*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::half3>(const double, value::half3*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::half4>(const double, value::half4*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::uint2>(const double, value::uint2*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::uint3>(const double, value::uint3*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::uint4>(const double, value::uint4*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::quath>(const double, value::quath*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::quatf>(const double, value::quatf*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::quatd>(const double, value::quatd*, value::TimeSampleInterpolationType) const;

// Additional template instantiations for USD geometry types
template bool Attribute::get<value::point3h>(const double, value::point3h*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::point3f>(const double, value::point3f*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::point3d>(const double, value::point3d*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::normal3h>(const double, value::normal3h*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::normal3f>(const double, value::normal3f*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::normal3d>(const double, value::normal3d*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::vector3h>(const double, value::vector3h*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::vector3f>(const double, value::vector3f*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::vector3d>(const double, value::vector3d*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::color3f>(const double, value::color3f*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::color3d>(const double, value::color3d*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::color4f>(const double, value::color4f*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::color4d>(const double, value::color4d*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::texcoord2h>(const double, value::texcoord2h*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::texcoord2f>(const double, value::texcoord2f*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::texcoord2d>(const double, value::texcoord2d*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::texcoord3h>(const double, value::texcoord3h*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::texcoord3f>(const double, value::texcoord3f*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<value::texcoord3d>(const double, value::texcoord3d*, value::TimeSampleInterpolationType) const;

// Vector template instantiations for arrays
template bool Attribute::get<std::vector<bool>>(const double, std::vector<bool>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<int>>(const double, std::vector<int>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<unsigned int>>(const double, std::vector<unsigned int>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<float>>(const double, std::vector<float>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<double>>(const double, std::vector<double>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::half>>(const double, std::vector<value::half>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<std::string>>(const double, std::vector<std::string>*, value::TimeSampleInterpolationType) const;

// Vector template instantiations for int arrays
template bool Attribute::get<std::vector<value::int2>>(const double, std::vector<value::int2>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::int3>>(const double, std::vector<value::int3>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::int4>>(const double, std::vector<value::int4>*, value::TimeSampleInterpolationType) const;

// Vector template instantiations for uint arrays
template bool Attribute::get<std::vector<value::uint2>>(const double, std::vector<value::uint2>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::uint3>>(const double, std::vector<value::uint3>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::uint4>>(const double, std::vector<value::uint4>*, value::TimeSampleInterpolationType) const;

// Vector template instantiations for half arrays  
template bool Attribute::get<std::vector<value::half2>>(const double, std::vector<value::half2>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::half3>>(const double, std::vector<value::half3>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::half4>>(const double, std::vector<value::half4>*, value::TimeSampleInterpolationType) const;

// Vector template instantiations for float arrays
template bool Attribute::get<std::vector<value::float2>>(const double, std::vector<value::float2>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::float3>>(const double, std::vector<value::float3>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::float4>>(const double, std::vector<value::float4>*, value::TimeSampleInterpolationType) const;

// Vector template instantiations for double arrays
template bool Attribute::get<std::vector<value::double2>>(const double, std::vector<value::double2>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::double3>>(const double, std::vector<value::double3>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::double4>>(const double, std::vector<value::double4>*, value::TimeSampleInterpolationType) const;

// Vector template instantiations for matrix types
template bool Attribute::get<std::vector<value::matrix2f>>(const double, std::vector<value::matrix2f>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::matrix3f>>(const double, std::vector<value::matrix3f>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::matrix4f>>(const double, std::vector<value::matrix4f>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::matrix2d>>(const double, std::vector<value::matrix2d>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::matrix3d>>(const double, std::vector<value::matrix3d>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::matrix4d>>(const double, std::vector<value::matrix4d>*, value::TimeSampleInterpolationType) const;

// Vector template instantiations for quaternion types
template bool Attribute::get<std::vector<value::quath>>(const double, std::vector<value::quath>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::quatf>>(const double, std::vector<value::quatf>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::quatd>>(const double, std::vector<value::quatd>*, value::TimeSampleInterpolationType) const;

// Vector template instantiations for point types
template bool Attribute::get<std::vector<value::point3h>>(const double, std::vector<value::point3h>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::point3f>>(const double, std::vector<value::point3f>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::point3d>>(const double, std::vector<value::point3d>*, value::TimeSampleInterpolationType) const;

// Vector template instantiations for normal types
template bool Attribute::get<std::vector<value::normal3h>>(const double, std::vector<value::normal3h>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::normal3f>>(const double, std::vector<value::normal3f>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::normal3d>>(const double, std::vector<value::normal3d>*, value::TimeSampleInterpolationType) const;

// Vector template instantiations for vector types
template bool Attribute::get<std::vector<value::vector3h>>(const double, std::vector<value::vector3h>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::vector3f>>(const double, std::vector<value::vector3f>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::vector3d>>(const double, std::vector<value::vector3d>*, value::TimeSampleInterpolationType) const;

// Vector template instantiations for color types
template bool Attribute::get<std::vector<value::color3f>>(const double, std::vector<value::color3f>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::color3d>>(const double, std::vector<value::color3d>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::color4f>>(const double, std::vector<value::color4f>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::color4d>>(const double, std::vector<value::color4d>*, value::TimeSampleInterpolationType) const;

// Vector template instantiations for texcoord types
template bool Attribute::get<std::vector<value::texcoord2h>>(const double, std::vector<value::texcoord2h>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::texcoord2f>>(const double, std::vector<value::texcoord2f>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::texcoord2d>>(const double, std::vector<value::texcoord2d>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::texcoord3h>>(const double, std::vector<value::texcoord3h>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::texcoord3f>>(const double, std::vector<value::texcoord3f>*, value::TimeSampleInterpolationType) const;
template bool Attribute::get<std::vector<value::texcoord3d>>(const double, std::vector<value::texcoord3d>*, value::TimeSampleInterpolationType) const;

// Template instantiations for get_value_view (const)
template TypedArrayView<const float> Attribute::get_value_view<float>(bool) const;
template TypedArrayView<const double> Attribute::get_value_view<double>(bool) const;
template TypedArrayView<const int> Attribute::get_value_view<int>(bool) const;
template TypedArrayView<const value::float2> Attribute::get_value_view<value::float2>(bool) const;
template TypedArrayView<const value::float3> Attribute::get_value_view<value::float3>(bool) const;
template TypedArrayView<const value::float4> Attribute::get_value_view<value::float4>(bool) const;
template TypedArrayView<const value::double2> Attribute::get_value_view<value::double2>(bool) const;
template TypedArrayView<const value::double3> Attribute::get_value_view<value::double3>(bool) const;
template TypedArrayView<const value::double4> Attribute::get_value_view<value::double4>(bool) const;
template TypedArrayView<const value::int2> Attribute::get_value_view<value::int2>(bool) const;
template TypedArrayView<const value::int3> Attribute::get_value_view<value::int3>(bool) const;
template TypedArrayView<const value::int4> Attribute::get_value_view<value::int4>(bool) const;

// Template instantiations for get_value_view (mutable)
template TypedArrayView<float> Attribute::get_value_view<float>(bool);
template TypedArrayView<double> Attribute::get_value_view<double>(bool);
template TypedArrayView<int> Attribute::get_value_view<int>(bool);
template TypedArrayView<value::float2> Attribute::get_value_view<value::float2>(bool);
template TypedArrayView<value::float3> Attribute::get_value_view<value::float3>(bool);
template TypedArrayView<value::float4> Attribute::get_value_view<value::float4>(bool);
template TypedArrayView<value::double2> Attribute::get_value_view<value::double2>(bool);
template TypedArrayView<value::double3> Attribute::get_value_view<value::double3>(bool);
template TypedArrayView<value::double4> Attribute::get_value_view<value::double4>(bool);
template TypedArrayView<value::int2> Attribute::get_value_view<value::int2>(bool);
template TypedArrayView<value::int3> Attribute::get_value_view<value::int3>(bool);
template TypedArrayView<value::int4> Attribute::get_value_view<value::int4>(bool);

}  // namespace tinyusdz