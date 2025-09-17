// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Property parsing and reading utilities for USDC
// Extracted from usdc-reader.cc

#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include "value-types.hh"
#include "prim-types.hh"
#include "crate-reader.hh"
#include "nonstd/expected.hpp"

namespace tinyusdz {
namespace usdc {

// Forward declarations
class Property;
class Attribute;
class Relationship;
struct AttrMeta;
struct RelMeta;

// Property parsing configuration
struct PropertyParseConfig {
  bool allow_custom_properties = true;
  bool strict_type_checking = false;
  bool resolve_value_blocks = true;
  size_t max_array_size = 100000000;  // 100M elements max
  size_t max_string_length = 10000000;  // 10MB strings max
};

// Property parser class
class PropertyParser {
public:
  PropertyParser(const crate::CrateReader &reader,
                const PropertyParseConfig &config = {});
  
  // Parse property from field value pairs
  bool ParseProperty(
      const SpecType specType,
      const std::string &propName,
      const crate::FieldValuePairVector &fields,
      const PathIndexToSpecIndexMap &pathIndexToSpecIndexMap,
      Property &prop,
      std::string *warn,
      std::string *err);
  
  // Parse attribute
  bool ParseAttribute(
      const std::string &attrName,
      const crate::FieldValuePairVector &fields,
      Attribute &attr,
      std::string *err);
  
  // Parse relationship
  bool ParseRelationship(
      const std::string &relName,
      const crate::FieldValuePairVector &fields,
      Relationship &rel,
      std::string *err);
  
  // Build property map from path indices
  bool BuildPropertyMap(
      const std::vector<size_t> &pathIndices,
      const PathIndexToSpecIndexMap &pathIndexToSpecIndexMap,
      PropertyMap &props,
      std::string *err);
  
  // Parse metadata
  bool ParseAttrMeta(
      const crate::FieldValuePairVector &fields,
      AttrMeta &meta,
      std::string *err);
  
  bool ParseRelMeta(
      const crate::FieldValuePairVector &fields,
      RelMeta &meta,
      std::string *err);
  
private:
  const crate::CrateReader &reader_;
  PropertyParseConfig config_;
  
  // Helper methods
  bool ParseTypeName(const value::Value &val, std::string &typeName);
  bool ParseVariability(const value::Value &val, Variability &var);
  bool ParseInterpolation(const value::Value &val, Interpolation &interp);
  bool ParseConnectionPaths(const value::Value &val, std::vector<Path> &paths);
  bool ParseTimeSamples(const value::Value &val, value::TimeSamples &samples);
  bool ParseDefault(const value::Value &val, value::Value &defaultVal);
  bool ParseCustomData(const value::Value &val, value::dict &customData);
};

// Property value reading utilities
namespace property_utils {

// Value extraction helpers
template<typename T>
bool ExtractValue(const Property &prop, T &value, double time = value::TimeCode::Default()) {
  if (auto attr = prop.as<Attribute>()) {
    return attr->get_value(&value, time);
  }
  return false;
}

// Specialized extractors for common types
bool ExtractBool(const Property &prop, bool &value, double time = value::TimeCode::Default());
bool ExtractInt(const Property &prop, int &value, double time = value::TimeCode::Default());
bool ExtractFloat(const Property &prop, float &value, double time = value::TimeCode::Default());
bool ExtractDouble(const Property &prop, double &value, double time = value::TimeCode::Default());
bool ExtractString(const Property &prop, std::string &value, double time = value::TimeCode::Default());
bool ExtractToken(const Property &prop, value::token &value, double time = value::TimeCode::Default());
bool ExtractFloat3(const Property &prop, value::float3 &value, double time = value::TimeCode::Default());
bool ExtractMatrix4d(const Property &prop, value::matrix4d &value, double time = value::TimeCode::Default());

// Array extractors
bool ExtractFloatArray(const Property &prop, std::vector<float> &values, double time = value::TimeCode::Default());
bool ExtractIntArray(const Property &prop, std::vector<int> &values, double time = value::TimeCode::Default());
bool ExtractStringArray(const Property &prop, std::vector<std::string> &values, double time = value::TimeCode::Default());

// Time sample extraction
bool ExtractTimeSamples(const Property &prop, value::TimeSamples &samples);
bool GetSampleTimes(const Property &prop, std::vector<double> &times);

// Connection and relationship utilities
bool ExtractConnections(const Property &prop, std::vector<Path> &connections);
bool ExtractTargets(const Property &prop, std::vector<Path> &targets);

// Metadata extraction
bool ExtractDisplayName(const Property &prop, std::string &displayName);
bool ExtractDisplayGroup(const Property &prop, std::string &displayGroup);
bool ExtractDocString(const Property &prop, std::string &doc);
bool ExtractHidden(const Property &prop, bool &hidden);

// Property validation
enum class PropertyValidationLevel {
  None,      // No validation
  Basic,     // Type checking only
  Strict,    // Full schema validation
  Custom     // User-defined validation
};

struct PropertyValidationResult {
  bool valid = true;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
};

PropertyValidationResult ValidateProperty(
    const Property &prop,
    PropertyValidationLevel level = PropertyValidationLevel::Basic);

// Property filtering and queries
struct PropertyFilter {
  std::vector<std::string> include_names;
  std::vector<std::string> exclude_names;
  std::vector<std::string> include_types;
  std::vector<std::string> exclude_types;
  bool include_custom = true;
  bool include_inherited = true;
  bool include_relationships = true;
  bool include_attributes = true;
};

std::vector<Property> FilterProperties(
    const PropertyMap &props,
    const PropertyFilter &filter);

// Find property by name with namespace support
Property* FindProperty(PropertyMap &props, const std::string &name);
const Property* FindProperty(const PropertyMap &props, const std::string &name);

// Get property with fallback
template<typename T>
T GetPropertyOr(const PropertyMap &props, const std::string &name, const T &defaultValue) {
  if (auto prop = FindProperty(props, name)) {
    T value;
    if (ExtractValue(*prop, value)) {
      return value;
    }
  }
  return defaultValue;
}

// Property comparison
bool PropertiesEqual(const Property &a, const Property &b);
bool PropertyMapsEqual(const PropertyMap &a, const PropertyMap &b);

// Property merging (for composition)
PropertyMap MergeProperties(
    const PropertyMap &stronger,
    const PropertyMap &weaker);

// Property conversion
value::Value PropertyToValue(const Property &prop);
bool ValueToProperty(const value::Value &val, Property &prop);

// Debug utilities
std::string DumpProperty(const Property &prop, int indent = 0);
std::string DumpPropertyMap(const PropertyMap &props, int indent = 0);

} // namespace property_utils

} // namespace usdc
} // namespace tinyusdz