// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-2022 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Crate(binary format) reader
//
//
// TODO:
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
#include "pprinter.hh"
#include "prim-types.hh"
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
  MEMORY_BUDGET_CHECK(memory_manager_, (__nbytes), kTag)

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

  // TODO(syoyo): Deferred loading of TimeSamples?(See USD's implementation for
  // details)

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

  // -8 to compensate sizeof(offset)
  if (!_sr->seek_from_current(offset - 8)) {
    PUSH_ERROR_AND_RETURN_TAG(
        kTag, "Failed to seek to TimeSample times. Invalid offset value: " +
                  std::to_string(offset));
  }

  // TODO(syoyo): Deduplicate times?

  crate::ValueRep times_rep{0};
  if (!ReadValueRep(&times_rep)) {
    PUSH_ERROR_AND_RETURN_TAG(
        kTag, "Failed to read ValueRep for TimeSample' `times` element.");
  }

  // Save offset
  auto values_offset = _sr->tell();

  // TODO: Enable Check if  type `double[]`
#if 0
  if (times_rep.GetType() == crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE_VECTOR) {
    // ok
  } else if ((times_rep.GetType() == crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBOLE) && times_rep.IsArray()) {
    // ok
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("`times` value must be type `double[]`, but got type `{}`", times_rep.GetTypeName()));
  }
#endif

#if 0
  crate::CrateValue times_value;
  if (!UnpackValueRep(times_rep, &times_value)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to unpack value of TimeSample's `times` element.");
  }

  // must be an array of double.
  DCOUT("TimeSample times:" << times_value.type_name());

  std::vector<double> times;
  if (auto pv = times_value.get_value<std::vector<double>>()) {
    times = pv.value();
    DCOUT("`times` = " << times);
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("`times` in TimeSamples must be type `double[]`, but got type `{}`", times_value.type_name()));
  }

#else
  // optimized version
  std::vector<double> times;
  if (!UnpackTimeSampleTimes(times_rep, times)) {
    PUSH_ERROR_AND_RETURN_TAG(
        kTag, "Failed to unpack value of TimeSample's `times` element.");
  }
  DCOUT("MARK: timeSamples.times = " << times);

#endif

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

  // -8 to compensate sizeof(offset)
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
  if (num_values > std::numeric_limits<size_t>::max()) {
    PUSH_ERROR_AND_RETURN_TAG(
        kTag, "Number of values exceeds maximum size_t limit.");
    return false;
  }
#endif

  // Read all ValueReps first
  auto vrep_start_offset = _sr->tell();
  std::vector<crate::ValueRep> value_reps(static_cast<size_t>(num_values));
  for (size_t i = 0; i < num_values; i++) {
    if (!ReadValueRep(&value_reps[i])) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Failed to read ValueRep for TimeSample' value element.");
    }
  }

  // Check if all samples have the same type (homogeneous)
  // Allow VALUE_BLOCK (None) to be mixed with other types
  // bool is_homogeneous = true;
  auto first_type = value_reps[0].GetType();
  bool first_is_array = value_reps[0].IsArray();
  for (size_t i = 1; i < num_values; i++) {
    auto curr_type = value_reps[i].GetType();
    bool curr_is_array = value_reps[i].IsArray();

    // Allow VALUE_BLOCK to mix with any type
    bool is_value_block_first =
        (static_cast<crate::CrateDataTypeId>(first_type) ==
         crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK);
    bool is_value_block_curr =
        (static_cast<crate::CrateDataTypeId>(curr_type) ==
         crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK);

    if (!is_value_block_first && !is_value_block_curr) {
      // Neither is VALUE_BLOCK, so they must match
      if (curr_type != first_type || curr_is_array != first_is_array) {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "Types in TimeSamples' ValueRep isn't the same.");
        // is_homogeneous = false;
      }
    }
  }

#if 0
  // Check if it's a common type that benefits from typed storage
  bool use_typed_path = false;
  if (is_homogeneous) {
    auto type_id = static_cast<crate::CrateDataTypeId>(first_type);
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif
    switch (type_id) {
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT:
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT:
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64:
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64:
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT:
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE:
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF:
        // POD scalar and array types - use typed/POD path
        use_typed_path = true;
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D:
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D:
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D:
        // POD matrix types
        use_typed_path = true;
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING:
        // Non-POD but use typed path for arrays
        use_typed_path = first_is_array;
        break;
      default:
        // All other types don't use typed storage
        use_typed_path = false;
        break;
    }
#ifdef __clang__
#pragma clang diagnostic pop
#endif
  }

  if (use_typed_path) {
    DCOUT("Using typed TimeSamples path for type: " << first_type);
    auto type_id = static_cast<crate::CrateDataTypeId>(first_type);
    bool success = false;

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif
    switch (type_id) {
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<int32_t>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<int32_t>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<uint32_t>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<uint32_t>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<int64_t>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<int64_t>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<uint64_t>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<uint64_t>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<float>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<float>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<double>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<double>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<value::half>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<value::half>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<std::string>>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<value::matrix2d>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<value::matrix2d>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<value::matrix3d>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<value::matrix3d>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<value::matrix4d>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<value::matrix4d>(times, value_reps, vrep_start_offset, d);
        }
        break;
      default:
        // Other types not handled by typed storage
        success = false;
        break;
    }
#ifdef __clang__
#pragma clang diagnostic pop
#endif

    if (success) {
      // Move to next location.
      // sizeof(uint64) = sizeof(ValueRep)
      _sr->seek_set(values_offset);
      if (!_sr->seek_from_current(int64_t(sizeof(uint64_t) * num_values))) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to seek over TimeSamples's values.");
      }
      return true;
    }

    // Fall back to standard path if typed path fails
    DCOUT("Typed path failed, falling back to standard path");
  }
#endif

  // Rewind to ValueReps start
  _sr->seek_set(vrep_start_offset);

#if 0
  for (size_t i = 0; i < num_values; i++) {
    crate::ValueRep rep;
    if (!ReadValueRep(&rep)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read ValueRep for TimeSample' value element.");
    }

    ///
    /// Type check of the content of `value` will be done at ReconstructPrim() in usdc-reader.cc.
    ///
    crate::CrateValue value;
    uint64_t value_offset = rep.GetPayload();
    if (!UnpackValueRepForTimeSamples(rep, value_offset, &value)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to unpack value of TimeSample's value element.");
    }

    d->add_sample(times[i], value.get_raw());
  }

#else
  if (!UnpackValueRepsToTimeSamples(times, value_reps, d)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to unpack TimeSamples's values.");
    return false;
  }
#endif

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

// Helper template to check if a type is POD (trivial and standard layout)
template <typename T>
struct is_pod_type
    : std::integral_constant<bool, std::is_trivial<T>::value &&
                                       std::is_standard_layout<T>::value> {};

// Helper to add sample - POD version
template <typename T>
typename std::enable_if<is_pod_type<T>::value, bool>::type
add_sample_to_timesamples(value::TimeSamples *d, double time, const T &val,
                          std::string *err, size_t expected_total_samples = 0) {
  // TUSDZ_LOG_I("pod_ty: " << value::TypeTraits<T>::type_name() << ",
  // is_use_pod " << d->is_using_pod());
  if (d->is_using_pod()) {
    return d->add_sample_pod<T>(time, val, err, expected_total_samples);
  } else {
    return d->add_sample(time, value::Value(val), err);
  }
}

// Helper to add sample - non-POD version
template <typename T>
typename std::enable_if<!is_pod_type<T>::value, bool>::type
add_sample_to_timesamples(value::TimeSamples *d, double time, const T &val,
                          std::string *err, size_t expected_total_samples = 0) {
  // TUSDZ_LOG_I("non pod_ty: " << value::TypeTraits<T>::type_name());
  (void)expected_total_samples;  // unused for non-POD
  return d->add_sample(time, value::Value(val), err);
}

#if 0
// TODO: Use pod path for array type.
template<typename T>
bool add_sample_to_timesamples(value::TimeSamples *d, double time, const std::vector<T>& val, std::string *err) {
  //TUSDZ_LOG_I("arr non pod_ty: " << value::TypeTraits<T>::type_name());
  return d->add_sample(time, value::Value(val), err);
}
#else

// Per-TimeSamples deduplication map for array offsets
// Maps (TimeSamples pointer, ValueRep payload) → first_sample_index
// Use ValueRep payload (uint64_t) as key since it uniquely identifies the array data in USDC
// When the same ValueRep payload appears multiple times in the same TimeSamples,
// the first occurrence is stored as original and subsequent ones are deduplicated
// Using function-static to avoid global constructor issues
// Suppress exit-time-destructor warning as this is the correct way to handle it
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#endif
static std::map<std::pair<void*, uint64_t>, size_t>& get_timesamples_dedup_map() {
  static std::map<std::pair<void*, uint64_t>, size_t> map;
  return map;
}

/// Clear all dedup entries for a specific TimeSamples pointer
/// Called when a TimeSamples finishes loading to prevent stale entries after object reallocation
void clear_timesamples_dedup_entries(void* timesamples_ptr) {
  auto& dedup_map = get_timesamples_dedup_map();

  // Find and erase all entries with this TimeSamples pointer
  auto it = dedup_map.begin();
  while (it != dedup_map.end()) {
    if (it->first.first == timesamples_ptr) {
      it = dedup_map.erase(it);
    } else {
      ++it;
    }
  }
}

/// Clear all dedup entries (called at start of each file load)
void clear_all_timesamples_dedup_entries() {
  auto& dedup_map = get_timesamples_dedup_map();
  dedup_map.clear();
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

template <typename T>
typename std::enable_if<is_pod_type<T>::value, bool>::type
add_array_sample_to_timesamples(value::TimeSamples *d, double time,
                                const std::vector<T> &arrval, std::string *err,
                                size_t expected_total_samples = 0,
                                const crate::ValueRep *vrep = nullptr) {
  // TUSDZ_LOG_I("arr pod_ty: " << value::TypeTraits<T>::type_name() << ",
  // is_use_pod " << d->is_using_pod());
  if (d->is_using_pod()) {
    // Check if this array valueRep has been seen before in this TimeSamples
    if (vrep) {
      auto key = std::make_pair(static_cast<void*>(d), vrep->GetPayload());
      auto& dedup_map = get_timesamples_dedup_map();
      auto it = dedup_map.find(key);
      if (it != dedup_map.end()) {
        // Deduplicated array - reuse offset from first occurrence
        size_t ref_index = it->second;
        DCOUT("Array dedup: reusing sample index " << ref_index);
        return d->add_dedup_array_sample_pod<T>(time, ref_index, err);
      } else {
        // First occurrence - store normally and remember the index
        size_t current_index = d->size();
        dedup_map[key] = current_index;
        DCOUT("Array dedup: storing new sample at index " << current_index);
        return d->add_array_sample_pod<T>(time, arrval, err,
                                          expected_total_samples);
      }
    } else {
      // No ValueRep provided - store normally without dedup tracking
      return d->add_array_sample_pod<T>(time, arrval, err,
                                        expected_total_samples);
    }
  } else {
    return d->add_sample(time, value::Value(arrval), err);
  }
}

template <typename T>
//typename std::enable_if<is_pod_type<T>::value, bool>::type
bool
add_array_sample_to_timesamples(value::TimeSamples *d, double time,
                                const TypedArray<T> &arrval, std::string *err,
                                size_t expected_total_samples = 0,
                                const crate::ValueRep *vrep = nullptr) {
  // Store actual array data inline in PODTimeSamples, not packed pointers.
  // The packed-pointer approach (add_typed_array_sample) has lifetime issues:
  // The TypedArrayImpl object may be destroyed before PODTimeSamples is printed,
  // leaving dangling pointers. Storing the data inline ensures it outlives PODTimeSamples.
  if (d->is_using_pod()) {
    // Check if this array valueRep has been seen before in this TimeSamples
    if (vrep) {
      auto key = std::make_pair(static_cast<void*>(d), vrep->GetPayload());
      auto& dedup_map = get_timesamples_dedup_map();
      auto it = dedup_map.find(key);
      if (it != dedup_map.end()) {
        // Deduplicated array - reuse offset from first occurrence
        size_t ref_index = it->second;
        DCOUT("Array dedup: reusing sample index " << ref_index);
        return d->add_dedup_array_sample_pod<T>(time, ref_index, err);
      } else {
        // First occurrence - store normally and remember the index
        size_t current_index = d->size();
        dedup_map[key] = current_index;
        DCOUT("Array dedup: storing new sample at index " << current_index);
        return d->add_array_sample_pod<T>(time, arrval, err,
                                          expected_total_samples);
      }
    } else {
      // No ValueRep provided - store normally without dedup tracking
      return d->add_array_sample_pod<T>(time, arrval, err,
                                        expected_total_samples);
    }
  } else {
    // Convert TypedArray to std::vector for non-POD path
    std::vector<T> vec(arrval.data(), arrval.data() + arrval.size());
    return d->add_sample(time, value::Value(vec), err);
  }
}

// Specialization for matrix(treat it as pod)
inline bool add_matrix2d_array_sample_to_timesamples(
    value::TimeSamples *d, double time,
    const std::vector<value::matrix2d> &arrval, std::string *err,
    size_t expected_total_samples = 0,
    const crate::ValueRep *vrep = nullptr) {
  if (d->is_using_pod()) {
    // Check if this array valueRep has been seen before
    if (vrep) {
      auto key = std::make_pair(static_cast<void*>(d), vrep->GetPayload());
      auto& dedup_map = get_timesamples_dedup_map();
      auto it = dedup_map.find(key);
      if (it != dedup_map.end()) {
        size_t ref_index = it->second;
        DCOUT("Matrix2d array dedup: reusing sample index " << ref_index);
        return d->add_dedup_matrix_array_sample_pod<value::matrix2d>(time, ref_index, err);
      } else {
        size_t current_index = d->size();
        dedup_map[key] = current_index;
        DCOUT("Matrix2d array dedup: storing new sample at index " << current_index);
        return d->add_matrix_array_sample_pod<value::matrix2d>(
            time, arrval, err, expected_total_samples);
      }
    } else {
      return d->add_matrix_array_sample_pod<value::matrix2d>(
          time, arrval, err, expected_total_samples);
    }
  } else {
    return d->add_sample(time, value::Value(arrval), err);
  }
}

inline bool add_matrix3d_array_sample_to_timesamples(
    value::TimeSamples *d, double time,
    const std::vector<value::matrix3d> &arrval, std::string *err,
    size_t expected_total_samples = 0,
    const crate::ValueRep *vrep = nullptr) {
  if (d->is_using_pod()) {
    // Check if this array valueRep has been seen before
    if (vrep) {
      auto key = std::make_pair(static_cast<void*>(d), vrep->GetPayload());
      auto& dedup_map = get_timesamples_dedup_map();
      auto it = dedup_map.find(key);
      if (it != dedup_map.end()) {
        size_t ref_index = it->second;
        DCOUT("Matrix3d array dedup: reusing sample index " << ref_index);
        return d->add_dedup_matrix_array_sample_pod<value::matrix3d>(time, ref_index, err);
      } else {
        size_t current_index = d->size();
        dedup_map[key] = current_index;
        DCOUT("Matrix3d array dedup: storing new sample at index " << current_index);
        return d->add_matrix_array_sample_pod<value::matrix3d>(
            time, arrval, err, expected_total_samples);
      }
    } else {
      return d->add_matrix_array_sample_pod<value::matrix3d>(
          time, arrval, err, expected_total_samples);
    }
  } else {
    return d->add_sample(time, value::Value(arrval), err);
  }
}

inline bool add_matrix4d_array_sample_to_timesamples(
    value::TimeSamples *d, double time,
    const std::vector<value::matrix4d> &arrval, std::string *err,
    size_t expected_total_samples = 0,
    const crate::ValueRep *vrep = nullptr) {
  if (d->is_using_pod()) {
    // Check if this array valueRep has been seen before
    if (vrep) {
      auto key = std::make_pair(static_cast<void*>(d), vrep->GetPayload());
      auto& dedup_map = get_timesamples_dedup_map();
      auto it = dedup_map.find(key);
      if (it != dedup_map.end()) {
        size_t ref_index = it->second;
        DCOUT("Matrix4d array dedup: reusing sample index " << ref_index);
        return d->add_dedup_matrix_array_sample_pod<value::matrix4d>(time, ref_index, err);
      } else {
        size_t current_index = d->size();
        dedup_map[key] = current_index;
        DCOUT("Matrix4d array dedup: storing new sample at index " << current_index);
        return d->add_matrix_array_sample_pod<value::matrix4d>(
            time, arrval, err, expected_total_samples);
      }
    } else {
      return d->add_matrix_array_sample_pod<value::matrix4d>(
          time, arrval, err, expected_total_samples);
    }
  } else {
    return d->add_sample(time, value::Value(arrval), err);
  }
}

// TypedArray overloads for matrix types
inline bool add_matrix2d_array_sample_to_timesamples(
    value::TimeSamples *d, double time,
    const TypedArray<value::matrix2d> &arrval, std::string *err,
    size_t expected_total_samples = 0) {
  if (d->is_using_pod()) {
    return d->add_matrix_array_sample_pod<value::matrix2d>(
        time, arrval, err, expected_total_samples);
  } else {
    std::vector<value::matrix2d> vec(arrval.data(), arrval.data() + arrval.size());
    return d->add_sample(time, value::Value(vec), err);
  }
}

inline bool add_matrix3d_array_sample_to_timesamples(
    value::TimeSamples *d, double time,
    const TypedArray<value::matrix3d> &arrval, std::string *err,
    size_t expected_total_samples = 0) {
  if (d->is_using_pod()) {
    return d->add_matrix_array_sample_pod<value::matrix3d>(
        time, arrval, err, expected_total_samples);
  } else {
    std::vector<value::matrix3d> vec(arrval.data(), arrval.data() + arrval.size());
    return d->add_sample(time, value::Value(vec), err);
  }
}

inline bool add_matrix4d_array_sample_to_timesamples(
    value::TimeSamples *d, double time,
    const TypedArray<value::matrix4d> &arrval, std::string *err,
    size_t expected_total_samples = 0) {
  if (d->is_using_pod()) {
    return d->add_matrix_array_sample_pod<value::matrix4d>(
        time, arrval, err, expected_total_samples);
  } else {
    std::vector<value::matrix4d> vec(arrval.data(), arrval.data() + arrval.size());
    return d->add_sample(time, value::Value(vec), err);
  }
}

#endif

// Helper to add blocked sample - POD version
template <typename T>
typename std::enable_if<is_pod_type<T>::value, bool>::type
add_blocked_sample_to_timesamples(value::TimeSamples *d, double time,
                                  std::string *err,
                                  size_t expected_total_samples = 0) {
  if (d->is_using_pod()) {
    return d->add_blocked_sample_pod<T>(time, err, expected_total_samples);
  } else {
    return d->add_blocked_sample(time, value::Value(T{}), err);
  }
}

// Helper to add blocked sample - non-POD version
template <typename T>
typename std::enable_if<!is_pod_type<T>::value, bool>::type
add_blocked_sample_to_timesamples(value::TimeSamples *d, double time,
                                  std::string *err,
                                  size_t expected_total_samples = 0) {
  (void)expected_total_samples;  // unused for non-POD
  return d->add_blocked_sample(time, value::Value(T{}), err);
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

    // Check deduplication cache for bool array
    auto it = _dedup_bool_array.find(rep);
    if (it != _dedup_bool_array.end()) {
      // Reuse cached array via ref_index
      size_t ref_index = it->second;
      DCOUT("Reusing cached BOOL array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // First occurrence - read and cache array
      if (!ReadArray(&v_uint8)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read bool array.");
      }

      // Convert uint8_t array to bool array
      std::vector<bool> v_bool;
      v_bool.reserve(v_uint8.size());
      for (uint8_t val : v_uint8) {
        v_bool.push_back(val != 0);
      }

      // Store current index before adding
      size_t current_index = dst.size();
      _dedup_bool_array[rep] = current_index;
      DCOUT("Caching BOOL array at sample index " << current_index);

      // Use value::Value array storage with dedup support (move, no copy)
      if (!dst.add_value_array_sample(t, value::Value(std::move(v_bool)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }

  } else {
    // Non-array value is not supported

    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for boolean is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_INT32(double t,
                                              const crate::ValueRep &rep,
                                              value::TimeSamples &dst,
                                              size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // Blocked value
    // VALUE_BLOCK can have any flags, just skip the flag check
    // Just add a blocked sample
    if (!add_blocked_sample_to_timesamples<int32_t>(&dst, t, &_err,
                                                    expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  // just in case
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_INT) {
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
      int32_t val;
      memcpy(&val, &data, sizeof(int32_t));

    if (!add_sample_to_timesamples<int32_t>(&dst, t, val, &_err,
                                            expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else if (rep.IsArray()) {
    TypedArray<int32_t> v;
    if (rep.GetPayload() == 0) {  // empty array
      if (!add_array_sample_to_timesamples<int32_t>(&dst, t, v, &_err,
                                                    expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check if this array ValueRep has been seen before in this TimeSamples
    auto key = std::make_pair(static_cast<void*>(&dst), rep.GetPayload());
    auto& dedup_map = get_timesamples_dedup_map();
    auto it = dedup_map.find(key);

    if (it != dedup_map.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("INT32 array dedup: reusing sample index " << ref_index << " for ValueRep payload " << rep.GetPayload());

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // First occurrence - read data, store as original and remember the index
      if (!ReadIntArrayTyped(rep.IsCompressed(), &v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read Int array.");
      }

      if (v.empty()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Empty int array.");
        return false;
      }

      DCOUT("timeSamples.INT32 " << value::print_array_snipped(v));

      size_t current_index = dst.size();
      dedup_map[key] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      std::vector<int32_t> vec(v.data(), v.data() + v.size());
      if (!dst.add_value_array_sample(t, value::Value(std::move(vec)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }

  } else {
    // Non-array value is not supported

    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for int32_t is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_HALF(double t,
                                             const crate::ValueRep &rep,
                                             value::TimeSamples &dst,
                                             size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // Blocked value - just add a blocked sample
    if (!add_blocked_sample_to_timesamples<float>(&dst, t, &_err,
                                                  expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  // just in case
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  DCOUT("rep " << to_string(rep));

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      // Compressed or array types are not inlined
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }

    // Decode value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    value::half f;
    uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
    memcpy(&f, &data, sizeof(value::half));

    if (!add_sample_to_timesamples<value::half>(&dst, t, f, &_err,
                                                expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }

  } else if (rep.IsArray()) {
    TypedArray<value::half> v;
    if (rep.GetPayload() == 0) {  // empty array
      if (!add_array_sample_to_timesamples<value::half>(
              &dst, t, v, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check deduplication cache for array
    auto it = _dedup_half_array.find(rep);
    if (it != _dedup_half_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached HALF array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array
      std::vector<value::half> temp_v;
      if (!ReadHalfArray(rep.IsCompressed(), &temp_v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read half array.");
      }

      DCOUT("timeSamples.HALF " << value::print_array_snipped(temp_v));

      if (temp_v.empty()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Empty half array.");
        return false;
      }

      size_t current_index = dst.size();
      _dedup_half_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      if (!dst.add_value_array_sample(t, value::Value(std::move(temp_v)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }

  } else {
    // Non-array value is not supported

    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for half is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_HALF2(double t,
                                              const crate::ValueRep &rep,
                                              value::TimeSamples &dst,
                                              size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // Blocked value - just add a blocked sample
    if (!add_blocked_sample_to_timesamples<float>(&dst, t, &_err,
                                                  expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  // just in case
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2H) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  DCOUT("rep " << to_string(rep));

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      // Compressed or array types are not inlined
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }

    // Decode value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    value::half2 v;
    uint32_t vdata =
        (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
    // Value is represented in int8
    int8_t data[2];
    memcpy(&data, &vdata, 2);
    v[0] = value::float_to_half_full(float(data[0]));
    v[1] = value::float_to_half_full(float(data[1]));

    DCOUT("value.half2 = " << v);

    if (!add_sample_to_timesamples<value::half2>(&dst, t, v, &_err,
                                                 expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }

  } else if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed half2 not supported for TimeSamples.");
    }

    std::vector<value::half2> v;
    if (rep.GetPayload() == 0) {  // empty array
      if (!add_array_sample_to_timesamples<value::half2>(
              &dst, t, v, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check deduplication cache for array
    auto it = _dedup_half2_array.find(rep);
    if (it != _dedup_half2_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached HALF2 array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array
      if (!ReadArray(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read vec2 array.");
      }

      DCOUT("timeSamples.VEC2H " << value::print_array_snipped(v));

      size_t current_index = dst.size();
      _dedup_half2_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      if (!dst.add_value_array_sample(t, value::Value(std::move(v)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }

  } else {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed half2 not supported for TimeSamples.");
    }

    // Read scalar value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    value::half2 v;
    CHECK_MEMORY_USAGE(sizeof(value::half2));
    if (!_sr->read(sizeof(value::half2), sizeof(value::half2),
                   reinterpret_cast<uint8_t *>(&v))) {
      PUSH_ERROR_AND_RETURN("Failed to read half2");
    }
    DCOUT("half2 = " << v);

    if (!add_sample_to_timesamples<value::half2>(&dst, t, v, &_err,
                                                 expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_HALF3(double t,
                                              const crate::ValueRep &rep,
                                              value::TimeSamples &dst,
                                              size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // Blocked value - just add a blocked sample
    if (!add_blocked_sample_to_timesamples<float>(&dst, t, &_err,
                                                  expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  // just in case
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3H) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  DCOUT("rep " << to_string(rep));

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      // Compressed or array types are not inlined
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }
    // Scalar deduplication was removed - see FLOAT3 fix
      // Decode and cache
      uint32_t vdata =
          (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
      // Value is represented in int8
      int8_t data[3];
      memcpy(&data, &vdata, 3);
      value::half3 v;
      v[0] = value::float_to_half_full(float(data[0]));
      v[1] = value::float_to_half_full(float(data[1]));
      v[2] = value::float_to_half_full(float(data[2]));

    DCOUT("value.half3 = " << v);

    if (!add_sample_to_timesamples<value::half3>(&dst, t, v, &_err,
                                                 expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }

  } else if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed half3 not supported for TimeSamples.");
    }

    std::vector<value::half3> v;
    if (rep.GetPayload() == 0) {  // empty array
      if (!add_array_sample_to_timesamples<value::half3>(
              &dst, t, v, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check deduplication cache for array
    auto it = _dedup_half3_array.find(rep);
    if (it != _dedup_half3_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached HALF3 array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array
      if (!ReadArray(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read vec3 array.");
      }
      DCOUT("timeSamples.VEC3H " << value::print_array_snipped(v));

      size_t current_index = dst.size();
      _dedup_half3_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      if (!dst.add_value_array_sample(t, value::Value(std::move(v)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }

  } else {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed half3 not supported for TimeSamples.");
    }

    // Scalar deduplication was removed - see FLOAT3 fix
    // Read scalar value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    value::half3 v;
    CHECK_MEMORY_USAGE(sizeof(value::half3));
    if (!_sr->read(sizeof(value::half3), sizeof(value::half3),
                   reinterpret_cast<uint8_t *>(&v))) {
        PUSH_ERROR_AND_RETURN("Failed to read half3");
      }
      DCOUT("half3 = " << v);

    if (!add_sample_to_timesamples<value::half3>(&dst, t, v, &_err,
                                                 expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_HALF4(double t,
                                              const crate::ValueRep &rep,
                                              value::TimeSamples &dst,
                                              size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // Blocked value - just add a blocked sample
    if (!add_blocked_sample_to_timesamples<float>(&dst, t, &_err,
                                                  expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  // just in case
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4H) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  DCOUT("rep " << to_string(rep));

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      // Compressed or array types are not inlined
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }

    // Scalar deduplication was removed - see FLOAT3 fix
      // Decode and cache
      uint32_t vdata =
          (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
      // Value is represented in int8
      int8_t data[4];
      memcpy(&data, &vdata, 4);
      value::half4 v;
      v[0] = value::float_to_half_full(float(data[0]));
      v[1] = value::float_to_half_full(float(data[1]));
      v[2] = value::float_to_half_full(float(data[2]));
      v[3] = value::float_to_half_full(float(data[3]));

    DCOUT("value.half4 = " << v);

    if (!add_sample_to_timesamples<value::half4>(&dst, t, v, &_err,
                                                 expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }

  } else if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed half4 not supported for TimeSamples.");
    }

    std::vector<value::half4> v;
    if (rep.GetPayload() == 0) {  // empty array
      if (!add_array_sample_to_timesamples<value::half4>(
              &dst, t, v, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check deduplication cache for array
    auto it = _dedup_half4_array.find(rep);
    if (it != _dedup_half4_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached HALF4 array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array
      if (!ReadArray(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read vec4 array.");
      }
      DCOUT("timeSamples.VEC4H " << value::print_array_snipped(v));

      size_t current_index = dst.size();
      _dedup_half4_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      if (!dst.add_value_array_sample(t, value::Value(std::move(v)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }

  } else {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed half4 not supported for TimeSamples.");
    }

    // Scalar deduplication was removed - see FLOAT3 fix
    // Read scalar value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    value::half4 v;
    CHECK_MEMORY_USAGE(sizeof(value::half4));
    if (!_sr->read(sizeof(value::half4), sizeof(value::half4),
                   reinterpret_cast<uint8_t *>(&v))) {
        PUSH_ERROR_AND_RETURN("Failed to read half4");
      }
      DCOUT("half4 = " << v);

    if (!add_sample_to_timesamples<value::half4>(&dst, t, v, &_err,
                                                 expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_FLOAT(double t,
                                              const crate::ValueRep &rep,
                                              value::TimeSamples &dst,
                                              size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // Blocked value - just add a blocked sample
    if (!add_blocked_sample_to_timesamples<float>(&dst, t, &_err,
                                                  expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  // just in case
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  DCOUT("rep " << to_string(rep));

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      // Compressed or array types are not inlined
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }

    // Decode value directly without caching
    // Scalar deduplication was removed - see float3 fix
    float val;
    uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
    memcpy(&val, &data, sizeof(float));

    if (!add_sample_to_timesamples<float>(&dst, t, val, &_err,
                                          expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }

  } else if (rep.IsArray()) {
    TypedArray<float> v;
    if (rep.GetPayload() == 0) {  // empty array
      if (!add_array_sample_to_timesamples<float>(&dst, t, v, &_err,
                                                  expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check if this array ValueRep has been seen before in this TimeSamples
    auto key = std::make_pair(static_cast<void*>(&dst), rep.GetPayload());
    auto& dedup_map = get_timesamples_dedup_map();
    auto it = dedup_map.find(key);

    if (it != dedup_map.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("FLOAT array dedup: reusing sample index " << ref_index << " for ValueRep payload " << rep.GetPayload());

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // First occurrence - read data, store as original and remember the index
      if (!ReadFloatArrayTyped(rep.IsCompressed(), &v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read Float array.");
      }

      DCOUT("timeSamples.FLOAT " << value::print_array_snipped(v));

      size_t current_index = dst.size();
      dedup_map[key] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      std::vector<float> vec(v.data(), v.data() + v.size());
      if (!dst.add_value_array_sample(t, value::Value(std::move(vec)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }

  } else {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Compressed float not supported for TimeSamples.");
    }

    // Read scalar value directly without caching
    // Scalar deduplication was removed - see float3 fix
    float v;
    CHECK_MEMORY_USAGE(sizeof(float));
    if (!_sr->read(sizeof(float), sizeof(float),
                   reinterpret_cast<uint8_t *>(&v))) {
      PUSH_ERROR_AND_RETURN("Failed to read float");
    }
    DCOUT("float = " << v);

    if (!add_sample_to_timesamples<float>(&dst, t, v, &_err,
                                          expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_FLOAT2(double t,
                                               const crate::ValueRep &rep,
                                               value::TimeSamples &dst,
                                               size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // Blocked value - just add a blocked sample
    if (!add_blocked_sample_to_timesamples<float>(&dst, t, &_err,
                                                  expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  // just in case
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  DCOUT("rep " << to_string(rep));

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      // Compressed or array types are not inlined
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }

    value::float2 v;
    {
      // Decode and cache
      uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));

      // Value is represented in int8
      int8_t vdata[2];
      memcpy(&vdata, &data, 2);

      v[0] = float(vdata[0]);
      v[1] = float(vdata[1]);

    }

    DCOUT("value.float2 = " << v);

    if (!add_sample_to_timesamples<value::float2>(&dst, t, v, &_err,
                                                  expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }

  } else if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed float2 not supported for TimeSamples.");
    }

    TypedArray<value::float2> v;
    if (rep.GetPayload() == 0) {  // empty array
      if (!add_array_sample_to_timesamples<value::float2>(
              &dst, t, v, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check if this array ValueRep has been seen before in this TimeSamples
    auto key = std::make_pair(static_cast<void*>(&dst), rep.GetPayload());
    auto& dedup_map = get_timesamples_dedup_map();
    auto it = dedup_map.find(key);

    if (it != dedup_map.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("FLOAT2 array dedup: reusing sample index " << ref_index << " for ValueRep payload " << rep.GetPayload());

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // First occurrence - read data, store as original and remember the index
      if (!ReadFloat2ArrayTyped(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read vec2 array.");
      }

      DCOUT("timeSamples.FLOAT2 " << value::print_array_snipped(v));

      size_t current_index = dst.size();
      dedup_map[key] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      std::vector<value::float2> vec(v.data(), v.data() + v.size());
      if (!dst.add_value_array_sample(t, value::Value(std::move(vec)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }

  } else {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed float2 not supported for TimeSamples.");
    }

    // Read scalar value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    value::float2 v;
    CHECK_MEMORY_USAGE(sizeof(value::float2));
    if (!_sr->read(sizeof(value::float2), sizeof(value::float2),
                   reinterpret_cast<uint8_t *>(&v))) {
      PUSH_ERROR_AND_RETURN("Failed to read float2");
    }
    DCOUT("float2 = " << v);

    if (!add_sample_to_timesamples<value::float2>(&dst, t, v, &_err,
                                                  expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_QUATF(double t,
                                              const crate::ValueRep &rep,
                                              value::TimeSamples &dst,
                                              size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // Blocked value - just add a blocked sample
    if (!add_blocked_sample_to_timesamples<float>(&dst, t, &_err,
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

    // Check deduplication cache for array
    auto it = _dedup_quatf_array.find(rep);
    if (it != _dedup_quatf_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached QUATF array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array
      if (!ReadArray(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read quatf array.");
      }

      DCOUT("timeSamples.QUATF " << value::print_array_snipped(v));

      size_t current_index = dst.size();
      _dedup_quatf_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      if (!dst.add_value_array_sample(t, value::Value(std::move(v)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }

  } else {
    // Scalar (non-inlined, non-array) quatf value
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Compressed quatf not supported for TimeSamples.");
    }

    // Read scalar value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    value::quatf val;
    CHECK_MEMORY_USAGE(sizeof(float) * 4);  // quatf has 4 floats
    if (!_sr->read(sizeof(float) * 4, sizeof(float) * 4,
                   reinterpret_cast<uint8_t *>(&val))) {
      PUSH_ERROR_AND_RETURN("Failed to read quatf value");
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
    // Token is stored as TokenIndex for inlined value
    uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
    if (auto v = GetToken(crate::Index(data))) {
      value::token tok(v.value().str());

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
    uint64_t n;
    if (!_sr->read8(&n)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of array elements.");
    }

    if (n > _config.maxArrayElements) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Token array too large.");
    }

    CHECK_MEMORY_USAGE(n * sizeof(crate::Index));

    std::vector<crate::Index> indices(static_cast<size_t>(n));
    if (!_sr->read(size_t(n) * sizeof(crate::Index),
                   size_t(n) * sizeof(crate::Index),
                   reinterpret_cast<uint8_t *>(indices.data()))) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read TokenIndex array.");
    }

    // Convert token indices to tokens
    v.reserve(n);
    for (size_t i = 0; i < n; i++) {
      if (auto tokv = GetToken(indices[i])) {
        v.emplace_back(tokv.value().str());
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
        v = value::token(tok_val.value().str());
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

bool CrateReader::UnpackTimeSampleValue_FLOAT3(double t,
                                               const crate::ValueRep &rep,
                                               value::TimeSamples &dst,
                                               size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::float3>(
            &dst, t, &_err, expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }

    // Decode value directly without caching
    // Scalar deduplication was removed as it was causing incorrect global
    // deduplication across different attributes. Each attribute's TimeSamples
    // must independently store its values.
    // Value is represented in int8
    value::float3 val;
    uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
    int8_t vdata[3];
    memcpy(&vdata, &data, 3);
    val[0] = float(vdata[0]);
    val[1] = float(vdata[1]);
    val[2] = float(vdata[2]);

    if (!add_sample_to_timesamples<value::float3>(&dst, t, val, &_err,
                                                  expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed float3 not supported for TimeSamples.");
    }

    std::vector<value::float3> v;
    if (rep.GetPayload() == 0) {
      if (!add_array_sample_to_timesamples<value::float3>(
              &dst, t, v, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check deduplication cache for array
    auto it = _dedup_float3_array.find(rep);
    if (it != _dedup_float3_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached FLOAT3 array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array
      if (!ReadArray(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read float3 array.");
      }

      DCOUT("timeSamples.FLOAT3 " << value::print_array_snipped(v));

      size_t current_index = dst.size();
      _dedup_float3_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      if (!dst.add_value_array_sample(t, value::Value(std::move(v)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }
  } else {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed float3 not supported for TimeSamples.");
    }

    // Read scalar value directly without caching
    // Scalar deduplication was removed as it was causing incorrect global
    // deduplication across different attributes. Each attribute's TimeSamples
    // must independently store its values.
    value::float3 v;
    CHECK_MEMORY_USAGE(sizeof(value::float3));
    if (!_sr->read(sizeof(value::float3), sizeof(value::float3),
                   reinterpret_cast<uint8_t *>(&v))) {
      PUSH_ERROR_AND_RETURN("Failed to read float3");
    }
    DCOUT("float3 = " << v);

    if (!add_sample_to_timesamples<value::float3>(&dst, t, v, &_err,
                                                  expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_FLOAT4(double t,
                                               const crate::ValueRep &rep,
                                               value::TimeSamples &dst,
                                               size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::float4>(
            &dst, t, &_err, expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }

    // Decode value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    // Value is represented in int8
    value::float4 val;
    uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
    int8_t vdata[4];
    memcpy(&vdata, &data, 4);
    val[0] = float(vdata[0]);
    val[1] = float(vdata[1]);
    val[2] = float(vdata[2]);
    val[3] = float(vdata[3]);

    if (!add_sample_to_timesamples<value::float4>(&dst, t, val, &_err,
                                                  expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed float4 not supported for TimeSamples.");
    }

    std::vector<value::float4> v;
    if (rep.GetPayload() == 0) {
      if (!add_array_sample_to_timesamples<value::float4>(
              &dst, t, v, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check deduplication cache for array
    auto it = _dedup_float4_array.find(rep);
    if (it != _dedup_float4_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached FLOAT4 array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array
      if (!ReadArray(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read float4 array.");
      }

      DCOUT("timeSamples.FLOAT4 " << value::print_array_snipped(v));

      size_t current_index = dst.size();
      _dedup_float4_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      if (!dst.add_value_array_sample(t, value::Value(std::move(v)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }
  } else {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed float4 not supported for TimeSamples.");
    }

    // Read scalar value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    value::float4 v;
    CHECK_MEMORY_USAGE(sizeof(value::float4));
    if (!_sr->read(sizeof(value::float4), sizeof(value::float4),
                   reinterpret_cast<uint8_t *>(&v))) {
      PUSH_ERROR_AND_RETURN("Failed to read float4");
    }
    DCOUT("float4 = " << v);

    if (!add_sample_to_timesamples<value::float4>(&dst, t, v, &_err,
                                                  expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_DOUBLE2(double t,
                                                const crate::ValueRep &rep,
                                                value::TimeSamples &dst,
                                                size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::double2>(
            &dst, t, &_err, expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }

    // Decode value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    // Value is represented in int8
    value::double2 val;
    uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
    int8_t vdata[2];
    memcpy(&vdata, &data, 2);
    val[0] = double(vdata[0]);
    val[1] = double(vdata[1]);

    if (!add_sample_to_timesamples<value::double2>(&dst, t, val, &_err,
                                                   expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed double2 not supported for TimeSamples.");
    }

    std::vector<value::double2> v;
    if (rep.GetPayload() == 0) {
      if (!add_array_sample_to_timesamples<value::double2>(
              &dst, t, v, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check deduplication cache for array
    auto it = _dedup_double2_array.find(rep);
    if (it != _dedup_double2_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached DOUBLE2 array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array
      if (!ReadArray(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read double2 array.");
      }

      size_t current_index = dst.size();
      _dedup_double2_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      if (!dst.add_value_array_sample(t, value::Value(std::move(v)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }
  } else {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed double2 not supported for TimeSamples.");
    }

    // Read scalar value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    value::double2 v;
    CHECK_MEMORY_USAGE(sizeof(value::double2));
    if (!_sr->read(sizeof(value::double2), sizeof(value::double2),
                   reinterpret_cast<uint8_t *>(&v))) {
      PUSH_ERROR_AND_RETURN("Failed to read double2");
    }
    DCOUT("double2 = " << v);

    if (!add_sample_to_timesamples<value::double2>(&dst, t, v, &_err,
                                                   expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_DOUBLE3(double t,
                                                const crate::ValueRep &rep,
                                                value::TimeSamples &dst,
                                                size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::double3>(
            &dst, t, &_err, expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }

    // Decode value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    // Value is represented in int8
    value::double3 val;
    uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
    int8_t vdata[3];
    memcpy(&vdata, &data, 3);
    val[0] = double(vdata[0]);
    val[1] = double(vdata[1]);
    val[2] = double(vdata[2]);

    if (!add_sample_to_timesamples<value::double3>(&dst, t, val, &_err,
                                                   expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed double3 not supported for TimeSamples.");
    }

    std::vector<value::double3> v;
    if (rep.GetPayload() == 0) {
      if (!add_array_sample_to_timesamples<value::double3>(
              &dst, t, v, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check deduplication cache for array
    auto it = _dedup_double3_array.find(rep);
    if (it != _dedup_double3_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached DOUBLE3 array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array
      if (!ReadArray(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read double3 array.");
      }

      size_t current_index = dst.size();
      _dedup_double3_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      if (!dst.add_value_array_sample(t, value::Value(std::move(v)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }
  } else {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed double3 not supported for TimeSamples.");
    }

    // Read scalar value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    value::double3 v;
    CHECK_MEMORY_USAGE(sizeof(value::double3));
    if (!_sr->read(sizeof(value::double3), sizeof(value::double3),
                   reinterpret_cast<uint8_t *>(&v))) {
      PUSH_ERROR_AND_RETURN("Failed to read double3");
    }
    DCOUT("double3 = " << v);

    if (!add_sample_to_timesamples<value::double3>(&dst, t, v, &_err,
                                                   expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_DOUBLE4(double t,
                                                const crate::ValueRep &rep,
                                                value::TimeSamples &dst,
                                                size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::double4>(
            &dst, t, &_err, expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }

    // Decode value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    // Value is represented in int8
    value::double4 val;
    uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
    int8_t vdata[4];
    memcpy(&vdata, &data, 4);
    val[0] = static_cast<double>(vdata[0]);
    val[1] = static_cast<double>(vdata[1]);
    val[2] = static_cast<double>(vdata[2]);
    val[3] = static_cast<double>(vdata[3]);

    if (!add_sample_to_timesamples<value::double4>(&dst, t, val, &_err,
                                                   expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed double4 not supported for TimeSamples.");
    }

    std::vector<value::double4> v;
    if (rep.GetPayload() == 0) {
      if (!add_array_sample_to_timesamples<value::double4>(
              &dst, t, v, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check deduplication cache for array
    auto it = _dedup_double4_array.find(rep);
    if (it != _dedup_double4_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached DOUBLE4 array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array
      if (!ReadArray(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read double4 array.");
      }

      size_t current_index = dst.size();
      _dedup_double4_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      if (!dst.add_value_array_sample(t, value::Value(std::move(v)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }
  } else {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed double4 not supported for TimeSamples.");
    }

    // Read scalar value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    value::double4 v;
    CHECK_MEMORY_USAGE(sizeof(value::double4));
    if (!_sr->read(sizeof(value::double4), sizeof(value::double4),
                   reinterpret_cast<uint8_t *>(&v))) {
      PUSH_ERROR_AND_RETURN("Failed to read double4");
    }
    DCOUT("double4 = " << v);

    if (!add_sample_to_timesamples<value::double4>(&dst, t, v, &_err,
                                                   expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_QUATH(double t,
                                              const crate::ValueRep &rep,
                                              value::TimeSamples &dst,
                                              size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::quath>(
            &dst, t, &_err, expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATH) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Inlined quath is not allowed.");
  } else if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed quath not supported for TimeSamples.");
    }

    std::vector<value::quath> v;
    if (rep.GetPayload() == 0) {
      if (!add_array_sample_to_timesamples<value::quath>(
              &dst, t, v, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check deduplication cache for array
    auto it = _dedup_quath_array.find(rep);
    if (it != _dedup_quath_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached QUATH array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array
      if (!ReadArray(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read quath array.");
      }

      DCOUT("timeSamples.QUATH " << value::print_array_snipped(v));

      size_t current_index = dst.size();
      _dedup_quath_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      if (!dst.add_value_array_sample(t, value::Value(std::move(v)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }
  } else {
    // Scalar (non-inlined, non-array) quath value
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Compressed quath not supported for TimeSamples.");
    }
    // Read scalar value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    value::quath val;
    // quath has 4 halfs (half is 2 bytes)
    CHECK_MEMORY_USAGE(sizeof(uint16_t) * 4);
    if (!_sr->read(sizeof(uint16_t) * 4, sizeof(uint16_t) * 4,
                   reinterpret_cast<uint8_t *>(&val))) {
      PUSH_ERROR_AND_RETURN("Failed to read quath value");
    }
    DCOUT("quath = [" << val[0] << ", " << val[1] << ", " << val[2] << ", " << val[3] << "]");
    if (!add_sample_to_timesamples<value::quath>(&dst, t, val, &_err,
                                                 expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_QUATD(double t,
                                              const crate::ValueRep &rep,
                                              value::TimeSamples &dst,
                                              size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::quatd>(
            &dst, t, &_err, expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATD) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Inlined quatd is not allowed.");
  } else if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed quatd not supported for TimeSamples.");
    }

    std::vector<value::quatd> v;
    if (rep.GetPayload() == 0) {
      if (!add_array_sample_to_timesamples<value::quatd>(
              &dst, t, v, &_err, expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check deduplication cache for array
    auto it = _dedup_quatd_array.find(rep);
    if (it != _dedup_quatd_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached QUATD array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array
      if (!ReadArray(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read quatd array.");
      }

      DCOUT("timeSamples.QUATD " << value::print_array_snipped(v));

      size_t current_index = dst.size();
      _dedup_quatd_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      if (!dst.add_value_array_sample(t, value::Value(std::move(v)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }
  } else {
    // Scalar (non-inlined, non-array) quatd value
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Compressed quatd not supported for TimeSamples.");
    }

    // Scalar deduplication was removed - see FLOAT3 fix
    // Read scalar value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    value::quatd val;
    // quatd has 4 doubles
    CHECK_MEMORY_USAGE(sizeof(double) * 4);
    if (!_sr->read(sizeof(double) * 4, sizeof(double) * 4,
                   reinterpret_cast<uint8_t *>(&val))) {
      PUSH_ERROR_AND_RETURN("Failed to read quatd value");
    }
    DCOUT("quatd = [" << val[0] << ", " << val[1] << ", " << val[2] << ", " << val[3] << "]");
    if (!add_sample_to_timesamples<value::quatd>(&dst, t, val, &_err,
                                                 expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_MATRIX2D(
    double t, const crate::ValueRep &rep, value::TimeSamples &dst,
    size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::matrix2d>(
            &dst, t, &_err, expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }

    // Scalar deduplication was removed - see FLOAT3 fix
      // Decode and cache
      // Matrix contains diagonal components only, values are represented in int8
      uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
      int8_t vdata[2];
      memcpy(&vdata, &data, 2);
      value::matrix2d val;
      memset(val.m, 0, sizeof(value::matrix2d));
      val.m[0][0] = static_cast<double>(vdata[0]);
      val.m[1][1] = static_cast<double>(vdata[1]);

    if (!add_sample_to_timesamples<value::matrix2d>(&dst, t, val, &_err,
                                                    expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed matrix2d not supported for TimeSamples.");
    }

    std::vector<value::matrix2d> v;
    if (rep.GetPayload() == 0) {
      if (!add_matrix2d_array_sample_to_timesamples(&dst, t, v, &_err,
                                                    expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check deduplication cache for array
    auto it = _dedup_matrix2d_array.find(rep);
    if (it != _dedup_matrix2d_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached MATRIX2D array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array
      if (!ReadArray(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read matrix2d array.");
      }

      size_t current_index = dst.size();
      _dedup_matrix2d_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      if (!dst.add_value_array_sample(t, value::Value(std::move(v)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for matrix2d is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_MATRIX3D(
    double t, const crate::ValueRep &rep, value::TimeSamples &dst,
    size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::matrix3d>(
            &dst, t, &_err, expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }

    // Scalar deduplication was removed - see FLOAT3 fix
      // Decode and cache
      // Matrix contains diagonal components only, values are represented in int8
      uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
      int8_t vdata[3];
      memcpy(&vdata, &data, 3);
      value::matrix3d val;
      memset(val.m, 0, sizeof(value::matrix3d));
      val.m[0][0] = static_cast<double>(vdata[0]);
      val.m[1][1] = static_cast<double>(vdata[1]);
      val.m[2][2] = static_cast<double>(vdata[2]);

    if (!add_sample_to_timesamples<value::matrix3d>(&dst, t, val, &_err,
                                                    expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed matrix3d not supported for TimeSamples.");
    }

    std::vector<value::matrix3d> v;
    if (rep.GetPayload() == 0) {
      if (!add_matrix3d_array_sample_to_timesamples(&dst, t, v, &_err,
                                                    expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check deduplication cache for array
    auto it = _dedup_matrix3d_array.find(rep);
    if (it != _dedup_matrix3d_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached MATRIX3D array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array
      if (!ReadArray(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read matrix3d array.");
      }

      size_t current_index = dst.size();
      _dedup_matrix3d_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      if (!dst.add_value_array_sample(t, value::Value(std::move(v)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for matrix3d is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_MATRIX4D(
    double t, const crate::ValueRep &rep, value::TimeSamples &dst,
    size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::matrix4d>(
            &dst, t, &_err, expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }

    // Scalar deduplication was removed - see FLOAT3 fix
      // Decode and cache
      // Matrix contains diagonal components only, values are represented in int8
      uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
      int8_t vdata[4];
      memcpy(&vdata, &data, 4);
      value::matrix4d val;
      memset(val.m, 0, sizeof(value::matrix4d));
      val.m[0][0] = static_cast<double>(vdata[0]);
      val.m[1][1] = static_cast<double>(vdata[1]);
      val.m[2][2] = static_cast<double>(vdata[2]);
      val.m[3][3] = static_cast<double>(vdata[3]);

    if (!add_sample_to_timesamples<value::matrix4d>(&dst, t, val, &_err,
                                                    expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Compressed matrix4d not supported for TimeSamples.");
    }

    std::vector<value::matrix4d> v;
    if (rep.GetPayload() == 0) {
      if (!add_matrix4d_array_sample_to_timesamples(&dst, t, v, &_err,
                                                    expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check deduplication cache for array
    auto it = _dedup_matrix4d_array.find(rep);
    if (it != _dedup_matrix4d_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached MATRIX4D array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array
      if (!ReadArray(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read matrix4d array.");
      }

      size_t current_index = dst.size();
      _dedup_matrix4d_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      if (!dst.add_value_array_sample(t, value::Value(std::move(v)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for matrix4d is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_UINT32(double t,
                                               const crate::ValueRep &rep,
                                               value::TimeSamples &dst,
                                               size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<uint32_t>(&dst, t, &_err,
                                                     expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }

    // Scalar deduplication was removed - see FLOAT3 fix
      // Decode and cache
      uint32_t val = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));

    if (!add_sample_to_timesamples<uint32_t>(&dst, t, val, &_err,
                                             expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else if (rep.IsArray()) {
    TypedArray<uint32_t> v;
    if (rep.GetPayload() == 0) {
      if (!add_array_sample_to_timesamples<uint32_t>(&dst, t, v, &_err,
                                                     expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check deduplication cache for array
    auto it = _dedup_uint32_array.find(rep);
    if (it != _dedup_uint32_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached UINT32 array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array
      if (!ReadIntArrayTyped(rep.IsCompressed(), &v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read uint32 array.");
      }

      DCOUT("timeSamples.UINT32 " << value::print_array_snipped(v));

      size_t current_index = dst.size();
      _dedup_uint32_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      std::vector<uint32_t> vec(v.data(), v.data() + v.size());
      if (!dst.add_value_array_sample(t, value::Value(std::move(vec)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for uint32 is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_INT64(double t,
                                              const crate::ValueRep &rep,
                                              value::TimeSamples &dst,
                                              size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<int64_t>(&dst, t, &_err,
                                                    expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }

    // Scalar deduplication was removed - see FLOAT3 fix
      // Decode and cache
      // Value is represented as int
      uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
      int _val;
      memcpy(&_val, &data, sizeof(int));
      int64_t val = static_cast<int64_t>(_val);

    if (!add_sample_to_timesamples<int64_t>(&dst, t, val, &_err,
                                            expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else if (rep.IsArray()) {
    TypedArray<int64_t> v;
    if (rep.GetPayload() == 0) {
      if (!add_array_sample_to_timesamples<int64_t>(&dst, t, v, &_err,
                                                    expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check deduplication cache for array
    auto it = _dedup_int64_array.find(rep);
    if (it != _dedup_int64_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached INT64 array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array
      if (!ReadIntArrayTyped(rep.IsCompressed(), &v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read int64 array.");
      }

      DCOUT("timeSamples.INT64 " << value::print_array_snipped(v));

      size_t current_index = dst.size();
      _dedup_int64_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      std::vector<int64_t> vec(v.data(), v.data() + v.size());
      if (!dst.add_value_array_sample(t, value::Value(std::move(vec)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }
  } else {
    // Scalar (non-inlined, non-array) int64 value
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Compressed int64 not supported for TimeSamples.");
    }

    // Read scalar value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    int64_t val;
    CHECK_MEMORY_USAGE(sizeof(int64_t));
    if (!_sr->read(sizeof(int64_t), sizeof(int64_t),
                   reinterpret_cast<uint8_t *>(&val))) {
      PUSH_ERROR_AND_RETURN("Failed to read int64 value");
    }
    DCOUT("int64 = " << val);
    if (!add_sample_to_timesamples<int64_t>(&dst, t, val, &_err,
                                            expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_UINT64(double t,
                                               const crate::ValueRep &rep,
                                               value::TimeSamples &dst,
                                               size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<uint64_t>(&dst, t, &_err,
                                                     expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }

    // Scalar deduplication was removed - see FLOAT3 fix
      // Decode and cache
      // Value is represented as uint
      uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
      uint32_t _val;
      memcpy(&_val, &data, sizeof(uint32_t));
      uint64_t val = static_cast<uint64_t>(_val);

    if (!add_sample_to_timesamples<uint64_t>(&dst, t, val, &_err,
                                             expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else if (rep.IsArray()) {
    TypedArray<uint64_t> v;
    if (rep.GetPayload() == 0) {
      if (!add_array_sample_to_timesamples<uint64_t>(&dst, t, v, &_err,
                                                     expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check deduplication cache for array
    auto it = _dedup_uint64_array.find(rep);
    if (it != _dedup_uint64_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached UINT64 array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array
      if (!ReadIntArrayTyped(rep.IsCompressed(), &v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read uint64 array.");
      }

      DCOUT("timeSamples.UINT64 " << value::print_array_snipped(v));

      size_t current_index = dst.size();
      _dedup_uint64_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      std::vector<uint64_t> vec(v.data(), v.data() + v.size());
      if (!dst.add_value_array_sample(t, value::Value(std::move(vec)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }
  } else {
    // Scalar (non-inlined, non-array) uint64 value
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Compressed uint64 not supported for TimeSamples.");
    }

    // Scalar deduplication was removed - see FLOAT3 fix
    // Read scalar value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    uint64_t val;
    CHECK_MEMORY_USAGE(sizeof(uint64_t));
    if (!_sr->read(sizeof(uint64_t), sizeof(uint64_t),
                   reinterpret_cast<uint8_t *>(&val))) {
      PUSH_ERROR_AND_RETURN("Failed to read uint64 value");
    }
    DCOUT("uint64 = " << val);
    if (!add_sample_to_timesamples<uint64_t>(&dst, t, val, &_err,
                                             expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_DOUBLE(double t,
                                               const crate::ValueRep &rep,
                                               value::TimeSamples &dst,
                                               size_t expected_total_samples) {
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) ==
      crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<double>(&dst, t, &_err,
                                                   expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) !=
      crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Invalid inlined ValueRep in TimeSamples.");
    }

    // Decode value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    // Value is stored as float
    double val;
    uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
    float _f;
    memcpy(&_f, &data, sizeof(float));
    val = static_cast<double>(_f);

    if (!add_sample_to_timesamples<double>(&dst, t, val, &_err,
                                           expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else if (rep.IsArray()) {
    TypedArray<double> v;
    if (rep.GetPayload() == 0) {
      if (!add_array_sample_to_timesamples<double>(&dst, t, v, &_err,
                                                   expected_total_samples)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }

    // Check deduplication cache for array
    auto it = _dedup_double_array.find(rep);
    if (it != _dedup_double_array.end()) {
      // Deduplicated array - reuse value from first occurrence (no copy)
      size_t ref_index = it->second;
      DCOUT("Reusing cached DOUBLE array at sample index " << ref_index);

      if (!dst.add_dedup_sample(t, ref_index, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add dedup sample to TimeSamples.");
      }
    } else {
      // Read and cache array using TypedArray
      if (!ReadDoubleArrayTyped(rep.IsCompressed(), &v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read double array.");
      }

      DCOUT("timeSamples.DOUBLE " << v.size() << " elements");

      if (v.empty()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Empty double array.");
        return false;
      }

      size_t current_index = dst.size();
      _dedup_double_array[rep] = current_index;

      // Use value::Value array storage with dedup support (move, no copy)
      std::vector<double> vec(v.data(), v.data() + v.size());
      if (!dst.add_value_array_sample(t, value::Value(std::move(vec)), &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
    }
  } else {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Compressed double not supported for TimeSamples.");
    }

    // Read scalar value directly without caching
    // Scalar deduplication was removed - see FLOAT3 fix
    double v;
    CHECK_MEMORY_USAGE(sizeof(double));
    if (!_sr->read(sizeof(double), sizeof(double),
                   reinterpret_cast<uint8_t *>(&v))) {
      PUSH_ERROR_AND_RETURN("Failed to read double");
    }
    DCOUT("double = " << v);

    if (!add_sample_to_timesamples<double>(&dst, t, v, &_err,
                                           expected_total_samples)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  return true;
}

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
      if (!d->init(value::TypeTraits<std::vector<VTYPE>>::type_id())) {       \
        PUSH_ERROR_AND_RETURN(fmt::format(                                    \
            "TimeSamples already initialized with different type. type_id = " \
            "{}[]({}[]) timeSamples.type_id = {}, crate_type = {}[]",         \
            value::TypeTraits<std::vector<VTYPE>>::type_id(),                 \
            value::TypeTraits<std::vector<VTYPE>>::type_name(), d->type_id(), \
            GetCrateDataTypeName(crate_type_id)));                            \
      }                                                                       \
    } else {                                                                  \
      if (!d->init(value::TypeTraits<VTYPE>::type_id())) {                    \
        PUSH_ERROR_AND_RETURN(fmt::format(                                    \
            "TimeSamples already initialized with different type. type_id = " \
            "{}({}) timeSamples.type_id = {}, crate_type = {}",               \
            value::TypeTraits<VTYPE>::type_id(),                              \
            value::TypeTraits<VTYPE>::type_name(), d->type_id(),              \
            GetCrateDataTypeName(crate_type_id)));                            \
      }                                                                       \
    }                                                                         \
    break;                                                                    \
  }

#define HANDLE_INIT_VECTOR_TYPE_CASE(ctype, VTYPE)                          \
  case crate::CrateDataTypeId::ctype: {                                     \
    if (!d->init(value::TypeTraits<std::vector<VTYPE>>::type_id())) {       \
      PUSH_ERROR_AND_RETURN(fmt::format(                                    \
          "TimeSamples already initialized with different type. type_id = " \
          "{}({}) timeSamples.type_id = {}, crate_type = {}",               \
          value::TypeTraits<VTYPE>::type_id(),                              \
          value::TypeTraits<VTYPE>::type_name(), d->type_id(),              \
          GetCrateDataTypeName(crate_type_id)));                            \
    }                                                                       \
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

  for (size_t i = 0; i < vreps.size(); i++) {
    const crate::ValueRep &rep = vreps[i];

    if (!rep.IsInlined()) {
      _sr->seek_set(rep.GetPayload());
    }

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

    // Dispatch to type-specific unpacker
    // Skip VALUE_BLOCK - it will be handled by the type-specific unpacker for
    // the actual type
    if (curr_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
      // Call the appropriate unpacker for the base type to handle VALUE_BLOCK
      // The UnpackTimeSampleValue_* functions handle VALUE_BLOCK internally
    }

    // Pass expected_total_samples only on the first sample (i == 0) for
    // pre-allocation
    size_t prealloc_hint = (i == 0) ? expected_total_samples : 0;

    if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL) {
      if (!UnpackTimeSampleValue_BOOL(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_INT) {
      if (!UnpackTimeSampleValue_INT32(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT) {
      if (!UnpackTimeSampleValue_UINT32(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64) {
      if (!UnpackTimeSampleValue_INT64(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id ==
               crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64) {
      if (!UnpackTimeSampleValue_UINT64(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF) {
      if (!UnpackTimeSampleValue_HALF(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT) {
      if (!UnpackTimeSampleValue_FLOAT(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id ==
               crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE) {
      if (!UnpackTimeSampleValue_DOUBLE(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2H) {
      if (!UnpackTimeSampleValue_HALF2(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3H) {
      if (!UnpackTimeSampleValue_HALF3(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4H) {
      if (!UnpackTimeSampleValue_HALF4(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F) {
      if (!UnpackTimeSampleValue_FLOAT2(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F) {
      if (!UnpackTimeSampleValue_FLOAT3(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F) {
      if (!UnpackTimeSampleValue_FLOAT4(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D) {
      if (!UnpackTimeSampleValue_DOUBLE2(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D) {
      if (!UnpackTimeSampleValue_DOUBLE3(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D) {
      if (!UnpackTimeSampleValue_DOUBLE4(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATF) {
      if (!UnpackTimeSampleValue_QUATF(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATH) {
      if (!UnpackTimeSampleValue_QUATH(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATD) {
      if (!UnpackTimeSampleValue_QUATD(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id ==
               crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D) {
      if (!UnpackTimeSampleValue_MATRIX2D(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id ==
               crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D) {
      if (!UnpackTimeSampleValue_MATRIX3D(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id ==
               crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D) {
      if (!UnpackTimeSampleValue_MATRIX4D(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id ==
               crate::CrateDataTypeId::CRATE_DATA_TYPE_ASSET_PATH) {
      if (!UnpackTimeSampleValue_ASSET_PATH(curr_time, rep, *d,
                                            prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING) {
      if (!UnpackTimeSampleValue_STRING(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN) {
      if (!UnpackTimeSampleValue_TOKEN(curr_time, rep, *d, prealloc_hint)) {
        return false;
      }
    } else {
      // TODO: Use generic value::Value as fallback for unimplemented types
      PUSH_ERROR_AND_RETURN(fmt::format("Unimplemented type in TimeSamples: {}",
                                        GetCrateDataTypeName(crate_type_id)));
    }
  }

#if 0
  // Use POD-aware TimeSamples directly for POD types
  // Initialize TimeSamples with the type_id for this type
  if (!d->init(value::TypeTraits<T>::type_id())) {
    // Already initialized with different type - fall back to standard path
    return false;
  }

  // Process each sample
  _sr->seek_set(vrep_start_offset);
  for (size_t i = 0; i < times.size(); i++) {
    crate::ValueRep rep;
    if (!ReadValueRep(&rep)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read ValueRep for typed TimeSample' value element.");
    }

    crate::CrateValue value;
    uint64_t value_offset = rep.GetPayload();
    if (!UnpackValueRepForTimeSamples(rep, value_offset, &value)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to unpack value of typed TimeSample's value element.");
    }

    // Check if this is a "none" (blocked) value
    bool is_blocked = value.get_raw().is_none();

    if (is_blocked) {
      // Handle blocked value using SFINAE helper
      std::string err;
      if (!add_blocked_sample_to_timesamples<T>(d, times[i], &err)) {
        if (!err.empty()) {
          _err += err;
        }
        return false;
      }
    } else if (auto pv = value.get_value<T>()) {
      // Extract typed value and add to TimeSamples using SFINAE helper
      std::string err;
      if (!add_sample_to_timesamples<T>(d, times[i], pv.value(), &err)) {
        if (!err.empty()) {
          _err += err;
        }
        return false;
      }
    } else {
      // Type mismatch - return false to fall back to standard path
      return false;
    }
  }
#endif

  return true;
}

#if 0
template<typename T>
bool CrateReader::CrateTypedTimeSamples(const std::vector<double> &times,
                                         const std::vector<crate::ValueRep> &,  // value_reps unused
                                         uint64_t vrep_start_offset,
                                         value::TimeSamples *d) {
  // Use POD-aware TimeSamples directly for POD types
  // Initialize TimeSamples with the type_id for this type
  if (!d->init(value::TypeTraits<T>::type_id())) {
    // Already initialized with different type - fall back to standard path
    return false;
  }

  // Process each sample
  _sr->seek_set(vrep_start_offset);
  for (size_t i = 0; i < times.size(); i++) {
    crate::ValueRep rep;
    if (!ReadValueRep(&rep)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read ValueRep for typed TimeSample' value element.");
    }

    crate::CrateValue value;
    uint64_t value_offset = rep.GetPayload();
    if (!UnpackValueRepForTimeSamples(rep, value_offset, &value)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to unpack value of typed TimeSample's value element.");
    }

    // Check if this is a "none" (blocked) value
    bool is_blocked = value.get_raw().is_none();

    if (is_blocked) {
      // Handle blocked value using SFINAE helper
      std::string err;
      if (!add_blocked_sample_to_timesamples<T>(d, times[i], &err)) {
        if (!err.empty()) {
          _err += err;
        }
        return false;
      }
    } else if (auto pv = value.get_value<T>()) {
      // Extract typed value and add to TimeSamples using SFINAE helper
      std::string err;
      if (!add_sample_to_timesamples<T>(d, times[i], pv.value(), &err)) {
        if (!err.empty()) {
          _err += err;
        }
        return false;
      }
    } else {
      // Type mismatch - return false to fall back to standard path
      return false;
    }
  }

  return true;
}

// Explicit instantiations for all supported types
// Array types
template bool CrateReader::CrateTypedTimeSamples<std::vector<int32_t>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<uint32_t>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<int64_t>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<uint64_t>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<float>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<double>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<value::half>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<std::string>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<value::matrix2d>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<value::matrix3d>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<value::matrix4d>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);

// Scalar POD types (use PODTimeSamples optimization)
template bool CrateReader::CrateTypedTimeSamples<int32_t>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<uint32_t>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<int64_t>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<uint64_t>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<float>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<double>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<value::half>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<value::matrix2d>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<value::matrix3d>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<value::matrix4d>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
#endif

}  // namespace crate
}  // namespace tinyusdz

#ifdef __clang__
#pragma clang diagnostic pop
#endif
