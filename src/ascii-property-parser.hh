// SPDX-License-Identifier: Apache 2.0
// Copyright 2021-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Property and attribute parsing module for USD ASCII parser
#pragma once

#include <map>
#include <string>
#include <vector>
#include "prim-types.hh"
#include "composition.hh"
#include "nonstd/expected.hpp"

namespace tinyusdz {
namespace ascii {

// Forward declarations
class AsciiLexer;
class AsciiTypeParser;
class AsciiExpressionParser;

///
/// Property and attribute parser for USD ASCII format.
/// Handles parsing of prim properties, attributes, relationships, and metadata.
///
class AsciiPropertyParser {
 public:
  AsciiPropertyParser(AsciiLexer* lexer, 
                     AsciiTypeParser* type_parser,
                     AsciiExpressionParser* expr_parser)
      : _lexer(lexer), _type_parser(type_parser), _expr_parser(expr_parser) {}

  // Property parsing
  bool ParseProperties(std::map<std::string, Property>* props,
                      std::vector<value::token>* propNames);
  
  bool ParsePrimProps(std::map<std::string, Property>* props,
                     std::vector<value::token>* propNames,
                     const std::string& closing_brace);
  
  // Attribute parsing
  bool ParseBasicPrimAttr(bool array_qual,
                         const std::string& type_name,
                         const std::string& name,
                         bool custom,
                         std::map<std::string, Property>* props,
                         std::vector<value::token>* propNames);
  
  bool ParseAttrMeta(AttrMeta* out_meta);
  
  // Relationship parsing
  bool ParseRelationship(Relationship* result);
  bool ParseRelationshipTarget(Path* target);
  bool ParseRelationshipTargetList(std::vector<Path>* targets);
  
  // Metadata parsing
  bool ParsePrimMetas(PrimMetaMap* args);
  bool ParseStageMeta(std::pair<ListEditQual, MetaVariable>* out);
  bool ParseStageMetaOpt();
  bool ParseStageMetas();
  
  // Variant and VariantSet parsing
  bool ParseVariantStatement(std::string* variantSetName,
                            std::string* variantName);
  
  // Connection parsing
  bool ParseConnection(const std::string& attr_name,
                      std::vector<Path>* connections);
  
  // TimeSamples parsing for properties
  bool ParseTimeSamplesForProperty(const std::string& prop_name,
                                  value::TimeSamples* samples);
  
  // Interpolation metadata
  bool ParseInterpolationMeta(Interpolation* interp);
  
  // Custom data parsing
  bool ParseCustomData(value::dict* customData);
  bool ParseCustomLayerData(std::map<std::string, MetaVariable>* customLayerData);
  
  // Inherits, references, payloads, specializes
  bool ParseInherits(std::vector<Path>* paths);
  bool ParseReferences(std::vector<Reference>* refs);
  bool ParsePayloads(std::vector<Payload>* payloads);
  bool ParseSpecializes(std::vector<Path>* paths);
  
  // API schemas
  bool ParseApiSchemas(ListOp<value::token>* schemas);
  
  // Kind metadata
  bool ParseKind(Kind* kind);
  
  // Purpose metadata
  bool ParsePurpose(Purpose* purpose);
  
  // Documentation and comment
  bool ParseDocumentation(value::StringData* doc);
  bool ParseComment(value::StringData* comment);
  
  // Visibility
  bool ParseVisibility(Visibility* vis);
  
  // Active/instanceable state
  bool ParseActive(bool* active);
  bool ParseInstanceable(bool* instanceable);
  
  // Property-specific metadata
  bool ParsePropertyAllowedTokens(std::vector<value::token>* tokens);
  bool ParsePropertyColorSpace(value::token* colorSpace);
  bool ParsePropertyDefault(const std::string& type_name, value::Value* defaultValue);
  bool ParsePropertyConnectability(value::token* connectability);
  bool ParsePropertyDisplayGroup(std::string* displayGroup);
  bool ParsePropertyDisplayName(std::string* displayName);
  
  // Utilities
  bool IsPropertyKeyword(const std::string& str) const;
  bool IsMetadataKeyword(const std::string& str) const;
  
  // Error handling
  void PushError(const std::string& msg);
  std::string GetError() const { return _err; }

 private:
  AsciiLexer* _lexer;
  AsciiTypeParser* _type_parser;
  AsciiExpressionParser* _expr_parser;
  std::string _err;
  
  // Helper methods
  bool ParsePropertyQualifiers(bool* uniform, bool* custom, bool* variability);
  bool ParsePropertyType(std::string* type_name, bool* isArray);
  bool ParsePropertyName(std::string* name);
  bool ParsePropertyMetadata(const std::string& prop_name, AttrMeta* meta);
  bool ValidatePropertyType(const std::string& type_name) const;
};

// Property metadata structure
struct PropertyMeta {
  nonstd::optional<value::Value> defaultValue;
  nonstd::optional<value::token> colorSpace;
  nonstd::optional<std::vector<value::token>> allowedTokens;
  nonstd::optional<value::token> connectability;
  nonstd::optional<std::string> displayGroup;
  nonstd::optional<std::string> displayName;
  nonstd::optional<value::StringData> doc;
  nonstd::optional<Interpolation> interpolation;
  bool hidden{false};
  bool custom{false};
};

}  // namespace ascii
}  // namespace tinyusdz