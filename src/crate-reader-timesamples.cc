// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-2022 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Crate(binary format) reader
//
//
// - [] Unify BuildDecompressedPathsImpl and BuildNodeHierarchy

#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "crate-reader.hh"

#ifdef __wasi__
#else
#include <thread>
#endif

#include <algorithm>
#include <stack>
#include <unordered_set>
#include <unordered_map>

#include "crate-format.hh"
#include "crate-pprint.hh"
#include "integerCoding.h"
#include "lz4-compression.hh"
#include "memory-budget.hh"
#include "parser-timing.hh"
#include "path-util.hh"
#include "core/prim-spec.hh"
#include "str-util.hh"
#include "stream-reader.hh"
#include "tiny-format.hh"
#include "tinyusdz.hh"
#include "value-pprint.hh"
#include "value-types.hh"

//
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "nonstd/expected.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

//

#include "common-macros.inc"

// Disable undefined-func-template warning for this split file
// Templates are defined in crate-reader.cc and will be linked
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-func-template"
#endif

namespace tinyusdz {
namespace crate {

// constexpr auto kTypeName = "typeName";
// constexpr auto kToken = "Token";
// constexpr auto kDefault = "default";

#define kTag "[Crate]"

#define CHECK_MEMORY_USAGE(__nbytes) \
  MEMORY_BUDGET_CHECK((*memory_manager_), (__nbytes), kTag)

// Extern template declaration - instantiated in crate-reader.cc
extern template bool CrateReader::ReadArray<unsigned char>(
    std::vector<unsigned char> *);

bool CrateReader::ReadTimeSamples(value::TimeSamples *d) {
  // Layout
  //
  // - `times`(double[])
  // - NumValueReps(int64)
  // - ArrayOfValueRep
  //
  // Note on deferred loading: Value loading is already deferred — this function
  // only reads the times array and ValueRep metadata.  Actual sample values are
  // unpacked later during prim reconstruction (ParseProperty → timeSamples
  // field handling).  Full OpenUSD-style lazy loading (deferring even the times
  // read) would require a callback-based TimeSamples container, which is not
  // yet implemented.

  DCOUT("ReadTimeSamples: offt before tell = " << _sr->tell());

  // 8byte for the offset for recursive value. See RecursiveRead() in
  // https://github.com/PixarAnimationStudios/USD/blob/release/pxr/usd/usd/crateFile.cpp
  // for details.
  int64_t offset{0};
  if (!_sr->read8(&offset)) {
    PUSH_ERROR_AND_RETURN_TAG(
        kTag, "Failed to read the offset for value in Dictionary.");
    return false;
  }

  DCOUT("TimeSample times value offset = " << offset);
  DCOUT("TimeSample tell = " << _sr->tell());

  // -8 to compensate sizeof(offset). Guard against int64 underflow.
  if (offset < (std::numeric_limits<int64_t>::min)() + 8) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "TimeSample times offset would underflow int64.");
  }
  if (!_sr->seek_from_current(offset - 8)) {
    PUSH_ERROR_AND_RETURN_TAG(
        kTag, "Failed to seek to TimeSample times. Invalid offset value: " +
                  std::to_string(offset));
  }

  crate::ValueRep times_rep{0};
  if (!ReadValueRep(&times_rep)) {
    PUSH_ERROR_AND_RETURN_TAG(
        kTag, "Failed to read ValueRep for TimeSample' `times` element.");
  }

  // Save offset
  auto values_offset = _sr->tell();

  // Validate that times ValueRep is double[] or DoubleVector
  {
    auto dtype = static_cast<crate::CrateDataTypeId>(times_rep.GetType());
    if (dtype != crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE &&
        dtype != crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE_VECTOR) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, fmt::format("TimeSamples `times` must be double[] or "
                            "DoubleVector, but got type {}",
                            GetCrateDataTypeName(times_rep.GetType())));
    }
    if (dtype == crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE &&
        !times_rep.IsArray() && times_rep.GetPayload() != 0) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "TimeSamples `times` must be an array, not a scalar double.");
    }
  }

  // Deduplicate times arrays: if another TimeSamples already read the same
  // file offset, share the parsed result instead of re-reading.
  std::vector<double> times;
  uint64_t times_payload = times_rep.GetPayload();
  auto cache_it = _shared_times_cache.find(times_payload);
  if (cache_it != _shared_times_cache.end()) {
    times = *(cache_it->second);
    DCOUT("TimeSamples times: reused cached array for payload offset "
          << times_payload << " (" << times.size() << " samples)");
  } else {
    if (!UnpackTimeSampleTimes(times_rep, times)) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Failed to unpack value of TimeSample's `times` element.");
    }
    _shared_times_cache[times_payload] =
        std::make_shared<std::vector<double>>(times);
  }
  DCOUT("MARK: timeSamples.times = " << times);

  //
  // Parse values(elements) of TimeSamples.
  //

  // seek position will be changed in `_UnpackValueRep`, so revert it.
  if (!_sr->seek_set(values_offset)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to seek to TimeSamples values.");
  }

  // 8byte for the offset for recursive value. See RecursiveRead() in
  // crateFile.cpp for details.
  if (!_sr->read8(&offset)) {
    PUSH_ERROR_AND_RETURN_TAG(
        kTag, "Failed to read the offset for value in TimeSamples.");
    return false;
  }

  DCOUT("TimeSample value offset = " << offset);
  DCOUT("TimeSample tell = " << _sr->tell());

  // -8 to compensate sizeof(offset). Guard against int64 underflow.
  if (offset < (std::numeric_limits<int64_t>::min)() + 8) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "TimeSample values offset would underflow int64.");
  }
  if (!_sr->seek_from_current(offset - 8)) {
    PUSH_ERROR_AND_RETURN_TAG(
        kTag, "Failed to seek to TimeSample values. Invalid offset value: " +
                  std::to_string(offset));
  }

  uint64_t num_values{0};
  if (!_sr->read8(&num_values)) {
    PUSH_ERROR_AND_RETURN_TAG(
        kTag, "Failed to read the number of values from TimeSamples.");
    return false;
  }

  DCOUT("Number of values = " << num_values);

  if (times.size() != num_values) {
    PUSH_ERROR_AND_RETURN_TAG(
        kTag, "# of `times` elements and # of values in Crate differs.");
  }

  if (num_values == 0) {
    return true;
  }

  // Check if num_values fits in size_t (for 32-bit builds)
  // On 64-bit systems uint64_t and size_t are the same size, so skip check
#if SIZE_MAX < UINT64_MAX
  if (num_values > (std::numeric_limits<size_t>::max)()) {
    PUSH_ERROR_AND_RETURN_TAG(
        kTag, "Number of values exceeds maximum size_t limit.");
    return false;
  }
#endif

  // Read all ValueReps first
  std::vector<crate::ValueRep> value_reps(static_cast<size_t>(num_values));
  for (size_t i = 0; i < num_values; i++) {
    if (!ReadValueRep(&value_reps[i])) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Failed to read ValueRep for TimeSample' value element.");
    }
  }

  // Type consistency check is handled during unpacking.
  // This avoids an additional pre-scan pass here.

  // Pre-allocate for the known number of samples to avoid repeated reallocation
  d->reserve(times.size());

  if (!UnpackValueRepsToTimeSamples(times, value_reps, d)) {
    // Unpack failed — clear any partial data and fall back to empty TimeSamples.
    // This preserves backward compatibility with USDC files whose TimeSamples
    // encoding doesn't match the expected layout (e.g., files written by
    // TinyUSDZ's crate writer which uses a slightly different format).
    d->clear();
    _err.clear();
  }

  // Move to next location.
  // sizeof(uint64) = sizeof(ValueRep)
  _sr->seek_set(values_offset);
  if (!_sr->seek_from_current(int64_t(sizeof(uint64_t) * num_values))) {
    PUSH_ERROR_AND_RETURN_TAG(kTag,
                              "Failed to seek over TimeSamples's values.");
  }

  // Clean up dedup entries for this specific TimeSamples now that loading is complete
  // This prevents accumulation of stale entries during long parsing sessions
  clear_timesamples_dedup_entries(static_cast<void*>(d));

  return true;
}

bool CrateReader::UnpackTimeSampleTimes(const crate::ValueRep &rep,
                                        std::vector<double> &dst) {
  uint64_t offset = rep.GetPayload();
  if (!_sr->seek_set(offset)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid offset.");
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE) {
    if (!rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "`times` must be array value.");
    }

    std::vector<double> v;
    if (rep.GetPayload() == 0) {
      dst.clear();
      // may ok
      return true;
    }

    if (!ReadDoubleArray(rep.IsCompressed(), &v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read double array.");
    }

    dst = std::move(v);

  } else if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
             crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE_VECTOR) {
    std::vector<double> v;
    if (rep.GetPayload() == 0) {
      dst.clear();
      // may ok
      return true;
    }

    if (!ReadDoubleVector(&v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read double vector.");
    }

    dst = std::move(v);
  } else {
    PUSH_ERROR_AND_RETURN_TAG(
        kTag,
        fmt::format("Invalid ValueRep type in TimeSamples times. expected type "
                    "double[] or DoubleVector but got type {}",
                    GetCrateDataTypeName(rep.GetType())));
  }

  return true;
}

template <typename T>
bool add_sample_to_timesamples(value::TimeSamples *d, double time, const T &val,
                          std::string *err, size_t expected_total_samples = 0) {
  return d->add_sample<T>(time, val, err, expected_total_samples);
}

// Dedup removed — crate reader stores full data; memory impact is negligible.
// These stubs are kept for backward compatibility with any external callers.
void clear_timesamples_dedup_entries(void* /*timesamples_ptr*/) {}
void clear_all_timesamples_dedup_entries() {}

template <typename T>
bool
add_array_sample_to_timesamples(value::TimeSamples *d, double time,
                                const std::vector<T> &arrval, std::string *err,
                                size_t expected_total_samples = 0,
                                const crate::ValueRep * /*vrep*/ = nullptr) {
  (void)expected_total_samples;
  if constexpr (std::is_same<T, bool>::value) {
    return d->add_sample(time, value::Value(arrval), err);
  } else if constexpr (value::uses_binary_timesample_array_storage_v<T>) {
    return d->add_array_sample<T>(time, arrval, err, expected_total_samples);
  } else {
    return d->add_sample(time, value::Value(arrval), err);
  }
}

template <typename T>
bool
add_array_sample_to_timesamples(value::TimeSamples *d, double time,
                                const TypedArray<T> &arrval, std::string *err,
                                size_t expected_total_samples = 0,
                                const crate::ValueRep * /*vrep*/ = nullptr) {
  (void)expected_total_samples;
  if constexpr (value::uses_binary_timesample_array_storage_v<T>) {
    return d->add_array_sample<T>(time, arrval, err, expected_total_samples);
  } else {
    std::vector<T> vec(arrval.data(), arrval.data() + arrval.size());
    return d->add_sample(time, value::Value(vec), err);
  }
}

template <typename T>
bool add_blocked_sample_to_timesamples(value::TimeSamples *d, double time,
                                  std::string *err,
                                  size_t expected_total_samples = 0) {
  return d->add_blocked_sample<T>(time, err, expected_total_samples);
}

bool CrateReader::UnpackTimeSampleValue_BOOL(double t,
                                             const crate::ValueRep &rep,
                                             value::TimeSamples &dst,
                                             size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // Blocked value
    // VALUE_BLOCK can have any flags, just skip the flag check
    // Just add a blocked sample - use bool type for bool timesamples
    if (!add_blocked_sample_to_timesamples<bool>(&dst, t, &_err,
                                                    expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  // just in case
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      // Compressed or array types are not inlined
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }
    // Scalar deduplication was removed - see FLOAT3 fix
      // Decode and cache
      uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
      bool val = data ? true : false;

    if (!add_sample_to_timesamples<bool>(&dst, t, val, &_err,
                                         expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else if (rep.IsArray()) {
    // bool array is encoded as uint8 array in the file format.
    std::vector<uint8_t> v_uint8;
    if (rep.GetPayload() == 0) {  // empty array
      std::vector<bool> v_bool;  // empty bool array
      if (!add_array_sample_to_timesamples<bool>(&dst, t, v_bool, &_err,
                                                 expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    if (!ReadArray(&v_uint8)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read bool array.");
    }

    std::vector<bool> v_bool;
    v_bool.reserve(v_uint8.size());
    for (uint8_t val : v_uint8) {
      v_bool.push_back(val != 0);
    }

    if (!add_array_sample_to_timesamples<bool>(&dst, t, v_bool, &_err,
                                               expected_total_samples, &rep)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }

  } else {
    // Non-array value is not supported

    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for boolean is invalid.");
  }

  return true;
}

// Macro for simple scalar/array types with memcpy-style inline decode.
// FUNC_SUFFIX: function name suffix (e.g. INT32)
// CPP_TYPE: C++ type (e.g. int32_t)
// CRATE_TYPE: crate data type enum suffix (e.g. CRATE_DATA_TYPE_INT)
// INLINE_DECODE: code block that declares `CPP_TYPE val` from `uint32_t data`
// READ_ARRAY_CALL: expression to read the array into `v`
// ARRAY_TYPE: container type for array read (TypedArray<CPP_TYPE> or
//             std::vector<CPP_TYPE>)
// HAS_SCALAR: 1 if there is a scalar (non-inlined, non-array) path, 0 if error
// CHECK_BLOCKED_FLAGS: 1 to check IsInlined/IsCompressed/IsArray on VALUE_BLOCK
// HAS_EMPTY_CHECK: 1 to check v.empty() after array read
#define DEFINE_UNPACK_SIMPLE_TIMESAMPLES(FUNC_SUFFIX, CPP_TYPE, CRATE_TYPE,    \
    INLINE_DECODE, READ_ARRAY_CALL, ARRAY_TYPE,                                \
    HAS_SCALAR, CHECK_BLOCKED_FLAGS, HAS_EMPTY_CHECK)                          \
bool CrateReader::UnpackTimeSampleValue_##FUNC_SUFFIX(                         \
    double t, const crate::ValueRep &rep, value::TimeSamples &dst,             \
    size_t expected_total_samples) {                                           \
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==                    \
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {                   \
    if (CHECK_BLOCKED_FLAGS &&                                                 \
        (rep.IsInlined() || rep.IsCompressed() || rep.IsArray())) {            \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
                                "Invalid blocked ValueRep in TimeSamples.");    \
    }                                                                          \
    if (!add_blocked_sample_to_timesamples<CPP_TYPE>(&dst, t, &_err,           \
                                                  expected_total_samples)) {    \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
                                "Failed to add blocked sample to TimeSamples.");\
    }                                                                          \
    return true;                                                               \
  }                                                                            \
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=                    \
      crate::CrateDataTypeId::CRATE_TYPE) {                                    \
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");  \
  }                                                                            \
  if (rep.IsInlined()) {                                                       \
    if (rep.IsCompressed() || rep.IsArray()) {                                 \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
                                "Invalid inlined ValueRep in TimeSamples.");    \
    }                                                                          \
    uint32_t data =                                                            \
        (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));           \
    INLINE_DECODE                                                              \
    if (!add_sample_to_timesamples<CPP_TYPE>(&dst, t, val, &_err,              \
                                              expected_total_samples)) {        \
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples."); \
    }                                                                          \
  } else if (rep.IsArray()) {                                                  \
    ARRAY_TYPE v;                                                              \
    if (rep.GetPayload() == 0) {                                               \
      if (!add_array_sample_to_timesamples<CPP_TYPE>(&dst, t, v, &_err,        \
                                                  expected_total_samples)) {    \
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");\
      }                                                                        \
      return true;                                                             \
    }                                                                          \
    if (!(READ_ARRAY_CALL)) {                                                  \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
          "Failed to read " #FUNC_SUFFIX " array.");                           \
    }                                                                          \
    if (HAS_EMPTY_CHECK && v.empty()) {                                        \
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Empty " #FUNC_SUFFIX " array.");        \
    }                                                                          \
    if (!add_array_sample_to_timesamples<CPP_TYPE>(                            \
            &dst, t, v, &_err, expected_total_samples, &rep)) {                \
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples."); \
    }                                                                          \
  } else if (HAS_SCALAR) {                                                     \
    if (rep.IsCompressed()) {                                                  \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
          "Compressed " #FUNC_SUFFIX " not supported for TimeSamples.");        \
    }                                                                          \
    CPP_TYPE v;                                                                \
    if (!ReadTimeSampleScalarValue(&v, sizeof(CPP_TYPE),                       \
                                   "Failed to read " #FUNC_SUFFIX)) {          \
      return false;                                                            \
    }                                                                          \
    if (!add_sample_to_timesamples<CPP_TYPE>(&dst, t, v, &_err,                \
                                              expected_total_samples)) {        \
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples."); \
    }                                                                          \
  } else {                                                                     \
    PUSH_ERROR_AND_RETURN_TAG(kTag,                                            \
        "Non-array value for " #FUNC_SUFFIX " is invalid.");                   \
  }                                                                            \
  return true;                                                                 \
}

// Inline decode helpers for DEFINE_UNPACK_SIMPLE_TIMESAMPLES
#define INLINE_MEMCPY_DECODE(CPP_TYPE)                                         \
    CPP_TYPE val; memcpy(&val, &data, sizeof(CPP_TYPE));
#define INLINE_UINT32_DECODE                                                   \
    uint32_t val = data;
#define INLINE_DOUBLE_FROM_FLOAT_DECODE                                        \
    float _f; memcpy(&_f, &data, sizeof(float));                               \
    double val = static_cast<double>(_f);

DEFINE_UNPACK_SIMPLE_TIMESAMPLES(INT32, int32_t, CRATE_DATA_TYPE_INT,
    INLINE_MEMCPY_DECODE(int32_t),
    ReadIntArrayTyped(rep.IsCompressed(), &v),
    TypedArray<int32_t>, 0, 0, 1)

DEFINE_UNPACK_SIMPLE_TIMESAMPLES(HALF, value::half, CRATE_DATA_TYPE_HALF,
    INLINE_MEMCPY_DECODE(value::half),
    ReadHalfArray(rep.IsCompressed(), &v),
    std::vector<value::half>, 0, 0, 1)

DEFINE_UNPACK_SIMPLE_TIMESAMPLES(FLOAT, float, CRATE_DATA_TYPE_FLOAT,
    INLINE_MEMCPY_DECODE(float),
    ReadFloatArrayTyped(rep.IsCompressed(), &v),
    TypedArray<float>, 1, 0, 0)

// FLOAT2 uses the same int8-component inline pattern as the vector macro but
// needs ReadFloat2ArrayTyped for the array path, so we use the simple macro
// with a custom inline decode.
#define INLINE_FLOAT2_DECODE                                                   \
    int8_t vdata[2]; memcpy(&vdata, &data, 2);                                \
    value::float2 val; val[0] = float(vdata[0]); val[1] = float(vdata[1]);

DEFINE_UNPACK_SIMPLE_TIMESAMPLES(FLOAT2, value::float2, CRATE_DATA_TYPE_VEC2F,
    INLINE_FLOAT2_DECODE,
    ReadFloat2ArrayTyped(&v),
    TypedArray<value::float2>, 1, 0, 0)

bool CrateReader::UnpackTimeSampleValue_QUATF(double t,
                                              const crate::ValueRep &rep,
                                              value::TimeSamples &dst,
                                              size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // Blocked value - just add a blocked sample
    if (!add_blocked_sample_to_timesamples<value::quatf>(&dst, t, &_err,
                                                  expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  // just in case
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATF) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  DCOUT("rep " << to_string(rep));

  if (rep.IsInlined()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Inlined quatf is not allowed.");
  } else if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed quatf not supported for TimeSamples.");
    }

    std::vector<value::quatf> v;
    if (rep.GetPayload() == 0) {  // empty array
      if (!add_array_sample_to_timesamples<value::quatf>(
              &dst, t, v, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    if (!ReadArray(&v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read quatf array.");
    }

    DCOUT("timeSamples.QUATF " << value::print_array_snipped(v));

    if (!add_array_sample_to_timesamples<value::quatf>(
            &dst, t, v, &_err, expected_total_samples, &rep)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }

  } else {
    // Scalar (non-inlined, non-array) quatf value
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Compressed quatf not supported for TimeSamples.");
    }

    value::quatf val;
    if (!ReadTimeSampleScalarValue(&val, sizeof(float) * 4,
                                   "Failed to read quatf value")) {
      return false;
    }
    DCOUT("quatf = [" << val[0] << ", " << val[1] << ", " << val[2] << ", " << val[3] << "]");
    if (!add_sample_to_timesamples<value::quatf>(&dst, t, val, &_err,
                                                 expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_ASSET_PATH(
    double t, const crate::ValueRep &rep, value::TimeSamples &dst,
    size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // Blocked value - just add a blocked sample
    if (!add_blocked_sample_to_timesamples<value::AssetPath>(
            &dst, t, &_err, expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_ASSET_PATH) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }
    // AssetPath is stored as TokenIndex for inlined value
    uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
    if (auto v = GetToken(crate::Index(data))) {
      std::string str = v.value().str();
      value::AssetPath asset_path(str);

      if (!add_sample_to_timesamples<value::AssetPath>(
              &dst, t, asset_path, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    } else {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Index for AssetPath.");
    }
  } else if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed AssetPath not supported for TimeSamples.");
    }

    std::vector<value::AssetPath> v;
    if (rep.GetPayload() == 0) {
      if (!add_sample_to_timesamples<std::vector<value::AssetPath>>(
              &dst, t, v, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }
    if (!ReadArray(&v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read AssetPath array.");
    }

    if (!add_sample_to_timesamples<std::vector<value::AssetPath>>(
            &dst, t, v, &_err, expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag,
                              "Non-array value for AssetPath is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_STRING(
    double t, const crate::ValueRep &rep, value::TimeSamples &dst,
    size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // Blocked value - just add a blocked sample
    if (!add_blocked_sample_to_timesamples<std::string>(
            &dst, t, &_err, expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }
    // String is stored as StringIndex (token) for inlined value
    uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
    if (auto v = GetStringToken(crate::Index(data))) {
      std::string str = v.value().str();

      if (!add_sample_to_timesamples<std::string>(
              &dst, t, str, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    } else {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Index for String.");
    }
  } else if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed String not supported for TimeSamples.");
    }

    std::vector<std::string> v;
    if (rep.GetPayload() == 0) {
      if (!add_sample_to_timesamples<std::vector<std::string>>(
              &dst, t, v, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }
    if (!ReadStringArray(&v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read String array.");
    }

    if (!add_sample_to_timesamples<std::vector<std::string>>(
            &dst, t, v, &_err, expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else {
    // Scalar (non-inlined, non-array) string value
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Compressed string not supported for TimeSamples.");
    }

    // Scalar deduplication was removed - see FLOAT3 fix
      // Read and cache scalar value
      // String is stored as StringIndex in the stream
      uint32_t index_data;
      CHECK_MEMORY_USAGE(sizeof(uint32_t));
      if (!_sr->read(sizeof(uint32_t), sizeof(uint32_t),
                     reinterpret_cast<uint8_t *>(&index_data))) {
        PUSH_ERROR_AND_RETURN("Failed to read string index");
      }
      std::string v;
      if (auto str_val = GetStringToken(crate::Index(index_data))) {
        v = str_val.value().str();
        DCOUT("string = " << v);
      } else {
        PUSH_ERROR_AND_RETURN("Invalid string index in TimeSamples.");
      }
    if (!add_sample_to_timesamples<std::string>(&dst, t, v, &_err,
                                                 expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_TOKEN(
    double t, const crate::ValueRep &rep, value::TimeSamples &dst,
    size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // Blocked value - just add a blocked sample
    if (!add_blocked_sample_to_timesamples<value::token>(
            &dst, t, &_err, expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }
    // Token is stored as token index for inlined value
    uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
    if (auto v = GetToken(crate::Index(data))) {
      value::token tok = v.value();

      if (!add_sample_to_timesamples<value::token>(
              &dst, t, tok, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    } else {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Index for Token.");
    }
  } else if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed Token not supported for TimeSamples.");
    }

    std::vector<value::token> v;
    if (rep.GetPayload() == 0) {
      if (!add_sample_to_timesamples<std::vector<value::token>>(
              &dst, t, v, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Read token array (stored as token indices)
    std::vector<crate::Index> indices;
    if (!ReadIndices(&indices)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read Token index array.");
    }

    // Convert indices to tokens
    v.reserve(indices.size());
    for (const auto& idx : indices) {
      if (auto tokv = GetToken(idx)) {
        v.emplace_back(tokv.value());
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid token index in array.");
      }
    }

    if (!add_sample_to_timesamples<std::vector<value::token>>(
            &dst, t, v, &_err, expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else {
    // Scalar (non-inlined, non-array) token value
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Compressed token not supported for TimeSamples.");
    }

    // Scalar deduplication was removed - see FLOAT3 fix
      // Read and cache scalar value
      // Token is stored as token index in the stream
      uint32_t index_data;
      CHECK_MEMORY_USAGE(sizeof(uint32_t));
      if (!_sr->read(sizeof(uint32_t), sizeof(uint32_t),
                     reinterpret_cast<uint8_t *>(&index_data))) {
        PUSH_ERROR_AND_RETURN("Failed to read token index");
      }
      value::token v;
      if (auto tok_val = GetToken(crate::Index(index_data))) {
        v = tok_val.value();
        DCOUT("token = " << v.str());
      } else {
        PUSH_ERROR_AND_RETURN("Invalid token index in TimeSamples.");
      }
    if (!add_sample_to_timesamples<value::token>(&dst, t, v, &_err,
                                                 expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  return true;
}

// Macro for vector types using int8 inline decode + ReadArray + ReadTimeSampleScalarValue.
// COMP_TYPE is float or double (the component scalar type).
// NCOMP is the number of components (2, 3, or 4).
// INLINE_CONV(x) converts an int8_t inline value to the component type.
#define DEFINE_UNPACK_VECTOR_TIMESAMPLES(FUNC_SUFFIX, CPP_TYPE, CRATE_TYPE,    \
                                          INLINE_CONV, NCOMP)                  \
bool CrateReader::UnpackTimeSampleValue_##FUNC_SUFFIX(                         \
    double t, const crate::ValueRep &rep, value::TimeSamples &dst,             \
    size_t expected_total_samples) {                                           \
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==                    \
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {                   \
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {              \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
                                "Invalid blocked ValueRep in TimeSamples.");   \
    }                                                                          \
    if (!add_blocked_sample_to_timesamples<CPP_TYPE>(                          \
            &dst, t, &_err, expected_total_samples)) {                         \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
                                "Failed to add blocked sample.");              \
    }                                                                          \
    return true;                                                               \
  }                                                                            \
                                                                               \
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=                    \
      crate::CrateDataTypeId::CRATE_TYPE) {                                    \
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");  \
  }                                                                            \
                                                                               \
  if (rep.IsInlined()) {                                                       \
    if (rep.IsCompressed() || rep.IsArray()) {                                 \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
                                "Invalid inlined ValueRep in TimeSamples.");   \
    }                                                                          \
    CPP_TYPE val;                                                              \
    uint32_t data =                                                            \
        (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));           \
    int8_t vdata[NCOMP];                                                       \
    memcpy(&vdata, &data, NCOMP);                                              \
    for (size_t _i = 0; _i < size_t(NCOMP); ++_i) {                            \
      val[_i] = INLINE_CONV(vdata[_i]);                                        \
    }                                                                          \
    if (!add_sample_to_timesamples<CPP_TYPE>(&dst, t, val, &_err,              \
                                              expected_total_samples)) {       \
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample.");               \
    }                                                                          \
  } else if (rep.IsArray()) {                                                  \
    if (rep.IsCompressed()) {                                                  \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
          "Compressed " #FUNC_SUFFIX " not supported for TimeSamples.");       \
    }                                                                          \
    std::vector<CPP_TYPE> v;                                                   \
    if (rep.GetPayload() == 0) {                                               \
      if (!add_array_sample_to_timesamples<CPP_TYPE>(                          \
              &dst, t, v, &_err, expected_total_samples)) {                    \
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample.");             \
      }                                                                        \
      return true;                                                             \
    }                                                                          \
    if (!ReadArray(&v)) {                                                      \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
          "Failed to read " #FUNC_SUFFIX " array.");                           \
    }                                                                          \
    if (!add_array_sample_to_timesamples<CPP_TYPE>(                            \
            &dst, t, v, &_err, expected_total_samples, &rep)) {                \
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample.");               \
    }                                                                          \
  } else {                                                                     \
    if (rep.IsCompressed()) {                                                  \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
          "Compressed " #FUNC_SUFFIX " not supported for TimeSamples.");       \
    }                                                                          \
    CPP_TYPE v;                                                                \
    if (!ReadTimeSampleScalarValue(&v, sizeof(CPP_TYPE),                       \
                                   "Failed to read " #FUNC_SUFFIX)) {         \
      return false;                                                            \
    }                                                                          \
    if (!add_sample_to_timesamples<CPP_TYPE>(&dst, t, v, &_err,                \
                                              expected_total_samples)) {       \
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample.");               \
    }                                                                          \
  }                                                                            \
  return true;                                                                 \
}

#define INLINE_TO_FLOAT(x) static_cast<float>(x)
#define INLINE_TO_DOUBLE(x) static_cast<double>(x)
#define INLINE_TO_HALF(x) value::float_to_half_full(static_cast<float>(x))

DEFINE_UNPACK_VECTOR_TIMESAMPLES(HALF2, value::half2, CRATE_DATA_TYPE_VEC2H, INLINE_TO_HALF, 2)
DEFINE_UNPACK_VECTOR_TIMESAMPLES(HALF3, value::half3, CRATE_DATA_TYPE_VEC3H, INLINE_TO_HALF, 3)
DEFINE_UNPACK_VECTOR_TIMESAMPLES(HALF4, value::half4, CRATE_DATA_TYPE_VEC4H, INLINE_TO_HALF, 4)
DEFINE_UNPACK_VECTOR_TIMESAMPLES(FLOAT3, value::float3, CRATE_DATA_TYPE_VEC3F, INLINE_TO_FLOAT, 3)
DEFINE_UNPACK_VECTOR_TIMESAMPLES(FLOAT4, value::float4, CRATE_DATA_TYPE_VEC4F, INLINE_TO_FLOAT, 4)
DEFINE_UNPACK_VECTOR_TIMESAMPLES(DOUBLE2, value::double2, CRATE_DATA_TYPE_VEC2D, INLINE_TO_DOUBLE, 2)
DEFINE_UNPACK_VECTOR_TIMESAMPLES(DOUBLE3, value::double3, CRATE_DATA_TYPE_VEC3D, INLINE_TO_DOUBLE, 3)
DEFINE_UNPACK_VECTOR_TIMESAMPLES(DOUBLE4, value::double4, CRATE_DATA_TYPE_VEC4D, INLINE_TO_DOUBLE, 4)

// Macro for types that reject inlined values (quaternions).
// Same as DEFINE_UNPACK_VECTOR_TIMESAMPLES but rejects inline instead of decoding.
#define DEFINE_UNPACK_NOINLINE_TIMESAMPLES(FUNC_SUFFIX, CPP_TYPE, CRATE_TYPE,  \
                                            SCALAR_SIZE)                       \
bool CrateReader::UnpackTimeSampleValue_##FUNC_SUFFIX(                         \
    double t, const crate::ValueRep &rep, value::TimeSamples &dst,             \
    size_t expected_total_samples) {                                           \
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==                    \
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {                   \
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {              \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
                                "Invalid blocked ValueRep in TimeSamples.");   \
    }                                                                          \
    if (!add_blocked_sample_to_timesamples<CPP_TYPE>(                          \
            &dst, t, &_err, expected_total_samples)) {                         \
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample.");       \
    }                                                                          \
    return true;                                                               \
  }                                                                            \
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=                    \
      crate::CrateDataTypeId::CRATE_TYPE) {                                    \
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");  \
  }                                                                            \
  if (rep.IsInlined()) {                                                       \
    PUSH_ERROR_AND_RETURN_TAG(kTag,                                            \
        "Inlined " #FUNC_SUFFIX " is not allowed.");                           \
  } else if (rep.IsArray()) {                                                  \
    if (rep.IsCompressed()) {                                                  \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
          "Compressed " #FUNC_SUFFIX " not supported for TimeSamples.");       \
    }                                                                          \
    std::vector<CPP_TYPE> v;                                                   \
    if (rep.GetPayload() == 0) {                                               \
      if (!add_array_sample_to_timesamples<CPP_TYPE>(                          \
              &dst, t, v, &_err, expected_total_samples)) {                    \
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample.");             \
      }                                                                        \
      return true;                                                             \
    }                                                                          \
    if (!ReadArray(&v)) {                                                      \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
          "Failed to read " #FUNC_SUFFIX " array.");                           \
    }                                                                          \
    if (!add_array_sample_to_timesamples<CPP_TYPE>(                            \
            &dst, t, v, &_err, expected_total_samples, &rep)) {                \
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample.");               \
    }                                                                          \
  } else {                                                                     \
    if (rep.IsCompressed()) {                                                  \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
          "Compressed " #FUNC_SUFFIX " not supported for TimeSamples.");       \
    }                                                                          \
    CPP_TYPE val;                                                              \
    if (!ReadTimeSampleScalarValue(&val, SCALAR_SIZE,                          \
                                   "Failed to read " #FUNC_SUFFIX)) {         \
      return false;                                                            \
    }                                                                          \
    if (!add_sample_to_timesamples<CPP_TYPE>(&dst, t, val, &_err,              \
                                              expected_total_samples)) {       \
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample.");               \
    }                                                                          \
  }                                                                            \
  return true;                                                                 \
}

DEFINE_UNPACK_NOINLINE_TIMESAMPLES(QUATH, value::quath, CRATE_DATA_TYPE_QUATH, sizeof(uint16_t) * 4)
DEFINE_UNPACK_NOINLINE_TIMESAMPLES(QUATD, value::quatd, CRATE_DATA_TYPE_QUATD, sizeof(double) * 4)

// QUATF rejects inlined values but has a unique blocked-check (no flag validation),
// so it's kept as a separate function.

// Macro for matrix types: diagonal inline decode, ReadArray, no scalar path.
#define DEFINE_UNPACK_MATRIX_TIMESAMPLES(FUNC_SUFFIX, CPP_TYPE, CRATE_TYPE,    \
                                          NDIAG)                               \
bool CrateReader::UnpackTimeSampleValue_##FUNC_SUFFIX(                         \
    double t, const crate::ValueRep &rep, value::TimeSamples &dst,             \
    size_t expected_total_samples) {                                           \
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==                    \
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {                   \
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {              \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
                                "Invalid blocked ValueRep in TimeSamples.");   \
    }                                                                          \
    if (!add_blocked_sample_to_timesamples<CPP_TYPE>(                          \
            &dst, t, &_err, expected_total_samples)) {                         \
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample.");       \
    }                                                                          \
    return true;                                                               \
  }                                                                            \
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=                    \
      crate::CrateDataTypeId::CRATE_TYPE) {                                    \
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");  \
  }                                                                            \
  if (rep.IsInlined()) {                                                       \
    if (rep.IsCompressed() || rep.IsArray()) {                                 \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
                                "Invalid inlined ValueRep in TimeSamples.");   \
    }                                                                          \
    uint32_t data =                                                            \
        (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));           \
    int8_t vdata[NDIAG];                                                       \
    memcpy(&vdata, &data, NDIAG);                                              \
    CPP_TYPE val;                                                              \
    memset(val.m, 0, sizeof(CPP_TYPE));                                        \
    for (size_t _i = 0; _i < size_t(NDIAG); ++_i) {                           \
      val.m[_i][_i] = static_cast<double>(vdata[_i]);                          \
    }                                                                          \
    if (!add_sample_to_timesamples<CPP_TYPE>(&dst, t, val, &_err,              \
                                              expected_total_samples)) {       \
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample.");               \
    }                                                                          \
  } else if (rep.IsArray()) {                                                  \
    if (rep.IsCompressed()) {                                                  \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
          "Compressed " #FUNC_SUFFIX " not supported for TimeSamples.");       \
    }                                                                          \
    std::vector<CPP_TYPE> v;                                                   \
    if (rep.GetPayload() != 0) {                                               \
      if (!ReadArray(&v)) {                                                    \
        PUSH_ERROR_AND_RETURN_TAG(kTag,                                        \
            "Failed to read " #FUNC_SUFFIX " array.");                         \
      }                                                                        \
    }                                                                          \
    const crate::ValueRep *vrep =                                              \
        (rep.GetPayload() != 0) ? &rep : nullptr;                              \
    if (!add_array_sample_to_timesamples<CPP_TYPE>(                            \
            &dst, t, v, &_err, expected_total_samples, vrep)) {                \
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample.");               \
    }                                                                          \
  } else {                                                                     \
    PUSH_ERROR_AND_RETURN_TAG(kTag,                                            \
        "Non-array value for " #FUNC_SUFFIX " is invalid.");                   \
  }                                                                            \
  return true;                                                                 \
}

DEFINE_UNPACK_MATRIX_TIMESAMPLES(MATRIX2D, value::matrix2d, CRATE_DATA_TYPE_MATRIX2D, 2)
DEFINE_UNPACK_MATRIX_TIMESAMPLES(MATRIX3D, value::matrix3d, CRATE_DATA_TYPE_MATRIX3D, 3)
DEFINE_UNPACK_MATRIX_TIMESAMPLES(MATRIX4D, value::matrix4d, CRATE_DATA_TYPE_MATRIX4D, 4)

// Remaining explicit functions (BOOL, QUATF, ASSET_PATH, STRING, TOKEN) have
// specialized logic and are kept as-is.

DEFINE_UNPACK_SIMPLE_TIMESAMPLES(UINT32, uint32_t, CRATE_DATA_TYPE_UINT,
    INLINE_UINT32_DECODE,
    ReadIntArrayTyped(rep.IsCompressed(), &v),
    TypedArray<uint32_t>, 0, 1, 0)

// Macro for 64-bit integer types: inline decode widens 32-bit to 64-bit.
// SMALL_TYPE is the 32-bit type used for memcpy from the payload.
#define DEFINE_UNPACK_INT64_TIMESAMPLES(FUNC_SUFFIX, CPP_TYPE, CRATE_TYPE,     \
                                          SMALL_TYPE)                           \
bool CrateReader::UnpackTimeSampleValue_##FUNC_SUFFIX(                         \
    double t, const crate::ValueRep &rep, value::TimeSamples &dst,             \
    size_t expected_total_samples) {                                           \
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==                    \
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {                   \
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {              \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
                                "Invalid blocked ValueRep in TimeSamples.");    \
    }                                                                          \
    if (!add_blocked_sample_to_timesamples<CPP_TYPE>(&dst, t, &_err,           \
                                                  expected_total_samples)) {    \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
                                "Failed to add blocked sample to TimeSamples.");\
    }                                                                          \
    return true;                                                               \
  }                                                                            \
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=                    \
      crate::CrateDataTypeId::CRATE_TYPE) {                                    \
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");  \
  }                                                                            \
  if (rep.IsInlined()) {                                                       \
    if (rep.IsCompressed() || rep.IsArray()) {                                 \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
                                "Invalid inlined ValueRep in TimeSamples.");    \
    }                                                                          \
    uint32_t data =                                                            \
        (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));           \
    SMALL_TYPE _val;                                                           \
    memcpy(&_val, &data, sizeof(SMALL_TYPE));                                  \
    CPP_TYPE val = static_cast<CPP_TYPE>(_val);                                \
    if (!add_sample_to_timesamples<CPP_TYPE>(&dst, t, val, &_err,              \
                                              expected_total_samples)) {        \
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples."); \
    }                                                                          \
  } else if (rep.IsArray()) {                                                  \
    TypedArray<CPP_TYPE> v;                                                    \
    if (rep.GetPayload() == 0) {                                               \
      if (!add_array_sample_to_timesamples<CPP_TYPE>(&dst, t, v, &_err,        \
                                                  expected_total_samples)) {    \
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");\
      }                                                                        \
      return true;                                                             \
    }                                                                          \
    if (!ReadIntArrayTyped(rep.IsCompressed(), &v)) {                          \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
          "Failed to read " #FUNC_SUFFIX " array.");                           \
    }                                                                          \
    if (!add_array_sample_to_timesamples<CPP_TYPE>(                            \
            &dst, t, v, &_err, expected_total_samples, &rep)) {                \
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples."); \
    }                                                                          \
  } else {                                                                     \
    if (rep.IsCompressed()) {                                                  \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                          \
          "Compressed " #FUNC_SUFFIX " not supported for TimeSamples.");        \
    }                                                                          \
    CPP_TYPE val;                                                              \
    if (!ReadTimeSampleScalarValue(&val, sizeof(CPP_TYPE),                     \
        "Failed to read " #FUNC_SUFFIX " value")) {                            \
      return false;                                                            \
    }                                                                          \
    if (!add_sample_to_timesamples<CPP_TYPE>(&dst, t, val, &_err,              \
                                              expected_total_samples)) {        \
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples."); \
    }                                                                          \
  }                                                                            \
  return true;                                                                 \
}

DEFINE_UNPACK_INT64_TIMESAMPLES(INT64, int64_t, CRATE_DATA_TYPE_INT64, int)
DEFINE_UNPACK_INT64_TIMESAMPLES(UINT64, uint64_t, CRATE_DATA_TYPE_UINT64, uint32_t)

DEFINE_UNPACK_SIMPLE_TIMESAMPLES(DOUBLE, double, CRATE_DATA_TYPE_DOUBLE,
    INLINE_DOUBLE_FROM_FLOAT_DECODE,
    ReadDoubleArrayTyped(rep.IsCompressed(), &v),
    TypedArray<double>, 1, 1, 1)

bool CrateReader::UnpackValueRepsToTimeSamples(
    const std::vector<double> &times,
    const std::vector<crate::ValueRep> &vreps,  // value_reps unused
    /* uint64_t vrep_start_offset, */
    value::TimeSamples *d) {
  if (times.size() != vreps.size()) {
    return false;
  }

  if (times.empty()) {
    return false;
  }

  // Find the first non-VALUE_BLOCK element to determine the actual type
  crate::CrateDataTypeId crate_type_id =
      static_cast<crate::CrateDataTypeId>(vreps[0].GetType());
  bool crate_is_array = vreps[0].IsArray();

  bool all_value_blocks = true;
  if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // First element is VALUE_BLOCK, find the first non-VALUE_BLOCK element
    for (size_t i = 1; i < vreps.size(); i++) {
      crate::CrateDataTypeId curr_type =
          static_cast<crate::CrateDataTypeId>(vreps[i].GetType());
      if (curr_type != crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
        crate_type_id = curr_type;
        crate_is_array = vreps[i].IsArray();
        all_value_blocks = false;
        break;
      }
    }
  } else {
    all_value_blocks = false;
  }

  // Special case: all elements are VALUE_BLOCK - use generic Value type
  if (all_value_blocks) {
    // Just add blocked samples without initializing a specific type
    for (size_t i = 0; i < vreps.size(); i++) {
      if (!d->add_blocked_sample(times[i], value::Value(), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "Failed to add blocked sample to TimeSamples.");
      }
    }
    return true;
  }

  DCOUT("UnpackValueRepsToTimeSamples");

#define HANDLE_INIT_TYPE_CASE(ctype, is_array, VTYPE)                         \
  case crate::CrateDataTypeId::ctype: {                                       \
    if (is_array) {                                                           \
      uint32_t tid = value::TypeTraits<std::vector<VTYPE>>::type_id();        \
      if (d->type_id() != 0 && d->type_id() != tid) {                        \
        PUSH_ERROR_AND_RETURN(fmt::format(                                    \
            "TimeSamples already initialized with different type. type_id = " \
            "{}[]({}[]) timeSamples.type_id = {}, crate_type = {}[]",         \
            tid, value::TypeTraits<std::vector<VTYPE>>::type_name(),          \
            d->type_id(), GetCrateDataTypeName(crate_type_id)));              \
      }                                                                       \
      d->set_type_id(tid);                                                    \
    } else {                                                                  \
      uint32_t tid = value::TypeTraits<VTYPE>::type_id();                     \
      if (d->type_id() != 0 && d->type_id() != tid) {                        \
        PUSH_ERROR_AND_RETURN(fmt::format(                                    \
            "TimeSamples already initialized with different type. type_id = " \
            "{}({}) timeSamples.type_id = {}, crate_type = {}",               \
            tid, value::TypeTraits<VTYPE>::type_name(), d->type_id(),         \
            GetCrateDataTypeName(crate_type_id)));                            \
      }                                                                       \
      d->set_type_id(tid);                                                    \
    }                                                                         \
    break;                                                                    \
  }

#define HANDLE_INIT_VECTOR_TYPE_CASE(ctype, VTYPE)                          \
  case crate::CrateDataTypeId::ctype: {                                     \
    uint32_t tid = value::TypeTraits<std::vector<VTYPE>>::type_id();        \
    if (d->type_id() != 0 && d->type_id() != tid) {                        \
      PUSH_ERROR_AND_RETURN(fmt::format(                                    \
          "TimeSamples already initialized with different type. type_id = " \
          "{}({}) timeSamples.type_id = {}, crate_type = {}",               \
          tid, value::TypeTraits<VTYPE>::type_name(), d->type_id(),         \
          GetCrateDataTypeName(crate_type_id)));                            \
    }                                                                       \
    d->set_type_id(tid);                                                    \
    break;                                                                  \
  }

  switch (crate_type_id) {
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_BOOL, crate_is_array, bool)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_UCHAR, crate_is_array, uint8_t)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_INT, crate_is_array, int32_t)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_UINT, crate_is_array, uint32_t)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_INT64, crate_is_array, int64_t)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_UINT64, crate_is_array, uint64_t)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_FLOAT, crate_is_array, float)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_DOUBLE, crate_is_array, double)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_HALF, crate_is_array, value::half)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_STRING, crate_is_array, std::string)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_TOKEN, crate_is_array, value::token)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_ASSET_PATH, crate_is_array,
                          value::AssetPath)

    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_MATRIX2D, crate_is_array,
                          value::matrix2d)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_MATRIX3D, crate_is_array,
                          value::matrix3d)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_MATRIX4D, crate_is_array,
                          value::matrix4d)

    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_QUATD, crate_is_array, value::quatd)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_QUATF, crate_is_array, value::quatf)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_QUATH, crate_is_array, value::quath)

    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC2D, crate_is_array, value::double2)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC2F, crate_is_array, value::float2)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC2H, crate_is_array, value::half2)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC2I, crate_is_array, value::int2)

    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC3D, crate_is_array, value::double3)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC3F, crate_is_array, value::float3)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC3H, crate_is_array, value::half3)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC3I, crate_is_array, value::int3)

    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC4D, crate_is_array, value::double4)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC4F, crate_is_array, value::float4)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC4H, crate_is_array, value::half4)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC4I, crate_is_array, value::int4)

    HANDLE_INIT_VECTOR_TYPE_CASE(CRATE_DATA_TYPE_DOUBLE_VECTOR, double)
    HANDLE_INIT_VECTOR_TYPE_CASE(CRATE_DATA_TYPE_STRING_VECTOR, std::string)
    HANDLE_INIT_VECTOR_TYPE_CASE(CRATE_DATA_TYPE_TOKEN_VECTOR, value::token)
    HANDLE_INIT_VECTOR_TYPE_CASE(CRATE_DATA_TYPE_PATH_VECTOR, Path)

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INVALID:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DICTIONARY:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_REFERENCE_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64_LIST_OP:

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_SPECIFIER:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PERMISSION:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIABILITY:

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIANT_SELECTION_MAP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_SAMPLES:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_LAYER_OFFSET_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_CODE:
    case crate::CrateDataTypeId::NumDataTypes:
      PUSH_ERROR_AND_RETURN(
          fmt::format("Unsupported or unimplemented type for TimeSamples. ty = "
                      "{}, is_array = {}",
                      GetCrateDataTypeName(crate_type_id), vreps[0].IsArray()));
  }

#undef HANDLE_INIT_TYPE_CASE
#undef HANDLE_INIT_VECTOR_TYPE_CASE

  // Pre-allocate on the first sample for better performance
  size_t expected_total_samples = times.size();

  using UnpackTimeSampleFn = bool (CrateReader::*)(
      double, const crate::ValueRep &, value::TimeSamples &, size_t);
  UnpackTimeSampleFn unpack_fn = nullptr;

#define UNPACK_CASE(ctype, suffix)                                    \
  case crate::CrateDataTypeId::ctype:                                 \
    unpack_fn = &CrateReader::UnpackTimeSampleValue_##suffix;         \
    break;

  switch (crate_type_id) {
    UNPACK_CASE(CRATE_DATA_TYPE_BOOL, BOOL)
    UNPACK_CASE(CRATE_DATA_TYPE_INT, INT32)
    UNPACK_CASE(CRATE_DATA_TYPE_UINT, UINT32)
    UNPACK_CASE(CRATE_DATA_TYPE_INT64, INT64)
    UNPACK_CASE(CRATE_DATA_TYPE_UINT64, UINT64)
    UNPACK_CASE(CRATE_DATA_TYPE_HALF, HALF)
    UNPACK_CASE(CRATE_DATA_TYPE_FLOAT, FLOAT)
    UNPACK_CASE(CRATE_DATA_TYPE_DOUBLE, DOUBLE)
    UNPACK_CASE(CRATE_DATA_TYPE_VEC2H, HALF2)
    UNPACK_CASE(CRATE_DATA_TYPE_VEC3H, HALF3)
    UNPACK_CASE(CRATE_DATA_TYPE_VEC4H, HALF4)
    UNPACK_CASE(CRATE_DATA_TYPE_VEC2F, FLOAT2)
    UNPACK_CASE(CRATE_DATA_TYPE_VEC3F, FLOAT3)
    UNPACK_CASE(CRATE_DATA_TYPE_VEC4F, FLOAT4)
    UNPACK_CASE(CRATE_DATA_TYPE_VEC2D, DOUBLE2)
    UNPACK_CASE(CRATE_DATA_TYPE_VEC3D, DOUBLE3)
    UNPACK_CASE(CRATE_DATA_TYPE_VEC4D, DOUBLE4)
    UNPACK_CASE(CRATE_DATA_TYPE_QUATF, QUATF)
    UNPACK_CASE(CRATE_DATA_TYPE_QUATH, QUATH)
    UNPACK_CASE(CRATE_DATA_TYPE_QUATD, QUATD)
    UNPACK_CASE(CRATE_DATA_TYPE_MATRIX2D, MATRIX2D)
    UNPACK_CASE(CRATE_DATA_TYPE_MATRIX3D, MATRIX3D)
    UNPACK_CASE(CRATE_DATA_TYPE_MATRIX4D, MATRIX4D)
    UNPACK_CASE(CRATE_DATA_TYPE_ASSET_PATH, ASSET_PATH)
    UNPACK_CASE(CRATE_DATA_TYPE_STRING, STRING)
    UNPACK_CASE(CRATE_DATA_TYPE_TOKEN, TOKEN)
    default:
      PUSH_ERROR_AND_RETURN(fmt::format("Unimplemented type in TimeSamples: {}",
                                        GetCrateDataTypeName(crate_type_id)));
  }
#undef UNPACK_CASE

  // Dedup map: ValueRep raw data -> first sample index in TimeSamples.
  // USDC files often deduplicate time sample values (multiple frames pointing
  // to the same file offset). Caching avoids redundant reads/decompression.
  std::unordered_map<uint64_t, size_t> dedup_map;
  dedup_map.reserve(vreps.size());

  for (size_t i = 0; i < vreps.size(); i++) {
    const crate::ValueRep &rep = vreps[i];
    const double curr_time = times[i];

    // Allow VALUE_BLOCK to mix with the actual type
    crate::CrateDataTypeId curr_type_id =
        static_cast<crate::CrateDataTypeId>(rep.GetType());
    if (curr_type_id != crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
      if (curr_type_id != crate_type_id || rep.IsArray() != crate_is_array) {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "Inconsistent ValueRep type in TimeSamples.");
      }
    }

    // Check dedup: if we've already unpacked an identical ValueRep, share data
    uint64_t rep_data = rep.GetData();
    auto dedup_it = dedup_map.find(rep_data);
    if (dedup_it != dedup_map.end()) {
      // Duplicate — reuse previously unpacked sample (zero-copy for binary storage)
      if (!d->duplicate_sample(dedup_it->second, curr_time)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "Failed to duplicate sample in TimeSamples.");
      }
      continue;
    }

    // First occurrence — record index and unpack from file
    dedup_map[rep_data] = i;

    if (!rep.IsInlined()) {
      _sr->seek_set(rep.GetPayload());
    }

    // Pass expected_total_samples only on the first sample (i == 0) for
    // pre-allocation
    size_t prealloc_hint = (i == 0) ? expected_total_samples : 0;

    if (!(this->*unpack_fn)(curr_time, rep, *d, prealloc_hint)) {
      return false;
    }
  }


  return true;
}


}  // namespace crate
}  // namespace tinyusdz

#ifdef __clang__
#pragma clang diagnostic pop
#endif
