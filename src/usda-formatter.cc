// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

#include "usda-formatter.hh"
#include "value-formatter.hh"
#include "pretty-print-utils.hh"
#include "layer.hh"
#include "stage.hh"
#include "prim.hh"
#include "composition.hh"
#include <algorithm>
#include <iomanip>

namespace tinyusdz {
namespace pprint {

USDAFormatter::USDAFormatter() {
  // Set default USDA configuration
  config_.format = PrintConfig::USDA;
  config_.sort_properties = true;
  config_.print_metadata = true;
  config_.float_precision = 6;
  config_.double_precision = 15;
}

std::string USDAFormatter::Format(const Layer &layer) {
  std::stringstream ss;
  
  // Header
  ss << FormatHeader(layer);
  
  // Stage metadata
  if (!layer.metas().empty()) {
    ss << "(\n";
    for (const auto &meta : layer.metas()) {
      ss << Indent(1) << FormatPrimMeta(meta.second, 1);
    }
    ss << ")\n\n";
  }
  
  // Prims
  for (const auto &prim : layer.primspecs()) {
    ss << Format(prim.second, 0);
  }
  
  return ss.str();
}

std::string USDAFormatter::Format(const Stage &stage) {
  std::stringstream ss;
  
  // Get root layer
  const Layer* root_layer = stage.root_layer();
  if (root_layer) {
    ss << Format(*root_layer);
  }
  
  return ss.str();
}

std::string USDAFormatter::Format(const Prim &prim, uint32_t indent) {
  std::stringstream ss;
  
  ss << FormatPrimHeader(prim, indent);
  ss << FormatPrimBody(prim, indent);
  ss << FormatPrimFooter(indent);
  
  return ss.str();
}

std::string USDAFormatter::Format(const PrimSpec &primspec, uint32_t indent) {
  std::stringstream ss;
  std::string indent_str = Indent(indent);
  
  // Specifier
  ss << indent_str;
  ss << FormatSpecifier(primspec.specifier());
  
  // Type name
  if (!primspec.typeName().empty()) {
    ss << " " << primspec.typeName();
  }
  
  // Prim name
  ss << " \"" << primspec.name() << "\"";
  
  // Metadata and properties
  if (!primspec.metas().empty() || !primspec.props().empty()) {
    ss << " {\n";
    
    // Metadata
    for (const auto &meta : primspec.metas()) {
      ss << Indent(indent + 1) << meta.first << " = ";
      ss << FormatValue(meta.second, indent + 1) << "\n";
    }
    
    // Properties
    std::vector<std::string> prop_names;
    for (const auto &prop : primspec.props()) {
      prop_names.push_back(prop.first);
    }
    
    if (config_.sort_properties) {
      std::sort(prop_names.begin(), prop_names.end());
    }
    
    for (const auto &name : prop_names) {
      const auto &prop = primspec.props().at(name);
      ss << Format(prop, indent + 1);
    }
    
    ss << indent_str << "}\n";
  } else {
    ss << "\n";
  }
  
  return ss.str();
}

std::string USDAFormatter::Format(const Property &prop, uint32_t indent) {
  std::stringstream ss;
  
  ss << FormatPropertyDeclaration(prop, indent);
  ss << FormatPropertyValue(prop, indent);
  
  if (config_.print_metadata) {
    ss << FormatPropertyMetadata(prop, indent);
  }
  
  return ss.str();
}

std::string USDAFormatter::Format(const Attribute &attr, uint32_t indent) {
  std::stringstream ss;
  std::string indent_str = Indent(indent);
  
  // Qualifiers
  ss << indent_str;
  ss << FormatAttributeQualifiers(attr);
  
  // Type
  ss << FormatAttributeType(attr);
  
  // Name
  ss << " " << attr.name();
  
  // Value or connection
  if (attr.has_value()) {
    ss << " = ";
    ss << FormatValue(attr.get_value(), indent);
  } else if (attr.has_connection()) {
    ss << ".connect = ";
    ss << FormatConnections(attr.connections(), indent);
  }
  
  // Metadata
  if (config_.print_metadata && !attr.metas().empty()) {
    ss << " (\n";
    ss << FormatAttrMeta(attr.metas(), indent + 1);
    ss << indent_str << ")";
  }
  
  ss << "\n";
  
  return ss.str();
}

std::string USDAFormatter::Format(const Relationship &rel, uint32_t indent) {
  std::stringstream ss;
  std::string indent_str = Indent(indent);
  
  ss << indent_str << "rel " << rel.name();
  
  if (!rel.targetPaths().empty()) {
    ss << " = ";
    ss << FormatRelationshipTargets(rel, indent);
  }
  
  // Metadata
  if (config_.print_metadata && rel.has_metadata()) {
    ss << " (\n";
    // Format relationship metadata
    ss << indent_str << ")";
  }
  
  ss << "\n";
  
  return ss.str();
}

std::string USDAFormatter::FormatValue(const value::Value &val, uint32_t indent) {
  ValueFormatter formatter(config_);
  return formatter.Format(val, indent);
}

std::string USDAFormatter::FormatPrimMeta(const PrimMeta &meta, uint32_t indent) {
  std::stringstream ss;
  std::string indent_str = Indent(indent);
  
  // Format each metadata field
  if (meta.doc.has_value()) {
    ss << indent_str << "doc = " << Quote(meta.doc.value()) << "\n";
  }
  
  if (meta.hidden) {
    ss << indent_str << "hidden = true\n";
  }
  
  if (!meta.displayName.empty()) {
    ss << indent_str << "displayName = " << Quote(meta.displayName) << "\n";
  }
  
  if (!meta.displayGroup.empty()) {
    ss << indent_str << "displayGroup = " << Quote(meta.displayGroup) << "\n";
  }
  
  // Custom metadata
  if (!meta.customData.empty()) {
    ss << indent_str << "customData = ";
    ss << FormatDictionary(meta.customData, indent);
    ss << "\n";
  }
  
  return ss.str();
}

std::string USDAFormatter::FormatAttrMeta(const AttrMeta &meta, uint32_t indent) {
  std::stringstream ss;
  std::string indent_str = Indent(indent);
  
  if (!meta.displayName.empty()) {
    ss << indent_str << "displayName = " << Quote(meta.displayName) << "\n";
  }
  
  if (!meta.displayGroup.empty()) {
    ss << indent_str << "displayGroup = " << Quote(meta.displayGroup) << "\n";
  }
  
  if (meta.hidden) {
    ss << indent_str << "hidden = true\n";
  }
  
  // Interpolation
  if (meta.interpolation != Interpolation::Default) {
    ss << indent_str << "interpolation = ";
    ss << FormatInterpolation(meta.interpolation) << "\n";
  }
  
  // Custom metadata
  if (!meta.customData.empty()) {
    ss << indent_str << "customData = ";
    ss << FormatDictionary(meta.customData, indent);
    ss << "\n";
  }
  
  return ss.str();
}

std::string USDAFormatter::FormatHeader(const Layer &layer) {
  std::stringstream ss;
  
  // USDA magic header
  ss << "#usda 1.0\n";
  
  // Layer metadata as header comment
  if (!layer.comment().empty()) {
    ss << "# " << layer.comment() << "\n";
  }
  
  // Stage metadata
  ss << "(\n";
  
  if (!layer.defaultPrim().empty()) {
    ss << "    defaultPrim = \"" << layer.defaultPrim() << "\"\n";
  }
  
  if (layer.has_startTimeCode()) {
    ss << "    startTimeCode = " << layer.startTimeCode() << "\n";
  }
  
  if (layer.has_endTimeCode()) {
    ss << "    endTimeCode = " << layer.endTimeCode() << "\n";
  }
  
  if (layer.has_framesPerSecond()) {
    ss << "    framesPerSecond = " << layer.framesPerSecond() << "\n";
  }
  
  if (layer.has_timeCodesPerSecond()) {
    ss << "    timeCodesPerSecond = " << layer.timeCodesPerSecond() << "\n";
  }
  
  ss << ")\n\n";
  
  return ss.str();
}

std::string USDAFormatter::FormatPrimHeader(const Prim &prim, uint32_t indent) {
  std::stringstream ss;
  std::string indent_str = Indent(indent);
  
  ss << indent_str;
  
  // Specifier
  ss << FormatSpecifier(prim.specifier());
  
  // Type
  if (!prim.typeName().empty()) {
    ss << " " << prim.typeName();
  }
  
  // Name
  ss << " \"" << prim.name() << "\"";
  
  // Inherits
  if (!prim.inherits().empty()) {
    ss << " (\n";
    ss << Indent(indent + 1) << "inherits = ";
    // Format inherits paths
    ss << "\n" << indent_str << ")";
  }
  
  ss << "\n" << indent_str << "{\n";
  
  return ss.str();
}

std::string USDAFormatter::FormatPrimBody(const Prim &prim, uint32_t indent) {
  std::stringstream ss;
  
  // Composition arcs
  if (config_.print_composition_arcs) {
    ss << FormatCompositionArcs(prim, indent + 1);
  }
  
  // Properties
  std::vector<std::string> prop_names;
  for (const auto &prop : prim.properties()) {
    prop_names.push_back(prop.first);
  }
  
  if (config_.sort_properties) {
    std::sort(prop_names.begin(), prop_names.end());
  }
  
  for (const auto &name : prop_names) {
    ss << Format(prim.properties().at(name), indent + 1);
  }
  
  // Variant sets
  for (const auto &variantSet : prim.variantSets()) {
    ss << FormatVariantSet(variantSet.first, variantSet.second, indent + 1);
  }
  
  // Children prims
  for (const auto &child : prim.children()) {
    ss << Format(child, indent + 1);
  }
  
  return ss.str();
}

std::string USDAFormatter::FormatPrimFooter(uint32_t indent) {
  return Indent(indent) + "}\n";
}

std::string USDAFormatter::FormatSpecifier(Specifier spec) {
  switch (spec) {
    case Specifier::Def: return "def";
    case Specifier::Over: return "over";
    case Specifier::Class: return "class";
    default: return "def";
  }
}

std::string USDAFormatter::FormatVisibility(Visibility vis) {
  switch (vis) {
    case Visibility::Inherited: return "inherited";
    case Visibility::Invisible: return "invisible";
    default: return "inherited";
  }
}

std::string USDAFormatter::FormatPurpose(Purpose purpose) {
  switch (purpose) {
    case Purpose::Default: return "default";
    case Purpose::Render: return "render";
    case Purpose::Proxy: return "proxy";
    case Purpose::Guide: return "guide";
    default: return "default";
  }
}

std::string USDAFormatter::FormatInterpolation(Interpolation interp) {
  switch (interp) {
    case Interpolation::Constant: return "constant";
    case Interpolation::Linear: return "linear";
    case Interpolation::Vertex: return "vertex";
    case Interpolation::Varying: return "varying";
    case Interpolation::FaceVarying: return "faceVarying";
    case Interpolation::Uniform: return "uniform";
    default: return "linear";
  }
}

std::string USDAFormatter::FormatDictionary(const value::dict &dict, uint32_t indent) {
  std::stringstream ss;
  
  ss << "{\n";
  
  for (const auto &kv : dict) {
    ss << Indent(indent + 1);
    ss << Quote(kv.first) << ": ";
    ss << FormatValue(*kv.second, indent + 1);
    ss << ",\n";
  }
  
  ss << Indent(indent) << "}";
  
  return ss.str();
}

std::string USDAFormatter::FormatTimeSamples(const value::TimeSamples &samples, uint32_t indent) {
  std::stringstream ss;
  
  ss << "{\n";
  
  for (const auto &sample : samples.samples()) {
    ss << Indent(indent + 1);
    ss << sample.first << ": ";
    ss << FormatValue(*sample.second, indent + 1);
    ss << ",\n";
  }
  
  ss << Indent(indent) << "}";
  
  return ss.str();
}

// Convenience functions
std::string FormatAsUSDA(const Layer &layer) {
  USDAFormatter formatter;
  return formatter.Format(layer);
}

std::string FormatAsUSDA(const Stage &stage) {
  USDAFormatter formatter;
  return formatter.Format(stage);
}

std::string FormatAsUSDA(const Prim &prim) {
  USDAFormatter formatter;
  return formatter.Format(prim);
}

std::string FormatAsUSDA(const value::Value &val) {
  USDAFormatter formatter;
  return formatter.FormatValue(val);
}

} // namespace pprint
} // namespace tinyusdz