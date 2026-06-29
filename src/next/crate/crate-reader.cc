// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Crate Reader Implementation

#include "crate-reader-internal.hh"

#include <cstdint>
#include <string>
#include <vector>

namespace tinyusdz {
namespace next {

// ============================================================
// Main value unpacker using switch statement
// ============================================================

bool CrateReader::Impl::UnpackValue(ValueRep rep, Value& out) {
  CrateTypeId type_id = rep.type_id();

  if (rep.is_array()) {
    return UnpackArray(rep, out);
  }

  switch (type_id) {
    case CrateTypeId::Bool: return UnpackBool(rep, out);
    case CrateTypeId::Int: return UnpackInt(rep, out);
    case CrateTypeId::UInt:
    case CrateTypeId::UChar: return UnpackUInt(rep, out);
    case CrateTypeId::Int64: return UnpackInt64(rep, out);
    case CrateTypeId::UInt64: return UnpackUInt64(rep, out);
    case CrateTypeId::Float: return UnpackFloat(rep, out);
    case CrateTypeId::Double: return UnpackDouble(rep, out);
    case CrateTypeId::Token: return UnpackToken(rep, out);
    case CrateTypeId::String: return UnpackString(rep, out);
    case CrateTypeId::AssetPath: return UnpackAssetPath(rep, out);
    case CrateTypeId::Vec2f: return UnpackVec2f(rep, out);
    case CrateTypeId::Vec3f: return UnpackVec3f(rep, out);
    case CrateTypeId::Vec4f: return UnpackVec4f(rep, out);
    case CrateTypeId::Vec2d: return UnpackVec2d(rep, out);
    case CrateTypeId::Vec3d: return UnpackVec3d(rep, out);
    case CrateTypeId::Vec4d: return UnpackVec4d(rep, out);
    case CrateTypeId::Quatf: return UnpackQuatf(rep, out);
    case CrateTypeId::Quatd: return UnpackQuatd(rep, out);
    case CrateTypeId::Matrix2d: return UnpackMatrix2d(rep, out);
    case CrateTypeId::Matrix3d: return UnpackMatrix3d(rep, out);
    case CrateTypeId::Matrix4d: return UnpackMatrix4d(rep, out);
    case CrateTypeId::Specifier: return UnpackSpecifier(rep, out);
    case CrateTypeId::Variability: return UnpackVariability(rep, out);
    case CrateTypeId::TimeCode: return UnpackDouble(rep, out);
    case CrateTypeId::TimeSamples: return UnpackTimeSamples(rep, out);
    case CrateTypeId::Half: return UnpackHalf(rep, out);
    case CrateTypeId::Vec2i: return UnpackVec2i(rep, out);
    case CrateTypeId::Vec3i: return UnpackVec3i(rep, out);
    case CrateTypeId::Vec4i: return UnpackVec4i(rep, out);
    case CrateTypeId::Vec2h: return UnpackVec2h(rep, out);
    case CrateTypeId::Vec3h: return UnpackVec3h(rep, out);
    case CrateTypeId::Vec4h: return UnpackVec4h(rep, out);
    case CrateTypeId::Quath: return UnpackQuath(rep, out);

    case CrateTypeId::ReferenceListOp:
    case CrateTypeId::PayloadListOp: {
      std::vector<std::string> arcs;
      if (!DecodeReferenceListOp(rep, type_id == CrateTypeId::PayloadListOp,
                                 arcs)) {
        return false;
      }
      out = Value::MakeTokenArray(std::move(arcs));
      return true;
    }

    case CrateTypeId::Invalid:     // an empty VtValue (e.g. an empty dict entry)
      out = Value();
      return true;

    case CrateTypeId::ValueBlock:  // a blocked (`= None`) value: a real opinion
      out = Value::MakeBlock();    // that suppresses weaker values and re-emits
      return true;                 // `= None` (not a declared-only attribute).

    // TokenVector / StringVector / DoubleVector: std::vector<T> stored as a
    // non-array value ([u64 count][elements]); an empty vector inlines as
    // payload 0.
    case CrateTypeId::TokenVector:
    case CrateTypeId::StringVector:
      return UnpackTokenOrStringVector(rep, type_id, out);

    case CrateTypeId::DoubleVector:
      return UnpackDoubleVector(rep, out);

    case CrateTypeId::TokenListOp:
    case CrateTypeId::StringListOp: {
      std::vector<std::string> toks;
      if (!DecodeTokenListOp(rep, toks)) return false;
      out = Value::MakeTokenArray(std::move(toks));
      return true;
    }

    case CrateTypeId::Dictionary:
      return DecodeDictionary(rep, out, 0);

    default:
      AddWarning(std::string("Unsupported value type: ") + CrateTypeIdName(type_id));
      return false;
  }
}

}  // namespace next
}  // namespace tinyusdz
