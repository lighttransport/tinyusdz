// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// JSON formatter for USD data
// Part of the pprinter.cc modularization effort

#pragma once

#include "pprinter-core.hh"
#include "value-types.hh"
#include <stack>

namespace tinyusdz {
namespace pprint {

// JSON formatter implementation
class JSONFormatter : public Formatter {
 public:
  JSONFormatter();
  ~JSONFormatter() override = default;
  
  // Configuration options specific to JSON
  struct JSONConfig {
    bool pretty_print = true;
    bool include_type_info = true;
    bool include_metadata = true;
    bool escape_unicode = false;
    bool null_for_none = true;
    bool arrays_on_single_line = false;
    int indent_size = 2;
  };
  
  void SetJSONConfig(const JSONConfig &config) { json_config_ = config; }
  const JSONConfig& GetJSONConfig() const { return json_config_; }
  
  // Main formatting methods
  std::string Format(const Layer &layer) override;
  std::string Format(const Stage &stage) override;
  std::string Format(const Prim &prim, uint32_t indent = 0) override;
  std::string Format(const PrimSpec &primspec, uint32_t indent = 0) override;
  std::string Format(const Property &prop, uint32_t indent = 0) override;
  std::string Format(const Attribute &attr, uint32_t indent = 0) override;
  std::string Format(const Relationship &rel, uint32_t indent = 0) override;
  
  // Value formatting
  std::string FormatValue(const value::Value &val, uint32_t indent = 0) override;
  
  // Metadata formatting
  std::string FormatPrimMeta(const PrimMeta &meta, uint32_t indent = 0) override;
  std::string FormatAttrMeta(const AttrMeta &meta, uint32_t indent = 0) override;
  
  // JSON-specific formatting methods
  std::string FormatObject(const std::map<std::string, std::string> &obj,
                           uint32_t indent = 0);
  std::string FormatArray(const std::vector<std::string> &array,
                          uint32_t indent = 0);
  
  // Value type formatting
  std::string FormatNull();
  std::string FormatBool(bool value);
  std::string FormatNumber(int32_t value);
  std::string FormatNumber(int64_t value);
  std::string FormatNumber(uint32_t value);
  std::string FormatNumber(uint64_t value);
  std::string FormatNumber(float value);
  std::string FormatNumber(double value);
  std::string FormatString(const std::string &str);
  
  // Complex type formatting
  std::string FormatDictionary(const value::dict &dict, uint32_t indent = 0);
  std::string FormatTimeSamples(const value::TimeSamples &samples,
                                uint32_t indent = 0);
  std::string FormatPath(const Path &path);
  std::string FormatAssetPath(const value::AssetPath &path);
  std::string FormatTimeCode(value::TimeCode tc);
  
  // Array formatting
  template <typename T>
  std::string FormatValueArray(const std::vector<T> &array, uint32_t indent = 0);
  
  // Vector/Matrix formatting as JSON arrays
  std::string FormatFloat2(const value::float2 &v);
  std::string FormatFloat3(const value::float3 &v);
  std::string FormatFloat4(const value::float4 &v);
  std::string FormatDouble2(const value::double2 &v);
  std::string FormatDouble3(const value::double3 &v);
  std::string FormatDouble4(const value::double4 &v);
  std::string FormatMatrix(const value::matrix2d &m);
  std::string FormatMatrix(const value::matrix3d &m);
  std::string FormatMatrix(const value::matrix4d &m);
  std::string FormatQuaternion(const value::quatf &q);
  std::string FormatQuaternion(const value::quatd &q);
  
  // ListOp formatting
  std::string FormatListOp(const ListOp<Path> &listOp);
  std::string FormatListOp(const ListOp<std::string> &listOp);
  std::string FormatListOp(const ListOp<value::token> &listOp);
  
  // Reference and payload formatting
  std::string FormatReference(const Reference &ref);
  std::string FormatPayload(const Payload &payload);
  
 private:
  JSONConfig json_config_;
  
  // JSON structure helpers
  class JSONWriter {
   public:
    JSONWriter(bool pretty = true, int indent_size = 2);
    
    void BeginObject();
    void EndObject();
    void BeginArray();
    void EndArray();
    
    void WriteKey(const std::string &key);
    void WriteString(const std::string &value);
    void WriteNumber(double value);
    void WriteBool(bool value);
    void WriteNull();
    void WriteRaw(const std::string &raw);
    
    void WriteKeyValue(const std::string &key, const std::string &value);
    void WriteKeyValue(const std::string &key, double value);
    void WriteKeyValue(const std::string &key, bool value);
    void WriteKeyNull(const std::string &key);
    
    std::string GetOutput() const { return output_.str(); }
    
   private:
    std::stringstream output_;
    bool pretty_print_;
    int indent_size_;
    int current_indent_;
    
    enum State {
      NONE,
      IN_OBJECT,
      IN_ARRAY,
      AFTER_KEY
    };
    
    std::stack<State> state_stack_;
    std::stack<bool> first_element_stack_;
    
    void WriteIndent();
    void WriteCommaIfNeeded();
    void UpdateState(State new_state);
    std::string EscapeString(const std::string &str);
  };
  
  // Helper to convert Prim to JSON object
  void PrimToJSON(const Prim &prim, JSONWriter &writer);
  void PropertyToJSON(const Property &prop, JSONWriter &writer);
  void ValueToJSON(const value::Value &val, JSONWriter &writer);
  
  // Type information helpers
  std::string GetJSONTypeName(uint32_t typeId);
  std::string GetJSONTypeName(const value::Value &val);
  
  // Create type-tagged JSON object
  void WriteTypedValue(const std::string &type, 
                       const std::string &value,
                       JSONWriter &writer);
};

// Convenience functions for JSON formatting
std::string FormatAsJSON(const Layer &layer, bool pretty = true);
std::string FormatAsJSON(const Stage &stage, bool pretty = true);
std::string FormatAsJSON(const Prim &prim, bool pretty = true);
std::string FormatAsJSON(const value::Value &val, bool pretty = true);

// JSON string escaping
std::string EscapeJSONString(const std::string &str);
std::string UnescapeJSONString(const std::string &str);

} // namespace pprint
} // namespace tinyusdz