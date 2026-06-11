// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Prim Printer Implementation

#include "prim-printer.hh"
#include "value-printer.hh"
#include "../layer/property-index.hh"
#include <sstream>

namespace tinyusdz {
namespace next {

namespace {

// Get specifier string
const char* GetSpecifierString(PrimSpecifier spec) {
  switch (spec) {
    case PrimSpecifier::Def:   return "def";
    case PrimSpecifier::Over:  return "over";
    case PrimSpecifier::Class: return "class";
    default: return "def";
  }
}

// Print indent
void PrintIndent(std::ostream& os, int depth, const std::string& indent) {
  for (int i = 0; i < depth; ++i) {
    os << indent;
  }
}

// Print a single property
void PrintProperty(std::ostream& os, const PropSlot& slot, const PrimSpec& spec,
                   int depth, const PrimPrintOptions& opts) {
  PrintIndent(os, depth, opts.indent);

  // Get property name
  PropNameTable& name_table = GetPropNameTable();
  const std::string& name = name_table.get(slot.name_id);

  // Get type name
  TypeId type_id = static_cast<TypeId>(slot.value_type);
  std::string type_name = GetTypeName(type_id);

  // Check for variability qualifiers
  if (slot.is_uniform()) {
    os << "uniform ";
  }
  if (slot.is_custom()) {
    os << "custom ";
  }

  // Print type and name
  os << type_name;
  if (slot.is_array()) {
    os << "[]";
  }
  os << " " << name;

  // Print value if requested
  if (opts.print_values) {
    const Value* value = spec.property_value(slot.name_id);
    if (value && !value->is_empty()) {
      PrintOptions print_opts;
      print_opts.indent = opts.indent;
      os << " = " << PrintValue(*value, print_opts);
    }
  }

  os << "\n";
}

// Print relationships
void PrintRelationships(std::ostream& os, const PrimSpec& spec,
                        int depth, const PrimPrintOptions& opts) {
  // Note: PrimSpec stores relationships in a private map
  // We need to iterate through properties and check relationship flag
  for (const auto& slot : spec.properties().slots()) {
    if (slot.is_relationship()) {
      PrintIndent(os, depth, opts.indent);

      PropNameTable& name_table = GetPropNameTable();
      const std::string& name = name_table.get(slot.name_id);

      // Get targets
      const std::vector<Path>* targets = spec.relationship(name);
      if (targets && !targets->empty()) {
        os << "rel " << name << " = ";
        if (targets->size() == 1) {
          os << "<" << (*targets)[0].str() << ">";
        } else {
          os << "[";
          for (size_t i = 0; i < targets->size(); ++i) {
            if (i > 0) os << ", ";
            os << "<" << (*targets)[i].str() << ">";
          }
          os << "]";
        }
        os << "\n";
      }
    }
  }
}

// Print metadata block
void PrintMetadata(std::ostream& os, const PrimSpecMeta& meta, int depth,
                   const PrimPrintOptions& opts) {
  bool has_meta = false;

  // Check what metadata we have
  if (!meta.active) has_meta = true;
  if (meta.hidden) has_meta = true;
  if (!meta.doc().empty()) has_meta = true;
  if (!meta.apiSchemas().empty()) has_meta = true;
  if (!meta.references.empty()) has_meta = true;
  if (!meta.payloads.empty()) has_meta = true;
  if (!meta.inherits.empty()) has_meta = true;
  if (!meta.specializes.empty()) has_meta = true;

  if (!has_meta) return;

  // Print opening (metadata goes inside prim braces in USDA)
  if (!meta.active) {
    PrintIndent(os, depth, opts.indent);
    os << "active = false\n";
  }
  if (meta.hidden) {
    PrintIndent(os, depth, opts.indent);
    os << "hidden = true\n";
  }
  if (!meta.doc().empty()) {
    PrintIndent(os, depth, opts.indent);
    os << "doc = \"" << meta.doc() << "\"\n";
  }
  if (!meta.apiSchemas().empty()) {
    PrintIndent(os, depth, opts.indent);
    os << "apiSchemas = [";
    for (size_t i = 0; i < meta.apiSchemas().size(); ++i) {
      if (i > 0) os << ", ";
      os << "\"" << meta.apiSchemas()[i] << "\"";
    }
    os << "]\n";
  }
  if (!meta.references.empty()) {
    PrintIndent(os, depth, opts.indent);
    os << "references = [";
    for (size_t i = 0; i < meta.references.size(); ++i) {
      if (i > 0) os << ", ";
      os << "@" << meta.references[i] << "@";
    }
    os << "]\n";
  }
  if (!meta.payloads.empty()) {
    PrintIndent(os, depth, opts.indent);
    os << "payload = [";
    for (size_t i = 0; i < meta.payloads.size(); ++i) {
      if (i > 0) os << ", ";
      os << "@" << meta.payloads[i] << "@";
    }
    os << "]\n";
  }
  if (!meta.inherits.empty()) {
    PrintIndent(os, depth, opts.indent);
    os << "inherits = [";
    for (size_t i = 0; i < meta.inherits.size(); ++i) {
      if (i > 0) os << ", ";
      os << "<" << meta.inherits[i] << ">";
    }
    os << "]\n";
  }
  if (!meta.specializes.empty()) {
    PrintIndent(os, depth, opts.indent);
    os << "specializes = [";
    for (size_t i = 0; i < meta.specializes.size(); ++i) {
      if (i > 0) os << ", ";
      os << "<" << meta.specializes[i] << ">";
    }
    os << "]\n";
  }
}

// Forward declaration for recursion
void PrintPrimSpecRecursive(std::ostream& os, const PrimSpec& spec, const Layer& layer,
                            int depth, const PrimPrintOptions& opts);

void PrintPrimSpecRecursive(std::ostream& os, const PrimSpec& spec, const Layer& layer,
                            int depth, const PrimPrintOptions& opts) {
  // Check max depth
  if (opts.max_depth >= 0 && depth > opts.max_depth) {
    return;
  }

  // Print prim header
  PrintIndent(os, depth, opts.indent);
  os << GetSpecifierString(spec.specifier());

  // Print type name
  const std::string& type_name = spec.type_name();
  if (!type_name.empty()) {
    os << " " << type_name;
  }

  // Print prim name
  os << " \"" << spec.name() << "\"";

  // Open brace
  os << "\n";
  PrintIndent(os, depth, opts.indent);
  os << "{\n";

  int content_depth = depth + 1;

  // Print metadata
  if (opts.print_metadata) {
    PrintMetadata(os, spec.meta(), content_depth, opts);
  }

  // Print properties
  const auto& slots = spec.properties().slots();
  size_t prop_count = 0;
  for (const auto& slot : slots) {
    if (!slot.is_relationship()) {  // Skip relationships, handled separately
      PrintProperty(os, slot, spec, content_depth, opts);
      ++prop_count;
      if (opts.max_properties > 0 && prop_count >= opts.max_properties) {
        PrintIndent(os, content_depth, opts.indent);
        os << "# ... " << (slots.size() - prop_count) << " more properties\n";
        break;
      }
    }
  }

  // Print relationships
  if (opts.print_relationships) {
    PrintRelationships(os, spec, content_depth, opts);
  }

  // Print children
  for (uint32_t child_idx : spec.child_indices()) {
    const PrimSpec* child = layer.prim(child_idx);
    if (child) {
      os << "\n";
      PrintPrimSpecRecursive(os, *child, layer, content_depth, opts);
    }
  }

  // Close brace
  PrintIndent(os, depth, opts.indent);
  os << "}\n";
}

// Print layer metadata
void PrintLayerMeta(std::ostream& os, const LayerMeta& meta, const PrimPrintOptions& opts) {
  os << "#usda 1.0\n";
  os << "(\n";

  bool first = true;
  if (!meta.defaultPrim.empty()) {
    os << opts.indent << "defaultPrim = \"" << meta.defaultPrim << "\"";
    first = false;
  }
  if (meta.upAxis != "Y") {
    if (!first) os << "\n";
    os << opts.indent << "upAxis = \"" << meta.upAxis << "\"";
    first = false;
  }
  if (meta.metersPerUnit != 0.01) {
    if (!first) os << "\n";
    os << opts.indent << "metersPerUnit = " << meta.metersPerUnit;
    first = false;
  }
  if (meta.timeCodesPerSecond != 24.0) {
    if (!first) os << "\n";
    os << opts.indent << "timeCodesPerSecond = " << meta.timeCodesPerSecond;
    first = false;
  }
  if (meta.startTimeCode != 0.0 || meta.endTimeCode != 0.0) {
    if (!first) os << "\n";
    os << opts.indent << "startTimeCode = " << meta.startTimeCode;
    os << "\n" << opts.indent << "endTimeCode = " << meta.endTimeCode;
    first = false;
  }
  if (!meta.doc.empty()) {
    if (!first) os << "\n";
    os << opts.indent << "doc = \"" << meta.doc << "\"";
    first = false;
  }
  if (!meta.subLayers.empty()) {
    if (!first) os << "\n";
    os << opts.indent << "subLayers = [\n";
    for (const auto& layer : meta.subLayers) {
      os << opts.indent << opts.indent << "@" << layer << "@,\n";
    }
    os << opts.indent << "]";
    first = false;
  }

  os << "\n)\n\n";
}

}  // anonymous namespace

// ============================================================
// Public API implementations
// ============================================================

std::string PrintPrim(const UsdPrim& prim, const PrimPrintOptions& opts) {
  std::ostringstream ss;
  PrintPrim(ss, prim, opts);
  return ss.str();
}

void PrintPrim(std::ostream& os, const UsdPrim& prim, const PrimPrintOptions& opts) {
  if (!prim.IsValid()) {
    os << "# Invalid prim\n";
    return;
  }

  const PrimSpec* spec = prim.GetPrimSpec();
  if (!spec) {
    os << "# Null prim spec\n";
    return;
  }

  // For UsdPrim printing, we don't have layer context for children
  // Print just this prim without recursion into children
  PrintIndent(os, 0, opts.indent);
  os << GetSpecifierString(spec->specifier());

  const std::string& type_name = spec->type_name();
  if (!type_name.empty()) {
    os << " " << type_name;
  }

  os << " \"" << spec->name() << "\"\n{\n";

  // Print metadata
  if (opts.print_metadata) {
    PrintMetadata(os, spec->meta(), 1, opts);
  }

  // Print properties
  const auto& slots = spec->properties().slots();
  size_t prop_count = 0;
  for (const auto& slot : slots) {
    if (!slot.is_relationship()) {
      PrintProperty(os, slot, *spec, 1, opts);
      ++prop_count;
      if (opts.max_properties > 0 && prop_count >= opts.max_properties) {
        PrintIndent(os, 1, opts.indent);
        os << "# ... " << (slots.size() - prop_count) << " more properties\n";
        break;
      }
    }
  }

  // Print relationships
  if (opts.print_relationships) {
    PrintRelationships(os, *spec, 1, opts);
  }

  // Note: children printed via Stage traversal, not here
  os << "}\n";
}

std::string PrintStage(const Stage& stage, const PrimPrintOptions& opts) {
  std::ostringstream ss;
  PrintStage(ss, stage, opts);
  return ss.str();
}

void PrintStage(std::ostream& os, const Stage& stage, const PrimPrintOptions& opts) {
  const Layer* root_layer = stage.GetRootLayer();
  if (!root_layer) {
    os << "# Empty stage\n";
    return;
  }

  // Print via layer
  PrintLayer(os, *root_layer, opts);
}

std::string PrintPrimSpec(const PrimSpec& spec, const PrimPrintOptions& opts) {
  std::ostringstream ss;
  // For standalone PrimSpec, we don't have layer context
  // Print just this spec without children
  ss << GetSpecifierString(spec.specifier());

  const std::string& type_name = spec.type_name();
  if (!type_name.empty()) {
    ss << " " << type_name;
  }

  ss << " \"" << spec.name() << "\"\n{\n";

  if (opts.print_metadata) {
    PrintMetadata(ss, spec.meta(), 1, opts);
  }

  const auto& slots = spec.properties().slots();
  size_t prop_count = 0;
  for (const auto& slot : slots) {
    if (!slot.is_relationship()) {
      PrintProperty(ss, slot, spec, 1, opts);
      ++prop_count;
      if (opts.max_properties > 0 && prop_count >= opts.max_properties) {
        PrintIndent(ss, 1, opts.indent);
        ss << "# ... " << (slots.size() - prop_count) << " more properties\n";
        break;
      }
    }
  }

  if (opts.print_relationships) {
    PrintRelationships(ss, spec, 1, opts);
  }

  ss << "}\n";
  return ss.str();
}

void PrintPrimSpec(std::ostream& os, const PrimSpec& spec, int depth, const PrimPrintOptions& opts) {
  // This version needs Layer for children, but signature doesn't include it
  // Just print the spec without children
  PrintIndent(os, depth, opts.indent);
  os << GetSpecifierString(spec.specifier());

  const std::string& type_name = spec.type_name();
  if (!type_name.empty()) {
    os << " " << type_name;
  }

  os << " \"" << spec.name() << "\"\n";
  PrintIndent(os, depth, opts.indent);
  os << "{\n";

  if (opts.print_metadata) {
    PrintMetadata(os, spec.meta(), depth + 1, opts);
  }

  const auto& slots = spec.properties().slots();
  size_t prop_count = 0;
  for (const auto& slot : slots) {
    if (!slot.is_relationship()) {
      PrintProperty(os, slot, spec, depth + 1, opts);
      ++prop_count;
      if (opts.max_properties > 0 && prop_count >= opts.max_properties) {
        PrintIndent(os, depth + 1, opts.indent);
        os << "# ... " << (slots.size() - prop_count) << " more properties\n";
        break;
      }
    }
  }

  if (opts.print_relationships) {
    PrintRelationships(os, spec, depth + 1, opts);
  }

  PrintIndent(os, depth, opts.indent);
  os << "}\n";
}

std::string PrintLayer(const Layer& layer, const PrimPrintOptions& opts) {
  std::ostringstream ss;
  PrintLayer(ss, layer, opts);
  return ss.str();
}

void PrintLayer(std::ostream& os, const Layer& layer, const PrimPrintOptions& opts) {
  // Print layer header
  PrintLayerMeta(os, layer.meta(), opts);

  // Print root prims
  for (uint32_t root_idx : layer.root_indices()) {
    const PrimSpec* root = layer.prim(root_idx);
    if (root) {
      PrintPrimSpecRecursive(os, *root, layer, 0, opts);
      os << "\n";
    }
  }
}

}  // namespace next
}  // namespace tinyusdz
