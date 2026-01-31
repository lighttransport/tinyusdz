// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
// TypedTimeSamples explicit template instantiations - Basic array types
// Split from timesamples-inst-array.cc for parallel compilation

#include "value-types.hh"
#include "timesamples.hh"
#include "value-eval-util.hh"  // For lerp functions

namespace tinyusdz {

// Include get() implementations (must be in tinyusdz namespace)
#include "timesamples-get-impl.inc"

// Basic numeric array types
template struct TypedTimeSamples<std::vector<bool>>;
template struct TypedTimeSamples<std::vector<int32_t>>;
template struct TypedTimeSamples<std::vector<uint32_t>>;
template struct TypedTimeSamples<std::vector<int64_t>>;
template struct TypedTimeSamples<std::vector<uint64_t>>;
template struct TypedTimeSamples<std::vector<value::half>>;
template struct TypedTimeSamples<std::vector<float>>;
template struct TypedTimeSamples<std::vector<double>>;

// Half vector arrays
template struct TypedTimeSamples<std::vector<value::half2>>;
template struct TypedTimeSamples<std::vector<value::half3>>;
template struct TypedTimeSamples<std::vector<value::half4>>;

// Float vector arrays
template struct TypedTimeSamples<std::vector<value::float2>>;
template struct TypedTimeSamples<std::vector<value::float3>>;
template struct TypedTimeSamples<std::vector<value::float4>>;

// Double vector arrays
template struct TypedTimeSamples<std::vector<value::double2>>;
template struct TypedTimeSamples<std::vector<value::double3>>;
template struct TypedTimeSamples<std::vector<value::double4>>;

// Int vector arrays
template struct TypedTimeSamples<std::vector<value::int2>>;
template struct TypedTimeSamples<std::vector<value::int3>>;
template struct TypedTimeSamples<std::vector<value::int4>>;

// Quaternion arrays
template struct TypedTimeSamples<std::vector<value::quath>>;
template struct TypedTimeSamples<std::vector<value::quatf>>;
template struct TypedTimeSamples<std::vector<value::quatd>>;

// ============================================================================
// Explicit template instantiations for TypedTimeSamples::get() - Basic arrays
// ============================================================================

// For vector container types (non-interpolatable)
template bool TypedTimeSamples<std::vector<bool>>::get<std::vector<bool>>(std::vector<bool>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<int32_t>>::get<std::vector<int32_t>>(std::vector<int32_t>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<uint32_t>>::get<std::vector<uint32_t>>(std::vector<uint32_t>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<int64_t>>::get<std::vector<int64_t>>(std::vector<int64_t>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<uint64_t>>::get<std::vector<uint64_t>>(std::vector<uint64_t>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::half>>::get<std::vector<value::half>>(std::vector<value::half>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<float>>::get<std::vector<float>>(std::vector<float>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<double>>::get<std::vector<double>>(std::vector<double>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::half2>>::get<std::vector<value::half2>>(std::vector<value::half2>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::half3>>::get<std::vector<value::half3>>(std::vector<value::half3>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::half4>>::get<std::vector<value::half4>>(std::vector<value::half4>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::float2>>::get<std::vector<value::float2>>(std::vector<value::float2>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::float3>>::get<std::vector<value::float3>>(std::vector<value::float3>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::float4>>::get<std::vector<value::float4>>(std::vector<value::float4>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::double2>>::get<std::vector<value::double2>>(std::vector<value::double2>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::double3>>::get<std::vector<value::double3>>(std::vector<value::double3>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::double4>>::get<std::vector<value::double4>>(std::vector<value::double4>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::int2>>::get<std::vector<value::int2>>(std::vector<value::int2>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::int3>>::get<std::vector<value::int3>>(std::vector<value::int3>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::int4>>::get<std::vector<value::int4>>(std::vector<value::int4>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::quath>>::get<std::vector<value::quath>>(std::vector<value::quath>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::quatf>>::get<std::vector<value::quatf>>(std::vector<value::quatf>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::quatd>>::get<std::vector<value::quatd>>(std::vector<value::quatd>*, double, value::TimeSampleInterpolationType) const;

}  // namespace tinyusdz
