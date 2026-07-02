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
#include <chrono>

// Parallel stage build (emit phase): prim bodies are decoded into per-worker
// Layer fragments on the shared parser worker pool, then stitched in authored
// order with a depth-stack link rebuild identical to the serial emit. The
// serial implementation below is fully retained and used when this gate is
// off, when threads are unavailable, or for small layers. Define
// TINYUSDZ_NEXT_DISABLE_PARALLEL_BUILD_STAGE to force the serial path at
// compile time.
#if defined(TINYUSDZ_ENABLE_THREAD) && \
    !defined(TINYUSDZ_NEXT_DISABLE_PARALLEL_BUILD_STAGE)
#define TINYUSDZ_NEXT_PARALLEL_BUILD_STAGE 1
#include <condition_variable>
#include <mutex>
#include <thread>
#endif
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tinyusdz {
namespace next {

// Decode a dictionary that was written as USDA dict text in a String field
// (see crate-writer add_dict_field). Returns an empty Value on failure.
static Value ParseDictText(std::string_view text) {
  if (text.empty()) return Value();
  Lexer lexer(text.data(), text.size());
  ParseResult r = ParseDict(lexer);
  return r.success ? std::move(r.value) : Value();
}


bool CrateReader::Impl::BuildStage() {
  using Clock = std::chrono::steady_clock;
  const bool timing = options_.enable_timing;
  auto ms = [](Clock::duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
  };
  const auto t_build_start = Clock::now();

  // Create layer and builder
  Layer layer;
  LayerBuilder builder(layer);

  auto read_field_token_or_string = [this](const ValueRep& rep,
                                          std::string_view* out) -> bool {
    switch (rep.type_id()) {
      case CrateTypeId::Token:
        return GetToken(static_cast<uint32_t>(rep.payload()), out);
      case CrateTypeId::String:
        return GetString(static_cast<uint32_t>(rep.payload()), out);
      default:
        return false;
    }
  };

  auto read_field_double = [&](const ValueRep& rep, double* out) -> bool {
    if (!out || rep.type_id() != CrateTypeId::Double) return false;
    if (rep.is_inlined()) {
      return false;
    }
    Value v;
    if (!UnpackValue(rep, v)) return false;
    if (const double* d = v.as_double()) {
      *out = *d;
      return true;
    }
    return false;
  };

  auto read_field_bool = [](const ValueRep& rep, bool* out) -> bool {
    if (!out || rep.type_id() != CrateTypeId::Bool) return false;
    if (!rep.is_inlined()) return false;
    *out = (rep.payload() != 0);
    return true;
  };

  auto read_field_specifier = [](const ValueRep& rep, PrimSpecifier* out) -> bool {
    if (!out || rep.type_id() != CrateTypeId::Specifier) return false;
    const uint32_t spec = static_cast<uint32_t>(rep.payload());
    if (spec == 1) {
      *out = PrimSpecifier::Over;
      return true;
    }
    if (spec == 2) {
      *out = PrimSpecifier::Class;
      return true;
    }
    *out = PrimSpecifier::Def;
    return true;
  };

  auto decode_dict_value = [&](const ValueRep& rep, Value& out) -> bool {
    if (rep.type_id() == CrateTypeId::Dictionary) {
      return DecodeDictionary(rep, out, 0);
    }
    std::string_view text;
    if (!read_field_token_or_string(rep, &text)) {
      return false;
    }
    out = ParseDictText(text);
    return true;
  };

  auto visit_fieldset_raw = [&](uint32_t fieldset_index,
                                const auto& visitor) -> bool {
    if (fieldset_indices_.empty() || fieldset_index >= fieldset_indices_.size()) {
      return false;
    }
    if (fieldset_index >= fieldset_index_to_id_.size()) {
      return false;
    }
    const uint32_t fi32 = fieldset_index_to_id_[fieldset_index];
    if (fi32 == 0xFFFFFFFFu) return false;
    const size_t fi = static_cast<size_t>(fi32);
    if (fi >= fieldset_counts_.size()) {
      return false;
    }
    const size_t start = static_cast<size_t>(fieldset_offsets_[fi]);
    const size_t count = static_cast<size_t>(fieldset_counts_[fi]);
    if (start >= fieldset_indices_.size() ||
        count > fieldset_indices_.size() - start) {
      return false;
    }
    for (size_t idx = start; idx < start + count; ++idx) {
      const uint32_t field_idx = fieldset_indices_[idx];
      if (field_idx >= fields_.size()) continue;
      const CrateField& field = fields_[field_idx];
      std::string_view name;
      if (!GetToken(field.token_index.value, &name)) continue;
      if (!visitor(name, field.value_rep)) {
        return true;
      }
    }
    return true;
  };

  auto is_prim_entry_sorted_by_path = [](const auto& entries) {
    if (entries.size() < 2) return true;
    for (size_t i = 1; i < entries.size(); ++i) {
      if (entries[i - 1].full_path > entries[i].full_path) return false;
    }
    return true;
  };

  auto is_entry_sorted_by_prim_path = [](const auto& entries) {
    if (entries.size() < 2) return true;
    for (size_t i = 1; i < entries.size(); ++i) {
      if (entries[i - 1].first > entries[i].first) return false;
    }
    return true;
  };

  auto is_variant_sel_sorted_by_prim_path = [](const auto& entries) {
    if (entries.size() < 2) return true;
    for (size_t i = 1; i < entries.size(); ++i) {
      if (entries[i - 1].prim_path > entries[i].prim_path) return false;
    }
    return true;
  };

  bool saw_pseudo_root = false;

  // Process prim specs to build prims with hierarchy from paths
  struct PrimEntry {
    std::string_view full_path;
    std::string_view name;
    std::string_view type_name;
    PrimSpecifier specifier;
    uint32_t fieldset_index = 0;
  };

  struct VariantSelectionEntry {
    std::string_view prim_path;
    std::string_view set_name;
    std::string_view selection;
  };
  // Variant content lives in the layer as bracketed holder/child prims
  // ("/Prim/{vset=sel}" + descendants); their attributes attach to those prims
  // via attr_map (so the layer fully represents the variants, enabling both the
  // compositor — which reads the selected holder — and unflattened round-trip).
  // We only need to know, per owning prim, which sets are selected.
  //   owning prim path -> { variantSet -> selected variant } (all selections).
  size_t estimated_prims = 0;
  size_t estimated_attrs = 0;
  size_t estimated_rels = 0;
  size_t estimated_variants = 0;
  for (const auto& spec : specs_) {
    switch (spec.spec_type) {
      case SpecType::Prim:
        ++estimated_prims;
        break;
      case SpecType::Attribute:
        ++estimated_attrs;
        break;
      case SpecType::Relationship:
        ++estimated_rels;
        break;
      case SpecType::Variant:
      case SpecType::VariantSet:
        ++estimated_variants;
        break;
      default:
        break;
    }
  }

  std::vector<VariantSelectionEntry> variant_sels;
  variant_sels.reserve(estimated_variants > 64 ? estimated_variants : 64);
  std::vector<std::pair<std::string_view, std::string_view>> variant_buf;
  variant_buf.reserve(8);

  std::vector<PrimEntry> prim_entries;
  prim_entries.reserve(estimated_prims + estimated_variants + 64);

  auto decode_prim_header = [&](PrimEntry& entry, const CrateSpec& spec,
                               const bool decode_variant) {
    bool got_type_name = false;
    bool got_specifier = false;
    bool got_variant_selection = !decode_variant;
    visit_fieldset_raw(spec.fieldset_index.value,
                       [&](std::string_view field_name,
                           const ValueRep& field_value) {
      if (field_name == "typeName") {
        std::string_view text;
        if (read_field_token_or_string(field_value, &text)) {
          entry.type_name = text;
          got_type_name = true;
        }
      } else if (field_name == "specifier") {
        read_field_specifier(field_value, &entry.specifier);
        got_specifier = true;
      } else if (decode_variant && field_name == "variantSelection") {
        variant_buf.clear();
        if (DecodeVariantSelectionMap(field_value, &variant_buf) &&
            !variant_buf.empty()) {
          for (const auto& kv : variant_buf) {
            variant_sels.push_back({entry.full_path, kv.first, kv.second});
          }
        }
        got_variant_selection = true;
      }
      return !(got_type_name && got_specifier && got_variant_selection);
    });
  };

  // Collect separate Attribute and Relationship specs (property specs) and variant
  // holders in one pass. pxrUSD-authored crates store these as separate specs at
  // "<primpath>/<name>" (rendered with a leading '.' marker by ReadPaths),
  // while Variant/VariantSet are represented by path-bracketed prim-like specs.
  struct AttrInfoCold {
    std::vector<Path> connection_targets;
    std::vector<std::pair<double, Value>> time_samples;
    Value custom_data;  // dictionary
  };
  struct AttrInfo {
    std::string_view name;
    std::string_view type_name;
    bool has_default = false;
    Value default_value;
    bool is_connection = false;
    bool uniform = false;
    bool custom = false;
    // Per-property metadata (round-tripped via attribute spec fields).
    std::string_view interpolation;
    std::string_view color_space;
    int32_t element_size = 1;
    bool has_interpolation = false;
    bool has_color_space = false;
    bool has_element_size = false;
    std::unique_ptr<AttrInfoCold> cold;
  };
  struct RelInfo {
    std::string_view name;
    std::vector<Path> targets;
    bool uniform = false;
  };
  std::vector<std::pair<std::string_view, AttrInfo>> attr_entries;
  std::vector<std::pair<std::string_view, RelInfo>> rel_entries;
  attr_entries.reserve(estimated_attrs + 64);
  rel_entries.reserve(estimated_rels + 64);
  auto ensure_attr_cold = [](AttrInfo& ai) -> AttrInfoCold& {
    if (!ai.cold) ai.cold.reset(new AttrInfoCold());
    return *ai.cold;
  };

  // Split a property spec path ".<primpath>/<name>" into (primpath, name).
  auto split_prop_path = [](const std::string_view raw,
                           std::string_view& prim_path,
                           std::string_view& prop_name) -> bool {
    if (raw.empty()) return false;
    size_t begin = (raw[0] == '.') ? 1 : 0;
    size_t slash = raw.rfind('/');
    if (slash == std::string::npos || slash < begin) return false;
    prim_path = raw.substr(begin, slash - begin);
    prop_name = raw.substr(slash + 1);
    return !prim_path.empty() && !prop_name.empty();
  };

  const auto t_collect_start = Clock::now();
  for (const auto& spec : specs_) {
    if (spec.path_index.value >= paths_.size()) continue;
    const std::string& full_path_ref = paths_[spec.path_index.value];
    const std::string_view full_path = full_path_ref;
    const bool bracketed = full_path.find('{') != std::string_view::npos;

    if (!saw_pseudo_root && spec.spec_type == SpecType::PseudoRoot) {
      if (!visit_fieldset_raw(
              spec.fieldset_index.value,
              [&](std::string_view field_name, const ValueRep& field_value) {
                Value v;
                if (field_name == "defaultPrim") {
                  std::string_view token;
                  if (read_field_token_or_string(field_value, &token))
                    layer.meta().defaultPrim = token;
                } else if (field_name == "upAxis") {
                  std::string_view token;
                  if (read_field_token_or_string(field_value, &token))
                    layer.meta().upAxis = token;
                } else if (field_name == "metersPerUnit") {
                  double v_double;
                  if (read_field_double(field_value, &v_double))
                    layer.meta().metersPerUnit = v_double;
                } else if (field_name == "timeCodesPerSecond") {
                  double v_double;
                  if (read_field_double(field_value, &v_double))
                    layer.meta().timeCodesPerSecond = v_double;
                } else if (field_name == "startTimeCode") {
                  double v_double;
                  if (read_field_double(field_value, &v_double))
                    layer.meta().startTimeCode = v_double;
                } else if (field_name == "endTimeCode") {
                  double v_double;
                  if (read_field_double(field_value, &v_double))
                    layer.meta().endTimeCode = v_double;
                } else if (field_name == "framesPerSecond") {
                  double v_double;
                  if (read_field_double(field_value, &v_double)) {
                    layer.meta().framesPerSecond = v_double;
                    layer.meta().framesPerSecond_set = true;
                  }
                } else if (field_name == "kilogramsPerUnit") {
                  double v_double;
                  if (read_field_double(field_value, &v_double)) {
                    layer.meta().kilogramsPerUnit = v_double;
                    layer.meta().kilogramsPerUnit_set = true;
                  }
                } else if (field_name == "customLayerData" ||
                           field_name == "expressionVariables") {
                  // pxr-authored: binary VtDictionary; next-authored: USDA dict text.
                  Value d;
                  if (!decode_dict_value(field_value, d)) return true;
                  if (d.is_dictionary()) {
                    if (field_name == "customLayerData")
                      layer.meta().customLayerData = std::move(d);
                    else
                      layer.meta().expressionVariables = std::move(d);
                  }
                } else if (field_name == "doc") {
                  std::string_view text;
                  if (read_field_token_or_string(field_value, &text))
                    layer.meta().doc = text;
                } else if (field_name == "comment") {
                  std::string_view text;
                  if (read_field_token_or_string(field_value, &text))
                    layer.meta().comment = text;
                } else if (field_name == "subLayers") {
                  if (UnpackValue(field_value, v)) {
                    const std::vector<std::string>* arr = v.as_token_array();
                    if (arr) {
                      for (const auto& s : *arr) layer.meta().subLayers.push_back(s);
                    } else if (const std::string* s = v.as_string()) {
                      layer.meta().subLayers.push_back(*s);
                    } else if (const std::string* s = v.as_token()) {
                      layer.meta().subLayers.push_back(*s);
                    }
                  }
                }
                return true;
              })) {
        AddWarning("Failed to resolve pseudo-root fieldset");
      }
      saw_pseudo_root = true;
      continue;
    }

    const bool is_prim = spec.spec_type == SpecType::Prim;
    const bool is_variant =
        spec.spec_type == SpecType::Variant || spec.spec_type == SpecType::VariantSet;
    const bool is_attr = spec.spec_type == SpecType::Attribute;
    const bool is_rel = spec.spec_type == SpecType::Relationship;

    if (!is_prim && !is_variant && !is_attr && !is_rel) continue;

    if (full_path.empty() || full_path == "/") continue;

    if (is_attr || is_rel) {
      std::string_view prim_path;
      std::string_view prop_name;
      if (!split_prop_path(full_path, prim_path, prop_name)) {
        continue;
      }

      if (is_rel) {
        RelInfo ri;
        ri.name = prop_name;
        visit_fieldset_raw(
            spec.fieldset_index.value,
            [&](std::string_view field_name, const ValueRep& field_value) {
              if (field_name == "targetPaths") {
                DecodePathTargets(field_value, ri.targets);
              } else if (field_name == "variability") {
                std::string_view text;
                if (read_field_token_or_string(field_value, &text)) {
                  ri.uniform = (text == "uniform");
                }
              }
              return true;
            });
        rel_entries.push_back({prim_path, std::move(ri)});
      } else {
        AttrInfo ai;
        ai.name = prop_name;
        visit_fieldset_raw(
            spec.fieldset_index.value,
            [&](std::string_view field_name, const ValueRep& field_value) {
              if (field_name == "typeName") {
                if (read_field_token_or_string(field_value, &ai.type_name)) {
                }
              } else if (field_name == "default") {
                Value v;
                if (UnpackValue(field_value, v)) {
                  ai.default_value = std::move(v);
                  ai.has_default = true;
                }
              } else if (field_name == "timeSamples") {
                DecodeTimeSamples(field_value, &ensure_attr_cold(ai).time_samples);
              } else if (field_name == "connectionPaths") {
                AttrInfoCold& cold = ensure_attr_cold(ai);
                if (DecodePathTargets(field_value, cold.connection_targets) &&
                    !cold.connection_targets.empty()) {
                  ai.is_connection = true;
                }
              } else if (field_name == "variability") {
                std::string_view text;
                if (read_field_token_or_string(field_value, &text)) {
                  ai.uniform = (text == "uniform");
                }
              } else if (field_name == "custom") {
                // The legacy `custom` qualifier (pxr stores a bool field). Preserved on
                // read; the USDA writer only re-emits it under emit_custom/--openusd-compat.
                Value v;
                if (UnpackValue(field_value, v)) {
                  if (const bool* b = v.as_bool()) {
                    ai.custom = *b;
                  }
                }
              } else if (field_name == "interpolation") {
                if (read_field_token_or_string(field_value, &ai.interpolation)) {
                  ai.has_interpolation = true;
                }
              } else if (field_name == "colorSpace") {
                if (read_field_token_or_string(field_value, &ai.color_space)) {
                  ai.has_color_space = true;
                }
              } else if (field_name == "elementSize") {
                Value v;
                if (UnpackValue(field_value, v)) {
                  if (const int32_t* i = v.as_int()) {
                    ai.element_size = *i;
                    ai.has_element_size = true;
                  }
                }
              } else if (field_name == "customData") {
                Value v;
                if (UnpackValue(field_value, v)) {
                  AttrInfoCold& cold = ensure_attr_cold(ai);
                  if (v.is_dictionary()) {
                    cold.custom_data = std::move(v);
                  } else if (const std::string* s = v.as_string()) {
                    cold.custom_data = ParseDictText(*s);
                  } else if (const std::string* s = v.as_token()) {
                    cold.custom_data = ParseDictText(*s);
                  }
                }
              }
              return true;
            });
        attr_entries.push_back({prim_path, std::move(ai)});
      }
      continue;
    }

    // Build Prim specs and variant namespaces.
    if (is_variant && !bracketed) continue;

    const size_t last_slash = full_path.rfind('/');
    const std::string_view prim_name =
        (last_slash != std::string::npos && last_slash < full_path.size() - 1)
            ? full_path.substr(last_slash + 1)
            : full_path;
    if (prim_name.empty()) continue;

    PrimEntry entry;
    entry.full_path = full_path;
    entry.name = prim_name;
    entry.fieldset_index = spec.fieldset_index.value;

    // Untyped prims stay untyped: composition fills the type from the
    // referenced prim (a forced "Xform" default would mask e.g. a referenced
    // Mesh definition; the writer already skips empty type names).
    entry.specifier = PrimSpecifier::Def;
    decode_prim_header(entry, spec, !bracketed);
    prim_entries.push_back(std::move(entry));
  }
  const auto t_collect_end = Clock::now();

  // Sort by full path (produces correct depth-first order with parents before children)
  const auto t_sort_start = Clock::now();
  if (!is_prim_entry_sorted_by_path(prim_entries)) {
    std::sort(prim_entries.begin(), prim_entries.end(),
              [](const PrimEntry& a, const PrimEntry& b) {
                return a.full_path < b.full_path;
              });
  }
  const auto t_sort_end = Clock::now();

  if (prim_entries.empty()) {
    if (options_.finalize_stage) {
      builder.finalize();
    }
    result_.stage.SetRootLayer(std::move(layer));
    return true;
  }

  const size_t kNoIndex = static_cast<size_t>(-1);
  const auto t_index_start = Clock::now();
  const bool attrs_sorted_by_prim = is_entry_sorted_by_prim_path(attr_entries);
  const bool rels_sorted_by_prim = is_entry_sorted_by_prim_path(rel_entries);
  const bool variants_sorted_by_prim =
      is_variant_sel_sorted_by_prim_path(variant_sels);
  const bool needs_link_index =
      !attrs_sorted_by_prim || !rels_sorted_by_prim || !variants_sorted_by_prim;
  std::unordered_map<std::string_view, size_t> prim_path_to_index;
  if (needs_link_index) {
    prim_path_to_index.reserve(prim_entries.size());
    for (size_t i = 0; i < prim_entries.size(); ++i) {
      prim_path_to_index.emplace(prim_entries[i].full_path, i);
    }
  }

  std::vector<size_t> attr_head;
  std::vector<size_t> attr_tail;
  std::vector<size_t> rel_head;
  std::vector<size_t> rel_tail;
  std::vector<size_t> variant_head;
  std::vector<size_t> variant_tail;

  std::vector<size_t> attr_next;
  std::vector<size_t> rel_next;
  std::vector<size_t> variant_next;

  if (!attrs_sorted_by_prim) {
    attr_head.assign(prim_entries.size(), kNoIndex);
    attr_tail.assign(prim_entries.size(), kNoIndex);
    attr_next.assign(attr_entries.size(), kNoIndex);
  }
  if (!rels_sorted_by_prim) {
    rel_head.assign(prim_entries.size(), kNoIndex);
    rel_tail.assign(prim_entries.size(), kNoIndex);
    rel_next.assign(rel_entries.size(), kNoIndex);
  }
  if (!variants_sorted_by_prim) {
    variant_head.assign(prim_entries.size(), kNoIndex);
    variant_tail.assign(prim_entries.size(), kNoIndex);
    variant_next.assign(variant_sels.size(), kNoIndex);
  }

  auto link_by_prim = [](const std::string_view prim_path,
                         const size_t node_index, const size_t kNoIndex,
                         const std::unordered_map<std::string_view, size_t>& map,
                         const size_t buckets_size, std::vector<size_t>& head,
                         std::vector<size_t>& tail,
                         std::vector<size_t>& next) {
    const auto it = map.find(prim_path);
    if (it == map.end()) return;
    const size_t prim_index = it->second;
    if (prim_index >= buckets_size) return;
    if (head[prim_index] == kNoIndex) {
      head[prim_index] = node_index;
      tail[prim_index] = node_index;
      next[node_index] = kNoIndex;
    } else {
      const size_t prev_tail = tail[prim_index];
      next[prev_tail] = node_index;
      tail[prim_index] = node_index;
      next[node_index] = kNoIndex;
    }
  };

  if (!attrs_sorted_by_prim) {
    for (size_t i = 0; i < attr_entries.size(); ++i) {
      link_by_prim(attr_entries[i].first, i, kNoIndex, prim_path_to_index,
                   prim_entries.size(), attr_head, attr_tail, attr_next);
    }
  }
  if (!rels_sorted_by_prim) {
    for (size_t i = 0; i < rel_entries.size(); ++i) {
      link_by_prim(rel_entries[i].first, i, kNoIndex, prim_path_to_index,
                   prim_entries.size(), rel_head, rel_tail, rel_next);
    }
  }
  if (!variants_sorted_by_prim) {
    for (size_t i = 0; i < variant_sels.size(); ++i) {
      link_by_prim(variant_sels[i].prim_path, i, kNoIndex, prim_path_to_index,
                   prim_entries.size(), variant_head, variant_tail, variant_next);
    }
  }
  const auto t_index_end = Clock::now();

  layer.reserve(prim_entries.size());

  // Build hierarchy using depth-based stack management
  // Track open prim depth directly; values are emitted by `builder` and don't
  // require local path storage.
  size_t prim_depth = 0;
  size_t attr_cursor = 0;
  size_t rel_cursor = 0;
  size_t variant_cursor = 0;

  const auto t_emit_start = Clock::now();
  bool parallel_emit_done = false;
#if defined(TINYUSDZ_NEXT_PARALLEL_BUILD_STAGE)
  {
    int nt = options_.num_threads;
    if (nt <= 0) {
      nt = static_cast<int>(std::thread::hardware_concurrency());
      if (nt < 1) nt = 1;
      nt = std::min(nt, 8);
    }
    if (nt > 1 && prim_entries.size() >= 512) {
      struct EmitChunk {
        size_t begin = 0;
        size_t end = 0;
        std::unique_ptr<Layer> layer;
      };
      const size_t chunk_size =
          std::max<size_t>(256, prim_entries.size() / (static_cast<size_t>(nt) * 8));
      std::vector<EmitChunk> chunks;
      chunks.reserve(prim_entries.size() / chunk_size + 2);
      for (size_t b = 0; b < prim_entries.size(); b += chunk_size) {
        EmitChunk ck;
        ck.begin = b;
        ck.end = std::min(prim_entries.size(), b + chunk_size);
        chunks.push_back(std::move(ck));
      }

      // Decode one contiguous prim range into its own Layer fragment. The
      // per-prim body below is a copy of the retained serial emit body with
      // three mechanical differences: it targets the worker builder `wb`,
      // prims are begun/ended flat (hierarchy links are rebuilt after the
      // stitch with the same depth-stack rule the serial path uses), and the
      // sorted-mode cursors start from a range-local lower_bound (equivalent
      // to the serial cursor position when it reaches this range).
      auto emit_range = [&](EmitChunk& ck) {
        ThreadDecodeCtx decode_ctx(*reader_);
        ScopedThreadDecodeCtx scoped_ctx(&decode_ctx);
        ck.layer.reset(new Layer());
        ck.layer->reserve(ck.end - ck.begin);
        LayerBuilder wb(*ck.layer);

        const std::string_view range_first_path =
            prim_entries[ck.begin].full_path;
        size_t attr_cursor = 0;
        size_t rel_cursor = 0;
        size_t variant_cursor = 0;
        if (attrs_sorted_by_prim) {
          attr_cursor = static_cast<size_t>(
              std::lower_bound(attr_entries.begin(), attr_entries.end(),
                               range_first_path,
                               [](const std::pair<std::string_view, AttrInfo>& a,
                                  const std::string_view key) {
                                 return a.first < key;
                               }) -
              attr_entries.begin());
        }
        if (rels_sorted_by_prim) {
          rel_cursor = static_cast<size_t>(
              std::lower_bound(rel_entries.begin(), rel_entries.end(),
                               range_first_path,
                               [](const std::pair<std::string_view, RelInfo>& a,
                                  const std::string_view key) {
                                 return a.first < key;
                               }) -
              rel_entries.begin());
        }
        if (variants_sorted_by_prim) {
          variant_cursor = static_cast<size_t>(
              std::lower_bound(variant_sels.begin(), variant_sels.end(),
                               range_first_path,
                               [](const VariantSelectionEntry& a,
                                  const std::string_view key) {
                                 return a.prim_path < key;
                               }) -
              variant_sels.begin());
        }

        for (size_t prim_index = ck.begin; prim_index < ck.end; ++prim_index) {
          auto& entry = prim_entries[prim_index];
          if (!entry.full_path.empty()) {
            wb.begin_prim(entry.name, entry.type_name, entry.specifier,
                          entry.full_path);
          } else {
            wb.begin_prim(entry.name, entry.type_name, entry.specifier);
          }

    // wb.current() is valid immediately after begin_prim; capture it once
    // and guard the metadata derefs (every other current() site is guarded too).
    PrimSpec* ps = wb.current();

    // Extracts a token-list metadata field (written as a token array, with
    // single-token / string fallbacks for older encodings). Warns rather than
    // silently dropping a known arc field that fails to decode.
    auto append_token_list = [&](const ValueRep& rep,
                                 std::vector<std::string>& dst,
                                 const char* field_name,
                                 bool wrap_bare_paths = false) {
      Value v;
      if (!UnpackValue(rep, v)) {
        AddWarning(std::string("Composition arc field '") + field_name +
                   "' has unexpected encoding; dropped");
        return;
      }
      if (const std::vector<std::string>* arr = v.as_token_array()) {
        for (const auto& s : *arr) {
          if (wrap_bare_paths && !s.empty() && s[0] == '/') {
            std::string arc;
            arc.reserve(s.size() + 2);
            arc.push_back('<');
            arc.append(s);
            arc.push_back('>');
            dst.push_back(std::move(arc));
          } else {
            dst.push_back(s);
          }
        }
      } else if (const std::string* s = v.as_token()) {
        if (wrap_bare_paths && !s->empty() && (*s)[0] == '/') {
          std::string arc;
          arc.reserve(s->size() + 2);
          arc.push_back('<');
          arc.append(*s);
          arc.push_back('>');
          dst.push_back(std::move(arc));
        } else {
          dst.push_back(*s);
        }
      } else if (const std::string* s = v.as_string()) {
        if (wrap_bare_paths && !s->empty() && (*s)[0] == '/') {
          std::string arc;
          arc.reserve(s->size() + 2);
          arc.push_back('<');
          arc.append(*s);
          arc.push_back('>');
          dst.push_back(std::move(arc));
        } else {
          dst.push_back(*s);
        }
      } else {
        AddWarning(std::string("Composition arc field '") + field_name +
                   "' has unexpected encoding; dropped");
      }
    };

    // Phase 7 S5: decode a `<arc>_listOp` companion token[] (marker-delimited
    // prepend/append/delete/order sublists) into an ArcEdit.
    auto decode_arc_listop = [&](const ValueRep& rep, ArcEdit& e) {
      Value v;
      if (!UnpackValue(rep, v)) return;
      const std::vector<std::string>* arr = v.as_token_array();
      if (!arr) return;
      e.authored = true;
      e.is_explicit = false;
      std::vector<std::string>* cur = nullptr;
      for (const std::string& s : *arr) {
        if (s == "\x01P") cur = &e.prepended;
        else if (s == "\x01A") cur = &e.appended;
        else if (s == "\x01D") cur = &e.deleted;
        else if (s == "\x01O") cur = &e.ordered;
        else if (s == "\x01N") cur = nullptr;
        else if (cur) cur->push_back(s);
      }
    };

    // Add properties and extract composition arcs / metadata into PrimSpecMeta
    visit_fieldset_raw(
        entry.fieldset_index,
        [&](std::string_view field_name, const ValueRep& field_value) {
      if (field_name == "typeName" || field_name == "specifier") return true;

      // Reserved prim metadata stored inline in the prim spec (pxrUSD keeps
      // these in the prim's fieldset, not as separate property specs). Route
      // them to PrimSpecMeta so they do not leak in as phantom properties.
      if (ps) {
        if (field_name == "active") {
          bool is_active = false;
          if (read_field_bool(field_value, &is_active)) ps->meta().active = is_active;
          return true;
        }
        if (field_name == "hidden") {
          bool is_hidden = false;
          if (read_field_bool(field_value, &is_hidden)) ps->meta().hidden = is_hidden;
          return true;
        }
        if (field_name == "doc") {
          std::string_view text;
          if (read_field_token_or_string(field_value, &text))
            ps->meta().doc() = text;
          return true;
        }
      }

      // Composition arc + metadata fields: store in PrimSpecMeta, not as
      // regular properties. (Guarded by ps; if current() were null we simply
      // skip them rather than crash.)
      if (ps) {
        if (field_name == "apiSchemas") {
          // Applied API schemas. pxrUSD stores this as a TokenListOp; by the
          // time it reaches this field loop UnpackValue has flattened it to its
          // effective token list. Route it to PrimSpecMeta (the composed/flatten
          // form pxr writes as `apiSchemas = [...]` in the metadata block);
          // otherwise it leaks as a phantom `token[] apiSchemas` body property.
          append_token_list(field_value, ps->meta().apiSchemas(), "apiSchemas");
          return true;
        }
        if (field_name == "references") {
          append_token_list(field_value, ps->meta().references, "references");
          return true;
        }
        if (field_name == "payload") {
          append_token_list(field_value, ps->meta().payloads, "payload");
          return true;
        }
        if (field_name == "inherits") {
          append_token_list(field_value, ps->meta().inherits, "inherits", true);
          return true;
        }
        if (field_name == "specializes") {
          append_token_list(field_value, ps->meta().specializes,
                            "specializes", true);
          return true;
        }
        if (field_name == "references_listOp") {
          decode_arc_listop(field_value, ps->meta().ensure_arc_edits().references);
          return true;
        }
        if (field_name == "payload_listOp") {
          decode_arc_listop(field_value, ps->meta().ensure_arc_edits().payloads);
          return true;
        }
        if (field_name == "inherits_listOp") {
          decode_arc_listop(field_value, ps->meta().ensure_arc_edits().inherits);
          return true;
        }
        if (field_name == "specializes_listOp") {
          decode_arc_listop(field_value,
                            ps->meta().ensure_arc_edits().specializes);
          return true;
        }
        if (field_name == "variantSelection") {
          std::string_view text;
          if (read_field_token_or_string(field_value, &text))
            ps->meta().variantSelection = text;
          return true;
        }
        if (field_name == "comment") {
          std::string_view text;
          if (read_field_token_or_string(field_value, &text))
            ps->meta().comment() = text;
          return true;
        }
        if (field_name == "kind") {
          std::string_view text;
          if (read_field_token_or_string(field_value, &text))
            ps->meta().kind() = text;
          return true;
        }
        if (field_name == "displayName") {
          std::string_view text;
          if (read_field_token_or_string(field_value, &text))
            ps->meta().displayName() = text;
          return true;
        }
        if (field_name == "instanceable") {
          bool instanceable = false;
          if (read_field_bool(field_value, &instanceable))
            ps->meta().instanceable = instanceable;
          return true;
        }
        if (field_name == "customData" || field_name == "assetInfo" ||
            field_name == "sdrMetadata" || field_name == "clips") {
          // pxr-authored: a binary VtDictionary (already decoded to a Dictionary
          // Value). next-authored: USDA dict text in a String field.
          Value d;
          if (decode_dict_value(field_value, d) && d.is_dictionary()) {
            if (field_name == "customData")
              ps->meta().customData() = std::move(d);
            else if (field_name == "assetInfo")
              ps->meta().assetInfo() = std::move(d);
            else if (field_name == "sdrMetadata")
              ps->meta().sdrMetadata() = std::move(d);
            else
              ps->meta().clips() = std::move(d);
          }
          return true;
        }
        if (field_name == "variantSets") {
          // Writer stores the variant-set names only; reconstruct name entries.
          Value vs_v;
          if (!UnpackValue(field_value, vs_v)) {
            AddWarning(
                std::string("Composition arc field 'variantSets' has unexpected "
                            "encoding; dropped"));
          } else if (const std::vector<std::string>* arr =
                         vs_v.as_token_array()) {
            for (const auto& n : *arr) {
              ps->meta().variantSets().push_back(VariantSetData{n, "", {}});
            }
          } else if (const std::string* s = vs_v.as_token()) {
            ps->meta().variantSets().push_back(VariantSetData{*s, "", {}});
          } else if (const std::string* s = vs_v.as_string()) {
            ps->meta().variantSets().push_back(VariantSetData{*s, "", {}});
          } else {
            AddWarning(std::string("Composition arc field 'variantSets' has unexpected encoding; dropped"));
          }
          return true;
        }
      }
      // A loose sibling "variability" field cannot be re-associated with a
      // property here; consume it so it does not surface as a stray property.
      // Per-property variability is decoded above through Attribute/Relationship
      // specs and preserved as kFlagUniform.
        if (field_name == "variability") return true;

      // Reserved Sdf children-key ordering fields. pxrUSD stores prim/property
      // order in these (SdfChildrenKeys); USDA flatten output does NOT emit them
      // as body attributes -- order is implicit in the authored child/property
      // sequence. Composition derives child order from child_indices() directly
      // (see cache.cc ComposeInto), so consume these so they do not leak as
      // phantom `token[] primChildren`/`properties` attributes.
      if (field_name == "primChildren" || field_name == "properties" ||
          field_name == "propertyChildren" ||
          field_name == "variantChildren" ||
          field_name == "variantSetChildren") {
        return true;
      }

      // `variantSetNames` (the declared list of variant-set names) likewise must
      // not leak as a phantom `string[] variantSetNames` body property. The
      // compositor drives variants from the bracketed-holder specs + the
      // variantSelection (not from this list), and flatten output drops variant
      // metadata entirely (see cache.cc ComposeInto), so consume it.
      if (field_name == "variantSetNames") {
        return true;
      }

      uint16_t flags = 0;
      Value v;
      if (!UnpackValue(field_value, v)) {
        AddWarning(std::string("Failed to decode prim property '") +
                   std::string(field_name) + "'; dropped");
        return true;
      }
      wb.add_property(field_name, std::move(v), flags);
      return true;
    });

    // Attach separate Attribute / Relationship specs that belong to this prim.
    // The dedup guard keeps any inline field authoritative.
    if (ps) {
      auto emit_attr = [&](AttrInfo& ai) {
        const PropNameId prop_name_id = GetPropNameTable().intern(ai.name);
        if (!prop_name_id.is_valid()) return;
        if (ps->property(prop_name_id)) return;  // inline opinion wins
        uint16_t flags = 0;
        if (ai.uniform) flags |= PropSlot::kFlagUniform;
        if (ai.custom) flags |= PropSlot::kFlagCustom;
        if (ai.is_connection) flags |= PropSlot::kFlagConnection;
        // The writer gates timeSamples emission on the slot flag, so mark a
        // time-sampled attribute (the value lives in add_time_sample below).
        const bool has_time_samples =
            ai.cold && !ai.cold->time_samples.empty();
        if (has_time_samples) flags |= PropSlot::kFlagTimeSampled;
        const bool is_array =
            ai.type_name.size() >= 2 &&
            ai.type_name.compare(ai.type_name.size() - 2, 2, "[]") == 0;
        if (ai.has_default) {
          ps->add_property(prop_name_id, std::move(ai.default_value), flags);
        } else {
          // Connection-only / declared-only / timeSamples-only attribute:
          // register a typed slot with no authored default so it round-trips.
          if (is_array) flags |= PropSlot::kFlagArray;
          TypeId tid = TypeId::Invalid;
          if (is_array) {
            const std::string_view base(ai.type_name.data(),
                                        ai.type_name.size() - 2);
            tid = GetTypeIdFromName(base);
          } else {
            tid = GetTypeIdFromName(ai.type_name);
          }
          ps->add_property_slot(prop_name_id, tid, flags);
        }
        // Time samples (an attribute may have timeSamples with or without a
        // default).
        if (has_time_samples) {
          for (auto& ts : ai.cold->time_samples) {
            ps->add_time_sample(prop_name_id, ts.first, std::move(ts.second));
          }
        }
        if (!ai.type_name.empty()) {
          ps->set_property_type_name(prop_name_id, ai.type_name);
        }
        if (ai.cold) {
          for (auto& t : ai.cold->connection_targets) {
            ps->add_connection(prop_name_id, std::move(t));
          }
        }
        // Per-property metadata.
        if (ai.has_interpolation || ai.has_color_space || ai.has_element_size ||
            (ai.cold && ai.cold->custom_data.is_dictionary())) {
          PropMeta& pm = ps->ensure_property_meta(prop_name_id);
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
          if (ai.cold && ai.cold->custom_data.is_dictionary()) {
            pm.customData = std::move(ai.cold->custom_data);
            pm.authored |= PropMeta::kCustomData;
          }
        }
      };

      if (attrs_sorted_by_prim) {
        while (attr_cursor < attr_entries.size() &&
               attr_entries[attr_cursor].first < entry.full_path) {
          ++attr_cursor;
        }
        size_t i = attr_cursor;
        while (i < attr_entries.size() &&
               attr_entries[i].first == entry.full_path) {
          emit_attr(attr_entries[i].second);
          ++i;
        }
        attr_cursor = i;
      } else {
        for (size_t node_index = attr_head[prim_index];
             node_index != kNoIndex; node_index = attr_next[node_index]) {
          emit_attr(attr_entries[node_index].second);
        }
      }
    }

    if (ps) {
      auto emit_rel = [&](RelInfo& ri) {
        // A target-less relationship is recorded with a single empty Path
        // marker (the writer emits no targetPaths for it); relationships with
        // targets push one Path per target.
        if (ri.targets.empty()) {
          ps->add_relationship(ri.name, Path());
        } else {
          for (auto& t : ri.targets) ps->add_relationship(ri.name, std::move(t));
        }
      };

      if (rels_sorted_by_prim) {
        while (rel_cursor < rel_entries.size() &&
               rel_entries[rel_cursor].first < entry.full_path) {
          ++rel_cursor;
        }
        size_t i = rel_cursor;
        while (i < rel_entries.size() && rel_entries[i].first == entry.full_path) {
          emit_rel(rel_entries[i].second);
          ++i;
        }
        rel_cursor = i;
      } else {
        for (size_t node_index = rel_head[prim_index];
             node_index != kNoIndex; node_index = rel_next[node_index]) {
          emit_rel(rel_entries[node_index].second);
        }
      }
    }

    // Variant sets: attach a VariantSetData{name, selected} per selected set.
    // The variant CONTENT (properties + child prims) lives in the layer's
    // bracketed holder prims, which the compositor reads on selection and the
    // writer re-emits; the model only carries the selection here.
    if (ps) {
      bool has_variant_sel = false;
      auto emit_variant_sel = [&](const VariantSelectionEntry& sel) {
        VariantSetData vsd;
        vsd.name = sel.set_name;
        vsd.selected = sel.selection;
        ps->meta().variantSets().push_back(std::move(vsd));

        if (!has_variant_sel) {
          has_variant_sel = true;
          std::string variant_selection;
          variant_selection.reserve(sel.set_name.size() + 1 + sel.selection.size());
          variant_selection.append(sel.set_name);
          variant_selection.push_back('=');
          variant_selection.append(sel.selection);
          ps->meta().variantSelection = std::move(variant_selection);
        }
      };

      if (variants_sorted_by_prim) {
        while (variant_cursor < variant_sels.size() &&
               variant_sels[variant_cursor].prim_path < entry.full_path) {
          ++variant_cursor;
        }
        size_t i = variant_cursor;
        while (i < variant_sels.size() &&
               variant_sels[i].prim_path == entry.full_path) {
          emit_variant_sel(variant_sels[i]);
          ++i;
        }
        variant_cursor = i;
      } else {
        for (size_t node_index = variant_head[prim_index];
             node_index != kNoIndex; node_index = variant_next[node_index]) {
          emit_variant_sel(variant_sels[node_index]);
        }
      }
    }
          wb.end_prim();
        }
      };

      std::mutex done_mu;
      std::condition_variable done_cv;
      size_t remaining = chunks.size();
      for (EmitChunk& ck : chunks) {
        EmitChunk* ckp = &ck;
        auto task = [&, ckp]() {
          emit_range(*ckp);
          std::lock_guard<std::mutex> lock(done_mu);
          if (--remaining == 0) done_cv.notify_all();
        };
        if (!SubmitPoolTask(options_.num_threads, task)) {
          task();  // pool unavailable: run inline (still correct)
        }
      }
      {
        std::unique_lock<std::mutex> lock(done_mu);
        done_cv.wait(lock, [&]() { return remaining == 0; });
      }

      // Stitch fragments in range order (final prim order == serial order),
      // then rebuild hierarchy links with the exact serial depth-stack rule.
      for (EmitChunk& ck : chunks) {
        if (ck.layer) layer.adopt_fragment(std::move(*ck.layer));
      }
      std::vector<uint32_t> link_stack;
      const size_t total_prims = layer.prim_count();
      for (uint32_t i = 0; i < total_prims; ++i) {
        const std::string& ppath = layer.prim(i)->path().str();
        const size_t depth =
            static_cast<size_t>(std::count(ppath.begin(), ppath.end(), '/'));
        const size_t want = depth > 0 ? depth - 1 : 0;
        if (link_stack.size() > want) link_stack.resize(want);
        if (link_stack.empty()) {
          layer.add_root(static_cast<uint32_t>(i));
        } else {
          layer.set_parent(static_cast<uint32_t>(i), link_stack.back());
        }
        link_stack.push_back(static_cast<uint32_t>(i));
      }
      parallel_emit_done = true;
    }
  }
#endif  // TINYUSDZ_NEXT_PARALLEL_BUILD_STAGE

  if (!parallel_emit_done) {
  for (size_t prim_index = 0; prim_index < prim_entries.size(); ++prim_index) {
    auto& entry = prim_entries[prim_index];
    // Compute depth of this prim (number of '/' in path)
    size_t depth = std::count(entry.full_path.begin(), entry.full_path.end(), '/');

    // Pop stack until we're at the correct parent level.
    while (prim_depth > (depth > 0 ? depth - 1 : 0)) {
      builder.end_prim();
      --prim_depth;
    }

    // Begin this prim
    if (!entry.full_path.empty()) {
      builder.begin_prim(entry.name, entry.type_name, entry.specifier,
                         entry.full_path);
    } else {
      builder.begin_prim(entry.name, entry.type_name, entry.specifier);
    }
    ++prim_depth;

    // builder.current() is valid immediately after begin_prim; capture it once
    // and guard the metadata derefs (every other current() site is guarded too).
    PrimSpec* ps = builder.current();

    // Extracts a token-list metadata field (written as a token array, with
    // single-token / string fallbacks for older encodings). Warns rather than
    // silently dropping a known arc field that fails to decode.
    auto append_token_list = [&](const ValueRep& rep,
                                 std::vector<std::string>& dst,
                                 const char* field_name,
                                 bool wrap_bare_paths = false) {
      Value v;
      if (!UnpackValue(rep, v)) {
        AddWarning(std::string("Composition arc field '") + field_name +
                   "' has unexpected encoding; dropped");
        return;
      }
      if (const std::vector<std::string>* arr = v.as_token_array()) {
        for (const auto& s : *arr) {
          if (wrap_bare_paths && !s.empty() && s[0] == '/') {
            std::string arc;
            arc.reserve(s.size() + 2);
            arc.push_back('<');
            arc.append(s);
            arc.push_back('>');
            dst.push_back(std::move(arc));
          } else {
            dst.push_back(s);
          }
        }
      } else if (const std::string* s = v.as_token()) {
        if (wrap_bare_paths && !s->empty() && (*s)[0] == '/') {
          std::string arc;
          arc.reserve(s->size() + 2);
          arc.push_back('<');
          arc.append(*s);
          arc.push_back('>');
          dst.push_back(std::move(arc));
        } else {
          dst.push_back(*s);
        }
      } else if (const std::string* s = v.as_string()) {
        if (wrap_bare_paths && !s->empty() && (*s)[0] == '/') {
          std::string arc;
          arc.reserve(s->size() + 2);
          arc.push_back('<');
          arc.append(*s);
          arc.push_back('>');
          dst.push_back(std::move(arc));
        } else {
          dst.push_back(*s);
        }
      } else {
        AddWarning(std::string("Composition arc field '") + field_name +
                   "' has unexpected encoding; dropped");
      }
    };

    // Phase 7 S5: decode a `<arc>_listOp` companion token[] (marker-delimited
    // prepend/append/delete/order sublists) into an ArcEdit.
    auto decode_arc_listop = [&](const ValueRep& rep, ArcEdit& e) {
      Value v;
      if (!UnpackValue(rep, v)) return;
      const std::vector<std::string>* arr = v.as_token_array();
      if (!arr) return;
      e.authored = true;
      e.is_explicit = false;
      std::vector<std::string>* cur = nullptr;
      for (const std::string& s : *arr) {
        if (s == "\x01P") cur = &e.prepended;
        else if (s == "\x01A") cur = &e.appended;
        else if (s == "\x01D") cur = &e.deleted;
        else if (s == "\x01O") cur = &e.ordered;
        else if (s == "\x01N") cur = nullptr;
        else if (cur) cur->push_back(s);
      }
    };

    // Add properties and extract composition arcs / metadata into PrimSpecMeta
    visit_fieldset_raw(
        entry.fieldset_index,
        [&](std::string_view field_name, const ValueRep& field_value) {
      if (field_name == "typeName" || field_name == "specifier") return true;

      // Reserved prim metadata stored inline in the prim spec (pxrUSD keeps
      // these in the prim's fieldset, not as separate property specs). Route
      // them to PrimSpecMeta so they do not leak in as phantom properties.
      if (ps) {
        if (field_name == "active") {
          bool is_active = false;
          if (read_field_bool(field_value, &is_active)) ps->meta().active = is_active;
          return true;
        }
        if (field_name == "hidden") {
          bool is_hidden = false;
          if (read_field_bool(field_value, &is_hidden)) ps->meta().hidden = is_hidden;
          return true;
        }
        if (field_name == "doc") {
          std::string_view text;
          if (read_field_token_or_string(field_value, &text))
            ps->meta().doc() = text;
          return true;
        }
      }

      // Composition arc + metadata fields: store in PrimSpecMeta, not as
      // regular properties. (Guarded by ps; if current() were null we simply
      // skip them rather than crash.)
      if (ps) {
        if (field_name == "apiSchemas") {
          // Applied API schemas. pxrUSD stores this as a TokenListOp; by the
          // time it reaches this field loop UnpackValue has flattened it to its
          // effective token list. Route it to PrimSpecMeta (the composed/flatten
          // form pxr writes as `apiSchemas = [...]` in the metadata block);
          // otherwise it leaks as a phantom `token[] apiSchemas` body property.
          append_token_list(field_value, ps->meta().apiSchemas(), "apiSchemas");
          return true;
        }
        if (field_name == "references") {
          append_token_list(field_value, ps->meta().references, "references");
          return true;
        }
        if (field_name == "payload") {
          append_token_list(field_value, ps->meta().payloads, "payload");
          return true;
        }
        if (field_name == "inherits") {
          append_token_list(field_value, ps->meta().inherits, "inherits", true);
          return true;
        }
        if (field_name == "specializes") {
          append_token_list(field_value, ps->meta().specializes,
                            "specializes", true);
          return true;
        }
        if (field_name == "references_listOp") {
          decode_arc_listop(field_value, ps->meta().ensure_arc_edits().references);
          return true;
        }
        if (field_name == "payload_listOp") {
          decode_arc_listop(field_value, ps->meta().ensure_arc_edits().payloads);
          return true;
        }
        if (field_name == "inherits_listOp") {
          decode_arc_listop(field_value, ps->meta().ensure_arc_edits().inherits);
          return true;
        }
        if (field_name == "specializes_listOp") {
          decode_arc_listop(field_value,
                            ps->meta().ensure_arc_edits().specializes);
          return true;
        }
        if (field_name == "variantSelection") {
          std::string_view text;
          if (read_field_token_or_string(field_value, &text))
            ps->meta().variantSelection = text;
          return true;
        }
        if (field_name == "comment") {
          std::string_view text;
          if (read_field_token_or_string(field_value, &text))
            ps->meta().comment() = text;
          return true;
        }
        if (field_name == "kind") {
          std::string_view text;
          if (read_field_token_or_string(field_value, &text))
            ps->meta().kind() = text;
          return true;
        }
        if (field_name == "displayName") {
          std::string_view text;
          if (read_field_token_or_string(field_value, &text))
            ps->meta().displayName() = text;
          return true;
        }
        if (field_name == "instanceable") {
          bool instanceable = false;
          if (read_field_bool(field_value, &instanceable))
            ps->meta().instanceable = instanceable;
          return true;
        }
        if (field_name == "customData" || field_name == "assetInfo" ||
            field_name == "sdrMetadata" || field_name == "clips") {
          // pxr-authored: a binary VtDictionary (already decoded to a Dictionary
          // Value). next-authored: USDA dict text in a String field.
          Value d;
          if (decode_dict_value(field_value, d) && d.is_dictionary()) {
            if (field_name == "customData")
              ps->meta().customData() = std::move(d);
            else if (field_name == "assetInfo")
              ps->meta().assetInfo() = std::move(d);
            else if (field_name == "sdrMetadata")
              ps->meta().sdrMetadata() = std::move(d);
            else
              ps->meta().clips() = std::move(d);
          }
          return true;
        }
        if (field_name == "variantSets") {
          // Writer stores the variant-set names only; reconstruct name entries.
          Value vs_v;
          if (!UnpackValue(field_value, vs_v)) {
            AddWarning(
                std::string("Composition arc field 'variantSets' has unexpected "
                            "encoding; dropped"));
          } else if (const std::vector<std::string>* arr =
                         vs_v.as_token_array()) {
            for (const auto& n : *arr) {
              ps->meta().variantSets().push_back(VariantSetData{n, "", {}});
            }
          } else if (const std::string* s = vs_v.as_token()) {
            ps->meta().variantSets().push_back(VariantSetData{*s, "", {}});
          } else if (const std::string* s = vs_v.as_string()) {
            ps->meta().variantSets().push_back(VariantSetData{*s, "", {}});
          } else {
            AddWarning(std::string("Composition arc field 'variantSets' has unexpected encoding; dropped"));
          }
          return true;
        }
      }
      // A loose sibling "variability" field cannot be re-associated with a
      // property here; consume it so it does not surface as a stray property.
      // Per-property variability is decoded above through Attribute/Relationship
      // specs and preserved as kFlagUniform.
        if (field_name == "variability") return true;

      // Reserved Sdf children-key ordering fields. pxrUSD stores prim/property
      // order in these (SdfChildrenKeys); USDA flatten output does NOT emit them
      // as body attributes -- order is implicit in the authored child/property
      // sequence. Composition derives child order from child_indices() directly
      // (see cache.cc ComposeInto), so consume these so they do not leak as
      // phantom `token[] primChildren`/`properties` attributes.
      if (field_name == "primChildren" || field_name == "properties" ||
          field_name == "propertyChildren" ||
          field_name == "variantChildren" ||
          field_name == "variantSetChildren") {
        return true;
      }

      // `variantSetNames` (the declared list of variant-set names) likewise must
      // not leak as a phantom `string[] variantSetNames` body property. The
      // compositor drives variants from the bracketed-holder specs + the
      // variantSelection (not from this list), and flatten output drops variant
      // metadata entirely (see cache.cc ComposeInto), so consume it.
      if (field_name == "variantSetNames") {
        return true;
      }

      uint16_t flags = 0;
      Value v;
      if (!UnpackValue(field_value, v)) {
        AddWarning(std::string("Failed to decode prim property '") +
                   std::string(field_name) + "'; dropped");
        return true;
      }
      builder.add_property(field_name, std::move(v), flags);
      return true;
    });

    // Attach separate Attribute / Relationship specs that belong to this prim.
    // The dedup guard keeps any inline field authoritative.
    if (ps) {
      auto emit_attr = [&](AttrInfo& ai) {
        const PropNameId prop_name_id = GetPropNameTable().intern(ai.name);
        if (!prop_name_id.is_valid()) return;
        if (ps->property(prop_name_id)) return;  // inline opinion wins
        uint16_t flags = 0;
        if (ai.uniform) flags |= PropSlot::kFlagUniform;
        if (ai.custom) flags |= PropSlot::kFlagCustom;
        if (ai.is_connection) flags |= PropSlot::kFlagConnection;
        // The writer gates timeSamples emission on the slot flag, so mark a
        // time-sampled attribute (the value lives in add_time_sample below).
        const bool has_time_samples =
            ai.cold && !ai.cold->time_samples.empty();
        if (has_time_samples) flags |= PropSlot::kFlagTimeSampled;
        const bool is_array =
            ai.type_name.size() >= 2 &&
            ai.type_name.compare(ai.type_name.size() - 2, 2, "[]") == 0;
        if (ai.has_default) {
          ps->add_property(prop_name_id, std::move(ai.default_value), flags);
        } else {
          // Connection-only / declared-only / timeSamples-only attribute:
          // register a typed slot with no authored default so it round-trips.
          if (is_array) flags |= PropSlot::kFlagArray;
          TypeId tid = TypeId::Invalid;
          if (is_array) {
            const std::string_view base(ai.type_name.data(),
                                        ai.type_name.size() - 2);
            tid = GetTypeIdFromName(base);
          } else {
            tid = GetTypeIdFromName(ai.type_name);
          }
          ps->add_property_slot(prop_name_id, tid, flags);
        }
        // Time samples (an attribute may have timeSamples with or without a
        // default).
        if (has_time_samples) {
          for (auto& ts : ai.cold->time_samples) {
            ps->add_time_sample(prop_name_id, ts.first, std::move(ts.second));
          }
        }
        if (!ai.type_name.empty()) {
          ps->set_property_type_name(prop_name_id, ai.type_name);
        }
        if (ai.cold) {
          for (auto& t : ai.cold->connection_targets) {
            ps->add_connection(prop_name_id, std::move(t));
          }
        }
        // Per-property metadata.
        if (ai.has_interpolation || ai.has_color_space || ai.has_element_size ||
            (ai.cold && ai.cold->custom_data.is_dictionary())) {
          PropMeta& pm = ps->ensure_property_meta(prop_name_id);
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
          if (ai.cold && ai.cold->custom_data.is_dictionary()) {
            pm.customData = std::move(ai.cold->custom_data);
            pm.authored |= PropMeta::kCustomData;
          }
        }
      };

      if (attrs_sorted_by_prim) {
        while (attr_cursor < attr_entries.size() &&
               attr_entries[attr_cursor].first < entry.full_path) {
          ++attr_cursor;
        }
        size_t i = attr_cursor;
        while (i < attr_entries.size() &&
               attr_entries[i].first == entry.full_path) {
          emit_attr(attr_entries[i].second);
          ++i;
        }
        attr_cursor = i;
      } else {
        for (size_t node_index = attr_head[prim_index];
             node_index != kNoIndex; node_index = attr_next[node_index]) {
          emit_attr(attr_entries[node_index].second);
        }
      }
    }

    if (ps) {
      auto emit_rel = [&](RelInfo& ri) {
        // A target-less relationship is recorded with a single empty Path
        // marker (the writer emits no targetPaths for it); relationships with
        // targets push one Path per target.
        if (ri.targets.empty()) {
          ps->add_relationship(ri.name, Path());
        } else {
          for (auto& t : ri.targets) ps->add_relationship(ri.name, std::move(t));
        }
      };

      if (rels_sorted_by_prim) {
        while (rel_cursor < rel_entries.size() &&
               rel_entries[rel_cursor].first < entry.full_path) {
          ++rel_cursor;
        }
        size_t i = rel_cursor;
        while (i < rel_entries.size() && rel_entries[i].first == entry.full_path) {
          emit_rel(rel_entries[i].second);
          ++i;
        }
        rel_cursor = i;
      } else {
        for (size_t node_index = rel_head[prim_index];
             node_index != kNoIndex; node_index = rel_next[node_index]) {
          emit_rel(rel_entries[node_index].second);
        }
      }
    }

    // Variant sets: attach a VariantSetData{name, selected} per selected set.
    // The variant CONTENT (properties + child prims) lives in the layer's
    // bracketed holder prims, which the compositor reads on selection and the
    // writer re-emits; the model only carries the selection here.
    if (ps) {
      bool has_variant_sel = false;
      auto emit_variant_sel = [&](const VariantSelectionEntry& sel) {
        VariantSetData vsd;
        vsd.name = sel.set_name;
        vsd.selected = sel.selection;
        ps->meta().variantSets().push_back(std::move(vsd));

        if (!has_variant_sel) {
          has_variant_sel = true;
          std::string variant_selection;
          variant_selection.reserve(sel.set_name.size() + 1 + sel.selection.size());
          variant_selection.append(sel.set_name);
          variant_selection.push_back('=');
          variant_selection.append(sel.selection);
          ps->meta().variantSelection = std::move(variant_selection);
        }
      };

      if (variants_sorted_by_prim) {
        while (variant_cursor < variant_sels.size() &&
               variant_sels[variant_cursor].prim_path < entry.full_path) {
          ++variant_cursor;
        }
        size_t i = variant_cursor;
        while (i < variant_sels.size() &&
               variant_sels[i].prim_path == entry.full_path) {
          emit_variant_sel(variant_sels[i]);
          ++i;
        }
        variant_cursor = i;
      } else {
        for (size_t node_index = variant_head[prim_index];
             node_index != kNoIndex; node_index = variant_next[node_index]) {
          emit_variant_sel(variant_sels[node_index]);
        }
      }
    }
  }

  // Close remaining prims
  while (prim_depth > 0) {
    builder.end_prim();
    --prim_depth;
  }
  }  // !parallel_emit_done (retained serial emit)
  const auto t_emit_end = Clock::now();

  // Finalize
  const auto t_finalize_start = Clock::now();
  if (options_.finalize_stage) {
    builder.finalize();
  }
  const auto t_finalize_end = Clock::now();

  // Create stage from layer
  result_.stage.SetRootLayer(std::move(layer));
  if (timing) {
    const auto t_build_end = Clock::now();
    std::fprintf(stderr,
                 "[next_crate_read_build_stage] collect=%.1fms sort=%.1fms "
                 "index=%.1fms emit=%.1fms finalize=%.1fms total=%.1fms "
                 "prims=%zu attrs=%zu rels=%zu variants=%zu sorted=%d/%d/%d\n",
                 ms(t_collect_end - t_collect_start),
                 ms(t_sort_end - t_sort_start), ms(t_index_end - t_index_start),
                 ms(t_emit_end - t_emit_start),
                 ms(t_finalize_end - t_finalize_start),
                 ms(t_build_end - t_build_start), prim_entries.size(),
                 attr_entries.size(), rel_entries.size(), variant_sels.size(),
                 attrs_sorted_by_prim ? 1 : 0, rels_sorted_by_prim ? 1 : 0,
                 variants_sorted_by_prim ? 1 : 0);
  }

  return true;
}


}  // namespace next
}  // namespace tinyusdz
