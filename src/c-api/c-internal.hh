// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Shared internals for the TinyUSDZ C API implementation.

#pragma once

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "tinyusdz-c.h"

#include "next/stage/stage.hh"
#include "next/types/value.hh"

// ============================================================
// Handle definitions (opaque in the public header)
// ============================================================

struct tusd_string {
  std::string s;
};

struct tusd_strlist {
  std::vector<std::string> items;
};

struct tusd_value {
  tinyusdz::next::Value v;
};

struct tusd_stage {
  tinyusdz::next::Stage stage;
  std::string warnings;
  // Bumped by structural mutations (define/remove prim) so bindings can
  // detect stale tusd_prim handles.
  std::atomic<uint64_t> generation{0};
};

namespace tusd_internal {

// Thread-local error message backing tusd_last_error().
void SetError(const std::string& msg);
void SetError(const char* msg);

// Convenience: set the error and return the status in one expression.
inline tusd_status Fail(tusd_status st, const std::string& msg) {
  SetError(msg);
  return st;
}

// tusd_prim <-> next::UsdPrim (both are {spec, layer, index} triples).
inline tinyusdz::next::UsdPrim FromC(tusd_prim p) {
  return tinyusdz::next::UsdPrim(
      static_cast<const tinyusdz::next::PrimSpec*>(p._spec),
      static_cast<const tinyusdz::next::Layer*>(p._layer), p._index);
}

inline tusd_prim ToC(const tinyusdz::next::UsdPrim& up) {
  tusd_prim p;
  p._spec = up.GetPrimSpec();
  p._layer = up.GetLayer();
  p._index = up.GetIndex();
  p._pad = 0;
  return p;
}

inline tusd_sv SV(const std::string& s) {
  tusd_sv v;
  v.data = s.c_str();
  v.len = s.size();
  return v;
}

inline tusd_sv EmptySV() {
  tusd_sv v;
  v.data = "";
  v.len = 0;
  return v;
}

// Build a borrowed zero-copy view from a Value. Materializes lazy arrays
// (serialized by an internal mutex). String-family / dictionary / token-array
// values yield data == NULL with storage == TUSD_COMP_NONE.
tusd_status MakeView(const tinyusdz::next::Value& v, tusd_value_view* out);

// Build a next::Value from raw (type, is_array, data, count) as documented on
// tusd_attr_set. Returns an empty Value and sets the error on failure.
bool ValueFromRaw(tusd_type type, uint8_t is_array, const void* data,
                  size_t count, tinyusdz::next::Value* out);

}  // namespace tusd_internal
