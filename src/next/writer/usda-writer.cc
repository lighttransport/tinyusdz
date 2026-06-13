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

// Prefix every line of `s` after the first with `prefix`. Used to re-indent a
// multi-line dictionary value (from PrintValue) under its metadata key so the
// `{` stays inline and the body/closing brace align with the surrounding depth.
std::string IndentContinuation(const std::string& s, const std::string& prefix) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out += c;
    if (c == '\n') out += prefix;
  }
  return out;
}

// Render a dictionary-valued metadatum as `<key> = { ... }` with body indented
// `level` units. Returns empty if the value is not a (non-empty) dictionary.
std::string DictMetaLine(const std::string& key, const Value& v, int level,
                         const USDAWriteOptions& opts) {
  if (!v.is_dictionary()) return "";
  const Dict* d = v.as_dictionary();
  if (!d || d->empty()) return "";
  PrintOptions print_opts;
  print_opts.indent = opts.indent;
  std::string base;
  for (int i = 0; i < level; ++i) base += opts.indent;
  return key + " = " + IndentContinuation(PrintValue(v, print_opts), base);
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

  if (!meta.comment.empty() && opts.include_comments) {
    lines.push_back(opts.indent + "comment = " + EscapeString(meta.comment));
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

  if (meta.framesPerSecond_set) {
    std::ostringstream ss;
    ss << std::setprecision(opts.double_precision) << meta.framesPerSecond;
    lines.push_back(opts.indent + "framesPerSecond = " + ss.str());
  }

  if (meta.kilogramsPerUnit_set) {
    std::ostringstream ss;
    ss << std::setprecision(opts.double_precision) << meta.kilogramsPerUnit;
    lines.push_back(opts.indent + "kilogramsPerUnit = " + ss.str());
  }

  if (!meta.colorConfiguration.empty()) {
    lines.push_back(opts.indent + "colorConfiguration = @" +
                    meta.colorConfiguration + "@");
  }

  if (!meta.colorManagementSystem.empty()) {
    lines.push_back(opts.indent + "colorManagementSystem = " +
                    EscapeString(meta.colorManagementSystem));
  }

  {
    std::string s = DictMetaLine("customLayerData", meta.customLayerData, 1, opts);
    if (!s.empty()) lines.push_back(opts.indent + s);
    s = DictMetaLine("expressionVariables", meta.expressionVariables, 1, opts);
    if (!s.empty()) lines.push_back(opts.indent + s);
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

// Emit the property metadata `( ... )` block (interpolation, customData, ...)
// when authored. Streams " (\n ... )" with NO trailing newline so the caller can
// place it before the property's terminating newline. Returns true if emitted.
bool WritePropMeta(std::ostream& os, const PrimSpec& spec, PropNameId name_id,
                   int depth, const USDAWriteOptions& opts) {
  const PropMeta* m = spec.property_meta(name_id);
  if (!m || m->empty()) return false;
  const int md = depth + 1;
  os << " (\n";
  auto kv = [&](const std::string& s) {
    WriteIndent(os, md, opts.indent);
    os << s << "\n";
  };
  if (m->authored & PropMeta::kInterpolation)
    kv("interpolation = " + EscapeString(m->interpolation));
  if (m->authored & PropMeta::kElementSize)
    kv("elementSize = " + std::to_string(m->elementSize));
  if (m->authored & PropMeta::kColorSpace)
    kv("colorSpace = " + EscapeString(m->colorSpace));
  if (m->authored & PropMeta::kCustomData)
    kv(DictMetaLine("customData", m->customData, md, opts));
  if (m->authored & PropMeta::kDisplayName)
    kv("displayName = " + EscapeString(m->displayName));
  if (m->authored & PropMeta::kDisplayGroup)
    kv("displayGroup = " + EscapeString(m->displayGroup));
  if (m->authored & PropMeta::kDoc)
    kv("doc = " + EscapeString(m->doc));
  if (m->authored & PropMeta::kHidden)
    kv(std::string("hidden = ") + (m->hidden ? "true" : "false"));
  if (m->authored & PropMeta::kRenderType)
    kv("renderType = " + EscapeString(m->renderType));
  if (m->authored & PropMeta::kConnectability)
    kv("connectability = " + EscapeString(m->connectability));
  if (m->authored & PropMeta::kOutputName)
    kv("outputName = " + EscapeString(m->outputName));
  if (m->authored & PropMeta::kBindMaterialAs)
    kv("bindMaterialAs = " + EscapeString(m->bindMaterialAs));
  if (m->authored & PropMeta::kKind)
    kv("kind = " + EscapeString(m->kind));
  if (m->authored & PropMeta::kWeight) {
    std::ostringstream ss;
    ss << std::setprecision(opts.double_precision) << m->weight;
    kv("weight = " + ss.str());
  }
  if (m->authored & PropMeta::kUnauthoredIdx)
    kv("unauthoredValuesIndex = " + std::to_string(m->unauthoredValuesIndex));
  if (m->authored & PropMeta::kAllowedTokens) {
    std::string s = "allowedTokens = [";
    for (size_t i = 0; i < m->allowedTokens.size(); ++i) {
      if (i) s += ", ";
      s += EscapeString(m->allowedTokens[i]);
    }
    s += "]";
    kv(s);
  }
  if (m->authored & PropMeta::kAssetInfo)
    kv(DictMetaLine("assetInfo", m->assetInfo, md, opts));
  if (m->authored & PropMeta::kSdrMetadata)
    kv(DictMetaLine("sdrMetadata", m->sdrMetadata, md, opts));
  WriteIndent(os, depth, opts.indent);
  os << ")";
  return true;
}

// Write time samples for a property
void WriteTimeSamples(std::ostream& os, const std::string& name, PropNameId name_id,
                      const PrimSpec& spec, int depth, const USDAWriteOptions& opts) {
  const auto* samples = spec.time_samples(name_id);
  if (!samples || samples->empty()) return;

  WriteIndent(os, depth, opts.indent);

  // Type name: prefer the declared name, else derive from the first sample.
  if (const std::string* decl = spec.property_type_name(name)) {
    os << *decl;
  } else {
    const Value* first_val = spec.time_sample_value(samples->front().second);
    if (first_val) {
      const char* tn = GetTypeName(first_val->type_id());
      os << (tn ? tn : "token");
      if (first_val->is_array()) os << "[]";
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
  os << "}";  // caller appends optional metadata block + newline
}

// Write a property value
void WriteProperty(std::ostream& os, const PropSlot& slot, const PrimSpec& spec,
                   int depth, const USDAWriteOptions& opts) {
  PropNameTable& name_table = GetPropNameTable();
  const std::string& name = name_table.get(slot.name_id);

  // Check if this property has time samples
  if (slot.is_time_sampled() && spec.has_time_samples(slot.name_id)) {
    WriteTimeSamples(os, name, slot.name_id, spec, depth, opts);
    WritePropMeta(os, spec, slot.name_id, depth, opts);
    os << "\n";
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

  // Write type name. Prefer the declared name (round-trips string[] vs token[],
  // role types, and value-less / connection-only attrs whose stored value type
  // is Invalid). GetTypeName returns nullptr for Invalid, so guard the fallback.
  TypeId type_id = static_cast<TypeId>(slot.value_type);
  if (const std::string* decl = spec.property_type_name(name)) {
    os << *decl;
  } else {
    const char* tn = GetTypeName(type_id);
    os << (tn ? tn : "token");
    if (slot.is_array()) os << "[]";
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
    WritePropMeta(os, spec, slot.name_id, depth, opts);
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

  WritePropMeta(os, spec, slot.name_id, depth, opts);
  os << "\n";
}

// Write relationship
void WriteRelationship(std::ostream& os, const std::string& name,
                       const std::vector<Path>& targets, const PrimSpec& spec,
                       PropNameId name_id, int depth,
                       const USDAWriteOptions& opts) {
  WriteIndent(os, depth, opts.indent);
  os << "rel " << name;

  if (targets.empty()) {
    os << " = None";
  } else if (targets.size() == 1) {
    os << " = <" << targets[0].str() << ">";
  } else {
    os << " = [\n";
    for (const auto& target : targets) {
      WriteIndent(os, depth + 1, opts.indent);
      os << "<" << target.str() << ">,\n";
    }
    WriteIndent(os, depth, opts.indent);
    os << "]";
  }
  WritePropMeta(os, spec, name_id, depth, opts);
  os << "\n";
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

  // All prim metadata goes in the parenthesized header section so it re-parses
  // (the parser reads prim metadata from `( ... )`, never from the prim body).
  const PrimSpecMeta& meta = spec.meta();
  const bool has_doc = !meta.doc().empty() && opts.include_comments;
  const bool has_comment = !meta.comment().empty() && opts.include_comments;
  auto has_dict = [](const Value& v) {
    return v.is_dictionary() && v.as_dictionary() && !v.as_dictionary()->empty();
  };
  const bool has_customData = has_dict(meta.customData());
  const bool has_assetInfo = has_dict(meta.assetInfo());
  const bool has_sdr = has_dict(meta.sdrMetadata());
  const bool has_clips = has_dict(meta.clips());
  bool has_meta = !meta.active || meta.hidden || meta.instanceable ||
                  !meta.kind().empty() || !meta.displayName().empty() ||
                  has_doc || has_comment || !meta.apiSchemas().empty() ||
                  has_customData || has_assetInfo || has_sdr || has_clips ||
                  !meta.references.empty() || !meta.payloads.empty() ||
                  !meta.inherits.empty() || !meta.specializes.empty() ||
                  !meta.variantSelections().empty() ||
                  !meta.variantSelection.empty();

  if (has_meta) {
    const int md = depth + 1;
    os << " (\n";
    auto kv = [&](const std::string& s) {
      WriteIndent(os, md, opts.indent);
      os << s << "\n";
    };
    if (!meta.active) kv("active = false");
    if (meta.hidden) kv("hidden = true");
    if (meta.instanceable) kv("instanceable = true");
    if (!meta.kind().empty()) kv("kind = " + EscapeString(meta.kind()));
    if (!meta.displayName().empty())
      kv("displayName = " + EscapeString(meta.displayName()));
    if (has_doc) kv("doc = " + EscapeString(meta.doc()));
    if (has_comment) kv("comment = " + EscapeString(meta.comment()));
    if (!meta.apiSchemas().empty()) {
      std::string s = "apiSchemas = [";
      for (size_t i = 0; i < meta.apiSchemas().size(); ++i) {
        if (i > 0) s += ", ";
        s += "\"" + meta.apiSchemas()[i] + "\"";
      }
      s += "]";
      kv(s);
    }
    if (has_customData) kv(DictMetaLine("customData", meta.customData(), md, opts));
    if (has_assetInfo) kv(DictMetaLine("assetInfo", meta.assetInfo(), md, opts));
    if (has_sdr) kv(DictMetaLine("sdrMetadata", meta.sdrMetadata(), md, opts));
    if (has_clips) kv(DictMetaLine("clips", meta.clips(), md, opts));

    // Composition arcs, re-emitting the authored list-op qualifier (Phase 7 S5):
    // a bare/explicit list as `references = [...]`, otherwise the
    // prepend/append/delete sublists. With no recorded edit the inline list is
    // an implicit explicit (bare) list.
    const ArcListOpEdits* edits = meta.arc_edits();
    auto write_arc = [&](const char* field, const std::vector<std::string>& inl,
                         const ArcEdit* e) {
      auto emit = [&](const char* qual, const std::vector<std::string>& items) {
        if (items.empty()) return;
        WriteIndent(os, md, opts.indent);
        os << qual << field << " = [\n";
        for (const auto& a : items) {
          WriteIndent(os, md + 1, opts.indent);
          os << "@" << a << "@,\n";
        }
        WriteIndent(os, md, opts.indent);
        os << "]\n";
      };
      if (!e || !e->authored || e->is_explicit) {
        emit("", inl);  // bare/explicit list
      } else {
        emit("prepend ", e->prepended);
        emit("append ", e->appended);
        emit("delete ", e->deleted);
      }
    };
    write_arc("references", meta.references,
              edits ? &edits->references : nullptr);
    write_arc("payload", meta.payloads, edits ? &edits->payloads : nullptr);
    write_arc("inherits", meta.inherits, edits ? &edits->inherits : nullptr);
    write_arc("specializes", meta.specializes,
              edits ? &edits->specializes : nullptr);

    // Variant selections: `variants = { string <set> = "<sel>" }`.
    auto write_variants = [&](const std::vector<std::pair<std::string, std::string>>& sels) {
      WriteIndent(os, md, opts.indent);
      os << "variants = {\n";
      for (const auto& sel : sels) {
        WriteIndent(os, md + 1, opts.indent);
        os << "string " << sel.first << " = " << EscapeString(sel.second) << "\n";
      }
      WriteIndent(os, md, opts.indent);
      os << "}\n";
    };
    if (!meta.variantSelections().empty()) {
      write_variants(meta.variantSelections());
    } else if (!meta.variantSelection.empty()) {
      auto eq = meta.variantSelection.find('=');
      if (eq != std::string::npos) {
        write_variants({{meta.variantSelection.substr(0, eq),
                         meta.variantSelection.substr(eq + 1)}});
      }
    }

    WriteIndent(os, depth, opts.indent);
    os << ")";
  }

  os << "\n";
  WriteIndent(os, depth, opts.indent);
  os << "{\n";

  int content_depth = depth + 1;

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

  // Write relationships. These live in the relationship map (add_relationship
  // does not create a PropSlot), so iterate the map's names, not prop slots.
  PropNameTable& name_table = GetPropNameTable();
  for (const std::string& rel_name : spec.relationship_names()) {
    const std::vector<Path>* targets = spec.relationship(rel_name);
    if (!targets) continue;
    PropNameId rid = name_table.find(rel_name);
    WriteRelationship(os, rel_name, *targets, spec, rid, content_depth, opts);
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
  meta.framesPerSecond = stage_meta.framesPerSecond;
  meta.framesPerSecond_set = stage_meta.framesPerSecond_set;
  meta.kilogramsPerUnit = stage_meta.kilogramsPerUnit;
  meta.kilogramsPerUnit_set = stage_meta.kilogramsPerUnit_set;
  meta.colorConfiguration = stage_meta.colorConfiguration;
  meta.colorManagementSystem = stage_meta.colorManagementSystem;
  meta.doc = stage_meta.doc;
  meta.comment = stage_meta.comment;
  // Dictionary-valued stage metadata is not mirrored on StageMeta; take it from
  // the composed root layer directly.
  meta.customLayerData = root_layer->meta().customLayerData;
  meta.expressionVariables = root_layer->meta().expressionVariables;

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
