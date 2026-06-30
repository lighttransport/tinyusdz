// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - private CrateReader implementation details.

#pragma once

#include "crate-reader.hh"

#include "crate-data-source.hh"
#include "stream-reader.hh"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tinyusdz {
namespace next {

class Path;

// TfToken-lite storage for the crate token table. The token section is one
// contiguous run of NUL-separated strings; keeping one blob plus spans avoids a
// per-token std::string allocation while the reader is reconstructing the layer.
class TokenPool {
 public:
  void clear() {
    blob_.clear();
    spans_.clear();
  }
  void reserve(size_t n) { spans_.reserve(n); }
  void push(const char* begin, size_t len) {
    spans_.push_back(Span{static_cast<uint32_t>(blob_.size()),
                          static_cast<uint32_t>(len)});
    blob_.append(begin, len);
  }
  size_t size() const { return spans_.size(); }
  bool empty() const { return spans_.empty(); }
  std::string_view view(size_t i) const {
    const Span& s = spans_[i];
    return std::string_view(blob_.data() + s.off, s.len);
  }
  std::string str(size_t i) const {
    return std::string(view(i));
  }
  std::vector<std::string> to_vector() const {
    std::vector<std::string> out;
    out.reserve(spans_.size());
    for (size_t i = 0; i < spans_.size(); ++i) {
      out.emplace_back(view(i));
    }
    return out;
  }

 private:
  struct Span {
    uint32_t off;
    uint32_t len;
  };
  std::string blob_;
  std::vector<Span> spans_;
};

class CrateReader::Impl {
 public:
  explicit Impl(const CrateReadOptions& options) : options_(options) {}

  CrateReadResult Read(const uint8_t* data, size_t size);
  CrateReadResult ReadBorrowed(const uint8_t* data, size_t size);
  CrateReadResult ReadOwned(std::string&& owned);
  CrateReadResult ReadFile(const char* filename);

  std::vector<std::string> tokens() const { return tokens_.to_vector(); }
  const std::vector<std::string>& paths() const { return paths_; }
  const std::vector<CrateField>& fields() const { return fields_; }
  const std::vector<CrateSpec>& specs() const { return specs_; }
  const std::vector<uint32_t>& fieldset_indices() const {
    return fieldset_indices_;
  }

 private:
  struct ArrayScratch {
    std::vector<uint8_t> compressed_data;
    std::vector<uint32_t> u32_indices;
    std::vector<uint8_t> u8_values;
    std::vector<uint64_t> u64_values;
    std::vector<uint16_t> half_values;
    std::vector<float> float_values;
    std::vector<double> double_values;
  };

  CrateReadOptions options_;
  std::unique_ptr<StreamReader> reader_;
  std::shared_ptr<CrateDataSource> source_;
  CrateReadResult result_;

  // Reusable scratch buffers shared by hot paths (array/lookup decoding) to
  // reduce repeated allocations while building layers for large scenes.
  ArrayScratch array_scratch_;

  CrateVersion version_;
  CrateTOC toc_;
  TokenPool tokens_;
  std::vector<uint32_t> string_indices_;
  std::vector<CrateField> fields_;
  std::vector<uint32_t> fieldset_indices_;
  std::vector<uint32_t> fieldset_offsets_;
  std::vector<uint32_t> fieldset_counts_;
  std::vector<CrateSpec> specs_;
  std::vector<uint32_t> fieldset_index_to_id_;
  std::vector<std::string> paths_;

  CrateReadResult ReadFromString(std::string&& bytes);
  CrateReadResult ParseFromSource();

  bool ReadBootstrap();
  bool ReadTOC();
  bool ReadTokens();
  bool ReadStrings();
  bool ReadFields();
  bool ReadFieldsets();
  bool ReadSpecs();
  bool ReadPaths();
  bool BuildStage();

  bool UnpackValue(ValueRep rep, Value& out);
  bool UnpackArray(ValueRep rep, Value& out);

  bool UnpackBool(ValueRep rep, Value& out);
  bool UnpackInt(ValueRep rep, Value& out);
  bool UnpackUInt(ValueRep rep, Value& out);
  bool UnpackInt64(ValueRep rep, Value& out);
  bool UnpackUInt64(ValueRep rep, Value& out);
  bool UnpackFloat(ValueRep rep, Value& out);
  bool UnpackDouble(ValueRep rep, Value& out);
  bool UnpackToken(ValueRep rep, Value& out);
  bool UnpackString(ValueRep rep, Value& out);
  bool UnpackAssetPath(ValueRep rep, Value& out);
  bool UnpackVec2f(ValueRep rep, Value& out);
  bool UnpackVec3f(ValueRep rep, Value& out);
  bool UnpackVec4f(ValueRep rep, Value& out);
  bool UnpackVec2d(ValueRep rep, Value& out);
  bool UnpackVec3d(ValueRep rep, Value& out);
  bool UnpackVec4d(ValueRep rep, Value& out);
  bool UnpackQuatf(ValueRep rep, Value& out);
  bool UnpackQuatd(ValueRep rep, Value& out);
  bool UnpackMatrix2d(ValueRep rep, Value& out);
  bool UnpackMatrix3d(ValueRep rep, Value& out);
  bool UnpackMatrix4d(ValueRep rep, Value& out);
  bool UnpackSpecifier(ValueRep rep, Value& out);
  bool UnpackVariability(ValueRep rep, Value& out);
  bool UnpackTimeSamples(ValueRep rep, Value& out);
  bool DecodeTimeSamples(ValueRep rep,
                         std::vector<std::pair<double, Value>>* out);
  bool UnpackTokenOrStringVector(ValueRep rep, CrateTypeId type_id, Value& out);
  bool UnpackDoubleVector(ValueRep rep, Value& out);
  bool UnpackVec2i(ValueRep rep, Value& out);
  bool UnpackVec3i(ValueRep rep, Value& out);
  bool UnpackVec4i(ValueRep rep, Value& out);
  bool UnpackHalf(ValueRep rep, Value& out);
  bool UnpackVec2h(ValueRep rep, Value& out);
  bool UnpackVec3h(ValueRep rep, Value& out);
  bool UnpackVec4h(ValueRep rep, Value& out);
  bool UnpackQuath(ValueRep rep, Value& out);

  bool CheckByteAllocation(uint64_t bytes, const char* what);
  bool CheckElementAllocation(uint64_t count, size_t elem_size,
                              const char* what);
  bool GetToken(uint32_t index, std::string_view* out) const;
  bool GetToken(uint32_t index, std::string& out);
  bool GetString(uint32_t index, std::string_view* out) const;
  bool GetString(uint32_t index, std::string& out);
  bool ResolveFieldset(uint32_t fieldset_index,
                       std::vector<std::pair<std::string_view, Value>>& out);
  bool ResolveFieldset(uint32_t fieldset_index,
                       std::vector<std::pair<std::string, Value>>& out);
  bool ResolveFieldsetRaw(uint32_t fieldset_index,
                         std::vector<std::pair<std::string, ValueRep>>& out);
  bool ResolveFieldsetRaw(uint32_t fieldset_index,
                         std::vector<std::pair<std::string_view, ValueRep>>& out);
  bool DecodePathTargets(ValueRep rep, std::vector<std::string>& out);
  bool DecodePathTargets(ValueRep rep, std::vector<Path>& out);
  bool DecodeReferenceListOp(ValueRep rep, bool is_payload,
                             std::vector<std::string>& out);
  bool DecodeVariantSelectionMap(
      ValueRep rep, std::vector<std::pair<std::string, std::string>>& out);
  bool DecodeVariantSelectionMap(
      ValueRep rep,
      std::vector<std::pair<std::string_view, std::string_view>>* out);
  bool DecodeTokenListOp(ValueRep rep, std::vector<std::string>& out);
  bool DecodeDictionary(ValueRep rep, Value& out, int depth);

  void AddError(const std::string& msg);
  void AddWarning(const std::string& msg);
};

}  // namespace next
}  // namespace tinyusdz
