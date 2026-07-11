// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Crate Reader stage reconstruction

#include "crate-reader-internal.hh"
#include "../layer/layer.hh"
#include "../parser/lexer.hh"          // dict-as-USDA-text decode
#include "../parser/value-parser.hh"  // ParseDict
#include "../types/type-info.hh"

#include <algorithm>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tinyusdz {
namespace next {

// Decode a dictionary that was written as USDA dict text in a String field
// (see crate-writer add_dict_field). Returns an empty Value on failure.
static Value ParseDictText(const std::string& text) {
  if (text.empty()) return Value();
  Lexer lexer(text.data(), text.size());
  ParseResult r = ParseDict(lexer);
  return r.success ? std::move(r.value) : Value();
}


bool CrateReader::Impl::BuildStage() {
  // Create layer and builder
  Layer layer;
  LayerBuilder builder(layer);
  constexpr size_t kReportInterval = 512;
  auto report_stage = [&](const char* phase, size_t current,
                          size_t total) -> bool {
    if (current == 0 || current == total ||
        (current % kReportInterval) == 0) {
      return ReportProgress(phase, current, total);
    }
    return true;
  };

  // First, process the PseudoRoot spec to extract layer metadata
  // Authored child order per prim path (from primChildren fields; "/" = the
  // pseudo-root's list), applied after the sorted hierarchy build.
  std::unordered_map<std::string, std::vector<std::string>> prim_children_order;

  for (const auto& spec : specs_) {
    if (spec.spec_type != SpecType::PseudoRoot) continue;
    if (spec.path_index.value >= paths_.size()) continue;

    std::vector<std::pair<std::string, Value>> fields;
    if (!ResolveFieldset(spec.fieldset_index.value, fields)) {
      AddWarning("Failed to resolve pseudo-root fieldset");
    }

    for (auto& field : fields) {
      if (field.first == "primChildren") {
        if (const std::vector<std::string>* names =
                field.second.as_token_array()) {
          if (!names->empty()) prim_children_order["/"] = *names;
        }
      } else if (field.first == "defaultPrim") {
        if (const std::string* s = field.second.as_token())
          layer.meta().defaultPrim = *s;
      } else if (field.first == "upAxis") {
        if (const std::string* s = field.second.as_token()) {
          layer.meta().upAxis = *s;
          layer.meta().upAxis_set = true;
        }
      } else if (field.first == "metersPerUnit") {
        const double* d = field.second.as_double();
        if (d) {
          layer.meta().metersPerUnit = *d;
          layer.meta().metersPerUnit_set = true;
        }
      } else if (field.first == "timeCodesPerSecond") {
        const double* d = field.second.as_double();
        if (d) {
          layer.meta().timeCodesPerSecond = *d;
          layer.meta().timeCodesPerSecond_set = true;
        }
      } else if (field.first == "startTimeCode") {
        const double* d = field.second.as_double();
        if (d) {
          layer.meta().startTimeCode = *d;
          layer.meta().startTimeCode_set = true;
        }
      } else if (field.first == "endTimeCode") {
        const double* d = field.second.as_double();
        if (d) {
          layer.meta().endTimeCode = *d;
          layer.meta().endTimeCode_set = true;
        }
      } else if (field.first == "framesPerSecond") {
        const double* d = field.second.as_double();
        if (d) {
          layer.meta().framesPerSecond = *d;
          layer.meta().framesPerSecond_set = true;
        }
      } else if (field.first == "kilogramsPerUnit") {
        const double* d = field.second.as_double();
        if (d) {
          layer.meta().kilogramsPerUnit = *d;
          layer.meta().kilogramsPerUnit_set = true;
        }
      } else if (field.first == "customLayerData" ||
                 field.first == "expressionVariables") {
        // pxr-authored: binary VtDictionary; next-authored: USDA dict text.
        Value d;
        if (field.second.is_dictionary()) {
          d = std::move(field.second);
        } else if (const std::string* s = field.second.as_string()) {
          d = ParseDictText(*s);
        } else if (const std::string* s = field.second.as_token()) {
          d = ParseDictText(*s);
        }
        if (d.is_dictionary()) {
          if (field.first == "customLayerData")
            layer.meta().customLayerData = std::move(d);
          else
            layer.meta().expressionVariables = std::move(d);
        }
      } else if (field.first == "doc") {
        if (const std::string* s = field.second.as_string())
          layer.meta().doc = *s;
      } else if (field.first == "comment") {
        if (const std::string* s = field.second.as_string())
          layer.meta().comment = *s;
      } else if (field.first == "subLayerOffsets") {
        if (const std::vector<double>* arr = field.second.as_double_array()) {
          layer.meta().subLayerOffsets.clear();
          for (size_t i = 0; i + 1 < arr->size(); i += 2) {
            layer.meta().subLayerOffsets.emplace_back((*arr)[i], (*arr)[i + 1]);
          }
        }
      } else if (field.first == "subLayers") {
        if (const std::vector<std::string>* arr = field.second.as_token_array()) {
          for (const auto& s : *arr) layer.meta().subLayers.push_back(s);
        } else if (const std::string* s = field.second.as_string()) {
          layer.meta().subLayers.push_back(*s);
        } else if (const std::string* s = field.second.as_token()) {
          layer.meta().subLayers.push_back(*s);
        }
      }
    }
    break; // Only process first PseudoRoot
  }

  // Process prim specs to build prims with hierarchy from paths
  struct PrimEntry {
    std::string full_path;
    std::string name;
    std::string type_name;
    PrimSpecifier specifier;
    uint32_t fieldset_index = 0;
  };

  // Variant content lives in the layer as bracketed holder/child prims
  // ("/Prim/{vset=sel}" + descendants); their attributes attach to those prims
  // via attr_map (so the layer fully represents the variants, enabling both the
  // compositor — which reads the selected holder — and unflattened round-trip).
  // We only need to know, per owning prim, which sets are selected.
  //   owning prim path -> { variantSet -> selected variant } (all selections).
  std::unordered_map<std::string, std::map<std::string, std::string>> variant_sel;
  // Variant OPTION names per owning prim/set, recovered from the holder specs
  // ("/Prim/{set=var}"): lets the model expose the full option list (not just
  // the selection) after a crate load.
  std::unordered_map<std::string, std::map<std::string, std::vector<std::string>>>
      variant_opts;

  std::vector<PrimEntry> prim_entries;
  std::vector<std::pair<std::string, ValueRep>> raw_field_scratch;
  std::vector<std::pair<std::string, Value>> value_field_scratch;
  if (!report_stage("stage.prims", 0, specs_.size())) return false;
  for (size_t spec_index = 0; spec_index < specs_.size(); ++spec_index) {
    if (spec_index > 0 &&
        !report_stage("stage.prims", spec_index, specs_.size())) {
      return false;
    }
    const auto& spec = specs_[spec_index];
    // Build Prim specs AND variant-namespace holders (Variant/VariantSet specs
    // at "/Prim/{vset=sel}" / "/Prim/{vset=}"). The holders are needed so that
    // variant CHILD prims ("/Prim/{vset=sel}/Geo") get a correct path from the
    // depth-stack builder (begin_prim recomputes path from its parent). Holders
    // and variant sub-prims are grafted onto the owning prim by the compositor
    // on selection and skipped by the writer (their paths contain '{').
    const bool is_prim = spec.spec_type == SpecType::Prim;
    const bool is_variant = spec.spec_type == SpecType::Variant ||
                            spec.spec_type == SpecType::VariantSet;
    if (!is_prim && !is_variant) continue;

    if (spec.path_index.value >= paths_.size()) continue;
    std::string full_path = paths_[spec.path_index.value];
    if (full_path.empty() || full_path == "/") continue;
    const bool bracketed = full_path.find('{') != std::string::npos;
    // Only real (non-bracketed) Prim specs carry a variantSelection.
    if (is_variant && !bracketed) continue;  // defensive

    // Record option names from holder specs "<owner>/{set=var}". The owner
    // is the path before the LAST bracketed component and may itself be a
    // holder ("/P/{a=x}/{b=y}" — a nested variant set on option {a=x}).
    if (bracketed && full_path.back() == '}') {
      size_t lb = full_path.rfind("/{");
      if (lb != std::string::npos) {
        const std::string nm = full_path.substr(lb + 2,
                                                full_path.size() - lb - 3);
        size_t eq = nm.find('=');
        if (eq != std::string::npos && eq + 1 < nm.size() &&
            nm.find('{') == std::string::npos) {
          variant_opts[full_path.substr(0, lb)][nm.substr(0, eq)]
              .push_back(nm.substr(eq + 1));
        }
      }
    }

    // Decode the prim's variantSelection (a VariantSelectionMap that
    // UnpackValue/ResolveFieldset cannot represent). Captures every selection
    // so prims with multiple variant sets compose correctly. Holder specs
    // carry a variantSelection too when they own NESTED variant sets.
    if (ResolveFieldsetRaw(spec.fieldset_index.value, raw_field_scratch)) {
      for (auto& f : raw_field_scratch) {
        if (f.first == "variantSelection") {
          std::vector<std::pair<std::string, std::string>> sels;
          if (DecodeVariantSelectionMap(f.second, sels)) {
            for (auto& kv : sels) variant_sel[full_path][kv.first] = kv.second;
          }
          break;
        }
      }
    }

    size_t last_slash = full_path.rfind('/');
    std::string prim_name = (last_slash != std::string::npos && last_slash < full_path.size() - 1)
                            ? full_path.substr(last_slash + 1) : full_path;
    if (prim_name.empty()) continue;

    PrimEntry entry;
    entry.full_path = full_path;
    entry.name = prim_name;
    entry.fieldset_index = spec.fieldset_index.value;

    ResolveFieldset(spec.fieldset_index.value, value_field_scratch);

    // Untyped prims stay untyped: composition fills the type from the
    // referenced prim (a forced "Xform" default would mask e.g. a referenced
    // Mesh definition; the writer already skips empty type names).
    entry.type_name.clear();
    entry.specifier = PrimSpecifier::Def;
    for (auto& f : value_field_scratch) {
      if (f.first == "typeName") {
        if (const std::string* s = f.second.as_token()) entry.type_name = *s;
      } else if (f.first == "specifier") {
        if (const std::string* s = f.second.as_token()) {
          if (*s == "over") entry.specifier = PrimSpecifier::Over;
          else if (*s == "class") entry.specifier = PrimSpecifier::Class;
        }
      }
    }
    prim_entries.push_back(std::move(entry));
  }
  if (!report_stage("stage.prims", specs_.size(), specs_.size())) {
    return false;
  }

  // Collect separate Attribute and Relationship specs. pxrUSD-authored crates
  // store each property as its own spec at "<primpath>/<name>" (rendered with a
  // leading '.' marker by ReadPaths), rather than inline in the prim's
  // fieldset. We capture, for each property: its declared typeName, its
  // `default` value (lazy if an array — no payload decode), any connection
  // targets, relationship targets, and uniform variability. These are attached
  // to the parent prim below so the writer can faithfully re-emit them.
  struct AttrInfo {
    std::string name;
    std::string type_name;
    bool has_default = false;
    Value default_value;
    bool is_connection = false;
    std::vector<std::string> connection_targets;
    bool uniform = false;
    bool custom = false;
    std::vector<std::pair<double, Value>> time_samples;
    // Per-property metadata (round-tripped via attribute spec fields).
    std::string interpolation;
    std::string color_space;
    int32_t element_size = 1;
    bool has_interpolation = false;
    bool has_color_space = false;
    bool has_element_size = false;
    Value custom_data;  // dictionary
  };
  struct RelInfo {
    std::string name;
    std::vector<std::string> targets;
    ArcEdit edits;        // authored list-op sublists (marker-decoded)
    bool custom = false;
    bool uniform = false;
  };
  std::unordered_map<std::string, std::vector<AttrInfo>> attr_map;
  std::unordered_map<std::string, std::vector<RelInfo>> rel_map;
  std::vector<std::pair<std::string, ValueRep>> property_raw_field_scratch;

  // Split a property spec path ".<primpath>/<name>" into (primpath, name).
  auto split_prop_path = [](const std::string& raw, std::string& prim_path,
                            std::string& prop_name) -> bool {
    if (raw.empty()) return false;
    size_t begin = (raw[0] == '.') ? 1 : 0;
    size_t slash = raw.rfind('/');
    if (slash == std::string::npos || slash < begin) return false;
    prim_path = raw.substr(begin, slash - begin);
    prop_name = raw.substr(slash + 1);
    return !prim_path.empty() && !prop_name.empty();
  };

  if (!report_stage("stage.properties", 0, specs_.size())) return false;
  for (size_t spec_index = 0; spec_index < specs_.size(); ++spec_index) {
    if (spec_index > 0 &&
        !report_stage("stage.properties", spec_index, specs_.size())) {
      return false;
    }
    const auto& spec = specs_[spec_index];
    const bool is_attr = spec.spec_type == SpecType::Attribute;
    const bool is_rel = spec.spec_type == SpecType::Relationship;
    if (!is_attr && !is_rel) continue;
    if (spec.path_index.value >= paths_.size()) continue;
    const std::string& raw_path = paths_[spec.path_index.value];

    // Variant attributes (bracketed paths) attach to their bracketed holder /
    // child prim like any other attribute (split_prop_path handles them): e.g.
    // ".Prim/{vset=sel}/prop" -> prim_path "/Prim/{vset=sel}" (the holder),
    // ".Prim/{vset=sel}/Geo/attr" -> "/Prim/{vset=sel}/Geo" (a variant child).
    // The compositor reads the selected holder; the writer re-emits it.
    std::string prim_path, prop_name;
    if (!split_prop_path(raw_path, prim_path, prop_name)) {
      continue;
    }

    if (!ResolveFieldsetRaw(spec.fieldset_index.value, property_raw_field_scratch)) continue;

    if (is_rel) {
      RelInfo ri;
      ri.name = std::move(prop_name);
      for (auto& f : property_raw_field_scratch) {
        if (f.first == "targetPaths") {
          std::vector<std::string> raw;
          DecodePathTargets(f.second, raw, /*with_markers=*/true);
          if (!raw.empty() && !raw[0].empty() && raw[0][0] == '\x01') {
            // Non-explicit list op: sublists are marker-delimited.
            ri.edits.authored = true;
            ri.edits.is_explicit = false;
            std::vector<std::string>* cur = nullptr;
            for (std::string& t : raw) {
              if (t == "\x01" "P") cur = &ri.edits.prepended;
              else if (t == "\x01" "A") cur = &ri.edits.appended;
              else if (t == "\x01" "D") cur = &ri.edits.deleted;
              else if (t == "\x01" "O") cur = &ri.edits.ordered;
              else if (cur) cur->push_back(std::move(t));
            }
            // Within-spec effective list: prepended then appended.
            ri.targets = ri.edits.prepended;
            ri.targets.insert(ri.targets.end(), ri.edits.appended.begin(),
                              ri.edits.appended.end());
          } else {
            ri.targets = std::move(raw);
          }
        } else if (f.first == "custom") {
          Value v;
          if (UnpackValue(f.second, v)) {
            if (const bool* b = v.as_bool()) ri.custom = *b;
          }
        } else if (f.first == "variability") {
          Value v;
          if (UnpackValue(f.second, v)) {
            if (const std::string* s = v.as_token()) ri.uniform = (*s == "uniform");
          }
        }
      }
      rel_map[prim_path].push_back(std::move(ri));
      continue;
    }

    // Attribute spec.
    AttrInfo ai;
    ai.name = std::move(prop_name);
    for (auto& f : property_raw_field_scratch) {
      if (f.first == "typeName") {
        Value v;
        if (UnpackValue(f.second, v)) {
          if (const std::string* s = v.as_token()) ai.type_name = *s;
        }
      } else if (f.first == "default") {
        Value v;
        if (UnpackValue(f.second, v)) {
          ai.default_value = std::move(v);
          ai.has_default = true;
        }
      } else if (f.first == "timeSamples") {
        DecodeTimeSamples(f.second, &ai.time_samples);
      } else if (f.first == "connectionPaths") {
        if (DecodePathTargets(f.second, ai.connection_targets)) {
          // Present-but-empty = authored connection block (`.connect = None`).
          ai.is_connection = true;
        }
      } else if (f.first == "variability") {
        Value v;
        if (UnpackValue(f.second, v)) {
          if (const std::string* s = v.as_token()) ai.uniform = (*s == "uniform");
        }
      } else if (f.first == "custom") {
        // The legacy `custom` qualifier (pxr stores a bool field). Preserved on
        // read; the USDA writer only re-emits it under emit_custom/--openusd-compat.
        Value v;
        if (UnpackValue(f.second, v)) {
          if (const bool* b = v.as_bool()) ai.custom = *b;
        }
      } else if (f.first == "interpolation") {
        Value v;
        if (UnpackValue(f.second, v)) {
          if (const std::string* s = v.as_token()) {
            ai.interpolation = *s;
            ai.has_interpolation = true;
          }
        }
      } else if (f.first == "colorSpace") {
        Value v;
        if (UnpackValue(f.second, v)) {
          if (const std::string* s = v.as_token()) {
            ai.color_space = *s;
            ai.has_color_space = true;
          }
        }
      } else if (f.first == "elementSize") {
        Value v;
        if (UnpackValue(f.second, v)) {
          if (const int32_t* i = v.as_int()) {
            ai.element_size = *i;
            ai.has_element_size = true;
          }
        }
      } else if (f.first == "customData") {
        Value v;
        if (UnpackValue(f.second, v)) {
          if (v.is_dictionary()) {
            ai.custom_data = std::move(v);
          } else if (const std::string* s = v.as_string()) {
            ai.custom_data = ParseDictText(*s);
          } else if (const std::string* s = v.as_token()) {
            ai.custom_data = ParseDictText(*s);
          }
        }
      }
    }
    // Role types (texCoord2f, point3f, color3h, ...) exist in the crate only
    // via the declared typeName — the value itself is stored as its base type
    // (Vec2f, Vec3f, ...). Re-tag so the in-memory Value matches what the
    // usda parser produces for the same authoring.
    if (ai.has_default && !ai.type_name.empty()) {
      std::string base = ai.type_name;
      if (base.size() >= 2 && base.compare(base.size() - 2, 2, "[]") == 0) {
        base.resize(base.size() - 2);
      }
      TypeId declared = GetTypeIdFromName(base.c_str());
      if (declared != TypeId::Invalid) ai.default_value.retag_role(declared);
    }
    attr_map[prim_path].push_back(std::move(ai));
  }
  if (!report_stage("stage.properties", specs_.size(), specs_.size())) {
    return false;
  }

  // Sort by full path (produces correct depth-first order with parents before children)
  std::sort(prim_entries.begin(), prim_entries.end(),
    [](const PrimEntry& a, const PrimEntry& b) {
      return a.full_path < b.full_path;
    });

  // Build hierarchy using depth-based stack management
  // Stack keeps ancestor paths at each depth level
  std::vector<std::string> prim_stack;
  std::vector<std::pair<std::string, Value>> prim_value_field_scratch;

  if (!report_stage("stage.hierarchy", 0, prim_entries.size())) return false;
  for (size_t entry_index = 0; entry_index < prim_entries.size();
       ++entry_index) {
    if (entry_index > 0 &&
        !report_stage("stage.hierarchy", entry_index, prim_entries.size())) {
      return false;
    }
    auto& entry = prim_entries[entry_index];
    // Compute depth of this prim (number of '/' in path)
    size_t depth = std::count(entry.full_path.begin(), entry.full_path.end(), '/');

    // Pop stack until we're at the correct parent level
    while (prim_stack.size() > depth - 1) {
      builder.end_prim();
      prim_stack.pop_back();
    }

    // Begin this prim
    builder.begin_prim(entry.name, entry.type_name, entry.specifier);
    prim_stack.push_back(entry.name);

    // builder.current() is valid immediately after begin_prim; capture it once
    // and guard the metadata derefs (every other current() site is guarded too).
    PrimSpec* ps = builder.current();

    // Extracts a token-list metadata field (written as a token array, with
    // single-token / string fallbacks for older encodings). Warns rather than
    // silently dropping a known arc field that fails to decode.
    auto append_token_list = [&](const Value& v, std::vector<std::string>& dst,
                                 const char* field_name) {
      if (const std::vector<std::string>* arr = v.as_token_array()) {
        // Marker-aware: keep prepend/append sublist items, skip
        // deleted/ordered sublists, strip the markers themselves.
        bool keep = true;
        for (const auto& s : *arr) {
          if (!s.empty() && s[0] == '\x01') {
            keep = !(s == "\x01" "D" || s == "\x01" "O");
            continue;
          }
          if (keep) dst.push_back(s);
        }
      } else if (const std::string* s = v.as_token()) {
        dst.push_back(*s);
      } else if (const std::string* s = v.as_string()) {
        dst.push_back(*s);
      } else {
        AddWarning(std::string("Composition arc field '") + field_name +
                   "' has unexpected encoding; dropped");
      }
    };

    // Phase 7 S5: decode a `<arc>_listOp` companion token[] (marker-delimited
    // prepend/append/delete/order sublists) into an ArcEdit.
    auto decode_arc_listop = [&](const Value& v, ArcEdit& e) {
      const std::vector<std::string>* arr = v.as_token_array();
      if (!arr) return;
      // "\x01E" alone = authored explicit-clear (`references = None` /
      // pxr's explicit-empty listop): authored with is_explicit kept true
      // and no items, which the USDA writer re-emits as `= None`.
      if (arr->size() == 1 && ((*arr)[0] == "\x01" "E" || (*arr)[0] == "\x01E")) {
        e = ArcEdit();
        e.authored = true;
        return;
      }
      e.authored = true;
      e.is_explicit = false;
      std::vector<std::string>* cur = nullptr;
      for (const std::string& s : *arr) {
        // Both marker spellings are accepted: the correct two-byte form
        // ("\x01" "A") and the legacy single-byte form (in C++, "\x01A"
        // parses as the one char 0x1A because 'A' is a hex digit) written by
        // older next crates.
        if (s == "\x01" "P" || s == "\x01P") cur = &e.prepended;
        else if (s == "\x01" "A" || s == "\x1A") cur = &e.appended;
        else if (s == "\x01" "D" || s == "\x1D") cur = &e.deleted;
        else if (s == "\x01" "O" || s == "\x01O") cur = &e.ordered;
        else if (s == "\x01" "N" || s == "\x01N") cur = nullptr;
        else if (cur) cur->push_back(s);
      }
    };

    ResolveFieldset(entry.fieldset_index, prim_value_field_scratch);

    // Add properties and extract composition arcs / metadata into PrimSpecMeta
    for (auto& field : prim_value_field_scratch) {
      if (field.first == "typeName" || field.first == "specifier") continue;

      // Reserved prim metadata stored inline in the prim spec (pxrUSD keeps
      // these in the prim's fieldset, not as separate property specs). Route
      // them to PrimSpecMeta so they do not leak in as phantom properties.
      if (ps) {
        if (field.first == "active") {
          if (const bool* b = field.second.as_bool()) {
            ps->meta().active = *b;
            ps->meta().active_authored = true;
          }
          continue;
        }
        if (field.first == "hidden") {
          if (const bool* b = field.second.as_bool()) {
            ps->meta().hidden = *b;
            ps->meta().hidden_authored = true;
          }
          continue;
        }
        if (field.first == "doc") {
          if (const std::string* s = field.second.as_string())
            ps->meta().doc() = *s;
          else if (const std::string* s = field.second.as_token())
            ps->meta().doc() = *s;
          continue;
        }
      }

      // Composition arc + metadata fields: store in PrimSpecMeta, not as
      // regular properties. (Guarded by ps; if current() were null we simply
      // skip them rather than crash.)
      if (ps) {
        if (field.first == "apiSchemas") {
          // Applied API schemas (TokenListOp). Non-explicit sublists arrive
          // marker-delimited; recover the qualifier and strip the markers.
          // Deleted items apply as in-place removals (same as the USDA
          // parser; the writer never emits a delete qualifier); ordered
          // items are ordering-only and are skipped.
          if (const std::vector<std::string>* arr =
                  field.second.as_token_array()) {
            std::string qual;
            std::vector<std::string> deleted;
            enum class Mode { Keep, Delete, Skip } mode = Mode::Keep;
            for (const std::string& t : *arr) {
              if (t == "\x01" "P") { qual = "prepend"; mode = Mode::Keep; continue; }
              if (t == "\x01" "A") { qual = "append"; mode = Mode::Keep; continue; }
              if (t == "\x01" "D") { mode = Mode::Delete; continue; }
              if (t == "\x01" "O") { mode = Mode::Skip; continue; }
              if (!t.empty() && t[0] == '\x01') continue;  // E / unknown
              if (mode == Mode::Keep) ps->meta().apiSchemas().push_back(t);
              else if (mode == Mode::Delete) deleted.push_back(t);
            }
            if (!deleted.empty()) {
              std::vector<std::string>& applied = ps->meta().apiSchemas();
              const std::unordered_set<std::string> del(deleted.begin(),
                                                        deleted.end());
              applied.erase(std::remove_if(applied.begin(), applied.end(),
                                           [&](const std::string& a) {
                                             return del.count(a) != 0;
                                           }),
                            applied.end());
            }
            if (!qual.empty()) ps->meta().apiSchemasQualifier() = qual;
          } else {
            append_token_list(field.second, ps->meta().apiSchemas(),
                              "apiSchemas");
          }
          continue;
        }
        // Composition arc lists. A marker-delimited token array (first entry
        // "\x01?") is a non-explicit list-op decoded from the native crate
        // encoding: reconstruct the authored edits and derive the effective
        // (within-spec) list; otherwise it is a plain explicit list.
        auto is_marker_list = [](const Value& v) {
          const std::vector<std::string>* arr = v.as_token_array();
          return arr && !arr->empty() && !(*arr)[0].empty() &&
                 (*arr)[0][0] == '\x01';
        };
        auto apply_arc_field = [&](const Value& v,
                                   std::vector<std::string>& target,
                                   ArcEdit& (*sel)(ArcListOpEdits&),
                                   const char* what) {
          if (is_marker_list(v)) {
            ArcEdit& e = sel(ps->meta().ensure_arc_edits());
            decode_arc_listop(v, e);
            // Effective list within a single spec: prepended then appended
            // (delete/reorder operate on weaker opinions, not this list).
            target = e.prepended;
            target.insert(target.end(), e.appended.begin(), e.appended.end());
          } else {
            append_token_list(v, target, what);
          }
        };
        if (field.first == "references") {
          apply_arc_field(field.second, ps->meta().references,
                          [](ArcListOpEdits& a) -> ArcEdit& { return a.references; },
                          "references");
          continue;
        }
        if (field.first == "payload") {
          apply_arc_field(field.second, ps->meta().payloads,
                          [](ArcListOpEdits& a) -> ArcEdit& { return a.payloads; },
                          "payload");
          continue;
        }
        if (field.first == "inherits" || field.first == "inheritPaths") {
          // pxr's crate field name is "inheritPaths"; next's own writer used
          // "inherits". Accept both.
          apply_arc_field(field.second, ps->meta().inherits,
                          [](ArcListOpEdits& a) -> ArcEdit& { return a.inherits; },
                          "inherits");
          continue;
        }
        if (field.first == "specializes") {
          apply_arc_field(field.second, ps->meta().specializes,
                          [](ArcListOpEdits& a) -> ArcEdit& { return a.specializes; },
                          "specializes");
          continue;
        }
        if (field.first == "references_listOp") {
          decode_arc_listop(field.second, ps->meta().ensure_arc_edits().references);
          continue;
        }
        if (field.first == "payload_listOp") {
          decode_arc_listop(field.second, ps->meta().ensure_arc_edits().payloads);
          continue;
        }
        if (field.first == "inherits_listOp") {
          decode_arc_listop(field.second, ps->meta().ensure_arc_edits().inherits);
          continue;
        }
        if (field.first == "specializes_listOp") {
          decode_arc_listop(field.second,
                            ps->meta().ensure_arc_edits().specializes);
          continue;
        }
        if (field.first == "variantSelection") {
          if (const std::string* s = field.second.as_token())
            ps->meta().variantSelection = *s;
          else if (const std::string* s = field.second.as_string())
            ps->meta().variantSelection = *s;
          continue;
        }
        if (field.first == "comment") {
          if (const std::string* s = field.second.as_token())
            ps->meta().comment() = *s;
          else if (const std::string* s = field.second.as_string())
            ps->meta().comment() = *s;
          continue;
        }
        if (field.first == "kind") {
          if (const std::string* s = field.second.as_token())
            ps->meta().kind() = *s;
          else if (const std::string* s = field.second.as_string())
            ps->meta().kind() = *s;
          continue;
        }
        if (field.first == "displayName") {
          if (const std::string* s = field.second.as_string())
            ps->meta().displayName() = *s;
          else if (const std::string* s = field.second.as_token())
            ps->meta().displayName() = *s;
          continue;
        }
        if (field.first == "instanceable") {
          if (const bool* b = field.second.as_bool())
            ps->meta().instanceable = *b;
          continue;
        }
        if (field.first == "customData" || field.first == "assetInfo" ||
            field.first == "sdrMetadata" || field.first == "clips") {
          // pxr-authored: a binary VtDictionary (already decoded to a Dictionary
          // Value). next-authored: USDA dict text in a String field.
          Value d;
          if (field.second.is_dictionary()) {
            d = std::move(field.second);
          } else if (const std::string* s = field.second.as_string()) {
            d = ParseDictText(*s);
          } else if (const std::string* s = field.second.as_token()) {
            d = ParseDictText(*s);
          }
          if (d.is_dictionary()) {
            if (field.first == "customData")
              ps->meta().customData() = std::move(d);
            else if (field.first == "assetInfo")
              ps->meta().assetInfo() = std::move(d);
            else if (field.first == "sdrMetadata")
              ps->meta().sdrMetadata() = std::move(d);
            else
              ps->meta().clips() = std::move(d);
          }
          continue;
        }
        if (field.first == "variantSets") {
          // Writer stores the variant-set names only; reconstruct name entries.
          std::vector<std::string> names;
          append_token_list(field.second, names, "variantSets");
          for (auto& n : names) {
            VariantSetData vsd;
            vsd.name = std::move(n);
            ps->meta().variantSets().push_back(std::move(vsd));
          }
          continue;
        }
      }
      // A loose sibling "variability" field cannot be re-associated with a
      // property here; consume it so it does not surface as a stray property.
      // Per-property variability is decoded above through Attribute/Relationship
      // specs and preserved as kFlagUniform.
      if (field.first == "variability") continue;

      // Reserved Sdf children-key ordering fields. pxrUSD stores prim/property
      // order in these (SdfChildrenKeys); USDA flatten output does NOT emit them
      // as body attributes -- order is implicit in the authored child/property
      // sequence. Composition derives child order from child_indices() directly
      // (see cache.cc ComposeInto), so consume these so they do not leak as
      // phantom `token[] primChildren`/`properties` attributes.
      if (field.first == "primChildren") {
        // Authored sibling order: the hierarchy below is built in path-sorted
        // order, so capture the order here and restore it after finalize.
        if (const std::vector<std::string>* names =
                field.second.as_token_array()) {
          if (!names->empty()) {
            prim_children_order[entry.full_path] = *names;
          }
        }
        continue;
      }
      if (field.first == "properties" ||
          field.first == "propertyChildren" ||
          field.first == "variantChildren" ||
          field.first == "variantSetChildren") {
        continue;
      }

      // `variantSetNames` (the declared list of variant-set names) likewise must
      // not leak as a phantom `string[] variantSetNames` body property. The
      // compositor drives variants from the bracketed-holder specs + the
      // variantSelection (not from this list), and flatten output drops variant
      // metadata entirely (see cache.cc ComposeInto), so consume it.
      if (field.first == "variantSetNames") {
        continue;
      }

      uint16_t flags = 0;
      builder.add_property(field.first, std::move(field.second), flags);
    }

    // Attach separate Attribute / Relationship specs that belong to this prim.
    // The dedup guard keeps any inline field authoritative.
    auto am = attr_map.find(entry.full_path);
    if (ps && am != attr_map.end()) {
      for (auto& ai : am->second) {
        if (ps->property(ai.name)) continue;  // inline opinion wins
        uint16_t flags = 0;
        if (ai.uniform) flags |= PropSlot::kFlagUniform;
        if (ai.custom) flags |= PropSlot::kFlagCustom;
        if (ai.is_connection) flags |= PropSlot::kFlagConnection;
        // The writer gates timeSamples emission on the slot flag, so mark a
        // time-sampled attribute (the value lives in add_time_sample below).
        if (!ai.time_samples.empty()) flags |= PropSlot::kFlagTimeSampled;
        const bool is_array =
            ai.type_name.size() >= 2 &&
            ai.type_name.compare(ai.type_name.size() - 2, 2, "[]") == 0;
        if (ai.has_default) {
          ps->add_property(ai.name, std::move(ai.default_value), flags);
        } else {
          // Connection-only / declared-only / timeSamples-only attribute:
          // register a typed slot with no authored default so it round-trips.
          if (is_array) flags |= PropSlot::kFlagArray;
          std::string base = is_array
              ? ai.type_name.substr(0, ai.type_name.size() - 2)
              : ai.type_name;
          TypeId tid = GetTypeIdFromName(base.c_str());
          ps->add_property_slot(GetPropNameTable().intern(ai.name), tid, flags);
        }
        // Time samples (an attribute may have timeSamples with or without a
        // default).
        if (!ai.time_samples.empty()) {
          PropNameId nid = GetPropNameTable().intern(ai.name);
          for (auto& ts : ai.time_samples) {
            ps->add_time_sample(nid, ts.first, std::move(ts.second));
          }
        }
        if (!ai.type_name.empty()) {
          ps->set_property_type_name(ai.name, ai.type_name);
        }
        if (ai.is_connection && ai.connection_targets.empty()) {
          ps->set_connection_block(ai.name);  // authored `.connect = None`
        }
        for (const auto& t : ai.connection_targets) {
          ps->add_connection(ai.name, Path(t));
        }
        // Per-property metadata.
        if (ai.has_interpolation || ai.has_color_space || ai.has_element_size ||
            ai.custom_data.is_dictionary()) {
          PropMeta& pm = ps->ensure_property_meta(ai.name);
          if (ai.has_interpolation) {
            pm.interpolation = ai.interpolation;
            pm.authored |= PropMeta::kInterpolation;
          }
          if (ai.has_color_space) {
            pm.colorSpace = ai.color_space;
            pm.authored |= PropMeta::kColorSpace;
          }
          if (ai.has_element_size) {
            pm.elementSize = ai.element_size;
            pm.authored |= PropMeta::kElementSize;
          }
          if (ai.custom_data.is_dictionary()) {
            pm.customData = std::move(ai.custom_data);
            pm.authored |= PropMeta::kCustomData;
          }
        }
      }
    }
    auto rm = rel_map.find(entry.full_path);
    if (ps && rm != rel_map.end()) {
      for (auto& ri : rm->second) {
        // A target-less relationship stays a declared (empty) relationship;
        // relationships with targets push one Path per target.
        if (ri.targets.empty()) {
          if (!ps->relationship(ri.name)) {
            ps->set_relationship_targets(ri.name, {});
          }
        } else {
          for (const auto& t : ri.targets) ps->add_relationship(ri.name, Path(t));
        }
        if (ri.edits.authored) {
          ps->ensure_relationship_edit(ri.name) = std::move(ri.edits);
        }
        if (ri.custom) {
          ps->set_relationship_flags(
              ri.name, static_cast<uint16_t>(ps->relationship_flags(ri.name) |
                                             PropSlot::kFlagCustom));
        }
      }
    }

    // Variant sets: attach a VariantSetData per set, carrying the selection
    // and the option NAMES (as empty VariantData entries — the compositor
    // ignores option entries without content/properties and grafts the
    // bracketed holder prims instead, which hold the variant CONTENT).
    auto sel = variant_sel.find(entry.full_path);
    auto opt = variant_opts.find(entry.full_path);
    if (ps && (sel != variant_sel.end() || opt != variant_opts.end())) {
      // Union of set names from selections and holder specs.
      std::map<std::string, VariantSetData> sets;
      if (opt != variant_opts.end()) {
        for (const auto& kv : opt->second) {
          VariantSetData& vsd = sets[kv.first];
          vsd.name = kv.first;
          for (const std::string& var : kv.second) {
            VariantData vd;
            vd.name = var;
            vsd.variants.push_back(std::move(vd));
          }
        }
      }
      if (sel != variant_sel.end()) {
        for (const auto& kv : sel->second) {
          VariantSetData& vsd = sets[kv.first];
          vsd.name = kv.first;
          vsd.selected = kv.second;
        }
      }
      for (auto& kv : sets) {
        // Merge into a same-named entry when one already exists (older
        // next-authored crates carry a legacy `variantSets` token field that
        // pre-pushed name-only entries; pushing a second full entry would
        // duplicate the set — grafted twice and emitted twice as USDA).
        VariantSetData* existing = nullptr;
        for (VariantSetData& evs : ps->meta().variantSets()) {
          if (evs.name == kv.first) {
            existing = &evs;
            break;
          }
        }
        if (existing) {
          if (existing->selected.empty()) {
            existing->selected = kv.second.selected;
          }
          for (VariantData& vd : kv.second.variants) {
            bool have = false;
            for (const VariantData& evd : existing->variants) {
              if (evd.name == vd.name) {
                have = true;
                break;
              }
            }
            if (!have) existing->variants.push_back(std::move(vd));
          }
        } else {
          ps->meta().variantSets().push_back(std::move(kv.second));
        }
      }
      // Record ALL selections in the plural list (the single legacy string
      // can carry only one set; consumers that read it alone would lose
      // every selection after the first on a multi-set prim).
      if (sel != variant_sel.end() && !sel->second.empty()) {
        for (const auto& kv : sel->second) {
          ps->meta().variantSelections().emplace_back(kv.first, kv.second);
        }
        const auto& first = *sel->second.begin();
        ps->meta().variantSelection = first.first + "=" + first.second;
      }
    }
  }
  if (!report_stage("stage.hierarchy", prim_entries.size(),
                    prim_entries.size())) {
    return false;
  }

  // Close remaining prims
  while (!prim_stack.empty()) {
    builder.end_prim();
    prim_stack.pop_back();
  }

  // Finalize
  builder.finalize();

  // Restore authored namespace order from primChildren: reorder each prim's
  // child links (and the root list) to the recorded name order; unlisted
  // children keep their relative (sorted) order at the end.
  if (!prim_children_order.empty()) {
    layer.build_path_index();
    auto reorder = [&](const std::vector<uint32_t>& current,
                       const std::vector<std::string>& names) {
      std::vector<uint32_t> out;
      out.reserve(current.size());
      std::vector<bool> used(current.size(), false);
      // Map each child name to a FIFO of its positions in `current` (preserving
      // original order for duplicate names). Reordering is then O(N+M) instead
      // of O(N*M): a malformed file with N root prims and an N-name primChildren
      // list previously cost O(N^2) string compares (a ~O(N) file could hang).
      std::unordered_map<std::string, std::deque<size_t>> by_name;
      for (size_t i = 0; i < current.size(); ++i) {
        const PrimSpec* c = layer.prim(current[i]);
        if (c) by_name[c->name()].push_back(i);
      }
      for (const std::string& nm : names) {
        auto it = by_name.find(nm);
        if (it == by_name.end() || it->second.empty()) continue;
        size_t idx = it->second.front();
        it->second.pop_front();
        used[idx] = true;
        out.push_back(current[idx]);
      }
      for (size_t i = 0; i < current.size(); ++i) {
        if (!used[i]) out.push_back(current[i]);
      }
      return out;
    };
    for (auto& kv : prim_children_order) {
      if (kv.first == "/") {
        std::vector<uint32_t> ro = reorder(layer.root_indices(), kv.second);
        layer.set_root_indices(std::move(ro));
        continue;
      }
      if (PrimSpec* p = layer.prim_at_path_mutable(kv.first)) {
        std::vector<uint32_t> co = reorder(p->child_indices(), kv.second);
        p->set_child_indices(std::move(co));
      }
    }
  }

  // Create stage from layer
  result_.stage.SetRootLayer(std::move(layer));

  return true;
}


}  // namespace next
}  // namespace tinyusdz
