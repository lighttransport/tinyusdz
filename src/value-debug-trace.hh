// SPDX-License-Identifier: Apache 2.0
// Debug utility for tracing Value type_id corruption through parser pipeline

#pragma once

#include <iostream>
#include <iomanip>
#include <cstring>

#if defined(TUSDZ_NEW_32BYTE_VALUE) || defined(TUSDZ_NEW_VALUE_TYPE)

namespace tinyusdz {
namespace value {

// Debug trace helper to log Value type_id at specific points
inline void DebugTraceValue(const char* location, const char* context, const Value& v) {
  std::cerr << "[DEBUG] " << location << ": " << context
            << " | type_id=" << v.type_id()
            << " (" << v.type_name() << ")"
            << std::endl;
}

inline void DebugTraceValuePtr(const char* location, const char* context, const Value* v) {
  if (v) {
    DebugTraceValue(location, context, *v);
  } else {
    std::cerr << "[DEBUG] " << location << ": " << context << " | NULL pointer" << std::endl;
  }
}

// Trace when PrimVar is accessed for type checking
inline void DebugTracePrimVar(const char* location, const char* context, const primvar::PrimVar& pv) {
  std::cerr << "[DEBUG] " << location << ": " << context
            << " | type_id=" << pv.type_id()
            << " (" << pv.type_name() << ")"
            << " | has_value=" << pv.has_value()
            << " | has_ts=" << pv.has_timesamples()
            << std::endl;
}

} // namespace value
} // namespace tinyusdz

#define DEBUG_TRACE_VALUE(location, context, v) \
  tinyusdz::value::DebugTraceValue(location, context, v)

#define DEBUG_TRACE_VALUE_PTR(location, context, v) \
  tinyusdz::value::DebugTraceValuePtr(location, context, v)

#define DEBUG_TRACE_PRIMVAR(location, context, pv) \
  tinyusdz::value::DebugTracePrimVar(location, context, pv)

#else // !TUSDZ_NEW_32BYTE_VALUE && !TUSDZ_NEW_VALUE_TYPE

// For OLD implementation, disable tracing
#define DEBUG_TRACE_VALUE(location, context, v) do {} while(0)
#define DEBUG_TRACE_VALUE_PTR(location, context, v) do {} while(0)
#define DEBUG_TRACE_PRIMVAR(location, context, pv) do {} while(0)

#endif // TUSDZ_NEW_32BYTE_VALUE || TUSDZ_NEW_VALUE_TYPE
