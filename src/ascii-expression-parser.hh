// SPDX-License-Identifier: Apache 2.0
// Copyright 2021-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Expression parsing module for USD ASCII parser
#pragma once

#include <map>
#include <vector>
#include <string>
#include "value-types.hh"
#include "prim-types.hh"
#include "composition.hh"
#include "nonstd/expected.hpp"

namespace tinyusdz {
namespace ascii {

// Forward declarations
class AsciiLexer;
class AsciiTypeParser;

///
/// Expression parser for USD ASCII format.
/// Handles complex expressions like dictionaries, arrays, variants, and metadata.
///
class AsciiExpressionParser {
 public:
  AsciiExpressionParser(AsciiLexer* lexer, AsciiTypeParser* type_parser)
      : _lexer(lexer), _type_parser(type_parser) {}

  // Dictionary parsing
  bool ParseDict(std::map<std::string, MetaVariable>* out_dict);
  bool ParseDictElement(std::string* out_key, MetaVariable* out_value);
  
  // Variant parsing
  bool ParseVariants(VariantSelectionMap* out_map);
  bool ParseVariantsElement(std::string* out_key, std::string* out_value);
  bool ParseVariantSet(const std::string& variantSetName,
                      const std::vector<std::string>& variantNames,
                      std::map<std::string, std::vector<std::pair<ListEditQual, PrimSpec>>>* variantPrimSpecDict);
  
  // Array parsing
  template <typename T>
  bool ParseBasicTypeArray(std::vector<nonstd::optional<T>>* result);
  
  template <typename T>
  bool ParseTypedArray(std::vector<T>* result);
  
  bool ParseStringArray(std::vector<std::string>* result);
  bool ParseTokenArray(std::vector<value::token>* result);
  bool ParsePathArray(std::vector<Path>* result);
  
  // Value parsing
  bool ParseMetaValue(const VariableDef& def, MetaVariable* outvar);
  bool ParseCustomMetaValue();
  bool ParseTimeSampleValue(const uint32_t type_id, value::Value* result);
  bool ParseTimeSampleValue(const std::string& type_name, value::Value* result);
  bool ParseTimeSampleValueOfArrayType(const uint32_t base_type_id,
                                       uint32_t ndims,
                                       value::Value* result);
  bool ParseTimeSampleValueOfArrayType(const std::string& type_name,
                                       value::Value* result);
  
  // ListOp parsing
  template <typename T>
  bool ParseListOp(ListOp<T>* result);
  
  bool ParseListOpHeader(ListEditQual* qual);
  bool ParseListOpItems(ListEditQual qual, std::vector<value::token>* items);
  bool ParseListOpPathItems(ListEditQual qual, std::vector<Path>* items);
  bool ParseListOpStringItems(ListEditQual qual, std::vector<std::string>* items);
  
  // Reference and payload parsing
  bool ParseReference(Reference* out, bool* triple_deliminated);
  bool ParsePayload(Payload* out, bool* triple_deliminated);
  bool ParseAssetIdentifier(value::AssetPath* out, bool* triple_deliminated);
  
  // Interpolation and purpose
  bool ParseInterpolation(Interpolation* interp);
  bool ParsePurpose(Purpose* result);
  
  // TimeSamples parsing
  bool ParseTimeSamples(value::TimeSamples* samples);
  bool ParseTimeSamplesBlock(value::TimeSamples* samples);
  
  // Connection parsing
  bool ParseConnection(std::vector<Path>* paths);
  
  // Error handling
  void PushError(const std::string& msg);
  std::string GetError() const;

 private:
  AsciiLexer* _lexer;
  AsciiTypeParser* _type_parser;
  std::string _err;
  
  // Helper methods
  bool ParseListOpQualifier(ListEditQual* qual);
  bool IsValidDictionaryKey(const std::string& key) const;
  bool ParseNestedExpression(MetaVariable* result);
};

// Template specializations for common array types
extern template bool AsciiExpressionParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<bool>>* result);
extern template bool AsciiExpressionParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<int32_t>>* result);
extern template bool AsciiExpressionParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<float>>* result);
extern template bool AsciiExpressionParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<double>>* result);
extern template bool AsciiExpressionParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::float3>>* result);
extern template bool AsciiExpressionParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::double3>>* result);

}  // namespace ascii
}  // namespace tinyusdz