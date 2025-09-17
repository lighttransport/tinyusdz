// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// USD ASCII (USDA) formatter
// Part of the pprinter.cc modularization effort

#pragma once

#include "pprinter-core.hh"
#include "value-types.hh"
#include "prim-types.hh"
#include <vector>
#include <map>

namespace tinyusdz {
namespace pprint {

// USDA-specific formatter implementation
class USDAFormatter : public Formatter {
 public:
  USDAFormatter();
  ~USDAFormatter() override = default;
  
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
  
  // USDA-specific formatting methods
  std::string FormatHeader(const Layer &layer);
  std::string FormatLayerStack(const std::vector<Layer> &layers);
  std::string FormatCompositionArcs(const Prim &prim, uint32_t indent = 0);
  std::string FormatVariantSet(const std::string &name, 
                               const VariantSet &variantSet,
                               uint32_t indent = 0);
  std::string FormatTimeSamples(const value::TimeSamples &samples,
                                uint32_t indent = 0);
  std::string FormatListOp(const ListOp<Path> &listOp, uint32_t indent = 0);
  std::string FormatListOp(const ListOp<std::string> &listOp, uint32_t indent = 0);
  std::string FormatListOp(const ListOp<value::token> &listOp, uint32_t indent = 0);
  
  // Dictionary formatting
  std::string FormatDictionary(const value::dict &dict, uint32_t indent = 0);
  std::string FormatCustomData(const value::dict &customData, uint32_t indent = 0);
  
  // Array formatting
  template <typename T>
  std::string FormatArray(const std::vector<T> &array, uint32_t indent = 0);
  
  // Connection formatting
  std::string FormatConnection(const Path &path, uint32_t indent = 0);
  std::string FormatConnections(const std::vector<Path> &paths, uint32_t indent = 0);
  
  // Reference and payload formatting
  std::string FormatReference(const Reference &ref, uint32_t indent = 0);
  std::string FormatPayload(const Payload &payload, uint32_t indent = 0);
  
  // Specifier formatting
  std::string FormatSpecifier(Specifier spec);
  
  // Visibility formatting
  std::string FormatVisibility(Visibility vis);
  
  // Purpose formatting
  std::string FormatPurpose(Purpose purpose);
  
  // Interpolation formatting
  std::string FormatInterpolation(Interpolation interp);
  
  // Orientation formatting
  std::string FormatOrientation(Orientation orient);
  
 private:
  // Helper methods
  std::string FormatPrimHeader(const Prim &prim, uint32_t indent);
  std::string FormatPrimBody(const Prim &prim, uint32_t indent);
  std::string FormatPrimFooter(uint32_t indent);
  
  std::string FormatPropertyDeclaration(const Property &prop, uint32_t indent);
  std::string FormatPropertyValue(const Property &prop, uint32_t indent);
  std::string FormatPropertyMetadata(const Property &prop, uint32_t indent);
  
  std::string FormatAttributeType(const Attribute &attr);
  std::string FormatAttributeQualifiers(const Attribute &attr);
  
  std::string FormatRelationshipTargets(const Relationship &rel, uint32_t indent);
  
  // Type name formatting
  std::string GetUSDATypeName(uint32_t typeId);
  std::string GetUSDATypeName(const value::Value &val);
  
  // Special value formatting
  std::string FormatNone();
  std::string FormatDefault();
  std::string FormatTimeCode(value::TimeCode tc);
  std::string FormatAssetPath(const value::AssetPath &path);
  
  // Numeric formatting with proper precision
  std::string FormatHalf(value::half h);
  std::string FormatFloat(float f);
  std::string FormatDouble(double d);
  
  // Vector/Matrix formatting
  std::string FormatFloat2(const value::float2 &v);
  std::string FormatFloat3(const value::float3 &v);
  std::string FormatFloat4(const value::float4 &v);
  std::string FormatDouble2(const value::double2 &v);
  std::string FormatDouble3(const value::double3 &v);
  std::string FormatDouble4(const value::double4 &v);
  std::string FormatMatrix2d(const value::matrix2d &m);
  std::string FormatMatrix3d(const value::matrix3d &m);
  std::string FormatMatrix4d(const value::matrix4d &m);
  std::string FormatQuatf(const value::quatf &q);
  std::string FormatQuatd(const value::quatd &q);
  
  // String formatting with proper escaping
  std::string FormatString(const std::string &str);
  std::string FormatToken(const value::token &tok);
  std::string FormatPath(const Path &path);
  
  // Comment formatting
  std::string FormatDocString(const std::string &doc, uint32_t indent);
  std::string FormatComment(const std::string &comment, uint32_t indent);
  
  // State for formatting
  bool in_variant_context_ = false;
  bool in_metadata_context_ = false;
  std::vector<std::string> current_path_stack_;
};

// Convenience functions for USDA formatting
std::string FormatAsUSDA(const Layer &layer);
std::string FormatAsUSDA(const Stage &stage);
std::string FormatAsUSDA(const Prim &prim);
std::string FormatAsUSDA(const value::Value &val);

// Legacy compatibility functions
std::string to_string(const Layer &layer);
std::string to_string(const Stage &stage);
std::string to_string(const Prim &prim, uint32_t indent = 0);
std::string to_string(const PrimSpec &primspec, uint32_t indent = 0);
std::string to_string(const Property &prop, uint32_t indent = 0);

} // namespace pprint
} // namespace tinyusdz