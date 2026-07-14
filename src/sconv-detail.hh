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
    // Value-type Animatable stores a type-erased value::TimeSamples directly.
    if (const value::TimeSamples *tsp = anim.get_timesamples_ptr()) {
      crate::CrateValue ts_crate_val;
      ts_crate_val.Set(*tsp);
      fields.push_back({std::string(name) + ".timeSamples", ts_crate_val});
    }
  }
  return true;
}

namespace sconv_detail {

// Emit an attribute's OWN metadata block as `<name>.<key>` fields. The field
// router in ConvertSinglePrim (stage-converter.cc, kAttrMetaSuffixes) recognises
// that spelling and folds each one into the attribute's spec.
//
// The TYPED writers build their fields by hand and never emitted any of this, so
// an attribute's comment / customData / colorSpace / ... were dropped on write
// even though the generic attribute path (ConvertAttributeToFields) has handled
// them all along.
inline void EmitAttrMetas(const std::string &name, const AttrMeta &metas,
                          crate::FieldValuePairVector &fields) {
  // A bare string in an attribute's metadata block (`double x = 1 ( """m""" )`)
  // IS the comment in USD -- the two ASCII spellings are one Sdf field, and only
  // the ASCII parser knows which was used: it parks the bare form in
  // AttrMeta::stringData and the `comment = ...` form in AttrMeta::comment.
  //
  // Emit exactly ONE `comment` field: two would corrupt the fieldset encoding.
  if (metas.has_comment() || !metas.stringData.empty()) {
    std::string comment_str;
    if (metas.has_comment()) {
      comment_str = metas.get_comment().value;
    } else {
      for (size_t i = 0; i < metas.stringData.size(); i++) {
        if (i > 0) comment_str += "\n";
        comment_str += metas.stringData[i].value;
      }
    }
    crate::CrateValue v;
    v.Set(comment_str);
    fields.push_back({name + ".comment", v});
  }

  if (metas.has_customData()) {
    crate::CrateValue v;
    v.Set(metas.get_customData());
    fields.push_back({name + ".customData", v});
  }

  if (metas.has_colorSpace()) {
    crate::CrateValue v;
    v.Set(metas.get_colorSpace());
    fields.push_back({name + ".colorSpace", v});
  }

  if (metas.has_displayName()) {
    crate::CrateValue v;
    v.Set(metas.get_displayName());
    fields.push_back({name + ".displayName", v});
  }

  if (metas.has_doc()) {
    crate::CrateValue v;
    v.Set(metas.get_doc().value);
    fields.push_back({name + ".documentation", v});
  }
}

// Emit `<name>.connect` for a typed attribute that carries connection targets.
// USD lets ANY attribute be connected -- not just shader inputs -- and the ASCII
// reader stores those targets on the TypedAttribute itself. The typed writers
// only ever emitted the value, so `double size.connect = </bora.value>` on a
// Cube was dropped. The field router in ConvertSinglePrim (stage-converter.cc)
// recognizes the `.connect` suffix and folds it into the attribute spec's
// `connectionPaths`.
template <typename T>
void EmitAttrConnections(const std::string &name, const T &attr,
                         crate::FieldValuePairVector &fields) {
  if (!attr.has_connections()) {
    return;
  }

  ListOp<Path> conn_listop;
  conn_listop.ClearAndMakeExplicit();
  conn_listop.SetExplicitItems(attr.connections());

  crate::CrateValue v;
  v.Set(conn_listop);
  fields.push_back({name + ".connect", v});
}

} // namespace sconv_detail

} // namespace experimental
} // namespace tinyusdz
