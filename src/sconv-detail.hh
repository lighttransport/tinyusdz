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

namespace sconv_detail {

// Emit an attribute's OWN metadata block as `<name>.<key>` fields. The field
// router in ConvertSinglePrim (stage-converter.cc, kAttrMetaSuffixes) recognises
// that spelling and folds each one into the attribute's spec.
//
// The TYPED writers build their fields by hand and never emitted any of this, so
// an attribute's customData / comment / colorSpace / ... were dropped on write
// even though the generic attribute path (ConvertAttributeToFields) has handled
// them all along.
inline void EmitAttrMetas(const std::string &name, const AttrMeta &metas,
                          crate::FieldValuePairVector &fields) {
  if (metas.has_customData()) {
    crate::CrateValue v;
    v.Set(metas.get_customData());
    fields.push_back({name + ".customData", v});
  }

  if (metas.has_comment()) {
    crate::CrateValue v;
    v.Set(metas.get_comment().value);
    fields.push_back({name + ".comment", v});
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

  // Bare string(s) in the metadata block (`double radius = 1.2 ( """muda""" )`).
  // Crate has no standard field for these -- the ASCII parser parks them in
  // AttrMeta::stringData -- so emit our own `stringData` field, which the crate
  // reader (usdc-reader-property.cc) puts straight back. Only the values need to
  // travel: to_string(StringData) re-derives the quoting from the value.
  if (!metas.stringData.empty()) {
    std::vector<std::string> strs;
    for (const auto &sd : metas.stringData) {
      strs.push_back(sd.value);
    }
    crate::CrateValue v;
    v.Set(strs);
    fields.push_back({name + ".stringData", v});
  }
}

// Emit a DECLARATION-ONLY attribute (`double radius`, no value).
//
// An attribute is `authored()` the moment it is DECLARED, with or without a
// value, and TypedAttributeWithFallback::get_value() silently returns the SCHEMA
// FALLBACK when no value was authored -- so a writer that calls get_value()
// straight off authored() invents a value the author never wrote (`double radius`
// came back as `double radius = 2`). That is not cosmetic: an authored opinion is
// a STRONG opinion, so a fabricated one WINS over the weaker opinions it should
// have deferred to during composition.
//
// The `.typeName` suffix is understood by the field router in ConvertSinglePrim
// (stage-converter.cc), which needs it because it otherwise infers the spec's
// type from the default value -- and here there is none.
inline void EmitAttrDeclaration(const std::string &name, const std::string &type_name,
                         crate::FieldValuePairVector &fields) {
  crate::CrateValue v;
  v.Set(value::token(type_name));
  fields.push_back({name + ".typeName", v});
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

template <typename T>
bool CrateWriter::EmitTypedAnimatableAttr(
    const char *name, const TypedAttributeWithFallback<Animatable<T>> &attr,
    crate::FieldValuePairVector &fields, std::string *err) {
  if (attr.authored()) {
    if (attr.is_value_empty()) {
      // DECLARED with no value. Emitting attr.get_value() here would emit the
      // schema fallback -- see EmitAttrDeclaration. The type comes from T, so a
      // call site cannot declare the wrong one.
      sconv_detail::EmitAttrDeclaration(name, value::TypeTraits<T>::type_name(), fields);
    } else if (!ExtractAnimatableDefault(attr.get_value(), name, fields, err)) {
      return false;
    }
    sconv_detail::EmitAttrMetas(name, attr.metas(), fields);
  }

  // A connection is authored independently of a value, so this sits outside the
  // authored() branch -- `double radius.connect = </x.y>` with no value at all.
  sconv_detail::EmitAttrConnections(name, attr, fields);

  return true;
}

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


} // namespace experimental
} // namespace tinyusdz
