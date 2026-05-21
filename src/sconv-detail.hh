// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Internal header for stage-converter split files.
// Contains template method definitions that must be visible at call sites.
//
#pragma once

#include "crate-writer.hh"
#include "timesamples.hh"  // value::TimeSamples (value-types.hh no longer pulls timesamples.hh transitively)
#include "common-macros.inc"
#include "pprinter.hh"  // For to_string(Specifier), to_string(GeomMesh enums), etc.

namespace tinyusdz {
namespace experimental {

template<typename T>
bool CrateWriter::AddTypedArrayAttribute(
    const char* name, const T& typed_attr,
    crate::FieldValuePairVector& fields) {
  if (typed_attr.has_value()) {
    auto val_opt = typed_attr.get_value();
    if (val_opt) {
      crate::CrateValue crate_val;
      value::Value v(*val_opt);
      std::string conv_err;
      if (!ConvertValue(v, crate_val, &conv_err)) {
        DCOUT("WARNING: Skipping unsupported type for " << name << ": " << conv_err);
        return true;
      }
      fields.push_back({name, crate_val});
    }
  }
  return true;
}

template<typename T>
bool CrateWriter::ExtractAnimatableDefault(
    const Animatable<T>& anim, const char* name,
    crate::FieldValuePairVector& fields, std::string* err) {
  if (anim.has_default()) {
    T val;
    if (anim.get_default(&val)) {
      crate::CrateValue crate_val;
      value::Value v(val);
      if (!ConvertValue(v, crate_val, err)) return false;
      fields.push_back({name, crate_val});
    }
  }
  if (anim.has_timesamples()) {
    const auto& typed_ts = anim.get_timesamples();
    value::TimeSamples ts;
    for (size_t i = 0; i < typed_ts.size(); i++) {
      value::Value v(typed_ts.get_samples()[i].value);
      ts.add_sample(typed_ts.get_samples()[i].t, v);
    }
    crate::CrateValue ts_crate_val;
    ts_crate_val.Set(ts);
    fields.push_back({std::string(name) + ".timeSamples", ts_crate_val});
  }
  return true;
}

} // namespace experimental
} // namespace tinyusdz
