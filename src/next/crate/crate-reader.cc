// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Crate Reader Implementation

#include "crate-reader-internal.hh"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace tinyusdz {
namespace next {

// ============================================================
// Main value unpacker using switch statement
// ============================================================

bool CrateReader::Impl::UnpackValue(ValueRep rep, Value& out) {
  // VtArrayEdit reps (crate 0.14): not supported — without this check the
  // rep masquerades as a scalar of the element type and reads the edit
  // tuple header as the value (silent corruption).
  if (rep.is_array_edit()) {
    AddWarning("VtArrayEdit values are not supported; value dropped");
    return false;
  }

  CrateTypeId type_id = rep.type_id();

  if (rep.is_array()) {
    return UnpackArray(rep, out);
  }

  switch (type_id) {
    case CrateTypeId::Bool: return UnpackBool(rep, out);
    case CrateTypeId::Int: return UnpackInt(rep, out);
    case CrateTypeId::UInt: return UnpackUInt(rep, out);
    case CrateTypeId::UChar: {
      // Keep the uchar type identity (was mutated to uint).
      if (!UnpackUInt(rep, out)) return false;
      if (const uint32_t* u = out.as_uint()) {
        const uint8_t b = static_cast<uint8_t>(*u & 0xFFu);
        out = Value::MakeFromRaw(TypeId::UChar, &b);
      }
      return true;
    }
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
    case CrateTypeId::Permission: return UnpackPermission(rep, out);
    case CrateTypeId::Variability: return UnpackVariability(rep, out);
    case CrateTypeId::TimeCode: {
      // Decode like a double but preserve the TimeCode type identity
      // (a crate rewrite must not silently mutate timecode -> double).
      if (!UnpackDouble(rep, out)) return false;
      if (const double* d = out.as_double()) {
        out = Value::MakeFromRaw(TypeId::TimeCode, d);
      }
      return true;
    }
    case CrateTypeId::TimeSamples: return UnpackTimeSamples(rep, out);
    case CrateTypeId::UnregisteredValue: {
      if (rep.is_inlined() || rep.payload() == 0) return false;
      const uint64_t wrapper = rep.payload_as_offset();
      if (!reader_->seek(static_cast<size_t>(wrapper))) return false;
      int64_t relative = 0;
      if (!reader_->read_i64(relative) || relative < 0) return false;
      uint64_t nested_pos = 0;
      if (static_cast<uint64_t>(relative) >
              (std::numeric_limits<uint64_t>::max)() - wrapper) {
        return false;
      }
      nested_pos = wrapper + static_cast<uint64_t>(relative);
      if (!reader_->seek(static_cast<size_t>(nested_pos))) {
        return false;
      }
      uint64_t nested_raw = 0;
      if (!reader_->read_u64(nested_raw)) return false;
      const ValueRep nested(nested_raw);
      // OpenUSD stores the authored source spelling as a nested String (and
      // may use Dictionary for registered dictionary-shaped extensions).
      if (nested.type_id() != CrateTypeId::String &&
          nested.type_id() != CrateTypeId::Dictionary) {
        return false;
      }
      return UnpackValue(nested, out);
    }
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

    // std::vector<SdfLayerOffset>: [u64 count][f64 offset, f64 scale]*count.
    // Surfaced as a flat double array (pairs); BuildStage pairs it with
    // subLayers into LayerMeta::subLayerOffsets.
    case CrateTypeId::LayerOffsetVector: {
      if (rep.payload() == 0) {
        out = Value::MakeDoubleArray(std::vector<double>());
        return true;
      }
      if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
      uint64_t n = 0;
      if (!reader_->read_u64(n)) return false;
      if (n > options_.max_array_elements) return false;
      if (!reader_->has_elements(static_cast<size_t>(n), 16)) return false;
      std::vector<double> vals(static_cast<size_t>(n) * 2);
      if (n && !reader_->read(vals.data(), vals.size() * sizeof(double))) return false;
      out = Value::MakeDoubleArray(std::move(vals));
      return true;
    }

    // Path-valued fields: inheritPaths / specializes arcs (PathListOp) and
    // raw path vectors. Flattened to a token array of path strings; BuildStage
    // routes them into PrimSpecMeta.
    case CrateTypeId::PathVector:
    case CrateTypeId::PathListOp: {
      std::vector<std::string> paths;
      if (!DecodePathTargets(rep, paths, /*with_markers=*/true)) return false;
      // Arc-string form ("</Base>"), matching what the usda parser stores in
      // PrimSpecMeta::inherits/specializes. Sublist markers ("\x01?") pass
      // through unbracketed for BuildStage's list-op reconstruction.
      for (std::string& p : paths) {
        if (!p.empty() && p[0] != '\x01') p = "<" + p + ">";
      }
      out = Value::MakeTokenArray(std::move(paths));
      return true;
    }

    // Single (non-listOp) SdfPayload item: [u32 assetIdx][u32 pathIdx]
    // (+ [f64 offset][f64 scale] from crate 0.8). Common in pxr-written 0.8.x
    // crates for `payload = @asset@</P>`.
    case CrateTypeId::Payload: {
      if (rep.payload() == 0) {
        out = Value::MakeTokenArray(std::vector<std::string>());
        return true;
      }
      if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
      uint32_t asset_idx = 0, path_idx = 0;
      if (!reader_->read_u32(asset_idx) || !reader_->read_u32(path_idx)) {
        return false;
      }
      double offset = 0.0, scale = 1.0;
      if (version_.minor >= 8) {
        if (!reader_->read_f64(offset) || !reader_->read_f64(scale)) {
          return false;
        }
      }
      std::string asset;
      GetString(asset_idx, asset);
      std::string prim = (path_idx < paths_.size()) ? paths_[path_idx] : "";
      // Internal arcs (no asset) render as "</Prim>", matching the usda parser.
      std::string arc;
      if (!asset.empty()) arc = "@" + asset + "@";
      if (!prim.empty() && prim != "/") arc += "<" + prim + ">";
      if (offset != 0.0 || scale != 1.0) {
        arc += "?layerOffset=" + std::to_string(offset) + ":" +
               std::to_string(scale);
      }
      out = Value::MakeTokenArray({std::move(arc)});
      return true;
    }

    case CrateTypeId::TokenListOp:
    case CrateTypeId::StringListOp: {
      std::vector<std::string> toks;
      if (!DecodeTokenListOp(rep, toks)) return false;
      out = Value::MakeTokenArray(std::move(toks));
      return true;
    }

    case CrateTypeId::PathExpression: {
      // SdfPathExpression (crate >= 0.10): the expression text as a u32
      // string index in the value stream.
      if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) {
        return false;
      }
      uint32_t sidx = 0;
      if (!reader_->read_u32(sidx)) return false;
      std::string text;
      GetString(sidx, text);
      out = Value::MakeStringLike(text, TypeId::PathExpression);
      return true;
    }

    case CrateTypeId::Dictionary:
      return DecodeDictionary(rep, out, 0);

    case CrateTypeId::Relocates: {
      // SdfRelocates (crate >= 0.11): [u64 count][(u32 src, u32 dst)*].
      // Surface as a flat token array of [src, dst, ...] pairs; the stage
      // builder folds them into PrimSpecMeta::relocates.
      if (rep.payload() == 0) { out = Value::MakeTokenArray({}); return true; }
      if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) {
        return false;
      }
      uint64_t n = 0;
      if (!reader_->read_u64(n)) return false;
      if (n > options_.max_array_elements) return false;
      std::vector<std::string> pairs;
      pairs.reserve(static_cast<size_t>(n) * 2);
      for (uint64_t i = 0; i < n; ++i) {
        uint32_t src = 0, dst = 0;
        if (!reader_->read_u32(src) || !reader_->read_u32(dst)) return false;
        if (src >= paths_.size() || dst >= paths_.size()) return false;
        pairs.push_back(paths_[src]);
        pairs.push_back(paths_[dst]);
      }
      out = Value::MakeTokenArray(std::move(pairs));
      return true;
    }

    default:
      AddWarning(std::string("Unsupported value type: ") + CrateTypeIdName(type_id));
      return false;
  }
}

}  // namespace next
}  // namespace tinyusdz
