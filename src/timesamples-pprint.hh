// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment Inc.

///
/// @file timesamples-pprint.hh
/// @brief Pretty printing functions for TimeSamples
///

#pragma once

#include <string>
#include <cstdint>

namespace tinyusdz {

// Forward declarations
class StreamWriter;

namespace value {
// Forward declarations
class Value;
struct TimeSamples;
} // namespace value

///
/// Pretty print a single POD value based on type_id
///
/// @param data Pointer to the raw data
/// @param type_id Type ID of the data
/// @return String representation of the value
///
std::string pprint_pod_value_by_type(const uint8_t* data, uint32_t type_id);

///
/// Pretty print a single POD value to a StreamWriter
///
/// @param writer StreamWriter to write to
/// @param data Pointer to the raw data
/// @param type_id Type ID of the data
///
void pprint_pod_value_by_type(StreamWriter& writer,
                              const uint8_t* data,
                              uint32_t type_id);

///
/// Get the element size in bytes for a given type_id
///
/// @param type_id Type ID
/// @return Size in bytes, or 0 if unknown type
///
size_t get_pod_type_size(uint32_t type_id);

///
/// Pretty print a TypedArray stored as a packed pointer
///
/// @param data Pointer to the packed TypedArray pointer (uint64_t)
/// @return String representation of the TypedArray
///
std::string print_typed_array(const uint8_t* data);

///
/// Pretty print non-POD TimeSamples with indentation support
///
/// @param samples value::TimeSamples to print
/// @param indent Indentation level (number of spaces)
/// @return String representation of the time samples
///
std::string pprint_timesamples(const value::TimeSamples& samples,
                               uint32_t indent = 0);

///
/// Pretty print non-POD TimeSamples to a StreamWriter
///
/// @param writer StreamWriter to write to
/// @param samples value::TimeSamples to print
/// @param indent Indentation level (number of spaces)
///
void pprint_timesamples(StreamWriter& writer,
                       const value::TimeSamples& samples,
                       uint32_t indent = 0);

///
/// Set the threshold for using threaded printing of typed array timesamples
/// Only effective when compiled with TINYUSDZ_ENABLE_THREAD
///
/// @param threshold Minimum number of samples to use threading (default: 1024)
///
void set_threaded_print_threshold(size_t threshold);

///
/// Set the number of threads to use for printing typed array timesamples
/// Only effective when compiled with TINYUSDZ_ENABLE_THREAD
///
/// @param num_threads Number of threads (0 = auto-detect from hardware)
///
void set_threaded_print_num_threads(unsigned int num_threads);

} // namespace tinyusdz
