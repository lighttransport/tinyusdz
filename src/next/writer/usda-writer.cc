// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDA Writer Implementation

#include "usda-writer.hh"
#include "value-printer.hh"
#include "../layer/property-index.hh"
#include <sstream>
#include <fstream>
#include <iomanip>
#include <algorithm>

namespace tinyusdz {
namespace next {

namespace {

// Get specifier keyword
const char* SpecifierKeyword(PrimSpecifier spec) {
  switch (spec) {
    case PrimSpecifier::Def:   return "def";
    case PrimSpecifier::Over:  return "over";
    case PrimSpecifier::Class: return "class";
    default: return "def";
  }
}

// Write indent
void WriteIndent(std::ostream& os, int depth, const std::string& indent) {
  for (int i = 0; i < depth; ++i) {
    os << indent;
  }
}

// Escape a string for USDA output
std::string EscapeString(const std::string& s) {
  std::string result;
  result.reserve(s.size() + 2);
  result += '"';
  for (char c : s) {
    switch (c) {
      case '"':  result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:   result += c; break;
    }
  }
  result += '"';
  return result;
}

// Write layer metadata block
void WriteLayerMeta(std::ostream& os, const LayerMeta& meta,
                    const USDAWriteOptions& opts) {
  os << "#usda 1.0\n";
  os << "(\n";

  std::vector<std::string> lines;

  if (!meta.defaultPrim.empty()) {
    lines.push_back(opts.indent + "defaultPrim = " + EscapeString(meta.defaultPrim));
  }

  if (!meta.doc.empty() && opts.include_comments) {
    lines.push_back(opts.indent + "doc = " + EscapeString(meta.doc));
  }

  if (meta.metersPerUnit != 0.01) {
    std::ostringstream ss;
    ss << std::setprecision(opts.double_precision) << meta.metersPerUnit;
    lines.push_back(opts.indent + "metersPerUnit = " + ss.str());
  }

  if (meta.startTimeCode != 0.0) {
    std::ostringstream ss;
    ss << std::setprecision(opts.double_precision) << meta.startTimeCode;
    lines.push_back(opts.indent + "startTimeCode = " + ss.str());
  }

  if (meta.endTimeCode != 0.0) {
    std::ostringstream ss;
    ss << std::setprecision(opts.double_precision) << meta.endTimeCode;
    lines.push_back(opts.indent + "endTimeCode = " + ss.str());
  }

  if (meta.timeCodesPerSecond != 24.0) {
    std::ostringstream ss;
    ss << std::setprecision(opts.double_precision) << meta.timeCodesPerSecond;
    lines.push_back(opts.indent + "timeCodesPerSecond = " + ss.str());
  }

  if (meta.upAxis != "Y") {
    lines.push_back(opts.indent + "upAxis = " + EscapeString(meta.upAxis));
  }

  if (!meta.subLayers.empty()) {
    std::ostringstream ss;
    ss << opts.indent << "subLayers = [\n";
    for (const auto& layer : meta.subLayers) {
      ss << opts.indent << opts.indent << "@" << layer << "@,\n";
    }
    ss << opts.indent << "]";
    lines.push_back(ss.str());
  }

  for (size_t i = 0; i < lines.size(); ++i) {
    os << lines[i];
    if (i + 1 < lines.size()) {
      os << "\n";
    }
  }

  os << "\n)\n\n";
}

// Write time samples for a property
void WriteTimeSamples(std::ostream& os, const std::string& name, PropNameId name_id,
                      const PrimSpec& spec, int depth, const USDAWriteOptions& opts) {
  const auto* samples = spec.time_samples(name_id);
  if (!samples || samples->empty()) return;

  WriteIndent(os, depth, opts.indent);

  // Get type from first sample
  const Value* first_val = spec.time_sample_value(samples->front().second);
  if (first_val) {
    TypeId type_id = first_val->type_id();
    os << GetTypeName(type_id);
    if (first_val->is_array()) {
      os << "[]";
    }
  }

  os << " " << name << ".timeSamples = {\n";

  PrintOptions print_opts;
  print_opts.float_precision = opts.float_precision;
  print_opts.double_precision = opts.double_precision;
  print_opts.max_array_elements = opts.max_elements_per_line;
  print_opts.compact = opts.compact;

  for (size_t i = 0; i < samples->size(); ++i) {
    const auto& sample = (*samples)[i];
    WriteIndent(os, depth + 1, opts.indent);
    os << std::setprecision(opts.double_precision) << sample.first << ": ";

    const Value* val = spec.time_sample_value(sample.second);
    if (val) {
      os << PrintValue(*val, print_opts);
    } else {
      os << "None";
    }
    os << ",\n";
  }

  WriteIndent(os, depth, opts.indent);
  os << "}\n";
}

// Write a property value
void WriteProperty(std::ostream& os, const PropSlot& slot, const PrimSpec& spec,
                   int depth, const USDAWriteOptions& opts) {
  PropNameTable& name_table = GetPropNameTable();
  const std::string& name = name_table.get(slot.name_id);

  // Check if this property has time samples
  if (slot.is_time_sampled() && spec.has_time_samples(slot.name_id)) {
    WriteTimeSamples(os, name, slot.name_id, spec, depth, opts);
    return;
  }

  WriteIndent(os, depth, opts.indent);

  // Write qualifiers
  if (slot.is_custom()) {
    os << "custom ";
  }
  if (slot.is_uniform()) {
    os << "uniform ";
  }

  // Write type name
  TypeId type_id = static_cast<TypeId>(slot.value_type);
  os << GetTypeName(type_id);
  if (slot.is_array()) {
    os << "[]";
  }

  os << " " << name;

  // Check for connection
  if (slot.is_connection()) {
    const Value* value = spec.property_value(slot.name_id);
    if (value) {
      const std::string* path = value->as_string();
      if (path) {
        os << ".connect = <" << *path << ">";
      }
    }
    os << "\n";
    return;
  }

  // Write value
  const Value* value = spec.property_value(slot.name_id);
  if (value && !value->is_empty()) {
    PrintOptions print_opts;
    print_opts.float_precision = opts.float_precision;
    print_opts.double_precision = opts.double_precision;
    print_opts.max_array_elements = opts.max_elements_per_line;
    print_opts.compact = opts.compact;

    os << " = " << PrintValue(*value, print_opts);
  }

  os << "\n";
}

// Write relationship
void WriteRelationship(std::ostream& os, const std::string& name,
                       const std::vector<Path>& targets, int depth,
                       const USDAWriteOptions& opts) {
  WriteIndent(os, depth, opts.indent);
  os << "rel " << name;

  if (targets.empty()) {
    os << " = None\n";
    return;
  }

  os << " = ";
  if (targets.size() == 1) {
    os << "<" << targets[0].str() << ">\n";
  } else {
    os << "[\n";
    for (const auto& target : targets) {
      WriteIndent(os, depth + 1, opts.indent);
      os << "<" << target.str() << ">,\n";
    }
    WriteIndent(os, depth, opts.indent);
    os << "]\n";
  }
}

// Forward declaration
void WritePrimSpec(std::ostream& os, const PrimSpec& spec, const Layer& layer,
                   int depth, const USDAWriteOptions& opts);

void WritePrimSpec(std::ostream& os, const PrimSpec& spec, const Layer& layer,
                   int depth, const USDAWriteOptions& opts) {
  // Write prim definition line
  WriteIndent(os, depth, opts.indent);
  os << SpecifierKeyword(spec.specifier());

  const std::string& type_name = spec.type_name();
  if (!type_name.empty()) {
    os << " " << type_name;
  }

  os << " " << EscapeString(spec.name());

  // Check if prim has metadata that needs to go in parentheses
  const PrimSpecMeta& meta = spec.meta();
  bool has_paren_meta = !meta.references.empty() || !meta.payloads.empty() ||
                        !meta.inherits.empty() || !meta.specializes.empty() ||
                        !meta.variantSelection.empty();

  if (has_paren_meta) {
    os << " (\n";
    // Write composition arcs in paren section
    if (!meta.references.empty()) {
      WriteIndent(os, depth + 1, opts.indent);
      os << "prepend references = [\n";
      for (const auto& ref : meta.references) {
        WriteIndent(os, depth + 2, opts.indent);
        os << "@" << ref << "@,\n";
      }
      WriteIndent(os, depth + 1, opts.indent);
      os << "]\n";
    }
    if (!meta.payloads.empty()) {
      WriteIndent(os, depth + 1, opts.indent);
      os << "prepend payload = [\n";
      for (const auto& payload : meta.payloads) {
        WriteIndent(os, depth + 2, opts.indent);
        os << "@" << payload << "@,\n";
      }
      WriteIndent(os, depth + 1, opts.indent);
      os << "]\n";
    }
    WriteIndent(os, depth, opts.indent);
    os << ")";
  }

  os << "\n";
  WriteIndent(os, depth, opts.indent);
  os << "{\n";

  int content_depth = depth + 1;

  // Write remaining metadata inside braces
  if (!meta.active) {
    WriteIndent(os, content_depth, opts.indent);
    os << "active = false\n";
  }
  if (meta.hidden) {
    WriteIndent(os, content_depth, opts.indent);
    os << "hidden = true\n";
  }
  if (!meta.doc().empty() && opts.include_comments) {
    WriteIndent(os, content_depth, opts.indent);
    os << "doc = " << EscapeString(meta.doc()) << "\n";
  }
  if (!meta.apiSchemas().empty()) {
    WriteIndent(os, content_depth, opts.indent);
    os << "apiSchemas = [";
    for (size_t i = 0; i < meta.apiSchemas().size(); ++i) {
      if (i > 0) os << ", ";
      os << "\"" << meta.apiSchemas()[i] << "\"";
    }
    os << "]\n";
  }

  // Get property slots
  std::vector<const PropSlot*> prop_slots;
  for (const auto& slot : spec.properties().slots()) {
    if (!slot.is_relationship()) {
      prop_slots.push_back(&slot);
    }
  }

  // Optionally sort by name
  if (opts.sort_properties) {
    PropNameTable& name_table = GetPropNameTable();
    std::sort(prop_slots.begin(), prop_slots.end(),
              [&name_table](const PropSlot* a, const PropSlot* b) {
                return name_table.get(a->name_id) < name_table.get(b->name_id);
              });
  }

  // Write properties
  for (const PropSlot* slot : prop_slots) {
    WriteProperty(os, *slot, spec, content_depth, opts);
  }

  // Write relationships
  PropNameTable& name_table = GetPropNameTable();
  for (const auto& slot : spec.properties().slots()) {
    if (slot.is_relationship()) {
      const std::string& name = name_table.get(slot.name_id);
      const std::vector<Path>* targets = spec.relationship(name);
      if (targets) {
        WriteRelationship(os, name, *targets, content_depth, opts);
      }
    }
  }

  // Write children
  for (uint32_t child_idx : spec.child_indices()) {
    const PrimSpec* child = layer.prim(child_idx);
    if (child) {
      os << "\n";
      WritePrimSpec(os, *child, layer, content_depth, opts);
    }
  }

  WriteIndent(os, depth, opts.indent);
  os << "}\n";
}

}  // anonymous namespace

// ============================================================
// Public API implementations
// ============================================================

std::string WriteUSDAToString(const Stage& stage, const USDAWriteOptions& options) {
  std::ostringstream ss;
  WriteUSDA(ss, stage, options);
  return ss.str();
}

USDAWriteResult WriteUSDA(std::ostream& os, const Stage& stage,
                           const USDAWriteOptions& options) {
  USDAWriteResult result;

  const Layer* root_layer = stage.GetRootLayer();
  if (!root_layer) {
    result.error = "Stage has no root layer";
    return result;
  }

  std::streampos start_pos = os.tellp();

  // Write layer header using stage metadata
  LayerMeta meta;
  const StageMeta& stage_meta = stage.GetMeta();
  meta.defaultPrim = stage_meta.defaultPrim;
  meta.upAxis = stage_meta.upAxis;
  meta.metersPerUnit = stage_meta.metersPerUnit;
  meta.timeCodesPerSecond = stage_meta.timeCodesPerSecond;
  meta.startTimeCode = stage_meta.startTimeCode;
  meta.endTimeCode = stage_meta.endTimeCode;
  meta.doc = stage_meta.doc;

  WriteLayerMeta(os, meta, options);

  // Write root prims
  for (uint32_t root_idx : root_layer->root_indices()) {
    const PrimSpec* root = root_layer->prim(root_idx);
    if (root) {
      WritePrimSpec(os, *root, *root_layer, 0, options);
      os << "\n";
    }
  }

  if (os.good()) {
    result.success = true;
    std::streampos end_pos = os.tellp();
    if (start_pos != std::streampos(-1) && end_pos != std::streampos(-1)) {
      result.bytes_written = static_cast<size_t>(end_pos - start_pos);
    }
  } else {
    result.error = "Stream write error";
  }

  return result;
}

USDAWriteResult WriteUSDAToFile(const std::string& filename, const Stage& stage,
                                 const USDAWriteOptions& options) {
  return WriteUSDAToFile(filename.c_str(), stage, options);
}

USDAWriteResult WriteUSDAToFile(const char* filename, const Stage& stage,
                                 const USDAWriteOptions& options) {
  USDAWriteResult result;

  if (!filename) {
    result.error = "Null filename";
    return result;
  }

  std::ofstream ofs(filename, std::ios::out | std::ios::binary);
  if (!ofs) {
    result.error = "Failed to open file for writing: ";
    result.error += filename;
    return result;
  }

  result = WriteUSDA(ofs, stage, options);

  if (!ofs.good() && result.success) {
    result.success = false;
    result.error = "Error writing to file";
  }

  return result;
}

std::string WriteLayerToString(const Layer& layer, const USDAWriteOptions& options) {
  std::ostringstream ss;
  WriteLayer(ss, layer, options);
  return ss.str();
}

USDAWriteResult WriteLayer(std::ostream& os, const Layer& layer,
                            const USDAWriteOptions& options) {
  USDAWriteResult result;

  std::streampos start_pos = os.tellp();

  // Write layer header
  WriteLayerMeta(os, layer.meta(), options);

  // Write root prims
  for (uint32_t root_idx : layer.root_indices()) {
    const PrimSpec* root = layer.prim(root_idx);
    if (root) {
      WritePrimSpec(os, *root, layer, 0, options);
      os << "\n";
    }
  }

  if (os.good()) {
    result.success = true;
    std::streampos end_pos = os.tellp();
    if (start_pos != std::streampos(-1) && end_pos != std::streampos(-1)) {
      result.bytes_written = static_cast<size_t>(end_pos - start_pos);
    }
  } else {
    result.error = "Stream write error";
  }

  return result;
}

USDAWriteResult WriteLayerToFile(const std::string& filename, const Layer& layer,
                                  const USDAWriteOptions& options) {
  USDAWriteResult result;

  std::ofstream ofs(filename, std::ios::out | std::ios::binary);
  if (!ofs) {
    result.error = "Failed to open file for writing: " + filename;
    return result;
  }

  result = WriteLayer(ofs, layer, options);

  if (!ofs.good() && result.success) {
    result.success = false;
    result.error = "Error writing to file";
  }

  return result;
}

}  // namespace next
}  // namespace tinyusdz
